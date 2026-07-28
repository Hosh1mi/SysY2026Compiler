#include "../../include/mid/opt/idiomRecognize.hpp"

#include "../../include/mid/analysis/recurrenceAnalysis.hpp"
#include "../../include/mid/opt/libFunc.hpp"
#include "../../include/mid/ir/irBuilder.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <climits>
#include <set>
#include <vector>

namespace {

bool isLoopInvariant(Value *val, const std::set<BasicBlock *> &blocks) {
    if (dynamic_cast<Constant *>(val) || dynamic_cast<Argument *>(val) ||
        dynamic_cast<GlobalVariable *>(val))
        return true;
    auto *inst = dynamic_cast<Instruction *>(val);
    return inst && !blocks.count(inst->parent_);
}

bool getLoopPhiIncoming(const Loop &loop, PhiInst *phi, Value *&init,
                        Value *&latchValue, BasicBlock *&latchBlock) {
    init = nullptr;
    latchValue = nullptr;
    latchBlock = nullptr;
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
        if (!pred) return false;
        if (loop.blocks.count(pred)) {
            if (latchValue) return false;
            latchValue = phi->get_operand(i);
            latchBlock = pred;
        } else {
            if (init) return false;
            init = phi->get_operand(i);
        }
    }
    return init && latchValue && latchBlock;
}

int typeSize(Type *type) {
    if (!type) return -1;
    switch (type->tid_) {
    case Type::IntegerTyID:
    case Type::FloatTyID:
        return 4;
    case Type::PointerTyID:
        return 8;
    case Type::ArrayTyID: {
        auto *array = static_cast<ArrayType *>(type);
        int element = typeSize(array->contained_);
        return element < 0 ? -1 : element * static_cast<int>(array->num_elements_);
    }
    default:
        return -1;
    }
}

bool matchIVPlusConstant(Value *value, PhiInst *iv, int &offset) {
    if (value == iv) {
        offset = 0;
        return true;
    }
    auto *bin = dynamic_cast<BinaryInst *>(value);
    if (!bin) return false;
    if (bin->is_add()) {
        if (bin->get_operand(0) == iv) {
            auto *c = dynamic_cast<ConstantInt *>(bin->get_operand(1));
            if (!c) return false;
            offset = c->value_;
            return true;
        }
        if (bin->get_operand(1) == iv) {
            auto *c = dynamic_cast<ConstantInt *>(bin->get_operand(0));
            if (!c) return false;
            offset = c->value_;
            return true;
        }
    }
    if (bin->is_sub() && bin->get_operand(0) == iv) {
        auto *c = dynamic_cast<ConstantInt *>(bin->get_operand(1));
        if (!c || c->value_ == INT_MIN) return false;
        offset = -c->value_;
        return true;
    }
    return false;
}

bool varyingIndexHasUnitElementStride(GetElementPtrInst *gep, unsigned varyingIndex,
                                      Type *scalarType) {
    if (!gep || gep->get_operand(0)->type_->tid_ != Type::PointerTyID)
        return false;
    Type *current =
        static_cast<PointerType *>(gep->get_operand(0)->type_)->contained_;
    int scalarBytes = typeSize(scalarType);
    if (scalarBytes <= 0) return false;

    for (unsigned i = 1; i < gep->num_ops_; ++i) {
        int indexBytes = typeSize(current);
        if (i == varyingIndex)
            return indexBytes == scalarBytes;
        if (current->tid_ == Type::ArrayTyID)
            current = static_cast<ArrayType *>(current)->contained_;
        else if (current->tid_ == Type::PointerTyID)
            current = static_cast<PointerType *>(current)->contained_;
    }
    return false;
}

bool pointerOffsetFromPhi(Value *pointer, PhiInst *phi, int &offset) {
    offset = 0;
    Value *cursor = pointer;
    while (cursor != phi) {
        auto *gep = dynamic_cast<GetElementPtrInst *>(cursor);
        if (!gep || gep->num_ops_ != 2) return false;
        auto *constant = dynamic_cast<ConstantInt *>(gep->get_operand(1));
        if (!constant) return false;
        offset += constant->value_;
        cursor = gep->get_operand(0);
    }
    return true;
}

bool isMemsetFillConstant(ConstantInt *ci, int &fillByte) {
    int v = ci->value_;
    fillByte = v & 0xFF;
    for (int shift = 8; shift < 32; shift += 8) {
        if (((v >> shift) & 0xFF) != fillByte)
            return false;
    }
    return true;
}

struct MemsetMatch {
    StoreInst *store = nullptr;
    Value *base = nullptr;
    GetElementPtrInst *inductionGeep = nullptr;
    unsigned inductionIndex = 0;
    unsigned outerInductionIndex = 0;
    Value *elementCount = nullptr;
    Value *innerBound = nullptr;
    int fillByte = 0;
    int elementStrideBytes = 0;
    bool nested2D = false;
};

struct MemcpyMatch {
    LoadInst *load = nullptr;
    StoreInst *store = nullptr;
    Value *destBase = nullptr;
    Value *srcBase = nullptr;
    GetElementPtrInst *destGeep = nullptr;
    GetElementPtrInst *srcGeep = nullptr;
    unsigned destIndex = 0;
    unsigned srcIndex = 0;
    Value *elementCount = nullptr;
    int elementStrideBytes = 0;
};

