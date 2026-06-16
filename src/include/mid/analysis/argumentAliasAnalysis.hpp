#pragma once
// ArgumentAliasAnalysis: 过程间"参数指向对象"分析，用于消除指针参数间的别名歧义。
//
// 动机：BasicAliasAnalysis 能区分不同 global / alloca，但对两个不同的指针【参数】
// 一律保守判 MayAlias（调用方理论上可能传同一数组）。要【证明】两参数不别名，
// 必须看所有调用点实际传入的底层对象——这正是本分析做的事。
//
// 方法：对每个函数 F 的每个指针参数 a，扫描模块里所有 call F 的实参，解析其底层
// 对象(穿透 gep/bitcast)。
//   - 实参底层是 global / alloca → 计入 a 的"根对象集"；
//   - 实参底层是调用方的某个参数 b → 递归求 b 的根集并并入(环 → unknown)；
//   - 实参底层无法识别(其它 call 结果、未知指针等) → a 标记 unknown；
//   - F 没有任何调用点(如 main / 取地址逃逸) → a 标记 unknown。
//
// noAlias(X, Y)：把 X/Y 解析成根对象集(global/alloca 自身即单元素集；参数查上表；
// 其它 unknown)，两个【已知且不相交】的根集 ⇒ 不别名（任一执行里两指针所指对象
// 必不相同）。任一 unknown ⇒ 保守 MayAlias。
//
// 纯查询、过程间一次性计算；不修改 IR。可被 DependenceAnalysis 等复用。

#include "../ir/ir.hpp"
#include "../ir/instruction.hpp"

#include <map>
#include <set>
#include <vector>

class ArgumentAliasAnalysis {
public:
    void analyze(Module *module);

    // 两个【底层基址】是否可证明不别名（已 gep 剥离到根的指针值）。
    bool noAlias(Value *baseA, Value *baseB) const;

    // 穿透 gep/bitcast 取底层对象。
    static Value *underlyingObject(Value *ptr);

private:
    // 返回参数 a 的根对象集；unknown 时返回 nullptr。
    const std::set<Value *> *argRoots(Argument *a) const;
    void resolveArg(Argument *a);

    Module *module_ = nullptr;
    std::map<Function *, std::vector<CallInst *>> callSites_;  // callee → 所有调用
    std::map<Argument *, std::set<Value *>>        roots_;     // 已知根集
    std::set<Argument *>                           unknown_;   // unknown 的参数
    std::set<Argument *>                           inProgress_;// resolveArg 递归栈(查环)
};
