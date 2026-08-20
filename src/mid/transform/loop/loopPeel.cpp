/**
 * @file loopPeel.cpp
 * @brief 循环剥离：克隆并剥离规范循环的第一次迭代，同时维护侧退出和退出 PHI。
 * @details 提交前预检所有待克隆指令和值映射；剥离后分别维护头部退出、侧退出和 LCSSA PHI 入边。
 */

// LoopPeel — peel the first iteration of a canonical 2-BB loop while keeping
// both the header exit and the latch side-exit.  Trivial-phi / icmp folding /
// block deletion are left to the subsequent DCE / InstCombine / CFGSimplify
// cleanup in the driver pipeline.

#include "../../../include/mid/opt/loopPeel.hpp"
#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/transform/loopCloneUtils.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using loop_clone::addRemappedIncomingForClonedEdge;
using loop_clone::cloneInstruction;
using loop_clone::incomingFrom;
using loop_clone::remapValueOrInvariant;
using loop_clone::removeIncomingFrom;

/**
 * @brief 原地执行 replaceTerminatorTarget 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param term 参数 `term`，用于本函数的分析、匹配或 IR 构造。
 * @param opIdx 参数 `opIdx`，用于本函数的分析、匹配或 IR 构造。
 * @param oldTarget 需要替换的原分支目标。
 * @param newTarget 替换后的新分支目标。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void replaceTerminatorTarget(Instruction *term, unsigned opIdx,
                             BasicBlock *oldTarget, BasicBlock *newTarget) {
    if (term->get_operand(opIdx) != oldTarget)
        return;
    term->set_operand(opIdx, newTarget);
}

/**
 * @brief 判断 isSupportedCloneInst 所描述的结构、合法性或安全条件是否成立。
 * @param inst 待分析、化简或克隆的指令。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isSupportedCloneInst(Instruction *inst) {
    if (inst->is_phi() || inst->isTerminator() || inst->is_alloca())
        return false;
    return dynamic_cast<BinaryInst *>(inst) ||
           dynamic_cast<UnaryInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst) ||
           dynamic_cast<FCmpInst *>(inst) ||
           dynamic_cast<SelectInst *>(inst) ||
           dynamic_cast<GetElementPtrInst *>(inst) ||
           dynamic_cast<LoadInst *>(inst) ||
           dynamic_cast<StoreInst *>(inst) ||
           dynamic_cast<ZextInst *>(inst) ||
           dynamic_cast<FpToSiInst *>(inst) ||
           dynamic_cast<SiToFpInst *>(inst) ||
           dynamic_cast<Bitcast *>(inst) ||
           dynamic_cast<CallInst *>(inst);
}

/**
 * @brief 判断 hasOnlyLCSSAOutsideUses 所描述的结构、合法性或安全条件是否成立。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool hasOnlyLCSSAOutsideUses(Loop &loop) {
    auto isExitBlock = [&](BasicBlock *bb) {
        for (auto *exit : loop.exits) {
            if (exit == bb)
                return true;
        }
        return false;
    };

    for (auto *bb : loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            for (auto &use : inst->use_list_) {
                auto *user = use.user_;
                if (!user || !user->parent_)
                    continue;
                if (loop.isInLoop(user->parent_))
                    continue;
                if (!user->is_phi() || !isExitBlock(user->parent_))
                    return false;
            }
        }
    }
    return true;
}

/**
 * @brief 实现 willCreateTrivialHeaderPhi 对应的局部分析或变换辅助逻辑。
 * @param loop 待检查或变换的循环。
 * @param preheader 循环预头基本块。
 * @param latch 循环回边基本块。
 * @param headerPhis 参数 `headerPhis`，用于本函数的分析、匹配或 IR 构造。
 * @param loopBlocks 循环所包含的基本块集合。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool willCreateTrivialHeaderPhi(
    Loop &loop, BasicBlock *preheader, BasicBlock *latch,
    const std::vector<PhiInst *> &headerPhis,
    const std::unordered_set<BasicBlock *> &loopBlocks) {
    std::unordered_map<Value *, Value *> seed;
    for (auto *phi : headerPhis) {
        Value *init = incomingFrom(phi, preheader);
        if (!init)
            return false;
        seed[phi] = init;
    }

    for (auto *phi : headerPhis) {
        Value *next = incomingFrom(phi, latch);
        if (!next)
            continue;
        // 收益预判：循环内定义的 latch 入值在剥离后会成为新的 SSA 值，不可能与
        // next 相同；只有循环不变量会映射回自身，从而让剩余 header PHI 退化。
        Value *peeled = remapValueOrInvariant(next, seed, loopBlocks);
        if (peeled && peeled == next)
            return true;
    }
    return false;
}

/**
 * @brief 判断 canRemap 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param valueMap 从原 IR 值到克隆值的映射表。
 * @param loopBlocks 循环所包含的基本块集合。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool canRemap(Value *value,
              const std::unordered_map<Value *, Value *> &valueMap,
              const std::unordered_set<BasicBlock *> &loopBlocks) {
    return remapValueOrInvariant(value, valueMap, loopBlocks) != nullptr;
}

/**
 * @brief 实现 preflightClone 对应的局部分析或变换辅助逻辑。
 * @param loop 待检查或变换的循环。
 * @param header 循环头基本块。
 * @param latch 循环回边基本块。
 * @param headerExit 参数 `headerExit`，用于本函数的分析、匹配或 IR 构造。
 * @param latchExit 参数 `latchExit`，用于本函数的分析、匹配或 IR 构造。
 * @param headerPhis 参数 `headerPhis`，用于本函数的分析、匹配或 IR 构造。
 * @param loopBlocks 循环所包含的基本块集合。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool preflightClone(Loop &loop, BasicBlock *header, BasicBlock *latch,
                    BasicBlock *headerExit, BasicBlock *latchExit,
                    const std::vector<PhiInst *> &headerPhis,
                    const std::unordered_set<BasicBlock *> &loopBlocks) {
    std::unordered_map<Value *, Value *> valueMap;
    for (auto *phi : headerPhis)
        valueMap[phi] = incomingFrom(phi, loop.preheader);

    for (auto *bb : {header, latch}) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator())
                continue;
            for (unsigned i = 0; i < inst->num_ops(); ++i)
                if (!canRemap(inst->get_operand(i), valueMap, loopBlocks))
                    return false;
            // 用原指令占位模拟克隆结果，使后续操作数按真实克隆时的 SSA 定义顺序
            // 接受检查；这样可以在创建任何基本块前发现无法映射的依赖。
            valueMap.emplace(inst, inst);
        }
    }

    if (!canRemap(header->get_terminator()->get_operand(0), valueMap,
                  loopBlocks) ||
        !canRemap(latch->get_terminator()->get_operand(0), valueMap,
                  loopBlocks))
        return false;
    for (auto *exit : {headerExit, latchExit}) {
        for (auto *inst : exit->instr_list_) {
            if (!inst->is_phi())
                break;
            auto *phi = static_cast<PhiInst *>(inst);
            BasicBlock *pred = exit == headerExit ? header : latch;
            Value *incoming = incomingFrom(phi, pred);
            if (incoming && !canRemap(incoming, valueMap, loopBlocks))
                return false;
        }
    }
    return true;
}

/**
 * @brief 尝试执行 PeelLoop 匹配或变换；前置条件不满足时不提交改写。
 * @param loop 待检查或变换的循环。
 * @param func 待分析或改写的函数。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool tryPeelLoop(Loop &loop, Function *func, Module *module) {
    // 剥离前完整预检两块循环、可克隆指令、LCSSA 外部使用和值映射。
    // 预检通过后才复制首轮 header/latch，并分别修复头部退出、侧退出和 PHI 入边。
    if (loop.blocks.size() != 2)
        return false;
    BasicBlock *header = loop.header;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *preheader = loop.preheader;
    if (!header || !latch || !preheader || latch == header)
        return false;

    auto *headerBr = dynamic_cast<BranchInst *>(header->get_terminator());
    auto *latchBr = dynamic_cast<BranchInst *>(latch->get_terminator());
    if (!headerBr || headerBr->num_ops() != 3)
        return false;
    if (!latchBr || latchBr->num_ops() != 3)
        return false;

    auto *headerTrue = dynamic_cast<BasicBlock *>(headerBr->get_operand(1));
    auto *headerFalse = dynamic_cast<BasicBlock *>(headerBr->get_operand(2));
    if (!headerTrue || !headerFalse)
        return false;

    BasicBlock *headerExit = nullptr;
    if (headerTrue == latch && !loop.isInLoop(headerFalse))
        headerExit = headerFalse;
    else if (headerFalse == latch && !loop.isInLoop(headerTrue))
        headerExit = headerTrue;
    else
        return false;

    auto *latchTrue = dynamic_cast<BasicBlock *>(latchBr->get_operand(1));
    auto *latchFalse = dynamic_cast<BasicBlock *>(latchBr->get_operand(2));
    if (!latchTrue || !latchFalse)
        return false;

    BasicBlock *latchExit = nullptr;
    if (latchTrue == header && !loop.isInLoop(latchFalse))
        latchExit = latchFalse;
    else if (latchFalse == header && !loop.isInLoop(latchTrue))
        latchExit = latchTrue;
    else
        return false;

    std::vector<PhiInst *> headerPhis;
    for (auto *inst : header->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (!incomingFrom(phi, preheader) || !incomingFrom(phi, latch))
            return false;
        // Exactly those two incomings.
        if (phi->num_ops() != 4)
            return false;
        headerPhis.push_back(phi);
    }
    if (headerPhis.empty())
        return false;

    std::unordered_set<BasicBlock *> loopBlocks(loop.blocks.begin(),
                                                loop.blocks.end());

    for (auto *bb : {header, latch}) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator())
                continue;
            if (!isSupportedCloneInst(inst))
                return false;
        }
    }

    if (!hasOnlyLCSSAOutsideUses(loop))
        return false;

    if (!willCreateTrivialHeaderPhi(loop, preheader, latch, headerPhis,
                                    loopBlocks))
        return false;

    if (!preflightClone(loop, header, latch, headerExit, latchExit,
                        headerPhis, loopBlocks))
        return false;

    // ── 提交变换：以下步骤依赖上面的完整预检，正常情况下不再失败 ─────────
    auto *peeledHeader = new BasicBlock(module, "peeled.header", func);
    auto *peeledLatch = new BasicBlock(module, "peeled.latch", func);

    std::unordered_map<Value *, Value *> valueMap;
    for (auto *phi : headerPhis)
        valueMap[phi] = incomingFrom(phi, preheader);

    // PHI 已由首轮初值替代，因此这里只克隆 header 的普通指令，并同步建立值映射。
    for (auto *inst : header->instr_list_) {
        if (inst->is_phi() || inst->isTerminator())
            continue;
        if (!cloneInstruction(inst, peeledHeader, valueMap, loopBlocks))
            return false;
    }

    // 克隆 header 终结指令：循环内分支改指向 peeledLatch，退出边保持原出口。
    {
        Value *cond = remapValueOrInvariant(headerBr->get_operand(0), valueMap,
                                            loopBlocks);
        if (!cond)
            return false;
        auto mapHeaderSucc = [&](BasicBlock *succ) -> BasicBlock * {
            if (succ == latch)
                return peeledLatch;
            if (succ == headerExit)
                return headerExit;
            return nullptr;
        };
        BasicBlock *tSucc = mapHeaderSucc(headerTrue);
        BasicBlock *fSucc = mapHeaderSucc(headerFalse);
        if (!tSucc || !fSucc)
            return false;
        new BranchInst(cond, tSucc, fSucc, peeledHeader);
    }

    // 继续克隆 latch 主体；它会使用刚建立的 header 克隆值和已有循环不变量。
    for (auto *inst : latch->instr_list_) {
        if (inst->isTerminator())
            continue;
        if (inst->is_phi())
            return false;
        if (!cloneInstruction(inst, peeledLatch, valueMap, loopBlocks))
            return false;
    }

    // peeledLatch 的回边直接进入原 header，表示首轮执行完毕后接回原循环；
    // 侧退出仍进入原 latchExit，以保持首轮提前退出语义。
    {
        Value *cond = remapValueOrInvariant(latchBr->get_operand(0), valueMap,
                                            loopBlocks);
        if (!cond)
            return false;
        BasicBlock *tSucc = latchTrue == header ? header : latchExit;
        BasicBlock *fSucc = latchFalse == header ? header : latchExit;
        new BranchInst(cond, tSucc, fSucc, peeledLatch);
    }

    // 趁原 CFG 入边仍可查询，先为两条克隆退出边补齐出口 PHI；若先改 header PHI
    // 或 preheader，就会丢失用于查找原入值的前驱关系。
    for (auto *inst : headerExit->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (!incomingFrom(phi, header))
            continue;
        if (!addRemappedIncomingForClonedEdge(phi, header, peeledHeader,
                                              valueMap, loopBlocks))
            return false;
    }
    for (auto *inst : latchExit->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (!incomingFrom(phi, latch))
            continue;
        if (!addRemappedIncomingForClonedEdge(phi, latch, peeledLatch,
                                              valueMap, loopBlocks))
            return false;
    }

    // 原循环不再由 preheader 直接进入：将 header PHI 的初值边替换成剥离首轮后
    // peeledLatch 产生的状态，使后续迭代从“第二轮”开始。
    for (auto *phi : headerPhis) {
        Value *latchIncoming = incomingFrom(phi, latch);
        Value *peeledIncoming =
            remapValueOrInvariant(latchIncoming, valueMap, loopBlocks);
        if (!peeledIncoming)
            return false;
        removeIncomingFrom(phi, preheader);
        phi->addIncoming(peeledIncoming, peeledLatch);
    }

    // 最后才重定向入口边，正式把 peeled.header 接入可达 CFG。
    auto *preBr = preheader->get_terminator();
    if (!preBr || !preBr->is_br())
        return false;
    for (unsigned i = 0; i < preBr->num_ops(); ++i) {
        if (preBr->get_operand(i) == header) {
            replaceTerminatorTarget(preBr, i, header, peeledHeader);
            preheader->remove_succ_basic_block(header);
            header->remove_pre_basic_block(preheader);
            preheader->add_succ_basic_block(peeledHeader);
            peeledHeader->add_pre_basic_block(preheader);
            break;
        }
    }

    if (std::getenv("DEBUG_LOOP_PEEL"))
        std::cerr << "[LoopPeel] func=" << func->name_
                  << " header=" << header->name_
                  << " peeled\n";

    func->set_instr_name();
    return true;
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool runOnFunction(Function *func, Module *module) {
    bool changed = false;
    LoopInfo LI;
    LI.analyze(func);

    std::vector<Loop *> loops;
    for (auto &l : LI.allLoops())
        loops.push_back(l.get());
    // Innermost first; each loop is considered once to keep this pass
    // bounded even when peeling leaves another structurally similar loop.
    std::sort(loops.begin(), loops.end(),
              [](Loop *a, Loop *b) { return a->depth > b->depth; });

    for (auto *loop : loops) {
        if (tryPeelLoop(*loop, func, module))
            changed = true;
    }
    return changed;
}

} // namespace

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopPeel::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func, module);
    }
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses LoopPeel::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, module);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
