// Constant 各子类的 print() —— 将常量值转为 IR 文本格式

#include "../../include/mid/ir/constant.hpp"

#include <cstdint>
#include <sstream>
#include <string>

// i32 常量或 i1 常量
std::string ConstantInt::print() {
    std::string const_ir;
    if (this->type_->tid_ == Type::IntegerTyID && static_cast<IntegerType*>(this->type_)->num_bits_ == 1) {
        const_ir += (this->value_ == 0) ? "0" : "1";  // i1
    } else
        const_ir += std::to_string(this->value_);      // i32
    return const_ir;
}

// float 常量，以十六进制双精度表示
std::string ConstantFloat::print() {
    std::stringstream fp_ir_ss;
    std::string fp_ir;
    double val = this->value_;
    fp_ir_ss << "0x" << std::hex << *reinterpret_cast<std::uint64_t*>(&val) << std::endl;
    fp_ir_ss >> fp_ir;
    return fp_ir;
}

// 数组常量，如 [2 x i32] [i32 1, i32 2]
std::string ConstantArray::print() {
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

std::string ConstantZero::print() {
    return "zeroinitializer";
}
