// Demand-driven lowering for a proven sequence of ordered triangular copies.
// The original consumer is preserved; only its matched array load is versioned.

#include "../../../include/mid/opt/triangularRemapSourceCompose.hpp"

#include "../../../include/mid/analysis/basicAliasAnalysis.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/globalVariable.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <queue>
#include <set>
#include <string>
#include <vector>

namespace {

bool debugEnabled() {
    return std::getenv("DEBUG_TRIANGULAR_REMAP_SOURCE") != nullptr;
}

Value *stripBitcast(Value *value) {
    while (auto *bitcast = dynamic_cast<Bitcast *>(value))
        value = bitcast->get_operand(0);
    return value;
}

Value *pointerRoot(Value *value) {
    value = stripBitcast(value);
    while (auto *gep = dynamic_cast<GetElementPtrInst *>(value))
        value = stripBitcast(gep->get_operand(0));
    return value;
}

bool isI32(Value *value, Module *module) {
    return value && value->type_ == module->int32_ty_;
}

bool isConstant(Value *value, int expected) {
    auto *constant = dynamic_cast<ConstantInt *>(value);
    return constant && constant->value_ == expected;
}

bool matchAddOne(Value *value, Value *base) {
    auto *add = dynamic_cast<BinaryInst *>(value);
    if (!add || !add->is_add())
        return false;
    return (add->get_operand(0) == base && isConstant(add->get_operand(1), 1)) ||
           (add->get_operand(1) == base && isConstant(add->get_operand(0), 1));
}

bool matchScaledPlus(Value *value, Value *scaled, Value *unit,
                     Value **scaleOut) {
    auto *add = dynamic_cast<BinaryInst *>(value);
    if (!add || !add->is_add())
        return false;

    Value *productValue = nullptr;
    if (add->get_operand(0) == unit)
        productValue = add->get_operand(1);
    else if (add->get_operand(1) == unit)
        productValue = add->get_operand(0);
    else
        return false;

    auto *product = dynamic_cast<BinaryInst *>(productValue);
    if (!product || !product->is_mul())
        return false;
    if (product->get_operand(0) == scaled)
        *scaleOut = product->get_operand(1);
    else if (product->get_operand(1) == scaled)
        *scaleOut = product->get_operand(0);
    else
        return false;
    return true;
}

bool matchTriangularBound(Value *bound, Value *outerIV, Value *rowsize) {
    auto *select = dynamic_cast<SelectInst *>(bound);
    if (!select)
        return false;

    Value *next = select->get_operand(1);
    Value *other = select->get_operand(2);
    if (!matchAddOne(next, outerIV) || other != rowsize)
        return false;

    auto *compare = dynamic_cast<ICmpInst *>(select->get_operand(0));
    if (!compare)
        return false;
    if (compare->icmp_op_ == ICmpInst::ICMP_SLT)
        return compare->get_operand(0) == next &&
               compare->get_operand(1) == rowsize;
    if (compare->icmp_op_ == ICmpInst::ICMP_SGT)
        return compare->get_operand(0) == rowsize &&
               compare->get_operand(1) == next;
    return false;
}

struct ElementAccess {
    GetElementPtrInst *gep = nullptr;
    Value *base = nullptr;
    Value *index = nullptr;
    bool flat = true;
};

bool parseElementAccess(Value *pointer, ElementAccess &access) {
    auto *gep = dynamic_cast<GetElementPtrInst *>(stripBitcast(pointer));
    if (!gep)
        return false;
    auto *pointerType = dynamic_cast<PointerType *>(gep->get_operand(0)->type_);
    if (!pointerType)
        return false;

    access = ElementAccess{};
    access.gep = gep;
    access.base = gep->get_operand(0);
    if (pointerType->contained_->tid_ == Type::ArrayTyID) {
        if (gep->num_ops_ != 3 || !isConstant(gep->get_operand(1), 0))
            return false;
        access.index = gep->get_operand(2);
        access.flat = false;
    } else {
        if (gep->num_ops_ != 2)
            return false;
        access.index = gep->get_operand(1);
        access.flat = true;
    }
    return true;
}

int globalArrayExtent(Value *root, Module *module) {
    auto *global = dynamic_cast<GlobalVariable *>(root);
    auto *pointerType = global
                            ? dynamic_cast<PointerType *>(global->type_)
                            : nullptr;
    auto *arrayType = pointerType
                          ? dynamic_cast<ArrayType *>(pointerType->contained_)
                          : nullptr;
    if (!arrayType || arrayType->contained_ != module->int32_ty_ ||
        arrayType->num_elements_ >
            static_cast<unsigned>(std::numeric_limits<int>::max()))
        return -1;
    return static_cast<int>(arrayType->num_elements_);
}

bool valueAvailableAt(Value *value, BasicBlock *block, const LoopInfo &LI) {
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction)
        return true;
    if (!instruction->parent_)
        return false;
    return instruction->parent_ == block ||
           LI.dominates(instruction->parent_, block);
}

