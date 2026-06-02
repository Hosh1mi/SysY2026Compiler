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

// Environment variable to enable new vector IR path (instead of scalar unrolling).
// When enabled, simple load-binop-store loops emit <4 x i32>/<4 x float> IR.
// When disabled (default), scalar unrolling + backend pattern matching is used.
static const bool useVectorIR = true;

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

        // pattern: phi + stride  or  stride + phi  (or  sub phi, |stride|)
        if (op0 == phi && dynamic_cast<ConstantInt*>(op1)) {
            stride = static_cast<ConstantInt*>(op1)->value_;
            if (!isAdd) stride = -stride; // sub phi, c  means stride = -c
        } else if (isAdd && op1 == phi && dynamic_cast<ConstantInt*>(op0)) {
            stride = static_cast<ConstantInt*>(op0)->value_;
        }

        // Only unit stride ±1 is supported
        if (stride != 1 && stride != -1) continue;

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

    // 5. Check for non-IV phis (accumulators, pointer phis from LICM, etc.).
    //    Pointer phis are handled by gep(phi, j) in headerNonIVPhis.
    //    Integer phis with a constant-offset update (add/sub phi, c) are
    //    handled by add(phi, j).  Accumulator phis ("sum += product") are
    //    rejected — they need cross-copy chaining which isn't implemented.
    for (auto inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        if (inst == iv.phi) continue;
        auto *phi = static_cast<PhiInst*>(inst);
        if (phi->type_->tid_ == Type::PointerTyID) continue; // pointer phi: OK
        // Integer non-IV phi: check whether it's a constant-offset pattern
        //   e.g.  %idx = phi [0], [add %idx, 1]
        // or an accumulator:
        //   e.g.  %sum = phi [0], [add %sum, %product]
        Value *latchVal = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (loop.blocks.count(static_cast<BasicBlock*>(phi->get_operand(i + 1)))) {
                latchVal = phi->get_operand(i); break;
            }
        }
        if (!latchVal) return false;
        auto *update = dynamic_cast<Instruction*>(latchVal);
        if (!update || (!update->is_add() && !update->is_sub())) return false;
        // Must be add/sub phi, constant  (not add phi, variable)
        Value *op0 = update->get_operand(0), *op1 = update->get_operand(1);
        bool isConst = (op0 == phi && dynamic_cast<ConstantInt*>(op1)) ||
                       (update->is_add() && op1 == phi && dynamic_cast<ConstantInt*>(op0));
        if (!isConst) return false; // accumulator — not yet supported
    }

    // 6. Find memory accesses with unit stride
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

    // Compute the vectorized loop's upper bound check.
    //   stride > 0: if vec_phi < bound - VF, continue; else go to remainder
    //   stride < 0: if vec_phi > bound + VF, continue; else go to remainder
    int adj = vecWidth;  // VF
    bool negStride = (iv.stride < 0);
    Value *boundMain;
    if (auto *cb = dynamic_cast<ConstantInt*>(bound)) {
        boundMain = new ConstantInt(module->int32_ty_,
                                     negStride ? cb->value_ + adj : cb->value_ - adj);
    } else {
        auto *adjConst = new ConstantInt(module->int32_ty_, adj);
        Instruction::OpID op = negStride ? Instruction::Add : Instruction::Sub;
        auto *adjInst  = new BinaryInst(module->int32_ty_, op,
                                         bound, adjConst, preheader, false);
        preheader->add_instruction_before_terminator(adjInst);
        boundMain = adjInst;
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
    // vec_next = vecPhi + (stride>0 ? VF : -VF)
    int step = negStride ? -vecWidth : vecWidth;
    auto *vecStride = new ConstantInt(module->int32_ty_, step);
    auto *vecNext   = new BinaryInst(module->int32_ty_, Instruction::Add,
                                      vecPhi, vecStride, vecBody);
    vecPhi->addIncoming(vecNext, vecBody);

    // —— 向量体生成 ——
    // 模式 A (纯 load-binop-store)：全向量化（vector load → vector binop → vector store）
    // 模式 B (IV 参与运算)：标量展开 + insertelement 打包 + vector store
    //
    // 判断：如果所有非 GEP/load/store 指令的操作数都不直接依赖 IV，
    //       则可以用模式 A 全向量化。
    // 但如果有非 IV phi（pointer phi、累加器等），模式 A 无法处理，
    // 必须走模式 B 让 headerNonIVPhis 映射来正确 remap 这些 phi。
    bool patternA = true;
    for (auto inst : origHeader->instr_list_) {
        if (!inst->is_phi()) break;
        if (inst != iv.phi) { patternA = false; break; }
    }
    for (auto *origInst : bodyInsts) {
        if (origInst == iv.updateInst || origInst->is_phi() || origInst->is_gep()) continue;
        // Skip ICmp: it is loop control flow, not a data operation
        if (dynamic_cast<ICmpInst*>(origInst)) continue;
        // Check if IV appears as a direct operand
        for (unsigned i = 0; i < origInst->num_ops_; i++) {
            if (origInst->get_operand(i) == iv.phi) {
                patternA = false; break;
            }
        }
        // Reject SDiv/SRem/FDiv and all float binops
        if (auto *bi = dynamic_cast<BinaryInst*>(origInst)) {
            if (bi->op_id_ == Instruction::SDiv ||
                bi->op_id_ == Instruction::SRem ||
                bi->op_id_ == Instruction::FDiv ||
                bi->type_->tid_ == Type::FloatTyID) {
                patternA = false; break;
            }
        }
        // Reject non-load/store ops that aren't binary
        if (!origInst->is_load() && !origInst->is_store() &&
            !dynamic_cast<BinaryInst*>(origInst) &&
            !dynamic_cast<UnaryInst*>(origInst)) {
            patternA = false; break;
        }
    }

    if (patternA) {
        // ── 模式 A: 全向量化 IR ──
        auto getVecTy = [&](Type *scalarTy) -> Type* {
            return module->get_vector_type(scalarTy, vecWidth);
        };
        auto getVecPtrTy = [&](Type *scalarTy) -> Type* {
            return module->get_pointer_type(getVecTy(scalarTy));
        };

        std::unordered_map<Value*, Value*> vmap;
        std::unordered_map<Instruction*, Value*> bcMap; // GEP -> bitcast
        vmap[iv.phi] = vecPhi;

        for (auto *origInst : bodyInsts) {
            if (origInst == iv.updateInst) continue;
            if (origInst->is_phi()) continue;

            // GEP: create new GEP + bitcast if feeds memory
            if (auto *gep = dynamic_cast<GetElementPtrInst*>(origInst)) {
                std::vector<Value*> idxs;
                for (unsigned i = 1; i < gep->num_ops_; i++) {
                    Value *idx = gep->get_operand(i);
                    auto it = vmap.find(idx);
                    idxs.push_back(it != vmap.end() ? it->second : idx);
                }
                // For negative stride, the last index (IV) needs adjustment:
                // vecPhi points to the LAST element of the VF-element block,
                // but the vector store writes forward.  Shift it back by VF-1.
                if (negStride && !idxs.empty()) {
                    Value *lastIdx = idxs.back();
                    if (lastIdx == vecPhi) {
                        auto *adjC = new ConstantInt(module->int32_ty_, vecWidth - 1);
                        auto *adjIdx = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                                       lastIdx, adjC, vecBody);
                        idxs.back() = adjIdx;
                    }
                }
                auto *newGep = new GetElementPtrInst(
                    gep->get_operand(0), idxs, vecBody);
                vmap[origInst] = newGep;
                bool feedsMem = false;
                for (auto &use : origInst->use_list_) {
                    if (auto *ui = dynamic_cast<Instruction*>(use.val_))
                        if (ui->is_load() || ui->is_store()) feedsMem = true;
                }
                if (feedsMem) {
                    Type *elemTy = static_cast<PointerType*>(gep->type_)->contained_;
                    auto *bc = new Bitcast(Instruction::BitCast, newGep,
                                           getVecPtrTy(elemTy), vecBody);
                    bcMap[origInst] = bc;
                }
                continue;
            }

            // Load: vector load from bitcast
            if (auto *load = dynamic_cast<LoadInst*>(origInst)) {
                auto *origPtr = dynamic_cast<Instruction*>(load->get_operand(0));
                auto bcIt = origPtr ? bcMap.find(origPtr) : bcMap.end();
                if (bcIt != bcMap.end()) {
                    vmap[origInst] = new LoadInst(bcIt->second, vecBody);
                } else {
                    auto ptrIt = vmap.find(load->get_operand(0));
                    vmap[origInst] = new LoadInst(
                        (ptrIt != vmap.end()) ? ptrIt->second : load->get_operand(0), vecBody);
                }
                continue;
            }

            // Store: vector store if value is vector
            if (auto *store = dynamic_cast<StoreInst*>(origInst)) {
                Value *origVal = store->get_operand(0);
                Value *origPtr = store->get_operand(1);
                auto valIt = vmap.find(origVal);
                Value *newVal = (valIt != vmap.end()) ? valIt->second : origVal;
                auto *ptrInst = dynamic_cast<Instruction*>(origPtr);
                auto bcIt = ptrInst ? bcMap.find(ptrInst) : bcMap.end();
                Value *newPtr = nullptr;
                if (bcIt != bcMap.end()) {
                    if (newVal->type_->tid_ == Type::VectorTyID) {
                        newPtr = bcIt->second;
                    } else {
                        // Scalar value on a vector-gep: splat into vector
                        // so the store covers all VF elements.  Without this,
                        // a scalar store + VF stride skips 3/4 of elements.
                        Type *vecTy = getVecTy(newVal->type_);
                        Value *result = nullptr;
                        for (int l = 0; l < vecWidth; l++) {
                            auto *idx = new ConstantInt(module->int32_ty_, l);
                            Value *base = result ? result
                                : static_cast<Value*>(new ConstantZero(vecTy));
                            result = new InsertElementInst(base, newVal, idx, vecBody);
                        }
                        newVal = result;
                        newPtr = bcIt->second;
                    }
                } else {
                    auto ptrIt = vmap.find(origPtr);
                    newPtr = (ptrIt != vmap.end()) ? ptrIt->second : origPtr;
                }
                if (newVal && newPtr) new StoreInst(newVal, newPtr, vecBody);
                continue;
            }

            // BinaryInst: promote to vector type; splat scalar operands
            if (auto *bi = dynamic_cast<BinaryInst*>(origInst)) {
                Value *r0 = nullptr, *r1 = nullptr;
                auto it0 = vmap.find(bi->get_operand(0));
                auto it1 = vmap.find(bi->get_operand(1));
                r0 = (it0 != vmap.end()) ? it0->second : bi->get_operand(0);
                r1 = (it1 != vmap.end()) ? it1->second : bi->get_operand(1);
                Type *resTy = bi->type_;
                if (r0->type_->tid_ == Type::VectorTyID) resTy = r0->type_;
                else if (r1->type_->tid_ == Type::VectorTyID) resTy = r1->type_;
                // Splat any scalar operand that is paired with a vector operand
                if (resTy->tid_ == Type::VectorTyID) {
                    auto splat = [&](Value *&op) {
                        if (op->type_->tid_ != Type::VectorTyID) {
                            Value *result = nullptr;
                            for (int l = 0; l < vecWidth; l++) {
                                auto *idx = new ConstantInt(module->int32_ty_, l);
                                Value *base = result ? result
                                    : static_cast<Value*>(new ConstantZero(resTy));
                                result = new InsertElementInst(base, op, idx, vecBody);
                            }
                            op = result;
                        }
                    };
                    splat(r0);
                    splat(r1);
                }
                vmap[origInst] = new BinaryInst(resTy, bi->op_id_, r0, r1, vecBody);
                continue;
            }

            // Other: clone as scalar
            auto remap = [&](Value *v) -> Value* {
                auto it = vmap.find(v);
                return it != vmap.end() ? it->second : v;
            };
            if (auto *ui = dynamic_cast<UnaryInst*>(origInst))
                vmap[origInst] = new UnaryInst(ui->type_, ui->op_id_,
                                                remap(ui->get_operand(0)), vecBody);
        }
    } else {
        // ── 标量展开（模式 B 或未开启 VECTOR_IR）──
        // Pre-collect non-IV phis from the loop header so cloned
        // instructions (stores, etc.) can remap them correctly.
        // Without this, all j>0 clones would reference the original
        // phi and write to the same address, losing 3/4 of stores.
        std::vector<PhiInst*> headerNonIVPhis;
        for (auto inst : origHeader->instr_list_) {
            if (!inst->is_phi()) break;
            if (inst != iv.phi)
                headerNonIVPhis.push_back(static_cast<PhiInst*>(inst));
        }

        for (int j = 0; j < vecWidth; j++) {
            std::unordered_map<Value*, Value*> vmap;

            if (j == 0) {
                vmap[iv.phi] = vecPhi;
            } else {
                auto *offset = new ConstantInt(module->int32_ty_, j);
                auto *iv_j   = new BinaryInst(module->int32_ty_, Instruction::Add,
                                            vecPhi, offset, vecBody);
                vmap[iv.phi] = iv_j;
            }

            // Map non-IV header phis:
            //   offset 0 → gep(phi, 0) for pointers (so VECTOR_IR can
            //     collect the store; gep 0 is a no-op), original phi for ints;
            //   offset j>0 → gep(phi, j) for pointers, add(phi, j) for ints
            for (auto *phi : headerNonIVPhis) {
                auto *offConst = new ConstantInt(module->int32_ty_, j);
                if (phi->type_->tid_ == Type::PointerTyID) {
                    vmap[phi] = new GetElementPtrInst(phi, {offConst}, vecBody);
                } else if (j == 0) {
                    vmap[phi] = phi;
                } else {
                    vmap[phi] = new BinaryInst(phi->type_, Instruction::Add,
                                               phi, offConst, vecBody);
                }
            }
    
            for (auto *origInst : bodyInsts) {
                if (origInst == iv.updateInst) continue;
                auto *newInst = cloneInst(origInst, vecBody, vmap);
                if (!newInst) continue;
                vmap[origInst] = newInst;
            }
        }
    }

    // —— VECTOR_IR 后处理：标量展开 → 向量算术 + 向量 store ——
    // 策略：
    //   1. 收集 store，按 GEP base 分成 4-offset 组
    //   2. 对每组，追踪 stored value 的来源 binop
    //   3. 将 binop 的两个操作数分别 pack 成向量
    //      - 循环不变量 → preheader 中 splat
    //      - IV 相关量 → vecBody 中 insertelement 打包 4 个 offset 版本
    //   4. 创建 vector binop + vector store
    //   5. 删除旧的 scalar binop 和 scalar store
    if (!patternA) {
        auto getVecTy = [&](Type *scalarTy) -> Type* {
            return module->get_vector_type(scalarTy, vecWidth);
        };
        auto getVecPtrTy = [&](Type *scalarTy) -> Type* {
            return module->get_pointer_type(getVecTy(scalarTy));
        };

        // Helper: splat a scalar into <VF x scalar> in a given block.
        // For blocks that already have a terminator (preheader), insert before it.
        auto emitSplat = [&](Value *scalar, BasicBlock *bb) -> Value* {
            Type *vecTy = getVecTy(scalar->type_);
            Value *result = nullptr;
            bool hasTerm = bb->get_terminator() != nullptr;
            for (int j = 0; j < vecWidth; j++) {
                auto *idxConst = new ConstantInt(module->int32_ty_, j);
                Value *base = result ? result : scalar;
                auto *ins = new InsertElementInst(base, scalar, idxConst, bb);
                if (j == 0) ins->type_ = vecTy;
                if (hasTerm) {
                    bb->remove_instr(ins);
                    bb->add_instruction_before_terminator(ins);
                }
                result = ins;
            }
            return result;
        };

        // Helper: pack 4 scalar values (at offsets 0..3) into a vector in vecBody
        auto emitPack4 = [&](Value *vals[4], BasicBlock *bb) -> Value* {
            Type *vecTy = getVecTy(vals[0]->type_);
            Value *result = nullptr;
            for (int j = 0; j < vecWidth; j++) {
                auto *idxConst = new ConstantInt(module->int32_ty_, j);
                Value *base = result ? result : vals[j];
                auto *ins = new InsertElementInst(base, vals[j], idxConst, bb);
                if (j == 0) ins->type_ = vecTy;
                result = ins;
            }
            return result;
        };

        // Step 1: Collect stores, grouped by GEP base key.
        // Also handle pointer-phi stores (e.g. store to %ptr_phi),
        // which represent offset 0 but have no GEP-typed pointer.
        struct StoreInfo {
            StoreInst *store;
            Value *storedVal;
            GetElementPtrInst *gep; // may be null for pointer-phi stores
            int offset;
        };
        std::vector<StoreInfo> storeInfos;
        for (auto inst : vecBody->instr_list_) {
            if (auto *si = dynamic_cast<StoreInst*>(inst)) {
                Value *ptr = si->get_operand(1);
                if (auto *gep = dynamic_cast<GetElementPtrInst*>(ptr)) {
                    unsigned lastIdx = gep->num_ops_ - 1;
                    Value *lastIdxVal = gep->get_operand(lastIdx);
                    int offset = -1;
                    if (lastIdxVal == vecPhi) offset = 0;
                    else if (auto *addInst = dynamic_cast<BinaryInst*>(lastIdxVal)) {
                        if (addInst->is_add()) {
                            Value *a0 = addInst->get_operand(0);
                            Value *a1 = addInst->get_operand(1);
                            if (a0 == vecPhi && dynamic_cast<ConstantInt*>(a1))
                                offset = static_cast<ConstantInt*>(a1)->value_;
                            else if (a1 == vecPhi && dynamic_cast<ConstantInt*>(a0))
                                offset = static_cast<ConstantInt*>(a0)->value_;
                        }
                    } else if (auto *ci = dynamic_cast<ConstantInt*>(lastIdxVal)) {
                        // gep ptr, constant — e.g. from LICM pointer-phi remapping
                        offset = ci->value_;
                    }
                    if (offset >= 0)
                        storeInfos.push_back({si, si->get_operand(0), gep, offset});
                } else if (auto *phi = dynamic_cast<PhiInst*>(ptr)) {
                    // Pointer-phi store: represents offset 0.
                    // Record with nullptr gep; the group will borrow a real
                    // GEP from a sibling store for vector-GEP construction.
                    if (phi->parent_ == vecHeader)
                        storeInfos.push_back({si, si->get_operand(0), nullptr, 0});
                }
            }
        }

        auto baseKey = [](GetElementPtrInst *gep) -> std::string {
            std::string key;
            for (unsigned i = 0; i < gep->num_ops_ - 1; i++)
                key += gep->get_operand(i)->name_ + "|";
            return key;
        };
        // For pointer-phi stores (gep == nullptr), derive the group key
        // from the phi's initial value, which is always a GEP like
        //   gep @C, 0, k, 0  →  baseKey = "@C|0|k|"
        auto phiBaseKey = [&](PhiInst *phi) -> std::string {
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                if (phi->get_operand(i + 1) == preheader) {
                    if (auto *gi = dynamic_cast<GetElementPtrInst*>(phi->get_operand(i)))
                        return baseKey(gi);
                }
            }
            return "__no_preheader__";
        };
        std::map<std::string, std::vector<StoreInfo*>> groups;
        for (auto &si : storeInfos) {
            if (si.gep) {
                groups[baseKey(si.gep)].push_back(&si);
            } else {
                // Pointer-phi store: derive key from the phi's initial gep
                auto *phi = static_cast<PhiInst*>(si.store->get_operand(1));
                groups[phiBaseKey(phi)].push_back(&si);
            }
        }

        // Cache for splatted invariants: scalar Value* → vector Value*
        std::unordered_map<Value*, Value*> splatCache;
        // Cache for vector IV phi: once created, shared across all store groups
        Value *vecIVPhi = nullptr;
        Value *vecIVInc  = nullptr; // <4,4,4,4> increment vector in preheader
        bool vecIVPhiNeedsIncoming = false; // true once phiNext is created

        // Helper: detect if opScalar[0..3] is the IV step pattern: j, j+1, j+2, j+3
        auto isIVStep = [&](Value *opScalar[4]) -> bool {
            if (opScalar[0] != vecPhi) return false;
            for (int j = 1; j < vecWidth; j++) {
                auto *addInst = dynamic_cast<BinaryInst*>(opScalar[j]);
                if (!addInst || !addInst->is_add()) return false;
                Value *a0 = addInst->get_operand(0), *a1 = addInst->get_operand(1);
                auto *ci = dynamic_cast<ConstantInt*>(a0 == vecPhi ? a1 : (a1 == vecPhi ? a0 : nullptr));
                if (!ci || ci->value_ != j) return false;
            }
            return true;
        };

        for (auto &kv : groups) {
            auto &vec = kv.second;
            if (vec.size() < (size_t)vecWidth) continue;
            std::sort(vec.begin(), vec.end(), [](StoreInfo *a, StoreInfo *b) {
                return a->offset < b->offset;
            });
            bool hasAll = true;
            for (int j = 0; j < vecWidth; j++) {
                bool foundJ = false;
                for (auto *si : vec) if (si->offset == j) { foundJ = true; break; }
                if (!foundJ) { hasAll = false; break; }
            }
            if (!hasAll) continue;

            // Step 2a: Check if stored values are all constants → direct ConstantVector store
            {
                bool allConst = true;
                for (int j = 0; j < vecWidth; j++) {
                    if (!dynamic_cast<ConstantInt*>(vec[j]->storedVal)) { allConst = false; break; }
                }
                if (allConst) {
                    Type *elemTy = vec[0]->storedVal->type_;
                    Type *vecTy  = getVecTy(elemTy);
                    Type *vecPtrTy = getVecPtrTy(elemTy);
                    std::vector<Constant*> constElems;
                    for (int j = 0; j < vecWidth; j++)
                        constElems.push_back(static_cast<ConstantInt*>(vec[j]->storedVal));
                    auto *constVec = new ConstantVector(static_cast<VectorType*>(vecTy), constElems);

                    // vec[0] may be a pointer-phi store (gep == nullptr);
                    // borrow a real GEP from any sibling in the group.
                    auto *firstGep = vec[0]->gep;
                    if (!firstGep) {
                        for (int jj = 1; jj < vecWidth; jj++)
                            if (vec[jj]->gep) { firstGep = vec[jj]->gep; break; }
                    }
                    if (!firstGep) continue; // should not happen
                    std::vector<Value*> idxs;
                    for (unsigned i = 1; i < firstGep->num_ops_ - 1; i++)
                        idxs.push_back(firstGep->get_operand(i));
                    idxs.push_back(vecPhi);
                    auto *newGep = new GetElementPtrInst(firstGep->get_operand(0), idxs, vecBody);
                    auto *bc = new Bitcast(Instruction::BitCast, newGep, vecPtrTy, vecBody);
                    new StoreInst(constVec, bc, vecBody);

                    for (int j = 0; j < vecWidth; j++) {
                        auto *si = vec[j];
                        si->store->parent_->remove_instr(si->store);
                        si->store->remove_use_of_ops();
                    }
                    continue;
                }
            }

            // Step 2b: Check if stored values come from a vectorizable binop
            auto *rootBinop = dynamic_cast<BinaryInst*>(vec[0]->storedVal);
            if (!rootBinop) continue;
            // Only integer NEON-supported opcodes; skip float
            if (rootBinop->type_->tid_ == Type::FloatTyID) continue;
            if (rootBinop->op_id_ != Instruction::Add &&
                rootBinop->op_id_ != Instruction::Sub &&
                rootBinop->op_id_ != Instruction::Mul) continue;
            // Verify all 4 stores share the same root binop
            bool sameBinop = true;
            for (int j = 1; j < vecWidth; j++) {
                auto *bi = dynamic_cast<BinaryInst*>(vec[j]->storedVal);
                if (!bi || bi->op_id_ != rootBinop->op_id_) { sameBinop = false; break; }
            }
            if (!sameBinop) continue;


            // Step 3: For each operand of the root binop, pack into vector
            Type *vecTy  = getVecTy(rootBinop->type_);
            Type *vecPtrTy = getVecPtrTy(rootBinop->type_);
            Value *vecOp[2] = {nullptr, nullptr};

            for (int opIdx = 0; opIdx < 2; opIdx++) {
                Value *opScalar[4];
                for (int j = 0; j < vecWidth; j++) {
                    auto *bi = static_cast<BinaryInst*>(vec[j]->storedVal);
                    opScalar[j] = bi->get_operand(opIdx);
                }
                // Check if loop-invariant (all 4 are the same Value*)
                bool invariant = true;
                for (int j = 1; j < vecWidth; j++)
                    if (opScalar[j] != opScalar[0]) { invariant = false; break; }

                if (invariant) {
                    // Splat in preheader (cache for reuse across groups)
                    auto &entry = splatCache[opScalar[0]];
                    if (!entry)
                        entry = emitSplat(opScalar[0], preheader);
                    vecOp[opIdx] = entry;
                } else if (isIVStep(opScalar)) {
                    // IV step pattern: {j, j+1, j+2, j+3}
                    // Use a vector phi to maintain this across iterations,
                    // eliminating 4× insertelement per iteration.
                    if (!vecIVPhi) {
                        auto *ivVecTy = static_cast<VectorType*>(getVecTy(vecPhi->type_));
                        // Constant step vector <0,1,2,3>
                        std::vector<Constant*> stepElems;
                        for (int j = 0; j < vecWidth; j++)
                            stepElems.push_back(new ConstantInt(module->int32_ty_, j));
                        auto *stepVec = new ConstantVector(ivVecTy, stepElems);
                        // Constant increment vector <4,4,4,4>
                        std::vector<Constant*> incElems;
                        for (int j = 0; j < vecWidth; j++)
                            incElems.push_back(new ConstantInt(module->int32_ty_, vecWidth));
                        vecIVInc = new ConstantVector(ivVecTy, incElems);
                        // Create vector phi in vecHeader
                        auto *phi = PhiInst::create_phi(ivVecTy, vecHeader);
                        vecHeader->add_instruction_front(phi);
                        phi->addIncoming(stepVec, preheader);
                        vecIVPhi = phi;
                    }
                    // In vecBody: advance the phi once (shared across all groups)
                    if (!vecIVPhiNeedsIncoming) {
                        auto *phiNext = new BinaryInst(getVecTy(vecPhi->type_),
                            Instruction::Add, vecIVPhi, vecIVInc, vecBody);
                        static_cast<PhiInst*>(vecIVPhi)->addIncoming(phiNext, vecBody);
                        vecIVPhiNeedsIncoming = true;
                    }
                    vecOp[opIdx] = vecIVPhi;
                } else {
                    // Pack the 4 offset versions in vecBody (generic fallback)
                    vecOp[opIdx] = emitPack4(opScalar, vecBody);
                }
            }

            // Step 4: Create vector binop
            auto *vecBinop = new BinaryInst(vecTy, rootBinop->op_id_,
                                             vecOp[0], vecOp[1], vecBody);

            // Step 5: GEP + bitcast + vector store
            auto *firstGep = vec[0]->gep;
            if (!firstGep) {
                for (int jj = 1; jj < vecWidth; jj++)
                    if (vec[jj]->gep) { firstGep = vec[jj]->gep; break; }
            }
            if (!firstGep) continue;
            std::vector<Value*> idxs;
            for (unsigned i = 1; i < firstGep->num_ops_ - 1; i++)
                idxs.push_back(firstGep->get_operand(i));
            idxs.push_back(vecPhi);
            auto *newGep = new GetElementPtrInst(firstGep->get_operand(0), idxs, vecBody);
            auto *bc = new Bitcast(Instruction::BitCast, newGep, vecPtrTy, vecBody);
            new StoreInst(vecBinop, bc, vecBody);

            // Step 6: Remove old scalar stores and binops.
            // Do NOT remove the GEPs — they may still be used by loads
            // (e.g. C[i][j] += A[i][k]*B[k][j] where C pointer is
            // shared between load and store). DCE will clean them up.
            for (int j = 0; j < vecWidth; j++) {
                auto *si = vec[j];
                // Remove the scalar store
                si->store->parent_->remove_instr(si->store);
                si->store->remove_use_of_ops();
                // Remove the scalar binop that fed this store
                auto *scalarBinop = static_cast<BinaryInst*>(si->storedVal);
                if (scalarBinop && scalarBinop->parent_) {
                    scalarBinop->parent_->remove_instr(scalarBinop);
                    scalarBinop->remove_use_of_ops();
                }
            }
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

    new BranchInst(origExit, afterLoop);

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

