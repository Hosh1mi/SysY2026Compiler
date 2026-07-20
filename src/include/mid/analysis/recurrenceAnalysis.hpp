#pragma once

#include "loopInfo.hpp"
#include "scalarEvolution.hpp"

#include <set>

struct AffineRecurrenceStep {
    bool valid = false;
    long long coeff = 0;
    long long constant = 0;
};

struct AccumulatorRecurrenceStep {
    bool valid = false;
    int totalRefs = 0;
    AffineRecurrenceStep step;
};

class RecurrenceAnalysis {
public:
    explicit RecurrenceAnalysis(ScalarEvolution &SE) : SE_(&SE) {}

    static bool fitsI32(long long value);
    static bool checkedAdd(long long a, long long b, long long &out);
    static bool checkedMul(long long a, long long b, long long &out);
    static bool checkedSub(long long a, long long b, long long &out);
    static bool scevConstant(const SCEV *s, long long &value);

    AccumulatorRecurrenceStep analyzeAccumulatorStep(
        Value *value,
        PhiInst *totalPhi,
        PhiInst *iv,
        Loop *loop,
        const std::set<BasicBlock *> &loopBlocks,
        std::set<Instruction *> &chain) const;

    bool computeAffineSumClosedForm(long long init,
                                    const AffineRecurrenceStep &step,
                                    long long iterations,
                                    long long &result) const;

private:
    AffineRecurrenceStep extractAffineStep(const SCEV *s,
                                           PhiInst *iv,
                                           Loop *loop) const;
    static bool addAffine(AffineRecurrenceStep &lhs,
                          const AffineRecurrenceStep &rhs);
    static bool scaleAffine(AffineRecurrenceStep &expr, long long factor);
    static AccumulatorRecurrenceStep invalidAccumulatorStep();
    static AccumulatorRecurrenceStep combineAccumulatorSteps(
        AccumulatorRecurrenceStep lhs,
        AccumulatorRecurrenceStep rhs,
        bool subtractRhs);

    ScalarEvolution *SE_;
};