bool hasNestedChildLoop(const Loop &loop, LoopInfo &LI) {
    for (auto &other : LI.allLoops()) {
        if (other.get() == &loop) continue;
        if (other->depth > loop.depth && loop.blocks.count(other->header))
            return true;
    }
    return false;
}

struct IVDesc {
    PhiInst *phi = nullptr;
    AllocaInst *alloca = nullptr;
    Value *bound = nullptr;
};

bool classifyStoreAddressPhi(const Loop &loop, PhiInst *ivPhi, StoreInst *store,
                             MemsetMatch &match) {
    Value *pointer = store->get_operand(1);
    Type *scalarType = store->get_operand(0)->type_;
    int scalarBytes = typeSize(scalarType);
    if (scalarBytes <= 0) return false;

    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi == ivPhi || phi->type_->tid_ != Type::PointerTyID) continue;

        int offset = 0;
        if (!pointerOffsetFromPhi(pointer, phi, offset)) continue;
        if (offset != 0) return false;

        Value *init = nullptr;
        Value *latchValue = nullptr;
        BasicBlock *latchBlock = nullptr;
        if (!getLoopPhiIncoming(loop, phi, init, latchValue, latchBlock))
            return false;
        auto *update = dynamic_cast<GetElementPtrInst *>(latchValue);
        if (!update || update->num_ops_ != 2 || update->get_operand(0) != phi)
            return false;
        auto *step = dynamic_cast<ConstantInt *>(update->get_operand(1));
        if (!step || step->value_ != 1) return false;

        match.base = init;
        match.elementStrideBytes = scalarBytes;
        return true;
    }

    auto *gep = dynamic_cast<GetElementPtrInst *>(pointer);
    if (!gep) return false;

    unsigned varying = 0;
    int ivOffset = 0;
    int varyingCount = 0;
    for (unsigned i = 1; i < gep->num_ops_; ++i) {
        int offset = 0;
        if (matchIVPlusConstant(gep->get_operand(i), ivPhi, offset)) {
            varying = i;
            ivOffset = offset;
            ++varyingCount;
        } else if (!isLoopInvariant(gep->get_operand(i), loop.blocks)) {
            return false;
        }
    }
    if (!isLoopInvariant(gep->get_operand(0), loop.blocks)) return false;
    if (varyingCount != 1 || ivOffset != 0) return false;
    if (!varyingIndexHasUnitElementStride(gep, varying, scalarType)) return false;

    match.inductionGeep = gep;
    match.inductionIndex = varying;
    match.elementStrideBytes = scalarBytes;
    return true;
}

bool classifyStoreAddressAlloca(const Loop &loop, AllocaInst *ivAlloca,
                                StoreInst *store, MemsetMatch &match) {
    Value *pointer = store->get_operand(1);
    Type *scalarType = store->get_operand(0)->type_;
    int scalarBytes = typeSize(scalarType);
    if (scalarBytes <= 0) return false;

    auto *gep = dynamic_cast<GetElementPtrInst *>(pointer);
    if (!gep) return false;

    unsigned varying = 0;
    int varyingCount = 0;
    for (unsigned i = 1; i < gep->num_ops_; ++i) {
        auto *load = dynamic_cast<LoadInst *>(gep->get_operand(i));
        if (load && load->get_operand(0) == ivAlloca) {
            varying = i;
            ++varyingCount;
        } else if (!isLoopInvariant(gep->get_operand(i), loop.blocks)) {
            return false;
        }
    }
    if (!isLoopInvariant(gep->get_operand(0), loop.blocks)) return false;
    if (varyingCount != 1) return false;
    if (!varyingIndexHasUnitElementStride(gep, varying, scalarType)) return false;

    match.inductionGeep = gep;
    match.inductionIndex = varying;
    match.elementStrideBytes = scalarBytes;
    return true;
}

bool ivValueMatches(Value *value, const IVDesc &iv) {
    if (iv.phi && value == iv.phi) return true;
    if (iv.alloca) {
        auto *load = dynamic_cast<LoadInst *>(value);
        return load && load->get_operand(0) == iv.alloca;
    }
    return false;
}

bool findCountingIV(Loop &loop, BasicBlock *latch, ICmpInst *compare, IVDesc &iv) {
    if (!compare || compare->icmp_op_ != ICmpInst::ICMP_SLT) return false;
    Value *bound = compare->get_operand(1);
    if (!isLoopInvariant(bound, loop.blocks)) return false;

    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->type_->tid_ != Type::IntegerTyID ||
            compare->get_operand(0) != phi)
            continue;

        Value *init = nullptr;
        Value *latchValue = nullptr;
        BasicBlock *latchBlock = nullptr;
        if (!getLoopPhiIncoming(loop, phi, init, latchValue, latchBlock))
            continue;
        auto *initCI = dynamic_cast<ConstantInt *>(init);
        if (!initCI || initCI->value_ != 0) continue;

        auto *update = dynamic_cast<BinaryInst *>(latchValue);
        if (!update || !update->is_add()) continue;
        auto *step = dynamic_cast<ConstantInt *>(update->get_operand(1));
        if (update->get_operand(0) != phi || !step || step->value_ != 1)
            continue;

        iv.phi = phi;
        iv.bound = bound;
        return true;
    }

    auto *load = dynamic_cast<LoadInst *>(compare->get_operand(0));
    if (!load) return false;
    auto *alloca = dynamic_cast<AllocaInst *>(load->get_operand(0));
    if (!alloca || alloca->alloca_ty_->tid_ != Type::IntegerTyID) return false;

    bool initZero = false;
    for (auto *inst : loop.preheader->instr_list_) {
        if (!inst->is_store() || inst->get_operand(1) != alloca) continue;
        auto *ci = dynamic_cast<ConstantInt *>(inst->get_operand(0));
        if (ci && ci->value_ == 0) initZero = true;
    }
    if (!initZero) return false;

    bool foundUpdate = false;
    for (auto *inst : latch->instr_list_) {
        if (!inst->is_store() || inst->get_operand(1) != alloca) continue;
        auto *add = dynamic_cast<BinaryInst *>(inst->get_operand(0));
        if (!add || !add->is_add()) continue;
        auto *step = dynamic_cast<ConstantInt *>(add->get_operand(1));
        if (!step || step->value_ != 1) {
            step = dynamic_cast<ConstantInt *>(add->get_operand(0));
            if (!step || step->value_ != 1) continue;
            if (!ivValueMatches(add->get_operand(1), IVDesc{nullptr, alloca, nullptr}))
                continue;
        } else if (!ivValueMatches(add->get_operand(0), IVDesc{nullptr, alloca, nullptr})) {
            continue;
        }
        foundUpdate = true;
        break;
    }
    if (!foundUpdate) return false;

    iv.alloca = alloca;
    iv.bound = bound;
    return true;
}

