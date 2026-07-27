#include "../../../include/mid/hira/conversion/exporter.hpp"

#include "../../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/basicBlock.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/module.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hira {
namespace {

Instruction::OpID binaryOpcode(ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Add:
        return Instruction::Add;
    case ComputeKind::Sub:
        return Instruction::Sub;
    case ComputeKind::Mul:
        return Instruction::Mul;
    case ComputeKind::SDiv:
        return Instruction::SDiv;
    case ComputeKind::SRem:
        return Instruction::SRem;
    case ComputeKind::UDiv:
        return Instruction::UDiv;
    case ComputeKind::URem:
        return Instruction::URem;
    case ComputeKind::FAdd:
        return Instruction::FAdd;
    case ComputeKind::FSub:
        return Instruction::FSub;
    case ComputeKind::FMul:
        return Instruction::FMul;
    case ComputeKind::FDiv:
        return Instruction::FDiv;
    case ComputeKind::And:
        return Instruction::And;
    case ComputeKind::Or:
        return Instruction::Or;
    case ComputeKind::Xor:
        return Instruction::Xor;
    case ComputeKind::Shl:
        return Instruction::Shl;
    case ComputeKind::LShr:
        return Instruction::LShr;
    case ComputeKind::AShr:
        return Instruction::AShr;
    default:
        return Instruction::Ret;
    }
}

bool isBinaryCompute(ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Add:
    case ComputeKind::Sub:
    case ComputeKind::Mul:
    case ComputeKind::SDiv:
    case ComputeKind::SRem:
    case ComputeKind::UDiv:
    case ComputeKind::URem:
    case ComputeKind::FAdd:
    case ComputeKind::FSub:
    case ComputeKind::FMul:
    case ComputeKind::FDiv:
    case ComputeKind::And:
    case ComputeKind::Or:
    case ComputeKind::Xor:
    case ComputeKind::Shl:
    case ComputeKind::LShr:
    case ComputeKind::AShr:
        return true;
    default:
        return false;
    }
}

std::vector<BasicBlock *> collectEntryPredecessors(
    const Loop &loop) {
    std::vector<BasicBlock *> entries;
    for (BasicBlock *predecessor : loop.header->pre_bbs_) {
        if (loop.isInLoop(predecessor))
            continue;
        entries.push_back(predecessor);
    }
    return entries;
}

class RegionExporter {
public:
    explicit RegionExporter(HiraRegion &region)
        : region_(region), sourceLoop_(region.sourceLoop()) {
        if (sourceLoop_ && sourceLoop_->header) {
            function_ = sourceLoop_->header->parent_;
            module_ = function_ ? function_->parent_ : nullptr;
        }
    }

    ExportResult run() {
        if (!validate())
            return failure_;
        if (region_.parallelPlan())
            return runParallel();
        if (!materializeScratches(function_->basic_blocks_.front())) {
            rollbackNewBlocks();
            return failure_;
        }

        BasicBlock *loopEntry = entryPredecessors_.front();
        const bool needsEntryMerge =
            rootLoopIndex_ != 0 || entryPredecessors_.size() != 1;
        if (needsEntryMerge) {
            const unsigned id = nextBlockId_++;
            entryTarget_ = createBlock("preheader", id);
            loopEntry = entryTarget_;
            for (std::size_t index = 0; index < rootLoopIndex_; ++index)
                if (!emitStructuredNode(
                        *region_.rootSequence().nodes()[index],
                        loopEntry)) {
                    rollbackNewBlocks();
                    return failure_;
                }
        }

        LoopEmission rootEmission;
        if (!emitLoop(*rootLoop_, loopEntry, needsEntryMerge,
                      rootEmission)) {
            rollbackNewBlocks();
            return failure_;
        }
        rootHeader_ = rootEmission.header;
        if (!entryTarget_)
            entryTarget_ = rootHeader_;

        BasicBlock *regionExit = rootEmission.exit;
        const auto &rootNodes = region_.rootSequence().nodes();
        for (std::size_t index = rootLoopIndex_ + 1;
             index < rootNodes.size(); ++index)
            if (!emitStructuredNode(*rootNodes[index], regionExit)) {
                rollbackNewBlocks();
                return failure_;
            }
        newExitPredecessor_ = regionExit;
        new BranchInst(oldExit_, regionExit);
        commit();
        return ExportResult::success();
    }

private:
    struct LoopEmission {
        BasicBlock *header = nullptr;
        BasicBlock *exit = nullptr;
    };

    struct ExitPhiRewrite {
        PhiInst *phi = nullptr;
        Value *sourceValue = nullptr;
    };

    struct CarriedPhi {
        const HiraLoop::CarriedBinding *binding = nullptr;
        PhiInst *phi = nullptr;
    };

    // Overrides applied when the root loop is lowered into a worker body
    // function: the band iterates the [lo, hi) chunk handed to the body
    // and exact-reduction accumulators start from their identity.
    struct ParallelRootOverrides {
        Value *lower = nullptr;
        Value *upper = nullptr;
        const std::map<std::size_t, std::int64_t> *identityInits =
            nullptr;
    };

    bool fail(ExportRejectReason reason, std::string detail = {}) {
        failure_ = ExportResult::reject(reason, std::move(detail));
        return false;
    }

