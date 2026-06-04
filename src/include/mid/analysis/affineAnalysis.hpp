#pragma once
// AffineAnalysis: 把 IR Value 表达成关于规范归纳变量的仿射形式
//   AffineExpr = constant + Σ coeffs[iv] * iv
//
// 支持的源形态（对每个 Value）：
//   ConstantInt              → c
//   canonical IV phi (LI 识别) → 1·iv
//   add(a,b)                  → analyze(a) + analyze(b)
//   sub(a,b)                  → analyze(a) - analyze(b)
//   mul(c, x) / mul(x, c)    → c · analyze(x)
//   其它                       → invalid
//
// 主要服务对象是 DependenceAnalysis 和 CostModel：能告诉它们
// "GEP 的第 i 个 index 等价于 a·iv₁ + b·iv₂ + c"，从而判断访存方向。

#include "loopInfo.hpp"
#include "scalarEvolution.hpp"
#include <map>
#include <set>

class AffineExpr {
public:
    int                     constant = 0;
    std::map<PhiInst *, int> coeffs;   // iv → coefficient（缺省即系数 0）
    bool                    valid    = true;

    // 工厂
    static AffineExpr makeConstant(int c) {
        AffineExpr e;
        e.constant = c;
        return e;
    }
    static AffineExpr makeIV(PhiInst *iv) {
        AffineExpr e;
        e.coeffs[iv] = 1;
        return e;
    }
    static AffineExpr makeInvalid() {
        AffineExpr e;
        e.valid = false;
        return e;
    }

    // 运算
    AffineExpr operator+(const AffineExpr &o) const;
    AffineExpr operator-(const AffineExpr &o) const;
    AffineExpr operator*(int k) const;
    AffineExpr negate() const { return (*this) * -1; }

    // 查询
    bool isZero()     const { return valid && constant == 0 && coeffs.empty(); }
    bool isConstant() const { return valid && coeffs.empty(); }
    int  coeffOf(PhiInst *iv) const {
        auto it = coeffs.find(iv);
        return it == coeffs.end() ? 0 : it->second;
    }
    // 删掉系数为 0 的项（运算结果可能产生 0）
    void canonicalize();

    std::string print() const;
};

class AffineAnalysis {
public:
    explicit AffineAnalysis(const LoopInfo &LI) : LI_(&LI), SE_(LI) {}

    // 主入口：返回 v 的仿射表示；若无法表达则 valid=false
    AffineExpr analyze(Value *v);

    void clearCache() { cache_.clear(); SE_.clear(); }

private:
    AffineExpr fromSCEV(const SCEV *s);

    const LoopInfo                  *LI_;
    ScalarEvolution                  SE_;
    std::map<Value *, AffineExpr>    cache_;
};
