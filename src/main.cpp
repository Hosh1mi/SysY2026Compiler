#include "include/frontend/ast/ast.hpp"
#include "include/frontend/ast/astPrinter.hpp"

#include "include/frontend/checker/checker.hpp"
#include "include/frontend/parser.hpp"

#include "include/mid/ir/irGen.hpp"
#include "include/mid/opt/passManager.hpp"
#include "include/mid/opt/deadCodeDelete.hpp"
#include "include/mid/opt/constFold.hpp"
#include "include/mid/opt/tailRecursionEliminate.hpp"
#include "include/mid/opt/mem2reg.hpp"
#include "include/mid/opt/earlyCSE.hpp"
#include "include/mid/opt/gvn.hpp"
#include "include/mid/opt/algebraSimplify.hpp"
#include "include/mid/opt/sccp.hpp"
#include "include/mid/opt/localCopyPropagation.hpp"
#include "include/mid/opt/inlineExpand.hpp"
#include "include/mid/opt/loopInvariantCodeMotion.hpp"
#include "include/mid/opt/splitGEP.hpp"
#include "include/mid/opt/indVarStrengthReduce.hpp"
#include "include/mid/opt/removeRedundantPhis.hpp"
#include "include/mid/opt/loopRepFold.hpp"
#include "include/mid/opt/scalarExpandedInterchange.hpp"
#include "include/mid/opt/loopUnroll.hpp"
#include "include/mid/opt/reassociate.hpp"
#include "include/mid/opt/loopVectorize.hpp"
#include "include/mid/opt/CFGSimplify.hpp"
#include "include/mid/opt/unifyExitNodes.hpp"

#include "include/backend/arm64/codegen.hpp"

// ── Pipeline helper modules ──────────────────────────────────────────
// Group common pass sequences for reusable cleanup after transforms.

// ConstantFold → AlgebraSimplify → DeadCodeDelete
static void make_basic_clean(PassManager &pm) {
    pm.addPass(std::make_unique<DeadCodeDelete>());
    pm.addPass(std::make_unique<SCCP>());
    pm.addPass(std::make_unique<ConstantFold>());
    pm.addPass(std::make_unique<AlgebraSimplify>());
    pm.addPass(std::make_unique<DeadCodeDelete>());
}

// DeadCodeDelete → CFGSimplify (trivial phi cleanup is inside DCE's fixed-point loop)
static void make_cfg_clean(PassManager &pm) {
    pm.addPass(std::make_unique<DeadCodeDelete>());
    pm.addPass(std::make_unique<CFGSimplify>());
}

// make_basic_clean + CFGSimplify + make_basic_clean (trivial phi cleanup is inside DCE)
static void make_deep_clean(PassManager &pm) {
    make_basic_clean(pm);
    pm.addPass(std::make_unique<CFGSimplify>());
    make_basic_clean(pm);
}

// DeadCodeDelete × 4 + CFGSimplify × 4 (aggressive post-unroll cleanup)
static void make_unroll_clean(PassManager &pm) {
    for (int i = 0; i < 4; i++) {
        pm.addPass(std::make_unique<DeadCodeDelete>());
        pm.addPass(std::make_unique<CFGSimplify>());
    }
}

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
	int print_asm = false;
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
    pm.addPass(std::make_unique<ScalarExpandedInterchange>());  // 标量提升 + P-L 循环交换
	if(optLevel >= 1){
	    pm.addPass(std::make_unique<CFGSimplify>());          // 化简 CFG
		pm.addPass(std::make_unique<Mem2Reg>());
        pm.addPass(std::make_unique<EarlyCSE>());            // 局部公共子表达式消除
        pm.addPass(std::make_unique<TailRecursionEliminate>());// 尾递归→循环
        pm.addPass(std::make_unique<Reassociate>());          // 重关联规范化
        make_basic_clean(pm);                                 // ConstantFold + AlgebraSimplify + DCE

        pm.addPass(std::make_unique<LocalCopyPropagation>()); // 局部复制传播
        make_basic_clean(pm);

        // pm.addPass(std::make_unique<GVN>());                  // 全局值编号
        make_basic_clean(pm);

        pm.addPass(std::make_unique<InlineExpand>());         // 内联展开（SSA + 尾递归消除后）
        make_deep_clean(pm);                                  // basic + CFGSimplify + basic

        pm.addPass(std::make_unique<UnifyExitNodes>());       // 统一返回点（方便 codegen）
        pm.addPass(std::make_unique<CFGSimplify>());          // 化简 CFG（清理内联产生的冗余分支等）

        // pm.addPass(std::make_unique<SplitGEP>());          // GEP split → LICM hoist (TODO: fix loop detection)
        pm.addPass(std::make_unique<LICM>());                 // 循环不变式外提
        pm.addPass(std::make_unique<LoopVectorize>());        // 循环向量化
        pm.addPass(std::make_unique<IndVarStrengthReduce>()); // 归纳变量强度削弱
        pm.addPass(std::make_unique<LoopRepFold>());          // 循环重复折叠（消除外层计数循环）
        pm.addPass(std::make_unique<LoopUnroll>());           // 循环展开
        make_deep_clean(pm);                                  // basic + CFGSimplify + basic

        make_basic_clean(pm);                                 // 最后再来一轮基本清理
        make_cfg_clean(pm);                                   // 最后再来一轮 CFG 清理

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
