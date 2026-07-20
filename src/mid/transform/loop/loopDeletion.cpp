#include "../../../include/mid/opt/loopDeletion.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

void LoopDeletion::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

PreservedAnalyses LoopDeletion::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

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

bool LoopDeletion::tryDelete(Loop &loop, Function *func) {
    // 1. 结构要求：真 preheader（单后继无条件 br）+ 单一 exit
    BasicBlock *preheader = loop.preheader;
    if (!preheader)
        return false;
    auto *preTerm = preheader->get_terminator();
    if (!preTerm || !preTerm->is_br() || preTerm->num_ops_ != 1 ||
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
                auto *user = dynamic_cast<Instruction *>(use.val_);
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
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
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
        for (int i = (int)phi->num_ops_ - 2; i >= 0; i -= 2) {
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

bool LoopDeletion::tryBreakSingleIterationBackedge(Loop &loop,
                                                    Function *func) {
    // Restrict the rewrite to the canonical two-block while form:
    // preheader -> header -> latch -> header, with header -> exit.  In
    // particular, an independently conditional latch must not be replaced.
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
        preheaderBr->num_ops_ != 1 ||
        preheaderBr->get_operand(0) != loop.header ||
        !headerBr || !headerBr->is_br() || headerBr->num_ops_ != 3 ||
        headerBr->get_operand(1) != latch ||
        headerBr->get_operand(2) != exit ||
        !latchBr || !latchBr->is_br() || latchBr->num_ops_ != 1 ||
        latchBr->get_operand(0) != loop.header)
        return false;

    // LoopInfo proves init=0, step=+1 and a signed-less-than header guard.
    // A constant bound of one therefore proves exactly one body execution.
    if (!loop.hasCanonicalIV()) return false;
    auto *bound = dynamic_cast<ConstantInt *>(loop.tripCount);
    auto *compare = dynamic_cast<ICmpInst *>(headerBr->get_operand(0));
    if (!bound || bound->value_ != 1 || !compare ||
        compare->icmp_op_ != ICmpInst::ICMP_SLT ||
        compare->get_operand(0) != loop.canonicalIV ||
        compare->get_operand(1) != bound)
        return false;
    for (const auto &use : compare->use_list_) {
        if (use.val_ != headerBr) return false;
    }

    struct PhiInfo {
        PhiInst *phi = nullptr;
        Value *initial = nullptr;
        Value *afterIteration = nullptr;
    };
    std::vector<PhiInfo> phis;
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->num_ops_ != 4) return false;
        Value *initial = nullptr;
        Value *afterIteration = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
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

    // Keep the simultaneous substitution simple and dominance-safe.  Normal
    // induction/reduction updates are instructions in the latch, not direct
    // references to another header phi.
    for (const auto &info : phis) {
        for (const auto &other : phis) {
            if (info.initial == other.phi ||
                info.afterIteration == other.phi)
                return false;
        }
    }

    // LCSSA is established before LoopDeletion.  Require every outside use
    // of a header phi to be an exit phi on the edge that will move to latch.
    for (const auto &info : phis) {
        for (const auto &use : info.phi->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user || !user->parent_ || loop.isInLoop(user->parent_))
                continue;
            auto *exitPhi = dynamic_cast<PhiInst *>(user);
            if (!exitPhi || exitPhi->parent_ != exit ||
                use.arg_no_ + 1 >= exitPhi->num_ops_ ||
                exitPhi->get_operand(use.arg_no_ + 1) != loop.header)
                return false;
        }
    }

    if (std::getenv("DEBUG_LOOP_DELETION"))
        std::cerr << "[LoopDeletion] break-single-iteration-backedge func="
                  << func->name_ << " header=" << loop.header->name_ << "\n";

    // During the only body execution, every header phi has its preheader
    // value.  After that execution, its live-out value is the latch incoming.
    for (const auto &info : phis) {
        std::vector<std::pair<Instruction *, unsigned>> insideUses;
        std::vector<std::pair<Instruction *, unsigned>> outsideUses;
        for (const auto &use : info.phi->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user || !user->parent_ || user == compare || user == headerBr)
                continue;
            auto entry = std::make_pair(user, use.arg_no_);
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

    // The exiting edge moves from header to latch.  Values defined in header
    // still dominate latch; phi live-outs were rewritten above.
    for (auto *inst : exit->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
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
