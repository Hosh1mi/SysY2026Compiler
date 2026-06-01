// Type::print() —— 将 IR 类型转为可读字符串（如 i32, float, [2 x i32]* 等）

#include "../../include/mid/ir/type.hpp"

#include <string>

std::string Type::print() {
    std::string type_ir;
    switch (this->tid_) {
        case VoidTyID:
            type_ir += "void";
            break;
        case LabelTyID:
            type_ir += "label";
            break;
        case IntegerTyID:
            type_ir += "i";
            type_ir += std::to_string(static_cast<IntegerType*>(this)->num_bits_);
            break;
        case FloatTyID:
            type_ir += "float";
            break;
        case FunctionTyID:
            type_ir += static_cast<FunctionType*>(this)->result_->print();
            type_ir += " (";
            for (size_t i = 0; i < static_cast<FunctionType*>(this)->args_.size(); i++) {
                if (i) type_ir += ", ";
                type_ir += static_cast<FunctionType*>(this)->args_[i]->print();
            }
            type_ir += ")";
            break;
        case PointerTyID:
            type_ir += static_cast<PointerType*>(this)->contained_->print();
            type_ir += "*";
            break;
        case ArrayTyID:
            type_ir += "[";
            type_ir += std::to_string(static_cast<ArrayType*>(this)->num_elements_);
            type_ir += " x ";
            type_ir += static_cast<ArrayType*>(this)->contained_->print();
            type_ir += "]";
            break;
        case VectorTyID:
            type_ir += "<";
            type_ir += std::to_string(static_cast<VectorType*>(this)->num_elements_);
            type_ir += " x ";
            type_ir += static_cast<VectorType*>(this)->contained_->print();
            type_ir += ">";
            break;
        default:
            break;
    }
    return type_ir;
}