void removeTerminatorAndEdges(BasicBlock *block) {
    auto *terminator = block ? block->get_terminator() : nullptr;
    if (!terminator)
        return;
    std::vector<BasicBlock *> successors = block->succ_bbs_;
    for (auto *successor : successors)
        successor->remove_pre_basic_block(block);
    block->succ_bbs_.clear();
    block->delete_instr(terminator);
}

void retargetPhiPredecessor(BasicBlock *block, BasicBlock *oldPredecessor,
                            BasicBlock *newPredecessor) {
    for (auto *instruction : block->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(instruction);
        if (!phi)
            break;
        for (unsigned index = 1; index < phi->num_ops_; index += 2) {
            if (phi->get_operand(index) == oldPredecessor)
                phi->set_operand(index, newPredecessor);
        }
    }
}

void redirectEdge(BasicBlock *from, BasicBlock *oldTarget,
                  BasicBlock *newTarget) {
    auto *branch = from
                       ? dynamic_cast<BranchInst *>(from->get_terminator())
                       : nullptr;
    if (!branch)
        return;
    for (unsigned index = 0; index < branch->num_ops_; ++index) {
        if (branch->get_operand(index) == oldTarget)
            branch->set_operand(index, newTarget);
    }
    from->remove_succ_basic_block(oldTarget);
    oldTarget->remove_pre_basic_block(from);
    from->add_succ_basic_block(newTarget);
    newTarget->add_pre_basic_block(from);
}

bool hasPhi(BasicBlock *block) {
    return block && !block->instr_list_.empty() &&
           block->instr_list_.front()->is_phi();
}

struct RemapPattern {
    Loop *outer = nullptr;
    Loop *inner = nullptr;
    PhiInst *outerIV = nullptr;
    PhiInst *innerIV = nullptr;
    Value *rowsize = nullptr;
    Value *colsize = nullptr;
    Value *matrixBase = nullptr;
    Value *matrixRoot = nullptr;
    bool matrixFlat = true;
    LoadInst *load = nullptr;
    StoreInst *store = nullptr;
};

bool isNonTrappingStructuralInstruction(Instruction *instruction) {
    if (!instruction)
        return false;
    if (instruction->is_phi() || instruction->is_br() ||
        dynamic_cast<ICmpInst *>(instruction) ||
        dynamic_cast<SelectInst *>(instruction) ||
        dynamic_cast<GetElementPtrInst *>(instruction) ||
        dynamic_cast<Bitcast *>(instruction))
        return true;
    auto *binary = dynamic_cast<BinaryInst *>(instruction);
    return binary &&
           (binary->is_add() || binary->is_sub() || binary->is_mul());
}

