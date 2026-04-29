compiler: parser.y lexer.l
	bison -d -o ../build/parser.cpp parser.y
	flex -o ../build/lexer.cpp lexer.l
	g++ -DDEBUG_PARSER ast/ast.cpp ast/astPrinter.cpp ../build/parser.cpp ../build/lexer.cpp ir/irGen.cpp ir/ir.cpp checker/checker.cpp checker/errReporter.cpp main.cpp -o ../build/compiler

run:
	../build/compiler ../test/ir/012_func_defn.sy
