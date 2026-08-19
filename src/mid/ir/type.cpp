// Type::print() —— 将 IR 类型转为可读字符串（如 i32, float, [2 x i32]* 等）

#include "../../include/mid/ir/type.hpp"

#include <limits>
#include <string>

long long typeStorageBytes(Type *type) {
    if (!type) return -1;

    if (auto *integer = dynamic_cast<IntegerType *>(type)) {
        const unsigned bits = integer->num_bits_;
        if (bits == 0) return -1;
        return static_cast<long long>(bits / 8 + (bits % 8 != 0));
    }
    if (type->tid_ == Type::FloatTyID) return 4;
    if (type->tid_ == Type::PointerTyID) return 8;

    Type *elementType = nullptr;
    unsigned elementCount = 0;
    if (auto *array = dynamic_cast<ArrayType *>(type)) {
        elementType = array->contained_;
        elementCount = array->num_elements_;
    } else if (auto *vector = dynamic_cast<VectorType *>(type)) {
        elementType = vector->contained_;
        elementCount = vector->num_elements_;
    // } else if (auto *tensor = dynamic_cast<TensorType *>(type)){
    //     elementType = tensor->contained_;
    //     elementCount = tensor->num_elements_;
    }
    else {
        return -1;
    }

    const long long elementBytes = typeStorageBytes(elementType);
    if (elementBytes < 0 ||
        (elementCount != 0 &&
         elementBytes > std::numeric_limits<long long>::max() / elementCount))
        return -1;
    return elementBytes * static_cast<long long>(elementCount);
}

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
            if (static_cast<FunctionType*>(this)->is_variadic_) {
                if (!static_cast<FunctionType*>(this)->args_.empty()) type_ir += ", ";
                type_ir += "...";
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
