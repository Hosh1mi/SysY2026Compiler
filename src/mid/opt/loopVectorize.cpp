#include "../../include/mid/opt/loopVectorize.hpp"
#include <stack>
#include <queue>
#include <algorithm>
#include <functional>
#include <cassert>

// =====================================================================
// Vectorization
//
// Match:
//   for (int i = 0; i < N; i++) {
//       A[i] = op(B[i], C[i], ...);   
//   }

// Transformation:
//   int i = 0;
//   for (; i + VF <= N; i += VF) {
//       A[i+0] = op(B[i+0], C[i+0], ...);
//       A[i+1] = op(B[i+1], C[i+1], ...);
//       ...
//       A[i+VF-1] = op(B[i+VF-1], C[i+VF-1], ...);
//   }
//   for (; i < N; i++) {
//       A[i] = op(B[i], C[i], ...);
//   }
// =====================================================================

static const int VECTORIZE_FACTOR = 4;   // process 4 elements per iteration

// ── Entry point ──────────────────────────────────────────────────────────

void LoopVectorize::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

// =====================================================================
// 支配树计算
// =====================================================================

void LoopVectorize::computeDominators(Function *func) {
    dom_.clear();
    idom_.clear();

    auto *entryBB = func->basic_blocks_.front();

    // Collect all blocks
    std::set<BasicBlock*> allBlocks;
    for (auto bb : func->basic_blocks_) allBlocks.insert(bb);

    // Initialise
    for (auto bb : func->basic_blocks_) {
        if (bb == entryBB)
            dom_[bb] = {entryBB};
        else
            dom_[bb] = allBlocks;
    }

    // Iterative data-flow
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto bb : func->basic_blocks_) {
            if (bb == entryBB) continue;
            std::set<BasicBlock*> newDom = allBlocks;
            bool first = true;
            for (auto pred : bb->pre_bbs_) {
                if (first) {
                    newDom = dom_[pred];
                    first = false;
                } else {
                    std::set<BasicBlock*> temp;
                    std::set_intersection(newDom.begin(), newDom.end(),
                                          dom_[pred].begin(), dom_[pred].end(),
                                          std::inserter(temp, temp.begin()));
                    newDom = temp;
                }
            }
            newDom.insert(bb);
            if (newDom != dom_[bb]) {
                dom_[bb] = newDom;
                changed = true;
            }
        }
    }

    // Compute immediate dominator
    for (auto bb : func->basic_blocks_) {
        if (bb == entryBB) {
            idom_[bb] = nullptr;
            continue;
        }
        for (auto d : dom_[bb]) {
            if (d == bb) continue;
            bool isIdom = true;
            for (auto other : dom_[bb]) {
                if (other == bb || other == d) continue;
                if (dom_[d].count(other)) { isIdom = false; break; }
            }
            if (isIdom) {
                idom_[bb] = d;
                break;
            }
        }
    }
}

// =====================================================================
// 循环检测
// =====================================================================

std::vector<LoopVectorize::Loop> LoopVectorize::findLoops(Function *func) {
    std::vector<Loop> loops;

    for (auto bb : func->basic_blocks_) {
        for (auto succ : bb->succ_bbs_) {
            // back edge: bb -> succ  and  succ dominates bb
            if (!dom_[bb].count(succ)) continue;
            if (bb == succ) continue; // skip self-loop for now

            Loop loop;
            loop.header = succ;
            loop.latch  = bb;

            // Collect loop body by reverse traversal
            loop.blocks.insert(succ);
            if (bb != succ) {
                std::queue<BasicBlock*> wl;
                wl.push(bb);
                while (!wl.empty()) {
                    auto cur = wl.front(); wl.pop();
                    if (!loop.blocks.insert(cur).second) continue;
                    for (auto pred : cur->pre_bbs_)
                        if (!loop.blocks.count(pred)) wl.push(pred);
                }
            }

            // Find preheader (single predecessor outside the loop)
            loop.preheader = nullptr;
            for (auto pred : loop.header->pre_bbs_) {
                if (!loop.blocks.count(pred)) {
                    if (loop.preheader) { loop.preheader = nullptr; break; }
                    loop.preheader = pred;
                }
            }

            // Find unique exit block
            loop.exitBB = nullptr;
            for (auto b : loop.blocks) {
                for (auto succExit : b->succ_bbs_) {
                    if (!loop.blocks.count(succExit)) {
                        if (loop.exitBB && loop.exitBB != succExit) {
                            loop.exitBB = nullptr; // multiple exits
                        } else {
                            loop.exitBB = succExit;
                        }
                    }
                }
            }

            loops.push_back(std::move(loop));
        }
    }

    return loops;
}

