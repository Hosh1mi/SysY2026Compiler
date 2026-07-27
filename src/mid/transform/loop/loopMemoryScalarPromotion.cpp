#include "../../../include/mid/opt/loopMemoryScalarPromotion.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/ir/basicBlock.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/type.hpp"
#include "../../../include/mid/opt/mem2reg.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

// Optimization form:
//
//   before:
//     preheader:
//       br loop
//     loop:
//       %old = load T, T* %p        ; %p is loop-invariant
//       ...
//       store T %new, T* %p
//       br ...
//
//   after this pass:
//     entry:
//       %slot = alloca T
//     preheader:
//       %init = load T, T* %p
//       store T %init, T* %slot
//       br loop
//     loop:
//       %old = load T, T* %slot
//       ...
//       store T %new, T* %slot
//       br ...
//     exit-check:
//       %final = load T, T* %slot
//       %initial = load T, T* %p
//       %changed = icmp ne T %final, %initial  ; integer cells
//       br i1 %changed, label %writeback, label %exit
//     writeback:
//       %final = load T, T* %slot
//       store T %final, T* %p
//       br exit
//
// A following Mem2Reg pass removes %slot and creates the required SSA phi
// nodes. This keeps the transformation local and avoids hand-rolling SSA repair
// for arbitrary loop CFGs.

// 位于 Unroll 之后、所有 CFG cleanup 之后：保留条件写的冷路径，
// 避免 value phi 被折成无条件计算的 select。

namespace {

bool isScalarType(Type *ty) {
    return ty && (ty->tid_ == Type::IntegerTyID || ty->tid_ == Type::FloatTyID);
}

bool isLoopInvariant(Value *value, const Loop &loop) {
    if (!value) return false;
    if (dynamic_cast<Constant *>(value) || dynamic_cast<Argument *>(value) ||
        dynamic_cast<GlobalVariable *>(value))
        return true;

    auto *inst = dynamic_cast<Instruction *>(value);
    return !inst || !loop.blocks.count(inst->parent_);
}

bool exitsAreDominatedByPreheader(const Loop &loop) {
    Function *func = loop.header ? loop.header->parent_ : nullptr;
    if (!func || !loop.preheader)
        return false;
    for (auto *exit : loop.exits) {
        if (!func->dominates(loop.preheader, exit))
            return false;
    }
    return true;
}

struct Candidate {
    Value *ptr = nullptr;
    Type *elemTy = nullptr;
    int exactLoads = 0;
    int exactStores = 0;
};

bool hasDedicatedPhiFreeExits(const Loop &loop) {
    for (auto *exit : loop.exits) {
        for (auto *pred : exit->pre_bbs_) {
            if (!loop.blocks.count(pred))
                return false;
        }
        for (auto *inst : exit->instr_list_) {
            if (inst->is_phi())
                return false;
            break;
        }
    }
    return true;
}

void replaceBranchTarget(BasicBlock *pred, BasicBlock *oldTarget,
                         BasicBlock *newTarget) {
    auto *term = pred->get_terminator();
    if (!term || !term->is_br())
        return;
    for (unsigned i = 0; i < term->num_ops_; ++i) {
        if (term->get_operand(i) == oldTarget)
            term->set_operand(i, newTarget);
    }
}

} // namespace

void LoopMemoryScalarPromotion::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses LoopMemoryScalarPromotion::execute(Module *module,
                                                     AnalysisManager &AM) {
    (void)AM;
    BasicAliasAnalysis BAA;
    BAA.analyze(module);

    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, BAA);
    }

    if (changed) {
        Mem2Reg mem2reg;
        mem2reg.execute(module, AM);
    }

    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool LoopMemoryScalarPromotion::runOnFunction(Function *func,
                                              const BasicAliasAnalysis &BAA) {
    bool changed = false;

    for (int iter = 0; iter < 32; ++iter) {
        LoopInfo LI;
        LI.analyze(func);
        if (LI.allLoops().empty())
            break;

        std::vector<Loop *> loops;
        for (auto &loopPtr : LI.allLoops())
            loops.push_back(loopPtr.get());
        std::sort(loops.begin(), loops.end(), [](Loop *a, Loop *b) {
            return a->depth > b->depth;
        });

        bool iterChanged = false;
        for (auto *loop : loops) {
            if (tryPromote(*loop, BAA)) {
                iterChanged = true;
                changed = true;
                break;
            }
        }

        if (!iterChanged)
            break;
    }

    return changed;
}

