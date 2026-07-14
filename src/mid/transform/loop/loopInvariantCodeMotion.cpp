#include "../../../include/mid/opt/loopInvariantCodeMotion.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include <algorithm>

void LICM::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses LICM::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, AM);
    }

    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

// ── Invariance / safety checks ─────────────────────────────────────────────

bool LICM::isInvariant(Value *val, const std::set<BasicBlock*>& loopBlocks,
                       const std::set<Instruction*>& toHoist) {
    if (dynamic_cast<Constant*>(val) ||
        dynamic_cast<GlobalVariable*>(val) ||
        dynamic_cast<Argument*>(val))
        return true;

    auto *inst = dynamic_cast<Instruction*>(val);
    if (!inst) return true;
    if (toHoist.count(inst)) return true;
    return !loopBlocks.count(inst->parent_);
}

bool LICM::isSafeToHoist(Instruction *inst, const Loop &loop,
                         const BasicAliasAnalysis &BAA) {
    if (inst->is_phi() || inst->is_br() || inst->is_ret() ||
        inst->is_store() || inst->is_alloca())
        return false;

    // 收益判断：当 ICmp 的全部使用都是循环内的分支时，外提会破坏 codegen 的
    // cmp+br 融合（变成 cset+cbz 并跨循环占用一个 w 寄存器），通常得不偿失。
    // 让 ICmp 保留在分支所在 BB，codegen 可融合为 cbz/cbnz 或 cmp+b.cond。
    if (inst->op_id_ == Instruction::ICmp) {
        bool allBranchUseInLoop = !inst->use_list_.empty();
        for (auto &use : inst->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user || !user->is_br()) { allBranchUseInLoop = false; break; }
            if (!loop.blocks.count(user->parent_)) { allBranchUseInLoop = false; break; }
        }
        if (allBranchUseInLoop) return false;
    }

    if (inst->is_call()) {
        if (inst->is_void()) return false;
        auto *call = static_cast<CallInst *>(inst);
        auto *callee = dynamic_cast<Function *>(
            call->get_operand(call->num_ops_ - 1));
        return callee &&
               (callee->hasSemFlag(SemFlag::FnPure) || BAA.isPure(callee));
    }

    if (inst->is_load()) {
        // 不变内存（const 全局/不逃逸局部 const 数组）的读：内容永不改写，
        // 可越过循环内别名歧义直接外提，无需逐指令扫描 clobber。
        if (inst->hasSemFlag(SemFlag::ImmutableLoad))
            return true;
        Value *ptr = inst->get_operand(0);
        for (auto *bb : loop.blocks) {
            for (auto *other : bb->instr_list_) {
                if (isModSet(BAA.getModRefInfo(other, ptr)))
                    return false;
            }
        }
        return true;
    }

    return true;
}

// ── Hoisting ──────────────────────────────────────────────────────────────

bool LICM::runOnLoop(const Loop &loop, const BasicAliasAnalysis *BAA) {
    BasicBlock *preheader = loop.preheader;
    if (!preheader) return false;
    if (!BAA) return false;

    bool changed = false;
    bool progress = true;
    while (progress) {
        progress = false;

        // 收集与外提两阶段必须同序遍历（保证 def 先于 use 外提），且必须
        // 用确定序 blocksOrdered——指针序会让 preheader 中外提指令的顺序
        // 跨进程漂移（中端不确定性来源之一）。
        std::set<Instruction*> toHoist;
        for (auto bb : loop.blocksOrdered) {
            for (auto inst : bb->instr_list_) {
                if (!isSafeToHoist(inst, loop, *BAA)) continue;

                bool allInvariant = true;
                unsigned operandLimit = inst->is_call() ? inst->num_ops_ - 1
                                                        : inst->num_ops_;
                for (unsigned i = 0; i < operandLimit; i++) {
                    if (!isInvariant(inst->get_operand(i), loop.blocks, toHoist)) {
                        allInvariant = false;
                        break;
                    }
                }
                if (allInvariant) toHoist.insert(inst);
            }
        }

        if (toHoist.empty()) break;

        for (auto bb : loop.blocksOrdered) {
            auto it = bb->instr_list_.begin();
            while (it != bb->instr_list_.end()) {
                Instruction *inst = *it;
                if (!toHoist.count(inst)) { ++it; continue; }

                it = bb->instr_list_.end();
                bb->remove_instr(inst);
                inst->parent_ = preheader;
                preheader->add_instruction_before_terminator(inst);

                it = bb->instr_list_.begin();
                progress = true;
                changed = true;
            }
        }
    }

    return changed;
}

