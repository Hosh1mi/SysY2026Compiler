// Detect proved multiplicative reset points in loop-carried memory state and
// eliminate the dynamically dead prefix without depending on source layout.

#include "../../../include/mid/opt/loopResetPointElimination.hpp"

#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../../include/mid/analysis/basicAliasAnalysis.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/globalVariable.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <unordered_set>
#include <vector>

namespace {

bool debugEnabled() {
    return std::getenv("DEBUG_LOOP_RESET_POINT_ELIMINATION") != nullptr;
}

void debugLog(const std::string &message) {
    if (debugEnabled())
        std::cerr << "[LoopResetPointElimination] " << message << "\n";
}

Value *incomingFrom(PhiInst *phi, BasicBlock *predecessor) {
    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2)
        if (phi->get_operand(i + 1) == predecessor)
            return phi->get_operand(i);
    return nullptr;
}

bool removePhiIncoming(PhiInst *phi, BasicBlock *predecessor) {
    for (int i = static_cast<int>(phi->num_ops_) - 1; i >= 1; i -= 2) {
        if (phi->get_operand(static_cast<unsigned>(i)) == predecessor) {
            phi->remove_operands(i - 1, i);
            return true;
        }
    }
    return false;
}

bool isConstantInt(Value *value, int expected) {
    auto *constant = dynamic_cast<ConstantInt *>(value);
    return constant && constant->value_ == expected;
}

bool matchAddOne(Value *value, Value *base) {
    auto *add = dynamic_cast<BinaryInst *>(value);
    if (!add || !add->is_add())
        return false;
    return (add->get_operand(0) == base &&
            isConstantInt(add->get_operand(1), 1)) ||
           (add->get_operand(1) == base &&
            isConstantInt(add->get_operand(0), 1));
}

bool isIntegerScalar(Value *value) {
    return value && value->type_ && value->type_->tid_ == Type::IntegerTyID &&
           static_cast<IntegerType *>(value->type_)->num_bits_ == 32;
}

bool valueDependsOnImpl(Value *value, Value *needle,
                        std::unordered_set<Value *> &visited) {
    if (value == needle)
        return true;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction || !visited.insert(value).second)
        return false;
    for (unsigned i = 0; i < instruction->num_ops_; ++i) {
        Value *operand = instruction->get_operand(i);
        if (dynamic_cast<BasicBlock *>(operand) ||
            dynamic_cast<Function *>(operand))
            continue;
        if (valueDependsOnImpl(operand, needle, visited))
            return true;
    }
    return false;
}

bool valueDependsOn(Value *value, Value *needle) {
    std::unordered_set<Value *> visited;
    return valueDependsOnImpl(value, needle, visited);
}

void collectLoadsImpl(Value *value, std::set<LoadInst *> &loads,
                      std::unordered_set<Value *> &visited) {
    if (!value || !visited.insert(value).second)
        return;
    if (auto *load = dynamic_cast<LoadInst *>(value)) {
        loads.insert(load);
        collectLoadsImpl(load->get_operand(0), loads, visited);
        return;
    }
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction)
        return;
    for (unsigned i = 0; i < instruction->num_ops_; ++i) {
        Value *operand = instruction->get_operand(i);
        if (!dynamic_cast<BasicBlock *>(operand) &&
            !dynamic_cast<Function *>(operand))
            collectLoadsImpl(operand, loads, visited);
    }
}

std::set<LoadInst *> collectLoads(Value *value) {
    std::set<LoadInst *> loads;
    std::unordered_set<Value *> visited;
    collectLoadsImpl(value, loads, visited);
    return loads;
}

bool containsUnexpectedPhiImpl(Value *value, const Loop &loop,
                               const std::set<Value *> &allowed,
                               std::unordered_set<Value *> &visited) {
    if (!value || allowed.count(value) || !visited.insert(value).second)
        return false;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction)
        return false;
    if (dynamic_cast<PhiInst *>(instruction) && loop.isInLoop(instruction))
        return true;
    for (unsigned i = 0; i < instruction->num_ops_; ++i) {
        Value *operand = instruction->get_operand(i);
        if (dynamic_cast<BasicBlock *>(operand) ||
            dynamic_cast<Function *>(operand))
            continue;
        if (containsUnexpectedPhiImpl(operand, loop, allowed, visited))
            return true;
    }
    return false;
}