    bool validateNode(const HiraNode &node) {
        if (auto *loop = dynamic_cast<const HiraLoop *>(&node))
            return validateLoop(*loop);
        if (auto *condition = dynamic_cast<const HiraIf *>(&node)) {
            if (condition->operands().size() !=
                    condition->resultBindings().size() * 2 + 1 ||
                condition->results().size() !=
                    condition->resultBindings().size() ||
                condition->condition()->type() != module_->int1_ty_)
                return fail(ExportRejectReason::InvalidRegion,
                            "invalid-if-interface");
            for (const auto &branchNode :
                 condition->thenSequence().nodes())
                if (!validateNode(*branchNode))
                    return false;
            for (const auto &branchNode :
                 condition->elseSequence().nodes())
                if (!validateNode(*branchNode))
                    return false;
            return true;
        }
        if (dynamic_cast<const HiraYield *>(&node))
            return fail(ExportRejectReason::InvalidRegion,
                        "misplaced-yield");
        if (dynamic_cast<const HiraLoad *>(&node))
            return node.results().size() == 1 ||
                   fail(ExportRejectReason::InvalidRegion,
                        "invalid-load-result");
        if (dynamic_cast<const HiraStore *>(&node))
            return node.results().empty() ||
                   fail(ExportRejectReason::InvalidRegion,
                        "invalid-store-result");

        auto *compute = dynamic_cast<const HiraComputeOp *>(&node);
        if (!compute)
            return fail(ExportRejectReason::UnsupportedNode);
        if (compute->results().size() != 1)
            return fail(ExportRejectReason::InvalidRegion,
                        "invalid-compute-result");
        ComputeKind kind = compute->computeKind();
        if (!isBinaryCompute(kind) && kind != ComputeKind::ICmp &&
            kind != ComputeKind::Select &&
            kind != ComputeKind::GetElementPtr &&
            kind != ComputeKind::ZExt &&
            kind != ComputeKind::BitCast &&
            kind != ComputeKind::Splat &&
            kind != ComputeKind::InsertElement &&
            kind != ComputeKind::ExtractElement)
            return fail(ExportRejectReason::UnsupportedNode);
        return true;
    }

    bool validateLoop(const HiraLoop &loop) {
        const auto &nodes = loop.body().nodes();
        if (loop.yieldValues().size() !=
                loop.carriedValues().size() + 1 ||
            nodes.empty())
            return fail(ExportRejectReason::InvalidRegion,
                        "invalid-loop-yields");
        auto *yield =
            dynamic_cast<const HiraYield *>(nodes.back().get());
        if (!yield ||
            yield->operands().size() != loop.yieldValues().size())
            return fail(ExportRejectReason::InvalidRegion,
                        "invalid-loop-yield-node");
        for (std::size_t index = 0;
             index < loop.yieldValues().size(); ++index)
            if (yield->operands()[index] != loop.yieldValues()[index])
                return fail(ExportRejectReason::InvalidRegion,
                            "mismatched-loop-yield");
        for (const auto &binding : loop.carriedValues())
            if (!binding.initial || !binding.iteration ||
                !binding.yielded || !binding.result)
                return fail(ExportRejectReason::InvalidRegion,
                            "invalid-carried-binding");
        for (std::size_t index = 0; index + 1 < nodes.size(); ++index)
            if (!validateNode(*nodes[index]))
                return false;
        return true;
    }

    bool validate() {
        if (!sourceLoop_ || !function_ || !module_ ||
            !sourceLoop_->singleExit())
            return fail(ExportRejectReason::InvalidRegion);

        oldHeader_ = sourceLoop_->header;
        entryPredecessors_ =
            collectEntryPredecessors(*sourceLoop_);
        oldExit_ = sourceLoop_->singleExit();
        if (entryPredecessors_.empty())
            return fail(ExportRejectReason::InvalidSourceCFG);
        for (BasicBlock *entry : entryPredecessors_) {
            Instruction *terminator = entry->get_terminator();
            if (!terminator || !terminator->is_br() ||
                (terminator->num_ops_ != 1 &&
                 terminator->num_ops_ != 3))
                return fail(ExportRejectReason::InvalidSourceCFG);
            bool targetsHeader = false;
            for (unsigned index = 0;
                 index < terminator->num_ops_; ++index)
                targetsHeader |=
                    terminator->get_operand(index) == oldHeader_;
            if (!targetsHeader)
                return fail(ExportRejectReason::InvalidSourceCFG);
        }

        for (Instruction *instruction : oldExit_->instr_list_) {
            auto *phi = dynamic_cast<PhiInst *>(instruction);
            if (!phi)
                break;
            Value *sourceValue = nullptr;
            bool foundRegionIncoming = false;
            for (unsigned index = 0;
                 index + 1 < phi->num_ops_; index += 2) {
                auto *predecessor = dynamic_cast<BasicBlock *>(
                    phi->get_operand(index + 1));
                if (!predecessor ||
                    !sourceLoop_->isInLoop(predecessor))
                    continue;
                Value *incoming = phi->get_operand(index);
                if (!foundRegionIncoming) {
                    sourceValue = incoming;
                    foundRegionIncoming = true;
                    continue;
                }
                if (incoming == sourceValue)
                    continue;
                auto *left =
                    dynamic_cast<ConstantInt *>(sourceValue);
                auto *right =
                    dynamic_cast<ConstantInt *>(incoming);
                if (!left || !right ||
                    left->type_ != right->type_ ||
                    left->value_ != right->value_)
                    return fail(
                        ExportRejectReason::UnsupportedExitPhi,
                        oldExit_->name_ +
                            ":path-dependent-incoming");
            }
            if (!foundRegionIncoming)
                return fail(ExportRejectReason::InvalidSourceCFG,
                            oldExit_->name_ +
                                ":missing-region-incoming");
            auto *sourceInstruction =
                dynamic_cast<Instruction *>(sourceValue);
            if (sourceInstruction &&
                sourceLoop_->isInLoop(sourceInstruction)) {
                bool exportedResult = false;
                for (HiraValue *result : region_.results())
                    exportedResult |=
                        region_.sourceMapping().sourceValue(result) ==
                        sourceValue;
                if (!exportedResult)
                    return fail(
                        ExportRejectReason::UnsupportedExitPhi,
                        oldExit_->name_ +
                            ":unmodeled-live-out");
            }
            exitPhiRewrites_.push_back({phi, sourceValue});
        }

        const auto &rootNodes = region_.rootSequence().nodes();
        if (rootNodes.empty())
            return fail(ExportRejectReason::InvalidRegion);
        for (std::size_t index = 0; index < rootNodes.size(); ++index) {
            if (auto *loop =
                    dynamic_cast<HiraLoop *>(rootNodes[index].get())) {
                if (!rootLoop_) {
                    rootLoop_ = loop;
                    rootLoopIndex_ = index;
                }
                if (!validateLoop(*loop))
                    return false;
            } else if (!validateNode(*rootNodes[index])) {
                return false;
            }
        }
        if (!rootLoop_)
            return fail(ExportRejectReason::InvalidRegion,
                        "missing-root-loop");
        Loop *mappedRoot =
            region_.sourceMapping().sourceLoop(rootLoop_);
        if (!mappedRoot || !mappedRoot->header ||
            !sourceLoop_->isInLoop(mappedRoot->header))
            return fail(ExportRejectReason::InvalidRegion,
                        "invalid-root-loop");

        for (HiraValue *parameter : region_.parameters())
            if (!region_.sourceMapping().sourceValue(parameter))
                return fail(ExportRejectReason::InvalidRegion,
                            "unmapped-parameter");

        for (HiraValue *result : region_.results()) {
            if (!region_.sourceMapping().sourceValue(result))
                return fail(ExportRejectReason::InvalidRegion,
                            "unmapped-result");
        }
        for (HiraValue *scratch : region_.scratches()) {
            auto *pointer =
                scratch
                    ? dynamic_cast<PointerType *>(scratch->type())
                    : nullptr;
            if (!pointer ||
                scratch->kind() != ValueKind::Scratch ||
                !scratch->allocatedType() ||
                pointer->contained_ != scratch->allocatedType() ||
                scratch->definingNode())
                return fail(ExportRejectReason::InvalidRegion,
                            "invalid-scratch");
        }
        return true;
    }

