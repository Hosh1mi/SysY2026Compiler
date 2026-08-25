/**
 * @file loopDeletion.cpp
 * @brief 循环删除：证明循环无可观察副作用且退出值可替代后删除整个无用循环。
 * @details 只有循环体无可观察副作用、退出唯一且所有活跃值可替代时才断开回边并交给后续 CFG 清理。
 */

#include "../../../include/mid/opt/loopDeletion.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopDeletion::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses LoopDeletion::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopDeletion::runOnFunction(Function *func) {
    if (func->basic_blocks_.empty())
        return false;

    bool changed = false;
    bool progress = true;
    while (progress) {
        progress = false;

        LoopInfo LI;
        LI.analyze(func);

        // 由内向外：内层删掉后外层可能跟着变"空"，固定点迭代覆盖
        std::vector<Loop *> loops;
        for (auto &l : LI.allLoops())
            loops.push_back(l.get());
        std::sort(loops.begin(), loops.end(),
                  [](Loop *a, Loop *b) { return a->depth > b->depth; });

        for (auto *loop : loops) {
            if (tryDelete(*loop, func)) {
                changed = true;
                progress = true;
                func->set_instr_name();
                break; // LoopInfo 已过期，重新分析
            }
            if (tryBreakSingleIterationBackedge(*loop, func)) {
                changed = true;
                progress = true;
                func->set_instr_name();
                break;
            }
        }
    }
    return changed;
}

