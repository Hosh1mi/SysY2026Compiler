#include "../../include/mid/opt/triangularLoopInterchange.hpp"
#include "../../include/mid/opt/cfgUtils.hpp"
#include "../../include/mid/ir/basicBlock.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/module.hpp"

#include <cstdlib>
#include <iostream>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

struct Candidate {
    BasicBlock *pre = nullptr;
    BasicBlock *outerH = nullptr;
    BasicBlock *midPre = nullptr;
    BasicBlock *midH = nullptr;
    BasicBlock *pivotB = nullptr;
    BasicBlock *innerPre = nullptr;
    BasicBlock *innerB = nullptr;
    BasicBlock *outerLatch = nullptr;
    BasicBlock *outerExit = nullptr;

    PhiInst *kPhi = nullptr;
    PhiInst *iPhi = nullptr;
    PhiInst *jPhi = nullptr;
    Value *bound = nullptr;
    Value *iNext = nullptr;
    Value *jNext = nullptr;

    StoreInst *pivotStore = nullptr;
    StoreInst *updateStore = nullptr;
    GetElementPtrInst *pivotPtr = nullptr;
    GetElementPtrInst *updatePtr = nullptr;
    ICmpInst *pivotGuard = nullptr;
    ICmpInst *innerGuard = nullptr;
};

using ValMap = std::unordered_map<Value *, Value *>;

bool debugEnabled() {
    return std::getenv("DEBUG_TRIANGULAR_INTERCHANGE") != nullptr;
}

bool reject(BasicBlock *bb, const char *reason) {
    if (debugEnabled() && bb)
        std::cerr << "[TriangularLoopInterchange] reject " << bb->name_
                  << ": " << reason << "\n";
    return false;
}

bool isZero(Value *v) {
    auto *ci = dynamic_cast<ConstantInt *>(v);
    return ci && ci->value_ == 0;
}

bool sameValue(Value *a, Value *b) {
    if (a == b) return true;
    auto *ai = dynamic_cast<ConstantInt *>(a);
    auto *bi = dynamic_cast<ConstantInt *>(b);
    return ai && bi && ai->value_ == bi->value_;
}

ConstantInt *one(Module *m) {
    return new ConstantInt(m->int32_ty_, 1);
}

ConstantInt *zero(Module *m) {
    return new ConstantInt(m->int32_ty_, 0);
}

BranchInst *asBranch(BasicBlock *bb) {
    return dynamic_cast<BranchInst *>(bb ? bb->get_terminator() : nullptr);
}

ICmpInst *asSltCmp(Value *v) {
    auto *cmp = dynamic_cast<ICmpInst *>(v);
    if (!cmp || cmp->icmp_op_ != ICmpInst::ICMP_SLT) return nullptr;
    return cmp;
}

bool isAddOneOf(Value *v, Value *base) {
    auto *add = dynamic_cast<BinaryInst *>(v);
    if (!add || !add->is_add()) return false;
    auto *c0 = dynamic_cast<ConstantInt *>(add->get_operand(0));
    auto *c1 = dynamic_cast<ConstantInt *>(add->get_operand(1));
    return (add->get_operand(0) == base && c1 && c1->value_ == 1) ||
           (add->get_operand(1) == base && c0 && c0->value_ == 1);
}

Value *incomingFrom(PhiInst *phi, BasicBlock *pred) {
    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
        if (phi->get_operand(i + 1) == pred)
            return phi->get_operand(i);
    }
    return nullptr;
}

PhiInst *firstIntPhi(BasicBlock *bb) {
    for (auto *inst : bb->instr_list_) {
        if (!inst->is_phi()) break;
        if (inst->type_->tid_ == Type::IntegerTyID)
            return static_cast<PhiInst *>(inst);
    }
    return nullptr;
}

bool hasPhi(BasicBlock *bb) {
    return bb && !bb->instr_list_.empty() && bb->instr_list_.front()->is_phi();
}

bool replaceSuccessor(BasicBlock *pred, BasicBlock *oldSucc, BasicBlock *newSucc) {
    auto *term = pred->get_terminator();
    if (!term) return false;
    bool replaced = false;
    for (unsigned i = 0; i < term->num_ops_; ++i) {
        if (term->get_operand(i) == oldSucc) {
            term->set_operand(i, newSucc);
            replaced = true;
        }
    }
    if (!replaced) return false;
    pred->remove_succ_basic_block(oldSucc);
    oldSucc->remove_pre_basic_block(pred);
    pred->add_succ_basic_block(newSucc);
    newSucc->add_pre_basic_block(pred);
    return true;
}

