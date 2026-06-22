#include "../../include/mid/opt/loopUnroll.hpp"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <vector>

static const int UNROLL_FACTOR   = 4;
static const int MAX_LATCH_INSTS = 8; // skip unrolling if body is too large
static const int MAX_STRUCTURED_LOOP_INSTS = 24;

static bool debugStructuredReject(Function *func, Loop &loop,
                                  const char *reason) {
    if (std::getenv("DEBUG_LOOP_UNROLL")) {
        std::cerr << "[LoopUnroll] func=" << func->name_
                  << " header=" << (loop.header ? loop.header->name_ : "<none>")
                  << " structured-reject=" << reason << "\n";
    }
    return false;
}

static bool debugCFGRegionReject(Function *func, Loop &loop,
                                 const char *reason) {
    if (std::getenv("DEBUG_LOOP_UNROLL")) {
        std::cerr << "[LoopUnroll] func=" << func->name_
                  << " header=" << (loop.header ? loop.header->name_ : "<none>")
                  << " cfg-region-reject=" << reason << "\n";
    }
    return false;
}

// A two-way vector unroll initially keeps each iteration's store next to its
// computation.  When alias analysis proves that the first store cannot affect
// any load in the second iteration, sink it next to the second store.  This
// exposes both load/compute chains to the machine scheduler without changing
// memory order unless the crossed accesses are proven disjoint.
static bool clusterTwoVectorStores(BasicBlock *body,
                                   BasicAliasAnalysis &BAA) {
    if (!body) return false;

    std::vector<std::list<Instruction *>::iterator> stores;
    for (auto it = body->instr_list_.begin(); it != body->instr_list_.end(); ++it) {
        auto *store = dynamic_cast<StoreInst *>(*it);
        if (!store || store->get_operand(0)->type_->tid_ != Type::VectorTyID)
            continue;
        stores.push_back(it);
    }
    if (stores.size() != 2) return false;

    auto first = stores[0];
    auto second = stores[1];
    Value *storedPtr = (*first)->get_operand(1);
    for (auto it = std::next(first); it != second; ++it) {
        Instruction *inst = *it;
        if (inst->is_call() || inst->is_store()) return false;
        if (!inst->is_load()) continue;
        if (BAA.alias(storedPtr, inst->get_operand(0)) != AliasResult::NoAlias)
            return false;
    }

    body->instr_list_.splice(second, body->instr_list_, first);
    return true;
}

// ── Instruction cloning ───────────────────────────────────────────────────

Instruction *LoopUnroll::cloneInst(Instruction *orig, BasicBlock *destBB,
                                    const std::unordered_map<Value *, Value *> &vmap) {
    auto remap = [&](Value *v) -> Value * {
        auto it = vmap.find(v);
        return it != vmap.end() ? it->second : v;
    };

    if (auto *bi = dynamic_cast<BinaryInst *>(orig))
        {
            auto *inst = new BinaryInst(bi->type_, bi->op_id_,
                                        remap(bi->get_operand(0)),
                                        remap(bi->get_operand(1)), destBB);
            inst->copySemFlagsFrom(bi);
            return inst;
        }

    if (auto *ui = dynamic_cast<UnaryInst *>(orig))
        {
            auto *inst = new UnaryInst(ui->type_, ui->op_id_,
                                       remap(ui->get_operand(0)), destBB);
            inst->copySemFlagsFrom(ui);
            return inst;
        }

    if (auto *ci = dynamic_cast<ICmpInst *>(orig))
        {
            auto *inst = new ICmpInst(ci->icmp_op_,
                                      remap(ci->get_operand(0)),
                                      remap(ci->get_operand(1)), destBB);
            inst->copySemFlagsFrom(ci);
            return inst;
        }

    if (auto *fi = dynamic_cast<FCmpInst *>(orig))
        {
            auto *inst = new FCmpInst(fi->fcmp_op_,
                                      remap(fi->get_operand(0)),
                                      remap(fi->get_operand(1)), destBB);
            inst->copySemFlagsFrom(fi);
            return inst;
        }

    if (auto *gi = dynamic_cast<GetElementPtrInst *>(orig)) {
        std::vector<Value *> idxs;
        for (unsigned i = 1; i < gi->num_ops_; i++)
            idxs.push_back(remap(gi->get_operand(i)));
        auto *inst = new GetElementPtrInst(remap(gi->get_operand(0)), idxs, destBB);
        inst->copySemFlagsFrom(gi);
        return inst;
    }

    if (auto *li = dynamic_cast<LoadInst *>(orig))
        {
            auto *inst = new LoadInst(remap(li->get_operand(0)), destBB);
            inst->copySemFlagsFrom(li);
            return inst;
        }

    if (auto *si = dynamic_cast<StoreInst *>(orig))
        {
            auto *inst = new StoreInst(remap(si->get_operand(0)),
                                       remap(si->get_operand(1)), destBB);
            inst->copySemFlagsFrom(si);
            return inst;
        }

    if (auto *zi = dynamic_cast<ZextInst *>(orig))
        {
            auto *inst = new ZextInst(zi->op_id_, remap(zi->get_operand(0)),
                                      zi->dest_ty_, destBB);
            inst->copySemFlagsFrom(zi);
            return inst;
        }

    if (auto *fp = dynamic_cast<FpToSiInst *>(orig))
        {
            auto *inst = new FpToSiInst(fp->op_id_, remap(fp->get_operand(0)),
                                        fp->dest_ty_, destBB);
            inst->copySemFlagsFrom(fp);
            return inst;
        }

    if (auto *sf = dynamic_cast<SiToFpInst *>(orig))
        {
            auto *inst = new SiToFpInst(sf->op_id_, remap(sf->get_operand(0)),
                                        sf->dest_ty_, destBB);
            inst->copySemFlagsFrom(sf);
            return inst;
        }

    if (auto *bc = dynamic_cast<Bitcast *>(orig))
        {
            auto *inst = new Bitcast(bc->op_id_, remap(bc->get_operand(0)),
                                     bc->dest_ty_, destBB);
            inst->copySemFlagsFrom(bc);
            return inst;
        }

    return nullptr; // unsupported (phi, branch, call, alloca …)
}

static Value *mapLoopValue(
    Value *val,
    const std::unordered_map<Value *, Value *> &valueMap,
    const std::unordered_map<BasicBlock *, BasicBlock *> &bbMap) {
    if (auto it = valueMap.find(val); it != valueMap.end())
        return it->second;
    if (auto *bb = dynamic_cast<BasicBlock *>(val)) {
        auto bit = bbMap.find(bb);
        return bit == bbMap.end() ? nullptr : bit->second;
    }
    return val;
}