bool matchRemap(Loop *outer, Module *module, RemapPattern &pattern) {
    if (!outer || outer->children.size() != 1 || !outer->canonicalIV ||
        !outer->preheader || !outer->singleLatch() || !outer->singleExit())
        return false;
    Loop *inner = outer->children.front();
    if (!inner || !inner->children.empty() || !inner->canonicalIV ||
        !inner->preheader || !inner->singleLatch() || !inner->singleExit())
        return false;

    LoadInst *load = nullptr;
    StoreInst *store = nullptr;
    for (auto *block : inner->blocksOrdered) {
        for (auto *instruction : block->instr_list_) {
            if (instruction->is_call())
                return false;
            if (instruction->is_load()) {
                if (load)
                    return false;
                load = static_cast<LoadInst *>(instruction);
            } else if (instruction->is_store()) {
                if (store)
                    return false;
                store = static_cast<StoreInst *>(instruction);
            }
        }
    }
    if (!load || !store || store->get_operand(0) != load)
        return false;

    ElementAccess readAccess;
    ElementAccess writeAccess;
    if (!parseElementAccess(load->get_operand(0), readAccess) ||
        !parseElementAccess(store->get_operand(1), writeAccess) ||
        readAccess.base != writeAccess.base ||
        readAccess.flat != writeAccess.flat)
        return false;

    Value *rowsize = nullptr;
    Value *colsize = nullptr;
    if (!matchScaledPlus(readAccess.index, outer->canonicalIV,
                         inner->canonicalIV, &rowsize) ||
        !matchScaledPlus(writeAccess.index, inner->canonicalIV,
                         outer->canonicalIV, &colsize) ||
        !isI32(rowsize, module) || !isI32(colsize, module))
        return false;

    if (outer->tripCount != colsize ||
        !matchTriangularBound(inner->tripCount, outer->canonicalIV, rowsize))
        return false;
    if (auto *instruction = dynamic_cast<Instruction *>(rowsize);
        instruction && outer->isInLoop(instruction))
        return false;
    if (auto *instruction = dynamic_cast<Instruction *>(colsize);
        instruction && outer->isInLoop(instruction))
        return false;

    for (auto *block : outer->blocksOrdered) {
        for (auto *instruction : block->instr_list_) {
            if (instruction == load || instruction == store)
                continue;
            if (!isNonTrappingStructuralInstruction(instruction))
                return false;
        }
    }

    pattern = RemapPattern{};
    pattern.outer = outer;
    pattern.inner = inner;
    pattern.outerIV = outer->canonicalIV;
    pattern.innerIV = inner->canonicalIV;
    pattern.rowsize = rowsize;
    pattern.colsize = colsize;
    pattern.matrixBase = readAccess.base;
    pattern.matrixRoot = pointerRoot(readAccess.base);
    pattern.matrixFlat = readAccess.flat;
    pattern.load = load;
    pattern.store = store;
    return pattern.matrixRoot != nullptr;
}

bool hasLiveOutValue(Loop *loop) {
    for (auto *block : loop->blocksOrdered) {
        for (auto *instruction : block->instr_list_) {
            for (const Use &use : instruction->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && !loop->isInLoop(user))
                    return true;
            }
        }
    }
    return false;
}

bool matchRowsizeLoad(Value *rowsize, PhiInst *index, Value **baseOut,
                      Value **rootOut, bool *flatOut) {
    auto *load = dynamic_cast<LoadInst *>(rowsize);
    ElementAccess access;
    if (!load || !parseElementAccess(load->get_operand(0), access) ||
        access.index != index)
        return false;
    *baseOut = access.base;
    *rootOut = pointerRoot(access.base);
    *flatOut = access.flat;
    return *rootOut != nullptr;
}

bool matchExtentDivision(Value *colsize, Value *rowsize, Value **extentOut) {
    auto *division = dynamic_cast<BinaryInst *>(colsize);
    if (!division || !division->is_div() ||
        division->get_operand(1) != rowsize)
        return false;
    *extentOut = division->get_operand(0);
    return true;
}

bool loopBodyHasOnlyRowsizeRead(Loop *top, const RemapPattern &remap) {
    for (auto *block : top->blocksOrdered) {
        if (remap.outer->isInLoop(block))
            continue;
        for (auto *instruction : block->instr_list_) {
            if (instruction == remap.rowsize ||
                instruction == remap.colsize)
                continue;
            if (!isNonTrappingStructuralInstruction(instruction))
                return false;
        }
    }
    return true;
}

bool simpleConsumerPreheader(Loop *consumer) {
    BasicBlock *preheader = consumer ? consumer->preheader : nullptr;
    if (!preheader || hasPhi(preheader) || preheader->instr_list_.size() != 1)
        return false;
    auto *branch = dynamic_cast<BranchInst *>(preheader->get_terminator());
    return branch && branch->num_ops_ == 1 &&
           branch->get_operand(0) == consumer->header;
}

bool callMayAccessMatrix(CallInst *call, Value *matrixRoot,
                        const BasicAliasAnalysis &BAA) {
    if (!call)
        return true;
    auto *callee = dynamic_cast<Function *>(
        call->get_operand(call->num_ops_ - 1));
    if (!callee)
        return true;
    if (!callee->is_declaration())
        return BAA.getCallModRef(call, matrixRoot) != ModRefInfo::NoModRef;

    for (unsigned index = 0; index + 1 < call->num_ops_; ++index) {
        Value *argument = call->get_operand(index);
        if (!dynamic_cast<PointerType *>(argument->type_))
            continue;
        if (BAA.alias(argument, matrixRoot) != AliasResult::NoAlias)
            return true;
    }
    // SysY declarations are runtime interfaces.  Without a pointer argument
    // they have no route to a source-level array object.
    return false;
}