bool isAllowedLoopInst(Instruction *inst, const IVDesc &iv, bool &sawStore) {
    if (inst->is_phi() || inst->isTerminator()) return true;
    if (inst->is_call() || inst->is_alloca()) return false;
    if (inst->is_store()) {
        if (sawStore) return false;
        sawStore = true;
        return true;
    }
    if (inst->is_load()) {
        if (!iv.alloca) return false;
        auto *load = static_cast<LoadInst *>(inst);
        return load->get_operand(0) == iv.alloca;
    }
    return dynamic_cast<BinaryInst *>(inst) ||
           dynamic_cast<GetElementPtrInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst);
}

bool classifyStoreAddress(const Loop &loop, const IVDesc &iv, StoreInst *store,
                          MemsetMatch &match) {
    if (iv.phi) return classifyStoreAddressPhi(loop, iv.phi, store, match);
    if (iv.alloca) return classifyStoreAddressAlloca(loop, iv.alloca, store, match);
    return false;
}

bool ivOperandMatches(Value *value, const IVDesc &iv, int &offset) {
    if (iv.phi && matchIVPlusConstant(value, iv.phi, offset)) return true;
    if (iv.alloca) {
        auto *load = dynamic_cast<LoadInst *>(value);
        if (load && load->get_operand(0) == iv.alloca) {
            offset = 0;
            return true;
        }
    }
    offset = 0;
    return false;
}

bool classifyStoreAddress2D(const Loop &outer, const IVDesc &outerIV,
                            const IVDesc &innerIV, StoreInst *store,
                            MemsetMatch &match) {
    Value *pointer = store->get_operand(1);
    Type *scalarType = store->get_operand(0)->type_;
    int scalarBytes = typeSize(scalarType);
    if (scalarBytes <= 0) return false;

    auto *gep = dynamic_cast<GetElementPtrInst *>(pointer);
    if (!gep) return false;

    unsigned outerVarying = 0;
    unsigned innerVarying = 0;
    int outerCount = 0;
    int innerCount = 0;
    for (unsigned i = 1; i < gep->num_ops_; ++i) {
        int offset = 0;
        if (ivOperandMatches(gep->get_operand(i), outerIV, offset)) {
            if (offset != 0) return false;
            outerVarying = i;
            ++outerCount;
        } else if (ivOperandMatches(gep->get_operand(i), innerIV, offset)) {
            if (offset != 0) return false;
            innerVarying = i;
            ++innerCount;
        } else if (!isLoopInvariant(gep->get_operand(i), outer.blocks)) {
            return false;
        }
    }
    if (!isLoopInvariant(gep->get_operand(0), outer.blocks)) return false;
    if (outerCount != 1 || innerCount != 1) return false;
    if (!varyingIndexHasUnitElementStride(gep, innerVarying, scalarType))
        return false;

    match.inductionGeep = gep;
    match.inductionIndex = innerVarying;
    match.outerInductionIndex = outerVarying;
    match.elementStrideBytes = scalarBytes;
    return true;
}

bool classifyMemAddressPhi(const Loop &loop, PhiInst *ivPhi, Value *pointer,
                           Type *scalarType, Value *&base,
                           GetElementPtrInst *&inductionGeep,
                           unsigned &inductionIndex, int &elementStrideBytes) {
    int scalarBytes = typeSize(scalarType);
    if (scalarBytes <= 0) return false;

    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi == ivPhi || phi->type_->tid_ != Type::PointerTyID) continue;

        int offset = 0;
        if (!pointerOffsetFromPhi(pointer, phi, offset)) continue;
        if (offset != 0) return false;

        Value *init = nullptr;
        Value *latchValue = nullptr;
        BasicBlock *latchBlock = nullptr;
        if (!getLoopPhiIncoming(loop, phi, init, latchValue, latchBlock))
            return false;
        auto *update = dynamic_cast<GetElementPtrInst *>(latchValue);
        if (!update || update->num_ops_ != 2 || update->get_operand(0) != phi)
            return false;
        auto *step = dynamic_cast<ConstantInt *>(update->get_operand(1));
        if (!step || step->value_ != 1) return false;

        base = init;
        inductionGeep = nullptr;
        inductionIndex = 0;
        elementStrideBytes = scalarBytes;
        return true;
    }

    auto *gep = dynamic_cast<GetElementPtrInst *>(pointer);
    if (!gep) return false;

    unsigned varying = 0;
    int ivOffset = 0;
    int varyingCount = 0;
    for (unsigned i = 1; i < gep->num_ops_; ++i) {
        int offset = 0;
        if (matchIVPlusConstant(gep->get_operand(i), ivPhi, offset)) {
            varying = i;
            ivOffset = offset;
            ++varyingCount;
        } else if (!isLoopInvariant(gep->get_operand(i), loop.blocks)) {
            return false;
        }
    }
    if (!isLoopInvariant(gep->get_operand(0), loop.blocks)) return false;
    if (varyingCount != 1 || ivOffset != 0) return false;
    if (!varyingIndexHasUnitElementStride(gep, varying, scalarType)) return false;

    base = nullptr;
    inductionGeep = gep;
    inductionIndex = varying;
    elementStrideBytes = scalarBytes;
    return true;
}

