#include "../../../include/mid/opt/loopInterchange.hpp"
#include "../../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../../include/mid/opt/cfgUtils.hpp"
#include "../../../include/mid/ir/basicBlock.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/globalVariable.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/module.hpp"

#include <cstdlib>
#include <iostream>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

bool debugEnabled() {
    return std::getenv("DEBUG_LOOP_INTERCHANGE") != nullptr;
}

std::vector<Instruction *> storeSlice(BasicBlock *B) {
    std::set<Instruction *> inW;
    std::vector<Instruction *> work;
    for (auto *inst : B->instr_list_)
        if (inst->is_store() && inW.insert(inst).second) work.push_back(inst);
    while (!work.empty()) {
        auto *x = work.back();
        work.pop_back();
        for (unsigned i = 0; i < x->num_ops_; i++) {
            auto *op = dynamic_cast<Instruction *>(x->get_operand(i));
            if (!op || op->parent_ != B || op->is_phi()) continue;
            if (inW.insert(op).second) work.push_back(op);
        }
    }
    std::vector<Instruction *> W;
    for (auto *inst : B->instr_list_)
        if (inW.count(inst)) W.push_back(inst);
    return W;
}

bool isCloneableType(Instruction *inst) {
    return dynamic_cast<BinaryInst *>(inst) || dynamic_cast<UnaryInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst)   || dynamic_cast<FCmpInst *>(inst) ||
           dynamic_cast<GetElementPtrInst *>(inst) || dynamic_cast<LoadInst *>(inst) ||
           dynamic_cast<ZextInst *>(inst)   || dynamic_cast<FpToSiInst *>(inst) ||
           dynamic_cast<SiToFpInst *>(inst) || dynamic_cast<Bitcast *>(inst) ||
           dynamic_cast<StoreInst *>(inst);
}

bool isDiscardablePureInstruction(Instruction *inst) {
    return inst && !inst->isTerminator() && !inst->is_store() &&
           !inst->is_call();
}

bool isSafeOutsideSunkLoop(Instruction *inst) {
    // Instructions interleaved with the cloned store slice remain outside the
    // newly-created inner loop.  They must neither observe memory modified by
    // the slice nor have side effects.  Dependence closure puts every value
    // needed by the slice into the slice itself; the IV-use check below then
    // proves the remaining computations invariant with respect to the loop
    // being sunk.
    return inst && !inst->isTerminator() && !inst->is_load() &&
           !inst->is_store() && !inst->is_call();
}

bool latchHasSideEffects(BasicBlock *latch) {
    if (!latch) return true;
    for (auto *inst : latch->instr_list_) {
        if (inst->isTerminator()) continue;
        if (!isDiscardablePureInstruction(inst)) return true;
    }
    return false;
}

void deleteUnusedPureInstructions(BasicBlock *bb) {
    if (!bb) return;
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<Instruction *> instructions(bb->instr_list_.begin(),
                                                 bb->instr_list_.end());
        for (auto it = instructions.rbegin(); it != instructions.rend(); ++it) {
            Instruction *inst = *it;
            if (isDiscardablePureInstruction(inst) && inst->use_list_.empty()) {
                bb->delete_instr(inst);
                changed = true;
            }
        }
    }
}

using ValMap = std::unordered_map<Value *, Value *>;

Value *cloneValInto(Value *v, BasicBlock *dest, ValMap &vm,
                    const std::set<Instruction *> &unionW, PhiInst *kIV,
                    Value *localk, bool &ok);

