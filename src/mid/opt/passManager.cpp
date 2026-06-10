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
            // 循环规范形 L1（唯一 preheader + 唯一 latch）：
            // 只在规范化 pass 之后断言——其他 pass 不承诺保持规范形
            const std::string &n = pass->name();
            if (n == "LoopSimplify" || n == "LoopRotate")
                verifyLoops(module, /*level=*/1, "after " + n, /*warnOnly=*/true);
        }

        analyses_.invalidate(module, preserved);

        if (dump_ir_) {
            std::cerr << "; === IR After " << pass->name() << " ===\n"
                      << module->print() << "\n";
        }
    }
}