    void collectSubtreeNodes(const HiraSequence &sequence,
                             std::set<const HiraNode *> &inside) {
        for (const auto &node : sequence.nodes()) {
            inside.insert(node.get());
            if (auto *loop =
                    dynamic_cast<const HiraLoop *>(node.get()))
                collectSubtreeNodes(loop->body(), inside);
            else if (auto *condition =
                         dynamic_cast<const HiraIf *>(node.get())) {
                collectSubtreeNodes(condition->thenSequence(),
                                    inside);
                collectSubtreeNodes(condition->elseSequence(),
                                    inside);
            }
        }
    }

    // Structural infallibility check for worker lowering: every value
    // used inside the band must be a region boundary value (passed
    // through the parallel context) or be produced within the band, so
    // the body function never references the source frame.
    bool validateParallel() {
        const HiraParallelPlan &plan = *region_.parallelPlan();
        if (plan.loop != rootLoop_)
            return fail(ExportRejectReason::InvalidRegion,
                        "parallel-root-mismatch");
        std::set<const HiraValue *> privateParameters;
        for (const HiraValue *parameter :
             plan.privateParameters) {
            if (!parameter ||
                !privateParameters.insert(parameter).second)
                return fail(ExportRejectReason::InvalidRegion,
                            "invalid-private-parameter");
            if (parameter->kind() == ValueKind::Scratch) {
                if (std::find(region_.scratches().begin(),
                              region_.scratches().end(),
                              parameter) ==
                    region_.scratches().end())
                    return fail(ExportRejectReason::InvalidRegion,
                                "invalid-private-scratch");
                continue;
            }
            if (std::find(region_.parameters().begin(),
                          region_.parameters().end(),
                          parameter) ==
                region_.parameters().end())
                return fail(ExportRejectReason::InvalidRegion,
                            "invalid-private-parameter");
            ::Value *source =
                region_.sourceMapping().sourceValue(parameter);
            source =
                ArgumentAliasAnalysis::underlyingObject(source);
            auto *alloca = dynamic_cast<AllocaInst *>(source);
            if (!alloca ||
                !alloca->isLoopExpansionScratch())
                return fail(ExportRejectReason::InvalidRegion,
                            "invalid-private-allocation");
        }
        std::set<const HiraNode *> inside;
        inside.insert(rootLoop_);
        collectSubtreeNodes(rootLoop_->body(), inside);
        std::set<const HiraNode *> allNodes;
        collectSubtreeNodes(region_.rootSequence(), allNodes);
        for (const HiraNode *node : allNodes)
            if (!inside.count(node))
                for (const HiraValue *operand : node->operands())
                    if (operand &&
                        operand->kind() == ValueKind::Scratch)
                        return fail(
                            ExportRejectReason::InvalidRegion,
                            "parallel-scratch-outside-band");
        std::set<const HiraNode *> availablePrefix;
        const auto &rootNodes = region_.rootSequence().nodes();
        for (std::size_t index = 0; index < rootLoopIndex_; ++index)
            availablePrefix.insert(rootNodes[index].get());
        std::set<const HiraValue *> captured;
        for (const HiraNode *node : inside) {
            for (const HiraValue *operand : node->operands()) {
                if (!operand)
                    return fail(ExportRejectReason::InvalidRegion,
                                "parallel-null-operand");
                if (operand->kind() != ValueKind::Temporary)
                    continue;
                if (inside.count(operand->definingNode()))
                    continue;
                if (!availablePrefix.count(
                        operand->definingNode()))
                    return fail(ExportRejectReason::InvalidRegion,
                                "parallel-operand-escapes-band");
                if (captured.insert(operand).second)
                    parallelCaptures_.push_back(
                        const_cast<HiraValue *>(operand));
            }
        }
        std::set<std::size_t> reductionIndices;
        for (const HiraParallelReduction &reduction :
             plan.reductions) {
            if (reduction.carriedIndex >=
                rootLoop_->carriedValues().size())
                return fail(ExportRejectReason::InvalidRegion,
                            "parallel-reduction-index");
            reductionIndices.insert(reduction.carriedIndex);
        }
        for (const HiraValue *result : region_.results()) {
            bool covered = false;
            for (std::size_t index : reductionIndices)
                covered |= rootLoop_->carriedValues()[index].result ==
                           result;
            if (!covered)
                return fail(ExportRejectReason::UnsupportedResult,
                            "parallel-result-not-reduction");
        }
        return true;
    }