bool containsUnexpectedPhi(Value *value, const Loop &loop,
                           const std::set<Value *> &allowed) {
    std::unordered_set<Value *> visited;
    return containsUnexpectedPhiImpl(value, loop, allowed, visited);
}

bool hasUnsafeLiveOut(const Loop &loop, PhiInst *induction) {
    BasicBlock *latch = loop.singleLatch();
    Value *inductionUpdate = latch ? incomingFrom(induction, latch) : nullptr;
    for (BasicBlock *block : loop.blocksOrdered) {
        for (Instruction *instruction : block->instr_list_) {
            bool safeInductionValue = instruction == induction ||
                                      instruction == inductionUpdate;
            for (const Use &use : instruction->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && user->parent_ && !loop.isInLoop(user) &&
                    !safeInductionValue)
                    return true;
            }
        }
    }
    return false;
}

Value *underlyingObject(Value *pointer) {
    return ArgumentAliasAnalysis::underlyingObject(pointer);
}

bool provenNoAlias(Value *first, Value *second, BasicAliasAnalysis &basicAA,
                   ArgumentAliasAnalysis &argumentAA) {
    if (basicAA.alias(first, second) == AliasResult::NoAlias)
        return true;
    Value *firstRoot = underlyingObject(first);
    Value *secondRoot = underlyingObject(second);
    return firstRoot && secondRoot &&
           argumentAA.noAlias(firstRoot, secondRoot);
}

bool matchPlusOneIV(Loop &loop, PhiInst **phiOut, Value **boundOut,
                    BasicBlock **latchOut) {
    PhiInst *phi = loop.getInductionIV();
    BasicBlock *latch = loop.singleLatch();
    if (!phi || !latch || !loop.preheader || !loop.tripCount ||
        loop.predicate != ICmpInst::ICMP_SLT ||
        loop.controlInduction.guardPosition != InductionGuardPosition::Header)
        return false;
    Value *fromLatch = incomingFrom(phi, latch);
    Value *fromPreheader = incomingFrom(phi, loop.preheader);
    if (!fromLatch || !matchAddOne(fromLatch, phi) ||
        !isConstantInt(fromPreheader, 0))
        return false;
    *phiOut = phi;
    *boundOut = loop.tripCount;
    *latchOut = latch;
    return true;
}

struct StoreRecurrence {
    StoreInst *store = nullptr;
    LoadInst *oldLoad = nullptr;
    Value *factor = nullptr;
    Value *fresh = nullptr;
};

bool matchStoreRecurrence(StoreInst *store, StoreRecurrence &result) {
    Value *stored = store->get_operand(0);
    Value *pointer = store->get_operand(1);
    BinaryInst *multiply = nullptr;
    Value *fresh = nullptr;

    if (auto *add = dynamic_cast<BinaryInst *>(stored); add && add->is_add()) {
        auto *left = dynamic_cast<BinaryInst *>(add->get_operand(0));
        auto *right = dynamic_cast<BinaryInst *>(add->get_operand(1));
        if (left && left->is_mul()) {
            multiply = left;
            fresh = add->get_operand(1);
        } else if (right && right->is_mul()) {
            multiply = right;
            fresh = add->get_operand(0);
        }
    } else if (auto *mul = dynamic_cast<BinaryInst *>(stored);
               mul && mul->is_mul()) {
        multiply = mul;
    }
    if (!multiply)
        return false;

    LoadInst *oldLoad = nullptr;
    Value *factor = nullptr;
    for (unsigned oldIndex = 0; oldIndex != 2; ++oldIndex) {
        auto *candidate =
            dynamic_cast<LoadInst *>(multiply->get_operand(oldIndex));
        if (candidate && candidate->get_operand(0) == pointer) {
            oldLoad = candidate;
            factor = multiply->get_operand(1 - oldIndex);
            break;
        }
    }
    if (!oldLoad || !isIntegerScalar(factor))
        return false;

    result.store = store;
    result.oldLoad = oldLoad;
    result.factor = factor;
    result.fresh = fresh;
    return true;
}

bool blockEntersLoop(BasicBlock *block, const Loop &loop) {
    if (!block)
        return false;
    if (loop.isInLoop(block))
        return true;
    auto *branch = dynamic_cast<BranchInst *>(block->get_terminator());
    return branch && branch->num_ops_ == 1 &&
           loop.isInLoop(dynamic_cast<BasicBlock *>(branch->get_operand(0)));
}