bool blockHasOnlyUncondBranch(BasicBlock *bb, BasicBlock *target) {
    auto *br = asBranch(bb);
    return br && br->num_ops_ == 1 && br->get_operand(0) == target;
}

bool isEmptyJumpBlock(BasicBlock *bb) {
    if (!bb) return false;
    for (auto *inst : bb->instr_list_) {
        if (inst->is_phi()) return false;
        if (inst->isTerminator()) {
            auto *br = dynamic_cast<BranchInst *>(inst);
            return br && br->num_ops_ == 1;
        }
        return false;
    }
    return false;
}

BasicBlock *skipEmptyJumps(BasicBlock *bb) {
    std::set<BasicBlock *> seen;
    while (isEmptyJumpBlock(bb) && seen.insert(bb).second) {
        auto *br = asBranch(bb);
        bb = dynamic_cast<BasicBlock *>(br->get_operand(0));
    }
    return bb;
}

std::vector<StoreInst *> storesIn(BasicBlock *bb) {
    std::vector<StoreInst *> stores;
    for (auto *inst : bb->instr_list_) {
        if (inst->is_store())
            stores.push_back(static_cast<StoreInst *>(inst));
    }
    return stores;
}

bool hasCallOrRet(BasicBlock *bb) {
    for (auto *inst : bb->instr_list_) {
        if (inst->is_call() || inst->is_ret())
            return true;
    }
    return false;
}

bool isCloneableNonControlInst(Instruction *inst) {
    return dynamic_cast<BinaryInst *>(inst) ||
           dynamic_cast<UnaryInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst) ||
           dynamic_cast<FCmpInst *>(inst) ||
           dynamic_cast<GetElementPtrInst *>(inst) ||
           dynamic_cast<LoadInst *>(inst) ||
           dynamic_cast<ZextInst *>(inst) ||
           dynamic_cast<FpToSiInst *>(inst) ||
           dynamic_cast<SiToFpInst *>(inst) ||
           dynamic_cast<Bitcast *>(inst);
}

bool blockIsCloneable(BasicBlock *bb) {
    for (auto *inst : bb->instr_list_) {
        if (inst->is_phi() || inst->isTerminator() || inst->is_store())
            continue;
        if (!isCloneableNonControlInst(inst))
            return false;
    }
    return true;
}

bool gepUsesOnlyLastDim(Value *ptr, Value *kPhi) {
    auto *gep = dynamic_cast<GetElementPtrInst *>(ptr);
    if (!gep) return true;
    for (unsigned i = 1; i < gep->num_ops_; ++i) {
        if (gep->get_operand(i) != kPhi) continue;
        if (i + 1 != gep->num_ops_)
            return false;
    }
    return true;
}

bool blockGEPsUseOnlyLastDim(BasicBlock *bb, Value *kPhi) {
    for (auto *inst : bb->instr_list_) {
        if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst)) {
            if (!gepUsesOnlyLastDim(gep, kPhi))
                return false;
        }
    }
    return true;
}

bool containsLoadFrom(Value *v, Value *ptr, std::set<Value *> &seen) {
    if (!seen.insert(v).second) return false;
    auto *load = dynamic_cast<LoadInst *>(v);
    if (load && load->get_operand(0) == ptr)
        return true;
    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst) return false;
    for (unsigned i = 0; i < inst->num_ops_; ++i) {
        if (containsLoadFrom(inst->get_operand(i), ptr, seen))
            return true;
    }
    return false;
}

bool containsLoadFrom(Value *v, Value *ptr) {
    std::set<Value *> seen;
    return containsLoadFrom(v, ptr, seen);
}