// =====================================================================
// 循环不变值判断
// =====================================================================

bool LoopVectorize::isLoopInvariant(Value *val,
                                     const std::set<BasicBlock*> &loopBlocks) {
    if (dynamic_cast<Constant*>(val))       return true;
    if (dynamic_cast<Argument*>(val))       return true;
    if (dynamic_cast<GlobalVariable*>(val)) return true;

    auto *inst = dynamic_cast<Instruction*>(val);
    if (!inst) return false;

    return !loopBlocks.count(inst->parent_);
}

// =====================================================================
// 获取指针的基地址（遍历 GEP 链）
// =====================================================================

Value *LoopVectorize::getBasePtr(Value *ptr) {
    while (true) {
        if (auto *gep = dynamic_cast<GetElementPtrInst*>(ptr)) {
            ptr = gep->get_operand(0);
            continue;
        }
        if (auto *bc = dynamic_cast<Bitcast*>(ptr)) {
            ptr = bc->get_operand(0);
            continue;
        }
        break;
    }
    return ptr;
}

// =====================================================================
// 寻找归纳变量
// =====================================================================

bool LoopVectorize::findInductionVar(const Loop &loop, InductionVar &iv) {
    // Look for a phi in the header with integer type
    for (auto inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst*>(inst);

        if (phi->type_->tid_ != Type::IntegerTyID) continue;

        // Identify the incoming value from outside the loop (initVal)
        // and from inside (updateInst)
        Value *initVal   = nullptr;
        Value *latchVal  = nullptr;
        BasicBlock *latchBB = nullptr;

        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *pred = static_cast<BasicBlock*>(phi->get_operand(i + 1));
            if (loop.blocks.count(pred)) {
                if (latchVal) { latchVal = nullptr; break; }
                latchVal = phi->get_operand(i);
                latchBB  = pred;
            } else {
                if (initVal) { initVal = nullptr; break; }
                initVal = phi->get_operand(i);
            }
        }

        if (!initVal || !latchVal || !latchBB) continue;

        // The latch value must be an add or sub of the phi with a constant
        auto *updateInst = dynamic_cast<Instruction*>(latchVal);
        if (!updateInst) continue;
        if (!updateInst->is_add() && !updateInst->is_sub()) continue;

        // Find the stride constant
        Value *op0 = updateInst->get_operand(0);
        Value *op1 = updateInst->get_operand(1);

        int stride = 0;
        bool isAdd = updateInst->is_add();

        // pattern: phi + stride  or  stride + phi
        if (op0 == phi && dynamic_cast<ConstantInt*>(op1)) {
            stride = static_cast<ConstantInt*>(op1)->value_;
        } else if (isAdd && op1 == phi && dynamic_cast<ConstantInt*>(op0)) {
            stride = static_cast<ConstantInt*>(op0)->value_;
        }

        // We only support positive unit stride for now
        if (stride != 1) continue;

        iv.phi         = phi;
        iv.initVal     = initVal;
        iv.stride      = stride;
        iv.isAdd       = isAdd;
        iv.updateInst  = updateInst;
        return true;
    }

    return false;
}

// =====================================================================
// 分析循环中步长为 1 的连续内存访问
// =====================================================================

