#include "../../include/mid/opt/localCopyPropagation.hpp"
#include <map>

void LocalCopyPropagation::execute(Module *module) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto func : module->function_list_) {
            if (func->is_declaration()) continue;
            if (runOnFunction(func)) changed = true;
        }
    }
}

bool LocalCopyPropagation::runOnFunction(Function *func) {
    bool changed = false;
    for (auto bb : func->basic_blocks_) {
        std::map<Value*, Value*> copies;

        for (auto inst : bb->instr_list_) {
            if (inst->is_phi()) continue;

            // Replace operands with known copies
            for (unsigned i = 0; i < inst->num_ops_; i++) {
                Value *op = inst->get_operand(i);
                auto it = copies.find(op);
                if (it != copies.end() && it->second != op) {
                    op->remove_used(inst, i);
                    inst->set_operand(i, it->second);
                    changed = true;
                }
            }

            // Kill copies: any value-producing instruction kills itself
            // as the destination of a copy
            copies.erase(inst);

            // Kill copies where this instruction is the source
            // (the source value might be clobbered)
            if (inst->is_store() || inst->is_call()) {
                for (auto it = copies.begin(); it != copies.end(); ) {
                    if (it->second == inst || it->first == inst)
                        it = copies.erase(it);
                    else
                        ++it;
                }
            }

            // Record identity copies
            if (isIdentityCopy(inst)) {
                copies[inst] = inst->get_operand(0);
            }
        }
    }
    return changed;
}

bool LocalCopyPropagation::isIdentityCopy(Instruction *inst) {
    if (inst->op_id_ == Instruction::ZExt) {
        auto srcTy = inst->get_operand(0)->type_;
        if (srcTy->tid_ == Type::IntegerTyID &&
            static_cast<IntegerType*>(srcTy)->num_bits_ == 32) {
            return true; // zext i32 to i32 is a nop
        }
    }
    if (inst->op_id_ == Instruction::BitCast) {
        if (inst->get_operand(0)->type_ == inst->type_)
            return true; // identity bitcast
    }
    return false;
}
