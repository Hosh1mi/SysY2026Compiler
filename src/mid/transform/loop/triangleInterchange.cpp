// TriangleInterchange recognizes the two-level triangular loop used by a
// wavefront dynamic program.  For a domain such as
//
//     0 <= distance < row <= extent
//
// it replaces the original descending-row / increasing-distance traversal
// with an increasing-distance outer loop and a lane loop over the independent
// cells in that wave.  The cell body is cloned into the new CFG, while the
// dependence check proves that every table load reads the current wave or an
// earlier one.  This exposes the lane loop to later parallelization/vector
// passes without changing the order of dependent waves.  Patterns that do
// not have the required affine indices, aliasing facts, or loop shape are
// left unchanged.

#include "../../../include/mid/opt/triangleInterchange.hpp"

#include "../../../include/mid/analysis/affineAnalysis.hpp"
#include "../../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../../include/mid/opt/cfgUtils.hpp"
#include "../../../include/mid/ir/basicBlock.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/globalVariable.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/module.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

using ValueMap = std::unordered_map<Value *, Value *>;

class ScheduleAffineAnalyzer {
public:
    AffineExpr analyze(Value *value) {
        auto found = cache_.find(value);
        if (found != cache_.end()) return found->second;
        if (!value || !visiting_.insert(value).second)
            return AffineExpr::makeInvalid();

        AffineExpr result = AffineExpr::makeInvalid();
        if (auto *constant = dynamic_cast<ConstantInt *>(value)) {
            result = AffineExpr::makeConstant(constant->value_);
        } else if (auto *phi = dynamic_cast<PhiInst *>(value)) {
            result = AffineExpr::makeIV(phi);
        } else if (auto *binary = dynamic_cast<BinaryInst *>(value)) {
            AffineExpr lhs = analyze(binary->get_operand(0));
            AffineExpr rhs = analyze(binary->get_operand(1));
            if (binary->is_add())
                result = lhs + rhs;
            else if (binary->is_sub())
                result = lhs - rhs;
            else if (binary->is_mul()) {
                if (auto *constant = dynamic_cast<ConstantInt *>(
                        binary->get_operand(0)))
                    result = rhs * constant->value_;
                else if (auto *constant = dynamic_cast<ConstantInt *>(
                             binary->get_operand(1)))
                    result = lhs * constant->value_;
            }
        }
        visiting_.erase(value);
        cache_[value] = result;
        return result;
    }

private:
    std::map<Value *, AffineExpr> cache_;
    std::set<Value *> visiting_;
};

bool debugEnabled() {
    return std::getenv("DEBUG_TRIANGLE_INTERCHANGE") != nullptr;
}

void debugReject(Function *function, Loop *loop, const std::string &reason) {
    if (!debugEnabled()) return;
    std::cerr << "[TriangleInterchange] reject func=" << function->name_
              << " loop="
              << (loop && loop->header ? loop->header->name_ : "?")
              << ": " << reason << "\n";
}

bool isI32(Value *value) {
    auto *type = value ? dynamic_cast<IntegerType *>(value->type_) : nullptr;
    return type && type->num_bits_ == 32;
}

Value *rootBase(Value *pointer) {
    Value *value = pointer;
    while (true) {
        if (auto *gep = dynamic_cast<GetElementPtrInst *>(value)) {
            value = gep->get_operand(0);
            continue;
        }
        if (auto *bitcast = dynamic_cast<Bitcast *>(value)) {
            value = bitcast->get_operand(0);
            continue;
        }
        return value;
    }
}

bool sameAffine(const AffineExpr &lhs, const AffineExpr &rhs) {
    return lhs.valid && rhs.valid && (lhs - rhs).isZero();
}

bool isDescendantOf(Loop *candidate, Loop *ancestor) {
    for (Loop *loop = candidate; loop; loop = loop->parent)
        if (loop == ancestor) return true;
    return false;
}

Value *incomingValue(PhiInst *phi, BasicBlock *predecessor) {
    if (!phi || !predecessor) return nullptr;
    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2)
        if (phi->get_operand(i + 1) == predecessor)
            return phi->get_operand(i);
    return nullptr;
}

