// Constant 各子类的 print() —— 将常量值转为 IR 文本格式

#include "../../include/mid/ir/constant.hpp"

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

// ── Recursively check whether a Constant represents zero ──────────────

static bool isZeroConstant(Constant *c) {
    if (dynamic_cast<ConstantZero*>(c)) return true;
    if (auto *ci = dynamic_cast<ConstantInt*>(c))  return ci->value_ == 0;
    if (auto *cf = dynamic_cast<ConstantFloat*>(c)) return cf->value_ == 0.0f;
    if (auto *ca = dynamic_cast<ConstantArray*>(c)) {
        for (auto *elem : ca->const_array)
            if (!isZeroConstant(elem)) return false;
        return true;
    }
    if (auto *cv = dynamic_cast<ConstantVector*>(c)) {
        for (auto *elem : cv->elements_)
            if (!isZeroConstant(elem)) return false;
        return true;
    }
    return false;
}

// ── Individual constant printing ──────────────────────────────────────
std::string ConstantInt::print() {
    std::string const_ir;
    if (this->type_->tid_ == Type::IntegerTyID && static_cast<IntegerType*>(this->type_)->num_bits_ == 1) {
        const_ir += (this->value_ == 0) ? "0" : "1";  // i1
    } else
        const_ir += std::to_string(this->value_);      // i32 / i64
    return const_ir;
}

// float 常量，以十六进制双精度表示
std::string ConstantFloat::print() {
    double val = this->value_;
    std::uint64_t bits;
    std::memcpy(&bits, &val, sizeof(bits));
    std::ostringstream out;
    out << "0x" << std::hex << bits;
    return out.str();
}

// 数组常量，如 [2 x i32] [i32 1, i32 2]
std::string ConstantArray::print() {
    // All-zero array → zeroinitializer
    bool allZero = true;
    for (auto *elem : const_array) {
        if (!isZeroConstant(elem)) { allZero = false; break; }
    }
    if (allZero) return "zeroinitializer";

    std::string const_ir;
    const_ir += "[";
    const_ir += static_cast<ArrayType*>(this->type_)->contained_->print();
    const_ir += " ";
    const_ir += const_array[0]->print();
    for (size_t i = 1; i < this->const_array.size(); i++) {
        const_ir += ", ";
        const_ir += static_cast<ArrayType*>(this->type_)->contained_->print();
        const_ir += " ";
        const_ir += const_array[i]->print();
    }
    const_ir += "]";
    return const_ir;
}

// 向量常量，如 <i32 0, i32 1, i32 2, i32 3>
std::string ConstantVector::print() {
    // All-zero vector → zeroinitializer
    bool allZero = true;
    for (auto *elem : elements_) {
        if (!isZeroConstant(elem)) { allZero = false; break; }
    }
    if (allZero) return "zeroinitializer";

    std::string const_ir;
    auto *vecTy = static_cast<VectorType*>(this->type_);
    const_ir += "<";
    for (size_t i = 0; i < elements_.size(); i++) {
        if (i > 0) const_ir += ", ";
        const_ir += vecTy->contained_->print();
        const_ir += " ";
        const_ir += elements_[i]->print();
    }
    const_ir += ">";
    return const_ir;
}

// print：按项目 IR 文本格式输出节点类型、操作数和必要属性。
std::string ConstantZero::print() {
    return "zeroinitializer";
}

// print：按项目 IR 文本格式输出节点类型、操作数和必要属性。
std::string UndefValue::print() {
    return "undef";
}
