#pragma once
// IR 模块：顶层容器，持有全局变量列表、函数列表和基本类型（i1/i32/float/void/label）

#include "type.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

class GlobalVariable;
class Function;

class Module {
public:
    explicit Module() {
        void_ty_ = new Type(Type::VoidTyID);
        label_ty_ = new Type(Type::LabelTyID);
        int1_ty_ = new IntegerType(1);
        int32_ty_ = new IntegerType(32);
        int64_ty_ = new IntegerType(64);
        float32_ty_ = new Type(Type::FloatTyID);
    }
    ~Module() {
        delete void_ty_;
        delete label_ty_;
        delete int1_ty_;
        delete int32_ty_;
        delete int64_ty_;
        delete float32_ty_;
    }
    virtual std::string print();
    void verify();  // IR 完整性验证（use-def链、SSA、基本块结构等）
    void verify(const std::string& context);  // 带上下文（如 pass 名）的版本
    void add_global_variable(GlobalVariable* g) { global_list_.push_back(g); }
    void add_function(Function* f) { function_list_.push_back(f); }

    // 获取指针类型（缓存避免重复创建）
    PointerType* get_pointer_type(Type* contained) {
        if (!pointer_map_.count(contained)) {
            pointer_map_[contained] = new PointerType(contained);
        }
        return pointer_map_[contained];
    }

    // 获取数组类型（缓存避免重复创建）
    ArrayType* get_array_type(Type* contained, unsigned num_elements) {
        if (!array_map_.count({contained, num_elements})) {
            array_map_[{contained, num_elements}] = new ArrayType(contained, num_elements);
        }
        return array_map_[{contained, num_elements}];
    }

    // 获取向量类型（缓存避免重复创建）
    VectorType* get_vector_type(Type* contained, unsigned num_elements) {
        if (!vector_map_.count({contained, num_elements})) {
            vector_map_[{contained, num_elements}] = new VectorType(contained, num_elements);
        }
        return vector_map_[{contained, num_elements}];
    }

    Function* getMainFunc();

    std::vector<GlobalVariable*> global_list_;
    std::vector<Function*> function_list_;

    // 常用基本类型
    IntegerType* int1_ty_;
    IntegerType* int32_ty_;
    IntegerType* int64_ty_;
    Type* float32_ty_;
    Type* label_ty_;
    Type* void_ty_;

private:
    std::map<Type*, PointerType*> pointer_map_;
    std::map<std::pair<Type*, int>, ArrayType*> array_map_;
    std::map<std::pair<Type*, int>, VectorType*> vector_map_;
};