bool matchesSubOne(Value *value, Value *lhs) {
    auto *sub = dynamic_cast<BinaryInst *>(value);
    auto *one = sub ? dynamic_cast<ConstantInt *>(sub->get_operand(1)) : nullptr;
    return sub && sub->is_sub() && sub->get_operand(0) == lhs && one &&
           one->value_ == 1;
}

bool preheaderProvesPositiveBound(const TriangleSchedulePlan &plan) {
    if (!plan.outerPreheader || plan.outerPreheader->pre_bbs_.size() != 1)
        return false;
    BasicBlock *guardBlock = plan.outerPreheader->pre_bbs_.front();
    auto *branch = dynamic_cast<BranchInst *>(guardBlock->get_terminator());
    if (!branch || branch->num_ops_ != 3 ||
        branch->get_operand(1) != plan.outerPreheader)
        return false;
    auto *compare = dynamic_cast<ICmpInst *>(branch->get_operand(0));
    auto *one = compare
                    ? dynamic_cast<ConstantInt *>(compare->get_operand(1))
                    : nullptr;
    return compare && compare->icmp_op_ == ICmpInst::ICMP_SGE &&
           compare->get_operand(0) == plan.innerBound && one &&
           one->value_ == 1;
}

bool matchDescendingOuter(const TriangleSchedulePlan &plan,
                          std::string &reason) {
    Loop *outer = plan.outer;
    BasicBlock *latch = outer ? outer->singleLatch() : nullptr;
    if (!outer || !latch || !plan.outerIV) {
        reason = "outer loop lacks a single descending latch";
        return false;
    }

    Value *initial = incomingValue(plan.outerIV, outer->preheader);
    Value *backedge = incomingValue(plan.outerIV, latch);
    auto *update = dynamic_cast<BinaryInst *>(backedge);
    auto *one = update ? dynamic_cast<ConstantInt *>(update->get_operand(1))
                       : nullptr;
    if (!initial || !update || !update->is_sub() ||
        update->get_operand(0) != plan.outerIV || !one || one->value_ != 1) {
        reason = "outer induction is not a unit descending recurrence";
        return false;
    }
    if (initial != plan.extent) {
        reason = "outer initial value is not the triangular extent";
        return false;
    }

    auto *branch = dynamic_cast<BranchInst *>(latch->get_terminator());
    auto *compare = branch && branch->num_ops_ == 3
                        ? dynamic_cast<ICmpInst *>(branch->get_operand(0))
                        : nullptr;
    auto *lowerGuard = compare
                           ? dynamic_cast<ConstantInt *>(compare->get_operand(1))
                           : nullptr;
    if (!compare || compare->icmp_op_ != ICmpInst::ICMP_SGE ||
        compare->get_operand(0) != plan.outerIV || !lowerGuard ||
        lowerGuard->value_ != 1 ||
        !outer->blocks.count(
            dynamic_cast<BasicBlock *>(branch->get_operand(1))) ||
        outer->blocks.count(
            dynamic_cast<BasicBlock *>(branch->get_operand(2)))) {
        reason = "outer latch does not prove a zero lower bound";
        return false;
    }
    return true;
}