static bool isCloneableForUnroll(Instruction *inst) {
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

static bool hasEntryLowerBoundGuard(BasicBlock *preheader, BasicBlock *header,
                                    Value *bound, ConstantInt *init) {
    auto *br = dynamic_cast<BranchInst *>(preheader ? preheader->get_terminator() : nullptr);
    if (!br || !init) return false;
    if (br->num_ops_ == 1 && br->get_operand(0) == header &&
        preheader->pre_bbs_.size() == 1) {
        return hasEntryLowerBoundGuard(preheader->pre_bbs_[0], preheader,
                                       bound, init);
    }
    if (br->num_ops_ != 3) return false;
    auto *cmp = dynamic_cast<ICmpInst *>(br->get_operand(0));
    if (!cmp) return false;

    bool headerOnTrue = br->get_operand(1) == header;
    bool headerOnFalse = br->get_operand(2) == header;
    if (headerOnTrue == headerOnFalse) return false;

    auto sameConst = [&](Value *v) {
        auto *c = dynamic_cast<ConstantInt *>(v);
        return c && c->value_ == init->value_;
    };

    auto provesOnTrue = [&]() {
        if (cmp->icmp_op_ == ICmpInst::ICMP_SGE &&
            cmp->get_operand(0) == bound && sameConst(cmp->get_operand(1)))
            return true;
        if (cmp->icmp_op_ == ICmpInst::ICMP_SLE &&
            sameConst(cmp->get_operand(0)) && cmp->get_operand(1) == bound)
            return true;
        return false;
    };
    auto provesOnFalse = [&]() {
        if (cmp->icmp_op_ == ICmpInst::ICMP_SLT &&
            cmp->get_operand(0) == bound && sameConst(cmp->get_operand(1)))
            return true;
        if (cmp->icmp_op_ == ICmpInst::ICMP_SGT &&
            sameConst(cmp->get_operand(0)) && cmp->get_operand(1) == bound)
            return true;
        return false;
    };

    return (headerOnTrue && provesOnTrue()) || (headerOnFalse && provesOnFalse());
}

static bool hasNestedLoopBackedge(const Loop &loop) {
    for (auto *bb : loop.blocksOrdered) {
        auto *br = dynamic_cast<BranchInst *>(bb->get_terminator());
        if (!br) continue;
        for (unsigned i = 0; i < br->num_ops_; ++i) {
            auto *succ = dynamic_cast<BasicBlock *>(br->get_operand(i));
            if (!succ || succ == loop.header) continue;
            if (loop.blocks.count(succ))
                return true;
        }
    }
    return false;
}

static bool isProfitableCFGRegionUnroll(const Loop &loop, int bodyInstCount,
                                        int condBranchBlocks, int memoryOps,
                                        int vectorOps) {
    if (bodyInstCount <= 0) return false;

    // CFG-region unroll clones whole inner control flow.  On Cortex-A53 this is
    // only worthwhile when the cloned region exposes real memory/vector work;
    // pure scalar branch nests usually lose to I-cache/BTB pressure.
    if (hasNestedLoopBackedge(loop) && memoryOps == 0 && vectorOps == 0)
        return false;

    if (condBranchBlocks * 3 > bodyInstCount && memoryOps == 0 && vectorOps == 0)
        return false;

    return true;
}

// ── Core unrolling ────────────────────────────────────────────────────────

bool LoopUnroll::tryUnroll(Loop &loop, Function *func, Module *module,
                           BasicAliasAnalysis &BAA) {
    // Only handle simple 2-BB loops: header + latch
    if (loop.blocks.size() != 2) return false;

    BasicBlock *header = loop.header;
    BasicBlock *latch  = loop.singleLatch();
    if (!latch || latch == header) return false;

    // Need a single preheader
    BasicBlock *preheader = loop.preheader;
    if (!preheader) return false;

    // Need a single exit from the header
    BasicBlock *exitBB = nullptr;
    for (auto succ : header->succ_bbs_) {
        if (!loop.blocks.count(succ)) {
            if (exitBB) return false;
            exitBB = succ;
        }
    }
    if (!exitBB) return false;

    // Collect header phis
    std::vector<PhiInst *> headerPhis;
    for (auto inst : header->instr_list_) {
        if (!inst->is_phi()) break;
        headerPhis.push_back(static_cast<PhiInst *>(inst));
    }
    if (headerPhis.empty()) return false;

    // Find IV: integer phi whose latch-incoming value is
    //   phi + ConstantInt stride (stride > 0)   — forward loop, or
    //   phi - ConstantInt stride (stride > 0)   — reverse loop
    PhiInst *    ivPhi    = nullptr;
    ConstantInt *stride   = nullptr;  // > 0 for add, < 0 for sub
    Instruction *ivUpdate = nullptr;

    for (auto phi : headerPhis) {
        if (phi->type_->tid_ != Type::IntegerTyID) continue;

        for (auto inst : latch->instr_list_) {
            if (!inst->is_add() && !inst->is_sub()) continue;
            Value *op0 = inst->get_operand(0);
            Value *op1 = inst->get_operand(1);

            if (inst->is_add()) {
                // add phi, c  or  add c, phi
                auto *c0 = dynamic_cast<ConstantInt *>(op0);
                auto *c1 = dynamic_cast<ConstantInt *>(op1);
                ConstantInt *c   = c1 ? c1 : c0;
                Value *      base = c1 ? op0 : op1;
                if (!c || c->value_ <= 0 || base != phi) continue;
                stride = c;
            } else {
                // sub phi, c   (reverse loop: stride = -c)
                auto *c1 = dynamic_cast<ConstantInt *>(op1);
                if (!c1 || c1->value_ <= 0 || op0 != phi) continue;
                stride = new ConstantInt(phi->type_, -c1->value_);
            }

            // Verify this instruction feeds back into the phi
            bool feedsBack = false;
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                if (phi->get_operand(i) == inst) { feedsBack = true; break; }
            }
            if (!feedsBack) continue;

            ivPhi    = phi;
            ivUpdate = inst;
            break;
        }
        if (ivPhi) break;
    }
    if (!ivPhi) return false;

    // Find condition: icmp using iv, loop-invariant bound, slt/sle/sgt/sge
    ICmpInst *cmpInst  = nullptr;
    Value *   bound    = nullptr;
    bool      ivIsLeft = true;

    for (auto inst : header->instr_list_) {
        if (!inst->is_cmp()) continue;
        auto *cmp = static_cast<ICmpInst *>(inst);
        auto  op  = cmp->icmp_op_;
        if (op != ICmpInst::ICMP_SLT && op != ICmpInst::ICMP_SLE &&
            op != ICmpInst::ICMP_SGT && op != ICmpInst::ICMP_SGE)
            break;
        if (cmp->get_operand(0) == ivPhi) {
            bound = cmp->get_operand(1); ivIsLeft = true;  cmpInst = cmp;
        } else if (cmp->get_operand(1) == ivPhi) {
            bound = cmp->get_operand(0); ivIsLeft = false; cmpInst = cmp;
        }
        break;
    }
    if (!cmpInst) return false;

    // Bound must be loop-invariant
    if (auto *bi = dynamic_cast<Instruction *>(bound))
        if (loop.blocks.count(bi->parent_)) return false;

    // Determine which branch successor is the body vs exit
    auto *headerBr = header->get_terminator();
    if (!headerBr || !headerBr->is_br() || headerBr->num_ops_ != 3) return false;
    auto *trueSucc = static_cast<BasicBlock *>(headerBr->get_operand(1));
    // true → body means the condition is "continue loop" (forward loop)
    if (!loop.blocks.count(trueSucc)) return false;

    // No calls/phis/allocas in latch; only handle instruction types we can clone;
    // also skip loops whose body is large enough to cause register pressure when
    // unrolled (Cortex-A53 has limited registers and no OOO execution).
    int latchBodySize = 0;
    bool hasVectorOps = false;
    for (auto inst : latch->instr_list_) {
        if (inst->isTerminator()) continue;
        if (inst->is_call() || inst->is_phi() || inst->is_alloca()) return false;
        latchBodySize++;
        if (inst->type_->tid_ == Type::VectorTyID)
            hasVectorOps = true;
        for (unsigned i = 0; i < inst->num_ops_; ++i) {
            if (inst->get_operand(i)->type_->tid_ == Type::VectorTyID) {
                hasVectorOps = true;
                break;
            }
        }
        bool canClone = dynamic_cast<BinaryInst *>(inst) ||
                        dynamic_cast<UnaryInst *>(inst) ||
                        dynamic_cast<ICmpInst *>(inst) ||
                        dynamic_cast<FCmpInst *>(inst) ||
                        dynamic_cast<GetElementPtrInst *>(inst) ||
                        dynamic_cast<LoadInst *>(inst) ||
                        dynamic_cast<StoreInst *>(inst) ||
                        dynamic_cast<ZextInst *>(inst) ||
                        dynamic_cast<FpToSiInst *>(inst) ||
                        dynamic_cast<SiToFpInst *>(inst) ||
                        dynamic_cast<Bitcast *>(inst);
        if (!canClone) return false;
    }
    // A two-way vector unroll exposes independent memory/ALU operations while
    // growing the body much less than the default four-way scalar unroll.
    // Permit that modestly larger source body, but keep the original bound for
    // scalar loops.
    int maxLatchInsts = hasVectorOps ? 12 : MAX_LATCH_INSTS;
    if (latchBodySize > maxLatchInsts) return false;

    // ── Register pressure estimation ─────────────────────────────────────
    // A 4× unrolled body creates 4 interleaved SSA chains for each header
    // phi.  When the loop has both an integer accumulator AND ≥2 pointer
    // phis (from IVSR), the register allocator cannot color all the
    // live-range-interleaved values → the accumulator spills to the stack
    // between consecutive madds.  Reduce the unroll factor in this case.
    int numPtrPhis = 0;
    int numIntNonIVPhis = 0;
    for (auto phi : headerPhis) {
        if (phi == ivPhi) continue;
        if (phi->type_->tid_ == Type::PointerTyID)
            numPtrPhis++;
        else if (phi->type_->tid_ == Type::IntegerTyID)
            numIntNonIVPhis++;
    }
    int effectiveUnrollFactor = UNROLL_FACTOR;  // 4
    if (hasVectorOps || (numIntNonIVPhis > 0 && numPtrPhis >= 2))
        effectiveUnrollFactor = 2;

    if (std::getenv("DEBUG_LOOP_UNROLL"))
        std::cerr << "[LoopUnroll] func=" << func->name_
                  << " header=" << header->name_
                  << " factor=" << effectiveUnrollFactor << "\n";

    // ── Transformation ────────────────────────────────────────────────────

    int N   = effectiveUnrollFactor;
    int s   = stride->value_;
    int adj = (N - 1) * s; // bound adjustment for main loop condition

    // Guard against integer underflow: if the loop bound is smaller than the
    // adjustment, bound - adj would go negative and wrap to a huge unsigned
    // value (e.g. 0xFFFFFFFF for -1), making the main loop condition always
    // true → infinite loop with out-of-bounds memory access.
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        if (cb->value_ < adj) return false;
    }

    // Helper: get the preheader-incoming value of a phi
    auto getInitVal = [&](PhiInst *phi) -> Value * {
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (phi->get_operand(i + 1) == preheader)
                return phi->get_operand(i);
        return nullptr;
    };

    // Helper: get the latch-incoming value of a phi
    auto getLatchVal = [&](PhiInst *phi) -> Value * {
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (loop.blocks.count(static_cast<BasicBlock *>(phi->get_operand(i + 1))))
                return phi->get_operand(i);
        return nullptr;
    };

    // 1. Compute adjusted bound (for the main unrolled loop check)
    Value *boundMain;
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        boundMain = new ConstantInt(module->int32_ty_, cb->value_ - adj);
    } else {
        auto *adjConst = new ConstantInt(module->int32_ty_, adj);
        auto *subInst  = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                         bound, adjConst, preheader, /*no-insert*/true);
        preheader->add_instruction_before_terminator(subInst);
        boundMain = subInst;
    }

    // 2. Create headerMain with phi nodes (added in reverse to maintain order)
    BasicBlock *headerMain = new BasicBlock(module, "unroll_hdr", func);

    std::unordered_map<PhiInst *, PhiInst *> phiToMain;
    for (int i = (int)headerPhis.size() - 1; i >= 0; i--) {
        auto *phi     = headerPhis[i];
        auto *mainPhi = PhiInst::create_phi(phi->type_, headerMain);
        headerMain->add_instruction_front(mainPhi);
        mainPhi->addIncoming(getInitVal(phi), preheader);
        // back-edge incoming (from unrolledBody) added later
        phiToMain[phi] = mainPhi;
    }

    // Condition in headerMain: iv < bound - adj  (same icmp op, adjusted bound)
    PhiInst *ivMain = phiToMain[ivPhi];
    ICmpInst *cmpMain;
    if (ivIsLeft)
        cmpMain = new ICmpInst(cmpInst->icmp_op_, ivMain, boundMain, headerMain);
    else
        cmpMain = new ICmpInst(cmpInst->icmp_op_, boundMain, ivMain, headerMain);

    // 3. Build unrolledBody: N copies of the latch (non-terminator instructions)
    BasicBlock *unrolledBody = new BasicBlock(module, "unroll_body", func);

    // Initial value map: header phis → their main-loop phi counterparts
    std::unordered_map<Value *, Value *> iterMap;
    for (auto phi : headerPhis)
        iterMap[phi] = phiToMain[phi];

    // Also seed with any latch-defined values that feed back into phis,
    // so iterations chain correctly
    std::unordered_map<PhiInst *, Value *> curPhiVals;
    for (auto phi : headerPhis)
        curPhiVals[phi] = phiToMain[phi];

    for (int iter = 0; iter < N; iter++) {
        std::unordered_map<Value *, Value *> localMap = iterMap;

        for (auto inst : latch->instr_list_) {
            if (inst->isTerminator()) continue;
            auto *newInst = cloneInst(inst, unrolledBody, localMap);
            if (!newInst) return false; // should not happen after pre-check
            localMap[inst] = newInst;
        }

        // Update iterMap for next iteration: replace each phi's "current" value
        // with what comes out of the latch update this iteration
        for (auto phi : headerPhis) {
            Value *lv = getLatchVal(phi);
            if (lv && localMap.count(lv))
                curPhiVals[phi] = localMap[lv];
        }
        // Next iteration uses the outputs of this one
        for (auto phi : headerPhis)
            iterMap[phi] = curPhiVals[phi];
    }

    if (N == 2 && hasVectorOps)
        clusterTwoVectorStores(unrolledBody, BAA);

    // 4. Branch in unrolledBody → headerMain (back-edge)
    new BranchInst(headerMain, unrolledBody);

    // 5. Now fill in the back-edge incoming values for headerMain phis
    for (auto phi : headerPhis)
        phiToMain[phi]->addIncoming(curPhiVals[phi], unrolledBody);

    // 6. Conditional branch in headerMain: true → unrolledBody, false → header (remainder)
    new BranchInst(cmpMain, unrolledBody, header, headerMain);

    // 7. Update original header phis: change preheader-incoming → [mainPhiVal, headerMain]
    for (auto phi : headerPhis) {
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) != preheader) continue;
            // Remove old uses
            phi->get_operand(i)->remove_use(phi->use_pos_[i]);
            phi->get_operand(i + 1)->remove_use(phi->use_pos_[i + 1]);
            // Set new incoming: value = mainPhi, block = headerMain
            phi->operands_[i]     = phiToMain[phi];
            phi->use_pos_[i]      = phiToMain[phi]->add_use(phi, i);
            phi->operands_[i + 1] = headerMain;
            phi->use_pos_[i + 1]  = headerMain->add_use(phi, i + 1);
            break;
        }
    }

    // 8. Redirect preheader → headerMain (was → header)
    auto *preheaderBr = preheader->get_terminator();
    for (unsigned i = 0; i < preheaderBr->num_ops_; i++) {
        if (preheaderBr->get_operand(i) != header) continue;
        preheaderBr->get_operand(i)->remove_use(preheaderBr->use_pos_[i]);
        preheaderBr->operands_[i] = headerMain;
        preheaderBr->use_pos_[i]  = headerMain->add_use(preheaderBr, i);
        break;
    }
    preheader->remove_succ_basic_block(header);
    preheader->add_succ_basic_block(headerMain);
    header->remove_pre_basic_block(preheader);
    headerMain->add_pre_basic_block(preheader);

    return true;
}

