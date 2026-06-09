#include "../../include/mid/opt/phiOpSink.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <vector>

void PhiOpSink::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

static bool isCommutative(Instruction::OpID op) {
    return op == Instruction::Add ||
           op == Instruction::Mul ||
           op == Instruction::And ||
           op == Instruction::Or ||
           op == Instruction::Xor ||
           op == Instruction::FAdd ||
           op == Instruction::FMul;
}

static bool sameValue(Value *a, Value *b) {
    if (a == b)
        return true;
    auto *ai = dynamic_cast<ConstantInt *>(a);
    auto *bi = dynamic_cast<ConstantInt *>(b);
    if (ai && bi)
        return ai->value_ == bi->value_ && ai->type_ == bi->type_;
    auto *af = dynamic_cast<ConstantFloat *>(a);
    auto *bf = dynamic_cast<ConstantFloat *>(b);
    if (af && bf)
        return af->value_ == bf->value_ && af->type_ == bf->type_;
    return false;
}

static bool sameOperands(BinaryInst *inst, Instruction::OpID op,
                         Value *lhs, Value *rhs) {
    if (!inst || inst->op_id_ != op)
        return false;
    Value *a = inst->get_operand(0);
    Value *b = inst->get_operand(1);
    if (sameValue(a, lhs) && sameValue(b, rhs))
        return true;
    return isCommutative(op) && sameValue(a, rhs) && sameValue(b, lhs);
}

static bool valueDominatesBlock(Value *value, Function *func, BasicBlock *bb) {
    auto *inst = dynamic_cast<Instruction *>(value);
    if (!inst)
        return true;
    if (!inst->parent_)
        return false;
    return func->dominates(inst->parent_, bb);
}

bool PhiOpSink::trySinkPhi(PhiInst *phi, Function *func) {
    if (!phi || phi->num_ops_ < 2)
        return false;

    for (auto &use : phi->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (auto *userPhi = dynamic_cast<PhiInst *>(user)) {
            if (use.arg_no_ + 1 >= userPhi->num_ops_ ||
                userPhi->get_operand(use.arg_no_ + 1) != phi->parent_)
                return false;
        }
    }

    auto *first = dynamic_cast<BinaryInst *>(phi->get_operand(0));
    if (!first)
        return false;

    Instruction::OpID op = first->op_id_;
    Type *ty = first->type_;
    Value *lhs = first->get_operand(0);
    Value *rhs = first->get_operand(1);
    if (lhs == phi || rhs == phi)
        return false;
    if (!valueDominatesBlock(lhs, func, phi->parent_) ||
        !valueDominatesBlock(rhs, func, phi->parent_))
        return false;

    for (unsigned i = 2; i < phi->num_ops_; i += 2) {
        auto *inst = dynamic_cast<BinaryInst *>(phi->get_operand(i));
        if (!sameOperands(inst, op, lhs, rhs))
            return false;
    }

    auto *common = new BinaryInst(ty, op, lhs, rhs, phi->parent_, true);
    Instruction *insertBefore = nullptr;
    for (auto *inst : phi->parent_->instr_list_) {
        if (!inst->is_phi()) {
            insertBefore = inst;
            break;
        }
    }

    bool inserted = insertBefore
        ? phi->parent_->add_instruction_before_inst(common, insertBefore)
        : phi->parent_->add_instruction_before_terminator(common);
    if (!inserted) {
        common->remove_use_of_ops();
        return false;
    }

    phi->replace_all_use_with(common);
    phi->parent_->delete_instr(phi);
    return true;
}

bool PhiOpSink::runOnFunction(Function *func) {
    bool changed = false;
    bool localChanged = true;
    while (localChanged) {
        localChanged = false;
        for (auto *bb : func->basic_blocks_) {
            std::vector<PhiInst *> phis;
            for (auto *inst : bb->instr_list_) {
                if (!inst->is_phi())
                    break;
                phis.push_back(static_cast<PhiInst *>(inst));
            }

            for (auto *phi : phis) {
                if (trySinkPhi(phi, func)) {
                    changed = true;
                    localChanged = true;
                    func->set_instr_name();
                    break;
                }
            }
            if (localChanged)
                break;
        }
    }
    return changed;
}