std::optional<TriangleSchedulePlan>
matchTriangle(Loop &inner, std::string &reason) {
    if (!inner.parent || !inner.preheader || !inner.singleLatch() ||
        !inner.parent->preheader || !inner.parent->singleExit()) {
        reason = "incomplete two-level loop structure";
        return std::nullopt;
    }

    const InductionDescriptor *control = inner.getInductionDescriptor();
    if (!control || !control->constantStep || *control->constantStep != 1 ||
        control->predicate != ICmpInst::ICMP_SLT ||
        control->guardPosition != InductionGuardPosition::Latch ||
        !control->comparesUpdate || !isI32(control->phi)) {
        reason = "inner loop is not the skewed +1 distance loop";
        return std::nullopt;
    }
    auto *zero = dynamic_cast<ConstantInt *>(control->start);
    auto *distanceBound = dynamic_cast<BinaryInst *>(control->bound);
    if (!zero || zero->value_ != 0 || !distanceBound ||
        !distanceBound->is_sub()) {
        reason = "inner distance bound is not bound-start";
        return std::nullopt;
    }

    Value *innerBound = distanceBound->get_operand(0);
    Value *innerStart = distanceBound->get_operand(1);
    auto *startAdd = dynamic_cast<BinaryInst *>(innerStart);
    if (!startAdd || !startAdd->is_add()) {
        reason = "inner start is not outer+1";
        return std::nullopt;
    }
    PhiInst *outerIV = dynamic_cast<PhiInst *>(startAdd->get_operand(0));
    auto *offset = dynamic_cast<ConstantInt *>(startAdd->get_operand(1));
    if (!outerIV || !offset) {
        outerIV = dynamic_cast<PhiInst *>(startAdd->get_operand(1));
        offset = dynamic_cast<ConstantInt *>(startAdd->get_operand(0));
    }
    if (!outerIV || !offset || offset->value_ != 1 ||
        outerIV->parent_ != inner.parent->header) {
        reason = "requires inner start outer+1";
        return std::nullopt;
    }

    Value *bodyIndex = nullptr;
    for (auto *instruction : inner.header->instr_list_) {
        auto *add = dynamic_cast<BinaryInst *>(instruction);
        if (!add || !add->is_add()) continue;
        if ((add->get_operand(0) == innerStart &&
             add->get_operand(1) == control->phi) ||
            (add->get_operand(1) == innerStart &&
             add->get_operand(0) == control->phi)) {
            bodyIndex = add;
            break;
        }
    }
    if (!bodyIndex) {
        reason = "missing reconstructed inner index";
        return std::nullopt;
    }

    Value *outerInitial = incomingValue(outerIV, inner.parent->preheader);
    if (!matchesSubOne(outerInitial, innerBound)) {
        reason = "outer extent is not inner-bound-1";
        return std::nullopt;
    }

    TriangleSchedulePlan plan;
    plan.outer = inner.parent;
    plan.inner = &inner;
    plan.outerIV = outerIV;
    plan.distanceIV = control->phi;
    plan.extent = outerInitial;
    plan.innerStart = innerStart;
    plan.innerBound = innerBound;
    plan.bodyIndex = bodyIndex;
    plan.outerPreheader = inner.parent->preheader;
    plan.outerExit = inner.parent->singleExit();
    plan.innerPreheader = inner.preheader;
    plan.innerLatch = inner.singleLatch();
    plan.offset = 1;
    if (!matchDescendingOuter(plan, reason)) return std::nullopt;
    if (!preheaderProvesPositiveBound(plan)) {
        reason = "missing dominating inner-bound>=1 guard";
        return std::nullopt;
    }
    return plan;
}

bool proveEarlierWave(const AffineExpr &delta, const TriangleSchedulePlan &plan,
                      const LoopInfo &loopInfo,
                      ScheduleAffineAnalyzer &affine) {
    if (!delta.valid) return false;
    if (delta.isConstant()) return delta.constant >= 1;
    for (const auto &[nestedIV, coefficient] : delta.coeffs) {
        (void)coefficient;
        Loop *nested = loopInfo.getLoopFor(nestedIV->parent_);
        if (!nested || !isDescendantOf(nested, plan.inner) ||
            nested == plan.inner)
            continue;
        const InductionDescriptor *control = nested->getInductionDescriptor();
        if (!control || control->phi != nestedIV || !control->constantStep ||
            *control->constantStep != 1 ||
            control->predicate != ICmpInst::ICMP_SLT)
            continue;

        AffineExpr iv = AffineExpr::makeIV(nestedIV);
        AffineExpr upper = affine.analyze(control->bound);
        AffineExpr lower = affine.analyze(control->start);
        if (!upper.valid || !lower.valid) continue;

        AffineExpr fromUpper = delta - (upper - iv);
        if (fromUpper.isConstant() && fromUpper.constant >= 0)
            return true;
        AffineExpr fromLower = delta - (iv - lower);
        if (fromLower.isConstant() && fromLower.constant >= 1)
            return true;
    }
    return false;
}

