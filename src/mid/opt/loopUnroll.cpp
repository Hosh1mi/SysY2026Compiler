#include "../../include/mid/opt/loopUnroll.hpp"
#include <algorithm>
#include <functional>
#include <queue>

static const int UNROLL_FACTOR   = 4;
static const int MAX_LATCH_INSTS = 8; // skip unrolling if body is too large

// ── CFG / Dominator helpers (mirrors LICM) ────────────────────────────────

std::vector<BasicBlock *> LoopUnroll::computeRPO(Function *func) {
    std::vector<BasicBlock *> postorder;
    std::set<BasicBlock *> visited;
    std::function<void(BasicBlock *)> dfs = [&](BasicBlock *bb) {
        visited.insert(bb);
        for (auto succ : bb->succ_bbs_)
            if (!visited.count(succ)) dfs(succ);
        postorder.push_back(bb);
    };
    if (!func->basic_blocks_.empty())
        dfs(func->basic_blocks_[0]);
    std::reverse(postorder.begin(), postorder.end());
    return postorder;
}

void LoopUnroll::computeDominators(const std::vector<BasicBlock *> &rpo) {
    idom_.clear();
    rpoIdx_.clear();
    if (rpo.empty()) return;
    BasicBlock *entry = rpo[0];
    for (int i = 0; i < (int)rpo.size(); i++)
        rpoIdx_[rpo[i]] = i;
    idom_[entry] = entry;
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto bb : rpo) {
            if (bb == entry) continue;
            BasicBlock *new_idom = nullptr;
            for (auto pred : bb->pre_bbs_) {
                if (!idom_.count(pred)) continue;
                new_idom = new_idom ? intersect(pred, new_idom) : pred;
            }
            if (new_idom && idom_[bb] != new_idom) {
                idom_[bb] = new_idom;
                changed = true;
            }
        }
    }
}

BasicBlock *LoopUnroll::intersect(BasicBlock *a, BasicBlock *b) {
    while (a != b) {
        while (rpoIdx_[a] > rpoIdx_[b]) a = idom_[a];
        while (rpoIdx_[b] > rpoIdx_[a]) b = idom_[b];
    }
    return a;
}

bool LoopUnroll::dominates(BasicBlock *a, BasicBlock *b) {
    while (b != idom_[b]) {
        if (b == a) return true;
        b = idom_[b];
    }
    return b == a;
}

std::vector<LoopUnroll::Loop> LoopUnroll::findLoops(Function *func) {
    std::vector<Loop> loops;
    for (auto bb : func->basic_blocks_) {
        for (auto succ : bb->succ_bbs_) {
            if (!idom_.count(succ)) continue;
            if (!dominates(succ, bb)) continue;
            Loop loop;
            loop.header = succ;
            loop.latch  = bb;
            loop.blocks.insert(succ);
            std::queue<BasicBlock *> wl;
            wl.push(bb);
            while (!wl.empty()) {
                auto cur = wl.front(); wl.pop();
                if (!loop.blocks.insert(cur).second) continue;
                for (auto pred : cur->pre_bbs_)
                    if (!loop.blocks.count(pred)) wl.push(pred);
            }
            loops.push_back(std::move(loop));
        }
    }
    return loops;
}

BasicBlock *LoopUnroll::getPreheader(const Loop &loop) {
    BasicBlock *pre = nullptr;
    for (auto pred : loop.header->pre_bbs_) {
        if (loop.blocks.count(pred)) continue;
        if (pre) return nullptr;
        pre = pred;
    }
    return pre;
}

// ── Instruction cloning ───────────────────────────────────────────────────

