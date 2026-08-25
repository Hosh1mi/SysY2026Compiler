// DependenceAnalysis 判断两次循环访存是否可能在不同迭代命中同一地址。它先处理基址别名，
// 再把 GEP 下标写成公共循环层上的仿射式，使用精确方程、GCD 和 Banerjee 界逐层排除依赖。
// 输出方向向量供循环交换、并行化和向量化判断合法性；证明不足时保留 DIR_ANY。
#include "../../include/mid/analysis/dependenceAnalysis.hpp"
#include "../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>

// 整数 gcd（取绝对值，gcd(0, x) = |x|）
static long gcdAbs(long a, long b) {
    a = std::abs(a); b = std::abs(b);
    while (b) { long t = a % b; a = b; b = t; }
    return a;
}

GetElementPtrInst *DependenceAnalysis::accessGEP(Instruction *acc) const {
    if (auto *st = dynamic_cast<StoreInst *>(acc))
        return dynamic_cast<GetElementPtrInst *>(st->get_operand(1));
    if (auto *ld = dynamic_cast<LoadInst *>(acc))
        return dynamic_cast<GetElementPtrInst *>(ld->get_operand(0));
    return nullptr;
}

Value *DependenceAnalysis::gepBase(GetElementPtrInst *gep) const {
    if (!gep) return nullptr;
    Value *base = gep->get_operand(0);
    // 追溯到最底层非 GEP 的值（嵌套 GEP 时取根）
    while (auto *inner = dynamic_cast<GetElementPtrInst *>(base))
        base = inner->get_operand(0);
    return base;
}

DependenceAnalysis::BaseRelation
DependenceAnalysis::baseRelation(Value *a, Value *b) const {
    if (a == b) return BaseRelation::MustAlias;
    // 不同全局变量绝对不别名
    if (dynamic_cast<GlobalVariable *>(a) && dynamic_cast<GlobalVariable *>(b))
        return BaseRelation::NoAlias;
    // 过程间参数别名 oracle：可证明不别名则不别名（含参数 vs global/参数）
    if (argAA_ && argAA_->noAlias(a, b))
        return BaseRelation::NoAlias;
    return BaseRelation::MayAlias;
}

// 求两个 BB 共同最内层包围循环（沿嵌套树向上求交）
static std::vector<Loop *> commonNest(Loop *l1, Loop *l2) {
    std::vector<Loop *> chain1, chain2;
    for (Loop *c = l1; c; c = c->parent) chain1.push_back(c);
    for (Loop *c = l2; c; c = c->parent) chain2.push_back(c);
    std::reverse(chain1.begin(), chain1.end());
    std::reverse(chain2.begin(), chain2.end());
    std::vector<Loop *> common;
    for (size_t i = 0; i < chain1.size() && i < chain2.size(); i++) {
        if (chain1[i] != chain2[i]) break;
        common.push_back(chain1[i]);
    }
    return common;   // 顶层在前
}

// ── Banerjee 不等式测试 ──────────────────────────────────────────────────
// 两个访问使用独立迭代实例 i/j。对每个数组维度求
//   e1(i) - e2(j) == 0
// 的保守值域；只有值域排除 0 时才能证明无依赖。

static long long getConstantTripCount(Loop *loop) {
    auto *ci = dynamic_cast<ConstantInt *>(loop->tripCount);
    return ci ? (long long)ci->value_ : -1;
}

struct BanerjeeRange {
    __int128 lo = 0;
    __int128 hi = 0;
    bool valid = false;
};

// addLinearRange：更新目标集合或 IR 关系，并同步维护反向引用与所属信息。
static void addLinearRange(__int128 coeff, long long lo, long long hi,
                           BanerjeeRange &range) {
    __int128 a = coeff * lo;
    __int128 b = coeff * hi;
    range.lo += std::min(a, b);
    range.hi += std::max(a, b);
}