bool LoopUnroll::tryUnrollStructured(Loop &loop, Function *func, Module *module) {
    if (loop.blocks.size() <= 2)
        return false;
    if (loop.blocks.size() > 4)
        return debugStructuredReject(func, loop, "too-many-blocks");

    BasicBlock *header = loop.header;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *preheader = loop.preheader;
    BasicBlock *exitBB = loop.singleExit();
    if (!header || !latch || !preheader || !exitBB)
        return debugStructuredReject(func, loop, "missing-structural-block");
    if (loop.exiting.size() != 1 || loop.exiting[0] != latch)
        return debugStructuredReject(func, loop, "multiple-exiting-blocks");

    auto *latchTerm = dynamic_cast<BranchInst *>(latch->get_terminator());
    if (!latchTerm || latchTerm->num_ops_ != 3)
        return debugStructuredReject(func, loop, "latch-not-cond-branch");
    auto *continueSucc = dynamic_cast<BasicBlock *>(latchTerm->get_operand(1));
    auto *exitSucc = dynamic_cast<BasicBlock *>(latchTerm->get_operand(2));
    if (continueSucc != header || exitSucc != exitBB) {
        continueSucc = dynamic_cast<BasicBlock *>(latchTerm->get_operand(2));
        exitSucc = dynamic_cast<BasicBlock *>(latchTerm->get_operand(1));
    }
    if (continueSucc != header || exitSucc != exitBB)
        return debugStructuredReject(func, loop, "latch-successors");

    std::vector<PhiInst *> headerPhis;
    std::unordered_map<PhiInst *, Value *> initVals;
    std::unordered_map<PhiInst *, Value *> latchVals;
    for (auto *inst : header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->num_ops_ != 4)
            return debugStructuredReject(func, loop, "non-canonical-header-phi");
        Value *init = nullptr;
        Value *back = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *src = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (src == preheader)
                init = phi->get_operand(i);
            else if (src == latch)
                back = phi->get_operand(i);
            else
                return debugStructuredReject(func, loop, "header-phi-not-preheader-latch");
        }
        if (!init || !back)
            return debugStructuredReject(func, loop, "header-phi-missing-edge");
        headerPhis.push_back(phi);
        initVals[phi] = init;
        latchVals[phi] = back;
    }
    if (headerPhis.empty())
        return debugStructuredReject(func, loop, "no-header-phis");

    auto *cmpInst = dynamic_cast<ICmpInst *>(latchTerm->get_operand(0));
    if (!cmpInst)
        return debugStructuredReject(func, loop, "latch-no-icmp");
    auto pred = cmpInst->icmp_op_;
    if (pred != ICmpInst::ICMP_SLT && pred != ICmpInst::ICMP_SLE &&
        pred != ICmpInst::ICMP_SGT && pred != ICmpInst::ICMP_SGE)
        return debugStructuredReject(func, loop, "unsupported-predicate");

    PhiInst *ivPhi = nullptr;
    int strideVal = 0;
    bool ivIsLeft = true;
    Value *bound = nullptr;
    for (auto *phi : headerPhis) {
        if (phi->type_->tid_ != Type::IntegerTyID)
            continue;
        auto *upd = dynamic_cast<Instruction *>(latchVals[phi]);
        if (!upd || upd->parent_ != latch || (!upd->is_add() && !upd->is_sub()))
            continue;
        if (upd->is_add()) {
            auto *c0 = dynamic_cast<ConstantInt *>(upd->get_operand(0));
            auto *c1 = dynamic_cast<ConstantInt *>(upd->get_operand(1));
            if (upd->get_operand(0) == phi && c1 && c1->value_ > 0)
                strideVal = c1->value_;
            else if (upd->get_operand(1) == phi && c0 && c0->value_ > 0)
                strideVal = c0->value_;
            else
                continue;
        } else {
            auto *c1 = dynamic_cast<ConstantInt *>(upd->get_operand(1));
            if (upd->get_operand(0) != phi || !c1 || c1->value_ <= 0)
                continue;
            strideVal = -c1->value_;
        }

        if (cmpInst->get_operand(0) == phi || cmpInst->get_operand(0) == upd) {
            ivIsLeft = true;
            bound = cmpInst->get_operand(1);
        } else if (cmpInst->get_operand(1) == phi || cmpInst->get_operand(1) == upd) {
            ivIsLeft = false;
            bound = cmpInst->get_operand(0);
        } else {
            continue;
        }
        ivPhi = phi;
        break;
    }
    if (!ivPhi || !bound)
        return debugStructuredReject(func, loop, "no-iv");
    if (auto *boundInst = dynamic_cast<Instruction *>(bound))
        if (loop.blocks.count(boundInst->parent_))
            return debugStructuredReject(func, loop, "variant-bound");

    int bodyInstCount = 0;
    int condBranchBlocks = 0;
    for (auto *bb : loop.blocks) {
        if (bb != header) {
            for (auto *inst : bb->instr_list_) {
                if (!inst->is_phi())
                    break;
                return debugStructuredReject(func, loop, "non-header-phi");
            }
        }
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator())
                continue;
            if (inst->is_call() || inst->is_alloca())
                return debugStructuredReject(func, loop, "unsupported-inst");
            bool canClone = dynamic_cast<BinaryInst *>(inst) ||
                            dynamic_cast<UnaryInst *>(inst) ||
                            dynamic_cast<ICmpInst *>(inst) ||
                            dynamic_cast<FCmpInst *>(inst) ||
                            dynamic_cast<GetElementPtrInst *>(inst) ||
                            dynamic_cast<LoadInst *>(inst) ||
                            dynamic_cast<StoreInst *>(inst) ||
                            dynamic_cast<ZextInst *>(inst) ||
                            dynamic_cast<FpToSiInst *>(inst) ||
                            dynamic_cast<SiToFpInst *>(inst) ||
                            dynamic_cast<Bitcast *>(inst);
            if (!canClone)
                return debugStructuredReject(func, loop, "unsupported-inst");
            ++bodyInstCount;
        }
        auto *term = dynamic_cast<BranchInst *>(bb->get_terminator());
        if (term && term->num_ops_ == 3 && bb != latch)
            ++condBranchBlocks;
    }
    if (bodyInstCount > 16 || bodyInstCount > MAX_STRUCTURED_LOOP_INSTS)
        return debugStructuredReject(func, loop, "too-many-body-insts");
    if (condBranchBlocks > 1)
        return debugStructuredReject(func, loop, "too-many-branches");

    int N = 0;
    if (loop.blocks.size() <= 3 && bodyInstCount <= 12) {
        N = 4;
    } else if (loop.blocks.size() <= 4 && bodyInstCount <= 16) {
        N = 2;
    } else {
        return debugStructuredReject(func, loop, "profitability");
    }
    if (bodyInstCount * N > 32)
        return debugStructuredReject(func, loop, "clone-budget");

    int guardAdj = (N - 1) * strideVal;
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        if (strideVal > 0 && cb->value_ < guardAdj)
            return debugStructuredReject(func, loop, "bound-underflow");
    }

    Value *boundMain = nullptr;
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        boundMain = new ConstantInt(module->int32_ty_, cb->value_ - guardAdj);
    } else {
        auto *adjC = new ConstantInt(module->int32_ty_, guardAdj);
        auto *sub = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                   bound, adjC, preheader, true);
        preheader->add_instruction_before_terminator(sub);
        boundMain = sub;
    }

    auto *headerMain = new BasicBlock(module, "unroll_main_hdr", func);
    std::unordered_map<PhiInst *, PhiInst *> mainPhis;
    for (int i = (int)headerPhis.size() - 1; i >= 0; --i) {
        auto *phi = headerPhis[i];
        auto *mainPhi = PhiInst::create_phi(phi->type_, headerMain);
        headerMain->add_instruction_front(mainPhi);
        mainPhi->addIncoming(initVals[phi], preheader);
        mainPhis[phi] = mainPhi;
    }

    ICmpInst *cmpMain = ivIsLeft
                            ? new ICmpInst(pred, mainPhis[ivPhi], boundMain, headerMain)
                            : new ICmpInst(pred, boundMain, mainPhis[ivPhi], headerMain);
    auto *remCheck = new BasicBlock(module, "unroll_rem_guard", func);
    ICmpInst *cmpRem = ivIsLeft
                           ? new ICmpInst(pred, mainPhis[ivPhi], bound, remCheck)
                           : new ICmpInst(pred, bound, mainPhis[ivPhi], remCheck);

    std::vector<std::unordered_map<BasicBlock *, BasicBlock *>> iterBBMaps(N);
    for (int iter = 0; iter < N; ++iter) {
        for (auto *oldBB : loop.blocksOrdered) {
            auto *newBB = new BasicBlock(module,
                                         "unroll_" + std::to_string(iter) + "_" + oldBB->name_,
                                         func);
            iterBBMaps[iter][oldBB] = newBB;
        }
    }

    auto headerCloneFor = [&](int iter) { return iterBBMaps[iter][header]; };

    std::unordered_map<PhiInst *, Value *> currentPhiVals;
    for (auto *phi : headerPhis)
        currentPhiVals[phi] = mainPhis[phi];

    for (int iter = 0; iter < N; ++iter) {
        std::unordered_map<Value *, Value *> valueMap;
        for (auto *phi : headerPhis)
            valueMap[phi] = currentPhiVals[phi];

        auto &bbMap = iterBBMaps[iter];
        for (auto *oldBB : loop.blocksOrdered) {
            auto *newBB = bbMap[oldBB];
            for (auto *oldInst : oldBB->instr_list_) {
                if (oldInst->is_phi())
                    continue;
                if (oldInst->isTerminator()) {
                    auto *oldBr = dynamic_cast<BranchInst *>(oldInst);
                    if (!oldBr)
                        return false;
                    if (oldBB == latch) {
                        if (iter + 1 < N)
                            new BranchInst(headerCloneFor(iter + 1), newBB);
                        else
                            new BranchInst(headerMain, newBB);
                        continue;
                    }
                    if (oldBr->num_ops_ == 1) {
                        auto *dest = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(0), valueMap, bbMap));
                        if (!dest) return false;
                        new BranchInst(dest, newBB);
                    } else {
                        auto *cond = mapLoopValue(oldBr->get_operand(0), valueMap, bbMap);
                        auto *ifTrue = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(1), valueMap, bbMap));
                        auto *ifFalse = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(2), valueMap, bbMap));
                        if (!cond || !ifTrue || !ifFalse) return false;
                        new BranchInst(cond, ifTrue, ifFalse, newBB);
                    }
                    continue;
                }

                auto *newInst = cloneInst(oldInst, newBB, valueMap);
                if (!newInst)
                    return false;
                valueMap[oldInst] = newInst;
            }
        }

        for (auto *phi : headerPhis) {
            auto *mapped = mapLoopValue(latchVals[phi], valueMap, bbMap);
            if (!mapped)
                return debugStructuredReject(func, loop, "latch-map-fail");
            currentPhiVals[phi] = mapped;
        }
    }

    for (auto *phi : headerPhis)
        mainPhis[phi]->addIncoming(currentPhiVals[phi], iterBBMaps[N - 1][latch]);

    new BranchInst(cmpMain, headerCloneFor(0), remCheck, headerMain);
    new BranchInst(cmpRem, header, exitBB, remCheck);

    auto *preBr = preheader->get_terminator();
    for (unsigned i = 0; i < preBr->num_ops_; ++i) {
        if (preBr->get_operand(i) == header)
            preBr->set_operand(i, headerMain);
    }
    preheader->remove_succ_basic_block(header);
    preheader->add_succ_basic_block(headerMain);
    header->remove_pre_basic_block(preheader);
    headerMain->add_pre_basic_block(preheader);

    for (auto *phi : headerPhis) {
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == preheader) {
                phi->set_operand(i, mainPhis[phi]);
                phi->set_operand(i + 1, remCheck);
                break;
            }
        }
    }

    std::unordered_map<Value *, Value *> remapToState;
    for (auto *phi : headerPhis) {
        remapToState[phi] = mainPhis[phi];
        remapToState[latchVals[phi]] = mainPhis[phi];
    }
    for (auto *inst : exitBB->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        Value *fromLatch = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == latch) {
                fromLatch = phi->get_operand(i);
                break;
            }
        }
        if (!fromLatch) continue;
        auto it = remapToState.find(fromLatch);
        if (it == remapToState.end()) {
            if (dynamic_cast<Constant *>(fromLatch))
                phi->addIncoming(fromLatch, remCheck);
            else
                return false;
        } else {
            phi->addIncoming(it->second, remCheck);
        }
    }

    return true;
}