bool LoopVectorize::analyzeStrideAccesses(
    const Loop &loop, const InductionVar &iv,
    std::vector<MemAccess> &loads,
    std::vector<MemAccess> &stores)
{
    loads.clear();
    stores.clear();

    for (auto bb : loop.blocks) {
        for (auto inst : bb->instr_list_) {
            MemAccess ma;
            ma.inst = inst;

            if (inst->is_load()) {
                ma.kind = MemAccess::LOAD;
                Value *ptr = inst->get_operand(0);

                // Check if the pointer is a GEP with IV as the last index
                auto *gep = dynamic_cast<GetElementPtrInst*>(ptr);
                if (!gep) return false; // can't handle non-GEP loads

                // The last index should be the IV (or IV + constant offset)
                unsigned lastIdx = gep->num_ops_ - 1;
                Value *idxVal = gep->get_operand(lastIdx);

                if (idxVal == iv.phi) {
                    ma.elementOffset = 0;
                } else if (auto *addInst = dynamic_cast<BinaryInst*>(idxVal)) {
                    if (addInst->is_add()) {
                        Value *a = addInst->get_operand(0);
                        Value *b = addInst->get_operand(1);
                        if (a == iv.phi && dynamic_cast<ConstantInt*>(b)) {
                            ma.elementOffset = static_cast<ConstantInt*>(b)->value_;
                        } else if (b == iv.phi && dynamic_cast<ConstantInt*>(a)) {
                            ma.elementOffset = static_cast<ConstantInt*>(a)->value_;
                        } else {
                            return false;
                        }
                    } else {
                        return false;
                    }
                } else {
                    return false;
                }

                // All other indices must be loop-invariant
                for (unsigned i = 1; i < lastIdx; i++) {
                    if (!isLoopInvariant(gep->get_operand(i), loop.blocks))
                        return false;
                }

                ma.basePtr = getBasePtr(gep);
                loads.push_back(ma);

            } else if (inst->is_store()) {
                ma.kind = MemAccess::STORE;
                Value *ptr = inst->get_operand(1);

                auto *gep = dynamic_cast<GetElementPtrInst*>(ptr);
                if (!gep) return false;

                unsigned lastIdx = gep->num_ops_ - 1;
                Value *idxVal = gep->get_operand(lastIdx);

                if (idxVal == iv.phi) {
                    ma.elementOffset = 0;
                } else if (auto *addInst = dynamic_cast<BinaryInst*>(idxVal)) {
                    if (addInst->is_add()) {
                        Value *a = addInst->get_operand(0);
                        Value *b = addInst->get_operand(1);
                        if (a == iv.phi && dynamic_cast<ConstantInt*>(b)) {
                            ma.elementOffset = static_cast<ConstantInt*>(b)->value_;
                        } else if (b == iv.phi && dynamic_cast<ConstantInt*>(a)) {
                            ma.elementOffset = static_cast<ConstantInt*>(a)->value_;
                        } else {
                            return false;
                        }
                    } else {
                        return false;
                    }
                } else {
                    return false;
                }

                for (unsigned i = 1; i < lastIdx; i++) {
                    if (!isLoopInvariant(gep->get_operand(i), loop.blocks))
                        return false;
                }

                ma.basePtr = getBasePtr(gep);
                stores.push_back(ma);
            }
        }
    }

    // Must have at least one memory access to vectorize
    return !loads.empty() || !stores.empty();
}

// =====================================================================
// 判断一条指令是否可以被向量化（纯算术运算）
// =====================================================================

static bool isVectorizableInstruction(Instruction *inst,
                                       const std::set<BasicBlock*> &loopBlocks) {
    // Skip terminator, phi, and memory instructions (handled separately)
    if (inst->is_br() || inst->is_ret()) return true;  // terminator, keep
    if (inst->is_phi()) return true;   // phi, keep
    if (inst->is_load() || inst->is_store()) return true; // memory, handled by strip-mining
    if (inst->is_call()) return false;  // calls cannot be vectorized
    if (inst->is_alloca()) return false;

    // Binary/unary/cmp operations are vectorizable
    if (inst->is_binary()) return true;
    if (inst->is_cmp() || inst->is_fcmp()) return true;
    if (dynamic_cast<UnaryInst*>(inst)) return true;
    if (dynamic_cast<ZextInst*>(inst)) return true;
    if (dynamic_cast<FpToSiInst*>(inst)) return true;
    if (dynamic_cast<SiToFpInst*>(inst)) return true;

    // GEP is vectorizable if the IV-indexed dimension is the last index
    if (inst->is_gep()) return true;

    return false;
}

// =====================================================================
// 判断循环是否可以被向量化
// =====================================================================

