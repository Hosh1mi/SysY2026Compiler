#include "../../include/mid/opt/loopDeletion.hpp"
#include "../../include/mid/ir/instruction.hpp"

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