bool LoopUnroll::tryUnrollCFGRegion(Loop &loop, Function *func, Module *module) {
    if (loop.blocks.size() <= 4)
        return false;

    BasicBlock *header = loop.header;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *preheader = loop.preheader;
    BasicBlock *exitBB = loop.singleExit();
    if (!header || !latch || !preheader || !exitBB)
        return debugCFGRegionReject(func, loop, "missing-structural-block");
    if (loop.exiting.size() != 1 || loop.exiting[0] != latch)
        return debugCFGRegionReject(func, loop, "non-latch-exit");

    auto *latchBr = dynamic_cast<BranchInst *>(latch->get_terminator());
    if (!latchBr || latchBr->num_ops_ != 3)
        return debugCFGRegionReject(func, loop, "latch-not-cond-branch");
    auto *latchTrue = dynamic_cast<BasicBlock *>(latchBr->get_operand(1));
    auto *latchFalse = dynamic_cast<BasicBlock *>(latchBr->get_operand(2));
    if (latchTrue != header || latchFalse != exitBB)
        return debugCFGRegionReject(func, loop, "unsupported-latch-branch");

    std::vector<PhiInst *> headerPhis;
    std::unordered_map<PhiInst *, Value *> initVals;
    std::unordered_map<PhiInst *, Value *> latchVals;
    for (auto *inst : header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->num_ops_ != 4)
            return debugCFGRegionReject(func, loop, "non-canonical-header-phi");
        Value *init = nullptr;
        Value *back = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *src = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (src == preheader)
                init = phi->get_operand(i);
            else if (src == latch)
                back = phi->get_operand(i);
            else
                return debugCFGRegionReject(func, loop, "header-phi-edge");
        }
        if (!init || !back)
            return debugCFGRegionReject(func, loop, "header-phi-missing-edge");
        headerPhis.push_back(phi);
        initVals[phi] = init;
        latchVals[phi] = back;
    }
    if (headerPhis.empty())
        return debugCFGRegionReject(func, loop, "no-header-phis");

    auto *cmpInst = dynamic_cast<ICmpInst *>(latchBr->get_operand(0));
    if (!cmpInst || cmpInst->icmp_op_ != ICmpInst::ICMP_SLT)
        return debugCFGRegionReject(func, loop, "unsupported-predicate");

    PhiInst *ivPhi = nullptr;
    Instruction *ivUpdate = nullptr;
    Value *bound = nullptr;
    for (auto *phi : headerPhis) {
        if (phi->type_->tid_ != Type::IntegerTyID)
            continue;
        auto *upd = dynamic_cast<Instruction *>(latchVals[phi]);
        if (!upd || upd->parent_ != latch || !upd->is_add())
            continue;
        auto *c0 = dynamic_cast<ConstantInt *>(upd->get_operand(0));
        auto *c1 = dynamic_cast<ConstantInt *>(upd->get_operand(1));
        bool stepOne = (upd->get_operand(0) == phi && c1 && c1->value_ == 1) ||
                       (upd->get_operand(1) == phi && c0 && c0->value_ == 1);
        if (!stepOne)
            continue;
        if (cmpInst->get_operand(0) != phi)
            continue;
        bound = cmpInst->get_operand(1);
        ivPhi = phi;
        ivUpdate = upd;
        break;
    }
    if (!ivPhi || !ivUpdate || !bound)
        return debugCFGRegionReject(func, loop, "no-iv");
    if (auto *boundInst = dynamic_cast<Instruction *>(bound))
        if (loop.blocks.count(boundInst->parent_))
            return debugCFGRegionReject(func, loop, "variant-bound");

    auto *initConst = dynamic_cast<ConstantInt *>(initVals[ivPhi]);
    if (!initConst || initConst->value_ < 0)
        return debugCFGRegionReject(func, loop, "non-constant-nonnegative-init");
    if (!dynamic_cast<ConstantInt *>(bound) &&
        !hasEntryLowerBoundGuard(preheader, header, bound, initConst))
        return debugCFGRegionReject(func, loop, "missing-entry-bound-guard");

    int bodyInstCount = 0;
    int condBranchBlocks = 0;
    int memoryOps = 0;
    int vectorOps = 0;
    for (auto *bb : loop.blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator())
                continue;
            if (inst->is_call() || inst->is_store() || inst->is_alloca())
                return debugCFGRegionReject(func, loop, "side-effect");
            if (!isCloneableForUnroll(inst))
                return debugCFGRegionReject(func, loop, "unsupported-inst");
            if (inst->is_load() || inst->is_gep())
                ++memoryOps;
            if (inst->type_->tid_ == Type::VectorTyID)
                ++vectorOps;
            for (unsigned i = 0; i < inst->num_ops_; ++i)
                if (inst->get_operand(i)->type_->tid_ == Type::VectorTyID)
                    ++vectorOps;
            ++bodyInstCount;
        }
        auto *term = dynamic_cast<BranchInst *>(bb->get_terminator());
        if (term && term->num_ops_ == 3)
            ++condBranchBlocks;
    }
    if (bodyInstCount > 48 || loop.blocksOrdered.size() > 16)
        return debugCFGRegionReject(func, loop, "clone-budget");
    if (!isProfitableCFGRegionUnroll(loop, bodyInstCount, condBranchBlocks,
                                     memoryOps, vectorOps))
        return debugCFGRegionReject(func, loop, "profitability");

    struct OutsideUse { Instruction *user; unsigned idx; };
    std::map<Value *, std::vector<OutsideUse>> liveOuts;
    for (auto *bb : loop.blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            if (inst->isTerminator()) continue;
            for (const auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (!user || !user->parent_ || loop.blocks.count(user->parent_))
                    continue;
                if (user->parent_ == exitBB && user->is_phi())
                    continue;
                liveOuts[inst].push_back({user, use.arg_no_});
            }
        }
    }

    ICmpInst *firstIterCmp = nullptr;
    if (auto *headerBr = dynamic_cast<BranchInst *>(header->get_terminator())) {
        if (headerBr->num_ops_ == 3) {
            auto *cmp = dynamic_cast<ICmpInst *>(headerBr->get_operand(0));
            if (cmp && cmp->icmp_op_ == ICmpInst::ICMP_EQ) {
                auto *c0 = dynamic_cast<ConstantInt *>(cmp->get_operand(0));
                auto *c1 = dynamic_cast<ConstantInt *>(cmp->get_operand(1));
                bool matchesInit =
                    (cmp->get_operand(0) == ivPhi && c1 && c1->value_ == initConst->value_) ||
                    (cmp->get_operand(1) == ivPhi && c0 && c0->value_ == initConst->value_);
                if (matchesInit)
                    firstIterCmp = cmp;
            }
        }
    }

    const int N = UNROLL_FACTOR;
    const int guardAdj = N - 1;
    Value *boundMain = nullptr;
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        if (cb->value_ < initConst->value_ + guardAdj)
            return debugCFGRegionReject(func, loop, "constant-trip-too-small");
        boundMain = new ConstantInt(module->int32_ty_, cb->value_ - guardAdj);
    } else {
        auto *adjC = new ConstantInt(module->int32_ty_, guardAdj);
        auto *sub = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                   bound, adjC, preheader, true);
        preheader->add_instruction_before_terminator(sub);
        boundMain = sub;
    }

    if (std::getenv("DEBUG_LOOP_UNROLL"))
        std::cerr << "[LoopUnroll] func=" << func->name_
                  << " header=" << header->name_
                  << " factor=" << N << " form=cfg-region\n";

    auto *headerMain = new BasicBlock(module, "unroll_cfg_hdr", func);
    std::unordered_map<PhiInst *, PhiInst *> mainPhis;
    for (int i = (int)headerPhis.size() - 1; i >= 0; --i) {
        auto *phi = headerPhis[i];
        auto *mainPhi = PhiInst::create_phi(phi->type_, headerMain);
        headerMain->add_instruction_front(mainPhi);
        mainPhi->addIncoming(initVals[phi], preheader);
        mainPhis[phi] = mainPhi;
    }
    auto *cmpMain = new ICmpInst(ICmpInst::ICMP_SLE, mainPhis[ivPhi],
                                 boundMain, headerMain);

    auto *remCheck = new BasicBlock(module, "unroll_cfg_rem", func);
    auto *cmpRem = new ICmpInst(ICmpInst::ICMP_SLE, mainPhis[ivPhi],
                                bound, remCheck);

    std::vector<std::unordered_map<BasicBlock *, BasicBlock *>> iterBBMaps(N);
    for (int iter = 0; iter < N; ++iter) {
        for (auto *oldBB : loop.blocksOrdered) {
            auto *newBB = new BasicBlock(module,
                                         "unroll_cfg_" + std::to_string(iter) +
                                             "_" + oldBB->name_,
                                         func);
            iterBBMaps[iter][oldBB] = newBB;
        }
    }

    auto headerCloneFor = [&](int iter) { return iterBBMaps[iter][header]; };

    std::unordered_map<PhiInst *, Value *> currentPhiVals;
    for (auto *phi : headerPhis)
        currentPhiVals[phi] = mainPhis[phi];

    std::unordered_map<Value *, Value *> finalValueMap;
    for (int iter = 0; iter < N; ++iter) {
        std::unordered_map<Value *, Value *> valueMap;
        auto &bbMap = iterBBMaps[iter];

        for (auto *phi : headerPhis)
            valueMap[phi] = currentPhiVals[phi];

        for (auto *oldBB : loop.blocksOrdered) {
            if (oldBB == header)
                continue;
            auto *newBB = bbMap[oldBB];
            for (auto *oldInst : oldBB->instr_list_) {
                if (!oldInst->is_phi()) break;
                auto *newPhi = PhiInst::create_phi(oldInst->type_, newBB);
                newBB->add_instruction(newPhi);
                valueMap[oldInst] = newPhi;
            }
        }

        for (auto *oldBB : loop.blocksOrdered) {
            auto *newBB = bbMap[oldBB];
            for (auto *oldInst : oldBB->instr_list_) {
                if (oldInst->is_phi())
                    continue;
                if (oldInst->isTerminator()) {
                    auto *oldBr = dynamic_cast<BranchInst *>(oldInst);
                    if (!oldBr)
                        return false;
                    if (oldBB == latch) {
                        if (iter + 1 < N)
                            new BranchInst(headerCloneFor(iter + 1), newBB);
                        else
                            new BranchInst(headerMain, newBB);
                        continue;
                    }
                    if (oldBr->num_ops_ == 1) {
                        auto *dest = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(0), valueMap, bbMap));
                        if (!dest) return false;
                        new BranchInst(dest, newBB);
                    } else {
                        auto *cond = mapLoopValue(oldBr->get_operand(0), valueMap, bbMap);
                        auto *ifTrue = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(1), valueMap, bbMap));
                        auto *ifFalse = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(2), valueMap, bbMap));
                        if (!cond || !ifTrue || !ifFalse) return false;
                        new BranchInst(cond, ifTrue, ifFalse, newBB);
                    }
                    continue;
                }
                if (oldInst == firstIterCmp && iter > 0) {
                    valueMap[oldInst] = new ConstantInt(module->int1_ty_, 0);
                    continue;
                }
                auto *newInst = cloneInst(oldInst, newBB, valueMap);
                if (!newInst)
                    return false;
                valueMap[oldInst] = newInst;
            }
        }

        for (auto *oldBB : loop.blocksOrdered) {
            if (oldBB == header)
                continue;
            auto *newBB = bbMap[oldBB];
            for (auto *oldInst : oldBB->instr_list_) {
                if (!oldInst->is_phi()) break;
                auto *newPhi = static_cast<PhiInst *>(valueMap[oldInst]);
                for (unsigned i = 0; i < oldInst->num_ops_; i += 2) {
                    auto *oldPred = dynamic_cast<BasicBlock *>(oldInst->get_operand(i + 1));
                    if (!oldPred || !loop.blocks.count(oldPred))
                        return debugCFGRegionReject(func, loop, "internal-phi-outside-pred");
                    Value *mappedVal = mapLoopValue(oldInst->get_operand(i), valueMap, bbMap);
                    auto *mappedPred = dynamic_cast<BasicBlock *>(
                        mapLoopValue(oldPred, valueMap, bbMap));
                    if (!mappedVal || !mappedPred)
                        return debugCFGRegionReject(func, loop, "internal-phi-map-fail");
                    newPhi->addIncoming(mappedVal, mappedPred);
                }
            }
        }

        for (auto *phi : headerPhis) {
            Value *mapped = mapLoopValue(latchVals[phi], valueMap, bbMap);
            if (!mapped)
                return debugCFGRegionReject(func, loop, "latch-map-fail");
            currentPhiVals[phi] = mapped;
        }
        if (iter == N - 1)
            finalValueMap = std::move(valueMap);
    }

    for (auto *phi : headerPhis)
        mainPhis[phi]->addIncoming(currentPhiVals[phi], iterBBMaps[N - 1][latch]);

    new BranchInst(cmpMain, headerCloneFor(0), remCheck, headerMain);
    new BranchInst(cmpRem, header, exitBB, remCheck);

    auto *preBr = preheader->get_terminator();
    bool redirected = false;
    for (unsigned i = 0; i < preBr->num_ops_; ++i) {
        if (preBr->get_operand(i) == header) {
            preBr->set_operand(i, headerMain);
            redirected = true;
        }
    }
    if (!redirected)
        return debugCFGRegionReject(func, loop, "preheader-no-edge");
    preheader->remove_succ_basic_block(header);
    preheader->add_succ_basic_block(headerMain);
    header->remove_pre_basic_block(preheader);
    headerMain->add_pre_basic_block(preheader);

    for (auto *phi : headerPhis) {
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == preheader) {
                phi->set_operand(i, mainPhis[phi]);
                phi->set_operand(i + 1, remCheck);
                break;
            }
        }
    }

    auto mapFinal = [&](Value *v) -> Value * {
        if (auto *phi = dynamic_cast<PhiInst *>(v)) {
            auto it = currentPhiVals.find(phi);
            if (it != currentPhiVals.end())
                return it->second;
        }
        for (auto *phi : headerPhis)
            if (latchVals[phi] == v)
                return currentPhiVals[phi];
        auto it = finalValueMap.find(v);
        return it != finalValueMap.end() ? it->second : v;
    };

    auto carriedModuloFor = [&](Value *raw, PhiInst *exitPhi) -> Value * {
        auto phiFeedsOnlySameSRem = [&](ConstantInt *mod) {
            bool sawUse = false;
            for (const auto &use : exitPhi->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (!user || user->parent_ != exitBB || !user->is_rem())
                    return false;
                auto *userMod = dynamic_cast<ConstantInt *>(user->get_operand(1));
                if (!userMod || userMod->value_ != mod->value_)
                    return false;
                sawUse = true;
            }
            return sawUse;
        };

        for (auto *phi : headerPhis) {
            auto *rem = dynamic_cast<Instruction *>(latchVals[phi]);
            if (!rem || rem->parent_ != latch || !rem->is_rem())
                continue;
            if (rem->get_operand(0) != raw)
                continue;
            auto *mod = dynamic_cast<ConstantInt *>(rem->get_operand(1));
            if (!mod || !phiFeedsOnlySameSRem(mod))
                continue;
            return currentPhiVals[phi];
        }
        return nullptr;
    };

    for (auto *inst : exitBB->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        Value *fromLatch = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (phi->get_operand(i + 1) == latch)
                fromLatch = phi->get_operand(i);
        if (fromLatch) {
            Value *finalVal = carriedModuloFor(fromLatch, phi);
            phi->addIncoming(finalVal ? finalVal : mapFinal(fromLatch), remCheck);
        }
    }

    for (auto &[val, uses] : liveOuts) {
        auto *mergePhi = PhiInst::create_phi(val->type_, exitBB);
        exitBB->add_instruction_front(mergePhi);
        mergePhi->addIncoming(val, latch);
        mergePhi->addIncoming(mapFinal(val), remCheck);
        for (auto &use : uses)
            use.user->set_operand(use.idx, mergePhi);
    }

    return true;
}

