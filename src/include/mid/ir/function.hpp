#pragma once
// IR 函数与参数：define/declare 函数，包含基本块列表和参数列表

#include "value.hpp"
#include "module.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class BasicBlock;

// 函数形参：没有定义指令，但和指令一样是可被引用的 SSA Value。
class Argument : public Value {
public:
    // f 和 arg_no 标明 ABI/源码参数位置；通常只由 Function 构造函数调用。
    explicit Argument(Type* ty, const std::string& name = "", Function* f = nullptr, unsigned arg_no = 0)
        : Value(ty, name), parent_(f), arg_no_(arg_no) {}
    // 输出形参类型和 %name；参数语义属性由 Function::print 的 sem 注释统一输出。
    virtual std::string print() override;
    Function* parent_;
    unsigned arg_no_;  // 参数序号
};

// 函数声明或定义。Function 本身可作为 CallInst 的 callee 操作数；函数体按
// basic_blocks_ 顺序保存，首块是 entry。
class Function : public Value {
public:
    // 内建函数的结构化身份；优化和后端不得依赖函数名字猜测语义。
    enum class IntrinsicID {
        None,
        SignedMin,
        SignedMax,
        // (a * b) srem m with wide product; lowered by the backend.
        MulMod,
    };

    // 创建函数、注册到 parent，并根据函数类型一次性创建全部 Argument。
    Function(FunctionType* ty, const std::string& name, Module* parent)
        : Value(ty, name), parent_(parent) {
        parent->add_function(this);
        size_t num_args = ty->args_.size();
        for (size_t i = 0; i < num_args; i++) {
            arguments_.push_back(new Argument(ty->args_[i], "", this, i));
        }
    }
    // 输出 declare 或完整 define；打印定义前会为未命名值分配稳定名字。
    virtual std::string print() override;
    // 返回结构化 intrinsic 身份。
    IntrinsicID intrinsicID() const { return intrinsicID_; }
    // 标记函数的 intrinsic 身份；仅在建立或识别内建函数时调用。
    void setIntrinsicID(IntrinsicID id) { intrinsicID_ = id; }
    // 把新块加入函数末尾并登记其当前名字；唯一名应先由 uniqueBasicBlockName 取得。
    void add_basic_block(BasicBlock* bb);
    // 从 base 开始分配不冲突的源码语义块名，不扫描整个函数。
    std::string uniqueBasicBlockName(const std::string &base);
    // 从 FunctionType 取得返回类型。
    Type* get_return_type() const { return static_cast<FunctionType*>(type_)->result_; }
    // 没有基本块的函数是外部声明，否则是定义。
    bool is_declaration() { return basic_blocks_.empty(); }
    // 按打印顺序为未命名参数、基本块和有结果指令分配唯一 SSA 名字。
    void set_instr_name();
    // 从函数与相邻 CFG 缓存移除 bb；调用方应先处理仍指向它的分支和 PHI。
    void remove_bb(BasicBlock* bb);
    // 按块顺序返回第一个含 ret 终止指令的基本块；没有时返回 nullptr。
    BasicBlock* getRetBB();

    std::vector<BasicBlock*> basic_blocks_;  // 布局/打印顺序，front() 为 entry
    std::vector<Argument*> arguments_;       // 与 FunctionType::args_ 一一对应
    Module* parent_;                         // 所属模块，不拥有
    // create_alloca 用：指向 entry 中最近一次插入的 alloca，保证 O(1) 追加到 alloca 段
    class AllocaInst *lastEntryAlloca_ = nullptr;

private:
    // 名字集合和下一个后缀共同保证增量创建基本块时无需全函数扫描。
    std::unordered_set<std::string> basic_block_names_;
    std::unordered_map<std::string, unsigned> basic_block_suffixes_;

    IntrinsicID intrinsicID_ = IntrinsicID::None; // None 表示普通函数
};
