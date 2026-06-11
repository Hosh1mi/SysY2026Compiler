#include "../../include/mid/opt/passManager.hpp"
#include "../../include/mid/analysis/loopVerify.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

void PassManager::addPass(std::unique_ptr<Pass> pass) {
    passes.push_back(std::move(pass));
}

void PassManager::beginRepeatGroup(int maxRounds) {
    RepeatGroup g;
    g.begin = passes.size();
    g.end   = passes.size(); // endRepeatGroup 回填
    g.maxRounds = maxRounds;
    groups_.push_back(g);
}

void PassManager::endRepeatGroup() {
    groups_.back().end = passes.size();
}

static size_t countInstructions(Module *module) {
    size_t n = 0;
    for (auto *func : module->function_list_) {
        for (auto *bb : func->basic_blocks_)
            n += bb->instr_list_.size();
    }
    return n;
}

PreservedAnalyses PassManager::runSinglePass(Pass &pass, Module *module) {
    if (dump_ir_) {
        std::cerr << "; === IR Before " << pass.name() << " ===\n"
                  << module->print() << "\n";
    }

    PreservedAnalyses preserved = pass.execute(module, analyses_);

    if (verify_ir_) {
        module->verify("after " + pass.name());
        // 循环规范形校验：只在规范化 pass 之后断言——其他 pass 不承诺
        // 保持规范形。LoopSimplify 后要求 L2（L1 + dedicated exits）；
        // LoopRotate 后要求 L1（guard 路径与循环出口在 exitSucc 汇合，
        // dedicated exits 不在其后置条件内）。
        const std::string &n = pass.name();
        if (n == "LoopSimplify")
            verifyLoops(module, /*level=*/2, "after " + n, /*warnOnly=*/true);
        else if (n == "LCSSA")
            verifyLoops(module, /*level=*/3, "after " + n, /*warnOnly=*/true);
        else if (n == "LoopRotate")
            verifyLoops(module, /*level=*/1, "after " + n, /*warnOnly=*/true);
    }

    analyses_.invalidate(module, preserved);

    if (dump_ir_) {
        std::cerr << "; === IR After " << pass.name() << " ===\n"
                  << module->print() << "\n";
    }
    return preserved;
}

void PassManager::run(Module *module) {
    const bool tracePipeline =
        dump_ir_ || std::getenv("DEBUG_LOOP_PIPELINE") != nullptr;

    size_t gi = 0; // groups_ 按 addPass 顺序构造，天然有序
    for (size_t i = 0; i < passes.size();) {
        if (gi < groups_.size() && groups_[gi].begin == i &&
            groups_[gi].end > groups_[gi].begin) {
            const RepeatGroup &g = groups_[gi];
            // 防失控护栏：指令膨胀阈值（相对组入口），unswitch 等克隆型
            // pass 合法增长有限，超阈说明调度失控
            const size_t entryInsts = countInstructions(module);
            const size_t instBudget = entryInsts * 2 + 1024;

            for (int round = 1; round <= g.maxRounds; round++) {
                bool roundChanged = false;
                std::string changedList;
                for (size_t j = g.begin; j < g.end; j++) {
                    PreservedAnalyses preserved =
                        runSinglePass(*passes[j], module);
                    if (passes[j]->convergenceRelevant() &&
                        !preserved.preservesAll()) {
                        roundChanged = true;
                        if (tracePipeline) {
                            if (!changedList.empty()) changedList += ", ";
                            changedList += passes[j]->name();
                        }
                    }
                }
                if (tracePipeline)
                    std::cerr << "[LoopPipeline] round " << round
                              << (roundChanged ? " changed: {" + changedList + "}"
                                               : " converged")
                              << "\n";
                if (!roundChanged)
                    break;
                size_t now = countInstructions(module);
                if (now > instBudget) {
                    std::cerr << "[LoopPipeline] WARNING: instruction count "
                              << now << " exceeds budget " << instBudget
                              << " (entry " << entryInsts
                              << "), stopping repeat group\n";
                    break;
                }
            }
            i = g.end;
            gi++;
            continue;
        }
        runSinglePass(*passes[i], module);
        i++;
    }
}