// ── Do-while (rotated single-block) unrolling ─────────────────────────────
//
// 形态：loop = { B }，B: phis…, body…, ivUpdate, cmp(ivUpdate, bound),
//       br cmp, B, exit。check 在 body 之后 ⇒ 展开需要：
//   checkBlock:  iv_init <op> bound-adj ? mainLoop : B（不足 N 次直接走原循环）
//   mainLoop:    N 份 body 链式克隆，cmp(iv_N, bound-adj) 回边/出
//   remCheck:    cmp(iv_N, bound) ? B（余数 1..N-1 次）: exit（余数 0 次）
//   B:           phi 入边改为 [init, checkBlock], [iv_N 等, remCheck]
//   exit:        来自 B 的 live-out 补 [主循环末值, remCheck] 入边
bool LoopUnroll::tryUnrollDoWhile(Loop &loop, Function *func, Module *module) {
    if (loop.blocks.size() != 1) return false;

    BasicBlock *header = loop.header;
    if (loop.singleLatch() != header) return false;
    BasicBlock *preheader = loop.preheader;
    if (!preheader) return false;

    auto *term = header->get_terminator();
    if (!term || !term->is_br() || term->num_ops_ != 3) return false;
    auto *trueSucc  = dynamic_cast<BasicBlock *>(term->get_operand(1));
    auto *falseSucc = dynamic_cast<BasicBlock *>(term->get_operand(2));
    if (trueSucc != header || !falseSucc || falseSucc == header) return false;
    BasicBlock *exitBB = falseSucc;
    // 需要专用出口（exit 唯一前驱是 header），否则 live-out phi 修补
    // 无法只看本循环
    if (exitBB->pre_bbs_.size() != 1 || exitBB->pre_bbs_[0] != header)
        return false;

    // ── 收集 phi ────────────────────────────────────────────────────────
    std::vector<PhiInst *> headerPhis;
    for (auto inst : header->instr_list_) {
        if (!inst->is_phi()) break;
        headerPhis.push_back(static_cast<PhiInst *>(inst));
    }
    if (headerPhis.empty()) return false;

    auto getInitVal = [&](PhiInst *phi) -> Value * {
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (phi->get_operand(i + 1) == preheader)
                return phi->get_operand(i);
        return nullptr;
    };
    auto getBackVal = [&](PhiInst *phi) -> Value * {
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (phi->get_operand(i + 1) == header)
                return phi->get_operand(i);
        return nullptr;
    };
    for (auto phi : headerPhis)
        if (phi->num_ops_ != 4 || !getInitVal(phi) || !getBackVal(phi))
            return false;

    // ── 识别 IV：backedge 入值 = add/sub(phi, 常量) ─────────────────────
    PhiInst     *ivPhi    = nullptr;
    Instruction *ivUpdate = nullptr;
    int          strideVal = 0;
    for (auto phi : headerPhis) {
        if (phi->type_->tid_ != Type::IntegerTyID) continue;
        auto *upd = dynamic_cast<Instruction *>(getBackVal(phi));
        if (!upd || upd->parent_ != header) continue;
        if (!upd->is_add() && !upd->is_sub()) continue;
        Value *op0 = upd->get_operand(0);
        Value *op1 = upd->get_operand(1);
        if (upd->is_add()) {
            auto *c0 = dynamic_cast<ConstantInt *>(op0);
            auto *c1 = dynamic_cast<ConstantInt *>(op1);
            ConstantInt *c    = c1 ? c1 : c0;
            Value       *base = c1 ? op0 : op1;
            if (!c || c->value_ <= 0 || base != phi) continue;
            strideVal = c->value_;
        } else {
            auto *c1 = dynamic_cast<ConstantInt *>(op1);
            if (!c1 || c1->value_ <= 0 || op0 != phi) continue;
            strideVal = -c1->value_;
        }
        ivPhi    = phi;
        ivUpdate = upd;
        break;
    }
    if (!ivPhi) return false;

    // ── 识别 cond：cmp(ivUpdate, 不变 bound)，且只被 br 使用 ────────────
    auto *cmpInst = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cmpInst || cmpInst->parent_ != header) return false;
    if (cmpInst->use_list_.size() != 1) return false;
    auto op = cmpInst->icmp_op_;
    if (op != ICmpInst::ICMP_SLT && op != ICmpInst::ICMP_SLE &&
        op != ICmpInst::ICMP_SGT && op != ICmpInst::ICMP_SGE)
        return false;
    Value *bound   = nullptr;
    bool   ivIsLeft = true;
    if (cmpInst->get_operand(0) == ivUpdate) {
        bound = cmpInst->get_operand(1); ivIsLeft = true;
    } else if (cmpInst->get_operand(1) == ivUpdate) {
        bound = cmpInst->get_operand(0); ivIsLeft = false;
    } else {
        return false;
    }
    if (auto *bi = dynamic_cast<Instruction *>(bound))
        if (bi->parent_ == header) return false;

    // ── body 可克隆性 ───────────────────────────────────────────────────
    int bodySize = 0;
    for (auto inst : header->instr_list_) {
        if (inst->is_phi() || inst == cmpInst || inst->isTerminator()) continue;
        if (inst->is_call() || inst->is_alloca()) return false;
        bool canClone = dynamic_cast<BinaryInst *>(inst) ||
                        dynamic_cast<UnaryInst *>(inst) ||
                        dynamic_cast<ICmpInst *>(inst) ||
                        dynamic_cast<FCmpInst *>(inst) ||
                        dynamic_cast<GetElementPtrInst *>(inst) ||
                        dynamic_cast<LoadInst *>(inst) ||
                        dynamic_cast<StoreInst *>(inst) ||
                        dynamic_cast<ZextInst *>(inst) ||
                        dynamic_cast<FpToSiInst *>(inst) ||
                        dynamic_cast<SiToFpInst *>(inst) ||
                        dynamic_cast<Bitcast *>(inst);
        if (!canClone) return false;
        bodySize++;
    }
    if (bodySize > MAX_LATCH_INSTS) return false;

    // ── 循环外使用清点：只允许 (a) exitBB 中的 phi（入边=header），
    //    (b) 其他位置的使用（exitBB 唯一出口支配它们，事后补 phi）─────
    struct OutsideUse { Instruction *user; unsigned idx; };
    std::map<Value *, std::vector<OutsideUse>> liveOuts; // 需补 phi 的值
    for (auto inst : header->instr_list_) {
        if (inst == cmpInst || inst->isTerminator()) continue;
        for (const auto &use : inst->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user || !user->parent_ || user->parent_ == header) continue;
            if (user->parent_ == exitBB && user->is_phi()) continue; // (a)
            liveOuts[inst].push_back({user, use.arg_no_});           // (b)
        }
    }

    // ── 压力启发式（与 while 形一致）────────────────────────────────────
    int numPtrPhis = 0, numIntNonIVPhis = 0;
    for (auto phi : headerPhis) {
        if (phi == ivPhi) continue;
        if (phi->type_->tid_ == Type::PointerTyID) numPtrPhis++;
        else if (phi->type_->tid_ == Type::IntegerTyID) numIntNonIVPhis++;
    }
    int N = UNROLL_FACTOR;
    if (numIntNonIVPhis > 0 && numPtrPhis >= 2) N = 2;

    int adj = (N - 1) * strideVal;
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        if (strideVal > 0 && cb->value_ < adj) return false;
        if (strideVal < 0 && cb->value_ > adj + std::numeric_limits<int>::max())
            return false;
    }

    if (std::getenv("DEBUG_LOOP_UNROLL"))
        std::cerr << "[LoopUnroll] func=" << func->name_
                  << " header=" << header->name_
                  << " factor=" << N << " form=dowhile\n";

    // ── 变换 ────────────────────────────────────────────────────────────
    // 1. boundMain = bound - adj
    Value *boundMain;
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        boundMain = new ConstantInt(module->int32_ty_, cb->value_ - adj);
    } else {
        auto *adjC = new ConstantInt(module->int32_ty_, adj);
        auto *sub  = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                    bound, adjC, preheader, /*no-insert*/true);
        preheader->add_instruction_before_terminator(sub);
        boundMain = sub;
    }

    // 2. checkBlock：iv_init 满足 N 次余量则进主循环，否则走原循环
    auto *checkBlock = new BasicBlock(module, "unroll_guard", func);
    ICmpInst *cmpEnter =
        ivIsLeft ? new ICmpInst(op, getInitVal(ivPhi), boundMain, checkBlock)
                 : new ICmpInst(op, boundMain, getInitVal(ivPhi), checkBlock);

    // 3. mainLoop：phi + N 份克隆
    auto *mainLoop = new BasicBlock(module, "unroll_main", func);
    std::unordered_map<PhiInst *, PhiInst *> phiToMain;
    for (int i = (int)headerPhis.size() - 1; i >= 0; i--) {
        auto *phi     = headerPhis[i];
        auto *mainPhi = PhiInst::create_phi(phi->type_, mainLoop);
        mainLoop->add_instruction_front(mainPhi);
        mainPhi->addIncoming(getInitVal(phi), checkBlock);
        phiToMain[phi] = mainPhi;
    }

    std::unordered_map<Value *, Value *> iterMap;
    for (auto phi : headerPhis)
        iterMap[phi] = phiToMain[phi];
    std::unordered_map<Value *, Value *> finalMap; // 最后一轮的完整映射
    std::unordered_map<PhiInst *, Value *> curPhiVals;
    for (int iter = 0; iter < N; iter++) {
        std::unordered_map<Value *, Value *> localMap = iterMap;
        for (auto inst : header->instr_list_) {
            if (inst->is_phi() || inst == cmpInst || inst->isTerminator())
                continue;
            auto *newInst = cloneInst(inst, mainLoop, localMap);
            if (!newInst) return false; // 前置检查后不应发生
            localMap[inst] = newInst;
        }
        for (auto phi : headerPhis) {
            Value *bv = getBackVal(phi);
            auto it = localMap.find(bv);
            curPhiVals[phi] = (it != localMap.end()) ? it->second : bv;
        }
        for (auto phi : headerPhis)
            iterMap[phi] = curPhiVals[phi];
        if (iter == N - 1)
            finalMap = std::move(localMap);
    }

    auto mapFinal = [&](Value *v) -> Value * {
        if (auto *p = dynamic_cast<PhiInst *>(v))
            if (curPhiVals.count(p)) return curPhiVals[p];
        auto it = finalMap.find(v);
        return it != finalMap.end() ? it->second : v;
    };

    Value *ivOut = curPhiVals[ivPhi]; // 主循环出口处的 iv（= 映射后的 ivUpdate）
    ICmpInst *cmpMain =
        ivIsLeft ? new ICmpInst(op, ivOut, boundMain, mainLoop)
                 : new ICmpInst(op, boundMain, ivOut, mainLoop);
    for (auto phi : headerPhis)
        phiToMain[phi]->addIncoming(curPhiVals[phi], mainLoop);

    // 4. remCheck：还有余数则进原循环，否则直接 exit
    auto *remCheck = new BasicBlock(module, "unroll_rem_guard", func);
    ICmpInst *cmpRem =
        ivIsLeft ? new ICmpInst(op, ivOut, bound, remCheck)
                 : new ICmpInst(op, bound, ivOut, remCheck);
    new BranchInst(cmpRem, header, exitBB, remCheck);

    // 5. 分支接线（BranchInst 构造器维护 CFG 链接）
    new BranchInst(cmpEnter, mainLoop, header, checkBlock);
    new BranchInst(cmpMain, mainLoop, remCheck, mainLoop);

    // 6. preheader → checkBlock（替换原指向 header 的边）
    auto *preBr = preheader->get_terminator();
    for (unsigned i = 0; i < preBr->num_ops_; i++) {
        if (preBr->get_operand(i) == header)
            preBr->set_operand(i, checkBlock);
    }
    preheader->remove_succ_basic_block(header);
    preheader->add_succ_basic_block(checkBlock);
    header->remove_pre_basic_block(preheader);
    checkBlock->add_pre_basic_block(preheader);

    // 7. 原循环 phi：preheader 入边 → checkBlock；新增 remCheck 入边
    for (auto phi : headerPhis) {
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == preheader) {
                phi->set_operand(i + 1, checkBlock);
                break;
            }
        }
        phi->addIncoming(curPhiVals[phi], remCheck);
    }

    // 8. exitBB phi：补 remCheck 入边（值 = 主循环末值映射）
    for (auto inst : exitBB->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        Value *fromHeader = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (phi->get_operand(i + 1) == header)
                fromHeader = phi->get_operand(i);
        if (!fromHeader) return false; // 不应发生：唯一前驱是 header
        phi->addIncoming(mapFinal(fromHeader), remCheck);
    }

    // 9. 其他循环外使用：在 exitBB 顶部补汇合 phi 后改写
    for (auto &[val, uses] : liveOuts) {
        auto *mergePhi = PhiInst::create_phi(val->type_, exitBB);
        exitBB->add_instruction_front(mergePhi);
        mergePhi->addIncoming(val, header);
        mergePhi->addIncoming(mapFinal(val), remCheck);
        for (auto &u : uses)
            u.user->set_operand(u.idx, mergePhi);
    }

    return true;
}