bool classifyMemcpyAddress(const Loop &loop, const IVDesc &iv, LoadInst *load,
                           StoreInst *store, MemcpyMatch &match) {
    if (store->get_operand(0) != load) return false;
    Type *scalarType = store->get_operand(0)->type_;
    int scalarBytes = typeSize(scalarType);
    if (scalarBytes <= 0) return false;

    Value *destBase = nullptr;
    Value *srcBase = nullptr;
    GetElementPtrInst *destGeep = nullptr;
    GetElementPtrInst *srcGeep = nullptr;
    unsigned destIndex = 0;
    unsigned srcIndex = 0;
    int destStride = 0;
    int srcStride = 0;

    if (iv.phi) {
        if (!classifyMemAddressPhi(loop, iv.phi, store->get_operand(1), scalarType,
                                   destBase, destGeep, destIndex, destStride))
            return false;
        if (!classifyMemAddressPhi(loop, iv.phi, load->get_operand(0), scalarType,
                                   srcBase, srcGeep, srcIndex, srcStride))
            return false;
    } else if (iv.alloca) {
        auto *destGep = dynamic_cast<GetElementPtrInst *>(store->get_operand(1));
        auto *srcGep = dynamic_cast<GetElementPtrInst *>(load->get_operand(0));
        if (!destGep || !srcGep) return false;

        unsigned destVarying = 0;
        unsigned srcVarying = 0;
        int destCount = 0;
        int srcCount = 0;
        for (unsigned i = 1; i < destGep->num_ops_; ++i) {
            auto *l = dynamic_cast<LoadInst *>(destGep->get_operand(i));
            if (l && l->get_operand(0) == iv.alloca) {
                destVarying = i;
                ++destCount;
            } else if (!isLoopInvariant(destGep->get_operand(i), loop.blocks)) {
                return false;
            }
        }
        for (unsigned i = 1; i < srcGep->num_ops_; ++i) {
            auto *l = dynamic_cast<LoadInst *>(srcGep->get_operand(i));
            if (l && l->get_operand(0) == iv.alloca) {
                srcVarying = i;
                ++srcCount;
            } else if (!isLoopInvariant(srcGep->get_operand(i), loop.blocks)) {
                return false;
            }
        }
        if (!isLoopInvariant(destGep->get_operand(0), loop.blocks) ||
            !isLoopInvariant(srcGep->get_operand(0), loop.blocks))
            return false;
        if (destCount != 1 || srcCount != 1) return false;
        if (!varyingIndexHasUnitElementStride(destGep, destVarying, scalarType) ||
            !varyingIndexHasUnitElementStride(srcGep, srcVarying, scalarType))
            return false;
        destGeep = destGep;
        srcGeep = srcGep;
        destIndex = destVarying;
        srcIndex = srcVarying;
        destStride = scalarBytes;
        srcStride = scalarBytes;
    } else {
        return false;
    }

    if (destStride != srcStride) return false;

    match.destBase = destBase;
    match.srcBase = srcBase;
    match.destGeep = destGeep;
    match.srcGeep = srcGeep;
    match.destIndex = destIndex;
    match.srcIndex = srcIndex;
    match.elementStrideBytes = destStride;
    return true;
}

bool validateCanonicalLoopShape(Loop &loop, BasicBlock *&bodyEntry,
                                ICmpInst *&compare) {
    if (!loop.preheader || !loop.singleLatch() || !loop.singleExit())
        return false;
    if (loop.singleLatch() == loop.header) return false;
    if (loop.blocks.size() < 2 || loop.blocks.size() > 3) return false;

    auto *preTerm = loop.preheader->get_terminator();
    auto *headerTerm = loop.header->get_terminator();
    auto *latch = loop.singleLatch();
    auto *latchTerm = latch->get_terminator();
    auto *exit = loop.singleExit();
    if (!preTerm || !preTerm->is_br() || preTerm->num_ops_ != 1 ||
        preTerm->get_operand(0) != loop.header)
        return false;
    if (!headerTerm || !headerTerm->is_br() || headerTerm->num_ops_ != 3 ||
        headerTerm->get_operand(2) != exit)
        return false;
    if (!latchTerm || !latchTerm->is_br() || latchTerm->num_ops_ != 1 ||
        latchTerm->get_operand(0) != loop.header)
        return false;

    bodyEntry = dynamic_cast<BasicBlock *>(headerTerm->get_operand(1));
    if (!bodyEntry || !loop.blocks.count(bodyEntry)) return false;
    if (bodyEntry != latch) {
        if (loop.blocks.size() != 3) return false;
        auto *bodyTerm = bodyEntry->get_terminator();
        if (!bodyTerm || !bodyTerm->is_br() || bodyTerm->num_ops_ != 1 ||
            bodyTerm->get_operand(0) != latch)
            return false;
    }

    compare = dynamic_cast<ICmpInst *>(headerTerm->get_operand(0));
    return compare != nullptr;
}

