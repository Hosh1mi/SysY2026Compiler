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
    if (!v) return AffineExpr::makeInvalid();

    auto it = cache_.find(v);
    if (it != cache_.end()) return it->second;

    AffineExpr result = fromSCEV(SE_.getSCEV(v));
    cache_[v] = result;
    return result;
}

AffineExpr AffineAnalysis::fromSCEV(const SCEV *s) {
    if (!s) return AffineExpr::makeInvalid();

    switch (s->kind()) {
    case SCEVKind::Constant: {
        auto *c = static_cast<const SCEVConstant*>(s);
        return AffineExpr::makeConstant(static_cast<int>(c->value()));
    }
    case SCEVKind::AddExpr: {
        auto *add = static_cast<const SCEVAddExpr*>(s);
        AffineExpr result = AffineExpr::makeConstant(0);
        for (auto *op : add->operands())
            result = result + fromSCEV(op);
        return result;
    }
    case SCEVKind::MulExpr: {
        auto *mul = static_cast<const SCEVMulExpr*>(s);
        long long coeff = 1;
        const SCEV *nonConst = nullptr;

        for (auto *op : mul->operands()) {
            if (auto *c = dynamic_cast<const SCEVConstant*>(op)) {
                coeff *= c->value();
            } else {
                if (nonConst) return AffineExpr::makeInvalid();
                nonConst = op;
            }
        }

        if (!nonConst)
            return AffineExpr::makeConstant(static_cast<int>(coeff));

        AffineExpr inner = fromSCEV(nonConst);
        if (!inner.valid) return AffineExpr::makeInvalid();
        return inner * static_cast<int>(coeff);
    }
    case SCEVKind::AddRecExpr: {
        auto *addrec = static_cast<const SCEVAddRecExpr*>(s);
        Loop *loop = addrec->loop();
        if (!loop || !loop->canonicalIV) return AffineExpr::makeInvalid();
        if (addrec->phi() != loop->canonicalIV) return AffineExpr::makeInvalid();

        AffineExpr start = fromSCEV(addrec->start());
        AffineExpr step = fromSCEV(addrec->step());
        if (!start.valid || !step.valid || !step.isConstant())
            return AffineExpr::makeInvalid();

        return start + (AffineExpr::makeIV(loop->canonicalIV) * step.constant);
    }
    case SCEVKind::Unknown:
    case SCEVKind::CouldNotCompute:
        return AffineExpr::makeInvalid();
    }

    return AffineExpr::makeInvalid();
}
