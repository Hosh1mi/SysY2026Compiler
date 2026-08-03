#include "../../include/mid/ir/intrinsics.hpp"

namespace {

const char *baseName(SignedMinMaxIntrinsic kind) {
    return kind == SignedMinMaxIntrinsic::SMin ? "llvm.smin" : "llvm.smax";
}

Function::IntrinsicID intrinsicID(SignedMinMaxIntrinsic kind) {
    return kind == SignedMinMaxIntrinsic::SMin
               ? Function::IntrinsicID::SignedMin
               : Function::IntrinsicID::SignedMax;
}

std::string typeSuffix(Type *type) {
    if (auto *integer = dynamic_cast<IntegerType *>(type)) {
        if (integer->num_bits_ == 32)
            return "i32";
    }
    if (auto *vector = dynamic_cast<VectorType *>(type)) {
        auto *element = dynamic_cast<IntegerType *>(vector->contained_);
        if (element && element->num_bits_ == 32 && vector->num_elements_ == 4)
            return "v4i32";
    }
    return "";
}

bool selectChoosesCompareOperands(SelectInst *select, ICmpInst *compare,
                                  bool swapped) {
    Value *lhs = compare->get_operand(0);
    Value *rhs = compare->get_operand(1);
    return select->get_operand(1) == (swapped ? rhs : lhs) &&
           select->get_operand(2) == (swapped ? lhs : rhs);
}

} // namespace

bool isSupportedSignedMinMaxType(Type *type) {
    return !typeSuffix(type).empty();
}

Function *getOrInsertSignedMinMaxIntrinsic(Module *module,
                                           SignedMinMaxIntrinsic kind,
                                           Type *type) {
    if (!module || !isSupportedSignedMinMaxType(type))
        return nullptr;

    Function::IntrinsicID id = intrinsicID(kind);
    std::string name = std::string(baseName(kind)) + "." + typeSuffix(type);
    for (auto *function : module->function_list_) {
        if (function->intrinsicID() != id)
            continue;
        auto *functionType = dynamic_cast<FunctionType *>(function->type_);
        if (!functionType || functionType->result_ != type ||
            functionType->args_.size() != 2 ||
            functionType->args_[0] != type || functionType->args_[1] != type)
            continue;
        function->setSemFlag(SemFlag::FnPure);
        return function;
    }

    auto *functionType = new FunctionType(type, {type, type});
    auto *function = new Function(functionType, name, module);
    function->setIntrinsicID(id);
    function->setSemFlag(SemFlag::FnPure);
    return function;
}

bool isSignedMinMaxIntrinsic(Function *function,
                             SignedMinMaxIntrinsic *kind) {
    if (!function)
        return false;

    SignedMinMaxIntrinsic matched;
    switch (function->intrinsicID()) {
    case Function::IntrinsicID::SignedMin:
        matched = SignedMinMaxIntrinsic::SMin;
        break;
    case Function::IntrinsicID::SignedMax:
        matched = SignedMinMaxIntrinsic::SMax;
        break;
    default:
        return false;
    }
    if (kind)
        *kind = matched;
    return true;
}

Function *getOrInsertMulModIntrinsic(Module *module) {
    if (!module)
        return nullptr;

    for (auto *function : module->function_list_) {
        if (function->intrinsicID() != Function::IntrinsicID::MulMod)
            continue;
        auto *functionType = dynamic_cast<FunctionType *>(function->type_);
        if (!functionType || functionType->result_ != module->int32_ty_ ||
            functionType->args_.size() != 3 ||
            functionType->args_[0] != module->int32_ty_ ||
            functionType->args_[1] != module->int32_ty_ ||
            functionType->args_[2] != module->int32_ty_)
            continue;
        function->setSemFlag(SemFlag::FnPure);
        return function;
    }

    auto *functionType = new FunctionType(
        module->int32_ty_,
        {module->int32_ty_, module->int32_ty_, module->int32_ty_});
    auto *function =
        new Function(functionType, "llvm.mulmod.i32", module);
    function->setIntrinsicID(Function::IntrinsicID::MulMod);
    function->setSemFlag(SemFlag::FnPure);
    return function;
}

bool isMulModIntrinsic(Function *function) {
    return function &&
           function->intrinsicID() == Function::IntrinsicID::MulMod;
}

bool matchSignedMinMaxSelect(SelectInst *select,
                             SignedMinMaxIntrinsic &kind,
                             Value *&lhs,
                             Value *&rhs) {
    lhs = nullptr;
    rhs = nullptr;
    if (!select || !isSupportedSignedMinMaxType(select->type_))
        return false;

    auto *compare = dynamic_cast<ICmpInst *>(select->get_operand(0));
    if (!compare || compare->get_operand(0)->type_ != select->type_ ||
        compare->get_operand(1)->type_ != select->type_)
        return false;

    bool normal = selectChoosesCompareOperands(select, compare, false);
    bool swapped = !normal && selectChoosesCompareOperands(select, compare, true);
    if (!normal && !swapped)
        return false;

    switch (compare->icmp_op_) {
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_SLE:
        kind = normal ? SignedMinMaxIntrinsic::SMin
                      : SignedMinMaxIntrinsic::SMax;
        break;
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_SGE:
        kind = normal ? SignedMinMaxIntrinsic::SMax
                      : SignedMinMaxIntrinsic::SMin;
        break;
    default:
        return false;
    }

    lhs = compare->get_operand(0);
    rhs = compare->get_operand(1);
    return true;
}