bool equivalentIterationValue(Value *first, Value *second) {
    if (first == second)
        return true;
    auto *firstLoad = dynamic_cast<LoadInst *>(first);
    auto *secondLoad = dynamic_cast<LoadInst *>(second);
    return firstLoad && secondLoad &&
           firstLoad->get_operand(0) == secondLoad->get_operand(0);
}

bool evaluateZeroCompare(ICmpInst *compare, Value *factor, bool &result) {
    Value *other = nullptr;
    bool factorOnLeft = false;
    if (equivalentIterationValue(compare->get_operand(0), factor)) {
        other = compare->get_operand(1);
        factorOnLeft = true;
    } else if (equivalentIterationValue(compare->get_operand(1), factor)) {
        other = compare->get_operand(0);
    } else {
        return false;
    }
    auto *constant = dynamic_cast<ConstantInt *>(other);
    if (!constant)
        return false;
    long long lhs = factorOnLeft ? 0 : constant->value_;
    long long rhs = factorOnLeft ? constant->value_ : 0;
    unsigned long long ulhs = static_cast<unsigned>(lhs);
    unsigned long long urhs = static_cast<unsigned>(rhs);
    switch (compare->icmp_op_) {
    case ICmpInst::ICMP_EQ: result = lhs == rhs; break;
    case ICmpInst::ICMP_NE: result = lhs != rhs; break;
    case ICmpInst::ICMP_SGT: result = lhs > rhs; break;
    case ICmpInst::ICMP_SGE: result = lhs >= rhs; break;
    case ICmpInst::ICMP_SLT: result = lhs < rhs; break;
    case ICmpInst::ICMP_SLE: result = lhs <= rhs; break;
    case ICmpInst::ICMP_UGT: result = ulhs > urhs; break;
    case ICmpInst::ICMP_UGE: result = ulhs >= urhs; break;
    case ICmpInst::ICMP_ULT: result = ulhs < urhs; break;
    case ICmpInst::ICMP_ULE: result = ulhs <= urhs; break;
    }
    return true;
}

bool resetExecutesStateLoop(Loop &recurrenceLoop, Loop &stateLoop,
                            Value *factor) {
    unsigned discriminatingBranches = 0;
    for (BasicBlock *block : recurrenceLoop.blocksOrdered) {
        if (stateLoop.isInLoop(block) || block == recurrenceLoop.header)
            continue;
        auto *branch = dynamic_cast<BranchInst *>(block->get_terminator());
        if (!branch || branch->num_ops_ == 1)
            continue;
        if (branch->num_ops_ != 3)
            return false;
        auto *trueBlock = dynamic_cast<BasicBlock *>(branch->get_operand(1));
        auto *falseBlock = dynamic_cast<BasicBlock *>(branch->get_operand(2));
        bool trueEnters = blockEntersLoop(trueBlock, stateLoop);
        bool falseEnters = blockEntersLoop(falseBlock, stateLoop);
        if (trueEnters == falseEnters)
            return false;
        auto *compare = dynamic_cast<ICmpInst *>(branch->get_operand(0));
        bool compareAtZero = false;
        if (!compare ||
            !evaluateZeroCompare(compare, factor, compareAtZero))
            return false;
        bool entersAtZero = compareAtZero ? trueEnters : falseEnters;
        if (!entersAtZero || ++discriminatingBranches != 1)
            return false;
    }
    return discriminatingBranches <= 1;
}

bool cloneableAtScan(Value *value, Value *induction, const Loop &loop,
                     std::unordered_set<Value *> &visiting) {
    if (value == induction || dynamic_cast<Constant *>(value) ||
        dynamic_cast<Argument *>(value) ||
        dynamic_cast<GlobalVariable *>(value))
        return true;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction)
        return false;
    if (!loop.isInLoop(instruction))
        return true;
    if (!visiting.insert(value).second)
        return false;

    bool supported = dynamic_cast<GetElementPtrInst *>(instruction) ||
                     dynamic_cast<BinaryInst *>(instruction) ||
                     dynamic_cast<Bitcast *>(instruction) ||
                     dynamic_cast<ZextInst *>(instruction) ||
                     dynamic_cast<LoadInst *>(instruction);
    if (!supported)
        return false;
    for (unsigned i = 0; i < instruction->num_ops_; ++i)
        if (!cloneableAtScan(instruction->get_operand(i), induction, loop,
                             visiting))
            return false;
    visiting.erase(value);
    return true;
}