Instruction *cloneInstInto(Instruction *orig, BasicBlock *dest, ValMap &vm,
                           const std::set<Instruction *> &unionW, PhiInst *kIV,
                           Value *localk, bool &ok) {
    auto it = vm.find(orig);
    if (it != vm.end()) return dynamic_cast<Instruction *>(it->second);
    auto C = [&](Value *v) { return cloneValInto(v, dest, vm, unionW, kIV, localk, ok); };

    Instruction *cl = nullptr;
    if (auto *bi = dynamic_cast<BinaryInst *>(orig)) {
        cl = new BinaryInst(bi->type_, bi->op_id_, C(bi->get_operand(0)),
                            C(bi->get_operand(1)), dest);
    } else if (auto *ui = dynamic_cast<UnaryInst *>(orig)) {
        cl = new UnaryInst(ui->type_, ui->op_id_, C(ui->get_operand(0)), dest);
    } else if (auto *ci = dynamic_cast<ICmpInst *>(orig)) {
        cl = new ICmpInst(ci->icmp_op_, C(ci->get_operand(0)), C(ci->get_operand(1)), dest);
    } else if (auto *fi = dynamic_cast<FCmpInst *>(orig)) {
        cl = new FCmpInst(fi->fcmp_op_, C(fi->get_operand(0)), C(fi->get_operand(1)), dest);
    } else if (auto *gi = dynamic_cast<GetElementPtrInst *>(orig)) {
        std::vector<Value *> idxs;
        for (unsigned i = 1; i < gi->num_ops_; i++) idxs.push_back(C(gi->get_operand(i)));
        cl = new GetElementPtrInst(C(gi->get_operand(0)), idxs, dest);
    } else if (auto *li = dynamic_cast<LoadInst *>(orig)) {
        cl = new LoadInst(C(li->get_operand(0)), dest);
    } else if (auto *st = dynamic_cast<StoreInst *>(orig)) {
        cl = new StoreInst(C(st->get_operand(0)), C(st->get_operand(1)), dest);
    } else if (auto *zi = dynamic_cast<ZextInst *>(orig)) {
        cl = new ZextInst(zi->op_id_, C(zi->get_operand(0)), zi->dest_ty_, dest);
    } else if (auto *fp = dynamic_cast<FpToSiInst *>(orig)) {
        cl = new FpToSiInst(fp->op_id_, C(fp->get_operand(0)), fp->dest_ty_, dest);
    } else if (auto *sf = dynamic_cast<SiToFpInst *>(orig)) {
        cl = new SiToFpInst(sf->op_id_, C(sf->get_operand(0)), sf->dest_ty_, dest);
    } else if (auto *bc = dynamic_cast<Bitcast *>(orig)) {
        cl = new Bitcast(bc->op_id_, C(bc->get_operand(0)), bc->dest_ty_, dest);
    } else {
        ok = false;
        return nullptr;
    }
    if (!ok) return nullptr;
    vm[orig] = cl;
    return cl;
}

Value *cloneValInto(Value *v, BasicBlock *dest, ValMap &vm,
                    const std::set<Instruction *> &unionW, PhiInst *kIV,
                    Value *localk, bool &ok) {
    if (v == kIV) return localk;
    auto it = vm.find(v);
    if (it != vm.end()) return it->second;
    if (dynamic_cast<Constant *>(v) || dynamic_cast<Argument *>(v) ||
        dynamic_cast<GlobalVariable *>(v) || dynamic_cast<BasicBlock *>(v))
        return v;
    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst) return v;
    if (unionW.count(inst))
        return cloneInstInto(inst, dest, vm, unionW, kIV, localk, ok);
    return v;   
}

void replaceBranchTarget(BasicBlock *pred, BasicBlock *oldT, BasicBlock *newT) {
    auto *term = pred->get_terminator();
    for (unsigned i = 0; i < term->num_ops_; i++)
        if (term->get_operand(i) == oldT) term->set_operand(i, newT);
    pred->remove_succ_basic_block(oldT);
    oldT->remove_pre_basic_block(pred);
    pred->add_succ_basic_block(newT);
    newT->add_pre_basic_block(pred);
}

void retargetPhiPred(BasicBlock *succ, BasicBlock *oldPred, BasicBlock *newPred) {
    for (auto *inst : succ->instr_list_) {
        if (!inst->is_phi()) break;
        for (unsigned i = 0; i + 1 < inst->num_ops_; i += 2)
            if (inst->get_operand(i + 1) == oldPred)
                inst->set_operand(i + 1, newPred);
    }
}