bool matchStorePointers(Candidate &c) {
    c.pivotPtr = dynamic_cast<GetElementPtrInst *>(c.pivotStore->get_operand(1));
    c.updatePtr = dynamic_cast<GetElementPtrInst *>(c.updateStore->get_operand(1));
    if (!c.pivotPtr || !c.updatePtr) return false;
    if (c.pivotPtr->num_ops_ != c.updatePtr->num_ops_) return false;
    if (c.pivotPtr->num_ops_ < 3) return false;
    if (c.pivotPtr->get_operand(0) != c.updatePtr->get_operand(0)) return false;

    unsigned row = c.pivotPtr->num_ops_ - 2;
    unsigned col = c.pivotPtr->num_ops_ - 1;
    if (c.pivotPtr->get_operand(row) != c.iPhi) return false;
    if (c.pivotPtr->get_operand(col) != c.kPhi) return false;
    if (c.updatePtr->get_operand(row) != c.jPhi) return false;
    if (c.updatePtr->get_operand(col) != c.kPhi) return false;
    for (unsigned i = 1; i < row; ++i) {
        if (!sameValue(c.pivotPtr->get_operand(i), c.updatePtr->get_operand(i)))
            return false;
    }

    return containsLoadFrom(c.updateStore->get_operand(0), c.updatePtr);
}

bool matchCandidate(BasicBlock *outerH, Candidate &c) {
    c.outerH = outerH;
    auto *outerBr = asBranch(outerH);
    if (!outerBr || outerBr->num_ops_ != 3) return reject(outerH, "not conditional outer branch");
    auto *outerCmp = asSltCmp(outerBr->get_operand(0));
    if (!outerCmp) return reject(outerH, "outer cmp not slt");
    c.kPhi = dynamic_cast<PhiInst *>(outerCmp->get_operand(0));
    if (!c.kPhi || c.kPhi->parent_ != outerH) return reject(outerH, "outer iv not header phi");
    c.bound = outerCmp->get_operand(1);
    c.midPre = dynamic_cast<BasicBlock *>(outerBr->get_operand(1));
    c.midH = skipEmptyJumps(c.midPre);
    c.outerExit = dynamic_cast<BasicBlock *>(outerBr->get_operand(2));
    if (!c.midPre || !c.midH || !c.outerExit || hasPhi(c.outerExit)) return reject(outerH, "bad outer successors");

    BasicBlock *outerLatch = nullptr;
    BasicBlock *pre = nullptr;
    for (unsigned i = 0; i + 1 < c.kPhi->num_ops_; i += 2) {
        auto *pred = dynamic_cast<BasicBlock *>(c.kPhi->get_operand(i + 1));
        Value *val = c.kPhi->get_operand(i);
        if (isZero(val)) pre = pred;
        else if (isAddOneOf(val, c.kPhi)) outerLatch = pred;
    }
    if (!pre || !outerLatch) return reject(outerH, "outer phi incomings not zero/add");
    auto *outerLatchBr = asBranch(outerLatch);
    if (!outerLatchBr || outerLatchBr->num_ops_ != 1 ||
        outerLatchBr->get_operand(0) != outerH || hasCallOrRet(outerLatch) ||
        !storesIn(outerLatch).empty())
        return reject(outerH, "bad outer latch");
    c.pre = pre;
    c.outerLatch = outerLatch;

    auto *midBr = asBranch(c.midH);
    if (!midBr || midBr->num_ops_ != 3) return reject(outerH, "not conditional middle branch");
    auto *midCmp = asSltCmp(midBr->get_operand(0));
    if (!midCmp || midCmp->get_operand(1) != c.bound) return reject(outerH, "middle cmp mismatch");
    c.iPhi = dynamic_cast<PhiInst *>(midCmp->get_operand(0));
    if (!c.iPhi || c.iPhi->parent_ != c.midH) return reject(outerH, "middle iv not header phi");
    if (!isZero(incomingFrom(c.iPhi, c.midPre))) return reject(outerH, "middle init not zero");
    c.iNext = nullptr;
    for (unsigned i = 0; i + 1 < c.iPhi->num_ops_; i += 2) {
        auto *pred = dynamic_cast<BasicBlock *>(c.iPhi->get_operand(i + 1));
        Value *val = c.iPhi->get_operand(i);
        if (pred == c.midPre) continue;
        if (!isAddOneOf(val, c.iPhi)) return reject(outerH, "middle latch value not add one");
        if (!c.iNext) c.iNext = val;
        else if (c.iNext != val) return reject(outerH, "middle latch values differ");
    }
    if (!c.iNext) return reject(outerH, "missing middle next");
    c.pivotB = dynamic_cast<BasicBlock *>(midBr->get_operand(1));
    if (skipEmptyJumps(dynamic_cast<BasicBlock *>(midBr->get_operand(2))) != outerLatch) return reject(outerH, "middle exit not outer latch");
    if (!c.pivotB) return reject(outerH, "missing pivot block");

    auto *pivotBr = asBranch(c.pivotB);
    if (!pivotBr || pivotBr->num_ops_ != 3) return reject(outerH, "pivot branch not conditional");
    c.pivotGuard = asSltCmp(pivotBr->get_operand(0));
    if (!c.pivotGuard || c.pivotGuard->get_operand(0) != c.iNext ||
        c.pivotGuard->get_operand(1) != c.bound)
        return reject(outerH, "pivot guard mismatch");
    auto *pSucc1 = dynamic_cast<BasicBlock *>(pivotBr->get_operand(1));
    auto *pSucc2 = dynamic_cast<BasicBlock *>(pivotBr->get_operand(2));
    if (skipEmptyJumps(pSucc1) == c.midH) {
        c.innerPre = pSucc2;
        c.innerB = skipEmptyJumps(pSucc2);
    } else if (skipEmptyJumps(pSucc2) == c.midH) {
        c.innerPre = pSucc1;
        c.innerB = skipEmptyJumps(pSucc1);
    }
    else return reject(outerH, "pivot successors mismatch");
    if (!c.innerB) return reject(outerH, "missing inner block");

    auto *innerBr = asBranch(c.innerB);
    if (!innerBr || innerBr->num_ops_ != 3) return reject(outerH, "inner branch not conditional");
    c.innerGuard = asSltCmp(innerBr->get_operand(0));
    if (!c.innerGuard || c.innerGuard->get_operand(1) != c.bound) return reject(outerH, "inner guard mismatch");
    auto *iSucc1 = dynamic_cast<BasicBlock *>(innerBr->get_operand(1));
    auto *iSucc2 = dynamic_cast<BasicBlock *>(innerBr->get_operand(2));
    if (!((skipEmptyJumps(iSucc1) == c.innerB && skipEmptyJumps(iSucc2) == c.midH) ||
          (skipEmptyJumps(iSucc2) == c.innerB && skipEmptyJumps(iSucc1) == c.midH)))
        return reject(outerH, "inner successors mismatch");

    c.jPhi = firstIntPhi(c.innerB);
    if (!c.jPhi) return reject(outerH, "missing j phi");
    if (incomingFrom(c.jPhi, c.innerPre) != c.iNext) return reject(outerH, "j init mismatch");
    c.jNext = incomingFrom(c.jPhi, c.innerB);
    if (!c.jNext || !isAddOneOf(c.jNext, c.jPhi)) return reject(outerH, "j next mismatch");
    if (c.innerGuard->get_operand(0) != c.jNext) return reject(outerH, "inner guard does not test j next");

    if (hasCallOrRet(c.pivotB) || hasCallOrRet(c.innerB) ||
        !blockIsCloneable(c.pivotB) || !blockIsCloneable(c.innerB) ||
        !blockGEPsUseOnlyLastDim(c.pivotB, c.kPhi) ||
        !blockGEPsUseOnlyLastDim(c.innerB, c.kPhi))
        return reject(outerH, "unsafe body contents");

    auto pivotStores = storesIn(c.pivotB);
    auto updateStores = storesIn(c.innerB);
    if (pivotStores.size() != 1 || updateStores.size() != 1) return reject(outerH, "store count mismatch");
    c.pivotStore = pivotStores[0];
    c.updateStore = updateStores[0];
    if (!matchStorePointers(c)) return reject(outerH, "store pointer mismatch");
    return true;
}