bool LoopVectorize::tryVectorize(Loop &loop, Function *func, Module *module) {
    (void)module;
    (void)func;

    // Requirements:
    // 1. Loop must have a preheader
    if (!loop.preheader) return false;

    // 2. Loop must have a unique exit
    if (!loop.exitBB) return false;

    // 3. Loop should be small (single block or simple 2-block: header+latch)
    if (loop.blocks.size() > 2) return false;

    // 4. Find induction variable (unit stride, constant stride = 1)
    InductionVar iv;
    if (!findInductionVar(loop, iv)) return false;

    // 5. Find memory accesses with unit stride
    std::vector<MemAccess> loads, stores;
    if (!analyzeStrideAccesses(loop, iv, loads, stores)) return false;

    // 6. Check that all instructions in the loop are vectorizable
    for (auto bb : loop.blocks) {
        for (auto inst : bb->instr_list_) {
            if (!isVectorizableInstruction(inst, loop.blocks))
                return false;
        }
    }

    // 7. Count total memory accesses - very small loops may not benefit
    //    from vectorization
    if (loads.empty() && stores.empty()) return false;

    // 8. Find the trip count bound from the comparison instruction
    //    We need to know the loop bound to determine if strip-mining is viable
    //    For now, we'll emit both vectorized loop and remainder loop

    // All checks passed - proceed to vectorize
    emitVectorizedLoop(loop, iv, loads, stores, VECTORIZE_FACTOR, func, func->parent_);
    return true;
}

// =====================================================================
// 克隆一条指令（用于展开向量体，使用 value map）
// =====================================================================

static Instruction *cloneInst(Instruction *orig, BasicBlock *destBB,
                               const std::unordered_map<Value*, Value*> &vmap) {
    auto remap = [&](Value *v) -> Value* {
        auto it = vmap.find(v);
        return it != vmap.end() ? it->second : v;
    };

    if (auto *bi = dynamic_cast<BinaryInst*>(orig))
        return new BinaryInst(bi->type_, bi->op_id_,
                               remap(bi->get_operand(0)),
                               remap(bi->get_operand(1)), destBB);

    if (auto *ui = dynamic_cast<UnaryInst*>(orig))
        return new UnaryInst(ui->type_, ui->op_id_,
                              remap(ui->get_operand(0)), destBB);

    if (auto *ci = dynamic_cast<ICmpInst*>(orig))
        return new ICmpInst(ci->icmp_op_,
                             remap(ci->get_operand(0)),
                             remap(ci->get_operand(1)), destBB);

    if (auto *fi = dynamic_cast<FCmpInst*>(orig))
        return new FCmpInst(fi->fcmp_op_,
                             remap(fi->get_operand(0)),
                             remap(fi->get_operand(1)), destBB);

    if (auto *gi = dynamic_cast<GetElementPtrInst*>(orig)) {
        std::vector<Value*> idxs;
        for (unsigned i = 1; i < gi->num_ops_; i++)
            idxs.push_back(remap(gi->get_operand(i)));
        return new GetElementPtrInst(remap(gi->get_operand(0)), idxs, destBB);
    }

    if (auto *li = dynamic_cast<LoadInst*>(orig))
        return new LoadInst(remap(li->get_operand(0)), destBB);

    if (auto *si = dynamic_cast<StoreInst*>(orig))
        return new StoreInst(remap(si->get_operand(0)),
                              remap(si->get_operand(1)), destBB);

    if (auto *zi = dynamic_cast<ZextInst*>(orig))
        return new ZextInst(zi->op_id_, remap(zi->get_operand(0)),
                             zi->dest_ty_, destBB);

    if (auto *fp = dynamic_cast<FpToSiInst*>(orig))
        return new FpToSiInst(fp->op_id_, remap(fp->get_operand(0)),
                               fp->dest_ty_, destBB);

    if (auto *sf = dynamic_cast<SiToFpInst*>(orig))
        return new SiToFpInst(sf->op_id_, remap(sf->get_operand(0)),
                               sf->dest_ty_, destBB);

    if (auto *bc = dynamic_cast<Bitcast*>(orig))
        return new Bitcast(bc->op_id_, remap(bc->get_operand(0)),
                            bc->dest_ty_, destBB);

    return nullptr;
}

// =====================================================================
// 生成向量化循环
//
// 将原循环分为两部分：
//   1) Vectorized main loop：每轮迭代处理 VF 个元素
//   2) Scalar remainder loop：处理剩余元素
// =====================================================================