static bool hasOnlyNestIVs(const AffineExpr &expr,
                           const std::vector<Loop *> &loops) {
    for (const auto &term : expr.coeffs) {
        bool found = false;
        for (auto *loop : loops) {
            if (loop->getInductionIV() == term.first) {
                found = true;
                break;
            }
        }
        if (!found && term.second != 0) return false;
    }
    return true;
}

// direction: 0=不约束，-1=i<j，1=i>j，2=i==j。
static BanerjeeRange computeBanerjee(const AffineExpr &e1,
                                     const AffineExpr &e2,
                                     const std::vector<Loop *> &loops,
                                     int constrainedIdx,
                                     int direction) {
    BanerjeeRange result;
    result.lo = result.hi = static_cast<__int128>(e1.constant) - e2.constant;
    result.valid = hasOnlyNestIVs(e1, loops) && hasOnlyNestIVs(e2, loops);
    if (!result.valid) return result;

    for (size_t k = 0; k < loops.size(); ++k) {
        auto *iv = loops[k]->canonicalIV;
        long long trip = getConstantTripCount(loops[k]);
        if (!iv || trip <= 0) {
            result.valid = false;
            return result;
        }
        long long a = e1.coeffOf(iv);
        long long b = e2.coeffOf(iv);
        long long last = trip - 1;

        if (static_cast<int>(k) != constrainedIdx || direction == 0) {
            addLinearRange(a, 0, last, result);
            addLinearRange(-b, 0, last, result);
            continue;
        }

        if (direction == 2) {
            addLinearRange(static_cast<__int128>(a) - b, 0, last, result);
            continue;
        }

        if (trip < 2) {
            result.valid = false;
            return result;
        }

        // A linear form over i<j / i>j reaches an extremum at a vertex of
        // the triangular iteration domain.
        const long long vertices[3][2] = {
            {direction < 0 ? 0 : 1, direction < 0 ? 1 : 0},
            {direction < 0 ? 0 : last, direction < 0 ? last : 0},
            {direction < 0 ? last - 1 : last,
             direction < 0 ? last : last - 1},
        };
        __int128 termLo = static_cast<__int128>(a) * vertices[0][0] -
                          static_cast<__int128>(b) * vertices[0][1];
        __int128 termHi = termLo;
        for (int v = 1; v < 3; ++v) {
            __int128 value = static_cast<__int128>(a) * vertices[v][0] -
                             static_cast<__int128>(b) * vertices[v][1];
            termLo = std::min(termLo, value);
            termHi = std::max(termHi, value);
        }
        result.lo += termLo;
        result.hi += termHi;
    }
    return result;
}

static bool mayContainZero(const BanerjeeRange &range) {
    return range.valid && range.lo <= 0 && range.hi >= 0;
}

// ── 主测试 ──────────────────────────────────────────────────────────────

