#include "../../include/mid/ir/intrinsics.hpp"

namespace {

const char *baseName(SignedMinMaxIntrinsic kind) {
    return kind == SignedMinMaxIntrinsic::SMin ? "llvm.smin" : "llvm.smax";
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

    std::string name = std::string(baseName(kind)) + "." + typeSuffix(type);
    for (auto *function : module->function_list_) {
        if (function->name_ == name) {
            function->setSemFlag(SemFlag::FnPure);
            return function;
        }
    }

    auto *functionType = new FunctionType(type, {type, type});
    auto *function = new Function(functionType, name, module);
    function->setSemFlag(SemFlag::FnPure);
    return function;
}

bool isSignedMinMaxIntrinsicName(const std::string &name,
                                 SignedMinMaxIntrinsic *kind) {
    SignedMinMaxIntrinsic matched;
    if (name == "llvm.smin.i32" || name == "llvm.smin.v4i32") {
        matched = SignedMinMaxIntrinsic::SMin;
    } else if (name == "llvm.smax.i32" || name == "llvm.smax.v4i32") {
        matched = SignedMinMaxIntrinsic::SMax;
    } else {
        return false;
    }
    if (kind)
        *kind = matched;
    return true;
}

bool isSignedMinMaxIntrinsic(Function *function,
                             SignedMinMaxIntrinsic *kind) {
    return function && isSignedMinMaxIntrinsicName(function->name_, kind);
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
