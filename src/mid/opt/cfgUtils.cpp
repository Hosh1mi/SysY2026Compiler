// CFG 公共工具实现。removeUnreachableBlocks 与 sccp.cpp 中的
// static 版本同源；此处为共享导出版，供改写 CFG 的 pass 复用。

#include "../../include/mid/opt/cfgUtils.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/basicBlock.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <queue>
#include <set>
#include <vector>

namespace {

// 在 succ 的所有 phi 中删除 deadBlock 对应的入边
void removeBBFromPhi(BasicBlock *deadBlock, BasicBlock *succ) {
    for (auto *instr : succ->instr_list_) {
        if (!instr->is_phi()) continue;
        auto *phi = static_cast<PhiInst *>(instr);
        for (int i = phi->num_ops_ - 1; i >= 0; i -= 2) {
            if (phi->get_operand(i) == deadBlock)
                phi->remove_operands(i - 1, i);
        }
    }
}

} // namespace

void removeUnreachableBlocks(Function *func) {
    if (func->basic_blocks_.empty()) return;
    auto *entry = func->basic_blocks_.front();

    // 可达性沿 terminator 的基本块操作数传播，不走 succ_bbs_：
    // 个别 pass 改写分支目标时遗漏维护 succ/pre 链表，terminator 才是事实。
    std::set<BasicBlock *> reachable;
    std::queue<BasicBlock *> worklist;
    reachable.insert(entry);
    worklist.push(entry);
    while (!worklist.empty()) {
        auto *bb = worklist.front();
        worklist.pop();
        auto *term = bb->get_terminator();
        if (!term) continue;
        for (unsigned i = 0; i < term->num_ops_; ++i) {
            auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
            if (succ && reachable.insert(succ).second)
                worklist.push(succ);
        }
    }

    std::vector<BasicBlock *> dead;
    for (auto *bb : func->basic_blocks_) {
        if (!reachable.count(bb))
            dead.push_back(bb);
    }
    if (dead.empty()) return;

    for (auto *bb : dead) {
        auto *term = bb->get_terminator();
        if (term) {
            for (unsigned i = 0; i < term->num_ops_; ++i) {
                auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
                if (succ && reachable.count(succ)) {
                    removeBBFromPhi(bb, succ);
                    succ->remove_pre_basic_block(bb);
                }
            }
        }
        std::vector<Instruction *> instrs(bb->instr_list_.begin(), bb->instr_list_.end());
        for (auto *instr : instrs)
            bb->delete_instr(instr);
    }

    // 清掉存活块中残留的指向死块的链接，再统一移除死块。
    // 死块自身的链表先清空，避免 remove_bb 解引用其中可能已失效的指针。
    std::set<BasicBlock *> deadSet(dead.begin(), dead.end());
    for (auto *bb : func->basic_blocks_) {
        if (deadSet.count(bb)) continue;
        auto isDead = [&](BasicBlock *b) { return deadSet.count(b) > 0; };
        bb->pre_bbs_.erase(
            std::remove_if(bb->pre_bbs_.begin(), bb->pre_bbs_.end(), isDead),
            bb->pre_bbs_.end());
        bb->succ_bbs_.erase(
            std::remove_if(bb->succ_bbs_.begin(), bb->succ_bbs_.end(), isDead),
            bb->succ_bbs_.end());
    }
    for (auto *bb : dead) {
        bb->pre_bbs_.clear();
        bb->succ_bbs_.clear();
        func->remove_bb(bb);
    }
}
