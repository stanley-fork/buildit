#include "blocks/loop_normalizer.h"
#include "blocks/block_visitor.h"
#include "blocks/declaration_utils.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace block {
namespace {

struct path_entry {
	stmt_block::Ptr block;
	unsigned int index;
};

class statement_path_finder : public block_visitor {
	stmt::Ptr target;
	std::vector<path_entry> current_path;

public:
	using block_visitor::visit;
	std::vector<path_entry> result;
	explicit statement_path_finder(stmt::Ptr target) : target(target) {}

	void visit(stmt_block::Ptr block) override {
		if (!result.empty())
			return;
		for (unsigned int i = 0; i < block->stmts.size(); i++) {
			current_path.push_back({block, i});
			if (block->stmts[i] == target) {
				result = current_path;
				return;
			}
			block->stmts[i]->accept(this);
			if (!result.empty())
				return;
			current_path.pop_back();
		}
	}
};

static std::vector<path_entry> find_statement_path(stmt::Ptr ast, stmt::Ptr target) {
	statement_path_finder finder(target);
	ast->accept(&finder);
	return finder.result;
}

class label_collector : public block_visitor {
public:
	using block_visitor::visit;
	std::vector<label_stmt::Ptr> labels;
	void visit(label_stmt::Ptr stmt) override {
		labels.push_back(stmt);
	}
};

class target_goto_collector : public block_visitor {
public:
	using block_visitor::visit;
	label::Ptr target;
	std::vector<goto_stmt::Ptr> gotos;
	void visit(goto_stmt::Ptr stmt) override {
		if (stmt->label1 == target)
			gotos.push_back(stmt);
	}
};

static std::vector<goto_stmt::Ptr> find_gotos(stmt::Ptr scope, label::Ptr target) {
	target_goto_collector collector;
	collector.target = target;
	scope->accept(&collector);
	return collector.gotos;
}

struct normalization_candidate {
	label_stmt::Ptr target;
	std::vector<path_entry> path;
	unsigned int common_parent_index = 0;
	std::vector<goto_stmt::Ptr> external_gotos;
};

static normalization_candidate find_candidate(stmt::Ptr ast, const std::unordered_set<label::Ptr> &skipped) {
	label_collector collector;
	ast->accept(&collector);
	for (auto target : collector.labels) {
		if (skipped.find(target->label1) != skipped.end())
			continue;
		auto path = find_statement_path(ast, target);
		assert(!path.empty());
		auto all_gotos = find_gotos(ast, target->label1);
		if (all_gotos.empty())
			continue;
		auto local_gotos = find_gotos(path.back().block, target->label1);
		if (local_gotos.size() == all_gotos.size())
			continue;

		unsigned int common_parent_index = path.size() - 1;
		while (common_parent_index > 0) {
			common_parent_index--;
			if (find_gotos(path[common_parent_index].block, target->label1).size() == all_gotos.size())
				break;
		}

		std::vector<goto_stmt::Ptr> external_gotos;
		for (auto jump : all_gotos) {
			if (std::find(local_gotos.begin(), local_gotos.end(), jump) == local_gotos.end())
				external_gotos.push_back(jump);
		}
		normalization_candidate result;
		result.target = target;
		result.path = path;
		result.common_parent_index = common_parent_index;
		result.external_gotos = external_gotos;
		return result;
	}
	return {};
}

class declaration_collector : public block_visitor {
public:
	using block_visitor::visit;
	std::vector<decl_stmt::Ptr> declarations;
	void visit(decl_stmt::Ptr declaration) override {
		for (auto existing : declarations)
			if (existing->decl_var == declaration->decl_var)
				return;
		declarations.push_back(declaration);
	}
};

static bool contains_var(const std::vector<decl_stmt::Ptr> &declarations, var::Ptr var) {
	for (auto declaration : declarations)
		if (declaration->decl_var == var)
			return true;
	return false;
}

class declaration_splitter : public block_visitor {
	std::vector<decl_stmt::Ptr> &declarations;

public:
	using block_visitor::visit;
	explicit declaration_splitter(std::vector<decl_stmt::Ptr> &declarations) : declarations(declarations) {}