bool proveWavefrontDependences(const TriangleSchedulePlan &plan,
                               const LoopInfo &loopInfo,
                               ScheduleAffineAnalyzer &affine,
                               const ArgumentAliasAnalysis &argAlias,
                               std::string &reason) {
    Value *writeRoot = nullptr;
    AffineExpr outer = AffineExpr::makeIV(plan.outerIV);
    AffineExpr distance = AffineExpr::makeIV(plan.distanceIV);
    AffineExpr bodyIndex = affine.analyze(plan.bodyIndex);
    if (!bodyIndex.valid) {
        reason = "reconstructed cell index is not affine";
        return false;
    }

    std::vector<Instruction *> loads;
    bool sawStore = false;
    for (auto *block : plan.inner->blocksOrdered) {
        for (auto *instruction : block->instr_list_) {
            if (instruction->is_call() || dynamic_cast<AllocaInst *>(instruction)) {
                reason = "call or alloca in cell region";
                return false;
            }
            if (instruction->is_load()) loads.push_back(instruction);
            if (!instruction->is_store()) continue;
            sawStore = true;
            auto *gep = dynamic_cast<GetElementPtrInst *>(instruction->get_operand(1));
            if (!gep || gep->num_ops_ < 3) {
                reason = "cell store is not a two-dimensional GEP";
                return false;
            }
            Value *root = rootBase(gep);
            if (!writeRoot) writeRoot = root;
            if (root != writeRoot) {
                reason = "multiple writable roots in triangular cell";
                return false;
            }
            AffineExpr row = affine.analyze(gep->get_operand(gep->num_ops_ - 2));
            AffineExpr column = affine.analyze(gep->get_operand(gep->num_ops_ - 1));
            if (!sameAffine(row, outer) || !sameAffine(column, bodyIndex)) {
                reason = "store is not injective in the current cell coordinate";
                return false;
            }
        }
    }
    if (!sawStore || !writeRoot) {
        reason = "triangular cell has no DP store";
        return false;
    }

    for (auto *load : loads) {
        auto *gep = dynamic_cast<GetElementPtrInst *>(load->get_operand(0));
        if (!gep) {
            reason = "cell load is not a GEP";
            return false;
        }
        Value *root = rootBase(gep);
        if (root != writeRoot) {
            if (!argAlias.noAlias(root, writeRoot)) {
                reason = "read-only root may alias the DP table";
                return false;
            }
            continue;
        }
        if (gep->num_ops_ < 3) {
            reason = "DP load is not a two-dimensional GEP";
            return false;
        }

        AffineExpr row = affine.analyze(gep->get_operand(gep->num_ops_ - 2));
        AffineExpr column = affine.analyze(gep->get_operand(gep->num_ops_ - 1));
        if (!row.valid || !column.valid) {
            reason = "DP load address is not affine";
            return false;
        }
        if (sameAffine(row, outer) && sameAffine(column, bodyIndex))
            continue;

        AffineExpr sourceWave = column - row -
                                AffineExpr::makeConstant(plan.offset);
        AffineExpr delta = distance - sourceWave;
        if (!proveEarlierWave(delta, plan, loopInfo, affine)) {
            reason = "load is not proven to read the current or an earlier wave: row=" +
                     row.print() + " column=" + column.print() +
                     " delta=" + delta.print();
            return false;
        }
    }
    return true;
}

bool isCloneableInstruction(Instruction *instruction) {
    return dynamic_cast<BinaryInst *>(instruction) ||
           dynamic_cast<UnaryInst *>(instruction) ||
           dynamic_cast<ICmpInst *>(instruction) ||
           dynamic_cast<FCmpInst *>(instruction) ||
           dynamic_cast<GetElementPtrInst *>(instruction) ||
           dynamic_cast<LoadInst *>(instruction) ||
           dynamic_cast<StoreInst *>(instruction) ||
           dynamic_cast<ZextInst *>(instruction) ||
           dynamic_cast<FpToSiInst *>(instruction) ||
           dynamic_cast<SiToFpInst *>(instruction) ||
           dynamic_cast<Bitcast *>(instruction) ||
           dynamic_cast<SelectInst *>(instruction);
}