    Instruction::OpID parallelReductionOpcode(
        ParallelReductionOp operation) {
        switch (operation) {
        case ParallelReductionOp::BitAnd:
            return Instruction::And;
        case ParallelReductionOp::BitOr:
            return Instruction::Or;
        case ParallelReductionOp::BitXor:
            return Instruction::Xor;
        }
        return Instruction::Xor;
    }

    Function *parallelForDeclaration() {
        for (Function *function : module_->function_list_)
            if (function->name_ == "__sysy_parallel_for")
                return function;
        auto *type = new FunctionType(
            module_->void_ty_,
            {module_->int32_ty_, module_->int32_ty_,
             module_->int32_ty_});
        return new Function(type, "__sysy_parallel_for", module_);
    }

    // Lowers the outer band into a worker body function.  The source
    // function publishes the region parameters through context slots,
    // calls the dual-core runtime with the full band bounds and folds
    // each worker's exact-reduction partial in band order after the
    // join.  Every step after the pre-root emission is infallible: the
    // transform gate and validateParallel guarantee all values resolve.
    ExportResult runParallel() {
        if (!validateParallel())
            return failure_;
        const HiraParallelPlan &plan = *region_.parallelPlan();

        BasicBlock *loopEntry = entryPredecessors_.front();
        if (rootLoopIndex_ != 0) {
            const unsigned id = nextBlockId_++;
            entryTarget_ = createBlock("preheader", id);
            loopEntry = entryTarget_;
            for (std::size_t index = 0; index < rootLoopIndex_;
                 ++index)
                if (!emitStructuredNode(
                        *region_.rootSequence().nodes()[index],
                        loopEntry)) {
                    rollbackNewBlocks();
                    return failure_;
                }
        }

        Value *lowerSource = sourceValueOf(rootLoop_->lowerBound());
        Value *upperSource = sourceValueOf(rootLoop_->upperBound());
        std::map<HiraValue *, Value *> captureSources;
        for (HiraValue *capture : parallelCaptures_)
            captureSources[capture] = sourceValueOf(capture);
        std::vector<Value *> reductionInitials;
        for (const HiraParallelReduction &reduction :
             plan.reductions)
            reductionInitials.push_back(sourceValueOf(
                rootLoop_->carriedValues()[reduction.carriedIndex]
                    .initial));

        // Worker body: lo/hi arrive as arguments, region parameters
        // arrive through context slots.
        const std::string suffix = std::to_string(plan.bodyId);
        auto *bodyType = new FunctionType(
            module_->void_ty_,
            {module_->int32_ty_, module_->int32_ty_});
        Function *bodyFunction = new Function(
            bodyType, "__sysy_par_body_" + suffix, module_);
        bodyFunction->markHiraParallelWorker();
        Value *loArgument = bodyFunction->arguments_[0];
        Value *hiArgument = bodyFunction->arguments_[1];
        auto *bodyEntry = new BasicBlock(module_, "label_par_entry",
                                         bodyFunction);
        if (!materializeScratches(bodyEntry))
            return failure_;

        std::map<HiraValue *, GlobalVariable *> contextSlots;
        std::size_t slotIndex = 0;
        for (HiraValue *parameter : region_.parameters()) {
            const bool taskPrivate =
                std::find(
                    plan.privateParameters.begin(),
                    plan.privateParameters.end(),
                    parameter) !=
                plan.privateParameters.end();
            if (taskPrivate) {
                ::Value *source =
                    region_.sourceMapping().sourceValue(parameter);
                source =
                    ArgumentAliasAnalysis::underlyingObject(source);
                auto *sourceAlloca =
                    dynamic_cast<AllocaInst *>(source);
                if (!sourceAlloca) {
                    fail(ExportRejectReason::InvalidRegion,
                         "private-parameter-not-alloca");
                    return failure_;
                }
                auto *privateAlloca = new AllocaInst(
                    sourceAlloca->alloca_ty_, bodyEntry, true);
                privateAlloca->markLoopExpansionScratch();
                bodyEntry->add_instruction_front(privateAlloca);
                bind(parameter, privateAlloca);
                ++slotIndex;
                continue;
            }
            auto *slot = new GlobalVariable(
                "__sysy_par_ctx_" + suffix + "_" +
                    std::to_string(slotIndex++),
                module_, parameter->type(), false,
                new ConstantZero(parameter->type()));
            contextSlots[parameter] = slot;
            auto *loaded = new LoadInst(slot, bodyEntry);
            bind(parameter, loaded);
        }
        for (HiraValue *capture : parallelCaptures_) {
            auto *slot = new GlobalVariable(
                "__sysy_par_ctx_" + suffix + "_" +
                    std::to_string(slotIndex++),
                module_, capture->type(), false,
                new ConstantZero(capture->type()));
            contextSlots[capture] = slot;
            auto *loaded = new LoadInst(slot, bodyEntry);
            bind(capture, loaded);
        }

        std::map<std::size_t, std::int64_t> identityInits;
        std::vector<GlobalVariable *> partialSlots;
        for (const HiraParallelReduction &reduction :
             plan.reductions) {
            identityInits.emplace(reduction.carriedIndex,
                                  reduction.identity);
            Type *valueType =
                rootLoop_->carriedValues()[reduction.carriedIndex]
                    .iteration->type();
            const std::string base =
                "__sysy_par_red_" + suffix + "_" +
                std::to_string(reduction.carriedIndex);
            partialSlots.push_back(new GlobalVariable(
                base + "_0", module_, valueType, false,
                new ConstantZero(valueType)));
            partialSlots.push_back(new GlobalVariable(
                base + "_1", module_, valueType, false,
                new ConstantZero(valueType)));
        }

        ParallelRootOverrides overrides;
        overrides.lower = loArgument;
        overrides.upper = hiArgument;
        overrides.identityInits = &identityInits;
        blockFunction_ = bodyFunction;
        LoopEmission emission;
        if (!emitLoop(*rootLoop_, bodyEntry, true, emission,
                      &overrides))
            return failure_;
        blockFunction_ = nullptr;

        // The runtime hands the first chunk to the calling thread and
        // the second chunk to the worker, so the body tells them apart
        // by comparing its lo argument against the band's lower bound.
        Value *bodyLower = value(rootLoop_->lowerBound());
        auto *isFirstChunk = new ICmpInst(ICmpInst::ICMP_EQ, loArgument,
                                          bodyLower, emission.exit);
        for (std::size_t index = 0; index < plan.reductions.size();
             ++index) {
            const HiraParallelReduction &reduction =
                plan.reductions[index];
            Value *partial = value(
                rootLoop_->carriedValues()[reduction.carriedIndex]
                    .result);
            auto *destination =
                new SelectInst(isFirstChunk, partialSlots[index * 2],
                               partialSlots[index * 2 + 1],
                               emission.exit);
            new StoreInst(partial, destination, emission.exit);
        }
        new ReturnInst(emission.exit);

        // Source side: publish the context, dispatch both workers, then
        // combine the partials.  Combining in chunk order keeps the
        // folded value identical to the sequential accumulation for
        // associative operators.
        const unsigned callId = nextBlockId_++;
        BasicBlock *parallelCall = createBlock("par_call", callId);
        for (HiraValue *parameter : region_.parameters()) {
            auto slot = contextSlots.find(parameter);
            if (slot == contextSlots.end())
                continue;
            new StoreInst(sourceValueOf(parameter),
                          slot->second, parallelCall);
        }
        for (HiraValue *capture : parallelCaptures_)
            new StoreInst(captureSources.at(capture),
                          contextSlots.at(capture),
                          parallelCall);
        for (std::size_t index = 0; index < plan.reductions.size();
             ++index) {
            Type *valueType =
                rootLoop_
                    ->carriedValues()[plan.reductions[index]
                                          .carriedIndex]
                    .iteration->type();
            auto *identity = new ConstantInt(
                valueType,
                static_cast<int>(plan.reductions[index].identity));
            new StoreInst(identity, partialSlots[index * 2],
                          parallelCall);
            new StoreInst(identity, partialSlots[index * 2 + 1],
                          parallelCall);
        }
        new CallInst(parallelForDeclaration(),
                     {new ConstantInt(module_->int32_ty_, plan.bodyId),
                      lowerSource, upperSource},
                     parallelCall);
        for (std::size_t index = 0; index < plan.reductions.size();
             ++index) {
            const HiraParallelReduction &reduction =
                plan.reductions[index];
            const HiraLoop::CarriedBinding &binding =
                rootLoop_->carriedValues()[reduction.carriedIndex];
            auto *first =
                new LoadInst(partialSlots[index * 2], parallelCall);
            auto *second = new LoadInst(partialSlots[index * 2 + 1],
                                        parallelCall);
            Instruction::OpID opcode =
                parallelReductionOpcode(reduction.op);
            auto *foldFirst =
                new BinaryInst(binding.iteration->type(), opcode,
                               reductionInitials[index], first,
                               parallelCall);
            auto *folded =
                new BinaryInst(binding.iteration->type(), opcode,
                               foldFirst, second, parallelCall);
            bind(binding.result, folded);
        }

        BasicBlock *regionExit = createBlock("par_join", callId);
        new BranchInst(regionExit, parallelCall);
        if (!entryTarget_)
            entryTarget_ = parallelCall;
        else
            new BranchInst(parallelCall, loopEntry);

        // The body function no longer needs the parameter rebinding;
        // post-band nodes resolve parameters to their source values.
        for (HiraValue *parameter : region_.parameters())
            bind(parameter,
                 region_.sourceMapping().sourceValue(parameter));
        for (HiraValue *capture : parallelCaptures_)
            bind(capture, captureSources.at(capture));

        const auto &rootNodes = region_.rootSequence().nodes();
        for (std::size_t index = rootLoopIndex_ + 1;
             index < rootNodes.size(); ++index)
            if (!emitStructuredNode(*rootNodes[index], regionExit)) {
                rollbackNewBlocks();
                return failure_;
            }
        newExitPredecessor_ = regionExit;
        new BranchInst(oldExit_, regionExit);
        commit();
        return ExportResult::success();
    }