DependenceAnalysis::Result
// test：综合基址别名、仿射下标和循环界执行依赖测试，并为各循环层生成方向信息。
DependenceAnalysis::test(Instruction *acc1, Instruction *acc2) {
    Result result;

    auto *g1 = accessGEP(acc1);
    auto *g2 = accessGEP(acc2);
    if (!g1 || !g2) {
        result.aliased = true;
        return result;
    }

    Loop *l1 = LI_->getLoopFor(acc1->parent_);
    Loop *l2 = LI_->getLoopFor(acc2->parent_);
    result.commonLoops = commonNest(l1, l2);

    BaseRelation relation = baseRelation(gepBase(g1), gepBase(g2));
    if (relation == BaseRelation::NoAlias) {
        result.provably_independent = true;
        return result;
    }
    result.aliased = true;
    if (result.commonLoops.empty()) return result;

    // GEP coordinates are comparable only for the same underlying object.
    // MayAlias roots can denote the same allocation at an unknown displacement.
    if (relation == BaseRelation::MayAlias) {
        result.direction.assign(result.commonLoops.size(), DIR_ANY);
        return result;
    }

    if (g1->num_ops() != g2->num_ops()) {
        result.direction.assign(result.commonLoops.size(), DIR_ANY);
        return result;
    }

    struct Dim {
        bool affine = false;
        AffineExpr e1;
        AffineExpr e2;
        Value *o1 = nullptr;
        Value *o2 = nullptr;
    };

    std::vector<Dim> dims;
    dims.reserve(g1->num_ops() - 1);
    for (unsigned index = 1; index < g1->num_ops(); ++index) {
        Dim dim;
        dim.o1 = g1->get_operand(index);
        dim.o2 = g2->get_operand(index);
        dim.e1 = AA_->analyze(dim.o1);
        dim.e2 = AA_->analyze(dim.o2);
        dim.affine = dim.e1.valid && dim.e2.valid;
        dims.push_back(dim);
    }

    // Instance-separated GCD test. The coefficients from e1(i) and e2(j)
    // belong to independent variables and therefore must never cancel.
    for (const Dim &dim : dims) {
        if (!dim.affine) continue;
        long gcd = 0;
        for (const auto &term : dim.e1.coeffs)
            gcd = gcdAbs(gcd, static_cast<long>(term.second));
        for (const auto &term : dim.e2.coeffs)
            gcd = gcdAbs(gcd, static_cast<long>(term.second));
        long rhs = static_cast<long>(dim.e2.constant) - dim.e1.constant;
        if ((gcd == 0 && rhs != 0) || (gcd != 0 && rhs % gcd != 0)) {
            result.provably_independent = true;
            return result;
        }
    }

    bool canUseBounds = true;
    for (const Dim &dim : dims) {
        if (!dim.affine ||
            !hasOnlyNestIVs(dim.e1, result.commonLoops) ||
            !hasOnlyNestIVs(dim.e2, result.commonLoops)) {
            canUseBounds = false;
            break;
        }
    }
    for (Loop *loop : result.commonLoops)
        if (!loop->canonicalIV || getConstantTripCount(loop) <= 0)
            canUseBounds = false;

    if (canUseBounds) {
        for (const Dim &dim : dims) {
            if (!mayContainZero(
                    computeBanerjee(dim.e1, dim.e2, result.commonLoops,
                                     /*constrainedIdx=*/-1,
                                     /*direction=*/0))) {
                result.provably_independent = true;
                return result;
            }
        }

        for (size_t level = 0; level < result.commonLoops.size(); ++level) {
            auto directionPossible = [&](int direction) {
                for (const Dim &dim : dims) {
                    BanerjeeRange range =
                        computeBanerjee(dim.e1, dim.e2, result.commonLoops,
                                         static_cast<int>(level), direction);
                    if (!mayContainZero(range)) return false;
                }
                return true;
            };

            bool lt = directionPossible(-1);
            bool eq = directionPossible(2);
            bool gt = directionPossible(1);
            int count = static_cast<int>(lt) + static_cast<int>(eq) +
                        static_cast<int>(gt);
            if (count == 0) {
                result.provably_independent = true;
                result.direction.clear();
                return result;
            }
            if (count != 1)
                result.direction.push_back(DIR_ANY);
            else if (lt)
                result.direction.push_back(DIR_LT);
            else if (gt)
                result.direction.push_back(DIR_GT);
            else
                result.direction.push_back(DIR_EQ);
        }
        return result;
    }

    // Symbolic fallback: recognize only a strong SIV equation. Everything
    // coupled, opaque, or absent from the address remains conservatively ANY.
    for (Loop *loop : result.commonLoops) {
        PhiInst *iv = loop->getInductionIV();
        if (!iv && loop == inductionOverrideLoop_)
            iv = inductionOverrideIV_;
        if (!iv) {
            result.direction.push_back(DIR_ANY);
            continue;
        }

        int dimensionsWithIV = 0;
        bool coupled = false;
        bool opaqueUse = false;
        bool distanceKnown = false;
        long distance = 0;

        for (const Dim &dim : dims) {
            if (!dim.affine) {
                if (!AffineAnalysis::provablyIndependentOfIV(dim.o1, iv) ||
                    !AffineAnalysis::provablyIndependentOfIV(dim.o2, iv))
                    opaqueUse = true;
                continue;
            }

            long a = dim.e1.coeffOf(iv);
            long b = dim.e2.coeffOf(iv);
            if (a == 0 && b == 0) continue;
            ++dimensionsWithIV;

            bool hasOtherIV = false;
            for (const auto &term : dim.e1.coeffs)
                if (term.first != iv && term.second != 0) hasOtherIV = true;
            for (const auto &term : dim.e2.coeffs)
                if (term.first != iv && term.second != 0) hasOtherIV = true;

            if (a == 0 || a != b || hasOtherIV) {
                coupled = true;
                continue;
            }
            long numerator =
                static_cast<long>(dim.e1.constant) - dim.e2.constant;
            if (numerator % a != 0) {
                coupled = true;
                continue;
            }
            distance = numerator / a;
            distanceKnown = true;
        }

        if (opaqueUse || coupled || dimensionsWithIV != 1 || !distanceKnown) {
            result.direction.push_back(DIR_ANY);
        } else if (distance == 0) {
            result.direction.push_back(DIR_EQ);
        } else {
            result.direction.push_back(distance > 0 ? DIR_LT : DIR_GT);
        }
    }

    return result;
}

