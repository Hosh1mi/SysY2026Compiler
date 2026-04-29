#define DEBUG_LEXER
#define DEBUG_PARSER
#define DEBUG_IRGEN

compiler: parser.y lexer.l
	bison -d -o ../build/parser.cpp parser.y
	flex -o ../build/lexer.cpp lexer.l
	g++ ast/ast.cpp ast/astPrinter.cpp ../build/parser.cpp ../build/lexer.cpp ir/irGen.cpp ir/ir.cpp arm/arm.cpp checker/checker.cpp checker/errReporter.cpp main.cpp -o ../build/compiler

run:
	../build/compiler ../test/functional/00_main.sy