    BasicBlock *createBlock(const std::string &role, unsigned id) {
        auto *block = new BasicBlock(
            module_, "label_hira_" + role + "_" + std::to_string(id),
            blockFunction_ ? blockFunction_ : function_);
        newBlocks_.push_back(block);
        return block;
    }

    bool materializeScratches(BasicBlock *entry) {
        if (!entry)
            return fail(ExportRejectReason::InvalidSourceCFG,
                        "missing-scratch-entry");
        for (HiraValue *scratch : region_.scratches()) {
            if (!scratch || !scratch->allocatedType())
                return fail(ExportRejectReason::InvalidRegion,
                            "invalid-scratch");
            auto *alloca = new AllocaInst(
                scratch->allocatedType(), entry, true);
            alloca->markLoopExpansionScratch();
            alloca->name_ =
                "hira.scratch." + std::to_string(scratch->id());
            if (!entry->add_instruction_front(alloca))
                return fail(ExportRejectReason::InvalidSourceCFG,
                            "failed-to-insert-scratch");
            scratchAllocas_.push_back(alloca);
            bind(scratch, alloca);
        }
        return true;
    }

    // Resolves a region boundary value for use inside the source
    // function, independent of any worker-body rebinding.
    Value *sourceValueOf(HiraValue *hiraValue) {
        if (!hiraValue)
            return nullptr;
        switch (hiraValue->kind()) {
        case ValueKind::Parameter:
            return region_.sourceMapping().sourceValue(hiraValue);
        case ValueKind::Scratch:
            return value(hiraValue);
        case ValueKind::IntegerConstant:
            return new ConstantInt(
                hiraValue->type(),
                static_cast<int>(hiraValue->integerValue()));
        case ValueKind::FloatConstant:
            return new ConstantFloat(hiraValue->type(),
                                     hiraValue->floatValue());
        case ValueKind::Temporary:
            return value(hiraValue);
        }
        return nullptr;
    }