bool matrixUnobservedAfter(BasicBlock *start, Value *matrixRoot,
                           const BasicAliasAnalysis &BAA) {
    std::queue<BasicBlock *> worklist;
    std::set<BasicBlock *> visited;
    worklist.push(start);
    while (!worklist.empty()) {
        BasicBlock *block = worklist.front();
        worklist.pop();
        if (!block || !visited.insert(block).second)
            continue;
        for (auto *instruction : block->instr_list_) {
            if ((instruction->is_load() || instruction->is_store()) &&
                BAA.getModRefInfo(instruction, matrixRoot) !=
                    ModRefInfo::NoModRef)
                return false;
            if (instruction->is_call() &&
                callMayAccessMatrix(static_cast<CallInst *>(instruction),
                                    matrixRoot, BAA))
                return false;
        }
        for (auto *successor : block->succ_bbs_)
            worklist.push(successor);
    }
    return true;
}

struct ConsumerPattern {
    Loop *loop = nullptr;
    LoadInst *load = nullptr;
    BasicBlock *bodyEntry = nullptr;
};

bool matchConsumer(Loop *loop, Value *length, Value *matrixRoot,
                   BasicBlock *fastOrigin, const LoopInfo &LI,
                   const BasicAliasAnalysis &BAA,
                   ConsumerPattern &consumer) {
    if (!loop || !loop->children.empty() || !loop->canonicalIV ||
        loop->tripCount != length || !loop->singleLatch() ||
        !loop->singleExit() || !simpleConsumerPreheader(loop))
        return false;

    auto *branch = dynamic_cast<BranchInst *>(loop->header->get_terminator());
    auto *compare = branch && branch->num_ops_ == 3
                        ? dynamic_cast<ICmpInst *>(branch->get_operand(0))
                        : nullptr;
    if (!compare || compare->icmp_op_ != ICmpInst::ICMP_SLT ||
        compare->get_operand(0) != loop->canonicalIV ||
        compare->get_operand(1) != length)
        return false;
    auto *bodyEntry = dynamic_cast<BasicBlock *>(branch->get_operand(1));
    auto *exit = dynamic_cast<BasicBlock *>(branch->get_operand(2));
    if (!bodyEntry || !loop->isInLoop(bodyEntry) || exit != loop->singleExit() ||
        hasPhi(bodyEntry))
        return false;

    LoadInst *matrixLoad = nullptr;
    for (auto *block : loop->blocksOrdered) {
        for (auto *instruction : block->instr_list_) {
            if (instruction->is_call() || instruction->is_store())
                return false;
            if (!instruction->is_load())
                continue;
            if (BAA.getModRefInfo(instruction, matrixRoot) ==
                ModRefInfo::NoModRef)
                continue;
            if (matrixLoad)
                return false;
            ElementAccess access;
            if (!parseElementAccess(instruction->get_operand(0), access) ||
                pointerRoot(access.base) != matrixRoot ||
                access.index != loop->canonicalIV)
                return false;
            matrixLoad = static_cast<LoadInst *>(instruction);
        }
    }
    if (!matrixLoad || matrixLoad->parent_ == loop->header ||
        !LI.dominates(bodyEntry, matrixLoad->parent_))
        return false;

    for (auto *instruction : loop->header->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(instruction);
        if (!phi)
            break;
        Value *initial = nullptr;
        for (unsigned index = 0; index + 1 < phi->num_ops_; index += 2) {
            if (phi->get_operand(index + 1) == loop->preheader) {
                initial = phi->get_operand(index);
                break;
            }
        }
        if (!initial || !valueAvailableAt(initial, fastOrigin, LI))
            return false;
    }

    consumer = ConsumerPattern{};
    consumer.loop = loop;
    consumer.load = matrixLoad;
    consumer.bodyEntry = bodyEntry;
    return true;
}

struct Pattern {
    Loop *sequence = nullptr;
    RemapPattern remap;
    ConsumerPattern consumer;
    Value *length = nullptr;
    Value *extent = nullptr;
    Value *rowsizeBase = nullptr;
    Value *rowsizeRoot = nullptr;
    bool rowsizeFlat = true;
    int rowsizeExtent = -1;
    int matrixExtent = -1;
};

