#include "builder/builder_context.h"
#include "builder/builder.h"
#include <iostream>
using int_var = builder::int_var;



// A simple straight line code with 2 variable declarations and one operator
void foo(builder::builder_context* context) {
	int_var a(context);
	int_var b(context);
	a && b;
}

int main(int argc, char* argv[]) {
	builder::builder_context context;
	context.extract_ast_from_function(foo)->dump(std::cout, 0);	
	return 0;
}