Instruction *LoopUnroll::cloneInst(Instruction *orig, BasicBlock *destBB,
                                    const std::unordered_map<Value *, Value *> &vmap) {
    auto remap = [&](Value *v) -> Value * {
        auto it = vmap.find(v);
        return it != vmap.end() ? it->second : v;
    };

    if (auto *bi = dynamic_cast<BinaryInst *>(orig))
        return new BinaryInst(bi->type_, bi->op_id_,
                               remap(bi->get_operand(0)),
                               remap(bi->get_operand(1)), destBB);

    if (auto *ui = dynamic_cast<UnaryInst *>(orig))
        return new UnaryInst(ui->type_, ui->op_id_,
                              remap(ui->get_operand(0)), destBB);

    if (auto *ci = dynamic_cast<ICmpInst *>(orig))
        return new ICmpInst(ci->icmp_op_,
                             remap(ci->get_operand(0)),
                             remap(ci->get_operand(1)), destBB);

    if (auto *fi = dynamic_cast<FCmpInst *>(orig))
        return new FCmpInst(fi->fcmp_op_,
                             remap(fi->get_operand(0)),
                             remap(fi->get_operand(1)), destBB);

    if (auto *gi = dynamic_cast<GetElementPtrInst *>(orig)) {
        std::vector<Value *> idxs;
        for (unsigned i = 1; i < gi->num_ops_; i++)
            idxs.push_back(remap(gi->get_operand(i)));
        return new GetElementPtrInst(remap(gi->get_operand(0)), idxs, destBB);
    }

    if (auto *li = dynamic_cast<LoadInst *>(orig))
        return new LoadInst(remap(li->get_operand(0)), destBB);

    if (auto *si = dynamic_cast<StoreInst *>(orig))
        return new StoreInst(remap(si->get_operand(0)),
                              remap(si->get_operand(1)), destBB);

    if (auto *zi = dynamic_cast<ZextInst *>(orig))
        return new ZextInst(zi->op_id_, remap(zi->get_operand(0)), zi->dest_ty_, destBB);

    if (auto *fp = dynamic_cast<FpToSiInst *>(orig))
        return new FpToSiInst(fp->op_id_, remap(fp->get_operand(0)), fp->dest_ty_, destBB);

    if (auto *sf = dynamic_cast<SiToFpInst *>(orig))
        return new SiToFpInst(sf->op_id_, remap(sf->get_operand(0)), sf->dest_ty_, destBB);

    if (auto *bc = dynamic_cast<Bitcast *>(orig))
        return new Bitcast(bc->op_id_, remap(bc->get_operand(0)), bc->dest_ty_, destBB);

    return nullptr; // unsupported (phi, branch, call, alloca …)
}

// ── Core unrolling ────────────────────────────────────────────────────────

bool LoopUnroll::tryUnroll(Loop &loop, Function *func, Module *module) {
    // Only handle simple 2-BB loops: header + latch
    if (loop.blocks.size() != 2) return false;

    BasicBlock *header = loop.header;
    BasicBlock *latch  = loop.latch;

    // Need a single preheader
    BasicBlock *preheader = getPreheader(loop);
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
    for (auto inst : latch->instr_list_) {
        if (inst->isTerminator()) continue;
        if (inst->is_call() || inst->is_phi() || inst->is_alloca()) return false;
        latchBodySize++;
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
    if (latchBodySize > MAX_LATCH_INSTS) return false;

    // ── Transformation ────────────────────────────────────────────────────

    int N   = UNROLL_FACTOR;
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

// ── Entry points ──────────────────────────────────────────────────────────

void LoopUnroll::runOnFunction(Function *func) {
    if (func->basic_blocks_.empty()) return;

    auto rpo = computeRPO(func);
    computeDominators(rpo);
    auto loops = findLoops(func);

    // Innermost first
    std::sort(loops.begin(), loops.end(), [](const Loop &a, const Loop &b) {
        return a.blocks.size() < b.blocks.size();
    });

    for (auto &loop : loops)
        tryUnroll(loop, func, func->parent_);

    func->set_instr_name();
}

void LoopUnroll::execute(Module *module) {
    for (auto func : module->function_list_)
        if (!func->is_declaration())
            runOnFunction(func);
}
