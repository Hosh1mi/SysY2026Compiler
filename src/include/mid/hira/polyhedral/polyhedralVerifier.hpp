#pragma once

#include <string>

namespace hira::polyhedral {

class PolyhedralModel;

enum class PolyhedralVerifyError {
    None,
    InvalidSpace,
    InvalidDomain,
    InvalidConstraint,
    InvalidStatement,
    InvalidSchedule,
    InvalidMemoryObject,
    InvalidAccess,
    InvalidScalarFlow,
    InvalidRecurrence,
    InvalidAlias,
    MissingProofObligation,
};

struct PolyhedralVerificationResult {
    PolyhedralVerifyError error = PolyhedralVerifyError::None;
    std::string detail;

    bool succeeded() const {
        return error == PolyhedralVerifyError::None;
    }
};

const char *polyhedralVerifyErrorName(PolyhedralVerifyError error);
PolyhedralVerificationResult
verifyPolyhedralModel(const PolyhedralModel &model);

} // namespace hira::polyhedral
