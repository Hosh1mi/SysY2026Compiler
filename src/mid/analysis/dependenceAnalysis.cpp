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


// ── helpers ─────────────────────────────────────────────────────────────

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

bool DependenceAnalysis::sameBase(Value *a, Value *b) const {
    if (a == b) return true;
    // 不同全局变量绝对不别名
    if (dynamic_cast<GlobalVariable *>(a) && dynamic_cast<GlobalVariable *>(b))
        return false;
    // 过程间参数别名 oracle：可证明不别名则不别名（含参数 vs global/参数）
    if (argAA_ && argAA_->noAlias(a, b))
        return false;
    return true;   // 保守：可能别名
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

// ── 主测试 ──────────────────────────────────────────────────────────────

DependenceAnalysis::Result
DependenceAnalysis::test(Instruction *acc1, Instruction *acc2) {
    Result r;

    auto *g1 = accessGEP(acc1);
    auto *g2 = accessGEP(acc2);
    if (!g1 || !g2) {
        r.aliased = true;        // 保守：可能别名
        return r;
    }

    Value *b1 = gepBase(g1);
    Value *b2 = gepBase(g2);
    if (!sameBase(b1, b2)) {
        r.provably_independent = true;
        return r;
    }
    r.aliased = true;

    // 共同嵌套循环
    Loop *l1 = LI_->getLoopFor(acc1->parent_);
    Loop *l2 = LI_->getLoopFor(acc2->parent_);
    r.commonLoops = commonNest(l1, l2);
    if (r.commonLoops.empty()) return r;   // 不在任何共同循环里

    // GEP 索引数得一致
    if (g1->num_ops_ != g2->num_ops_) return r;
    unsigned n_idx = g1->num_ops_ - 1;

    // 逐维提取【两侧各自】的仿射下标（instance-separated）。acc1 用迭代实例 i1，
    // acc2 用迭代实例 i2——两个实例的同名 IV 是【独立变量】，绝不做同迭代抵消。
    // 这是循环携带依赖判定的关键：同迭代 diff 只能证"同一次迭代不撞地址"，
    // 不能证跨迭代无依赖（如 a[i+1] 写 vs a[i] 读，距离 1 的真 RAW 携带依赖）。
    // 某维任一侧无法仿射化时标记为 opaque，只影响该维，不污染其它维。
    struct Dim {
        bool       affine = false;
        AffineExpr e1, e2;          // affine=true 时有效（分别是 acc1/acc2 的下标）
        Value     *o1 = nullptr;    // 原始下标（opaque 时用于 use-def 可达判定）
        Value     *o2 = nullptr;
    };
    std::vector<Dim> dims;
    dims.reserve(n_idx);
    for (unsigned k = 0; k < n_idx; k++) {
        Value *o1 = g1->get_operand(k + 1);
        Value *o2 = g2->get_operand(k + 1);
        AffineExpr a = AA_->analyze(o1);
        AffineExpr b = AA_->analyze(o2);
        Dim d;
        d.o1 = o1;
        d.o2 = o2;
        if (a.valid && b.valid) { d.affine = true; d.e1 = a; d.e2 = b; }
        dims.push_back(d);
    }

    // GCD test（instance-separated）：某可仿射维的依赖方程
    //   Σ A_iv·i1_iv − Σ B_iv·i2_iv = e2.const − e1.const
    // 里全体整系数（两侧的 A_iv 与 B_iv 都算，因 i1/i2 是独立变量）的 gcd
    // 不整除右端常数 → 该维无整数解 → 整对无依赖。任一维证得即独立。
    for (auto &d : dims) {
        if (!d.affine) continue;
        long g = 0;
        for (auto &kv : d.e1.coeffs) g = gcdAbs(g, (long)kv.second);
        for (auto &kv : d.e2.coeffs) g = gcdAbs(g, (long)kv.second);
        long rhs = (long)d.e2.constant - (long)d.e1.constant;
        if (g == 0) {
            if (rhs != 0) { r.provably_independent = true; return r; }
        } else {
            if (rhs % g != 0) { r.provably_independent = true; return r; }
        }
    }

    // 方向：对每个共同嵌套循环 L（IV=kIV），判断依赖是否【强制 i1_L = i2_L】。
    //   强 SIV：kIV 只出现在唯一一维、两侧系数相等(s≠0)、该维再无其它 IV →
    //     该维方程 s·i1_L + c1 = s·i2_L + c2 精确给出距离 δ = i2_L − i1_L =
    //     (c1 − c2)/s，方向取 δ 符号（δ=0 才是 EQ=不携带）。
    //   kIV 完全不进任何下标 → 地址对 L 不变 → 任意 L 迭代对同址 → ANY（携带）。
    //   其余（系数不等/耦合多 IV/跨多维/opaque 里含 kIV）→ 无法证等 → 保守 ANY。
    for (Loop *loop : r.commonLoops) {
        PhiInst *kIV = loop->canonicalIV;
        if (!kIV) { r.direction.push_back(DIR_ANY); continue; }

        int  dimsWithKiv = 0;
        bool coupled = false, kivInOpaque = false;
        long dist = 0;
        bool distKnown = false;
        for (auto &d : dims) {
            if (!d.affine) {
                // opaque 维：kIV 可能喂入任一侧即无法给出干净方向 → 保守。
                if (!AffineAnalysis::provablyIndependentOfIV(d.o1, kIV) ||
                    !AffineAnalysis::provablyIndependentOfIV(d.o2, kIV))
                    kivInOpaque = true;
                continue;
            }
            long a = d.e1.coeffOf(kIV);
            long b = d.e2.coeffOf(kIV);
            if (a == 0 && b == 0) continue;   // kIV 不在此维
            dimsWithKiv++;
            bool otherIv = false;
            for (auto &kv : d.e1.coeffs)
                if (kv.first != kIV && kv.second != 0) otherIv = true;
            for (auto &kv : d.e2.coeffs)
                if (kv.first != kIV && kv.second != 0) otherIv = true;
            if (a != b || otherIv) { coupled = true; continue; }  // 非强 SIV
            long num = (long)d.e1.constant - (long)d.e2.constant;  // = δ·s
            if (num % a == 0) { dist = num / a; distKnown = true; }
            else coupled = true;   // GCD 理应已拦，兜底保守
        }

        if (kivInOpaque || coupled || dimsWithKiv > 1)
            r.direction.push_back(DIR_ANY);
        else if (dimsWithKiv == 0)
            r.direction.push_back(DIR_ANY);         // 地址对 L 不变 → 携带
        else if (distKnown)
            r.direction.push_back(dist == 0 ? DIR_EQ
                                            : (dist > 0 ? DIR_LT : DIR_GT));
        else
            r.direction.push_back(DIR_ANY);
    }

    return r;
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
            if (it_o == r.commonLoops.end() || it_i == r.commonLoops.end()) continue;
            int idx_o = it_o - r.commonLoops.begin();
            int idx_i = it_i - r.commonLoops.begin();
            if (idx_o >= (int)r.direction.size() || idx_i >= (int)r.direction.size())
                continue;

            Dir d_o = r.direction[idx_o];
            Dir d_i = r.direction[idx_i];

            // 反转条件：原本 outer < inner > → 交换后变 outer > inner < 反转
            // 但我们的方向只到 ANY 粒度，没法判 < 还是 >，所以：
            //   - 若两者都是 EQ → 安全
            //   - 否则保守判定：若 outer 不是 EQ（说明有依赖跨外层）且 inner 不是 EQ → 不安全
            if (d_o != DIR_EQ && d_i != DIR_EQ) {
                return false;
            }
        }
    }
    return true;
}

