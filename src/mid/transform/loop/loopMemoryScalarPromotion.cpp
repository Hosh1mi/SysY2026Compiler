/**
 * @file loopMemoryScalarPromotion.cpp
 * @brief 循环内存标量化：把循环内对不变地址的反复读写提升为标量 SSA 状态，并在边界同步内存。
 * @details 仅提升循环不变且无冲突别名的地址；预头加载、循环内标量更新和所有出口回写必须成套生成。
 */

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

/**
 * @brief 判断 isScalarType 所描述的结构、合法性或安全条件是否成立。
 * @param ty 相关 IR 类型。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isScalarType(Type *ty) {
    return ty && (ty->tid_ == Type::IntegerTyID || ty->tid_ == Type::FloatTyID);
}

/**
 * @brief 判断 isLoopInvariant 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isLoopInvariant(Value *value, const Loop &loop) {
    if (!value) return false;
    if (dynamic_cast<Constant *>(value) || dynamic_cast<Argument *>(value) ||
        dynamic_cast<GlobalVariable *>(value))
        return true;

    auto *inst = dynamic_cast<Instruction *>(value);
    return !inst || !loop.blocks.count(inst->parent_);
}

/**
 * @brief 实现 exitsAreDominatedByPreheader 对应的局部分析或变换辅助逻辑。
 * @param loop 待检查或变换的循环。
 * @param DT 参数 `DT`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool exitsAreDominatedByPreheader(const Loop &loop,
                                  const DominatorTreeAnalysis &DT) {
    Function *func = loop.header ? loop.header->parent_ : nullptr;
    if (!func || !loop.preheader)
        return false;
    for (auto *exit : loop.exits) {
        if (!DT.dominates(loop.preheader, exit))
            return false;
    }
    return true;
}

/**
 * @brief 记录循环中可提升到标量 PHI 的固定地址及其精确访问次数。
 */
struct Candidate {
    Value *ptr = nullptr;      ///< 在循环内保持不变的候选内存地址。
    Type *elemTy = nullptr;    ///< 该地址所指向的标量元素类型。
    int exactLoads = 0;        ///< 直接从该地址读取的 load 数量。
    int exactStores = 0;       ///< 直接写入该地址的 store 数量。
};

/**
 * @brief 判断 hasDedicatedPhiFreeExits 所描述的结构、合法性或安全条件是否成立。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
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

/**
 * @brief 原地执行 replaceBranchTarget 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param pred 前驱基本块。
 * @param oldTarget 需要替换的原分支目标。
 * @param newTarget 替换后的新分支目标。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void replaceBranchTarget(BasicBlock *pred, BasicBlock *oldTarget,
                         BasicBlock *newTarget) {
    auto *term = pred->get_terminator();
    if (!term || !term->is_br())
        return;
    for (unsigned i = 0; i < term->num_ops(); ++i) {
        if (term->get_operand(i) == oldTarget)
            term->set_operand(i, newTarget);
    }
}

} // namespace

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopMemoryScalarPromotion::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses LoopMemoryScalarPromotion::execute(Module *module,
                                                     AnalysisManager &AM) {
    BasicAliasAnalysis BAA;
    BAA.analyze(module);

    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, BAA, AM);
    }

    if (changed) {
        Mem2Reg mem2reg;
        mem2reg.execute(module, AM);
    }

    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @param BAA 参数 `BAA`，用于本函数的分析、匹配或 IR 构造。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopMemoryScalarPromotion::runOnFunction(Function *func,
                                              const BasicAliasAnalysis &BAA,
                                              AnalysisManager &AM) {
    bool changed = false;

    for (int iter = 0; iter < 32; ++iter) {
        LoopInfo &LI = AM.getLoopInfo(func);
        DominatorTreeAnalysis &DT = AM.getDominatorTree(func);
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
            if (tryPromote(*loop, BAA, DT)) {
                iterChanged = true;
                changed = true;
                AM.clear(func);
                break;
            }
        }

        if (!iterChanged)
            break;
    }

    return changed;
}

/**
 * @brief 尝试执行 Promote 匹配或变换；前置条件不满足时不提交改写。
 * @param loop 待检查或变换的循环。
 * @param BAA 参数 `BAA`，用于本函数的分析、匹配或 IR 构造。
 * @param DT 参数 `DT`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopMemoryScalarPromotion::tryPromote(Loop &loop,
                                           const BasicAliasAnalysis &BAA,
                                           const DominatorTreeAnalysis &DT) {
    // 第一遍按不变地址聚合 load/store，第二遍用 ModRef 排除别名写入并选择收益最大候选。
    // 最终以“预头加载—循环内标量槽—所有出口回写”成套替换，不能遗漏任一退出边。
    if (!loop.preheader || loop.exits.empty() ||
        !exitsAreDominatedByPreheader(loop, DT) || !hasDedicatedPhiFreeExits(loop))
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
            // 写回边重新加载初值，可避免让 preheader 的 %init 跨整个循环
            // 保持活跃。别名检查已证明原单元此前未被其他指令修改。
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

    return true;
}