bool applyParallelSink(Function *func, Loop *K) {
    auto reject = [&](const char *reason) {
        if (debugEnabled())
            std::cerr << "[LoopInterchange] parallel sink rejected: "
                      << reason << "\n";
        return false;
    };
    Module  *module = func->parent_;
    Type    *i32    = module->int32_ty_;
    PhiInst *kIV    = K->canonicalIV;
    Value   *kBound = K->tripCount;
    BasicBlock *kHeader = K->header;
    BasicBlock *kPre    = K->preheader;
    BasicBlock *kLatch  = K->singleLatch();
    BasicBlock *kExit   = K->singleExit();
    if (!kIV || !kBound || !kHeader || !kPre || !kLatch || !kExit)
        return reject("incomplete canonical loop structure");
    if (kLatch == kHeader || latchHasSideEffects(kLatch)) {
        if (debugEnabled())
            std::cerr << "[LoopInterchange] parallel sink requires a distinct, "
                         "side-effect-free latch; header="
                      << kHeader->name_ << " latch=" << kLatch->name_ << "\n";
        return reject("latch is not safe to remove");
    }

    auto *kbr = dynamic_cast<BranchInst *>(kHeader->get_terminator());
    if (!kbr || kbr->num_ops_ != 3)
        return reject("header is not conditionally branched");
    auto *t1 = dynamic_cast<BasicBlock *>(kbr->get_operand(1));
    auto *t2 = dynamic_cast<BasicBlock *>(kbr->get_operand(2));
    BasicBlock *bodyEntry = K->blocks.count(t1) ? t1 : (K->blocks.count(t2) ? t2 : nullptr);
    if (!bodyEntry) return reject("loop body entry was not found");

    std::vector<BasicBlock *> storeBlocks;
    std::unordered_map<BasicBlock *, std::vector<Instruction *>> slices;
    std::set<Instruction *> unionW;
    for (auto *bb : K->blocksOrdered) {
        if (bb == kHeader || bb == kLatch) continue;
        bool hasStore = false;
        for (auto *inst : bb->instr_list_) if (inst->is_store()) { hasStore = true; break; }
        if (!hasStore) continue;
        auto W = storeSlice(bb);
        if (W.empty()) continue;
        storeBlocks.push_back(bb);
        slices[bb] = W;
        for (auto *w : W) unionW.insert(w);
    }
    if (storeBlocks.empty()) return reject("loop has no movable store slice");

    // A value feeding the store may have been hoisted to a nested-loop
    // preheader.  Such a value is still evaluated once per K iteration and
    // must therefore move with K.  Close the slice over non-phi definitions
    // inside K instead of relying on a particular block layout produced by
    // LICM.
    std::vector<Instruction *> dependencyWork(unionW.begin(), unionW.end());
    while (!dependencyWork.empty()) {
        Instruction *value = dependencyWork.back();
        dependencyWork.pop_back();
        for (unsigned i = 0; i < value->num_ops_; ++i) {
            auto *dependency =
                dynamic_cast<Instruction *>(value->get_operand(i));
            if (!dependency || !dependency->parent_ ||
                !K->blocks.count(dependency->parent_) || dependency->is_phi() ||
                dependency->isTerminator())
                continue;
            if (unionW.insert(dependency).second)
                dependencyWork.push_back(dependency);
        }
    }

    for (auto *w : unionW)
        if (!isCloneableType(w))
            return reject("store slice contains an unclonable instruction");
  
    for (auto *bb : storeBlocks) {
        auto &W = slices[bb];
        std::set<Instruction *> inW(W.begin(), W.end());
        Instruction *lastW = W.back();
        bool seenLast = false, passedPhi = false;
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi()) continue;
            passedPhi = true;
            (void)passedPhi;
            if (inst == lastW) { seenLast = true; break; }
            if (!inW.count(inst) && !isSafeOutsideSunkLoop(inst))
                return reject("store slice crosses a memory or side-effecting instruction");
        }
        if (!seenLast) return reject("store slice endpoint was not found");
    }

    for (auto *w : unionW) {
        if (w->is_store()) continue;
        for (auto &u : w->use_list_) {
            auto *user = dynamic_cast<Instruction *>(u.val_);
            if (!user || !unionW.count(user)) {
                if (debugEnabled()) {
                    std::cerr << "[LoopInterchange] escaping slice value="
                              << w->name_ << " op=" << w->op_id_
                              << " user="
                              << (user ? user->name_ : "<non-instruction>")
                              << " user-block="
                              << (user && user->parent_
                                      ? user->parent_->name_
                                      : "<none>")
                              << "\n  value-ir: " << w->print()
                              << "\n  user-ir: "
                              << (user ? user->print() : "<none>")
                              << "\n";
                }
                return reject("store slice value escapes the slice");
            }
        }
    }
    
    for (auto &u : kIV->use_list_) {
        auto *user = dynamic_cast<Instruction *>(u.val_);
        if (!user) return reject("induction has a non-instruction use");
        if (unionW.count(user)) continue;
        if (user->parent_ == kHeader || user->parent_ == kLatch) continue;
        if (debugEnabled())
            std::cerr << "[LoopInterchange] escaping induction user: "
                      << user->print() << "\n";
        return reject("induction is used outside loop control and store slices");
    }
  
    for (auto *w : unionW) {
        for (unsigned i = 0; i < w->num_ops_; i++) {
            auto *op = dynamic_cast<Instruction *>(w->get_operand(i));
            if (!op || op == kIV || unionW.count(op)) continue;
            if (!func->dominates(op->parent_, w->parent_))
                return reject("store slice operand does not dominate its use");
        }
    }
    
    auto bbNum = [&]() { return std::to_string((int)func->basic_blocks_.size() + 2000); };
    auto newBB = [&](const std::string &tag) {
        return new BasicBlock(module, "li_" + tag + "_" + bbNum(), func);
    };
    auto *c0 = new ConstantInt(i32, 0);
    auto *c1 = new ConstantInt(i32, 1);

    for (auto *B : storeBlocks) {
        auto &W = slices[B];
        Instruction *lastW = W.back();
        
        std::vector<Instruction *> tail;
        bool after = false;
        for (auto *inst : B->instr_list_) {
            if (after) tail.push_back(inst);
            if (inst == lastW) after = true;
        }
        auto *origTerm = tail.empty() ? nullptr : tail.back();
        if (!origTerm || !origTerm->isTerminator())
            return reject("store block has no movable terminator tail");

        BasicBlock *bTail = newBB("tail");
        std::vector<BasicBlock *> origSuccs;
        for (unsigned i = 0; i < origTerm->num_ops_; i++)
            if (auto *s = dynamic_cast<BasicBlock *>(origTerm->get_operand(i)))
                origSuccs.push_back(s);

        for (auto *inst : tail) { B->remove_instr(inst); bTail->add_instruction(inst); }
        
        for (auto *s : origSuccs) {
            B->remove_succ_basic_block(s);
            s->remove_pre_basic_block(B);
            bTail->add_succ_basic_block(s);
            s->add_pre_basic_block(bTail);
            retargetPhiPred(s, B, bTail);
        }
        
        BasicBlock *skH = newBB("kH");
        BasicBlock *skB = newBB("kB");
        BasicBlock *skL = newBB("kL");

        new BranchInst(skH, B);
        B->add_succ_basic_block(skH);
        skH->add_pre_basic_block(B);
        
        auto *localk = PhiInst::create_phi(i32, skH);
        localk->add_phi_pair_operand(c0, B);
        skH->add_instruction_front(localk);
        auto *cmp = new ICmpInst(ICmpInst::ICMP_SLT, localk, kBound, skH);
        new BranchInst(cmp, skB, bTail, skH);
        skH->add_pre_basic_block(skL);   
        
        ValMap vm;
        bool ok = true;
        for (auto *w : W) {
            cloneInstInto(w, skB, vm, unionW, kIV, localk, ok);
            if (!ok) return reject("failed to clone a store slice");
        }
        new BranchInst(skL, skB);
        
        auto *inc = new BinaryInst(i32, Instruction::Add, localk, c1, skL);
        localk->add_phi_pair_operand(inc, skL);
        new BranchInst(skH, skL);
    }

    std::set<Instruction *> pending = unionW;
    bool progress = true;
    while (progress && !pending.empty()) {
        progress = false;
        for (auto it = pending.begin(); it != pending.end();) {
            Instruction *w = *it;
            if (w->use_list_.empty()) {
                w->parent_->delete_instr(w);
                it = pending.erase(it);
                progress = true;
            } else {
                ++it;
            }
        }
    }
    if (!pending.empty())
        return reject("original store slice remains live after cloning");
    
    replaceBranchTarget(kPre, kHeader, bodyEntry);
    replaceBranchTarget(kLatch, kHeader, kExit);

    removeUnreachableBlocks(func);
    deleteUnusedPureInstructions(kLatch);
    return true;
}

