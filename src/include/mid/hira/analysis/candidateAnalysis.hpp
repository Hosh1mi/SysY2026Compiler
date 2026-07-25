#pragma once

#include <string>

class Loop;
class LoopInfo;

namespace hira {

enum class CandidateRejectReason {
    None,
    MissingHeader,
    MissingPreheader,
    MultipleLatches,
    MultipleEntry,
    MultipleExitingBlocks,
    MultipleExits,
    NonDedicatedExit,
    MissingInductionVariable,
    NonAffineBound,
    UnsupportedCall,
    UnsupportedControlFlow,
    UnsupportedChildLoop,
    StripMinedPointLoop,
};

struct CandidateResult {
    CandidateRejectReason reason = CandidateRejectReason::None;
    std::string detail;

    bool accepted() const { return reason == CandidateRejectReason::None; }

    static CandidateResult accept() { return {}; }
    static CandidateResult reject(CandidateRejectReason reason,
                                  std::string detail = {});
};

const char *candidateRejectReasonName(CandidateRejectReason reason);
CandidateResult analyzeHiraCandidate(const Loop &loop, const LoopInfo &loopInfo);

} // namespace hira