bool validateOuterLoopShape(Loop &outer, Loop &inner, ICmpInst *&compare) {
    if (!outer.preheader || !outer.singleLatch() || !outer.singleExit())
        return false;
    if (outer.singleLatch() == outer.header) return false;

    auto *preTerm = outer.preheader->get_terminator();
    auto *headerTerm = outer.header->get_terminator();
    auto *latch = outer.singleLatch();
    auto *latchTerm = latch->get_terminator();
    auto *exit = outer.singleExit();
    if (!preTerm || !preTerm->is_br() || preTerm->num_ops_ != 1 ||
        preTerm->get_operand(0) != outer.header)
        return false;
    if (!headerTerm || !headerTerm->is_br() || headerTerm->num_ops_ != 3 ||
        headerTerm->get_operand(2) != exit)
        return false;
    if (!latchTerm || !latchTerm->is_br() || latchTerm->num_ops_ != 1 ||
        latchTerm->get_operand(0) != outer.header)
        return false;

    auto *bodyEntry = dynamic_cast<BasicBlock *>(headerTerm->get_operand(1));
    if (!bodyEntry) return false;
    if (!inner.blocks.count(bodyEntry) && bodyEntry != inner.preheader)
        return false;

    compare = dynamic_cast<ICmpInst *>(headerTerm->get_operand(0));
    return compare != nullptr;
}

bool tryMatchNestedMemset2D(Loop &outer, Loop &inner, MemsetMatch &match) {
    if (outer.children.size() != 1 || outer.children[0] != &inner) return false;
    if (!inner.children.empty()) return false;

    ICmpInst *outerCompare = nullptr;
    if (!validateOuterLoopShape(outer, inner, outerCompare)) return false;

    BasicBlock *innerBody = nullptr;
    ICmpInst *innerCompare = nullptr;
    if (!validateCanonicalLoopShape(inner, innerBody, innerCompare)) return false;

    auto *outerLatch = outer.singleLatch();
    IVDesc outerIV;
    IVDesc innerIV;
    if (!findCountingIV(outer, outerLatch, outerCompare, outerIV) ||
        !findCountingIV(inner, inner.singleLatch(), innerCompare, innerIV))
        return false;
    if (outerIV.bound != innerIV.bound) return false;

    for (auto *bb : outer.blocks) {
        if (inner.blocks.count(bb)) continue;
        if (bb != outer.header && bb != outerLatch && bb != inner.preheader)
            return false;
    }

    for (auto *inst : outer.header->instr_list_) {
        if (inst->is_phi() || inst == outerCompare || inst->isTerminator())
            continue;
        if (outerIV.alloca && inst->is_load() &&
            inst->get_operand(0) == outerIV.alloca)
            continue;
        return false;
    }
    for (auto *inst : outerLatch->instr_list_) {
        if (inst->isTerminator()) continue;
        if (auto *update = dynamic_cast<BinaryInst *>(inst)) {
            if (update->is_add() && ivValueMatches(update->get_operand(0), outerIV))
                continue;
        }
        if (outerIV.alloca && inst->is_store() &&
            inst->get_operand(1) == outerIV.alloca) {
            auto *add = dynamic_cast<BinaryInst *>(inst->get_operand(0));
            if (add && add->is_add()) continue;
        }
        return false;
    }

    std::vector<BasicBlock *> innerBlocks{inner.header, innerBody};
    if (inner.singleLatch() != innerBody)
        innerBlocks.push_back(inner.singleLatch());

    StoreInst *store = nullptr;
    bool sawStore = false;
    for (auto *bb : innerBlocks) {
        for (auto *inst : bb->instr_list_) {
            if (!isAllowedLoopInst(inst, innerIV, sawStore)) return false;
            if (inst->is_store()) store = static_cast<StoreInst *>(inst);
        }
    }
    if (!store || !sawStore) return false;

    auto *fillCI = dynamic_cast<ConstantInt *>(store->get_operand(0));
    if (!fillCI) return false;
    int fillByte = 0;
    if (!isMemsetFillConstant(fillCI, fillByte)) return false;

    MemsetMatch local;
    if (!classifyStoreAddress2D(outer, outerIV, innerIV, store, local))
        return false;

    for (auto *bb : outer.blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() && inst->parent_ == outer.header) continue;
            for (const auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && user->parent_ && !outer.blocks.count(user->parent_))
                    return false;
            }
        }
    }

    match.store = store;
    match.base = local.base;
    match.inductionGeep = local.inductionGeep;
    match.inductionIndex = local.inductionIndex;
    match.outerInductionIndex = local.outerInductionIndex;
    match.elementCount = outerIV.bound;
    match.innerBound = innerIV.bound;
    match.fillByte = fillByte;
    match.elementStrideBytes = local.elementStrideBytes;
    match.nested2D = true;
    return true;
}

