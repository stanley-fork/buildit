// Include the headers
#include "blocks/c_code_generator.h"
#include "builder/dyn_var.h"
#include "builder/static_var.h"
#include <iostream>

// Include the BuildIt types
using builder::dyn_var;
using builder::static_var;

static dyn_var<int> simple_loop(dyn_var<int> n) {
	dyn_var<int> i(0);
	while (i < n) {
		dyn_var<int> one(1);
		i = i + one;
	}
	return i;
}

static dyn_var<int> nested_loops(dyn_var<int> n, dyn_var<int> m) {
	dyn_var<int> i(0);
	dyn_var<int> total(0);
	while (i < n) {
		dyn_var<int> j(0);
		while (j < m) {
			dyn_var<int> one(1);
			total = total + one;
			j = j + one;
		}
		i = i + 1;
	}
	return total;
}

static dyn_var<int> return_in_loop(dyn_var<int> n, dyn_var<int> target) {
	dyn_var<int> i(0);
	while (i < n) {
		if (i == target)
			return i;
		dyn_var<int> one(1);
		i = i + one;
	}
	return -1;
}

int main(int argc, char *argv[]) {
	builder::builder_context context;
	auto simple_ast = context.extract_function_ast(simple_loop, "generated_simple");
	block::c_code_generator::generate_code(simple_ast, std::cout, 0);

	auto nested_ast = context.extract_function_ast(nested_loops, "generated_nested");
	block::c_code_generator::generate_code(nested_ast, std::cout, 0);

	auto return_ast = context.extract_function_ast(return_in_loop, "generated_early_return");
	block::c_code_generator::generate_code(return_ast, std::cout, 0);
	return 0;
}