bool matchPattern(Function *function, LoopInfo &LI,
                  const BasicAliasAnalysis &BAA, Pattern &pattern) {
    Module *module = function->parent_;
    for (Loop *top : LI.topLevelLoops()) {
        if (!top || top->children.size() != 1 || !top->canonicalIV ||
            !top->preheader || !top->singleLatch() || !top->singleExit() ||
            hasLiveOutValue(top))
            continue;

        RemapPattern remap;
        if (!matchRemap(top->children.front(), module, remap) ||
            remap.outer->parent != top || !loopBodyHasOnlyRowsizeRead(top, remap))
            continue;

        Value *rowsizeBase = nullptr;
        Value *rowsizeRoot = nullptr;
        bool rowsizeFlat = true;
        if (!matchRowsizeLoad(remap.rowsize, top->canonicalIV, &rowsizeBase,
                              &rowsizeRoot, &rowsizeFlat))
            continue;
        Value *extent = nullptr;
        if (!matchExtentDivision(remap.colsize, remap.rowsize, &extent) ||
            !isI32(extent, module) || !isI32(top->tripCount, module) ||
            !valueAvailableAt(extent, top->preheader, LI) ||
            !valueAvailableAt(top->tripCount, top->preheader, LI) ||
            !valueAvailableAt(remap.matrixBase, top->preheader, LI))
            continue;

        auto *matrixGlobal = dynamic_cast<GlobalVariable *>(remap.matrixRoot);
        auto *rowsizeGlobal = dynamic_cast<GlobalVariable *>(rowsizeRoot);
        if (!matrixGlobal || !rowsizeGlobal || matrixGlobal == rowsizeGlobal)
            continue;
        int matrixExtent = globalArrayExtent(matrixGlobal, module);
        int rowsizeExtent = globalArrayExtent(rowsizeGlobal, module);
        if (matrixExtent < 0 || rowsizeExtent < 0)
            continue;

        BasicBlock *consumerPreheader = top->singleExit();
        ConsumerPattern matchedConsumer;
        Loop *consumerLoop = nullptr;
        for (Loop *candidate : LI.topLevelLoops()) {
            if (candidate == top || candidate->preheader != consumerPreheader)
                continue;
            if (!matchConsumer(candidate, top->tripCount, remap.matrixRoot,
                               top->preheader, LI, BAA, matchedConsumer))
                continue;
            consumerLoop = candidate;
            break;
        }
        if (!consumerLoop ||
            !matrixUnobservedAfter(consumerLoop->singleExit(),
                                   remap.matrixRoot, BAA))
            continue;

        pattern = Pattern{};
        pattern.sequence = top;
        pattern.remap = remap;
        pattern.consumer = std::move(matchedConsumer);
        pattern.length = top->tripCount;
        pattern.extent = extent;
        pattern.rowsizeBase = rowsizeBase;
        pattern.rowsizeRoot = rowsizeRoot;
        pattern.rowsizeFlat = rowsizeFlat;
        pattern.rowsizeExtent = rowsizeExtent;
        pattern.matrixExtent = matrixExtent;
        return true;
    }
    return false;
}

GetElementPtrInst *elementGEP(Value *base, Value *index, bool flat,
                              BasicBlock *block) {
    if (flat)
        return new GetElementPtrInst(base, {index}, block);
    auto *zero = new ConstantInt(block->parent_->parent_->int32_ty_, 0);
    return new GetElementPtrInst(base, {zero, index}, block);
}

struct GuardBlocks {
    BasicBlock *ready = nullptr;
};