bool tryMatchMemcpyLoop(Loop &loop, MemcpyMatch &match) {
    BasicBlock *bodyEntry = nullptr;
    ICmpInst *compare = nullptr;
    if (!validateCanonicalLoopShape(loop, bodyEntry, compare)) return false;

    auto *latch = loop.singleLatch();
    IVDesc iv;
    if (!findCountingIV(loop, latch, compare, iv)) return false;

    std::vector<BasicBlock *> recipeBlocks{loop.header, bodyEntry};
    if (latch != bodyEntry) recipeBlocks.push_back(latch);

    LoadInst *load = nullptr;
    StoreInst *store = nullptr;
    int dataLoads = 0;
    for (auto *bb : recipeBlocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator()) continue;
            if (inst->is_load()) {
                if (iv.alloca) {
                    auto *l = static_cast<LoadInst *>(inst);
                    if (l->get_operand(0) == iv.alloca) continue;
                }
                if (dataLoads) return false;
                dataLoads++;
                load = static_cast<LoadInst *>(inst);
                continue;
            }
            if (inst->is_store()) {
                if (store) return false;
                store = static_cast<StoreInst *>(inst);
                continue;
            }
            if (inst->is_call() || inst->is_alloca()) return false;
            if (!dynamic_cast<BinaryInst *>(inst) &&
                !dynamic_cast<GetElementPtrInst *>(inst) &&
                !dynamic_cast<ICmpInst *>(inst))
                return false;
        }
    }
    if (!load || !store) return false;

    MemcpyMatch local;
    if (!classifyMemcpyAddress(loop, iv, load, store, local)) return false;

    for (auto *bb : loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() && inst->parent_ == loop.header) continue;
            for (const auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && user->parent_ && !loop.blocks.count(user->parent_))
                    return false;
            }
        }
    }

    match.load = load;
    match.store = store;
    match.destBase = local.destBase;
    match.srcBase = local.srcBase;
    match.destGeep = local.destGeep;
    match.srcGeep = local.srcGeep;
    match.destIndex = local.destIndex;
    match.srcIndex = local.srcIndex;
    match.elementCount = iv.bound;
    match.elementStrideBytes = local.elementStrideBytes;
    return true;
}

bool tryMatchMemsetLoop(Loop &loop, MemsetMatch &match) {
    if (!loop.preheader || !loop.singleLatch() || !loop.singleExit())
        return false;
    if (loop.singleLatch() == loop.header) return false;
    if (loop.blocks.size() < 2 || loop.blocks.size() > 3) return false;

    auto *preTerm = loop.preheader->get_terminator();
    auto *headerTerm = loop.header->get_terminator();
    auto *latch = loop.singleLatch();
    auto *latchTerm = latch->get_terminator();
    auto *exit = loop.singleExit();
    if (!preTerm || !preTerm->is_br() || preTerm->num_ops_ != 1 ||
        preTerm->get_operand(0) != loop.header)
        return false;
    if (!headerTerm || !headerTerm->is_br() || headerTerm->num_ops_ != 3 ||
        headerTerm->get_operand(2) != exit)
        return false;
    if (!latchTerm || !latchTerm->is_br() || latchTerm->num_ops_ != 1 ||
        latchTerm->get_operand(0) != loop.header)
        return false;

    auto *bodyEntry = dynamic_cast<BasicBlock *>(headerTerm->get_operand(1));
    if (!bodyEntry || !loop.blocks.count(bodyEntry)) return false;
    if (bodyEntry != latch) {
        if (loop.blocks.size() != 3) return false;
        auto *bodyTerm = bodyEntry->get_terminator();
        if (!bodyTerm || !bodyTerm->is_br() || bodyTerm->num_ops_ != 1 ||
            bodyTerm->get_operand(0) != latch)
            return false;
    }

    auto *compare = dynamic_cast<ICmpInst *>(headerTerm->get_operand(0));
    IVDesc iv;
    if (!findCountingIV(loop, latch, compare, iv)) return false;

    std::vector<BasicBlock *> recipeBlocks{loop.header, bodyEntry};
    if (latch != bodyEntry) recipeBlocks.push_back(latch);

    StoreInst *store = nullptr;
    bool sawStore = false;
    for (auto *bb : recipeBlocks) {
        for (auto *inst : bb->instr_list_) {
            if (!isAllowedLoopInst(inst, iv, sawStore)) return false;
            if (inst->is_store()) store = static_cast<StoreInst *>(inst);
        }
    }
    if (!store || !sawStore) return false;

    auto *fillCI = dynamic_cast<ConstantInt *>(store->get_operand(0));
    if (!fillCI) return false;
    int fillByte = 0;
    if (!isMemsetFillConstant(fillCI, fillByte)) return false;

    MemsetMatch local;
    if (!classifyStoreAddress(loop, iv, store, local)) return false;

    for (auto *bb : loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() && inst->parent_ == loop.header) continue;
            for (const auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && user->parent_ && !loop.blocks.count(user->parent_))
                    return false;
            }
        }
    }

    match.store = store;
    match.base = local.base;
    match.inductionGeep = local.inductionGeep;
    match.inductionIndex = local.inductionIndex;
    match.elementCount = iv.bound;
    match.fillByte = fillByte;
    match.elementStrideBytes = local.elementStrideBytes;
    return true;
}

