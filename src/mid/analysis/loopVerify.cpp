// LoopVerify —— 循环规范形校验
//
// 在 LoopInfo 之上做分级断言。LoopInfo 自身从 pre_bbs_/succ_bbs_ 推导
// CFG，因此本校验器假定调用前已通过 module->verify()（它会断言链表与
// terminator 一致）；passManager 中的调用顺序保证了这一点。

#include "../../include/mid/analysis/loopVerify.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/analysis/loopUtils.hpp"
#include "../../include/mid/ir/module.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/basicBlock.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

const int kMaxReports = 20;

void report(int &levelViolations, int &totalReports,
            const std::string &context, Function *func,
            const std::string &msg) {
    ++levelViolations;
    if (++totalReports <= kMaxReports) {
        std::cerr << "[LOOP-VERIFY] "
                  << (context.empty() ? "" : context + ": ")
                  << func->name_ << ": " << msg << "\n";
    }
}

std::string useLocation(Instruction *user, BasicBlock *semanticBlock) {
    if (!user || !user->parent_)
        return "<unknown>";

    std::string loc = user->parent_->name_;
    if (semanticBlock && semanticBlock != user->parent_)
        loc += " via incoming edge from '" + semanticBlock->name_ + "'";
    return loc;
}

std::string preheaderDiagnostic(Loop *loop) {
    if (!loop || !loop->header)
        return "loop has no header";

    std::vector<BasicBlock *> outsidePreds;
    for (auto *pred : loop->header->pre_bbs_) {
        if (!loop->blocks.count(pred))
            outsidePreds.push_back(pred);
    }

    if (outsidePreds.empty())
        return "has no outside predecessor for a preheader";
    if (outsidePreds.size() > 1)
        return "has " + std::to_string(outsidePreds.size()) +
               " outside predecessors (want 1 dedicated preheader)";

    BasicBlock *pred = outsidePreds.front();
    if (pred->succ_bbs_.size() != 1 || pred->succ_bbs_[0] != loop->header)
        return "outside predecessor '" + pred->name_ +
               "' is not a single-successor preheader";

    auto *term = pred->get_terminator();
    if (!term || !term->is_br())
        return "outside predecessor '" + pred->name_ +
               "' has no branch terminator";
    if (term->num_ops() != 1 || term->get_operand(0) != loop->header)
        return "outside predecessor '" + pred->name_ +
               "' does not unconditionally branch to header";

    return "has no dedicated preheader";
}

std::string latchDiagnostic(Loop *loop) {
    if (!loop || !loop->header)
        return "loop has no header";

    if (loop->latches.empty())
        return "has no latch";
    if (loop->latches.size() > 1) {
        std::string msg = "has " + std::to_string(loop->latches.size()) +
                          " latches (want 1):";
        for (auto *latch : loop->latches)
            msg += " " + latch->name_;
        return msg;
    }

    BasicBlock *latch = loop->latches.front();
    if (!loop->blocks.count(latch))
        return "latch '" + latch->name_ + "' is not in loop blocks";

    auto *term = latch->get_terminator();
    if (!term || !term->is_br())
        return "latch '" + latch->name_ + "' has no branch terminator";

    bool branchesToHeader = false;
    for (unsigned i = 0; i < term->num_ops(); ++i)
        if (term->get_operand(i) == loop->header)
            branchesToHeader = true;
    if (!branchesToHeader)
        return "latch '" + latch->name_ + "' does not branch to header";

    return "has no unique latch";
}

} // namespace

LoopVerifyResult verifyLoopForms(Module *m, int level,
                                 const std::string &context,
                                 bool warnOnly,
                                 bool reportClean) {
    LoopVerifyResult result;
    int totalReports = 0;

    for (auto *func : m->function_list_) {
        if (func->is_declaration()) continue;

        LoopInfo li;
        li.analyze(func);

        for (const auto &loopPtr : li.allLoops()) {
            Loop *loop = loopPtr.get();
            result.loops++;
            const std::string at = "loop@" + loop->header->name_;

            // ── L1：dedicated preheader、唯一 latch ──
            if (!loop->preheader)
                report(result.l1Violations, totalReports, context, func,
                       at + " " + preheaderDiagnostic(loop));
            if (loop->latches.size() != 1)
                report(result.l1Violations, totalReports, context, func,
                       at + " " + latchDiagnostic(loop));

            // ── L2：dedicated exits——exit 块的前驱必须全在循环内 ──
            if (level >= 2) {
                for (auto *exit : loop->exits) {
                    for (auto *pred : exit->pre_bbs_) {
                        if (!loop->blocks.count(pred)) {
                            report(result.l2Violations, totalReports,
                                   context, func,
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
                            auto *user = use.user_;
                            if (!user || !user->parent_) continue;
                            BasicBlock *useBlock =
                                getSemanticUseBlock(user, use.operand_index_);
                            if (useBlock && loop->blocks.count(useBlock))
                                continue;
                            bool isLcssaPhi = false;
                            if (user->is_phi()) {
                                for (auto *exit : loop->exits)
                                    if (user->parent_ == exit) { isLcssaPhi = true; break; }
                            }
                            if (!isLcssaPhi)
                                report(result.l3Violations, totalReports,
                                       context, func,
                                       at + " value defined in '" + bb->name_ +
                                       "' used outside loop in '" +
                                       useLocation(user, useBlock) +
                                       "' without LCSSA phi");
                        }
                    }
                }
            }
        }
    }

    if (reportClean || result.totalViolations() > 0) {
        std::cerr << "[LOOP-VERIFY] "
                  << (context.empty() ? "" : context + ": ")
                  << "loops=" << result.loops
                  << " L1=" << (result.l1Violations == 0 ? "ok" : std::to_string(result.l1Violations))
                  << " L2=" << (level >= 2 ? (result.l2Violations == 0 ? "ok" : std::to_string(result.l2Violations)) : "skip")
                  << " L3=" << (level >= 3 ? (result.l3Violations == 0 ? "ok" : std::to_string(result.l3Violations)) : "skip")
                  << "\n";
    }

    if (result.totalViolations() > 0) {
        std::cerr << "[LOOP-VERIFY] "
                  << (context.empty() ? "" : context + ": ")
                  << result.totalViolations() << " violation(s)"
                  << (warnOnly ? " (warn-only)" : ", aborting") << "\n";
        if (!warnOnly) std::abort();
    }
    return result;
}

int verifyLoops(Module *m, int level, const std::string &context,
                bool warnOnly) {
    return verifyLoopForms(m, level, context, warnOnly,
                           /*reportClean=*/false).totalViolations();
}