struct ResetCandidate {
    Loop *recurrenceLoop = nullptr;
    Loop *stateLoop = nullptr;
    PhiInst *induction = nullptr;
    BasicBlock *preheader = nullptr;
    BasicBlock *header = nullptr;
    BasicBlock *latch = nullptr;
    Value *bound = nullptr;
    Value *factor = nullptr;
    bool boundKnownPositive = false;
};

bool enclosingLoopProvesPositiveBound(const Loop &loop, Value *bound) {
    const Loop *parent = loop.parent;
    if (!parent || parent->tripCount != bound || !parent->getInductionIV() ||
        parent->predicate != ICmpInst::ICMP_SLT ||
        parent->controlInduction.guardPosition !=
            InductionGuardPosition::Header)
        return false;
    return isConstantInt(parent->inductionInit, 0);
}

bool analyzeCandidate(Loop &recurrenceLoop, LoopInfo &loopInfo,
                      BasicAliasAnalysis &basicAA,
                      ArgumentAliasAnalysis &argumentAA,
                      ResetCandidate &result) {
    if (recurrenceLoop.children.size() != 1)
        return false;
    Loop *stateLoop = recurrenceLoop.children.front();
    if (!stateLoop || !stateLoop->children.empty() ||
        stateLoop->exiting.size() != 1 ||
        stateLoop->exiting.front() != stateLoop->header)
        return false;

    PhiInst *induction = nullptr;
    Value *bound = nullptr;
    BasicBlock *latch = nullptr;
    if (!matchPlusOneIV(recurrenceLoop, &induction, &bound, &latch) ||
        !isIntegerScalar(bound) || valueDependsOn(bound, induction))
        return false;
    PhiInst *stateIV = stateLoop->getInductionIV();
    auto *stateInitInstruction =
        dynamic_cast<Instruction *>(stateLoop->inductionInit);
    auto *stateBoundInstruction =
        dynamic_cast<Instruction *>(stateLoop->tripCount);
    if (!stateIV || !stateLoop->singleLatch() || !stateLoop->tripCount ||
        (stateInitInstruction &&
         recurrenceLoop.isInLoop(stateInitInstruction)) ||
        (stateBoundInstruction &&
         recurrenceLoop.isInLoop(stateBoundInstruction)) ||
        hasUnsafeLiveOut(recurrenceLoop, induction))
        return false;

    std::vector<StoreRecurrence> recurrences;
    for (BasicBlock *block : recurrenceLoop.blocksOrdered) {
        for (Instruction *instruction : block->instr_list_) {
            if (instruction->is_call() || instruction->is_ret())
                return false;
            auto *store = dynamic_cast<StoreInst *>(instruction);
            if (!store)
                continue;
            if (!stateLoop->isInLoop(store))
                return false;
            StoreRecurrence recurrence;
            if (!matchStoreRecurrence(store, recurrence) ||
                valueDependsOn(store->get_operand(1), induction) ||
                containsUnexpectedPhi(store->get_operand(1), recurrenceLoop,
                                      {stateIV}) ||
                !loopInfo.dominates(store->parent_, stateLoop->singleLatch()))
                return false;
            recurrences.push_back(recurrence);
        }
    }
    if (recurrences.empty())
        return false;

    Value *factor = recurrences.front().factor;
    if (!valueDependsOn(factor, induction))
        return false;
    for (const StoreRecurrence &recurrence : recurrences)
        if (recurrence.factor != factor)
            return false;

    std::set<LoadInst *> factorLoads = collectLoads(factor);
    if (factorLoads.empty())
        return false;
    for (const StoreRecurrence &recurrence : recurrences) {
        if (recurrence.fresh &&
            containsUnexpectedPhi(recurrence.fresh, recurrenceLoop,
                                  {induction, stateIV}))
            return false;
        for (const StoreRecurrence &stateRecurrence : recurrences) {
            Value *statePointer = stateRecurrence.store->get_operand(1);
            for (LoadInst *load : factorLoads)
                if (!provenNoAlias(load->get_operand(0), statePointer,
                                   basicAA, argumentAA))
                    return false;
            if (recurrence.fresh) {
                for (LoadInst *load : collectLoads(recurrence.fresh))
                    if (!provenNoAlias(load->get_operand(0), statePointer,
                                       basicAA, argumentAA))
                        return false;
            }
        }
    }

    if (!resetExecutesStateLoop(recurrenceLoop, *stateLoop, factor))
        return false;
    std::unordered_set<Value *> visiting;
    if (!cloneableAtScan(factor, induction, recurrenceLoop, visiting))
        return false;

    result.recurrenceLoop = &recurrenceLoop;
    result.stateLoop = stateLoop;
    result.induction = induction;
    result.preheader = recurrenceLoop.preheader;
    result.header = recurrenceLoop.header;
    result.latch = latch;
    result.bound = bound;
    result.factor = factor;
    result.boundKnownPositive =
        enclosingLoopProvesPositiveBound(recurrenceLoop, bound);
    return true;
}