    Value *value(HiraValue *hiraValue) {
        if (!hiraValue)
            return nullptr;
        auto it = values_.find(hiraValue);
        if (it != values_.end())
            return it->second;

        Value *result = nullptr;
        switch (hiraValue->kind()) {
        case ValueKind::Parameter:
            result = region_.sourceMapping().sourceValue(hiraValue);
            break;
        case ValueKind::Scratch:
            break;
        case ValueKind::IntegerConstant:
            result = new ConstantInt(
                hiraValue->type(),
                static_cast<int>(hiraValue->integerValue()));
            break;
        case ValueKind::FloatConstant:
            result =
                new ConstantFloat(hiraValue->type(),
                                  hiraValue->floatValue());
            break;
        case ValueKind::Temporary:
            break;
        }
        if (result)
            values_[hiraValue] = result;
        return result;
    }

    bool bind(HiraValue *hiraValue, Value *llvmValue) {
        if (!hiraValue || !llvmValue)
            return false;
        values_[hiraValue] = llvmValue;
        return true;
    }

    bool emitLoop(HiraLoop &loop, BasicBlock *entry,
                  bool connectEntry, LoopEmission &emission,
                  const ParallelRootOverrides *parallel = nullptr) {
        const unsigned id = nextBlockId_++;
        BasicBlock *header = createBlock("header", id);
        BasicBlock *body = createBlock("body", id);
        BasicBlock *exit = createBlock("exit", id);
        emission = {header, exit};
        if (loop.role() == HiraLoop::Role::VectorMain)
            header->setSemFlag(
                SemFlag::TargetPointerRecurrenceLoop);
        else if (loop.role() ==
                 HiraLoop::Role::ScalarRemainder)
            header->setSemFlag(
                SemFlag::VectorizedEpilogue);
        else if (loop.role() ==
                 HiraLoop::Role::RepetitionFolded)
            header->setSemFlag(
                SemFlag::HiraRepetitionFolded);

        Value *lower = parallel ? parallel->lower
                                : value(loop.lowerBound());
        Value *upper = parallel ? parallel->upper
                                : value(loop.upperBound());
        if (!lower || !upper)
            return fail(ExportRejectReason::MissingValue,
                        "loop-bound");

        auto *inductionPhi =
            PhiInst::create_phi(loop.induction()->type(), header);
        header->add_instruction(inductionPhi);
        inductionPhi->addIncoming(lower, entry);
        bind(loop.induction(), inductionPhi);

        std::vector<CarriedPhi> carriedPhis;
        std::size_t bindingIndex = 0;
        for (const auto &binding : loop.carriedValues()) {
            Value *initial = nullptr;
            if (parallel && parallel->identityInits) {
                auto identity =
                    parallel->identityInits->find(bindingIndex);
                if (identity != parallel->identityInits->end())
                    initial = new ConstantInt(
                        binding.iteration->type(),
                        static_cast<int>(identity->second));
            }
            if (!initial)
                initial = value(binding.initial);
            if (!initial)
                return fail(ExportRejectReason::MissingValue,
                            "carried-initial");
            auto *phi =
                PhiInst::create_phi(binding.iteration->type(), header);
            header->add_instruction(phi);
            phi->addIncoming(initial, entry);
            bind(binding.iteration, phi);
            carriedPhis.push_back({&binding, phi});
            ++bindingIndex;
        }

        auto *comparison = new ICmpInst(ICmpInst::ICMP_SLT,
                                        inductionPhi, upper, header);
        new BranchInst(comparison, body, exit, header);
        if (connectEntry)
            new BranchInst(header, entry);

        BasicBlock *continuation = nullptr;
        if (!emitSequence(loop.body(), body, continuation))
            return false;

        // The unique continuation is already a valid loop latch.  Avoid an
        // empty forwarding block and preserve Hira's compact structured CFG
        // in the scalar IR handed to the backend.
        new BranchInst(header, continuation);

        Value *inductionYield = value(loop.yieldValues().front());
        if (!inductionYield)
            return fail(ExportRejectReason::MissingValue,
                        "induction-yield");
        inductionPhi->addIncoming(inductionYield, continuation);

        for (const CarriedPhi &entryPhi : carriedPhis) {
            Value *yielded = value(entryPhi.binding->yielded);
            if (!yielded)
                return fail(ExportRejectReason::MissingValue,
                            "carried-yield");
            entryPhi.phi->addIncoming(yielded, continuation);

            auto *exitPhi = PhiInst::create_phi(
                entryPhi.binding->result->type(), exit);
            exit->add_instruction(exitPhi);
            exitPhi->addIncoming(entryPhi.phi, header);
            bind(entryPhi.binding->result, exitPhi);
        }
        return true;
    }

