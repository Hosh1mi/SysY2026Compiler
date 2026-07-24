#include "../../../include/mid/hira/analysis/candidateAnalysis.hpp"

#include "../../../include/mid/analysis/affineAnalysis.hpp"
#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/ir/basicBlock.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace hira {
namespace {

CandidateResult reject(CandidateRejectReason reason,
                       const std::string &detail = {}) {
    return CandidateResult::reject(reason, detail);
}

bool isDefinedInLoop(Value *value, const Loop &loop) {
    auto *instruction = dynamic_cast<Instruction *>(value);
    return instruction && loop.isInLoop(instruction);
}

bool hasSingleEntry(const Loop &loop) {
    for (BasicBlock *block : loop.blocksOrdered) {
        if (block == loop.header)
            continue;
        for (BasicBlock *predecessor : block->pre_bbs_)
            if (!loop.isInLoop(predecessor))
                return false;
    }
    return true;
}

bool hasDedicatedExit(const Loop &loop) {
    BasicBlock *exit = loop.singleExit();
    if (!exit)
        return false;
    if (exit->pre_bbs_.empty())
        return false;
    return std::all_of(exit->pre_bbs_.begin(), exit->pre_bbs_.end(),
                       [&loop](BasicBlock *predecessor) {
                           return loop.isInLoop(predecessor);
                       });
}

bool isSupportedControlFlow(const Loop &loop) {
    BasicBlock *exit = loop.singleExit();
    for (BasicBlock *block : loop.blocksOrdered) {
        Instruction *terminator = block->get_terminator();
        if (!terminator || !terminator->is_br())
            return false;
        if (block->succ_bbs_.empty() || block->succ_bbs_.size() > 2)
            return false;
        for (BasicBlock *successor : block->succ_bbs_)
            if (!loop.isInLoop(successor) && successor != exit)
                return false;
    }
    return true;
}

std::vector<PhiInst *> collectInductionVariables(const Loop &loop) {
    std::vector<PhiInst *> result;
    const Loop *current = &loop;
    std::vector<const Loop *> worklist{current};
    while (!worklist.empty()) {
        current = worklist.back();
        worklist.pop_back();
        if (PhiInst *iv = current->getInductionIV())
            result.push_back(iv);
        for (Loop *child : current->children)
            worklist.push_back(child);
    }
    return result;
}

bool isInvariantForAll(Value *value,
                       const std::vector<PhiInst *> &inductionVariables) {
    for (PhiInst *iv : inductionVariables)
        if (!AffineAnalysis::provablyIndependentOfIV(value, iv))
            return false;
    return true;
}

bool isSupportedAddress(Value *address, const Loop &loop,
                        AffineAnalysis &affine,
                        const std::vector<PhiInst *> &inductionVariables) {
    auto *instruction = dynamic_cast<Instruction *>(address);
    if (!instruction || !loop.isInLoop(instruction))
        return true;

    auto *gep = dynamic_cast<GetElementPtrInst *>(instruction);
    if (!gep)
        return false;
    if (!isSupportedAddress(gep->get_operand(0), loop, affine,
                            inductionVariables))
        return false;

    for (unsigned i = 1; i < gep->num_ops_; ++i) {
        Value *index = gep->get_operand(i);
        if (affine.analyze(index).valid)
            continue;
        if (!isInvariantForAll(index, inductionVariables))
            return false;
    }
    return true;
}

CandidateResult checkInstructions(const Loop &loop, const LoopInfo &loopInfo) {
    AffineAnalysis affine(loopInfo);
    std::vector<PhiInst *> inductionVariables =
        collectInductionVariables(loop);

    for (BasicBlock *block : loop.blocksOrdered) {
        for (Instruction *instruction : block->instr_list_) {
            if (instruction->is_call())
                return reject(CandidateRejectReason::UnsupportedCall);

            Value *address = nullptr;
            if (instruction->is_load())
                address = instruction->get_operand(0);
            else if (instruction->is_store())
                address = instruction->get_operand(1);

            if (address &&
                !isSupportedAddress(address, loop, affine,
                                    inductionVariables))
                return reject(CandidateRejectReason::NonAffineAccess);
        }
    }
    return CandidateResult::accept();
}

} // namespace

CandidateResult CandidateResult::reject(CandidateRejectReason reason,
                                        std::string detail) {
    CandidateResult result;
    result.reason = reason;
    result.detail = std::move(detail);
    return result;
}

const char *candidateRejectReasonName(CandidateRejectReason reason) {
    switch (reason) {
    case CandidateRejectReason::None:
        return "none";
    case CandidateRejectReason::MissingHeader:
        return "missing-header";
    case CandidateRejectReason::MissingPreheader:
        return "missing-preheader";
    case CandidateRejectReason::MultipleLatches:
        return "multiple-latches";
    case CandidateRejectReason::MultipleEntry:
        return "multiple-entry";
    case CandidateRejectReason::MultipleExitingBlocks:
        return "multiple-exiting-blocks";
    case CandidateRejectReason::MultipleExits:
        return "multiple-exits";
    case CandidateRejectReason::NonDedicatedExit:
        return "non-dedicated-exit";
    case CandidateRejectReason::MissingInductionVariable:
        return "missing-induction-variable";
    case CandidateRejectReason::NonAffineBound:
        return "non-affine-bound";
    case CandidateRejectReason::NonAffineAccess:
        return "non-affine-access";
    case CandidateRejectReason::UnsupportedCall:
        return "unsupported-call";
    case CandidateRejectReason::UnsupportedControlFlow:
        return "unsupported-control-flow";
    case CandidateRejectReason::UnsupportedChildLoop:
        return "unsupported-child-loop";
    }
    return "unknown";
}

CandidateResult analyzeHiraCandidate(const Loop &loop,
                                     const LoopInfo &loopInfo) {
    if (!loop.header)
        return reject(CandidateRejectReason::MissingHeader);
    if (!loop.preheader)
        return reject(CandidateRejectReason::MissingPreheader);
    if (loop.latches.size() != 1)
        return reject(CandidateRejectReason::MultipleLatches);
    if (!hasSingleEntry(loop))
        return reject(CandidateRejectReason::MultipleEntry);
    if (loop.exiting.size() != 1)
        return reject(CandidateRejectReason::MultipleExitingBlocks);
    if (loop.exits.size() != 1)
        return reject(CandidateRejectReason::MultipleExits);
    if (!hasDedicatedExit(loop))
        return reject(CandidateRejectReason::NonDedicatedExit);
    if (!loop.hasInductionIV() || !loop.inductionInit || !loop.tripCount)
        return reject(CandidateRejectReason::MissingInductionVariable);
    if (isDefinedInLoop(loop.tripCount, loop))
        return reject(CandidateRejectReason::NonAffineBound);
    if (!isSupportedControlFlow(loop))
        return reject(CandidateRejectReason::UnsupportedControlFlow);

    return checkInstructions(loop, loopInfo);
}

} // namespace hira
