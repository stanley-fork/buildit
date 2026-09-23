#include "blocks/declaration_utils.h"
#include "blocks/block_visitor.h"

namespace block {
namespace {

class declaration_finder : public block_visitor {
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

} // namespace

bool is_splittable(decl_stmt::Ptr decl) {
	type::Ptr type = decl->decl_var->var_type;
	return !decl->is_typedef && !decl->is_extern && !decl->is_static && !type->is_const &&
	       !isa<reference_type>(type) && !isa<array_type>(type) && !isa<function_type>(type);
}

std::vector<decl_stmt::Ptr> find_declarations(stmt::Ptr scope) {
	declaration_finder finder;
	scope->accept(&finder);
	return finder.declarations;
}

void split_declarations(stmt::Ptr scope, std::vector<decl_stmt::Ptr> &declarations) {
	declaration_splitter splitter(declarations);
	scope->accept(&splitter);
}

} // namespace block
