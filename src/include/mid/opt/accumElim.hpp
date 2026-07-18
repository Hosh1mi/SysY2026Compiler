#pragma once
#include "../opt/pass.hpp"

class Function;

// 累积参数消除 + 哈希记忆化（pre-TRE pass）。
//
// 识别形如
//     f(x..., acc) {
//         if (n == ...) return acc + C0;        // acc-dep base
//         if (...)      return f(child(x), acc + Δi);
//         return Kcap;                          // acc-indep "cap" base
//     }
// 的纯自递归函数（所有非自调用 callee 为 pure；函数自身无 store）。
//
// 变换为：
//     aux(x...) 返回 i32：
//         若该路径最终命中 acc-dep base，返回累计 Δ + C0；
//         若命中 cap base，返回 INT_MIN（SENTINEL）。
//     f(x..., acc) 改写为：
//         r = aux(x...);
//         return r == SENTINEL ? Kcap : r + acc;
// 并在 aux 入口装 AutoMemoization 的哈希缓存。
//
// 通用结构特征（不依赖函数名或具体输入）：
//   1. 纯函数 + 自递归
//   2. 存在 i32 形参 acc，所有 acc 的使用都是 (a) 直接 return；
//      (b) Add(acc-derived, ConstInt)，结果或被 return 或作为自调用同位置实参；
//      (c) 在该位置传给自调用
//   3. 自调用其他位置实参不依赖 acc
//   4. 所有 return 形为 ret acc-derived / ret call-result / ret ConstInt(K)
//   5. 所有 acc-indep return 的常量必须相同（Kcap）
class AccumElim : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "AccumElim"; }
};
