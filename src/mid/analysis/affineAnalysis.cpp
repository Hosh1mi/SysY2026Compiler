// AffineAnalysis 将整数下标化成“常数 + 若干归纳变量的一次组合”。它借助 SCEV 识别
// 常量、加法、常量乘法和固定步长递推，拒绝变量相乘等非线性结构。结果主要提供给循环
// 依赖测试、循环交换和访存步长分析，用来判断不同迭代的数组下标是否可能相等。
#include "../../include/mid/analysis/affineAnalysis.hpp"
#include "../../include/mid/ir/basicBlock.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <set>
#include <sstream>

// v 是否可证明与 iv 无关：use-def 反向可达；load 保守判有关；环用 seen 截断。
static bool ivIndepImpl(Value *v, PhiInst *iv, std::set<Value *> &seen) {
    if (v == iv)                      return false;
    if (dynamic_cast<LoadInst *>(v))  return false;
    if (!seen.insert(v).second)       return true;
    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst)                        return true;
    for (unsigned i = 0; i < inst->num_ops(); i++) {
        Value *op = inst->get_operand(i);
        if (dynamic_cast<BasicBlock *>(op)) continue;
        if (!ivIndepImpl(op, iv, seen)) return false;
    }
    return true;
}

// provablyIndependentOfIV：沿 use-def 链检查值是否依赖指定归纳变量；load 等未知来源保守视为相关。
bool AffineAnalysis::provablyIndependentOfIV(Value *v, PhiInst *iv) {
    if (!v || !iv) return false;
    std::set<Value *> seen;
    return ivIndepImpl(v, iv, seen);
}

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

// canonicalize：合并相同归纳变量的系数并删除零项，使仿射表达式具有稳定形式。
void AffineExpr::canonicalize() {
    for (auto it = coeffs.begin(); it != coeffs.end();) {
        if (it->second == 0) it = coeffs.erase(it);
        else                 ++it;
    }
}

// print：按项目 IR 文本格式输出节点类型、操作数和必要属性。
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
    if (!result.valid) {
        if (auto *binary = dynamic_cast<BinaryInst *>(v)) {
            if (binary->is_add() || binary->is_sub()) {
                AffineExpr lhs = analyze(binary->get_operand(0));
                AffineExpr rhs = analyze(binary->get_operand(1));
                result = binary->is_add() ? lhs + rhs : lhs - rhs;
            } else if (binary->is_mul()) {
                auto *lhsConstant =
                    dynamic_cast<ConstantInt *>(binary->get_operand(0));
                auto *rhsConstant =
                    dynamic_cast<ConstantInt *>(binary->get_operand(1));
                if (lhsConstant)
                    result = analyze(binary->get_operand(1)) *
                             static_cast<int>(lhsConstant->value_);
                else if (rhsConstant)
                    result = analyze(binary->get_operand(0)) *
                             static_cast<int>(rhsConstant->value_);
            } else if (binary->op_id_ == Instruction::Shl) {
                auto *shift =
                    dynamic_cast<ConstantInt *>(binary->get_operand(1));
                if (shift && shift->value_ >= 0 && shift->value_ < 31)
                    result = analyze(binary->get_operand(0)) *
                             (1 << shift->value_);
            }
        }
    }
    cache_[v] = result;
    return result;
}

// fromSCEV：把 SCEV 常量、加法、常量乘法和 AddRec 转成仿射式，非线性节点返回无效。
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
        if (!addrec->loop() || !addrec->phi())
            return AffineExpr::makeInvalid();

        AffineExpr start = fromSCEV(addrec->start());
        AffineExpr step = fromSCEV(addrec->step());
        if (!start.valid || !step.valid || !step.isConstant())
            return AffineExpr::makeInvalid();

        return start + (AffineExpr::makeIV(addrec->phi()) * step.constant);
    }
    case SCEVKind::Unknown:
    case SCEVKind::CouldNotCompute:
        return AffineExpr::makeInvalid();
    }

    return AffineExpr::makeInvalid();
}