bool LICM::eliminateSinglePredPhis(Function *func) {
    bool anyChanged = false;
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto bb : func->basic_blocks_) {
            if (bb->pre_bbs_.size() != 1) continue;
            auto it = bb->instr_list_.begin();
            while (it != bb->instr_list_.end()) {
                auto *phi = dynamic_cast<PhiInst*>(*it);
                if (!phi) break;
                Value *incoming = phi->get_operand(0);
                phi->replace_all_use_with(incoming);
                bb->delete_instr(phi);
                changed = true;
                anyChanged = true;
                it = bb->instr_list_.begin();
            }
        }
    }
    return anyChanged;
}

bool LICM::eliminateTrivialHeaderPhis(const Loop &loop) {
    // 单 latch 是 LoopSimplify 的后置条件；多 latch 时"来自 latch 的入边"
    // 不唯一，保守跳过
    BasicBlock *latch = loop.singleLatch();
    if (!latch) return false;
    bool anyChanged = false;
    bool changed = true;
    while (changed) {
        changed = false;
        auto it = loop.header->instr_list_.begin();
        while (it != loop.header->instr_list_.end()) {
            auto *phi = dynamic_cast<PhiInst*>(*it);
            if (!phi) break;

            Value *nonLatchVal = nullptr;
            bool selfLoop = true;
            bool valid = true;

            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                Value *incVal = phi->get_operand(i);
                Value *incBB  = phi->get_operand(i + 1);
                if (incBB == latch) {
                    if (incVal != phi) selfLoop = false;
                } else {
                    if (!nonLatchVal) nonLatchVal = incVal;
                    else if (nonLatchVal != incVal) valid = false;
                }
            }

            if (valid && selfLoop && nonLatchVal) {
                auto *nvInst = dynamic_cast<Instruction*>(nonLatchVal);
                if (nvInst && loop.blocks.count(nvInst->parent_)) {
                    ++it;
                    continue;
                }
                phi->replace_all_use_with(nonLatchVal);
                loop.header->delete_instr(phi);
                changed = true;
                anyChanged = true;
                it = loop.header->instr_list_.begin();
                continue;
            }
            ++it;
        }
    }
    return anyChanged;
}

bool LICM::runOnFunction(Function *func, AnalysisManager &AM) {
    if (func->basic_blocks_.empty()) return false;

    bool anyChanged = eliminateSinglePredPhis(func);

    // 由内向外处理。本 pass 不改 CFG，但 changed 后会失效 AM 缓存的
    // LoopInfo，Loop* 不能跨失效持有——先收集稳定的 header 列表，
    // 每轮按 header 重查当前 LoopInfo。
    std::vector<BasicBlock *> headers;
    {
        LoopInfo &LI = AM.getLoopInfo(func);
        std::vector<Loop *> loops;
        for (auto &l : LI.allLoops())
            loops.push_back(l.get());
        std::sort(loops.begin(), loops.end(), [](Loop *a, Loop *b) {
            return a->depth > b->depth;
        });
        for (auto *l : loops)
            headers.push_back(l->header);
    }

    for (auto *header : headers) {
        LoopInfo &LI = AM.getLoopInfo(func);
        Loop *loop = nullptr;
        for (auto &l : LI.allLoops()) {
            if (l->header == header) {
                loop = l.get();
                break;
            }
        }
        if (!loop) continue;

        BasicAliasAnalysis &BAA = AM.getBasicAA(func->parent_);

        bool changed = runOnLoop(*loop, &BAA);
        bool phiChanged = eliminateTrivialHeaderPhis(*loop);
        if (changed || phiChanged) {
            anyChanged = true;
            PreservedAnalyses pa;
            pa.preserveBasicAA();
            AM.invalidateFunction(func, pa);
        }
    }
    return anyChanged;
}
