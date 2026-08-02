#pragma once

#include "../ir/ir.hpp"

// A composable summary for an integer expression that can be summed over an
// arithmetic induction sequence.  The represented value is
//
//   (linearMultiplier * u
//      + quotientMultiplier * ((divisionMultiplier * u) / divisor)
//      + constant) % modulus
//
// where u is either the induction value itself or a single min/max selection
// between two affine functions of the induction.  All arithmetic before the
// signed remainder has i32 semantics.
namespace SummableExpressionAnalysis {

struct LinearFloorExpression {
    PhiInst *induction = nullptr;
    bool piecewise = false;
    int lhsMultiplier = 1;
    int lhsConstant = 0;
    int rhsMultiplier = 1;
    int rhsConstant = 0;
    bool trueUsesRight = true;
    int linearMultiplier = 0;
    int divisionMultiplier = 0;
    int divisor = 1;
    int quotientMultiplier = 0;
    int constant = 0;
    int modulus = 0;
};

// Recognize the expression by composing independent add/sub, constant-mul,
// constant-div, reflected-max and signed-modulo rules.  No loop or function
// name participates in the result.
bool analyzeModular(Value *value, PhiInst *induction,
                    LinearFloorExpression &result);

} // namespace SummableExpressionAnalysis
