#include "../../include/mid/opt/localCopyPropagation.hpp"
#include <map>
#include <vector>

namespace {

Value *resolveCopy(Value *value, std::map<Value *, Value *> &copies) {
    std::vector<Value *> path;
    Value *cur = value;
    while (true) {
        auto it = copies.find(cur);
        if (it == copies.end() || it->second == cur) break;
        path.push_back(cur);
        cur = it->second;
    }
    for (auto *v : path) copies[v] = cur;
    return cur;
}

} // namespace

void LocalCopyPropagation::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func);
    }
}

bool LocalCopyPropagation::runOnFunction(Function *func) {
    bool changed = false;
    for (auto bb : func->basic_blocks_) {
        std::map<Value*, Value*> copies;

        for (auto inst : bb->instr_list_) {
            if (inst->is_phi()) continue;

            // Replace operands with the final representative of local identity copies.
            for (unsigned i = 0; i < inst->num_ops_; i++) {
                Value *op = inst->get_operand(i);
                Value *rep = resolveCopy(op, copies);
                if (rep != op) {
                    inst->set_operand(i, rep);
                    changed = true;
                }
            }

            // A redefined SSA value cannot keep an old representative.
            copies.erase(inst);

            // Record identity copies
            if (isIdentityCopy(inst)) {
                copies[inst] = resolveCopy(inst->get_operand(0), copies);
            }
        }
    }
    return changed;
}

bool LocalCopyPropagation::isIdentityCopy(Instruction *inst) {
    if (!inst || inst->num_ops_ == 0) return false;
    if (inst->op_id_ == Instruction::ZExt) {
        auto *srcTy = dynamic_cast<IntegerType *>(inst->get_operand(0)->type_);
        auto *dstTy = dynamic_cast<IntegerType *>(inst->type_);
        return srcTy && dstTy && srcTy->num_bits_ == dstTy->num_bits_;
    }
    if (inst->op_id_ == Instruction::BitCast) {
        if (inst->get_operand(0)->type_ == inst->type_)
            return true; // identity bitcast
    }
    return false;
}
