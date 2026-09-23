#include "blocks/c_code_generator.h"
#include "builder/builder_context.h"
#include "builder/dyn_var.h"
#include <iostream>

using builder::dyn_var;

static dyn_var<int> same_body(dyn_var<int> n) {
	dyn_var<int> i(0);
	dyn_var<int> z(0);
	if (i < n) {
		z = z + 1;
		i = i + 1;
	}
	while (i < n) {
		z = z + 1;
		i = i + 1;
	}
	return z;
}

static dyn_var<int> different_prefix(dyn_var<int> n) {
	dyn_var<int> i(0);
	dyn_var<int> a(0);
	dyn_var<int> b(0);
	if (i < n) {
		b = b + 1;
		i = i + 1;
	}
	while (i < n) {
		a = a + 1;
		i = i + 1;
	}
	return a + b;
}

static dyn_var<int> followed_by_loop(dyn_var<int> n) {
	dyn_var<int> i(0);
	dyn_var<int> j(0);
	dyn_var<int> z(0);
	if (i < n) {
		z = z + 1;
		i = i + 1;
	}
	while (i < n) {
		z = z + 1;
		i = i + 1;
	}
	while (j < n) {
		z = z + 1;
		j = j + 1;
	}
	return z;
}

int main() {
	builder::builder_context context;
	auto same_body_ast = context.extract_function_ast(same_body, "generated_same_body");
	block::c_code_generator::generate_code(same_body_ast, std::cout, 0);

	auto different_prefix_ast = context.extract_function_ast(different_prefix, "generated_different_prefix");
	block::c_code_generator::generate_code(different_prefix_ast, std::cout, 0);

	auto followed_by_loop_ast = context.extract_function_ast(followed_by_loop, "generated_followed_by_loop");
	block::c_code_generator::generate_code(followed_by_loop_ast, std::cout, 0);
}