// ── 单层并行性 ─────────────────────────────────────────────────────────────
// L 是否携带依赖（不同 L 迭代之间存在内存依赖）。
//
// 复用 test() 的 instance-separated 判定：对每对可能别名、含 store 的访问，
// 若已证独立则跳过；否则查该对在 L 这一层的方向——方向为 EQ（依赖强制
// i1_L=i2_L，仅同迭代碰撞）才不携带，其余（LT/GT/ANY，或 L 不在共同嵌套层、
// 访问无法解析）一律判为携带。无 canonical IV 时 test() 已对该层给出 ANY。
bool DependenceAnalysis::loopCarriesDependence(
    Loop *L, const std::vector<Instruction *> &accesses)
{
    if (!L) return true;

    int n = (int)accesses.size();
    for (int i = 0; i < n; i++) {
        for (int j = i; j < n; j++) {
            bool has_store = accesses[i]->is_store() || accesses[j]->is_store();
            if (!has_store) continue;   // read-read 无依赖

            Result r = test(accesses[i], accesses[j]);
            if (r.provably_independent) continue;

            auto it = std::find(r.commonLoops.begin(), r.commonLoops.end(), L);
            if (it == r.commonLoops.end()) return true;   // 无法定位 L → 保守携带
            size_t idx = it - r.commonLoops.begin();
            if (idx >= r.direction.size()) return true;   // 方向缺失 → 保守
            if (r.direction[idx] != DIR_EQ) return true;  // 非同迭代 → 携带
        }
    }
    return false;
}