bool LoopMemoryScalarPromotion::tryPromote(Loop &loop,
                                           const BasicAliasAnalysis &BAA) {
    if (!loop.preheader || loop.exits.empty() ||
        !exitsAreDominatedByPreheader(loop) || !hasDedicatedPhiFreeExits(loop))
        return false;
    Function *func = loop.header ? loop.header->parent_ : nullptr;
    if (!func || func->basic_blocks_.empty())
        return false;

    std::map<Value *, Candidate> candidates;
    for (auto *bb : loop.blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            Value *ptr = nullptr;
            bool isLoad = false;
            bool isStore = false;
            if (inst->is_load()) {
                ptr = inst->get_operand(0);
                isLoad = true;
            } else if (inst->is_store()) {
                ptr = inst->get_operand(1);
                isStore = true;
            } else {
                continue;
            }

            // 本 pass 新建的 slot 会在本轮结束后由 Mem2Reg 消除；不能在
            // Mem2Reg 之前再次把它当作新的 memory candidate。
            if (dynamic_cast<AllocaInst *>(ptr))
                continue;
            auto *ptrTy = dynamic_cast<PointerType *>(ptr->type_);
            if (!ptrTy || !isScalarType(ptrTy->contained_))
                continue;
            if (!isLoopInvariant(ptr, loop))
                continue;

            auto &cand = candidates[ptr];
            cand.ptr = ptr;
            cand.elemTy = ptrTy->contained_;
            if (isLoad) cand.exactLoads++;
            if (isStore) cand.exactStores++;
        }
    }

    Candidate *best = nullptr;
    for (auto &[ptr, cand] : candidates) {
        if (cand.exactLoads < 1 || cand.exactStores < 1)
            continue;

        bool safe = true;
        for (auto *bb : loop.blocksOrdered) {
            for (auto *inst : bb->instr_list_) {
                if (inst->is_load() && inst->get_operand(0) == cand.ptr)
                    continue;
                if (inst->is_store() && inst->get_operand(1) == cand.ptr)
                    continue;

                if (isModSet(BAA.getModRefInfo(inst, cand.ptr))) {
                    safe = false;
                    break;
                }
            }
            if (!safe) break;
        }
        if (!safe)
            continue;

        if (!best ||
            cand.exactLoads + cand.exactStores >
                best->exactLoads + best->exactStores) {
            best = &cand;
        }
    }

    if (!best)
        return false;

    if (std::getenv("DEBUG_LOOP_MEMORY_SCALAR_PROMOTION")) {
        std::cerr << "[LoopMemoryScalarPromotion] function=" << func->name_
                  << " header=" << loop.header->name_
                  << " loads=" << best->exactLoads
                  << " stores=" << best->exactStores << "\n";
    }

    BasicBlock *entry = func->basic_blocks_.front();
    auto *slot = new AllocaInst(best->elemTy, entry, true);
    entry->add_instruction_front(slot);
    const bool compareFinalValue = best->elemTy->tid_ == Type::IntegerTyID;
    AllocaInst *dirtySlot = nullptr;
    if (!compareFinalValue) {
        dirtySlot = new AllocaInst(func->parent_->int1_ty_, entry, true);
        entry->add_instruction_front(dirtySlot);
    }

    auto *initLoad = new LoadInst(best->ptr, loop.preheader);
    loop.preheader->remove_instr(initLoad);
    loop.preheader->add_instruction_before_terminator(initLoad);

    auto *initStore = new StoreInst(initLoad, slot, loop.preheader, true);
    loop.preheader->add_instruction_before_terminator(initStore);
    if (dirtySlot) {
        auto *initDirty = new StoreInst(new ConstantInt(func->parent_->int1_ty_, 0),
                                        dirtySlot, loop.preheader, true);
        loop.preheader->add_instruction_before_terminator(initDirty);
    }

    std::vector<BasicBlock *> dirtyBlocks;
    for (auto *bb : loop.blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_load() && inst->get_operand(0) == best->ptr) {
                inst->set_operand(0, slot);
            } else if (inst->is_store() && inst->get_operand(1) == best->ptr) {
                inst->set_operand(1, slot);
                if (dirtySlot && std::find(dirtyBlocks.begin(), dirtyBlocks.end(), bb) ==
                    dirtyBlocks.end())
                    dirtyBlocks.push_back(bb);
            }
        }
    }

    for (auto *bb : dirtyBlocks) {
        auto *markDirty = new StoreInst(new ConstantInt(func->parent_->int1_ty_, 1),
                                        dirtySlot, bb, true);
        bb->add_instruction_before_terminator(markDirty);
    }

    for (auto *exit : loop.exits) {
        auto *check = new BasicBlock(func->parent_, exit->name_ + ".lmsp.check", func);
        auto *writeback = new BasicBlock(func->parent_,
                                         exit->name_ + ".lmsp.writeback", func);
        std::vector<BasicBlock *> preds = exit->pre_bbs_;
        for (auto *pred : preds) {
            replaceBranchTarget(pred, exit, check);
            pred->remove_succ_basic_block(exit);
            pred->add_succ_basic_block(check);
            exit->remove_pre_basic_block(pred);
            check->add_pre_basic_block(pred);
        }

        Value *finalValue = nullptr;
        Value *needsWriteback = nullptr;
        if (compareFinalValue) {
            finalValue = new LoadInst(slot, check);
            // Reloading here avoids keeping %init live across the whole loop.
            // Alias safety guarantees the original cell is unchanged until this
            // writeback edge, so this is the same value loaded in the preheader.
            auto *initialValue = new LoadInst(best->ptr, check);
            needsWriteback = new ICmpInst(ICmpInst::ICMP_NE, finalValue,
                                          initialValue, check);
        } else {
            auto *dirty = new LoadInst(dirtySlot, check);
            needsWriteback = new ICmpInst(ICmpInst::ICMP_NE, dirty,
                                           new ConstantInt(func->parent_->int1_ty_, 0), check);
        }
        new BranchInst(needsWriteback, writeback, exit, check);

        if (!finalValue)
            finalValue = new LoadInst(slot, writeback);
        new StoreInst(finalValue, best->ptr, writeback);
        new BranchInst(exit, writeback);
    }

    func->invalidateDominatorInfo();
    return true;
}
