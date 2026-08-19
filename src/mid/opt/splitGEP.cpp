// 典型示例：
//   优化前：循环每轮都计算 GEP @a, 0, %row, %i。
//   优化后：预头计算行基址 GEP @a, 0, %row，循环内仅追加 %i。
// %row 在循环中保持不变，因此多维地址的不变前缀可以只计算一次。

#include "../../include/mid/opt/splitGEP.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

// SplitGEP 将循环内多维地址计算拆成“不变前缀 + 变化下标”。不变的行基址
// GEP 被移动到循环预头，循环体只保留随迭代变化的末级索引，以减少重复计算，
// 同时为后续向量化暴露更直接的连续地址形式。

static bool isSplitGEPDebugEnabled() {
    static bool enabled = std::getenv("DEBUG_SPLIT_GEP") != nullptr;
    return enabled;
}

// 值未由循环体内的指令定义时视为循环不变量；常量、全局、alloca 和参数
// 天然满足此条件。
static bool isInvariantInLoop(Value *v, Loop *loop) {
    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst) return true;
    return loop->blocks.count(inst->parent_) == 0;
}

// 在单个规范循环中寻找可拆分 GEP，把最大不变前缀提到预头并替换原地址链。
bool SplitGEP::runOnLoop(Loop *loop, LoopInfo &LI,
                         const DominatorTreeAnalysis &DT) {
    BasicBlock *preheader = loop->preheader;
    if (!preheader) return false;

    Module *mod = preheader->parent_->parent_;
    bool changed = false;

    for (auto *bb : loop->blocksOrdered) {
        // Only handle GEPs whose innermost containing loop is exactly `loop`,
        // so a nested access is peeled once, at the deepest level where its
        // row index is invariant.
        if (LI.getLoopFor(bb) != loop) continue;

        std::vector<Instruction *> insts(bb->instr_list_.begin(),
                                         bb->instr_list_.end());
        for (auto *inst : insts) {
            if (!inst->is_gep()) continue;
            auto *gep = static_cast<GetElementPtrInst *>(inst);
            unsigned numIndices = gep->num_ops() - 1;
            if (numIndices < 2) continue;

            // Longest prefix of indices that are invariant in this loop.
            unsigned splitIdx = 0;
            for (unsigned i = 1; i <= numIndices; i++) {
                if (!isInvariantInLoop(gep->get_operand(i), loop)) break;
                splitIdx = i;
            }
            // Need a non-empty invariant prefix and a non-empty variant suffix.
            if (splitIdx == 0 || splitIdx == numIndices) continue;
            // Peeling a constant outermost index buys nothing (its byte offset
            // folds either way) and re-splitting a leading-`[0]` suffix would
            // churn degenerate GEPs; require a real invariant value to hoist.
            if (dynamic_cast<ConstantInt *>(gep->get_operand(1))) continue;

            // Invariant prefix GEP: &A[i] (for a 2D access A[i][j]).
            std::vector<Value *> prefixIdxs;
            for (unsigned i = 1; i <= splitIdx; i++)
                prefixIdxs.push_back(gep->get_operand(i));
            auto *prefixGEP =
                new GetElementPtrInst(gep->get_operand(0), prefixIdxs, bb, true);

            // The prefix must still address an aggregate (a row), otherwise the
            // suffix has nothing to descend into.
            auto *prefixPtrTy = dynamic_cast<PointerType *>(prefixGEP->type_);
            if (!prefixPtrTy || prefixPtrTy->contained_->tid_ != Type::ArrayTyID) {
                prefixGEP->remove_use_of_ops();
                delete prefixGEP;
                continue;
            }

            // Hoisting the row base into the preheader is only valid if every
            // operand's definition dominates the preheader. "Invariant in the
            // loop" (defined outside the loop body) normally implies this, but
            // guard explicitly to stay robust against irreducible/rotated shapes.
            bool dominatesPreheader = true;
            for (Value *op : prefixIdxs) {
                auto *opInst = dynamic_cast<Instruction *>(op);
                if (opInst && !DT.dominates(opInst->parent_, preheader)) {
                    dominatesPreheader = false;
                    break;
                }
            }
            if (auto *baseInst = dynamic_cast<Instruction *>(gep->get_operand(0)))
                if (!DT.dominates(baseInst->parent_, preheader))
                    dominatesPreheader = false;
            if (!dominatesPreheader) {
                prefixGEP->remove_use_of_ops();
                delete prefixGEP;
                continue;
            }

            // Hoist the row base into the preheader: computed once per entry to
            // this loop (i.e. once per outer-loop iteration) and shared by every
            // access that peels to the same prefix (GVN later dedups them).
            if (!preheader->add_instruction_before_terminator(prefixGEP)) {
                prefixGEP->remove_use_of_ops();
                delete prefixGEP;
                continue;
            }
            prefixGEP->parent_ = preheader;

            // Variant suffix GEP. The prefix result still points at the row
            // aggregate, so the suffix re-enters it with a leading 0 before the
            // remaining (variant) indices: &(*prefix)[0][j].
            std::vector<Value *> suffixIdxs;
            suffixIdxs.push_back(new ConstantInt(mod->int32_ty_, 0));
            for (unsigned i = splitIdx + 1; i <= numIndices; i++)
                suffixIdxs.push_back(gep->get_operand(i));
            auto *suffixGEP = new GetElementPtrInst(prefixGEP, suffixIdxs, bb, true);
            if (!bb->add_instruction_before_inst(suffixGEP, inst)) {
                suffixGEP->remove_use_of_ops();
                delete suffixGEP;
                preheader->delete_instr(prefixGEP);
                continue;
            }

            if (isSplitGEPDebugEnabled()) {
                std::cerr << "[SplitGEP] function=" << bb->parent_->name_
                          << " loop_header=" << loop->header->name_
                          << " gep=%" << gep->name_ << " splitIdx=" << splitIdx
                          << " prefix=%" << prefixGEP->name_
                          << " suffix=%" << suffixGEP->name_ << "\n";
            }

            inst->replace_all_use_with(suffixGEP);
            bb->delete_instr(inst);
            changed = true;
        }
    }
    return changed;
}

// 以内层优先顺序处理循环；改写后声明 CFG 分析仍有效，值相关分析失效。
PreservedAnalyses SplitGEP::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *f : module->function_list_) {
        if (f->is_declaration()) continue;
        LoopInfo &LI = AM.getLoopInfo(f);
        DominatorTreeAnalysis &DT = AM.getDominatorTree(f);
        for (const auto &loop : LI.allLoops())
            changed |= runOnLoop(loop.get(), LI, DT);
    }
    return changed ? PreservedAnalyses::cfgAnalyses()
                   : PreservedAnalyses::all();
}

// 兼容旧式 Pass 接口。
void SplitGEP::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}
