#include "../../include/mid/analysis/recurrenceAnalysis.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <limits>

bool RecurrenceAnalysis::fitsI32(long long value) {
    return value >= std::numeric_limits<int>::min() &&
           value <= std::numeric_limits<int>::max();
}

bool RecurrenceAnalysis::checkedAdd(long long a, long long b, long long &out) {
    if ((b > 0 && a > std::numeric_limits<long long>::max() - b) ||
        (b < 0 && a < std::numeric_limits<long long>::min() - b))
        return false;
    out = a + b;
    return true;
}

bool RecurrenceAnalysis::checkedMul(long long a, long long b, long long &out) {
    __int128 product = static_cast<__int128>(a) * static_cast<__int128>(b);
    if (product > std::numeric_limits<long long>::max() ||
        product < std::numeric_limits<long long>::min())
        return false;
    out = static_cast<long long>(product);
    return true;
}

bool RecurrenceAnalysis::checkedSub(long long a, long long b, long long &out) {
    if (b == std::numeric_limits<long long>::min()) return false;
    return checkedAdd(a, -b, out);
}

bool RecurrenceAnalysis::scevConstant(const SCEV *s, long long &value) {
    auto *constant = dynamic_cast<const SCEVConstant *>(s);
    if (!constant) return false;
    value = constant->value();
    return true;
}

bool RecurrenceAnalysis::addAffine(AffineRecurrenceStep &lhs,
                                   const AffineRecurrenceStep &rhs) {
    long long coeff = 0;
    long long constant = 0;
    if (!checkedAdd(lhs.coeff, rhs.coeff, coeff)) return false;
    if (!checkedAdd(lhs.constant, rhs.constant, constant)) return false;
    lhs.valid = true;
    lhs.coeff = coeff;
    lhs.constant = constant;
    return true;
}

bool RecurrenceAnalysis::scaleAffine(AffineRecurrenceStep &expr,
                                     long long factor) {
    long long coeff = 0;
    long long constant = 0;
    if (!checkedMul(expr.coeff, factor, coeff)) return false;
    if (!checkedMul(expr.constant, factor, constant)) return false;
    expr.coeff = coeff;
    expr.constant = constant;
    return true;
}

AffineRecurrenceStep RecurrenceAnalysis::extractAffineStep(
    const SCEV *s, PhiInst *iv, Loop *loop) const {
    AffineRecurrenceStep invalid;
    if (!s || !iv || !loop) return invalid;

    long long constant = 0;
    if (scevConstant(s, constant))
        return {true, 0, constant};

    if (auto *unknown = dynamic_cast<const SCEVUnknown *>(s)) {
        if (unknown->value() == iv)
            return {true, 1, 0};
        return invalid;
    }

    if (auto *addrec = dynamic_cast<const SCEVAddRecExpr *>(s)) {
        if (addrec->loop() != loop || addrec->phi() != iv)
            return invalid;
        long long start = 0;
        long long step = 0;
        if (!scevConstant(addrec->start(), start) ||
            !scevConstant(addrec->step(), step))
            return invalid;
        return {true, step, start};
    }

    if (auto *add = dynamic_cast<const SCEVAddExpr *>(s)) {
        AffineRecurrenceStep result{true, 0, 0};
        for (auto *op : add->operands()) {
            AffineRecurrenceStep term = extractAffineStep(op, iv, loop);
            if (!term.valid || !addAffine(result, term))
                return invalid;
        }
        return result;
    }

    if (auto *mul = dynamic_cast<const SCEVMulExpr *>(s)) {
        AffineRecurrenceStep result{true, 0, 1};
        bool sawAffine = false;
        long long factor = 1;
        for (auto *op : mul->operands()) {
            long long constVal = 0;
            if (scevConstant(op, constVal)) {
                if (!checkedMul(factor, constVal, factor))
                    return invalid;
                continue;
            }

            if (sawAffine) return invalid;
            result = extractAffineStep(op, iv, loop);
            if (!result.valid) return invalid;
            sawAffine = true;
        }
        if (!sawAffine)
            return {true, 0, factor};
        if (!scaleAffine(result, factor))
            return invalid;
        return result;
    }

    return invalid;
}

AccumulatorRecurrenceStep RecurrenceAnalysis::invalidAccumulatorStep() {
    return {false, 0, {}};
}

AccumulatorRecurrenceStep RecurrenceAnalysis::combineAccumulatorSteps(
    AccumulatorRecurrenceStep lhs,
    AccumulatorRecurrenceStep rhs,
    bool subtractRhs) {
    if (!lhs.valid || !rhs.valid) return invalidAccumulatorStep();
    if (subtractRhs && !scaleAffine(rhs.step, -1)) return invalidAccumulatorStep();
    if (!addAffine(lhs.step, rhs.step)) return invalidAccumulatorStep();
    lhs.totalRefs += rhs.totalRefs;
    return lhs;
}

AccumulatorRecurrenceStep RecurrenceAnalysis::analyzeAccumulatorStep(
    Value *value,
    PhiInst *totalPhi,
    PhiInst *iv,
    Loop *loop,
    const std::set<BasicBlock *> &loopBlocks,
    std::set<Instruction *> &chain) const {
    if (!value || !totalPhi || !iv || !loop || !SE_)
        return invalidAccumulatorStep();
    if (value == totalPhi)
        return {true, 1, {true, 0, 0}};

    auto *inst = dynamic_cast<Instruction *>(value);
    if (inst && loopBlocks.count(inst->parent_) &&
        (inst->is_add() || inst->is_sub())) {
        chain.insert(inst);
        AccumulatorRecurrenceStep lhs =
            analyzeAccumulatorStep(inst->get_operand(0), totalPhi, iv, loop,
                                   loopBlocks, chain);
        AccumulatorRecurrenceStep rhs =
            analyzeAccumulatorStep(inst->get_operand(1), totalPhi, iv, loop,
                                   loopBlocks, chain);
        return combineAccumulatorSteps(lhs, rhs, inst->is_sub());
    }

    AffineRecurrenceStep step = extractAffineStep(SE_->getSCEV(value), iv, loop);
    if (!step.valid) return invalidAccumulatorStep();
    return {true, 0, step};
}

bool RecurrenceAnalysis::computeAffineSumClosedForm(
    long long init,
    const AffineRecurrenceStep &step,
    long long iterations,
    long long &result) const {
    if (!step.valid) return false;

    long long nMinusOne = 0;
    long long pairCount = 0;
    long long triangular = 0;
    long long linearTerm = 0;
    long long constantTerm = 0;
    if (!checkedAdd(iterations, -1, nMinusOne)) return false;
    if (!checkedMul(iterations, nMinusOne, pairCount)) return false;
    triangular = pairCount / 2;
    if (!checkedMul(step.coeff, triangular, linearTerm)) return false;
    if (!checkedMul(step.constant, iterations, constantTerm)) return false;
    if (!checkedAdd(init, linearTerm, result)) return false;
    if (!checkedAdd(result, constantTerm, result)) return false;
    return fitsI32(result);
}

