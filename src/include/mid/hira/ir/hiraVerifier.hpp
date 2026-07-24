#pragma once

#include <string>

namespace hira {

class HiraRegion;

enum class HiraVerifyError {
    None,
    InvalidBoundary,
    InvalidParent,
    InvalidNode,
    InvalidOperand,
    InvalidResult,
    InvalidLoop,
    InvalidYield,
    ScopeViolation,
};

struct HiraVerificationResult {
    HiraVerifyError error = HiraVerifyError::None;
    std::string detail;

    bool succeeded() const { return error == HiraVerifyError::None; }
};

const char *hiraVerifyErrorName(HiraVerifyError error);
HiraVerificationResult verifyHiraRegion(const HiraRegion &region);

} // namespace hira
