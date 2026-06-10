// LoopVerify —— 循环规范形校验（plan 阶段 0.2）
//
// 在 LoopInfo 之上做分级断言。LoopInfo 自身从 pre_bbs_/succ_bbs_ 推导
// CFG，因此本校验器假定调用前已通过 module->verify()（它会断言链表与
// terminator 一致）；passManager 中的调用顺序保证了这一点。

#include "../../include/mid/analysis/loopVerify.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/ir/module.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/basicBlock.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <cstdlib>
#include <iostream>

namespace {

const int kMaxReports = 20;

void report(int &violations, const std::string &context, Function *func,
            const std::string &msg) {
    if (++violations <= kMaxReports) {
        std::cerr << "[LOOP-VERIFY] "
                  << (context.empty() ? "" : context + ": ")
                  << func->name_ << ": " << msg << "\n";
    }
}

} // namespace

int verifyLoops(Module *m, int level, const std::string &context,
                bool warnOnly) {
    int violations = 0;

    for (auto *func : m->function_list_) {
        if (func->is_declaration()) continue;

        LoopInfo li;
        li.analyze(func);

        for (const auto &loopPtr : li.allLoops()) {
            Loop *loop = loopPtr.get();
            const std::string at = "loop@" + loop->header->name_;

            // ── L1：唯一 preheader、唯一 latch ──
            if (!loop->preheader)
                report(violations, context, func, at + " has no unique preheader");
            if (loop->latches.size() != 1)
                report(violations, context, func,
                       at + " has " + std::to_string(loop->latches.size()) +
                       " latches (want 1)");

            // ── L2：dedicated exits——exit 块的前驱必须全在循环内 ──
            if (level >= 2) {
                for (auto *exit : loop->exits) {
                    for (auto *pred : exit->pre_bbs_) {
                        if (!loop->blocks.count(pred)) {
                            report(violations, context, func,
                                   at + " exit '" + exit->name_ +
                                   "' has out-of-loop predecessor '" +
                                   pred->name_ + "' (not dedicated)");
                            break;
                        }
                    }
                }
            }

            // ── L3：LCSSA——循环内定义在循环外的 use 必须是 exit 块的 phi ──
            if (level >= 3) {
                for (auto *bb : loop->blocks) {
                    for (auto *inst : bb->instr_list_) {
                        for (const auto &use : inst->use_list_) {
                            auto *user = dynamic_cast<Instruction *>(use.val_);
                            if (!user || !user->parent_) continue;
                            if (loop->blocks.count(user->parent_)) continue;
                            bool isLcssaPhi = false;
                            if (user->is_phi()) {
                                for (auto *exit : loop->exits)
                                    if (user->parent_ == exit) { isLcssaPhi = true; break; }
                            }
                            if (!isLcssaPhi)
                                report(violations, context, func,
                                       at + " value defined in '" + bb->name_ +
                                       "' used outside loop in '" +
                                       user->parent_->name_ + "' without LCSSA phi");
                        }
                    }
                }
            }
        }
    }

    if (violations > 0) {
        std::cerr << "[LOOP-VERIFY] "
                  << (context.empty() ? "" : context + ": ")
                  << violations << " violation(s)"
                  << (warnOnly ? " (warn-only)" : ", aborting") << "\n";
        if (!warnOnly) std::abort();
    }
    return violations;
}