// ── parallel float ──────────────────────────────────────────────────────
// Interchange K (outer, carries dependence) with M (inner, parallel).
//
// Before: for K { for M { body } }
// After:  for M { for K { body } }
//
// M must have a canonical header guard.  Its phi nodes and guard move to a
// new outer header (mOuter), while the old header becomes the entry to the
// interchanged body.  Guards that depend on K remain in their body blocks.

bool applyInterchange(Function *func, Loop *K, Loop *M) {
    Module *module = func->parent_;
    auto reject = [&](const char *reason) {
        if (debugEnabled())
            std::cerr << "[LoopInterchange] full interchange rejected: "
                      << reason << "\n";
        return false;
    };

    BasicBlock *kHeader = K->header;
    BasicBlock *kPre    = K->preheader;
    BasicBlock *kLatch  = K->singleLatch();
    BasicBlock *kExit   = K->singleExit();
    if (!kHeader || !kPre || !kLatch || !kExit)
        return reject("incomplete outer structure");

    BasicBlock *mHeader = M->header;
    BasicBlock *mPre    = M->preheader;
    BasicBlock *mLatch  = M->singleLatch();
    if (!mHeader || !mPre || !mLatch)
        return reject("incomplete inner structure");

    if (K->children.size() != 1)
        return reject("outer does not have exactly one child");

    auto hasPhi = [](BasicBlock *bb) -> bool {
        return !bb->instr_list_.empty() && bb->instr_list_.front()->is_phi();
    };
    if (hasPhi(kExit) || hasPhi(kLatch) || hasPhi(mLatch)) {
        if (debugEnabled())
            std::cerr << "[LoopInterchange] phi blocks outer-exit="
                      << kExit->name_ << ":" << hasPhi(kExit)
                      << " outer-latch=" << kLatch->name_ << ":"
                      << hasPhi(kLatch) << " inner-latch=" << mLatch->name_
                      << ":" << hasPhi(mLatch) << "\n";
        return reject("exit or latch contains phi nodes");
    }

    std::vector<PhiInst *> mPhis;
    for (auto *inst : mHeader->instr_list_) {
        if (!inst->is_phi()) break;
        mPhis.push_back(static_cast<PhiInst *>(inst));
    }
    if (mPhis.empty()) return reject("inner header has no phi nodes");

    auto *mBranch = dynamic_cast<BranchInst *>(mHeader->get_terminator());
    auto *mGuard = mBranch && mBranch->num_ops_ == 3
                       ? dynamic_cast<ICmpInst *>(mBranch->get_operand(0))
                       : nullptr;
    BasicBlock *mBodySucc = nullptr;
    BasicBlock *mExitSucc = nullptr;
    if (mBranch && mBranch->num_ops_ == 3) {
        for (unsigned i = 1; i < mBranch->num_ops_; ++i) {
            auto *succ = dynamic_cast<BasicBlock *>(mBranch->get_operand(i));
            if (!succ) continue;
            if (M->isInLoop(succ)) mBodySucc = succ;
            else                   mExitSucc = succ;
        }
    }
    PhiInst *mIV = M->canonicalIV;
    if (!mIV || !mGuard ||
        mGuard->icmp_op_ != ICmpInst::ICMP_SLT ||
        mGuard->get_operand(0) != mIV ||
        !mBodySucc || !mExitSucc)
        return reject("inner loop is not guarded by its canonical induction");

    for (auto *inst : mHeader->instr_list_) {
        if (inst->is_phi() || inst == mGuard || inst == mBranch) continue;
        return reject("inner header contains instructions outside its guard");
    }

    auto bbNum = [&]() { return std::to_string((int)func->basic_blocks_.size() + 3000); };

    auto *mOuter = new BasicBlock(module, "li_float_" + bbNum(), func);

    for (auto *phi : mPhis) {
        mHeader->remove_instr(phi);
        mOuter->add_instruction(phi);
    }

    // The inner loop guard becomes the guard of the new outer loop.  Keeping
    // it in the old header would merely skip the body once the inner IV is out
    // of range; the old outer loop would continue to advance forever.
    mHeader->remove_instr(mGuard);
    mOuter->add_instruction(mGuard);
    for (auto *succ : std::vector<BasicBlock *>(mHeader->succ_bbs_)) {
        mHeader->remove_succ_basic_block(succ);
        succ->remove_pre_basic_block(mHeader);
    }
    mHeader->delete_instr(mBranch);
    new BranchInst(mBodySucc, mHeader);
    new BranchInst(mGuard, mPre, kExit, mOuter);

    for (auto *phi : mPhis) {
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (phi->get_operand(i + 1) == mPre)
                phi->set_operand(i + 1, kPre);
    }
    for (auto *inst : kHeader->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (phi->get_operand(i + 1) == kPre)
                phi->set_operand(i + 1, mPre);
    }

    auto isInMExits = [&](BasicBlock *bb) -> bool {
        return std::find(M->exits.begin(), M->exits.end(), bb) != M->exits.end();
    };

    // 1. K_pre → mOuter (was → kHeader)
    replaceBranchTarget(kPre, kHeader, mOuter);

    // 2. K_header: body → mHeader, exit → mLatch
    {
        BasicBlock *bodySucc = nullptr, *exitSucc = nullptr;
        auto *term = kHeader->get_terminator();
        for (unsigned i = 1; i < term->num_ops_; i++) {
            auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
            if (!succ) continue;
            if (K->isInLoop(succ)) bodySucc = succ;
            else                   exitSucc = succ;
        }
        if (bodySucc) replaceBranchTarget(kHeader, bodySucc, mHeader);
        if (exitSucc) replaceBranchTarget(kHeader, exitSucc, mLatch);
    }

    // 3. M_pre: enter → kHeader, skip → mLatch
    {
        BasicBlock *enterSucc = nullptr, *skipSucc = nullptr;
        for (auto *succ : mPre->succ_bbs_) {
            if (isInMExits(succ)) skipSucc = succ;
            else                  enterSucc = succ;
        }
        if (enterSucc) replaceBranchTarget(mPre, enterSucc, kHeader);
        if (skipSucc)  replaceBranchTarget(mPre, skipSucc,  mLatch);
    }

    // 4. Blocks inside M that branch to mLatch → branch to kLatch
    {
        std::vector<BasicBlock *> predsToRetarget;
        for (auto *pred : mLatch->pre_bbs_) {
            if (pred == mLatch) continue;
            if (!M->isInLoop(pred)) continue;
            predsToRetarget.push_back(pred);
        }
        for (auto *pred : predsToRetarget)
            replaceBranchTarget(pred, mLatch, kLatch);
    }

    // 5. M_latch: continue → mOuter, exit → kExit
    {
        BasicBlock *continueSucc = nullptr, *exitSucc = nullptr;
        for (auto *succ : mLatch->succ_bbs_) {
            if (isInMExits(succ)) exitSucc = succ;
            else                  continueSucc = succ;
        }
        if (continueSucc) replaceBranchTarget(mLatch, continueSucc, mOuter);
        if (exitSucc)     replaceBranchTarget(mLatch, exitSucc,     kExit);
    }

    // Update phis in blocks whose predecessors changed
    retargetPhiPred(kExit, kHeader, mLatch);
    retargetPhiPred(kLatch, mLatch, mHeader);
    for (auto *phi : mPhis)
        retargetPhiPred(mPre, mHeader, mOuter);

    removeUnreachableBlocks(func);
    return true;
}
} // namespace