GuardBlocks buildGuard(Pattern &pattern, Function *function,
                       BasicBlock *fastTarget, int &counter) {
    Module *module = function->parent_;
    Type *i32 = module->int32_ty_;
    Type *i1 = module->int1_ty_;
    auto *zero = new ConstantInt(i32, 0);
    auto *one = new ConstantInt(i32, 1);
    auto block = [&](const char *suffix) {
        return new BasicBlock(module,
                              "remap.guard." + std::string(suffix) + "." +
                                  std::to_string(counter),
                              function);
    };
    BasicBlock *entry = block("entry");
    BasicBlock *loopPreheader = block("preheader");
    BasicBlock *header = block("header");
    BasicBlock *body = block("body");
    BasicBlock *latch = block("latch");
    BasicBlock *fallback = block("fallback");
    BasicBlock *ready = block("ready");
    ++counter;

    auto *rowsizeBound =
        new ConstantInt(i32, pattern.rowsizeExtent);
    auto *matrixBound = new ConstantInt(i32, pattern.matrixExtent);
    auto *lengthNonNegative =
        new ICmpInst(ICmpInst::ICMP_SGE, pattern.length, zero, entry);
    auto *lengthFitsRows =
        new ICmpInst(ICmpInst::ICMP_SLE, pattern.length, rowsizeBound, entry);
    auto *lengthFitsMatrix =
        new ICmpInst(ICmpInst::ICMP_SLE, pattern.length, matrixBound, entry);
    auto *extentNonNegative =
        new ICmpInst(ICmpInst::ICMP_SGE, pattern.extent, zero, entry);
    auto *extentFitsMatrix =
        new ICmpInst(ICmpInst::ICMP_SLE, pattern.extent, matrixBound, entry);
    Value *safe = new BinaryInst(i1, Instruction::And, lengthNonNegative,
                                 lengthFitsRows, entry);
    safe = new BinaryInst(i1, Instruction::And, safe, lengthFitsMatrix, entry);
    safe = new BinaryInst(i1, Instruction::And, safe, extentNonNegative, entry);
    safe = new BinaryInst(i1, Instruction::And, safe, extentFitsMatrix, entry);
    new BranchInst(safe, loopPreheader, fallback, entry);
    new BranchInst(header, loopPreheader);

    auto *index = PhiInst::create_phi(i32, header);
    index->add_phi_pair_operand(zero, loopPreheader);
    header->add_instruction_front(index);
    auto *more = new ICmpInst(ICmpInst::ICMP_SLT, index, pattern.length, header);
    new BranchInst(more, body, ready, header);

    auto *rowsizePointer = elementGEP(pattern.rowsizeBase, index,
                                      pattern.rowsizeFlat, body);
    auto *rowsize = new LoadInst(rowsizePointer, body);
    auto *positive = new ICmpInst(ICmpInst::ICMP_SGT, rowsize, zero, body);
    new BranchInst(positive, latch, fallback, body);

    auto *next = new BinaryInst(i32, Instruction::Add, index, one, latch);
    index->add_phi_pair_operand(next, latch);
    new BranchInst(header, latch);

    new BranchInst(pattern.sequence->header, fallback);
    new BranchInst(fastTarget, ready);

    BasicBlock *originalPreheader = pattern.sequence->preheader;
    removeTerminatorAndEdges(originalPreheader);
    retargetPhiPredecessor(pattern.sequence->header, originalPreheader,
                           fallback);
    new BranchInst(entry, originalPreheader);
    return GuardBlocks{ready};
}

struct TraceBlocks {
    BasicBlock *entry = nullptr;
    BasicBlock *done = nullptr;
    Value *sourcePointer = nullptr;
};

