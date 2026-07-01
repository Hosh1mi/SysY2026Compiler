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
#include <functional>
#include <iostream>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

bool debugEnabled() {
    return std::getenv("DEBUG_LOOP_INTERCHANGE") != nullptr;
}

void collectAccesses(Loop *L, std::vector<Instruction *> &accs,
                     std::vector<GetElementPtrInst *> &geps) {
    for (auto *bb : L->blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            Value *ptr = nullptr;
            if (inst->is_load())       ptr = inst->get_operand(0);
            else if (inst->is_store()) ptr = inst->get_operand(1);
            else continue;
            accs.push_back(inst);
            if (auto *g = dynamic_cast<GetElementPtrInst *>(ptr))
                geps.push_back(g);
        }
    }
}

Loop *deepestCanonicalDescendant(Loop *K) {
    Loop *best = nullptr;
    int bestDepth = -1;
    std::function<void(Loop *)> dfs = [&](Loop *L) {
        for (auto *c : L->children) {
            if (c->hasCanonicalIV() && c->depth > bestDepth) {
                best = c;
                bestDepth = c->depth;
            }
            dfs(c);
        }
    };
    dfs(K);
    return best;
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
    Module  *module = func->parent_;
    Type    *i32    = module->int32_ty_;
    PhiInst *kIV    = K->canonicalIV;
    Value   *kBound = K->tripCount;
    BasicBlock *kHeader = K->header;
    BasicBlock *kPre    = K->preheader;
    BasicBlock *kLatch  = K->singleLatch();
    BasicBlock *kExit   = K->singleExit();
    if (!kIV || !kBound || !kHeader || !kPre || !kLatch || !kExit) return false;

    auto *kbr = dynamic_cast<BranchInst *>(kHeader->get_terminator());
    if (!kbr || kbr->num_ops_ != 3) return false;
    auto *t1 = dynamic_cast<BasicBlock *>(kbr->get_operand(1));
    auto *t2 = dynamic_cast<BasicBlock *>(kbr->get_operand(2));
    BasicBlock *bodyEntry = K->blocks.count(t1) ? t1 : (K->blocks.count(t2) ? t2 : nullptr);
    if (!bodyEntry) return false;

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
    if (storeBlocks.empty()) return false;

    for (auto *w : unionW)
        if (!isCloneableType(w)) return false;
  
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
            if (!inW.count(inst)) return false;   
        }
        if (!seenLast) return false;
    }

    for (auto *w : unionW) {
        if (w->is_store()) continue;
        for (auto &u : w->use_list_) {
            auto *user = dynamic_cast<Instruction *>(u.val_);
            if (!user || !unionW.count(user)) return false;
        }
    }
    
    for (auto &u : kIV->use_list_) {
        auto *user = dynamic_cast<Instruction *>(u.val_);
        if (!user) return false;
        if (unionW.count(user)) continue;
        if (user->parent_ == kHeader || user->parent_ == kLatch) continue;
        return false;
    }
  
    for (auto *w : unionW) {
        for (unsigned i = 0; i < w->num_ops_; i++) {
            auto *op = dynamic_cast<Instruction *>(w->get_operand(i));
            if (!op || op == kIV || unionW.count(op)) continue;
            if (!func->dominates(op->parent_, w->parent_)) return false;
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
        if (!origTerm || !origTerm->isTerminator()) return false;

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
            if (!ok) return false;   
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
    if (!pending.empty()) return false;   
    
    replaceBranchTarget(kPre, kHeader, bodyEntry);
    replaceBranchTarget(kLatch, kHeader, kExit);

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
        CostModel          CM(AA);

        auto dbg = [&](Loop *K, const char *why) {
            if (debugEnabled())
                std::cerr << "[LoopInterchange] reject K=" << func->name_ << "/"
                          << (K->header ? K->header->name_ : "?")
                          << " depth=" << K->depth << ": " << why << "\n";
        };

        Loop *target = nullptr;
        for (auto &Lp : LI.allLoops()) {
            Loop *K = Lp.get();
            if (K->children.empty())  continue;
            if (!K->hasCanonicalIV()) { dbg(K, "no canonical IV"); continue; }
            if (!K->preheader || !K->singleLatch() || !K->singleExit()) {
                dbg(K, "not single pre/latch/exit"); continue;
            }
            
            bool scalarCarried = false;
            for (auto *inst : K->header->instr_list_) {
                if (!inst->is_phi()) break;
                if (inst != K->canonicalIV) { scalarCarried = true; break; }
            }
            if (scalarCarried) { dbg(K, "carries scalar reduction"); continue; }
            std::vector<Instruction *>       accs;
            std::vector<GetElementPtrInst *> geps;
            collectAccesses(K, accs, geps);
            if (!DA.isLoopParallel(K, accs)) { dbg(K, "not parallel"); continue; }

            Loop *M = deepestCanonicalDescendant(K);
            if (!M) { dbg(K, "no canonical descendant"); continue; }
            long before = CM.totalStride(geps, M->canonicalIV);
            long after  = CM.totalStride(geps, K->canonicalIV);
            if (before < 0 || after < 0) { dbg(K, "stride unknown"); continue; }
            if (!(after < before))       { dbg(K, "not profitable"); continue; }

            if (debugEnabled())
                std::cerr << "[LoopInterchange] sink candidate K=" << func->name_ << "/"
                          << K->header->name_ << " stride " << before << "->" << after << "\n";
            target = K;
            break;
        }

        if (!target) break;
        if (!applyParallelSink(func, target)) {
            if (debugEnabled())
                std::cerr << "[LoopInterchange] applyParallelSink bailed\n";
            break;   
        }
        everChanged = true;
    }
    return everChanged;
}