void LoopInterchange::execute(Module *module) {
    ArgumentAliasAnalysis argAA;
    argAA.analyze(module);
    argAA_ = &argAA;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
    argAA_ = nullptr;
}

PreservedAnalyses LoopInterchange::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    ArgumentAliasAnalysis argAA;
    argAA.analyze(module);
    argAA_ = &argAA;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    argAA_ = nullptr;
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool LoopInterchange::runOnFunction(Function *func) {
    bool everChanged = false;
    for (int iter = 0; iter < 16; iter++) {
        LoopInfo LI;
        LI.analyze(func);
        if (LI.allLoops().empty()) break;

        AffineAnalysis     AA(LI);
        DependenceAnalysis DA(LI, AA);
        DA.setArgAlias(argAA_);
        LoopAccessAnalysis LA(AA);
        CostModel          CM(AA);
        LoopInterchangeAnalysis IA(DA, LA, CM);

        auto dbg = [&](Loop *K, const char *why) {
            if (debugEnabled())
                std::cerr << "[LoopInterchange] reject K=" << func->name_ << "/"
                          << (K->header ? K->header->name_ : "?")
                          << " depth=" << K->depth << ": " << why << "\n";
        };

        Loop *target = nullptr;
        for (auto &Lp : LI.allLoops()) {
            Loop *K = Lp.get();
            ParallelSinkAnalysisResult analysis = IA.analyzeParallelSink(K);
            if (!analysis.accepted) {
                if (!K->children.empty()) dbg(K, analysis.reason);
                continue;
            }

            if (debugEnabled())
                std::cerr << "[LoopInterchange] sink candidate K=" << func->name_ << "/"
                          << K->header->name_ << " stride "
                          << analysis.cost.before << "->"
                          << analysis.cost.after << "\n";
            target = K;
            break;
        }

        if (target) {
            if (applyParallelSink(func, target)) {
                everChanged = true;
                continue;
            }
            if (debugEnabled())
                std::cerr << "[LoopInterchange] applyParallelSink bailed\n";
        }

        // Phase 2: parallel float — K carries dependence, M (child) is parallel.
        // Interchange K and M so the reduction loop moves closer to innermost,
        // reducing the reuse distance of the reduction variable.
        {
            Loop *floatK = nullptr;
            Loop *floatM = nullptr;
            for (auto &Lp : LI.allLoops()) {
                Loop *K = Lp.get();
                if (K->children.empty())  continue;
                if (!K->hasCanonicalIV()) continue;
                if (!K->preheader || !K->singleLatch() || !K->singleExit()) continue;

                bool scalarCarried = false;
                for (auto *inst : K->header->instr_list_) {
                    if (!inst->is_phi()) break;
                    if (inst != K->canonicalIV) { scalarCarried = true; break; }
                }
                if (scalarCarried) continue;

                LoopAccessInfo accessInfo = LA.collect(K);

                if (DA.isLoopParallel(K, accessInfo.memory_instructions)) continue;

                if (K->children.size() != 1) continue;
                Loop *M = K->children[0];
                if (!M->preheader || !M->singleLatch()) continue;

                if (!IA.isInterchangeLegal(K, M, accessInfo.memory_instructions)) {
                    dbg(K, "float: not legal");
                    continue;
                }

                bool profitable = !M->children.empty();
                if (!profitable) {
                    Loop *deepest = IA.deepestCanonicalDescendant(K);
                    if (deepest && deepest != K) {
                        LoopInterchangeCost cost = IA.estimateCost(
                            accessInfo.memory_geps, deepest->canonicalIV,
                            K->canonicalIV);
                        profitable = cost.profitable();
                    }
                }
                if (!profitable) { dbg(K, "float: not profitable"); continue; }

                if (debugEnabled())
                    std::cerr << "[LoopInterchange] float candidate K=" << func->name_ << "/"
                              << K->header->name_ << " M=" << M->header->name_ << "\n";
                floatK = K;
                floatM = M;
                break;
            }

            if (floatK) {
                if (applyInterchange(func, floatK, floatM)) {
                    everChanged = true;
                    continue;
                }
                if (debugEnabled())
                    std::cerr << "[LoopInterchange] applyInterchange bailed\n";
            }
        }

        break;
    }
    return everChanged;
}