Value *remap(Value *value, const ValueMap &map) {
    auto found = map.find(value);
    return found == map.end() ? value : found->second;
}

Instruction *cloneInstruction(Instruction *original, BasicBlock *destination,
                              const ValueMap &map) {
    auto R = [&](Value *value) { return remap(value, map); };
    Instruction *clone = nullptr;
    if (auto *binary = dynamic_cast<BinaryInst *>(original)) {
        clone = new BinaryInst(binary->type_, binary->op_id_,
                               R(binary->get_operand(0)),
                               R(binary->get_operand(1)), destination);
    } else if (auto *unary = dynamic_cast<UnaryInst *>(original)) {
        clone = new UnaryInst(unary->type_, unary->op_id_,
                              R(unary->get_operand(0)), destination);
    } else if (auto *compare = dynamic_cast<ICmpInst *>(original)) {
        clone = new ICmpInst(compare->icmp_op_, R(compare->get_operand(0)),
                             R(compare->get_operand(1)), destination);
    } else if (auto *compare = dynamic_cast<FCmpInst *>(original)) {
        clone = new FCmpInst(compare->fcmp_op_, R(compare->get_operand(0)),
                             R(compare->get_operand(1)), destination);
    } else if (auto *gep = dynamic_cast<GetElementPtrInst *>(original)) {
        std::vector<Value *> indices;
        for (unsigned i = 1; i < gep->num_ops_; ++i)
            indices.push_back(R(gep->get_operand(i)));
        clone = new GetElementPtrInst(R(gep->get_operand(0)), indices,
                                      destination);
    } else if (auto *load = dynamic_cast<LoadInst *>(original)) {
        clone = new LoadInst(R(load->get_operand(0)), destination);
    } else if (auto *store = dynamic_cast<StoreInst *>(original)) {
        clone = new StoreInst(R(store->get_operand(0)),
                              R(store->get_operand(1)), destination);
    } else if (auto *zext = dynamic_cast<ZextInst *>(original)) {
        clone = new ZextInst(zext->op_id_, R(zext->get_operand(0)),
                             zext->dest_ty_, destination);
    } else if (auto *cast = dynamic_cast<FpToSiInst *>(original)) {
        clone = new FpToSiInst(cast->op_id_, R(cast->get_operand(0)),
                               cast->dest_ty_, destination);
    } else if (auto *cast = dynamic_cast<SiToFpInst *>(original)) {
        clone = new SiToFpInst(cast->op_id_, R(cast->get_operand(0)),
                               cast->dest_ty_, destination);
    } else if (auto *cast = dynamic_cast<Bitcast *>(original)) {
        clone = new Bitcast(cast->op_id_, R(cast->get_operand(0)),
                            cast->dest_ty_, destination);
    } else if (auto *select = dynamic_cast<SelectInst *>(original)) {
        clone = new SelectInst(R(select->get_operand(0)),
                               R(select->get_operand(1)),
                               R(select->get_operand(2)), destination);
    }
    if (clone) clone->copySemFlagsFrom(original);
    return clone;
}

bool validateCloneRegion(const TriangleSchedulePlan &plan,
                         const std::set<BasicBlock *> &cellBlocks,
                         std::string &reason) {
    for (auto *block : cellBlocks) {
        auto *branch = dynamic_cast<BranchInst *>(block->get_terminator());
        if (!branch) {
            reason = "cell region contains a non-branch terminator";
            return false;
        }
        for (auto *instruction : block->instr_list_) {
            if (instruction->is_phi() || instruction->isTerminator()) continue;
            if (!isCloneableInstruction(instruction)) {
                reason = "cell region contains an unclonable instruction";
                return false;
            }
        }
        for (unsigned i = 0; i < branch->num_ops_; ++i) {
            auto *successor = dynamic_cast<BasicBlock *>(branch->get_operand(i));
            if (!successor) continue;
            if (!cellBlocks.count(successor) && successor != plan.innerLatch) {
                reason = "cell region has an unsupported escape edge";
                return false;
            }
        }
    }

    for (auto *instruction : plan.outerExit->instr_list_) {
        if (!instruction->is_phi()) break;
        for (unsigned i = 0; i + 1 < instruction->num_ops_; i += 2) {
            auto *predecessor = dynamic_cast<BasicBlock *>(
                instruction->get_operand(i + 1));
            if (predecessor && plan.outer->blocks.count(predecessor)) {
                reason = "outer loop has a live-out phi";
                return false;
            }
        }
    }
    return true;
}