	void visit(stmt_block::Ptr block) override {
		for (unsigned int i = 0; i < block->stmts.size();) {
			if (!isa<decl_stmt>(block->stmts[i]) ||
			    !contains_var(declarations, to<decl_stmt>(block->stmts[i])->decl_var)) {
				block->stmts[i]->accept(this);
				i++;
				continue;
			}
			auto declaration = to<decl_stmt>(block->stmts[i]);
			if (!declaration->init_expr) {
				block->stmts.erase(block->stmts.begin() + i);
				continue;
			}
			auto lhs = std::make_shared<var_expr>();
			lhs->static_offset = declaration->static_offset;
			lhs->var1 = declaration->decl_var;
			auto assignment = std::make_shared<assign_expr>();
			assignment->static_offset = declaration->static_offset;
			assignment->var1 = lhs;
			assignment->expr1 = declaration->init_expr;
			auto replacement = std::make_shared<expr_stmt>();
			replacement->static_offset = declaration->static_offset;
			replacement->metadata_map = declaration->metadata_map;
			replacement->annotation = declaration->annotation;
			replacement->expr1 = assignment;
			block->stmts[i] = replacement;
			i++;
		}
	}
};

class copied_label_rewriter : public block_visitor {
	unsigned int &label_counter;

public:
	using block_visitor::visit;
	std::unordered_map<label::Ptr, label::Ptr> replacements;
	explicit copied_label_rewriter(unsigned int &label_counter) : label_counter(label_counter) {}

	label::Ptr replacement_for(label::Ptr original) {
		auto found = replacements.find(original);
		if (found != replacements.end())
			return found->second;
		auto replacement = std::make_shared<label>();
		replacement->label_name = "normalized_label" + std::to_string(label_counter++);
		replacements[original] = replacement;
		return replacement;
	}

	void visit(label_stmt::Ptr stmt) override {
		stmt->label1 = replacement_for(stmt->label1);
	}
	void visit(goto_stmt::Ptr stmt) override {
		auto found = replacements.find(stmt->label1);
		if (found != replacements.end())
			stmt->label1 = found->second;
	}
};

/*
 * Normalize loop backedges whose targets are in sibling scopes.
 *
 * A loop backedge normally targets a label in the goto's current scope or one
 * of its parent scopes.  The first-stage compiler, however, may restructure
 * control flow so that the label and goto end up in separate sibling scopes.
 * Tail-merging a loop's first iteration with a preceding conditional is one
 * example; loop rotation, branch folding, or explicitly staged control flow
 * can produce the same general shape.  Such a backedge is valid in the raw AST
 * but violates the loop finder's assumption that it can find the target label
 * while walking the goto's containing statement blocks.
 *
 * For example, the input may contain:
 *
 *     if (first) {              if (first) {
 *       L:                        L:
 *       body;                     body;
 *     }                         }
 *     if (again) {       ==>    R:
 *       goto L;                  if (again) {
 *     }                            L_copy:
 *                                  body;
 *                                  goto R;
 *                                }
 *
 * The pass finds the common parent of the label and its backedges, copies the
 * continuation from the label to each out-of-scope goto, and adds a backedge
 * to a new label in that common parent.  Nested scopes are handled by copying
 * each suffix along the path from the label out to the common parent, stopping
 * once a goto or return terminates the continuation.  Any copied declarations
 * are hoisted to the common parent; only their initializations remain on the
 * original and copied paths.
 *
 * See sample67 and BuildIt-lang/buildit#119 for concrete regression cases.
 */
class normalizer {
	stmt::Ptr ast;
	unsigned int label_counter = 0;
	std::unordered_set<label::Ptr> skipped;