// ── Entry points ──────────────────────────────────────────────────────────

void LoopUnroll::runOnFunction(Function *func, BasicAliasAnalysis &BAA) {
    if (func->basic_blocks_.empty()) return;

    LoopInfo LI;
    LI.analyze(func);
    if (LI.allLoops().empty()) return;

    // Innermost first。unroll 成功会改 CFG（preheader 改指 headerMain），
    // 快照内其余 Loop 结构随之过期——与迁移前行为一致：外层循环因
    // blocks.size()!=2 本就不会命中，错过的机会留给下一次调度。
    std::vector<Loop *> loops;
    for (auto &l : LI.allLoops())
        loops.push_back(l.get());
    std::sort(loops.begin(), loops.end(),
              [](Loop *a, Loop *b) { return a->depth > b->depth; });

    for (auto *loop : loops) {
        if (tryUnroll(*loop, func, func->parent_, BAA))
            continue;
        if (tryUnrollStructured(*loop, func, func->parent_))
            continue;
        if (tryUnrollCFGRegion(*loop, func, func->parent_))
            continue;
        tryUnrollDoWhile(*loop, func, func->parent_);
    }

    func->set_instr_name();
}

void LoopUnroll::execute(Module *module) {
    BasicAliasAnalysis BAA;
    BAA.analyze(module);
    for (auto func : module->function_list_)
        if (!func->is_declaration())
            runOnFunction(func, BAA);
}