DependenceAnalysis::DistanceResult
// getConstantDistance：从 IR 和已有分析结果取得目标信息；缺少可靠结论时返回空值或保守结果。
DependenceAnalysis::getConstantDistance(
    Instruction *source, Instruction *sink,
    const std::vector<Loop *> &nest) {
    DistanceResult result;
    auto *sourceGEP = accessGEP(source);
    auto *sinkGEP = accessGEP(sink);
    if (!sourceGEP || !sinkGEP || nest.empty()) return result;

    BaseRelation relation = baseRelation(gepBase(sourceGEP), gepBase(sinkGEP));
    if (relation == BaseRelation::NoAlias) {
        result.status = DistanceStatus::NoDependence;
        return result;
    }
    if (relation != BaseRelation::MustAlias ||
        sourceGEP->num_ops() != sinkGEP->num_ops())
        return result;

    struct Equation {
        std::vector<long long> coeffs;
        long long rhs = 0;
    };
    std::vector<Equation> equations;
    for (unsigned index = 1; index < sourceGEP->num_ops(); ++index) {
        AffineExpr sourceExpr = AA_->analyze(sourceGEP->get_operand(index));
        AffineExpr sinkExpr = AA_->analyze(sinkGEP->get_operand(index));
        if (!sourceExpr.valid || !sinkExpr.valid) return result;

        Equation equation;
        equation.rhs = static_cast<long long>(sourceExpr.constant) -
                       static_cast<long long>(sinkExpr.constant);
        for (Loop *loop : nest) {
            PhiInst *iv = loop ? loop->getInductionIV() : nullptr;
            if (!iv) return result;
            long long sourceCoeff = sourceExpr.coeffOf(iv);
            long long sinkCoeff = sinkExpr.coeffOf(iv);
            if (sourceCoeff != sinkCoeff) return result;
            equation.coeffs.push_back(sourceCoeff);
        }
        for (const auto &term : sourceExpr.coeffs) {
            bool inNest = false;
            for (Loop *loop : nest)
                inNest |= loop && loop->getInductionIV() == term.first;
            if (!inNest && term.second != sinkExpr.coeffOf(term.first))
                return result;
        }
        for (const auto &term : sinkExpr.coeffs) {
            bool inNest = false;
            for (Loop *loop : nest)
                inNest |= loop && loop->getInductionIV() == term.first;
            if (!inNest && term.second != sourceExpr.coeffOf(term.first))
                return result;
        }
        equations.push_back(std::move(equation));
    }

    std::vector<long long> distance(nest.size(), 0);
    for (size_t column = 0; column < nest.size(); ++column) {
        const Equation *selected = nullptr;
        for (const Equation &equation : equations) {
            if (equation.coeffs[column] != 1 &&
                equation.coeffs[column] != -1)
                continue;
            bool separates = true;
            for (size_t other = 0; other < nest.size(); ++other)
                if (other != column && equation.coeffs[other] != 0)
                    separates = false;
            if (separates) {
                selected = &equation;
                break;
            }
        }
        if (!selected) return result;
        distance[column] = selected->rhs / selected->coeffs[column];
    }

    for (const Equation &equation : equations) {
        __int128 lhs = 0;
        for (size_t i = 0; i < distance.size(); ++i)
            lhs += static_cast<__int128>(equation.coeffs[i]) * distance[i];
        if (lhs != equation.rhs) {
            result.status = DistanceStatus::NoDependence;
            return result;
        }
    }
    result.status = DistanceStatus::Exact;
    result.distance = std::move(distance);
    return result;
}
// ── 循环交换合法性 ─────────────────────────────────────────────────────────
// 检查给定一组访问之间的依赖方向，看 outer/inner 互换后是否反转。
// 简化规则：
//   - 如果某依赖方向在 (outer_idx, inner_idx) 上是 (<, >)，交换后变 (>, <) 反转 → 非法
//   - (=, *)、(*, =) 都合法
//   - 含 ANY 时保守允许（注：真实工业实现会更严格）