	label::Ptr create_label() {
		auto result = std::make_shared<label>();
		result->label_name = "normalized_label" + std::to_string(label_counter++);
		return result;
	}

	bool normalize(normalization_candidate candidate) {
		auto common_parent = candidate.path[candidate.common_parent_index].block;
		auto owner = common_parent->stmts[candidate.path[candidate.common_parent_index].index];
		std::vector<stmt::Ptr> source;
		bool falls_through = true;
		for (unsigned int i = candidate.path.size();
		     falls_through && i-- > candidate.common_parent_index + 1;) {
			auto entry = candidate.path[i];
			for (unsigned int j = entry.index + 1; j < entry.block->stmts.size(); j++) {
				source.push_back(entry.block->stmts[j]);
				if (isa<goto_stmt>(entry.block->stmts[j]) || isa<return_stmt>(entry.block->stmts[j])) {
					falls_through = false;
					break;
				}
			}
		}

		auto template_block = std::make_shared<stmt_block>();
		for (auto statement : source)
			template_block->stmts.push_back(clone(statement));
		auto source_block = std::make_shared<stmt_block>();
		source_block->stmts = source;
		declaration_collector declarations;
		source_block->accept(&declarations);
		for (auto declaration : declarations.declarations)
			if (!is_splittable(declaration))
				return false;

		std::vector<stmt::Ptr> hoisted;
		for (auto declaration : declarations.declarations) {
			auto copy = clone(declaration);
			copy->init_expr = nullptr;
			copy->annotation.clear();
			hoisted.push_back(copy);
		}
		declaration_splitter splitter(declarations.declarations);
		ast->accept(&splitter);
		template_block->accept(&splitter);

		auto owner_position = std::find(common_parent->stmts.begin(), common_parent->stmts.end(), owner);
		assert(owner_position != common_parent->stmts.end());
		owner_position = common_parent->stmts.insert(owner_position, hoisted.begin(), hoisted.end());
		owner_position += hoisted.size();

		auto deferred_target = create_label();
		auto deferred_target_stmt = std::make_shared<label_stmt>();
		deferred_target_stmt->label1 = deferred_target;
		common_parent->stmts.insert(owner_position + 1, deferred_target_stmt);

		for (auto jump : candidate.external_gotos) {
			auto jump_path = find_statement_path(ast, jump);
			assert(!jump_path.empty());
			auto jump_parent = jump_path.back().block;
			auto jump_index = jump_path.back().index;
			auto copied_entry = create_label();
			copied_label_rewriter rewriter(label_counter);
			rewriter.replacements[candidate.target->label1] = copied_entry;
			std::vector<stmt::Ptr> replacement;
			auto copied_entry_stmt = std::make_shared<label_stmt>();
			copied_entry_stmt->label1 = copied_entry;
			replacement.push_back(copied_entry_stmt);
			for (auto statement : template_block->stmts) {
				auto copied_statement = clone(statement);
				copied_statement->accept(&rewriter);
				replacement.push_back(copied_statement);
			}
			if (falls_through) {
				auto deferred_jump = std::make_shared<goto_stmt>();
				deferred_jump->label1 = deferred_target;
				replacement.push_back(deferred_jump);
			}
			auto position = jump_parent->stmts.begin() + jump_index;
			position = jump_parent->stmts.erase(position);
			jump_parent->stmts.insert(position, replacement.begin(), replacement.end());
		}
		return true;
	}

public:
	explicit normalizer(stmt::Ptr ast) : ast(ast) {}
	void run() {
		while (true) {
			auto candidate = find_candidate(ast, skipped);
			if (!candidate.target)
				return;
			if (!normalize(candidate))
				skipped.insert(candidate.target->label1);
		}
	}
};

} // namespace

void normalize_loop_backedges(stmt::Ptr ast) {
	normalizer pass(ast);
	pass.run();
}

} // namespace block