Value *cloneAtScan(Value *value, Value *induction, Value *scanIndex,
                   const Loop &loop, BasicBlock *block,
                   std::map<Value *, Value *> &clones) {
    if (value == induction)
        return scanIndex;
    auto found = clones.find(value);
    if (found != clones.end())
        return found->second;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction || !loop.isInLoop(instruction))
        return value;

    Value *clone = nullptr;
    if (auto *gep = dynamic_cast<GetElementPtrInst *>(instruction)) {
        Value *base = cloneAtScan(gep->get_operand(0), induction, scanIndex,
                                  loop, block, clones);
        std::vector<Value *> indices;
        for (unsigned i = 1; i < gep->num_ops_; ++i)
            indices.push_back(cloneAtScan(gep->get_operand(i), induction,
                                          scanIndex, loop, block, clones));
        clone = new GetElementPtrInst(base, indices, block);
    } else if (auto *binary = dynamic_cast<BinaryInst *>(instruction)) {
        Value *left = cloneAtScan(binary->get_operand(0), induction,
                                  scanIndex, loop, block, clones);
        Value *right = cloneAtScan(binary->get_operand(1), induction,
                                   scanIndex, loop, block, clones);
        clone = new BinaryInst(binary->type_, binary->op_id_, left, right,
                               block);
    } else if (auto *bitcast = dynamic_cast<Bitcast *>(instruction)) {
        Value *operand = cloneAtScan(bitcast->get_operand(0), induction,
                                     scanIndex, loop, block, clones);
        clone = new Bitcast(Instruction::BitCast, operand, bitcast->type_,
                            block);
    } else if (auto *zext = dynamic_cast<ZextInst *>(instruction)) {
        Value *operand = cloneAtScan(zext->get_operand(0), induction,
                                     scanIndex, loop, block, clones);
        clone = new ZextInst(Instruction::ZExt, operand, zext->type_, block);
    } else if (auto *load = dynamic_cast<LoadInst *>(instruction)) {
        Value *pointer = cloneAtScan(load->get_operand(0), induction,
                                     scanIndex, loop, block, clones);
        clone = new LoadInst(pointer, block);
    }
    if (clone)
        clones[value] = clone;
    return clone;
}