bool DependenceAnalysis::isInterchangeLegal(
    Loop *outer, Loop *inner,
    const std::vector<Instruction *> &accesses)
{
    int n = (int)accesses.size();
    for (int i = 0; i < n; i++) {
        for (int j = i; j < n; j++) {
            // 至少一个是 store 才有 RAW/WAR/WAW 风险（read-read 无碍）
            bool has_store = accesses[i]->is_store() || accesses[j]->is_store();
            if (!has_store) continue;

            Result r = test(accesses[i], accesses[j]);
            if (r.provably_independent) continue;

            // 找 outer/inner 在 commonLoops 里的位置
            auto it_o = std::find(r.commonLoops.begin(), r.commonLoops.end(), outer);
            auto it_i = std::find(r.commonLoops.begin(), r.commonLoops.end(), inner);
            if (it_o == r.commonLoops.end() || it_i == r.commonLoops.end())
                return false;
            int idx_o = it_o - r.commonLoops.begin();
            int idx_i = it_i - r.commonLoops.begin();
            if (idx_o >= (int)r.direction.size() || idx_i >= (int)r.direction.size())
                return false;

            Dir d_o = r.direction[idx_o];
            Dir d_i = r.direction[idx_i];

            // Swapping an equality component with any other component leaves
            // the first non-equality direction unchanged.  This is important
            // for recurrences whose address is invariant in the outer loop:
            // the outer direction can be unknown while the inner dimension is
            // still proven equal.  Otherwise, unknown or opposing components
            // may reverse the dependence and must be rejected.
            if (d_o == DIR_EQ || d_i == DIR_EQ)
                continue;
            if (d_o == DIR_ANY || d_i == DIR_ANY || d_o != d_i)
                return false;
        }
    }
    return true;
}

// ── 单层并行性 ─────────────────────────────────────────────────────────────
// L 是否携带依赖（不同 L 迭代之间存在内存依赖）。
//
// 复用 test() 的 instance-separated 结果。只有方向被证明为 EQ 时，
// 才能断言依赖局限于同一次 L 迭代。
bool DependenceAnalysis::loopCarriesDependence(
    Loop *L, const std::vector<Instruction *> &accesses)
{
    if (!L) return true;

    int n = (int)accesses.size();
    for (int i = 0; i < n; i++) {
        for (int j = i; j < n; j++) {
            bool has_store = accesses[i]->is_store() || accesses[j]->is_store();
            if (!has_store) continue;   // read-read 无依赖

            Result result = test(accesses[i], accesses[j]);
            if (result.provably_independent) continue;

            auto level = std::find(result.commonLoops.begin(),
                                   result.commonLoops.end(), L);
            if (level == result.commonLoops.end()) return true;
            size_t index = level - result.commonLoops.begin();
            if (index >= result.direction.size() ||
                result.direction[index] != DIR_EQ)
                return true;
        }
    }
    return false;
}