Value *buildMemBase(Value *base, GetElementPtrInst *geep,
                    unsigned inductionIndex, unsigned outerInductionIndex,
                    bool zeroOuterIndex, BasicBlock *preheader,
                    Function *func, Module *module) {
    if (!base && geep) {
        std::vector<Value *> idxs;
        for (unsigned i = 1; i < geep->num_ops_; ++i) {
            if (i == inductionIndex ||
                (zeroOuterIndex && i == outerInductionIndex))
                idxs.push_back(new ConstantInt(module->int32_ty_, 0));
            else
                idxs.push_back(geep->get_operand(i));
        }
        auto *firstGEP = GetElementPtrInst::create_split_suffix_gep(
            geep->get_operand(0), idxs, preheader, true);
        preheader->add_instruction_before_terminator(firstGEP);
        base = firstGEP;
    }
    if (!base) return nullptr;
    auto *ptrTy = module->get_pointer_type(module->int32_ty_);
    if (base->type_ != ptrTy) {
        auto *cast = new Bitcast(Instruction::BitCast, base, ptrTy, preheader, true);
        preheader->add_instruction_before_terminator(cast);
        base = cast;
    }
    return base;
}

bool lowerMemcpyLoop(Loop &loop, const MemcpyMatch &match, Module *module,
                     Function *memcpyDecl, Function *func) {
    BasicBlock *preheader = loop.preheader;
    BasicBlock *exit = loop.singleExit();
    if (!preheader || !exit) return false;

    auto *preTerm = preheader->get_terminator();
    if (!preTerm || !preTerm->is_br()) return false;
    int headerOperand = -1;
    for (unsigned i = 0; i < preTerm->num_ops_; i++) {
        if (preTerm->get_operand(i) == loop.header) {
            headerOperand = static_cast<int>(i);
            break;
        }
    }
    if (headerOperand < 0) return false;

    int stride = match.elementStrideBytes;
    Value *byteCount = nullptr;
    if (stride == 1) {
        byteCount = match.elementCount;
    } else {
        auto *strideC = new ConstantInt(module->int32_ty_, stride);
        auto *mul = new BinaryInst(
            module->int32_ty_, Instruction::Mul, match.elementCount, strideC,
            preheader, true);
        preheader->add_instruction_before_terminator(mul);
        byteCount = mul;
    }

    auto *boundCI = dynamic_cast<ConstantInt *>(match.elementCount);
    if (boundCI) {
        long long bytes = 0;
        if (!RecurrenceAnalysis::checkedMul(boundCI->value_, stride, bytes) ||
            bytes > INT_MAX)
            return false;
    }

    Value *dest = buildMemBase(match.destBase, match.destGeep,
                               match.destIndex, 0, false, preheader, func,
                               module);
    Value *src = buildMemBase(match.srcBase, match.srcGeep, match.srcIndex,
                              0, false, preheader, func, module);
    if (!dest || !src) return false;

    std::vector<Value *> args{dest, src, byteCount};
    auto *call = new CallInst(memcpyDecl, args, preheader, true);
    preheader->add_instruction_before_terminator(call);
    preheader->setSemFlag(SemFlag::MemsetIdiomLoop);

    for (auto *inst : exit->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        std::vector<unsigned> loopIncoming;
        Value *fromLoop = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred && loop.blocks.count(pred)) {
                loopIncoming.push_back(i);
                fromLoop = phi->get_operand(i);
            }
        }
        if (loopIncoming.empty()) continue;
        if (!fromLoop || !isLoopInvariant(fromLoop, loop.blocks)) return false;
        for (unsigned idx : loopIncoming)
            phi->remove_operands(static_cast<int>(idx),
                                 static_cast<int>(idx + 1));
    }

    preTerm->set_operand(static_cast<unsigned>(headerOperand), exit);
    preheader->remove_succ_basic_block(loop.header);
    preheader->add_succ_basic_block(exit);
    loop.header->remove_pre_basic_block(preheader);
    exit->add_pre_basic_block(preheader);

    std::vector<BasicBlock *> deadBlocks(loop.blocksOrdered.begin(),
                                         loop.blocksOrdered.end());
    for (auto *bb : deadBlocks)
        func->remove_bb(bb);
    return true;
}

