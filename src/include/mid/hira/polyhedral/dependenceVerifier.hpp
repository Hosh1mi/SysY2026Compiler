#pragma once

#include <string>

namespace hira::polyhedral {

class DependenceSet;
class PolyhedralModel;

enum class DependenceVerifyError {
    None,
    InvalidRelation,
    InvalidEndpoint,
    InvalidConstraint,
};

struct DependenceVerificationResult {
    DependenceVerifyError error = DependenceVerifyError::None;
    std::string detail;

    bool succeeded() const {
        return error == DependenceVerifyError::None;
    }
};

const char *dependenceVerifyErrorName(DependenceVerifyError error);
DependenceVerificationResult
verifyDependenceRelations(const PolyhedralModel &model,
                          const DependenceSet &dependences);

} // namespace hira::polyhedral
