#include "include/frontend/ast/ast.hpp"
#include "include/frontend/ast/astPrinter.hpp"

#include "include/frontend/checker/checker.hpp"
#include "include/frontend/parser.hpp"

#include "include/mid/ir/irGen.hpp"
#include "include/mid/opt/passManager.hpp"
#include "include/mid/opt/deadCodeDelete.hpp"
#include "include/mid/opt/arraySimplify.hpp"
#include "include/mid/opt/constFold.hpp"
#include "include/mid/opt/tailRecursionEliminate.hpp"
#include "include/mid/opt/mem2reg.hpp"
#include "include/mid/opt/constSubexprEliminate.hpp"
#include "include/mid/opt/algebraSimplify.hpp"
#include "include/mid/opt/localCopyPropagation.hpp"
#include "include/mid/opt/inlineExpand.hpp"
#include "include/mid/opt/loopInvariantCodeMotion.hpp"
#include "include/mid/opt/indVarStrengthReduce.hpp"
#include "include/mid/opt/removeRedundantPhis.hpp"
#include "include/mid/opt/loopUnroll.hpp"
#include "include/mid/opt/reassociate.hpp"
#include "include/mid/opt/loopVectorize.hpp"
#include "include/mid/opt/CFGSimplify.hpp"

#include "include/backend/arm64/arm64_codegen.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <unistd.h>

extern unique_ptr<CompUnitAST> root;
extern int yyparse();
extern FILE *yyin;

int main(int argc, char **argv) {
	if (argc < 2) {
        std::cerr << "No input file.\n";
        return -1;
    }

	char *filename = nullptr;
	int print_ir = false;
	int print_asm = true; 
	std::string output = "-";
	int optLevel = 0;

	for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-S") {
            print_asm = true;
            print_ir = false;
        } 
        else if (arg == "-c") {
            print_ir = true;
            print_asm = false;
        } 
        else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "-o requires a filename\n";
                return -1;
            }
            output = argv[++i];
        } 
        else if (arg.rfind("-O", 0) == 0) {
            // 支持 -O1 / -O2 / -O 1
            if (arg.size() > 2) {
                optLevel = arg[2] - '0';
            } else {
                if (i + 1 < argc) {
                    optLevel = std::stoi(argv[++i]);
                } else {
                    optLevel = 1; // 默认 -O == -O1
                }
            }
        } 
        else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            return -1;
        } 
        else {
            filename = argv[i];
        }
    }

	if (!filename) {
        std::cerr << "No input file provided.\n";
        return -1;
    }

    // Set default to asm for now
    if (!print_ir && !print_asm) {
        print_asm = true;
    }

    yyin = fopen(filename, "r");
    if (!yyin) {
        std::cerr << "Failed to open input file: " << filename << "\n";
        return -1;
    }

	/* frontend */
	// Lexer, Parser, and generate AST
	yyparse();

	/* mid */
	// Generate IR from AST
	GenIR genIR;
	root->accept(genIR);
	std::unique_ptr<Module> m = genIR.getModule();

    // TODO：设计合适的Pass Pipeline
	PassManager pm;
	if(optLevel >= 1){
        // pm.addPass(std::make_unique<InlineExpand>());
        pm.addPass(std::make_unique<DimArrayArgSimplify>());  // 清理数组参数
        pm.addPass(std::make_unique<CFGSimplify>());          // 化简 CFG
        pm.addPass(std::make_unique<Mem2Reg>());              // 构造 SSA
        pm.addPass(std::make_unique<RemoveRedundantPhis>());  // 清理 trivial phi
        // pm.addPass(std::make_unique<CFGSimplify>());          // 再化简 CFG

        pm.addPass(std::make_unique<TailRecursionEliminate>());// 尾递归→循环
        // pm.addPass(std::make_unique<CFGSimplify>());          // 尾部消除后清理 CFG
        pm.addPass(std::make_unique<Reassociate>());          // 重关联规范化
        pm.addPass(std::make_unique<ConstantFold>());         // 常数折叠
        pm.addPass(std::make_unique<AlgebraSimplify>());      // 代数简化
        pm.addPass(std::make_unique<LocalCopyPropagation>()); // 局部复制传播
        pm.addPass(std::make_unique<DeadCodeDelete>());       // 死代码消除

        pm.addPass(std::make_unique<ConstantFold>());         // 折叠新常量
        pm.addPass(std::make_unique<CSE>());                  // 公共子表达式消除
        pm.addPass(std::make_unique<DeadCodeDelete>());       // 清理死代码

        pm.addPass(std::make_unique<LICM>());                 // 循环不变式外提
        pm.addPass(std::make_unique<ConstantFold>());         // 外提后折叠
        pm.addPass(std::make_unique<DeadCodeDelete>());       // 清理外提后死代码
        
        pm.addPass(std::make_unique<LoopUnroll>());           // 循环展开
        pm.addPass(std::make_unique<LoopVectorize>());        // 循环向量化（实验性）
        
        pm.addPass(std::make_unique<DeadCodeDelete>());       // 展开后消除死代码
        pm.addPass(std::make_unique<CFGSimplify>());          // 展开后化简 CFG

        pm.addPass(std::make_unique<DeadCodeDelete>());       // 最终 DCE
	}  
    
	pm.run(m.get());

	std::ofstream fout;
	std::ostream *out = &std::cout;
	if (output != "-") {
		fout.open(output);
		if (!fout.is_open()) {
			std::cerr << "failed to open output file: " << output << std::endl;
			return -1;
		}
		out = &fout;
	}

	/* backend */
	if (print_asm) {
		Arm64CodeGen codegen(m.get(), *out);
		codegen.generate();
	}else if (print_ir) {
		*out << m->print();
	}

	return 0;
}