    bool emitSequence(HiraSequence &sequence, BasicBlock *entry,
                      BasicBlock *&continuation) {
        continuation = entry;
        for (const auto &node : sequence.nodes()) {
            if (dynamic_cast<HiraYield *>(node.get()))
                break;
            if (!emitStructuredNode(*node, continuation))
                return false;
        }
        return true;
    }

    bool emitStructuredNode(HiraNode &node,
                            BasicBlock *&continuation) {
        if (auto *nested = dynamic_cast<HiraLoop *>(&node)) {
            LoopEmission nestedEmission;
            if (!emitLoop(*nested, continuation, true,
                          nestedEmission))
                return false;
            continuation = nestedEmission.exit;
            return true;
        }
        if (auto *condition = dynamic_cast<HiraIf *>(&node))
            return emitIf(*condition, continuation);
        return emitNode(node, continuation);
    }

    bool emitIf(HiraIf &condition, BasicBlock *&continuation) {
        Value *guard = value(condition.condition());
        if (!guard)
            return fail(ExportRejectReason::MissingValue,
                        "if-condition");

        const unsigned id = nextBlockId_++;
        BasicBlock *thenBlock = createBlock("if_then", id);
        BasicBlock *elseBlock = createBlock("if_else", id);
        BasicBlock *joinBlock = createBlock("if_end", id);
        new BranchInst(guard, thenBlock, elseBlock, continuation);

        BasicBlock *thenContinuation = nullptr;
        if (!emitSequence(condition.thenSequence(), thenBlock,
                          thenContinuation))
            return false;
        new BranchInst(joinBlock, thenContinuation);

        BasicBlock *elseContinuation = nullptr;
        if (!emitSequence(condition.elseSequence(), elseBlock,
                          elseContinuation))
            return false;
        new BranchInst(joinBlock, elseContinuation);

        for (const HiraIf::ResultBinding &binding :
             condition.resultBindings()) {
            Value *thenValue = value(binding.thenValue);
            Value *elseValue = value(binding.elseValue);
            if (!thenValue || !elseValue)
                return fail(ExportRejectReason::MissingValue,
                            "if-result");
            auto *phi =
                PhiInst::create_phi(binding.result->type(), joinBlock);
            joinBlock->add_instruction(phi);
            phi->addIncoming(thenValue, thenContinuation);
            phi->addIncoming(elseValue, elseContinuation);
            bind(binding.result, phi);
        }

        continuation = joinBlock;
        return true;
    }

    bool emitNode(HiraNode &node, BasicBlock *destination) {
        Instruction *instruction = nullptr;

        if (auto *load = dynamic_cast<HiraLoad *>(&node)) {
            Value *address = value(load->address());
            if (!address)
                return fail(ExportRejectReason::MissingValue, "load");
            instruction = new LoadInst(address, destination);
        } else if (auto *store = dynamic_cast<HiraStore *>(&node)) {
            Value *stored = value(store->value());
            Value *address = value(store->address());
            if (!stored || !address)
                return fail(ExportRejectReason::MissingValue, "store");
            instruction = new StoreInst(stored, address, destination);
        } else if (auto *compute = dynamic_cast<HiraComputeOp *>(&node)) {
            std::vector<Value *> operands;
            for (HiraValue *operand : compute->operands()) {
                Value *mapped = value(operand);
                if (!mapped)
                    return fail(ExportRejectReason::MissingValue,
                                "compute-operand");
                operands.push_back(mapped);
            }

            ComputeKind kind = compute->computeKind();
            if (isBinaryCompute(kind) && operands.size() == 2) {
                instruction = new BinaryInst(
                    node.results().front()->type(), binaryOpcode(kind),
                    operands[0], operands[1], destination);
            } else if (kind == ComputeKind::ICmp &&
                       operands.size() == 2) {
                instruction = new ICmpInst(
                    static_cast<ICmpInst::ICmpOp>(
                        compute->predicate()),
                    operands[0], operands[1], destination);
            } else if (kind == ComputeKind::Select &&
                       operands.size() == 3) {
                instruction = new SelectInst(
                    operands[0], operands[1], operands[2],
                    destination);
            } else if (kind == ComputeKind::GetElementPtr &&
                       operands.size() >= 2) {
                std::vector<Value *> indices(operands.begin() + 1,
                                             operands.end());
                instruction = new GetElementPtrInst(
                    operands[0], indices, destination);
            } else if (kind == ComputeKind::ZExt &&
                       operands.size() == 1) {
                instruction = new ZextInst(
                    Instruction::ZExt, operands[0],
                    node.results().front()->type(), destination);
            } else if (kind == ComputeKind::BitCast &&
                       operands.size() == 1) {
                instruction = new Bitcast(
                    Instruction::BitCast, operands[0],
                    node.results().front()->type(), destination);
            } else if (kind == ComputeKind::Splat &&
                       operands.size() == 1) {
                auto *vectorType =
                    dynamic_cast<VectorType *>(
                        node.results().front()->type());
                if (!vectorType)
                    return fail(
                        ExportRejectReason::UnsupportedNode);
                Value *packed = operands[0];
                for (unsigned lane = 0;
                     lane < vectorType->num_elements_; ++lane) {
                    auto *index = new ConstantInt(
                        module_->int32_ty_,
                        static_cast<int>(lane));
                    auto *insert = new InsertElementInst(
                        packed, operands[0], index, destination);
                    if (lane == 0)
                        insert->type_ = vectorType;
                    packed = insert;
                }
                instruction =
                    dynamic_cast<Instruction *>(packed);
            } else if (kind == ComputeKind::InsertElement &&
                       operands.size() == 3) {
                instruction = new InsertElementInst(
                    operands[0], operands[1], operands[2],
                    destination);
            } else if (kind == ComputeKind::ExtractElement &&
                       operands.size() == 2) {
                instruction = new ExtractElementInst(
                    operands[0], operands[1], destination);
            } else {
                return fail(ExportRejectReason::UnsupportedNode);
            }
        } else {
            return fail(ExportRejectReason::UnsupportedNode);
        }

        if (Instruction *source =
                region_.sourceMapping().sourceInstruction(&node))
            instruction->copySemFlagsFrom(source);
        if (!node.results().empty())
            bind(node.results().front(), instruction);
        return true;
    }

