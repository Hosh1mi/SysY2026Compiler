#pragma once
// IR 函数与参数：define/declare 函数，包含基本块列表和参数列表

#include "value.hpp"
#include "module.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class BasicBlock;

// 函数形参，由 Function 构造函数自动创建
class Argument : public Value {
public:
    explicit Argument(Type* ty, const std::string& name = "", Function* f = nullptr, unsigned arg_no = 0)
        : Value(ty, name), parent_(f), arg_no_(arg_no) {}
    ~Argument() {}
    virtual std::string print() override;
    Function* parent_;
    unsigned arg_no_;  // 参数序号
};

// 共享支配树信息，由 Function 统一计算并缓存，各优化 Pass 复用
struct DominatorInfo {
    bool valid = false;
    std::map<BasicBlock*, BasicBlock*>             idom;        // 立即支配者（entry 为 nullptr）
    std::map<BasicBlock*, std::set<BasicBlock*>>   domFront;    // 支配边界
    std::map<BasicBlock*, std::vector<BasicBlock*>> domChildren; // 支配树子节点

    bool dominates(BasicBlock *a, BasicBlock *b) const;
};

class Function : public Value {
public:
    enum class IntrinsicID {
        None,
        SignedMin,
        SignedMax,
    };

    Function(FunctionType* ty, const std::string& name, Module* parent) : Value(ty, name), parent_(parent), seq_cnt_(0), gep_cnt_(0) {
        parent->add_function(this);
        size_t num_args = ty->args_.size();
        use_ret_cnt = 0;
        for (size_t i = 0; i < num_args; i++) {
            arguments_.push_back(new Argument(ty->args_[i], "", this, i));
        }
    }
    ~Function();
    virtual std::string print() override;
    IntrinsicID intrinsicID() const { return intrinsicID_; }
    void setIntrinsicID(IntrinsicID id) { intrinsicID_ = id; }
    void add_basic_block(BasicBlock* bb) { basic_blocks_.push_back(bb); invalidateDominatorInfo(); }
    Type* get_return_type() const { return static_cast<FunctionType*>(type_)->result_; }
    bool is_declaration() { return basic_blocks_.empty(); }  // 无基本块→仅为声明
    enum class HiraWorkerState {
        None,
        Pending,
        Optimized,
    };
    void markHiraParallelWorker() {
        hiraWorkerState_ = HiraWorkerState::Pending;
    }
    bool isPendingHiraParallelWorker() const {
        return hiraWorkerState_ == HiraWorkerState::Pending;
    }
    void markHiraParallelWorkerOptimized() {
        if (hiraWorkerState_ == HiraWorkerState::Pending)
            hiraWorkerState_ = HiraWorkerState::Optimized;
    }
    void set_instr_name();       // 统一命名所有未命名的指令
    void remove_bb(BasicBlock* bb);
    BasicBlock* getRetBB();      // 获取唯一 return 基本块

    // 支配树信息（惰性计算，CFG 变更时自动失效）
    DominatorInfo &getDominatorInfo();
    void invalidateDominatorInfo();
    bool dominates(BasicBlock *a, BasicBlock *b) { return getDominatorInfo().dominates(a, b); }

    std::vector<BasicBlock*> basic_blocks_;
    std::vector<Argument*> arguments_;
    Module* parent_;
    unsigned seq_cnt_;                        // 命名序号计数器
    unsigned gep_cnt_;                        // GEP 独立命名计数器
    std::vector<std::set<Value*>> vreg_set_; // 虚拟寄存器集合（优化用）
    int use_ret_cnt;                          // 返回值的实际使用次数

private:
    void computeDominatorInfo();
    std::unique_ptr<DominatorInfo> domInfo_;
    HiraWorkerState hiraWorkerState_ = HiraWorkerState::None;
    IntrinsicID intrinsicID_ = IntrinsicID::None;
};
