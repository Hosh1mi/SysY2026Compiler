#pragma once
// IR 模块：顶层容器，持有全局变量列表、函数列表和基本类型（i1/i32/float/void/label）

#include "type.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

class GlobalVariable;
class Function;

// 一个编译单元的 IR 根对象。它提供类型驻留工厂，并按输出顺序保存全局对象与函数。
// 当前析构函数只释放内建基本类型，IR 节点与派生类型沿用进程期所有权模型。
class Module {
public:
    // 创建每个模块唯一的一组常用基本类型。
    explicit Module() {
        void_ty_ = new Type(Type::VoidTyID);
        label_ty_ = new Type(Type::LabelTyID);
        int1_ty_ = new IntegerType(1);
        int8_ty_ = new IntegerType(8);
        int32_ty_ = new IntegerType(32);
        int64_ty_ = new IntegerType(64);
        float32_ty_ = new Type(Type::FloatTyID);
    }
    // 释放构造函数直接拥有的基本类型；缓存类型和 IR 对象目前不在此回收。
    ~Module() {
        delete void_ty_;
        delete label_ty_;
        delete int1_ty_;
        delete int8_ty_;
        delete int32_ty_;
        delete int64_ty_;
        delete float32_ty_;
    }
    // 依次打印全局定义、被使用的外部声明和函数定义。
    virtual std::string print();
    // 验证 use-def、CFG、PHI、支配关系和基本块结构；失败时报告并终止。
    void verify();
    // 与 verify() 相同，但把 pass 名等 context 附在诊断信息中。
    void verify(const std::string& context);
    // 注册全局对象并保持构造顺序；不接管其生命周期。
    void add_global_variable(GlobalVariable* g) { global_list_.push_back(g); }
    // 注册函数声明或定义并保持构造顺序；不接管其生命周期。
    void add_function(Function* f) { function_list_.push_back(f); }

    // 获取 contained*；按被指类型驻留，重复请求返回同一对象。
    PointerType* get_pointer_type(Type* contained) {
        auto &type = pointer_map_[contained];
        if (!type)
            type = new PointerType(contained);
        return type;
    }

    // 获取 [num_elements x contained]；按元素类型和长度驻留。
    ArrayType* get_array_type(Type* contained, unsigned num_elements) {
        auto &type = array_map_[{contained, num_elements}];
        if (!type)
            type = new ArrayType(contained, num_elements);
        return type;
    }

    VectorType* get_vector_type(Type* contained, unsigned num_elements) {
        auto &type = vector_map_[{contained, num_elements}];
        if (!type)
            type = new VectorType(contained, num_elements);
        return type;
    }

    // 返回名为 main 的函数；未找到时返回 nullptr。
    Function* getMainFunc();

    std::vector<GlobalVariable*> global_list_;
    std::vector<Function*> function_list_;

    // 常用基本类型
    IntegerType* int1_ty_;
    IntegerType* int8_ty_;
    IntegerType* int32_ty_;
    IntegerType* int64_ty_;
    Type* float32_ty_;
    Type* label_ty_;
    Type* void_ty_;

private:
    // 类型缓存使同一模块中的结构等价类型可用指针直接比较。
    std::map<Type*, PointerType*> pointer_map_;
    std::map<std::pair<Type*, unsigned>, ArrayType*> array_map_;
    std::map<std::pair<Type*, unsigned>, VectorType*> vector_map_;
};