    void replaceExternalUses(Value *oldValue, Value *newValue) {
        auto uses = oldValue->use_list_;
        for (const Use &use : uses) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && !sourceLoop_->isInLoop(user))
                user->set_operand(use.arg_no_, newValue);
        }
    }

    void commit() {
        for (HiraValue *result : region_.results()) {
            Value *oldValue =
                region_.sourceMapping().sourceValue(result);
            Value *newValue = value(result);
            replaceExternalUses(oldValue, newValue);
        }

        for (const ExitPhiRewrite &rewrite :
             exitPhiRewrites_) {
            Value *replacement = rewrite.sourceValue;
            auto *sourceInstruction =
                dynamic_cast<Instruction *>(replacement);
            if (sourceInstruction &&
                sourceLoop_->isInLoop(sourceInstruction)) {
                replacement = nullptr;
                for (HiraValue *result : region_.results()) {
                    if (region_.sourceMapping().sourceValue(result) !=
                        rewrite.sourceValue)
                        continue;
                    replacement = value(result);
                    break;
                }
            }
            for (int index =
                     static_cast<int>(rewrite.phi->num_ops_) - 2;
                 index >= 0; index -= 2) {
                auto *predecessor =
                    dynamic_cast<BasicBlock *>(
                        rewrite.phi->get_operand(index + 1));
                if (predecessor &&
                    sourceLoop_->isInLoop(predecessor))
                    rewrite.phi->remove_operands(index,
                                                 index + 1);
            }
            rewrite.phi->addIncoming(
                replacement, newExitPredecessor_);
        }

        for (BasicBlock *entry : entryPredecessors_) {
            Instruction *oldBranch = entry->get_terminator();
            entry->remove_succ_basic_block(oldHeader_);
            oldHeader_->remove_pre_basic_block(entry);
            for (unsigned index = 0;
                 index < oldBranch->num_ops_; ++index)
                if (oldBranch->get_operand(index) == oldHeader_)
                    oldBranch->set_operand(index, entryTarget_);
            entry->add_succ_basic_block(entryTarget_);
            entryTarget_->add_pre_basic_block(entry);
        }

        std::vector<BasicBlock *> oldBlocks =
            sourceLoop_->blocksOrdered;
        for (BasicBlock *block : oldBlocks) {
            std::vector<Instruction *> instructions(
                block->instr_list_.begin(), block->instr_list_.end());
            for (auto it = instructions.rbegin();
                 it != instructions.rend(); ++it)
                block->delete_instr(*it);
        }
        for (BasicBlock *block : oldBlocks)
            function_->remove_bb(block);
        function_->invalidateDominatorInfo();
    }

    void rollbackNewBlocks() {
        for (BasicBlock *block : newBlocks_) {
            std::vector<Instruction *> instructions(
                block->instr_list_.begin(), block->instr_list_.end());
            for (auto it = instructions.rbegin();
                 it != instructions.rend(); ++it)
                block->delete_instr(*it);
        }
        for (BasicBlock *block : newBlocks_)
            if (block->parent_)
                block->parent_->remove_bb(block);
        for (AllocaInst *alloca : scratchAllocas_)
            if (alloca && alloca->parent_)
                alloca->parent_->delete_instr(alloca);
        scratchAllocas_.clear();
    }

    HiraRegion &region_;
    Loop *sourceLoop_ = nullptr;
    HiraLoop *rootLoop_ = nullptr;
    Function *function_ = nullptr;
    Function *blockFunction_ = nullptr;
    Module *module_ = nullptr;
    std::vector<BasicBlock *> entryPredecessors_;
    BasicBlock *oldHeader_ = nullptr;
    BasicBlock *oldExit_ = nullptr;
    BasicBlock *rootHeader_ = nullptr;
    BasicBlock *entryTarget_ = nullptr;
    BasicBlock *newExitPredecessor_ = nullptr;
    std::size_t rootLoopIndex_ = 0;
    std::vector<BasicBlock *> newBlocks_;
    std::vector<ExitPhiRewrite> exitPhiRewrites_;
    std::vector<HiraValue *> parallelCaptures_;
    std::vector<AllocaInst *> scratchAllocas_;
    std::map<HiraValue *, Value *> values_;
    ExportResult failure_;

    static unsigned nextBlockId_;
};

unsigned RegionExporter::nextBlockId_ = 0;

} // namespace

ExportResult ExportResult::success() {
    ExportResult result;
    result.changed = true;
    return result;
}

ExportResult ExportResult::reject(ExportRejectReason reason,
                                  std::string detail) {
    ExportResult result;
    result.reason = reason;
    result.detail = std::move(detail);
    return result;
}

const char *exportRejectReasonName(ExportRejectReason reason) {
    switch (reason) {
    case ExportRejectReason::None:
        return "none";
    case ExportRejectReason::InvalidRegion:
        return "invalid-region";
    case ExportRejectReason::UnsupportedExitPhi:
        return "unsupported-exit-phi";
    case ExportRejectReason::UnsupportedNode:
        return "unsupported-node";
    case ExportRejectReason::UnsupportedResult:
        return "unsupported-result";
    case ExportRejectReason::MissingValue:
        return "missing-value";
    case ExportRejectReason::InvalidSourceCFG:
        return "invalid-source-cfg";
    }
    return "unknown";
}

ExportResult exportHiraRegion(HiraRegion &region) {
    return RegionExporter(region).run();
}

} // namespace hira