bool applyTighten(const ResetCandidate &candidate, Module *module,
                  Function *function) {
    BasicBlock *preheader = candidate.preheader;
    BasicBlock *header = candidate.header;
    auto *oldBranch =
        dynamic_cast<BranchInst *>(preheader->get_terminator());
    if (!oldBranch || oldBranch->num_ops_ != 1 ||
        oldBranch->get_operand(0) != header)
        return false;

    header->remove_pre_basic_block(preheader);
    preheader->remove_succ_basic_block(header);
    preheader->delete_instr(oldBranch);

    auto *zero = new ConstantInt(module->int32_ty_, 0);
    auto *one = new ConstantInt(module->int32_ty_, 1);
    BasicBlock *scanInit = nullptr;
    if (!candidate.boundKnownPositive)
        scanInit = new BasicBlock(module, "resetpoint.scan.init", function);
    auto *scanHeader =
        new BasicBlock(module, "resetpoint.scan.header", function);
    auto *scanBody =
        new BasicBlock(module, "resetpoint.scan.body", function);
    auto *scanContinue =
        new BasicBlock(module, "resetpoint.scan.cont", function);
    auto *scanFound =
        new BasicBlock(module, "resetpoint.scan.found", function);
    auto *scanMiss =
        new BasicBlock(module, "resetpoint.scan.miss", function);
    auto *merge = new BasicBlock(module, "resetpoint.merge", function);

    BasicBlock *scanEntry = preheader;
    if (!candidate.boundKnownPositive) {
        auto *hasIterations = new ICmpInst(ICmpInst::ICMP_SGT,
                                           candidate.bound, zero, preheader);
        new BranchInst(hasIterations, scanInit, merge, preheader);
        scanEntry = scanInit;
    }
    auto *lastIndex = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                     candidate.bound, one, scanEntry);
    new BranchInst(scanHeader, scanEntry);

    auto *scanIndex = PhiInst::create_phi(module->int32_ty_, scanHeader);
    scanHeader->add_instruction_front(scanIndex);
    scanIndex->addIncoming(lastIndex, scanEntry);
    auto *inRange = new ICmpInst(ICmpInst::ICMP_SGE, scanIndex, zero,
                                 scanHeader);
    new BranchInst(inRange, scanBody, scanMiss, scanHeader);

    std::map<Value *, Value *> clones;
    Value *scanFactor = cloneAtScan(candidate.factor, candidate.induction,
                                    scanIndex, *candidate.recurrenceLoop,
                                    scanBody, clones);
    auto *factorZero =
        new ConstantInt(scanFactor ? scanFactor->type_ : module->int32_ty_, 0);
    auto *isReset = new ICmpInst(ICmpInst::ICMP_EQ, scanFactor, factorZero,
                                 scanBody);
    new BranchInst(isReset, scanFound, scanContinue, scanBody);

    auto *previous = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                    scanIndex, one, scanContinue);
    new BranchInst(scanHeader, scanContinue);
    scanIndex->addIncoming(previous, scanContinue);

    new BranchInst(merge, scanFound);
    new BranchInst(merge, scanMiss);

    auto *start = PhiInst::create_phi(module->int32_ty_, merge);
    merge->add_instruction_front(start);
    if (!candidate.boundKnownPositive)
        start->addIncoming(zero, preheader);
    start->addIncoming(scanIndex, scanFound);
    start->addIncoming(zero, scanMiss);
    new BranchInst(header, merge);

    if (!removePhiIncoming(candidate.induction, preheader))
        return false;
    candidate.induction->addIncoming(start, merge);

    debugLog("tightened recurrence loop=" + header->name_ +
             " state loop=" + candidate.stateLoop->header->name_ +
             " in func=" + function->name_);
    return true;
}

bool runOnFunctionImpl(Function *function, Module *module,
                       BasicAliasAnalysis &basicAA,
                       ArgumentAliasAnalysis &argumentAA) {
    if (function->basic_blocks_.empty())
        return false;

    LoopInfo loopInfo;
    loopInfo.analyze(function);
    std::vector<ResetCandidate> candidates;
    std::set<BasicBlock *> seenHeaders;
    for (const auto &loop : loopInfo.allLoops()) {
        ResetCandidate candidate;
        if (analyzeCandidate(*loop, loopInfo, basicAA, argumentAA,
                             candidate) &&
            seenHeaders.insert(candidate.header).second)
            candidates.push_back(candidate);
    }

    bool changed = false;
    for (const ResetCandidate &candidate : candidates)
        changed |= applyTighten(candidate, module, function);
    if (changed)
        function->set_instr_name();
    return changed;
}

} // namespace

void LoopResetPointElimination::execute(Module *module) {
    BasicAliasAnalysis basicAA;
    basicAA.analyze(module);
    ArgumentAliasAnalysis argumentAA;
    argumentAA.analyze(module);
    for (Function *function : module->function_list_)
        if (!function->is_declaration())
            runOnFunction(function, module, basicAA, argumentAA);
}

PreservedAnalyses LoopResetPointElimination::execute(
    Module *module, AnalysisManager &manager) {
    BasicAliasAnalysis &basicAA = manager.getBasicAA(module);
    ArgumentAliasAnalysis argumentAA;
    argumentAA.analyze(module);
    bool changed = false;
    for (Function *function : module->function_list_)
        if (!function->is_declaration())
            changed |= runOnFunction(function, module, basicAA, argumentAA);
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool LoopResetPointElimination::runOnFunction(
    Function *function, Module *module, BasicAliasAnalysis &basicAA,
    ArgumentAliasAnalysis &argumentAA) {
    return runOnFunctionImpl(function, module, basicAA, argumentAA);
}
