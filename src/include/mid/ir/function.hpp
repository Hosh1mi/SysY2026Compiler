#pragma once
// IR 函数与参数：define/declare 函数，包含基本块列表和参数列表

#include "value.hpp"
#include "module.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class BasicBlock;

// 函数形参，由 Function 构造函数自动创建
class Argument : public Value {
public:
    explicit Argument(Type* ty, const std::string& name = "", Function* f = nullptr, unsigned arg_no = 0)
        : Value(ty, name), parent_(f), arg_no_(arg_no) {}
    virtual std::string print() override;
    Function* parent_;
    unsigned arg_no_;  // 参数序号
};

class Function : public Value {
public:
    enum class IntrinsicID {
        None,
        SignedMin,
        SignedMax,
        // (a * b) srem m with wide product; lowered by the backend.
        MulMod,
    };

    Function(FunctionType* ty, const std::string& name, Module* parent)
        : Value(ty, name), parent_(parent) {
        parent->add_function(this);
        size_t num_args = ty->args_.size();
        for (size_t i = 0; i < num_args; i++) {
            arguments_.push_back(new Argument(ty->args_[i], "", this, i));
        }
    }
    virtual std::string print() override;
    IntrinsicID intrinsicID() const { return intrinsicID_; }
    void setIntrinsicID(IntrinsicID id) { intrinsicID_ = id; }
    void add_basic_block(BasicBlock* bb);
    // Allocate source-level block names without rescanning the whole function.
    std::string uniqueBasicBlockName(const std::string &base);
    Type* get_return_type() const { return static_cast<FunctionType*>(type_)->result_; }
    bool is_declaration() { return basic_blocks_.empty(); }  // 无基本块→仅为声明
    void set_instr_name();       // 统一命名所有未命名的指令
    void remove_bb(BasicBlock* bb);
    BasicBlock* getRetBB();      // 获取唯一 return 基本块

    std::vector<BasicBlock*> basic_blocks_;
    std::vector<Argument*> arguments_;
    Module* parent_;
    // create_alloca 用：指向 entry 中最近一次插入的 alloca，保证 O(1) 追加到 alloca 段
    class AllocaInst *lastEntryAlloca_ = nullptr;

private:
    std::unordered_set<std::string> basic_block_names_;
    std::unordered_map<std::string, unsigned> basic_block_suffixes_;

    IntrinsicID intrinsicID_ = IntrinsicID::None;
};
