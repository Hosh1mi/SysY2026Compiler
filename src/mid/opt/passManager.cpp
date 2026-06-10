#include "../../include/mid/opt/passManager.hpp"
#include "../../include/mid/analysis/loopVerify.hpp"
#include <iostream>

void PassManager::addPass(std::unique_ptr<Pass> pass) {
    passes.push_back(std::move(pass));
}

void PassManager::run(Module *module) {
    for (auto &pass : passes) {
        if (dump_ir_) {
            std::cerr << "; === IR Before " << pass->name() << " ===\n"
                      << module->print() << "\n";
        }

        PreservedAnalyses preserved = pass->execute(module, analyses_);

        if (verify_ir_) {
            module->verify("after " + pass->name());
            // 循环规范形校验：只在规范化 pass 之后断言——其他 pass 不承诺
            // 保持规范形。LoopSimplify 后要求 L2（L1 + dedicated exits）；
            // LoopRotate 后要求 L1（guard 路径与循环出口在 exitSucc 汇合，
            // dedicated exits 不在其后置条件内）。
            const std::string &n = pass->name();
            if (n == "LoopSimplify")
                verifyLoops(module, /*level=*/2, "after " + n, /*warnOnly=*/true);
            else if (n == "LoopRotate")
                verifyLoops(module, /*level=*/1, "after " + n, /*warnOnly=*/true);
        }

        analyses_.invalidate(module, preserved);

        if (dump_ir_) {
            std::cerr << "; === IR After " << pass->name() << " ===\n"
                      << module->print() << "\n";
        }
    }
}
