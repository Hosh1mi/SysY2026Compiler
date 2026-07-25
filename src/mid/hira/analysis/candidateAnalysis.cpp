#include "../../../include/mid/hira/analysis/candidateAnalysis.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/ir/basicBlock.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <utility>

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

bool isStripMinedPointLoop(const Loop &loop) {
    if (!loop.parent || !loop.inductionInit)
        return false;
    auto *parentInduction =
        dynamic_cast<PhiInst *>(loop.inductionInit);
    if (!parentInduction)
        return false;
    const Loop *tileLoop = loop.parent;
    while (tileLoop &&
           tileLoop->header !=
               parentInduction->parent_)
        tileLoop = tileLoop->parent;
    if (!tileLoop || tileLoop->getInductionIV())
        return false;
    BasicBlock *latch = tileLoop->singleLatch();
    if (!latch)
        return false;
    for (unsigned index = 0;
         index + 1 < parentInduction->num_ops_;
         index += 2) {
        auto *predecessor = dynamic_cast<BasicBlock *>(
            parentInduction->get_operand(index + 1));
        if (predecessor != latch)
            continue;
        auto *update = dynamic_cast<BinaryInst *>(
            parentInduction->get_operand(index));
        if (!update || update->op_id_ != Instruction::Add)
            return false;
        Value *other = nullptr;
        if (update->get_operand(0) == parentInduction)
            other = update->get_operand(1);
        else if (update->get_operand(1) == parentInduction)
            other = update->get_operand(0);
        auto *step = dynamic_cast<ConstantInt *>(other);
        return step && step->value_ > 1;
    }
    return false;
}

CandidateResult checkInstructions(const Loop &loop) {
    for (BasicBlock *block : loop.blocksOrdered) {
        for (Instruction *instruction : block->instr_list_) {
            if (instruction->is_call())
                return reject(CandidateRejectReason::UnsupportedCall);
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
    case CandidateRejectReason::UnsupportedCall:
        return "unsupported-call";
    case CandidateRejectReason::UnsupportedControlFlow:
        return "unsupported-control-flow";
    case CandidateRejectReason::UnsupportedChildLoop:
        return "unsupported-child-loop";
    case CandidateRejectReason::StripMinedPointLoop:
        return "strip-mined-point-loop";
    case CandidateRejectReason::AlreadyVectorized:
        return "already-vectorized";
    }
    return "unknown";
}

CandidateResult analyzeHiraCandidate(const Loop &loop,
                                     const LoopInfo &loopInfo) {
    (void)loopInfo;
    if (loop.header &&
        (loop.header->hasSemFlag(
             SemFlag::VectorizedEpilogue) ||
         loop.header->hasSemFlag(
             SemFlag::TargetPointerRecurrenceLoop)))
        return reject(
            CandidateRejectReason::AlreadyVectorized);
    if (isStripMinedPointLoop(loop))
        return reject(
            CandidateRejectReason::StripMinedPointLoop);
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

    return checkInstructions(loop);
}

} // namespace hira
