#pragma once

#include <string>

namespace hira {

class HiraRegion;

enum class ExportRejectReason {
    None,
    InvalidRegion,
    UnsupportedExitPhi,
    UnsupportedNode,
    UnsupportedResult,
    MissingValue,
    InvalidSourceCFG,
};

struct ExportResult {
    bool changed = false;
    ExportRejectReason reason = ExportRejectReason::None;
    std::string detail;

    static ExportResult success();
    static ExportResult reject(ExportRejectReason reason,
                               std::string detail = {});
};

const char *exportRejectReasonName(ExportRejectReason reason);
ExportResult exportHiraRegion(HiraRegion &region);

} // namespace hira
