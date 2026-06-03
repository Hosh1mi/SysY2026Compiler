#include "../../include/mid/analysis/affineAnalysis.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <sstream>

// ── AffineExpr 算术 ───────────────────────────────────────────────────────

AffineExpr AffineExpr::operator+(const AffineExpr &o) const {
    if (!valid || !o.valid) return makeInvalid();
    AffineExpr r;
    r.constant = constant + o.constant;
    r.coeffs   = coeffs;
    for (auto &[iv, c] : o.coeffs) r.coeffs[iv] += c;
    r.canonicalize();
    return r;
}

AffineExpr AffineExpr::operator-(const AffineExpr &o) const {
    return (*this) + o.negate();
}

AffineExpr AffineExpr::operator*(int k) const {
    if (!valid) return makeInvalid();
    AffineExpr r;
    r.constant = constant * k;
    if (k == 0) return r;
    for (auto &[iv, c] : coeffs) r.coeffs[iv] = c * k;
    return r;
}

void AffineExpr::canonicalize() {
    for (auto it = coeffs.begin(); it != coeffs.end();) {
        if (it->second == 0) it = coeffs.erase(it);
        else                 ++it;
    }
}

std::string AffineExpr::print() const {
    if (!valid) return "<invalid>";
    std::ostringstream oss;
    bool first = true;
    if (constant != 0) {
        oss << constant;
        first = false;
    }
    for (auto &[iv, c] : coeffs) {
        if (c == 0) continue;
        if (!first) oss << (c > 0 ? " + " : " - ");
        else if (c < 0) oss << "-";
        int abs_c = c < 0 ? -c : c;
        if (abs_c != 1) oss << abs_c << "·";
        oss << (iv->name_.empty() ? "<anon>" : iv->name_);
        first = false;
    }
    if (first) oss << "0";
    return oss.str();
}

// ── AffineAnalysis ─────────────────────────────────────────────────────────

AffineExpr AffineAnalysis::analyze(Value *v) {
    visiting_.clear();
    return analyzeImpl(v);
}

AffineExpr AffineAnalysis::analyzeImpl(Value *v) {
    if (!v) return AffineExpr::makeInvalid();

    auto it = cache_.find(v);
    if (it != cache_.end()) return it->second;

    if (!visiting_.insert(v).second) {
        // 出现循环引用，无法静态展开
        return AffineExpr::makeInvalid();
    }

    AffineExpr result = AffineExpr::makeInvalid();

    // 1. 常量
    if (auto *ci = dynamic_cast<ConstantInt *>(v)) {
        result = AffineExpr::makeConstant(ci->value_);
    }
    // 2. 规范 IV（必须是某个 loop 的 canonical IV）
    else if (auto *phi = dynamic_cast<PhiInst *>(v)) {
        Loop *loop = LI_->getLoopFor(phi->parent_);
        // 沿嵌套树向上找是不是哪一层 loop 的 canonical IV
        bool is_iv = false;
        for (Loop *cur = loop; cur; cur = cur->parent) {
            if (cur->canonicalIV == phi) {
                is_iv = true;
                break;
            }
        }
        if (is_iv) result = AffineExpr::makeIV(phi);
    }
    // 3. 二元运算
    else if (auto *bin = dynamic_cast<BinaryInst *>(v)) {
        Value *a = bin->get_operand(0);
        Value *b = bin->get_operand(1);
        if (bin->is_add()) {
            result = analyzeImpl(a) + analyzeImpl(b);
        } else if (bin->op_id_ == Instruction::Sub) {
            result = analyzeImpl(a) - analyzeImpl(b);
        } else if (bin->is_mul()) {
            // 必须一侧是常量
            auto *ca = dynamic_cast<ConstantInt *>(a);
            auto *cb = dynamic_cast<ConstantInt *>(b);
            if (ca)      result = analyzeImpl(b) * ca->value_;
            else if (cb) result = analyzeImpl(a) * cb->value_;
            // 两个 IV 相乘 → 非线性 → invalid
        }
    }
    // 4. 其它（load / call / gep 等）→ invalid

    visiting_.erase(v);
    cache_[v] = result;
    return result;
}
