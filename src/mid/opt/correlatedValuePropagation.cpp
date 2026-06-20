#include "../../include/mid/opt/correlatedValuePropagation.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/lazyValueInfo.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/opt/branchFactUtils.hpp"

#include <optional>
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

bool sameIntegerConstant(Constant *lhs, Value *rhs) {
    int lhsValue = 0;
    int rhsValue = 0;
    if (!lhs) return false;
    return getIntegerConstantValue(lhs, lhsValue) &&
           getIntegerConstantValue(rhs, rhsValue) && lhsValue == rhsValue;
}

ConstantInt *getBoolConstant(Module *module, bool value) {
    return new ConstantInt(module->int1_ty_, value ? 1 : 0);
}

Constant *getConstantAt(Value *value, Instruction *user, LazyValueInfo &LVI) {
    if (!value) return nullptr;
    if (auto *constant = dynamic_cast<Constant *>(value))
        return constant;
    return LVI.getConstant(value, user);
}

Value *getValueOnEdge(Value *value, BasicBlock *fromBB, BasicBlock *toBB,
                      Instruction *user, LazyValueInfo &LVI) {
    if (!value) return nullptr;
    if (auto *constant = dynamic_cast<Constant *>(value))
        return constant;

    if (auto *select = dynamic_cast<SelectInst *>(value)) {
        Constant *cond = LVI.getConstantOnEdge(select->get_operand(0), fromBB,
                                               toBB, user);
        int condValue = 0;
        if (!cond || !getIntegerConstantValue(cond, condValue))
            return nullptr;
        return getValueOnEdge(select->get_operand(condValue != 0 ? 1 : 2), fromBB,
                              toBB, user, LVI);
    }

    return LVI.getConstantOnEdge(value, fromBB, toBB, user);
}

bool isSafePhiReplacement(Value *value, PhiInst *phi) {
    if (!value || !phi) return false;
    if (dynamic_cast<Constant *>(value) || dynamic_cast<Argument *>(value))
        return true;
    if (dynamic_cast<GlobalVariable *>(value))
        return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    if (!inst || !inst->parent_ || inst->parent_ == phi->parent_)
        return false;
    return phi->parent_->parent_->dominates(inst->parent_, phi->parent_);
}

bool simplifyTrivialPhi(PhiInst *phi) {
    Value *common = nullptr;
    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
        Value *incoming = phi->get_operand(i);
        if (incoming == phi) continue;
        if (!common) {
            common = incoming;
            continue;
        }
        if (common != incoming)
            return false;
    }

    if (!common) return false;
    phi->replace_all_use_with(common);
    return phi->parent_ && phi->parent_->delete_instr(phi);
}

bool simplifyPhisInBlock(BasicBlock *bb, LazyValueInfo &LVI) {
    bool changed = false;
    std::vector<Instruction *> snapshot(bb->instr_list_.begin(), bb->instr_list_.end());
    for (auto *inst : snapshot) {
        if (!inst->parent_ || !inst->is_phi()) continue;
        auto *phi = static_cast<PhiInst *>(inst);

        for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (!pred) continue;

            Value *refined =
                getValueOnEdge(phi->get_operand(i), pred, bb, phi, LVI);
            if (!refined || refined == phi->get_operand(i))
                continue;

            phi->set_operand(i, refined);
            changed = true;
        }

        if (!phi->parent_) continue;
        if (simplifyTrivialPhi(phi)) {
            changed = true;
            continue;
        }

        Value *candidate = nullptr;
        bool failed = false;
        for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
            Value *incoming = phi->get_operand(i);
            if (dynamic_cast<Constant *>(incoming)) continue;
            if (incoming == phi) {
                failed = true;
                break;
            }
            if (!candidate) {
                candidate = incoming;
                continue;
            }
            if (candidate != incoming) {
                failed = true;
                break;
            }
        }

        if (failed || !candidate || !isSafePhiReplacement(candidate, phi))
            continue;

        bool canReplace = true;
        for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
            Value *incoming = phi->get_operand(i);
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (incoming == candidate) continue;
            auto *constIncoming = dynamic_cast<Constant *>(incoming);
            if (!constIncoming || !pred) {
                canReplace = false;
                break;
            }

            Constant *edgeValue =
                LVI.getConstantOnEdge(candidate, pred, bb, phi);
            if (!sameIntegerConstant(edgeValue, constIncoming)) {
                canReplace = false;
                break;
            }
        }

        if (!canReplace) continue;

        phi->replace_all_use_with(candidate);
        bb->delete_instr(phi);
        changed = true;
    }
    return changed;
}

