#pragma once

#include "../ir/hiraIR.hpp"

#include <memory>
#include <string>

class Loop;
class LoopInfo;

namespace hira {

enum class ImportRejectReason {
    None,
    NestedLoop,
    NonStraightLineControlFlow,
    UnsupportedHeader,
    UnsupportedPhi,
    UnsupportedInstruction,
    MissingValue,
    MissingYield,
    LiveOutInduction,
};

struct ImportResult {
    std::unique_ptr<HiraRegion> region;
    ImportRejectReason reason = ImportRejectReason::None;
    std::string detail;

    bool succeeded() const { return region != nullptr; }

    static ImportResult success(std::unique_ptr<HiraRegion> region);
    static ImportResult reject(ImportRejectReason reason,
                               std::string detail = {});
};

const char *importRejectReasonName(ImportRejectReason reason);
ImportResult importHiraRegion(Loop &loop, const LoopInfo &loopInfo);

} // namespace hira