bool validateExternalValue(Value *value, const TriangleSchedulePlan &plan,
                           const std::set<BasicBlock *> &cellBlocks,
                           std::set<Value *> &visiting) {
    if (!value || dynamic_cast<Constant *>(value) ||
        dynamic_cast<Argument *>(value) ||
        dynamic_cast<GlobalVariable *>(value) ||
        dynamic_cast<BasicBlock *>(value) || value == plan.outerIV ||
        value == plan.distanceIV)
        return true;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction || cellBlocks.count(instruction->parent_) ||
        !plan.outer->blocks.count(instruction->parent_))
        return true;
    if (instruction->is_phi() || instruction->isTerminator() ||
        instruction->is_load() || instruction->is_store() ||
        instruction->is_call() || !isCloneableInstruction(instruction) ||
        !visiting.insert(value).second)
        return false;
    for (unsigned i = 0; i < instruction->num_ops_; ++i)
        if (!validateExternalValue(instruction->get_operand(i), plan,
                                   cellBlocks, visiting))
            return false;
    visiting.erase(value);
    return true;
}

Value *materializeExternal(Value *value, BasicBlock *destination,
                           const TriangleSchedulePlan &plan,
                           const std::set<BasicBlock *> &cellBlocks,
                           ValueMap &map) {
    auto found = map.find(value);
    if (found != map.end()) return found->second;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction || cellBlocks.count(instruction->parent_) ||
        !plan.outer->blocks.count(instruction->parent_))
        return value;
    for (unsigned i = 0; i < instruction->num_ops_; ++i) {
        Value *operand = instruction->get_operand(i);
        if (dynamic_cast<BasicBlock *>(operand)) continue;
        Value *mapped = materializeExternal(operand, destination, plan,
                                            cellBlocks, map);
        if (mapped != operand) map[operand] = mapped;
    }
    Instruction *clone = cloneInstruction(instruction, destination, map);
    map[value] = clone;
    return clone;
}