TraceBlocks buildTrace(Pattern &pattern, Function *function,
                       BasicBlock *merge, int &counter) {
    Module *module = function->parent_;
    Type *i32 = module->int32_ty_;
    auto *zero = new ConstantInt(i32, 0);
    auto *one = new ConstantInt(i32, 1);

    auto block = [&](const char *suffix) {
        return new BasicBlock(module,
                              "remap.trace." + std::string(suffix) + "." +
                                  std::to_string(counter),
                              function);
    };
    BasicBlock *entry = block("entry");
    BasicBlock *remapHeader = block("remap.header");
    BasicBlock *remapBody = block("remap.body");
    BasicBlock *positiveColumns = block("columns");
    BasicBlock *chainHeader = block("chain.header");
    BasicBlock *chainRange = block("chain.range");
    BasicBlock *chainCoordinates = block("chain.coordinates");
    BasicBlock *chainTriangle = block("chain.triangle");
    BasicBlock *chainOuterOrder = block("chain.outer-order");
    BasicBlock *chainSameOuter = block("chain.same-outer");
    BasicBlock *chainInnerOrder = block("chain.inner-order");
    BasicBlock *chainStep = block("chain.step");
    BasicBlock *chainDone = block("chain.done");
    BasicBlock *noRemap = block("no-remap");
    BasicBlock *remapLatch = block("remap.latch");
    BasicBlock *done = block("done");
    ++counter;

    new BranchInst(remapHeader, entry);
    auto *iteration = PhiInst::create_phi(i32, remapHeader);
    auto *source = PhiInst::create_phi(i32, remapHeader);
    iteration->add_phi_pair_operand(zero, entry);
    source->add_phi_pair_operand(pattern.consumer.loop->canonicalIV, entry);
    remapHeader->add_instruction_front(source);
    remapHeader->add_instruction_front(iteration);
    auto *hasRemap = new ICmpInst(ICmpInst::ICMP_SLT, iteration,
                                  pattern.length, remapHeader);
    new BranchInst(hasRemap, remapBody, done, remapHeader);

    auto *last = new BinaryInst(i32, Instruction::Sub, pattern.length, one,
                                remapBody);
    auto *reverseIndex = new BinaryInst(i32, Instruction::Sub, last, iteration,
                                        remapBody);
    auto *rowsizePointer = elementGEP(pattern.rowsizeBase, reverseIndex,
                                      pattern.rowsizeFlat, remapBody);
    auto *rowsize = new LoadInst(rowsizePointer, remapBody);
    auto *colsize = new BinaryInst(i32, Instruction::SDiv, pattern.extent,
                                   rowsize, remapBody);
    auto *hasColumns = new ICmpInst(ICmpInst::ICMP_SGT, colsize, zero,
                                    remapBody);
    new BranchInst(hasColumns, positiveColumns, noRemap, remapBody);

    auto *covered = new BinaryInst(i32, Instruction::Mul, rowsize, colsize,
                                   positiveColumns);
    new BranchInst(chainHeader, positiveColumns);

    auto *position = PhiInst::create_phi(i32, chainHeader);
    auto *previousOuter = PhiInst::create_phi(i32, chainHeader);
    auto *previousInner = PhiInst::create_phi(i32, chainHeader);
    position->add_phi_pair_operand(source, positiveColumns);
    previousOuter->add_phi_pair_operand(colsize, positiveColumns);
    previousInner->add_phi_pair_operand(zero, positiveColumns);
    chainHeader->add_instruction_front(previousInner);
    chainHeader->add_instruction_front(previousOuter);
    chainHeader->add_instruction_front(position);
    new BranchInst(chainRange, chainHeader);

    auto *outside = new ICmpInst(ICmpInst::ICMP_SGE, position, covered,
                                 chainRange);
    new BranchInst(outside, chainDone, chainCoordinates, chainRange);

    auto *outer = new BinaryInst(i32, Instruction::SRem, position, colsize,
                                 chainCoordinates);
    auto *inner = new BinaryInst(i32, Instruction::SDiv, position, colsize,
                                 chainCoordinates);
    new BranchInst(chainTriangle, chainCoordinates);

    auto *outsideTriangle = new ICmpInst(ICmpInst::ICMP_SGT, inner, outer,
                                         chainTriangle);
    new BranchInst(outsideTriangle, chainDone, chainOuterOrder, chainTriangle);

    auto *tooLateOuter = new ICmpInst(ICmpInst::ICMP_SGT, outer,
                                      previousOuter, chainOuterOrder);
    new BranchInst(tooLateOuter, chainDone, chainSameOuter, chainOuterOrder);

    auto *sameOuter = new ICmpInst(ICmpInst::ICMP_EQ, outer, previousOuter,
                                   chainSameOuter);
    new BranchInst(sameOuter, chainInnerOrder, chainStep, chainSameOuter);

    auto *tooLateInner = new ICmpInst(ICmpInst::ICMP_SGE, inner,
                                      previousInner, chainInnerOrder);
    new BranchInst(tooLateInner, chainDone, chainStep, chainInnerOrder);

    auto *nextPosition = new BinaryInst(i32, Instruction::Mul, outer, rowsize,
                                        chainStep);
    nextPosition = new BinaryInst(i32, Instruction::Add, nextPosition, inner,
                                  chainStep);
    position->add_phi_pair_operand(nextPosition, chainStep);
    previousOuter->add_phi_pair_operand(outer, chainStep);
    previousInner->add_phi_pair_operand(inner, chainStep);
    new BranchInst(chainHeader, chainStep);

    new BranchInst(remapLatch, chainDone);
    new BranchInst(remapLatch, noRemap);
    auto *nextSource = PhiInst::create_phi(i32, remapLatch);
    nextSource->add_phi_pair_operand(position, chainDone);
    nextSource->add_phi_pair_operand(source, noRemap);
    remapLatch->add_instruction_front(nextSource);
    auto *nextIteration = new BinaryInst(i32, Instruction::Add, iteration, one,
                                         remapLatch);
    iteration->add_phi_pair_operand(nextIteration, remapLatch);
    source->add_phi_pair_operand(nextSource, remapLatch);
    new BranchInst(remapHeader, remapLatch);

    auto *sourcePointer = elementGEP(pattern.remap.matrixBase, source,
                                     pattern.remap.matrixFlat, done);
    new BranchInst(merge, done);
    return TraceBlocks{entry, done, sourcePointer};
}