Value *cloneValue(Value *v, BasicBlock *dest, ValMap &vm,
                  const std::set<Instruction *> &skip, bool &ok);

Instruction *cloneInst(Instruction *orig, BasicBlock *dest, ValMap &vm,
                       const std::set<Instruction *> &skip, bool &ok) {
    if (vm.count(orig)) return dynamic_cast<Instruction *>(vm[orig]);
    if (skip.count(orig)) {
        ok = false;
        return nullptr;
    }

    Instruction *cl = nullptr;
    if (auto *bi = dynamic_cast<BinaryInst *>(orig)) {
        cl = new BinaryInst(bi->type_, bi->op_id_,
                            cloneValue(bi->get_operand(0), dest, vm, skip, ok),
                            cloneValue(bi->get_operand(1), dest, vm, skip, ok),
                            dest);
    } else if (auto *ui = dynamic_cast<UnaryInst *>(orig)) {
        cl = new UnaryInst(ui->type_, ui->op_id_,
                           cloneValue(ui->get_operand(0), dest, vm, skip, ok),
                           dest);
    } else if (auto *ci = dynamic_cast<ICmpInst *>(orig)) {
        cl = new ICmpInst(ci->icmp_op_,
                          cloneValue(ci->get_operand(0), dest, vm, skip, ok),
                          cloneValue(ci->get_operand(1), dest, vm, skip, ok),
                          dest);
    } else if (auto *fi = dynamic_cast<FCmpInst *>(orig)) {
        cl = new FCmpInst(fi->fcmp_op_,
                          cloneValue(fi->get_operand(0), dest, vm, skip, ok),
                          cloneValue(fi->get_operand(1), dest, vm, skip, ok),
                          dest);
    } else if (auto *gi = dynamic_cast<GetElementPtrInst *>(orig)) {
        std::vector<Value *> idxs;
        for (unsigned i = 1; i < gi->num_ops_; ++i)
            idxs.push_back(cloneValue(gi->get_operand(i), dest, vm, skip, ok));
        cl = new GetElementPtrInst(cloneValue(gi->get_operand(0), dest, vm, skip, ok),
                                   idxs, dest);
    } else if (auto *li = dynamic_cast<LoadInst *>(orig)) {
        cl = new LoadInst(cloneValue(li->get_operand(0), dest, vm, skip, ok), dest);
    } else if (auto *zi = dynamic_cast<ZextInst *>(orig)) {
        cl = new ZextInst(zi->op_id_, cloneValue(zi->get_operand(0), dest, vm, skip, ok),
                          zi->dest_ty_, dest);
    } else if (auto *fp = dynamic_cast<FpToSiInst *>(orig)) {
        cl = new FpToSiInst(fp->op_id_, cloneValue(fp->get_operand(0), dest, vm, skip, ok),
                            fp->dest_ty_, dest);
    } else if (auto *sf = dynamic_cast<SiToFpInst *>(orig)) {
        cl = new SiToFpInst(sf->op_id_, cloneValue(sf->get_operand(0), dest, vm, skip, ok),
                            sf->dest_ty_, dest);
    } else if (auto *bc = dynamic_cast<Bitcast *>(orig)) {
        cl = new Bitcast(bc->op_id_, cloneValue(bc->get_operand(0), dest, vm, skip, ok),
                         bc->dest_ty_, dest);
    } else {
        ok = false;
        return nullptr;
    }

    if (!ok) return nullptr;
    vm[orig] = cl;
    return cl;
}