bool lowerMemsetLoop(Loop &loop, const MemsetMatch &match, Module *module,
                     Function *memsetDecl, Function *func) {
    BasicBlock *preheader = loop.preheader;
    BasicBlock *exit = loop.singleExit();
    if (!preheader || !exit) return false;

    auto *preTerm = preheader->get_terminator();
    if (!preTerm || !preTerm->is_br()) return false;
    int headerOperand = -1;
    for (unsigned i = 0; i < preTerm->num_ops_; i++) {
        if (preTerm->get_operand(i) == loop.header) {
            headerOperand = static_cast<int>(i);
            break;
        }
    }
    if (headerOperand < 0) return false;

    int stride = match.elementStrideBytes;
    Value *byteCount = nullptr;
    if (match.nested2D) {
        Value *elementCount = nullptr;
        if (auto *outerC = dynamic_cast<ConstantInt *>(match.elementCount)) {
            if (auto *innerC = dynamic_cast<ConstantInt *>(match.innerBound)) {
                long long elements = 0;
                if (!RecurrenceAnalysis::checkedMul(outerC->value_, innerC->value_,
                                                    elements) ||
                    elements > INT_MAX)
                    return false;
                elementCount =
                    new ConstantInt(module->int32_ty_, static_cast<int>(elements));
            }
        }
        if (!elementCount) {
            auto *mul = new BinaryInst(
                module->int32_ty_, Instruction::Mul, match.elementCount,
                match.innerBound, preheader, true);
            preheader->add_instruction_before_terminator(mul);
            elementCount = mul;
        }
        if (stride == 1) {
            byteCount = elementCount;
        } else {
            auto *strideC = new ConstantInt(module->int32_ty_, stride);
            auto *mul = new BinaryInst(
                module->int32_ty_, Instruction::Mul, elementCount, strideC,
                preheader, true);
            preheader->add_instruction_before_terminator(mul);
            byteCount = mul;
        }
        if (auto *countCI = dynamic_cast<ConstantInt *>(elementCount)) {
            long long bytes = 0;
            if (!RecurrenceAnalysis::checkedMul(countCI->value_, stride, bytes) ||
                bytes > INT_MAX)
                return false;
        }
    } else if (stride == 1) {
        byteCount = match.elementCount;
    } else {
        auto *strideC = new ConstantInt(module->int32_ty_, stride);
        auto *mul = new BinaryInst(
            module->int32_ty_, Instruction::Mul, match.elementCount, strideC,
            preheader, true);
        preheader->add_instruction_before_terminator(mul);
        byteCount = mul;
    }

    auto *boundCI = dynamic_cast<ConstantInt *>(match.elementCount);
    if (!match.nested2D && boundCI) {
        long long bytes = 0;
        if (!RecurrenceAnalysis::checkedMul(boundCI->value_, stride, bytes) ||
            bytes > INT_MAX)
            return false;
    }

    Value *base = buildMemBase(match.base, match.inductionGeep,
                               match.inductionIndex,
                               match.outerInductionIndex, match.nested2D,
                               preheader, func, module);
    if (!base) return false;

    auto *fillC = new ConstantInt(module->int32_ty_, match.fillByte);
    std::vector<Value *> args{base, fillC, byteCount};
    auto *call = new CallInst(memsetDecl, args, preheader, true);
    preheader->add_instruction_before_terminator(call);
    preheader->setSemFlag(SemFlag::MemsetIdiomLoop);

    for (auto *inst : exit->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        std::vector<unsigned> loopIncoming;
        Value *fromLoop = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred && loop.blocks.count(pred)) {
                loopIncoming.push_back(i);
                fromLoop = phi->get_operand(i);
            }
        }
        if (loopIncoming.empty()) continue;
        if (!fromLoop || !isLoopInvariant(fromLoop, loop.blocks)) return false;
        for (unsigned idx : loopIncoming)
            phi->remove_operands(static_cast<int>(idx),
                                 static_cast<int>(idx + 1));
    }

    preTerm->set_operand(static_cast<unsigned>(headerOperand), exit);
    preheader->remove_succ_basic_block(loop.header);
    preheader->add_succ_basic_block(exit);
    loop.header->remove_pre_basic_block(preheader);
    exit->add_pre_basic_block(preheader);

    std::vector<BasicBlock *> deadBlocks(loop.blocksOrdered.begin(),
                                         loop.blocksOrdered.end());
    for (auto *bb : deadBlocks)
        func->remove_bb(bb);
    return true;
}

} // namespace

void IdiomRecognize::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses IdiomRecognize::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func, AM);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool IdiomRecognize::runOnFunction(Function *func, AnalysisManager &AM) {
    if (func->basic_blocks_.empty()) return false;

    bool changed = false;
    bool progress = true;
    while (progress) {
        progress = false;
        LoopInfo &LI = AM.getLoopInfo(func);

        std::vector<Loop *> loops;
        for (auto &l : LI.allLoops())
            loops.push_back(l.get());

        std::sort(loops.begin(), loops.end(),
                  [](Loop *a, Loop *b) { return a->depth < b->depth; });
        for (auto *loop : loops) {
            if (loop->children.size() != 1) continue;

            MemsetMatch match;
            if (!tryMatchNestedMemset2D(*loop, *loop->children[0], match))
                continue;

            Function *memsetDecl =
                getOrInsertLibFunc(func->parent_, LibFunc::Memset);
            if (!lowerMemsetLoop(*loop, match, func->parent_, memsetDecl, func))
                continue;

            changed = true;
            progress = true;
            func->set_instr_name();
            AM.invalidateFunction(func, PreservedAnalyses::none());
            break;
        }
        if (progress) continue;

        std::sort(loops.begin(), loops.end(),
                  [](Loop *a, Loop *b) { return a->depth > b->depth; });
        for (auto *loop : loops) {
            if (hasNestedChildLoop(*loop, LI)) continue;

            MemcpyMatch memcpyMatch;
            if (tryMatchMemcpyLoop(*loop, memcpyMatch)) {
                Function *memcpyDecl =
                    getOrInsertLibFunc(func->parent_, LibFunc::Memcpy);
                if (lowerMemcpyLoop(*loop, memcpyMatch, func->parent_, memcpyDecl,
                                     func)) {
                    changed = true;
                    progress = true;
                    func->set_instr_name();
                    AM.invalidateFunction(func, PreservedAnalyses::none());
                    break;
                }
            }

            MemsetMatch match;
            if (!tryMatchMemsetLoop(*loop, match)) continue;

            Function *memsetDecl =
                getOrInsertLibFunc(func->parent_, LibFunc::Memset);
            if (!lowerMemsetLoop(*loop, match, func->parent_, memsetDecl, func))
                continue;

            changed = true;
            progress = true;
            func->set_instr_name();
            AM.invalidateFunction(func, PreservedAnalyses::none());
            break;
        }
    }
    return changed;
}