void versionConsumer(Pattern &pattern, GuardBlocks guard,
                     BasicBlock *consumerEntry, Function *function,
                     int &counter) {
    Module *module = function->parent_;
    Type *i1 = module->int1_ty_;
    auto *falseValue = new ConstantInt(i1, 0);
    auto *trueValue = new ConstantInt(i1, 1);

    std::string suffix = std::to_string(counter++);
    auto *dispatch = new BasicBlock(module, "remap.consumer.dispatch." + suffix,
                                    function);
    auto *slow = new BasicBlock(module, "remap.consumer.slow." + suffix,
                                function);
    auto *merge = new BasicBlock(module, "remap.consumer.merge." + suffix,
                                 function);

    redirectEdge(pattern.consumer.loop->preheader,
                 pattern.consumer.loop->header, consumerEntry);
    retargetPhiPredecessor(pattern.consumer.loop->header,
                           pattern.consumer.loop->preheader, consumerEntry);
    auto *entryMode = PhiInst::create_phi(i1, consumerEntry);
    entryMode->add_phi_pair_operand(falseValue,
                                    pattern.consumer.loop->preheader);
    entryMode->add_phi_pair_operand(trueValue, guard.ready);
    consumerEntry->add_instruction_front(entryMode);
    new BranchInst(pattern.consumer.loop->header, consumerEntry);

    auto *fastMode = PhiInst::create_phi(i1, pattern.consumer.loop->header);
    fastMode->add_phi_pair_operand(entryMode, consumerEntry);
    fastMode->add_phi_pair_operand(fastMode,
                                   pattern.consumer.loop->singleLatch());
    pattern.consumer.loop->header->add_instruction_front(fastMode);

    TraceBlocks trace = buildTrace(pattern, function, merge, counter);
    auto *slowPointer = elementGEP(pattern.remap.matrixBase,
                                   pattern.consumer.loop->canonicalIV,
                                   pattern.remap.matrixFlat, slow);
    new BranchInst(merge, slow);

    auto *pointer = PhiInst::create_phi(slowPointer->type_, merge);
    pointer->add_phi_pair_operand(slowPointer, slow);
    pointer->add_phi_pair_operand(trace.sourcePointer, trace.done);
    merge->add_instruction_front(pointer);
    new BranchInst(pattern.consumer.bodyEntry, merge);

    new BranchInst(fastMode, trace.entry, slow, dispatch);
    redirectEdge(pattern.consumer.loop->header, pattern.consumer.bodyEntry,
                 dispatch);
    pattern.consumer.load->set_operand(0, pointer);
}

bool applyTransform(Function *function, Pattern &pattern) {
    auto *preheaderBranch = dynamic_cast<BranchInst *>(
        pattern.sequence->preheader->get_terminator());
    if (!preheaderBranch || preheaderBranch->num_ops_ != 1 ||
        preheaderBranch->get_operand(0) != pattern.sequence->header)
        return false;

    int counter = 0;
    auto *consumerEntry = new BasicBlock(
        function->parent_, "remap.consumer.entry." + std::to_string(counter++),
        function);
    GuardBlocks guard = buildGuard(pattern, function, consumerEntry, counter);
    versionConsumer(pattern, guard, consumerEntry, function, counter);
    function->set_instr_name();
    if (debugEnabled())
        std::cerr << "[TriangularRemapSourceCompose] versioned source tracing in "
                  << function->name_ << "\n";
    return true;
}

} // namespace

void TriangularRemapSourceCompose::execute(Module *module) {
    for (auto *function : module->function_list_) {
        if (!function->is_declaration())
            runOnFunction(function);
    }
}

PreservedAnalyses
TriangularRemapSourceCompose::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *function : module->function_list_) {
        if (!function->is_declaration())
            changed |= runOnFunction(function);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool TriangularRemapSourceCompose::runOnFunction(Function *function) {
    if (!function || function->basic_blocks_.empty())
        return false;

    LoopInfo loopInfo;
    loopInfo.analyze(function);
    BasicAliasAnalysis aliasAnalysis;
    aliasAnalysis.analyze(function->parent_);
    Pattern pattern;
    if (!matchPattern(function, loopInfo, aliasAnalysis, pattern))
        return false;
    return applyTransform(function, pattern);
}