void LoopVectorize::emitVectorizedLoop(
    const Loop &loop, const InductionVar &iv,
    const std::vector<MemAccess> &loads,
    const std::vector<MemAccess> &stores,
    int vecWidth, Function *func, Module *module)
{
    BasicBlock *preheader  = loop.preheader;
    BasicBlock *origHeader = loop.header;
    BasicBlock *origLatch  = loop.latch;
    BasicBlock *origExit   = loop.exitBB;

    // We only handle the case where the loop header has a conditional branch
    // that exits to origExit when the condition is false, and goes to the
    // loop body (latch) when true.
    auto *headerBr = origHeader->get_terminator();
    if (!headerBr || !headerBr->is_br() || headerBr->num_ops_ != 3) return;

    // We need the condition to be icmp iv < bound
    Value *cond = headerBr->get_operand(0);
    auto *cmpInst = dynamic_cast<ICmpInst*>(cond);
    if (!cmpInst) return;

    // The cmp operand should involve the IV
    Value *cmpOp0 = cmpInst->get_operand(0);
    Value *cmpOp1 = cmpInst->get_operand(1);

    Value *bound = nullptr;
    bool ivIsLeft = true;
    if (cmpOp0 == iv.phi) {
        bound = cmpOp1;
        ivIsLeft = true;
    } else if (cmpOp1 == iv.phi) {
        bound = cmpOp0;
        ivIsLeft = false;
    } else {
        return;
    }

    // Bound must be loop-invariant
    if (!isLoopInvariant(bound, loop.blocks)) return;

    // ── Create new blocks ──────────────────────────────────────────

    // 1. Vectorized loop header: check if i + VF <= bound
    BasicBlock *vecHeader = new BasicBlock(module, "vec_hdr", func);

    // 2. Vectorized loop body: contains VF unrolled copies of the loop body
    BasicBlock *vecBody = new BasicBlock(module, "vec_body", func);

    // 3. Remainder loop header: handles leftover iterations
    BasicBlock *remHeader = new BasicBlock(module, "rem_hdr", func);

    // 4. Exit block: after both loops complete
    BasicBlock *afterLoop = new BasicBlock(module, "vec_exit", func);

    // ── Build the vectorized main loop ─────────────────────────────

    // Create a phi for the induction variable in vecHeader
    //   vec_phi = phi [iv.initVal, preheader], [vec_next, vec_body]
    auto *vecPhi = PhiInst::create_phi(module->int32_ty_, vecHeader);
    vecHeader->add_instruction_front(vecPhi);
    vecPhi->addIncoming(iv.initVal, preheader);

    // Compute bound - VF for the vectorized loop's upper bound check
    //   if vec_phi <= bound - VF, continue vectorized loop
    //   else go to remainder loop
    int adj = vecWidth;  // VF
    Value *boundMain;
    if (auto *cb = dynamic_cast<ConstantInt*>(bound)) {
        boundMain = new ConstantInt(module->int32_ty_, cb->value_ - adj);
    } else {
        auto *adjConst = new ConstantInt(module->int32_ty_, adj);
        auto *subInst  = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                         bound, adjConst, preheader, false);
        preheader->add_instruction_before_terminator(subInst);
        boundMain = subInst;
    }

    // Create the comparison: vec_phi < boundMain (or the appropriate op)
    ICmpInst::ICmpOp vecCmpOp;
    if (ivIsLeft) {
        vecCmpOp = cmpInst->icmp_op_;
    } else {
        // Reverse the comparison for iv on the right
        switch (cmpInst->icmp_op_) {
            case ICmpInst::ICMP_SLT: vecCmpOp = ICmpInst::ICMP_SGT; break;
            case ICmpInst::ICMP_SLE: vecCmpOp = ICmpInst::ICMP_SGE; break;
            case ICmpInst::ICMP_SGT: vecCmpOp = ICmpInst::ICMP_SLT; break;
            case ICmpInst::ICMP_SGE: vecCmpOp = ICmpInst::ICMP_SLE; break;
            default: vecCmpOp = cmpInst->icmp_op_; break; // fallback
        }
    }

    ICmpInst *vecCmp;
    if (ivIsLeft)
        vecCmp = new ICmpInst(vecCmpOp, vecPhi, boundMain, vecHeader);
    else
        vecCmp = new ICmpInst(vecCmpOp, boundMain, vecPhi, vecHeader);

    // Branch: true -> vecBody, false -> remHeader
    new BranchInst(vecCmp, vecBody, remHeader, vecHeader);

    // ── Build vectorized body (VF copies of the loop body) ─────────

    // Collect non-terminator instructions from the original loop in order
    std::vector<Instruction*> bodyInsts;
    for (auto bb : loop.blocks) {
        for (auto inst : bb->instr_list_) {
            if (inst->isTerminator() || inst->is_phi()) continue;
            bodyInsts.push_back(inst);
        }
    }

    // Create the increment value for the next iteration
    // vec_next = vecPhi + VF
    auto *vecStride = new ConstantInt(module->int32_ty_, vecWidth);
    auto *vecNext   = new BinaryInst(module->int32_ty_, Instruction::Add,
                                      vecPhi, vecStride, vecBody);
    vecPhi->addIncoming(vecNext, vecBody);

    // Generate VF copies
    for (int j = 0; j < vecWidth; j++) {
        // Build the value map for copy j
        // IV -> vecPhi + j
        std::unordered_map<Value*, Value*> vmap;

        // Map the IV phi -> vecPhi + j
        if (j == 0) {
            vmap[iv.phi] = vecPhi;
        } else {
            auto *offset = new ConstantInt(module->int32_ty_, j);
            auto *iv_j   = new BinaryInst(module->int32_ty_, Instruction::Add,
                                           vecPhi, offset, vecBody);
            vmap[iv.phi] = iv_j;
        }

        // For each non-phi instruction in the body, create a clone
        // with the remapped operands
        for (auto *origInst : bodyInsts) {
            // Skip the IV update instruction (it's handled by the stride)
            if (origInst == iv.updateInst) continue;

            // Skip GEP instructions that are only used by the updateInst
            // (we'll create fresh GEPs)
            auto *newInst = cloneInst(origInst, vecBody, vmap);
            if (!newInst) continue;
            vmap[origInst] = newInst;
        }
    }

    // Branch back to vecHeader
    new BranchInst(vecHeader, vecBody);

    // ── Build remainder loop header ──────────────────────────────────

    // Create phi for remainder loop
    auto *remPhi = PhiInst::create_phi(module->int32_ty_, remHeader);
    remHeader->add_instruction_front(remPhi);

    remPhi->addIncoming(vecPhi, vecHeader);

    // The remainder loop will use the original comparison
    ICmpInst *remCmp;
    if (ivIsLeft)
        remCmp = new ICmpInst(cmpInst->icmp_op_, remPhi, bound, remHeader);
    else
        remCmp = new ICmpInst(cmpInst->icmp_op_, bound, remPhi, remHeader);

    // Branch: true -> remBody (which is origHeader redirected), false -> afterLoop
    new BranchInst(remCmp, origHeader, afterLoop, remHeader);

    // ── Redirect original loop to be the remainder body ──────────────

    // Update the original header phi: change preheader incoming to remHeader
    for (auto inst : origHeader->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst*>(inst);

        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == preheader) {
                // Replace preheader with remHeader for the incoming
                Value *val = phi->get_operand(i);
                // Remove old incoming
                phi->get_operand(i)->remove_use(phi->use_pos_[i]);
                phi->get_operand(i + 1)->remove_use(phi->use_pos_[i + 1]);
                // Set new: value = same, block = remHeader
                phi->operands_[i]     = val;
                phi->use_pos_[i]      = val->add_use(phi, i);
                phi->operands_[i + 1] = remHeader;
                phi->use_pos_[i + 1]  = remHeader->add_use(phi, i + 1);
                break;
            }
        }
    }

    // Also add the remPhi -> remHeader mapping
    // For the IV phi, the incoming from vecHeader should be remPhi
    // This is because remPhi takes the value of vecPhi when entering remHeader
    for (auto inst : origHeader->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst*>(inst);
        if (phi == iv.phi) {
            // Find the vecHeader incoming and replace it
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                if (phi->get_operand(i + 1) == remHeader) {
                    // Add remPhi as the new incoming (it already has vecHeader->remHeader)
                    // We need to connect: from vecHeader, the IV value comes through remPhi
                    phi->get_operand(i)->remove_use(phi->use_pos_[i]);
                    phi->operands_[i]    = remPhi;
                    phi->use_pos_[i]     = remPhi->add_use(phi, i);
                    break;
                }
            }
        }
    }

    // Redirect the preheader branch: preheader -> vecHeader (instead of origHeader)
    auto *preheaderBr = preheader->get_terminator();
    for (unsigned i = 0; i < preheaderBr->num_ops_; i++) {
        if (preheaderBr->get_operand(i) == origHeader) {
            preheaderBr->get_operand(i)->remove_use(preheaderBr->use_pos_[i]);
            preheaderBr->operands_[i] = vecHeader;
            preheaderBr->use_pos_[i]  = vecHeader->add_use(preheaderBr, i);
            break;
        }
    }
    preheader->remove_succ_basic_block(origHeader);
    preheader->add_succ_basic_block(vecHeader);
    origHeader->remove_pre_basic_block(preheader);
    vecHeader->add_pre_basic_block(preheader);

    // Redirect the latch of the remainder loop to branch to remHeader
    // (Currently, origLatch branches back to origHeader. Change it to remHeader.)
    auto *latchBr = origLatch->get_terminator();
    for (unsigned i = 0; i < latchBr->num_ops_; i++) {
        if (latchBr->get_operand(i) == origHeader) {
            latchBr->get_operand(i)->remove_use(latchBr->use_pos_[i]);
            latchBr->operands_[i] = remHeader;
            latchBr->use_pos_[i]  = remHeader->add_use(latchBr, i);
            break;
        }
    }
    origLatch->remove_succ_basic_block(origHeader);
    origLatch->add_succ_basic_block(remHeader);
    origHeader->remove_pre_basic_block(origLatch);
    remHeader->add_pre_basic_block(origLatch);

    // Add remPhi incoming from origLatch (the latch's phi value)
    for (auto inst : origHeader->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst*>(inst);
        if (phi == iv.phi) {
            // Find the latch value
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                auto *pred = static_cast<BasicBlock*>(phi->get_operand(i + 1));
                if (pred == origLatch) {
                    // The phi value from origLatch becomes the back-edge value for remPhi
                    remPhi->addIncoming(phi->get_operand(i), origLatch);
                    break;
                }
            }
        }
    }

    // Add the backedge for remPhi: the value from origLatch's update instruction
    remPhi->addIncoming(iv.updateInst, origLatch);

    for (auto bb : loop.blocks) {
        auto *term = bb->get_terminator();
        for (unsigned i = 0; i < term->num_ops_; i++) {
            auto *succ = dynamic_cast<BasicBlock*>(term->get_operand(i));
            if (succ && succ == origExit) {
                term->get_operand(i)->remove_use(term->use_pos_[i]);
                term->operands_[i] = afterLoop;
                term->use_pos_[i]  = afterLoop->add_use(term, i);
                succ->remove_pre_basic_block(bb);
                afterLoop->add_pre_basic_block(bb);
            }
        }
    }

    bool exitHasPhis = false;
    for (auto inst : origExit->instr_list_) {
        if (inst->is_phi()) { exitHasPhis = true; break; }
    }

    if (!exitHasPhis) {
        new BranchInst(origExit, afterLoop);
    } else {
        for (auto inst : origExit->instr_list_) {
            if (!inst->is_phi()) break;
            auto *phi = static_cast<PhiInst*>(inst);
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                auto *pred = static_cast<BasicBlock*>(phi->get_operand(i + 1));
                if (loop.blocks.count(pred)) {
                    Value *val = phi->get_operand(i);
                }
            }
        }
        new BranchInst(origExit, afterLoop);
    }

    // ── Clean up: set function to renumber instructions ──────────
    func->set_instr_name();
}

// =====================================================================
// 对函数运行向量化
// =====================================================================

void LoopVectorize::runOnFunction(Function *func) {
    if (func->basic_blocks_.empty()) return;

    computeDominators(func);
    auto loops = findLoops(func);

    bool changed = true;
    for (int iter = 0; iter < 5 && changed; iter++) {
        changed = false;
        std::sort(loops.begin(), loops.end(), [](const Loop &a, const Loop &b) {
            return a.blocks.size() < b.blocks.size();
        });

        for (auto &loop : loops) {
            if (tryVectorize(loop, func, func->parent_)) {
                changed = true;
                // Need to recompute dominators and loops after transformation
                computeDominators(func);
                loops = findLoops(func);
                break; // restart iteration
            }
        }
    }

    func->set_instr_name();
}