/**
 * @brief 尝试执行 Delete 匹配或变换；前置条件不满足时不提交改写。
 * @param loop 待检查或变换的循环。
 * @param func 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopDeletion::tryDelete(Loop &loop, Function *func) {
    // 1. 结构要求：真 preheader（单后继无条件 br）+ 单一 exit
    BasicBlock *preheader = loop.preheader;
    if (!preheader)
        return false;
    auto *preTerm = preheader->get_terminator();
    if (!preTerm || !preTerm->is_br() || preTerm->num_ops() != 1 ||
        preTerm->get_operand(0) != loop.header)
        return false;
    BasicBlock *exit = loop.singleExit();
    if (!exit)
        return false;

    // 2. 可证终止：规范 IV（init=0, +1, header slt 不变上界）。
    //    旋转过的循环 cond 在 latch，analyzeIV 不会识别——天然排除。
    if (!loop.hasCanonicalIV())
        return false;

    // 3/4. 无副作用；循环内定义无任何循环外使用
    for (auto *bb : loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_store() || inst->is_call())
                return false;
            for (auto &use : inst->use_list_) {
                auto *user = use.user_;
                if (user && user->parent_ && !loop.isInLoop(user->parent_))
                    return false;
            }
        }
    }

    // 5. exit phi 中来自循环的入边：值必须循环不变且彼此相等
    //    （条件 4 已保证不是循环内定义；这里只需"全相等"以便收拢成
    //    一条来自 preheader 的入边）
    for (auto *inst : exit->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        Value *fromLoop = nullptr;
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (!pred || !loop.isInLoop(pred)) continue;
            Value *val = phi->get_operand(i);
            if (fromLoop && fromLoop != val)
                return false;
            fromLoop = val;
        }
    }

    // ── 变换 ────────────────────────────────────────────────────────────
    if (std::getenv("DEBUG_LOOP_DELETION"))
        std::cerr << "[LoopDeletion] delete func=" << func->name_
                  << " header=" << loop.header->name_
                  << " blocks=" << loop.blocks.size() << "\n";

    // exit phi：循环入边收拢为一条来自 preheader 的入边
    for (auto *inst : exit->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        Value *fromLoop = nullptr;
        for (int i = (int)phi->num_ops() - 2; i >= 0; i -= 2) {
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (!pred || !loop.isInLoop(pred)) continue;
            fromLoop = phi->get_operand(i);
            phi->remove_operands(i, i + 1);
        }
        if (fromLoop)
            phi->addIncoming(fromLoop, preheader);
    }

    // preheader 直跳 exit
    preTerm->set_operand(0, exit);
    preheader->remove_succ_basic_block(loop.header);
    loop.header->remove_pre_basic_block(preheader);
    preheader->add_succ_basic_block(exit);
    exit->add_pre_basic_block(preheader);

    // 移除循环块：先摘空指令（delete_instr 只解 use 链不释放），再删块
    //（remove_bb 会清理两侧残余 CFG 链接）。参照 CFGSimplify
    // removeDeadBlocks 的既有模式。
    std::vector<BasicBlock *> doomed(loop.blocksOrdered);
    for (auto *bb : doomed) {
        std::vector<Instruction *> insts(bb->instr_list_.begin(),
                                         bb->instr_list_.end());
        for (auto *inst : insts)
            bb->delete_instr(inst);
    }
    for (auto *bb : doomed)
        func->remove_bb(bb);

    return true;
}

/**
 * @brief 尝试执行 BreakSingleIterationBackedge 匹配或变换；前置条件不满足时不提交改写。
 * @param loop 待检查或变换的循环。
 * @param func 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopDeletion::tryBreakSingleIterationBackedge(Loop &loop,
                                                    Function *func) {
    // 仅处理规范两块 while：preheader→header→latch→header，且 header→exit。
    // 若 latch 自己还有条件分支，就不能把它当作确定的单次执行路径。
    BasicBlock *preheader = loop.preheader;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *exit = loop.singleExit();
    if (!preheader || !latch || !exit || latch == loop.header ||
        loop.blocks.size() != 2 || !loop.isInLoop(latch) ||
        loop.exiting.size() != 1 || loop.exiting.front() != loop.header)
        return false;

    auto *preheaderBr = preheader->get_terminator();
    auto *headerBr = loop.header->get_terminator();
    auto *latchBr = latch->get_terminator();
    if (!preheaderBr || !preheaderBr->is_br() ||
        preheaderBr->num_ops() != 1 ||
        preheaderBr->get_operand(0) != loop.header ||
        !headerBr || !headerBr->is_br() || headerBr->num_ops() != 3 ||
        headerBr->get_operand(1) != latch ||
        headerBr->get_operand(2) != exit ||
        !latchBr || !latchBr->is_br() || latchBr->num_ops() != 1 ||
        latchBr->get_operand(0) != loop.header)
        return false;

    // LoopInfo 已证明初值为 0、步长为 +1，且 header 使用有符号小于比较；
    // 因而常量上界 1 能严格证明循环体恰好执行一次。
    if (!loop.hasCanonicalIV()) return false;
    auto *bound = dynamic_cast<ConstantInt *>(loop.tripCount);
    auto *compare = dynamic_cast<ICmpInst *>(headerBr->get_operand(0));
    if (!bound || bound->value_ != 1 || !compare ||
        compare->icmp_op_ != ICmpInst::ICMP_SLT ||
        compare->get_operand(0) != loop.canonicalIV ||
        compare->get_operand(1) != bound)
        return false;
    for (const auto &use : compare->use_list_) {
        if (use.user_ != headerBr) return false;
    }

    /**
     * @brief 保存单次迭代循环头 PHI 在进入前与执行后的两个可选值。
     */
    struct PhiInfo {
        PhiInst *phi = nullptr;             ///< 待替换并随循环删除的循环头 PHI。
        Value *initial = nullptr;           ///< 来自循环预头的零次迭代值。
        Value *afterIteration = nullptr;    ///< 来自回边的一次迭代后值。
    };
    std::vector<PhiInfo> phis;
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->num_ops() != 4) return false;
        Value *initial = nullptr;
        Value *afterIteration = nullptr;
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred == preheader)
                initial = phi->get_operand(i);
            else if (pred == latch)
                afterIteration = phi->get_operand(i);
            else
                return false;
        }
        if (!initial || !afterIteration) return false;
        phis.push_back({phi, initial, afterIteration});
    }

    // 限制 PHI 之间不能直接互相引用，使后续同步替换保持简单且满足支配关系；
    // 正常归纳/归约更新应由 latch 指令产生，而不是直接引用另一个 header PHI。
    for (const auto &info : phis) {
        for (const auto &other : phis) {
            if (info.initial == other.phi ||
                info.afterIteration == other.phi)
                return false;
        }
    }

    // LoopDeletion 前已建立 LCSSA；header PHI 的循环外使用必须是对应退出边
    // 上的 exit PHI，才能把该 incoming 安全迁移到 latch。
    for (const auto &info : phis) {
        for (const auto &use : info.phi->use_list_) {
            auto *user = use.user_;
            if (!user || !user->parent_ || loop.isInLoop(user->parent_))
                continue;
            auto *exitPhi = dynamic_cast<PhiInst *>(user);
            if (!exitPhi || exitPhi->parent_ != exit ||
                use.operand_index_ + 1 >= exitPhi->num_ops() ||
                exitPhi->get_operand(use.operand_index_ + 1) != loop.header)
                return false;
        }
    }

    if (std::getenv("DEBUG_LOOP_DELETION"))
        std::cerr << "[LoopDeletion] break-single-iteration-backedge func="
                  << func->name_ << " header=" << loop.header->name_ << "\n";

    // 唯一一次循环体执行期间，header PHI 等于 preheader 初值；执行结束后，
    // 循环外可见的终值则是 latch 入值，因此内外 use 必须分别替换。
    for (const auto &info : phis) {
        std::vector<std::pair<Instruction *, unsigned>> insideUses;
        std::vector<std::pair<Instruction *, unsigned>> outsideUses;
        for (const auto &use : info.phi->use_list_) {
            auto *user = use.user_;
            if (!user || !user->parent_ || user == compare || user == headerBr)
                continue;
            auto entry = std::make_pair(user, use.operand_index_);
            if (loop.isInLoop(user->parent_))
                insideUses.push_back(entry);
            else
                outsideUses.push_back(entry);
        }
        for (const auto &[user, operand] : insideUses)
            user->set_operand(operand, info.initial);
        for (const auto &[user, operand] : outsideUses)
            user->set_operand(operand, info.afterIteration);
    }

    // 退出边由 header 移到 latch；header 中定义的普通值仍支配 latch，而 PHI 的
    // live-out 已在上面改写，所以此时只需更新出口 PHI 的前驱块。
    for (auto *inst : exit->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            if (phi->get_operand(i + 1) == loop.header)
                phi->set_operand(i + 1, latch);
        }
    }

    loop.header->delete_instr(headerBr);
    loop.header->delete_instr(compare);
    loop.header->remove_succ_basic_block(exit);
    exit->remove_pre_basic_block(loop.header);
    new BranchInst(latch, loop.header);

    latch->delete_instr(latchBr);
    latch->remove_succ_basic_block(loop.header);
    loop.header->remove_pre_basic_block(latch);
    new BranchInst(exit, latch);

    for (const auto &info : phis)
        loop.header->delete_instr(info.phi);

    return true;
}
