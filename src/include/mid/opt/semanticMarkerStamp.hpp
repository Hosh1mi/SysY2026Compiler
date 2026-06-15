#pragma once
// SemanticMarkerStamp：把"分析能证明、但源级标记尚未直接给出"的语义事实戳到 IR 上。
//   - ImmutableLoad：load 的底层对象是 const 内存（const 全局，或不逃逸且非循环内声明的
//     局部 const 数组），且初始化区支配该 load —— 标记后 LICM 可越过别名歧义直接外提、
//     GVN 可跨内存状态 CSE。
//   - FnPure / FnReadOnly / ArgReadOnly / ArgNoCapture：从 BasicAliasAnalysis
//     的函数摘要持久化到 Function / Argument，主要用于 IR 可见性，也可作为
//     后续查询 live AA 时的保守语义兜底。
//
// 只置位、不改 IR 结构与语义，返回 PreservedAnalyses::all()。仅在 -O1 管线注册，
// 安排在 inline / mem2reg 之后、循环管线之前一次性运行。

#include "pass.hpp"
#include "../analysis/analysisManager.hpp"
#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"

#include <unordered_map>

class SemanticMarkerStamp : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "SemanticMarkerStamp"; }
    // 不改 IR，不参与重复组收敛判定。
    bool convergenceRelevant() const override { return false; }

private:
    void runOnFunction(Function *func, BasicAliasAnalysis &BAA, LoopInfo &LI);
    void stampImmutableLoads(Function *func, BasicAliasAnalysis &BAA, LoopInfo &LI);
    void stampFunctionAttrs(Function *func, BasicAliasAnalysis &BAA);

    // 底层对象是否可作为"不变内存"被读取（含逃逸/循环内声明判定，结果缓存）。
    bool isImmutableObject(Value *base, BasicAliasAnalysis &BAA, LoopInfo &LI);
    // 局部对象地址是否逃逸（被传入调用 / 指针本身被写入内存 / 未知使用者）。
    bool addressEscapes(Value *obj, BasicAliasAnalysis &BAA);

    std::unordered_map<Value *, bool> objImmutableCache_;
};