bool applyTriangle(const TriangleSchedulePlan &plan, Function *function) {
    Module *module = function->parent_;
    std::set<BasicBlock *> cellBlocks;
    for (auto *block : plan.inner->blocksOrdered)
        if (block != plan.innerLatch) cellBlocks.insert(block);

    std::string reason;
    if (!validateCloneRegion(plan, cellBlocks, reason)) {
        debugReject(function, plan.inner, reason);
        return false;
    }
    for (auto *block : cellBlocks) {
        for (auto *instruction : block->instr_list_) {
            for (unsigned i = 0; i < instruction->num_ops_; ++i) {
                Value *operand = instruction->get_operand(i);
                if (dynamic_cast<BasicBlock *>(operand)) continue;
                std::set<Value *> visiting;
                if (!validateExternalValue(operand, plan, cellBlocks,
                                           visiting)) {
                    debugReject(function, plan.inner,
                                "cell depends on a non-rematerializable outer value");
                    return false;
                }
            }
        }
    }

    int serial = static_cast<int>(function->basic_blocks_.size());
    auto block = [&](const std::string &name) {
        return new BasicBlock(module,
                              "triangle." + name + "." +
                                  std::to_string(serial++),
                              function);
    };
    auto *waveHeader = block("wave.h");
    auto *waveBody = block("wave.body");
    auto *laneHeader = block("lane.h");
    auto *cellSetup = block("cell.setup");
    auto *laneLatch = block("lane.latch");
    auto *waveLatch = block("wave.latch");
    laneHeader->setSemFlag(SemFlag::WavefrontCoincident);

    auto *zero = new ConstantInt(module->int32_ty_, 0);
    auto *one = new ConstantInt(module->int32_ty_, 1);

    auto *wave = PhiInst::create_phi(module->int32_ty_, waveHeader);
    wave->add_phi_pair_operand(zero, plan.outerPreheader);
    waveHeader->add_instruction_front(wave);
    auto *waveCompare = new ICmpInst(ICmpInst::ICMP_SLT, wave, plan.extent,
                                     waveHeader);
    new BranchInst(waveCompare, waveBody, plan.outerExit, waveHeader);

    auto *laneCount = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                     plan.extent, wave, waveBody);
    new BranchInst(laneHeader, waveBody);

    auto *lane = PhiInst::create_phi(module->int32_ty_, laneHeader);
    lane->add_phi_pair_operand(zero, waveBody);
    laneHeader->add_instruction_front(lane);
    auto *laneCompare = new ICmpInst(ICmpInst::ICMP_SLT, lane, laneCount,
                                     laneHeader);

    auto *rowBase = new BinaryInst(module->int32_ty_, Instruction::Add,
                                   wave, one, laneHeader);
    auto *row = new BinaryInst(module->int32_ty_, Instruction::Add,
                               rowBase, lane, laneHeader);
    auto *outerValue = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                      plan.extent, row, laneHeader);
    new BranchInst(laneCompare, cellSetup, waveLatch, laneHeader);

    ValueMap valueMap;
    valueMap[plan.outerIV] = outerValue;
    valueMap[plan.distanceIV] = wave;

    for (auto *oldBlock : cellBlocks) {
        for (auto *instruction : oldBlock->instr_list_) {
            for (unsigned i = 0; i < instruction->num_ops_; ++i) {
                Value *operand = instruction->get_operand(i);
                if (dynamic_cast<BasicBlock *>(operand)) continue;
                materializeExternal(operand, cellSetup, plan, cellBlocks,
                                    valueMap);
            }
        }
    }

    std::unordered_map<BasicBlock *, BasicBlock *> blockMap;
    for (auto *oldBlock : plan.inner->blocksOrdered)
        if (cellBlocks.count(oldBlock))
            blockMap[oldBlock] = block("cell");
    BasicBlock *cellEntry = blockMap[plan.inner->header];
    new BranchInst(cellEntry, cellSetup);

    for (auto *oldBlock : plan.inner->blocksOrdered) {
        if (!cellBlocks.count(oldBlock)) continue;
        BasicBlock *newBlock = blockMap[oldBlock];
        for (auto *instruction : oldBlock->instr_list_) {
            if (!instruction->is_phi() || instruction == plan.distanceIV)
                continue;
            auto *newPhi = PhiInst::create_phi(instruction->type_, newBlock);
            newPhi->copySemFlagsFrom(instruction);
            newBlock->add_instruction(newPhi);
            valueMap[instruction] = newPhi;
        }
    }

    for (auto *oldBlock : plan.inner->blocksOrdered) {
        if (!cellBlocks.count(oldBlock)) continue;
        BasicBlock *newBlock = blockMap[oldBlock];
        for (auto *instruction : oldBlock->instr_list_) {
            if (instruction->is_phi() || instruction->isTerminator()) continue;
            Instruction *clone = cloneInstruction(instruction, newBlock,
                                                  valueMap);
            valueMap[instruction] = clone;
        }
    }

    for (auto *oldBlock : plan.inner->blocksOrdered) {
        if (!cellBlocks.count(oldBlock)) continue;
        BasicBlock *newBlock = blockMap[oldBlock];
        for (auto *instruction : oldBlock->instr_list_) {
            if (!instruction->is_phi() || instruction == plan.distanceIV)
                continue;
            auto *newPhi = static_cast<PhiInst *>(valueMap[instruction]);
            for (unsigned i = 0; i + 1 < instruction->num_ops_; i += 2) {
                auto *oldPred = dynamic_cast<BasicBlock *>(
                    instruction->get_operand(i + 1));
                BasicBlock *newPred = nullptr;
                if (oldPred == plan.innerPreheader)
                    newPred = cellSetup;
                else {
                    auto found = blockMap.find(oldPred);
                    if (found != blockMap.end()) newPred = found->second;
                }
                if (newPred)
                    newPhi->add_phi_pair_operand(
                        remap(instruction->get_operand(i), valueMap), newPred);
            }
        }

        auto *oldBranch = static_cast<BranchInst *>(oldBlock->get_terminator());
        if (oldBranch->num_ops_ == 1) {
            auto *oldTarget = static_cast<BasicBlock *>(oldBranch->get_operand(0));
            BasicBlock *newTarget = oldTarget == plan.innerLatch
                                        ? laneLatch
                                        : blockMap[oldTarget];
            new BranchInst(newTarget, newBlock);
        } else {
            auto mapTarget = [&](Value *target) {
                auto *oldTarget = static_cast<BasicBlock *>(target);
                return oldTarget == plan.innerLatch ? laneLatch
                                                    : blockMap[oldTarget];
            };
            new BranchInst(remap(oldBranch->get_operand(0), valueMap),
                           mapTarget(oldBranch->get_operand(1)),
                           mapTarget(oldBranch->get_operand(2)), newBlock);
        }
    }

    auto *laneNext = new BinaryInst(module->int32_ty_, Instruction::Add,
                                    lane, one, laneLatch);
    lane->add_phi_pair_operand(laneNext, laneLatch);
    new BranchInst(laneHeader, laneLatch);

    auto *waveNext = new BinaryInst(module->int32_ty_, Instruction::Add,
                                    wave, one, waveLatch);
    wave->add_phi_pair_operand(waveNext, waveLatch);
    new BranchInst(waveHeader, waveLatch);

    auto *preTerm = plan.outerPreheader->get_terminator();
    for (unsigned i = 0; i < preTerm->num_ops_; ++i)
        if (preTerm->get_operand(i) == plan.outer->header)
            preTerm->set_operand(i, waveHeader);
    plan.outerPreheader->remove_succ_basic_block(plan.outer->header);
    plan.outer->header->remove_pre_basic_block(plan.outerPreheader);
    plan.outerPreheader->add_succ_basic_block(waveHeader);
    waveHeader->add_pre_basic_block(plan.outerPreheader);

    removeUnreachableBlocks(function);
    function->set_instr_name();
    if (debugEnabled())
        std::cerr << "[TriangleInterchange] transformed func="
                  << function->name_ << " domain=0<=distance<row<=extent"
                  << " schedule=(distance,lane) offset=" << plan.offset
                  << "\n";
    return true;
}

} // namespace

