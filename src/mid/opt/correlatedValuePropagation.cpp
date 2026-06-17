#include "../../include/mid/opt/correlatedValuePropagation.hpp"
#include "../../include/mid/opt/branchFactUtils.hpp"
#include "../../include/mid/ir/constant.hpp"

#include <vector>

namespace {

void removeIncomingFromPred(BasicBlock *succ, BasicBlock *pred) {
    for (auto *inst : succ->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (int i = static_cast<int>(phi->num_ops_) - 1; i >= 1; i -= 2) {
            if (phi->get_operand(i) == pred)
                phi->remove_operands(i - 1, i);
        }
    }
}

void replaceBranchWithUncond(BasicBlock *bb, BasicBlock *target) {
    auto *br = dynamic_cast<BranchInst *>(bb->get_terminator());
    if (!br || br->num_ops_ != 3) return;

    auto *trueDest = static_cast<BasicBlock *>(br->get_operand(1));
    auto *falseDest = static_cast<BasicBlock *>(br->get_operand(2));
    BasicBlock *dropped = target == trueDest ? falseDest : trueDest;

    removeIncomingFromPred(dropped, bb);
    bb->remove_succ_basic_block(trueDest);
    bb->remove_succ_basic_block(falseDest);
    trueDest->remove_pre_basic_block(bb);
    falseDest->remove_pre_basic_block(bb);
    bb->delete_instr(br);
    new BranchInst(target, bb);
}

bool collectEdgeFacts(BasicBlock *bb, BoolFactMap &boolFacts,
                      ICmpFactMap &cmpFacts) {
    if (bb->pre_bbs_.size() != 1) return false;
    auto *pred = bb->pre_bbs_[0];
    auto *br = dynamic_cast<BranchInst *>(pred->get_terminator());
    if (!br || br->num_ops_ != 3) return false;

    auto *trueDest = static_cast<BasicBlock *>(br->get_operand(1));
    auto *falseDest = static_cast<BasicBlock *>(br->get_operand(2));
    if (trueDest != bb && falseDest != bb) return false;

    bool takeTrue = trueDest == bb;
    recordAssumedBool(br->get_operand(0), takeTrue, boolFacts, cmpFacts);
    return true;
}

ConstantInt *getBoolConstant(Module *module, bool value) {
    return new ConstantInt(module->int1_ty_, value ? 1 : 0);
}

bool foldInstructionsInBlock(BasicBlock *bb, BoolFactMap &boolFacts,
                             ICmpFactMap &cmpFacts) {
    bool changed = false;
    std::vector<Instruction *> toDelete;
    Module *module = bb->parent_->parent_;

    std::vector<Instruction *> instrs(bb->instr_list_.begin(), bb->instr_list_.end());
    for (auto *inst : instrs) {
        if (inst->parent_ != bb) continue;
        if (inst->is_phi()) continue;
        if (inst->is_br()) break;

        if (auto *icmp = dynamic_cast<ICmpInst *>(inst)) {
            auto known = getKnownBool(icmp, boolFacts, cmpFacts);
            if (known.has_value()) {
                inst->replace_all_use_with(getBoolConstant(module, *known));
                bb->delete_instr(inst);
                toDelete.push_back(inst);
                recordAssumedBool(icmp, *known, boolFacts, cmpFacts);
                changed = true;
                continue;
            }
        }

        if (auto *select = dynamic_cast<SelectInst *>(inst)) {
            auto known = getKnownBool(select->get_operand(0), boolFacts, cmpFacts);
            if (!known.has_value()) continue;
            Value *chosen = select->get_operand(*known ? 1 : 2);
            select->replace_all_use_with(chosen);
            bb->delete_instr(select);
            toDelete.push_back(select);
            changed = true;
            continue;
        }

        if (auto *zext = dynamic_cast<ZextInst *>(inst)) {
            auto known = getKnownBool(zext->get_operand(0), boolFacts, cmpFacts);
            if (!known.has_value()) continue;
            auto *replacement = new ConstantInt(zext->type_, *known ? 1 : 0);
            zext->replace_all_use_with(replacement);
            bb->delete_instr(zext);
            toDelete.push_back(zext);
            changed = true;
            continue;
        }
    }

    auto *br = dynamic_cast<BranchInst *>(bb->get_terminator());
    if (br && br->num_ops_ == 3) {
        auto known = getKnownBool(br->get_operand(0), boolFacts, cmpFacts);
        if (known.has_value()) {
            auto *target = static_cast<BasicBlock *>(br->get_operand(*known ? 1 : 2));
            replaceBranchWithUncond(bb, target);
            changed = true;
        }
    }

    return changed;
}

} // namespace

void CorrelatedValuePropagation::execute(Module *module) {
    runOnModule(module);
}

PreservedAnalyses CorrelatedValuePropagation::execute(Module *module,
                                                      AnalysisManager &AM) {
    (void)AM;
    return runOnModule(module) ? PreservedAnalyses::none()
                               : PreservedAnalyses::all();
}

bool CorrelatedValuePropagation::runOnModule(Module *module) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func);
    }
    return changed;
}

bool CorrelatedValuePropagation::runOnFunction(Function *func) {
    bool changed = false;
    for (auto *bb : func->basic_blocks_) {
        BoolFactMap boolFacts;
        ICmpFactMap cmpFacts;
        if (!collectEdgeFacts(bb, boolFacts, cmpFacts))
            continue;
        changed |= foldInstructionsInBlock(bb, boolFacts, cmpFacts);
    }
    return changed;
}