Value *cloneValue(Value *v, BasicBlock *dest, ValMap &vm,
                  const std::set<Instruction *> &skip, bool &ok) {
    auto it = vm.find(v);
    if (it != vm.end()) return it->second;
    if (dynamic_cast<Constant *>(v) || dynamic_cast<Argument *>(v) ||
        dynamic_cast<GlobalVariable *>(v) || dynamic_cast<BasicBlock *>(v))
        return v;
    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst || inst->is_phi() || inst->is_store() || inst->is_br() ||
        inst->is_ret() || inst->is_call() || inst->is_alloca()) {
        ok = false;
        return v;
    }
    return cloneInst(inst, dest, vm, skip, ok);
}

bool cloneBlockBody(BasicBlock *src, BasicBlock *dest, ValMap &vm,
                    const std::set<Instruction *> &skip) {
    bool ok = true;
    for (auto *inst : src->instr_list_) {
        if (inst->is_phi() || inst->isTerminator() || skip.count(inst))
            continue;
        if (auto *st = dynamic_cast<StoreInst *>(inst)) {
            new StoreInst(cloneValue(st->get_operand(0), dest, vm, skip, ok),
                          cloneValue(st->get_operand(1), dest, vm, skip, ok), dest);
        } else {
            cloneInst(inst, dest, vm, skip, ok);
        }
        if (!ok) return false;
    }
    return true;
}