bool foldSelectPerUse(SelectInst *select, LazyValueInfo &LVI) {
    if (!select || !select->parent_) return false;

    bool changed = false;
    std::vector<Use> uses(select->use_list_.begin(), select->use_list_.end());
    for (const auto &use : uses) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || !user->parent_) continue;

        Value *replacement = nullptr;
        if (auto *phi = dynamic_cast<PhiInst *>(user)) {
            if (use.arg_no_ + 1 >= phi->num_ops_) continue;
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(use.arg_no_ + 1));
            if (!pred) continue;
            replacement = getValueOnEdge(select, pred, phi->parent_, phi, LVI);
        } else {
            Constant *cond = getConstantAt(select->get_operand(0), user, LVI);
            int condValue = 0;
            if (!cond || !getIntegerConstantValue(cond, condValue))
                continue;
            replacement = select->get_operand(condValue != 0 ? 1 : 2);
        }

        if (!replacement || replacement == select)
            continue;

        user->set_operand(use.arg_no_, replacement);
        changed = true;
    }

    if (changed && select->use_list_.empty() && select->parent_) {
        select->parent_->delete_instr(select);
    }
    return changed;
}

bool foldInstructionsInBlock(BasicBlock *bb, LazyValueInfo &LVI) {
    bool changed = false;
    Module *module = bb->parent_->parent_;

    std::vector<Instruction *> instrs(bb->instr_list_.begin(), bb->instr_list_.end());
    for (auto *inst : instrs) {
        if (inst->parent_ != bb) continue;
        if (inst->is_phi()) continue;
        if (inst->is_br()) break;

        if (auto *icmp = dynamic_cast<ICmpInst *>(inst)) {
            auto known = LVI.getPredicateAt(icmp->icmp_op_, icmp->get_operand(0),
                                            icmp->get_operand(1), icmp);
            if (known.has_value()) {
                inst->replace_all_use_with(getBoolConstant(module, *known));
                bb->delete_instr(inst);
                changed = true;
                continue;
            }
        }

        if (auto *select = dynamic_cast<SelectInst *>(inst)) {
            changed |= foldSelectPerUse(select, LVI);
            continue;
        }

        if (auto *zext = dynamic_cast<ZextInst *>(inst)) {
            auto *known = getConstantAt(zext->get_operand(0), zext, LVI);
            int knownValue = 0;
            if (!known || !getIntegerConstantValue(known, knownValue)) continue;
            auto *replacement = new ConstantInt(zext->type_, knownValue != 0 ? 1 : 0);
            zext->replace_all_use_with(replacement);
            bb->delete_instr(zext);
            changed = true;
            continue;
        }
    }

    auto *br = dynamic_cast<BranchInst *>(bb->get_terminator());
    if (br && br->num_ops_ == 3) {
        auto *condValue = getConstantAt(br->get_operand(0), br, LVI);
        int cond = 0;
        if (condValue && getIntegerConstantValue(condValue, cond)) {
            auto *target =
                static_cast<BasicBlock *>(br->get_operand(cond != 0 ? 1 : 2));
            replaceBranchWithUncond(bb, target);
            changed = true;
        }
    }

    return changed;
}

} // namespace

void CorrelatedValuePropagation::execute(Module *module) {
    AnalysisManager AM;
    runOnModule(module, AM);
}

PreservedAnalyses CorrelatedValuePropagation::execute(Module *module,
                                                      AnalysisManager &AM) {
    return runOnModule(module, AM) ? PreservedAnalyses::none()
                                   : PreservedAnalyses::all();
}

bool CorrelatedValuePropagation::runOnModule(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func, AM);
    }
    return changed;
}

bool CorrelatedValuePropagation::runOnFunction(Function *func,
                                               AnalysisManager &AM) {
    bool changedAny = false;
    bool changed = false;
    do {
        changed = false;
        LazyValueInfo &LVI = AM.getLazyValueInfo(func);
        std::vector<BasicBlock *> blocks(func->basic_blocks_.begin(),
                                         func->basic_blocks_.end());
        for (auto *bb : blocks) {
            if (bb->parent_ != func) continue;
            bool blockChanged = simplifyPhisInBlock(bb, LVI);
            if (!blockChanged)
                blockChanged = foldInstructionsInBlock(bb, LVI);
            if (blockChanged) {
                changed = true;
                break;
            }
        }
        changedAny |= changed;
        if (changed)
            AM.invalidateFunction(func, PreservedAnalyses::none());
    } while (changed);
    return changedAny;
}