bool TriangleInterchange::runOnFunction(Function *function) {
    ArgumentAliasAnalysis argumentAlias;
    argumentAlias.analyze(function->parent_);
    for (int iteration = 0; iteration < 8; ++iteration) {
        LoopInfo loopInfo;
        loopInfo.analyze(function);
        ScheduleAffineAnalyzer scheduleAffine;
        bool changed = false;
        for (const auto &owned : loopInfo.allLoops()) {
            Loop *loop = owned.get();
            std::string reason;
            auto plan = matchTriangle(*loop, reason);
            if (!plan) {
                if (loop->parent) debugReject(function, loop, reason);
                continue;
            }
            if (!proveWavefrontDependences(*plan, loopInfo, scheduleAffine,
                                           argumentAlias, reason)) {
                debugReject(function, loop, reason);
                continue;
            }
            if (applyTriangle(*plan, function)) {
                changed = true;
                return true;
            }
        }
        if (!changed) break;
    }
    return false;
}

void TriangleInterchange::execute(Module *module) {
    for (auto *function : module->function_list_)
        if (!function->is_declaration()) runOnFunction(function);
}

PreservedAnalyses TriangleInterchange::execute(Module *module,
                                               AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *function : module->function_list_)
        if (!function->is_declaration())
            changed |= runOnFunction(function);
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