bool applyTransform(Candidate &c) {
    Function *func = c.outerH->parent_;
    Module *module = func->parent_;
    auto *i32 = module->int32_ty_;

    auto bbNum = [&]() { return std::to_string((int)func->basic_blocks_.size() + 1000); };
    auto newBB = [&](const std::string &tag) {
        return new BasicBlock(module, "tri_" + tag + "_" + bbNum(), func);
    };

    BasicBlock *iH = newBB("iH");
    BasicBlock *pkH = newBB("pkH");
    BasicBlock *pkB = newBB("pkB");
    BasicBlock *pDone = newBB("pDone");
    BasicBlock *jH = newBB("jH");
    BasicBlock *ukH = newBB("ukH");
    BasicBlock *ukB = newBB("ukB");
    BasicBlock *jLatch = newBB("jLatch");
    BasicBlock *iLatch = newBB("iLatch");

    if (!replaceSuccessor(c.pre, c.outerH, iH))
        return false;

    auto *iPhi = PhiInst::create_phi(i32, iH);
    iPhi->add_phi_pair_operand(zero(module), c.pre);
    iH->add_instruction_front(iPhi);
    auto *iCmp = new ICmpInst(ICmpInst::ICMP_SLT, iPhi, c.bound, iH);
    new BranchInst(iCmp, pkH, c.outerExit, iH);

    auto *pkPhi = PhiInst::create_phi(i32, pkH);
    pkPhi->add_phi_pair_operand(zero(module), iH);
    pkH->add_instruction_front(pkPhi);
    auto *pkCmp = new ICmpInst(ICmpInst::ICMP_SLT, pkPhi, c.bound, pkH);
    new BranchInst(pkCmp, pkB, pDone, pkH);

    auto *pkNext = new BinaryInst(i32, Instruction::Add, pkPhi, one(module), pkB);
    {
        ValMap vm;
        vm[c.kPhi] = pkPhi;
        vm[c.iPhi] = iPhi;
        std::set<Instruction *> skip;
        skip.insert(dynamic_cast<Instruction *>(c.iNext));
        skip.insert(c.pivotGuard);
        if (!cloneBlockBody(c.pivotB, pkB, vm, skip))
            return false;
    }
    pkPhi->add_phi_pair_operand(pkNext, pkB);
    new BranchInst(pkH, pkB);

    auto *iNext = new BinaryInst(i32, Instruction::Add, iPhi, one(module), pDone);
    auto *hasJ = new ICmpInst(ICmpInst::ICMP_SLT, iNext, c.bound, pDone);
    new BranchInst(hasJ, jH, iLatch, pDone);

    auto *jPhi = PhiInst::create_phi(i32, jH);
    jPhi->add_phi_pair_operand(iNext, pDone);
    jH->add_instruction_front(jPhi);
    auto *jCmp = new ICmpInst(ICmpInst::ICMP_SLT, jPhi, c.bound, jH);
    new BranchInst(jCmp, ukH, iLatch, jH);

    auto *ukPhi = PhiInst::create_phi(i32, ukH);
    ukPhi->add_phi_pair_operand(zero(module), jH);
    ukH->add_instruction_front(ukPhi);
    auto *ukCmp = new ICmpInst(ICmpInst::ICMP_SLT, ukPhi, c.bound, ukH);
    new BranchInst(ukCmp, ukB, jLatch, ukH);

    auto *ukNext = new BinaryInst(i32, Instruction::Add, ukPhi, one(module), ukB);
    {
        ValMap vm;
        vm[c.kPhi] = ukPhi;
        vm[c.iPhi] = iPhi;
        vm[c.jPhi] = jPhi;
        std::set<Instruction *> skip;
        skip.insert(dynamic_cast<Instruction *>(c.jNext));
        skip.insert(c.innerGuard);
        if (!cloneBlockBody(c.innerB, ukB, vm, skip))
            return false;
    }
    ukPhi->add_phi_pair_operand(ukNext, ukB);
    new BranchInst(ukH, ukB);

    auto *jNext = new BinaryInst(i32, Instruction::Add, jPhi, one(module), jLatch);
    jPhi->add_phi_pair_operand(jNext, jLatch);
    new BranchInst(jH, jLatch);

    iPhi->add_phi_pair_operand(iNext, iLatch);
    new BranchInst(iH, iLatch);

    removeUnreachableBlocks(func);
    return true;
}

} // namespace

void TriangularLoopInterchange::execute(Module *module) {
    AnalysisManager *AM = nullptr;
    (void)AM;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

PreservedAnalyses TriangularLoopInterchange::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool TriangularLoopInterchange::runOnFunction(Function *func) {
    for (int iter = 0; iter < 8; ++iter) {
        bool changed = false;
        std::vector<BasicBlock *> blocks(func->basic_blocks_.begin(), func->basic_blocks_.end());
        for (auto *bb : blocks) {
            Candidate c;
            if (!matchCandidate(bb, c))
                continue;
            if (applyTransform(c)) {
                changed = true;
                break;
            }
        }
        if (!changed)
            return iter > 0;
    }
    return true;
}
