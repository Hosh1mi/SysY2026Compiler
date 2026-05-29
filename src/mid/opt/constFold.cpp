#include "../../include/mid/opt/constFold.hpp"

void ConstantFold::execute(Module *module) {
    bool changed = true;
    // 反复执行，直到没有新的常量折叠发生（常量传播）
    while (changed) {
        changed = false;
        for (auto &func : module->function_list_) {
            if (func->is_declaration()) continue;
            changed |= runOnFunction(func, module);
        }
    }
}

bool ConstantFold::runOnFunction(Function *func, Module *module) {
    bool changed = false;
    for (auto bb : func->basic_blocks_) {
        // 快照当前基本块的指令列表，防止迭代中修改列表导致失效
        std::vector<Instruction*> snapshot(bb->instr_list_.begin(), bb->instr_list_.end());
        for (auto instr : snapshot) {
            if (instr->parent_ == nullptr) continue; // 已被删除
            Constant *folded = tryFold(instr, module);
            if (folded) {
                // 用常量替换所有使用该指令的地方
                instr->replace_all_use_with(folded);
                // 从基本块中删除该指令
                bb->delete_instr(instr);
                changed = true;
            }
        }
    }
    return changed;
}

Constant* ConstantFold::tryFold(Instruction *instr, Module *module) {
    // Phi 节点：若所有 incoming 值（排除自引用）相同且为常数，替换为该常数
    if (auto *phi = dynamic_cast<PhiInst*>(instr)) {
        Value *common = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            Value *v = phi->get_operand(i);
            if (v == phi) continue;          // skip self-reference
            if (!common) common = v;
            else if (common != v) return nullptr;
        }
        if (auto *c = dynamic_cast<Constant*>(common))
            return c;
        return nullptr;
    }
    // 二元算术指令
    if (auto *bin = dynamic_cast<BinaryInst*>(instr)) {
        return foldBinary(bin, module);
    }
    // 整型比较
    if (auto *icmp = dynamic_cast<ICmpInst*>(instr)) {
        return foldICmp(icmp, module);
    }
    // 浮点比较
    if (auto *fcmp = dynamic_cast<FCmpInst*>(instr)) {
        return foldFCmp(fcmp, module);
    }
    // 一元类型转换指令
    if (auto *unary = dynamic_cast<UnaryInst*>(instr)) {
        return foldUnary(unary, module);
    }
    // TODO: 可以扩展 GetElementPtr 等，此处略
    return nullptr;
}

bool ConstantFold::bothConstant(Value *op0, Value *op1) {
    if (dynamic_cast<ConstantInt*>(op0) && dynamic_cast<ConstantInt*>(op1))
        return op0->type_ == op1->type_;
    if (dynamic_cast<ConstantFloat*>(op0) && dynamic_cast<ConstantFloat*>(op1))
        return op0->type_ == op1->type_;
    return false;
}

Constant* ConstantFold::foldBinary(BinaryInst *bin, Module *module) {
    Value *op0 = bin->get_operand(0);
    Value *op1 = bin->get_operand(1);
    if (!bothConstant(op0, op1)) return nullptr;
    auto *int0 = dynamic_cast<ConstantInt*>(op0);
    if (int0) { // 整型运算
        int v0 = int0->value_;
        int v1 = static_cast<ConstantInt*>(op1)->value_;
        int result;
        switch (bin->op_id_) {
            case Instruction::Add:  result = v0 + v1; break;
            case Instruction::Sub:  result = v0 - v1; break;
            case Instruction::Mul:  result = v0 * v1; break;
            case Instruction::SDiv:
                if (v1 == 0) return nullptr; // 除零不折叠
                result = v0 / v1; break;
            case Instruction::SRem:
                if (v1 == 0) return nullptr;
                result = v0 % v1; break;
            default: return nullptr; // 不支持的操作
        }
        return new ConstantInt(bin->type_, result);
    } else { // 浮点运算
        float v0 = static_cast<ConstantFloat*>(op0)->value_;
        float v1 = static_cast<ConstantFloat*>(op1)->value_;
        float result;
        switch (bin->op_id_) {
            case Instruction::FAdd: result = v0 + v1; break;
            case Instruction::FSub: result = v0 - v1; break;
            case Instruction::FMul: result = v0 * v1; break;
            case Instruction::FDiv:
                if (v1 == 0.0f) return nullptr;
                result = v0 / v1; break;
            default: return nullptr;
        }
        return new ConstantFloat(bin->type_, result);
    }
}

Constant* ConstantFold::foldICmp(ICmpInst *icmp, Module *module) {
    Value *op0 = icmp->get_operand(0);
    Value *op1 = icmp->get_operand(1);
    if (!bothConstant(op0, op1)) return nullptr;
    int v0 = static_cast<ConstantInt*>(op0)->value_;
    int v1 = static_cast<ConstantInt*>(op1)->value_;
    bool result;
    switch (icmp->icmp_op_) {
        case ICmpInst::ICMP_EQ:  result = (v0 == v1); break;
        case ICmpInst::ICMP_NE:  result = (v0 != v1); break;
        case ICmpInst::ICMP_SGT: result = (v0 >  v1); break;
        case ICmpInst::ICMP_SGE: result = (v0 >= v1); break;
        case ICmpInst::ICMP_SLT: result = (v0 <  v1); break;
        case ICmpInst::ICMP_SLE: result = (v0 <= v1); break;
        default: return nullptr;
    }
    return new ConstantInt(module->int1_ty_, result ? 1 : 0);
}

Constant* ConstantFold::foldFCmp(FCmpInst *fcmp, Module *module) {
    Value *op0 = fcmp->get_operand(0);
    Value *op1 = fcmp->get_operand(1);
    if (!bothConstant(op0, op1)) return nullptr;
    float v0 = static_cast<ConstantFloat*>(op0)->value_;
    float v1 = static_cast<ConstantFloat*>(op1)->value_;
    bool result;
    switch (fcmp->fcmp_op_) {
        case FCmpInst::FCMP_OEQ: case FCmpInst::FCMP_UEQ:
            result = (v0 == v1); break;
        case FCmpInst::FCMP_ONE: case FCmpInst::FCMP_UNE:
            result = (v0 != v1); break;
        case FCmpInst::FCMP_OGT: case FCmpInst::FCMP_UGT:
            result = (v0 >  v1); break;
        case FCmpInst::FCMP_OGE: case FCmpInst::FCMP_UGE:
            result = (v0 >= v1); break;
        case FCmpInst::FCMP_OLT: case FCmpInst::FCMP_ULT:
            result = (v0 <  v1); break;
        case FCmpInst::FCMP_OLE: case FCmpInst::FCMP_ULE:
            result = (v0 <= v1); break;
        default: return nullptr;
    }
    return new ConstantInt(module->int1_ty_, result ? 1 : 0);
}

Constant* ConstantFold::foldUnary(UnaryInst *unary, Module *module) {
    Value *op = unary->get_operand(0);
    if (auto *ci = dynamic_cast<ConstantInt*>(op)) {
        if (unary->op_id_ == Instruction::ZExt) {
            // i1 -> i32
            return new ConstantInt(unary->type_, ci->value_);
        }
        if (unary->op_id_ == Instruction::SItoFP) {
            // int32 -> float
            return new ConstantFloat(unary->type_, (float)ci->value_);
        }
    }
    if (auto *cf = dynamic_cast<ConstantFloat*>(op)) {
        if (unary->op_id_ == Instruction::FPtoSI) {
            // float -> int32
            return new ConstantInt(unary->type_, (int)cf->value_);
        }
    }
    return nullptr;
}
