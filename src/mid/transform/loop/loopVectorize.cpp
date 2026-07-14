#include "../../../include/mid/opt/loopVectorize.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include <stack>
#include <queue>
#include <algorithm>
#include <functional>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

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
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses LoopVectorize::execute(Module *module, AnalysisManager &AM) {
    BasicAliasAnalysis &BAA = AM.getBasicAA(module);
    for (auto func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func, BAA);
    }
    return PreservedAnalyses::none();
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

static bool getLoopPhiIncoming(const Loop &loop, PhiInst *phi,
                               Value *&initVal, Value *&latchVal,
                               BasicBlock *&latchBB) {
    initVal = nullptr;
    latchVal = nullptr;
    latchBB = nullptr;

    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        auto *pred = static_cast<BasicBlock*>(phi->get_operand(i + 1));
        if (loop.blocks.count(pred)) {
            if (latchVal) return false;
            latchVal = phi->get_operand(i);
            latchBB = pred;
        } else {
            if (initVal) return false;
            initVal = phi->get_operand(i);
        }
    }

    return initVal && latchVal && latchBB;
}

static bool decomposePointerOffset(Value *ptr, PhiInst *basePhi, int &offset) {
    offset = 0;
    Value *cur = ptr;
    while (cur != basePhi) {
        auto *gep = dynamic_cast<GetElementPtrInst*>(cur);
        if (!gep || gep->num_ops_ != 2) return false;
        auto *step = dynamic_cast<ConstantInt*>(gep->get_operand(1));
        if (!step) return false;
        offset += step->value_;
        cur = gep->get_operand(0);
    }
    return true;
}

static bool matchIVPlusConstant(Value *val, PhiInst *ivPhi, int &offset) {
    if (val == ivPhi) {
        offset = 0;
        return true;
    }
    auto *add = dynamic_cast<BinaryInst*>(val);
    if (!add || !add->is_add()) return false;
    Value *a = add->get_operand(0);
    Value *b = add->get_operand(1);
    if (a == ivPhi) {
        auto *ci = dynamic_cast<ConstantInt*>(b);
        if (!ci) return false;
        offset = ci->value_;
        return true;
    }
    if (b == ivPhi) {
        auto *ci = dynamic_cast<ConstantInt*>(a);
        if (!ci) return false;
        offset = ci->value_;
        return true;
    }
    return false;
}

static int scalarTypeSizeInBytes(Type *ty) {
    switch (ty->tid_) {
    case Type::IntegerTyID: return 4;
    case Type::FloatTyID: return 4;
    case Type::PointerTyID: return 8;
    case Type::ArrayTyID:
        return static_cast<ArrayType*>(ty)->num_elements_ *
               scalarTypeSizeInBytes(static_cast<ArrayType*>(ty)->contained_);
    case Type::VectorTyID:
        return static_cast<VectorType*>(ty)->num_elements_ *
               scalarTypeSizeInBytes(static_cast<VectorType*>(ty)->contained_);
    default:
        return 8;
    }
}

static bool computeGEPIndexStride(GetElementPtrInst *gep, unsigned varyPos,
                                  Type *scalarTy, int &laneStride) {
    Type *curTy = static_cast<PointerType*>(gep->get_operand(0)->type_)->contained_;
    int scalarSize = scalarTypeSizeInBytes(scalarTy);

    for (unsigned i = 1; i < gep->num_ops_; ++i) {
        int elemBytes = scalarTypeSizeInBytes(curTy);
        if (i == varyPos) {
            if (elemBytes % scalarSize != 0) return false;
            laneStride = elemBytes / scalarSize;
            return laneStride > 0;
        }

        if (curTy->tid_ == Type::ArrayTyID) {
            curTy = static_cast<ArrayType*>(curTy)->contained_;
        } else if (curTy->tid_ == Type::PointerTyID) {
            curTy = static_cast<PointerType*>(curTy)->contained_;
        }
    }

    return false;
}

static void rewritePhiIncoming(PhiInst *phi, BasicBlock *oldPred,
                               Value *newVal, BasicBlock *newPred) {
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        if (phi->get_operand(i + 1) != oldPred) continue;
        phi->get_operand(i)->remove_use(phi->use_pos_[i]);
        phi->get_operand(i + 1)->remove_use(phi->use_pos_[i + 1]);
        phi->operands_[i] = newVal;
        phi->use_pos_[i] = newVal->add_use(phi, i);
        phi->operands_[i + 1] = newPred;
        phi->use_pos_[i + 1] = newPred->add_use(phi, i + 1);
        return;
    }
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

        Value *initVal = nullptr;
        Value *latchVal = nullptr;
        BasicBlock *latchBB = nullptr;
        if (!getLoopPhiIncoming(loop, phi, initVal, latchVal, latchBB))
            continue;

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

        if (stride == 0) continue;

        iv.phi         = phi;
        iv.initVal     = initVal;
        iv.stride      = stride;
        iv.isAdd       = isAdd;
        iv.updateInst  = updateInst;
        return true;
    }

    return false;
}

bool LoopVectorize::analyzeReductionLoop(const Loop &loop, const InductionVar &iv,
                                         ReductionGroup &group) {
    const bool debugReduction =
        std::getenv("DEBUG_LOOP_VECTORIZE_REDUCTION") != nullptr;
    auto reject = [&](const char *reason) {
        if (debugReduction) {
            std::cerr << "[LoopVectorize:reduction] reject header="
                      << (loop.header ? loop.header->name_ : "<null>")
                      << " reason=" << reason << "\n";
        }
        return false;
    };

    if (iv.phi->type_->tid_ != Type::IntegerTyID || iv.stride <= 0)
        return reject("iv-type-or-step");
    if (VECTORIZE_FACTOR % iv.stride != 0)
        return reject("iv-step-not-divisible");
    if (loop.blocks.size() > 3)
        return reject("too-many-blocks");
    if (!loop.preheader)
        return reject("missing-preheader");
    for (auto *inst : loop.preheader->instr_list_) {
        if (inst->type_->tid_ == Type::VectorTyID)
            return reject("already-vectorized");
        if (inst->is_store() &&
            inst->get_operand(0)->type_->tid_ == Type::VectorTyID)
            return reject("already-vectorized");
    }

    auto *latch = loop.singleLatch();
    if (!latch || latch == loop.header)
        return reject("bad-latch");

    auto *headerBr = loop.header->get_terminator();
    if (!headerBr || !headerBr->is_br() || headerBr->num_ops_ != 3)
        return reject("bad-header-branch");
    auto *cmpInst = dynamic_cast<ICmpInst*>(headerBr->get_operand(0));
    if (!cmpInst)
        return reject("missing-header-cmp");
    if (cmpInst->get_operand(0) != iv.phi && cmpInst->get_operand(1) != iv.phi)
        return reject("iv-not-in-cmp");

    struct PointerPhiInfo {
        PhiInst *phi = nullptr;
        Value *initVal = nullptr;
        int totalStep = 0;
    };

    std::vector<PointerPhiInfo> pointerPhis;
    PhiInst *accPhi = nullptr;
    Value *accInit = nullptr;
    Value *accLatch = nullptr;

    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst*>(inst);
        if (phi == iv.phi) continue;

        Value *initVal = nullptr;
        Value *latchVal = nullptr;
        BasicBlock *latchBB = nullptr;
        if (!getLoopPhiIncoming(loop, phi, initVal, latchVal, latchBB))
            return reject("phi-incoming");

        if (phi->type_->tid_ == Type::PointerTyID) {
            int totalStep = 0;
            if (!decomposePointerOffset(latchVal, phi, totalStep))
                return reject("pointer-phi-step");
            if (totalStep == 0)
                return reject("pointer-phi-zero-step");
            pointerPhis.push_back({phi, initVal, totalStep});
            continue;
        }

        if (phi->type_->tid_ != Type::IntegerTyID)
            return reject("unsupported-phi-type");
        if (accPhi)
            return reject("multiple-int-phis");
        accPhi = phi;
        accInit = initVal;
        accLatch = latchVal;
    }

    if (!accPhi)
        return reject("missing-acc-phi");
    if (accPhi->type_ != iv.phi->type_)
        return false;
    if (accPhi->type_->tid_ != Type::IntegerTyID)
        return false;

    // 归约链识别。支持三类（整型，mod 2^32 下加/减结合且交换，
    // 4-lane 部分和 + 退出处水平相加与顺序累加结果完全一致——安全）：
    //   acc = acc - (a*b) - (a*b) ...   乘-减链（原有，stride 可 >1）
    //   acc = acc + (a*b)               点积（stride==1）
    //   acc = acc + load                求和（stride==1）
    auto *topBin = dynamic_cast<BinaryInst*>(accLatch);
    if (!topBin || !(topBin->is_add() || topBin->is_sub()))
        return reject("reduction-not-add-or-sub");
    bool isAdd = topBin->is_add();
    bool noMul = false;

    std::vector<std::pair<Value*, Value*>> mulInputsRev;
    Value *cursor = accLatch;
    while (cursor != accPhi) {
        auto *bin = dynamic_cast<BinaryInst*>(cursor);
        if (!bin || (isAdd ? !bin->is_add() : !bin->is_sub()))
            return reject("reduction-chain-op");

        Value *recur, *newVal;
        if (isAdd) {
            // 加法可交换：递归项是直接等于 accPhi 的一侧（仅支持单步 add ⇒ stride==1）
            if (bin->get_operand(0) == accPhi) { recur = bin->get_operand(0); newVal = bin->get_operand(1); }
            else if (bin->get_operand(1) == accPhi) { recur = bin->get_operand(1); newVal = bin->get_operand(0); }
            else return reject("reduction-add-recurrence");
        } else {
            recur = bin->get_operand(0);   // a - b：递归项是 a，被减项是 b
            newVal = bin->get_operand(1);
        }

        auto *mul = dynamic_cast<BinaryInst*>(newVal);
        if (mul && mul->is_mul() && mul->type_->tid_ == Type::IntegerTyID) {
            if (noMul) return reject("reduction-mixed-kind");
            mulInputsRev.push_back({mul->get_operand(0), mul->get_operand(1)});
        } else {
            if (!isAdd) return reject("reduction-not-mul");       // 减法链仍只接受乘积
            if (!mulInputsRev.empty()) return reject("reduction-mixed-kind");
            noMul = true;
            mulInputsRev.push_back({newVal, nullptr});            // 求和：单操作数
        }
        cursor = recur;
        if (mulInputsRev.size() > static_cast<size_t>(VECTORIZE_FACTOR))
            return reject("reduction-too-wide");
    }

    if (mulInputsRev.size() != static_cast<size_t>(iv.stride))
        return reject("reduction-step-mismatch");

    std::reverse(mulInputsRev.begin(), mulInputsRev.end());

    auto classifyOperand = [&](int opIdx, PackedOperand &packed) -> bool {
        Value *first = mulInputsRev[0].first;
        if (opIdx == 1) first = mulInputsRev[0].second;

        bool invariant = true;
        for (size_t i = 1; i < mulInputsRev.size(); ++i) {
            Value *cur = opIdx == 0 ? mulInputsRev[i].first : mulInputsRev[i].second;
            if (cur != first) {
                invariant = false;
                break;
            }
        }

        if (invariant && isLoopInvariant(first, loop.blocks)) {
            packed.kind = PackedOperand::INVARIANT;
            packed.scalarTy = first->type_;
            packed.source = first;
            packed.laneStride = 0;
            return first->type_->tid_ == Type::IntegerTyID;
        }

        std::vector<Value*> ptrs;
        ptrs.reserve(mulInputsRev.size());
        for (auto &pair : mulInputsRev) {
            Value *laneVal = opIdx == 0 ? pair.first : pair.second;
            auto *load = dynamic_cast<LoadInst*>(laneVal);
            if (!load || load->type_->tid_ != Type::IntegerTyID)
                return false;
            ptrs.push_back(load->get_operand(0));
        }

        if (auto *firstGep = dynamic_cast<GetElementPtrInst*>(ptrs[0])) {
            unsigned varyPos = 0;
            int firstOffset = 0;
            bool foundVary = false;

            for (unsigned idx = 1; idx < firstGep->num_ops_; ++idx) {
                int off = 0;
                if (!matchIVPlusConstant(firstGep->get_operand(idx), iv.phi, off))
                    continue;
                if (off != 0) return false;
                varyPos = idx;
                foundVary = true;
                break;
            }

            if (foundVary) {
                bool sameShape = true;
                for (size_t lane = 0; lane < ptrs.size(); ++lane) {
                    auto *gep = dynamic_cast<GetElementPtrInst*>(ptrs[lane]);
                    if (!gep || gep->num_ops_ != firstGep->num_ops_ ||
                        gep->get_operand(0) != firstGep->get_operand(0)) {
                        sameShape = false;
                        break;
                    }
                    for (unsigned idx = 1; idx < gep->num_ops_; ++idx) {
                        if (idx == varyPos) {
                            int off = 0;
                            if (!matchIVPlusConstant(gep->get_operand(idx), iv.phi, off) ||
                                off != static_cast<int>(lane)) {
                                sameShape = false;
                            }
                        } else if (gep->get_operand(idx) != firstGep->get_operand(idx)) {
                            sameShape = false;
                        } else if (!isLoopInvariant(gep->get_operand(idx), loop.blocks)) {
                            sameShape = false;
                        }
                        if (!sameShape) break;
                    }
                    if (!sameShape) break;
                }

                int laneStride = 0;
                if (sameShape &&
                    computeGEPIndexStride(firstGep, varyPos,
                                          static_cast<LoadInst*>(
                                              opIdx == 0 ? mulInputsRev[0].first
                                                         : mulInputsRev[0].second)
                                              ->type_,
                                          laneStride)) {
                    auto *firstLoad = dynamic_cast<LoadInst*>(
                        opIdx == 0 ? mulInputsRev[0].first : mulInputsRev[0].second);
                    packed.kind = laneStride == 1
                                      ? PackedOperand::CONTIGUOUS
                                      : PackedOperand::GATHER;
                    packed.scalarTy = firstLoad->type_;
                    packed.source = firstGep;
                    packed.laneStride = laneStride;
                    return true;
                }
            }
        }

        for (const auto &ptrInfo : pointerPhis) {
            std::vector<int> offsets;
            offsets.reserve(ptrs.size());
            for (auto *ptr : ptrs) {
                int offset = 0;
                if (!decomposePointerOffset(ptr, ptrInfo.phi, offset)) {
                    offsets.clear();
                    break;
                }
                offsets.push_back(offset);
            }
            if (offsets.size() != ptrs.size())
                continue;

            if (offsets.empty() || offsets[0] != 0)
                continue;
            int laneStride = offsets.size() > 1 ? offsets[1] : ptrInfo.totalStep;
            if (laneStride <= 0)
                continue;

            bool arithmetic = true;
            for (size_t lane = 0; lane < offsets.size(); ++lane) {
                if (offsets[lane] != static_cast<int>(lane) * laneStride) {
                    arithmetic = false;
                    break;
                }
            }
            if (!arithmetic)
                continue;
            if (ptrInfo.totalStep != laneStride * iv.stride)
                continue;

            packed.kind = (std::abs(laneStride) == 1)
                              ? PackedOperand::CONTIGUOUS
                              : PackedOperand::GATHER;
            auto *firstLoad = dynamic_cast<LoadInst*>(
                opIdx == 0 ? mulInputsRev[0].first : mulInputsRev[0].second);
            packed.scalarTy = firstLoad->type_;
            packed.source = ptrInfo.phi;
            packed.laneStride = laneStride;
            return true;
        }

        return false;
    };

    PackedOperand lhs, rhs;
    if (!classifyOperand(0, lhs))
        return reject("lhs-classify");
    if (noMul) {
        // 求和无第二操作数：给 rhs 一个惰性占位（INVARIANT/source=null），发射端不使用
        rhs.kind = PackedOperand::INVARIANT;
        rhs.scalarTy = lhs.scalarTy;
        rhs.source = nullptr;
        rhs.laneStride = 0;
    } else if (!classifyOperand(1, rhs)) {
        return reject("rhs-classify");
    }
    if (lhs.scalarTy->tid_ != Type::IntegerTyID || rhs.scalarTy->tid_ != Type::IntegerTyID)
        return reject("non-i32");
    if (lhs.kind == PackedOperand::GATHER && rhs.kind == PackedOperand::GATHER)
        return reject("two-gathers");
    // 两路非不变载入的点积（acc += a[i]*b[i]）暂不向量化：后端向量 mla 的
    // 取址有 loadAddr 缓存 bug（两路 ld1 撞同一暂存寄存器，算成 b*b）。
    // 待后端修好该缓存问题后再放开。求和与 a[i]*const 不受影响。
    if (isAdd && !noMul &&
        lhs.kind != PackedOperand::INVARIANT &&
        rhs.kind != PackedOperand::INVARIANT)
        return reject("add-dot-two-loads-deferred");
    size_t usedPointerPhis = 0;
    if (dynamic_cast<PhiInst*>(lhs.source)) usedPointerPhis++;
    if (dynamic_cast<PhiInst*>(rhs.source) && rhs.source != lhs.source) usedPointerPhis++;
    if (!pointerPhis.empty() && usedPointerPhis != pointerPhis.size())
        return reject("unused-pointer-phi");

    auto isReductionExemptPhi = [&](Instruction *inst) -> bool {
        if (!inst->is_phi() || inst->parent_ != loop.header) return false;
        if (inst == iv.phi || inst == accPhi) return true;
        for (const auto &p : pointerPhis)
            if (p.phi == inst) return true;
        return false;
    };
    for (auto *bb : loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            if (isReductionExemptPhi(inst)) continue;
            for (const auto &u : inst->use_list_) {
                auto *user = dynamic_cast<Instruction*>(u.val_);
                if (user && user->parent_ && !loop.blocks.count(user->parent_))
                    return reject("reduction-live-out");
            }
        }
    }

    for (auto *bb : loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator()) continue;
            if (inst->is_store() || inst->is_call() || inst->is_alloca())
                return reject("unsupported-side-effect");
            if (inst->is_load() || inst->is_gep() || inst->is_mul() ||
                inst->is_sub() || inst->is_add() || inst->is_cmp())
                continue;
            return reject("unsupported-inst");
        }
    }

    // ── 盈利性成本模型 ────────────────────────────────────────────────
    // 与 trip 无关的结构性判定：一次向量迭代的代价 vs 它替代的 VF 次标量迭代。
    //   连续载入  = 1   （单条向量 load）
    //   聚集 gather = 2*VF（A53 无原生 gather：退化成 VF 次标量 load + VF 次打包插入）
    //   不变量    = 0   （splat 提升到 preheader，循环内零成本）
    // 这样：纯 gather 求和（无计算可摊销）被拒；gather×连续 的点积（有乘法/累加
    // 摊销，如 h-5）保留；连续访问一律保留。只有结构上确实不划算的才退回标量。
    {
        auto loadCost = [&](PackedOperand::Kind k) -> int {
            if (k == PackedOperand::GATHER) return 2 * VECTORIZE_FACTOR;
            if (k == PackedOperand::INVARIANT) return 0;
            return 1;  // CONTIGUOUS
        };
        int vecIterCost = loadCost(lhs.kind) + (noMul ? 0 : loadCost(rhs.kind))
                        + (noMul ? 1 : 2);  // 累加(+ 乘法)
        int scalarLoads = (lhs.kind != PackedOperand::INVARIANT ? 1 : 0)
                        + ((!noMul && rhs.kind != PackedOperand::INVARIANT) ? 1 : 0);
        int scalarEquiv = VECTORIZE_FACTOR * (scalarLoads + (noMul ? 1 : 2));
        if (vecIterCost >= scalarEquiv)
            return reject("not-profitable");

        // 常量上界且 trip 太小：向量序言 + 水平归约 + 标量余数 的固定开销不值。
        Value *bnd = (cmpInst->get_operand(0) == iv.phi)
                         ? cmpInst->get_operand(1) : cmpInst->get_operand(0);
        if (auto *cb = dynamic_cast<ConstantInt*>(bnd)) {
            long long trip = (long long)cb->value_ / (iv.stride > 0 ? iv.stride : 1);
            if (trip < 2 * VECTORIZE_FACTOR)
                return reject("trip-too-small");
        }
    }

    if (debugReduction) {
        std::cerr << "[LoopVectorize:reduction] match header="
                  << loop.header->name_ << " gather="
                  << (lhs.kind == PackedOperand::GATHER ||
                      rhs.kind == PackedOperand::GATHER)
                  << "\n";
    }

    group.accPhi = accPhi;
    group.initVal = accInit;
    group.latchValue = accLatch;
    group.lhs = lhs;
    group.rhs = rhs;
    group.scalarStep = iv.stride;
    group.isAdd = isAdd;
    group.noMul = noMul;
    return true;
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
                if (!gep) continue; // skip non-GEP loads (e.g. pointer-phi loads from IVSR)

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
                if (!gep) continue; // skip non-GEP stores (e.g. pointer-phi stores from IVSR)

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

    // Binary/unary/cmp operations are vectorizable.
    // Note: is_binary() only covers Add/Sub/Mul/Div/Rem — we also need
    // Shl/LShr/AShr/And/Or/Xor, which are BinaryInst subclasses.
    if (inst->is_binary() || dynamic_cast<BinaryInst*>(inst)) return true;
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

static bool isLoopVectorizeAADebugEnabled() {
    static bool enabled = std::getenv("DEBUG_LOOP_VECTORIZE_AA") != nullptr;
    return enabled;
}

static std::string formatLoopVectorizeValue(Value *val) {
    if (!val) return "<null>";
    if (auto *inst = dynamic_cast<Instruction *>(val))
        return "%" + inst->name_;
    if (auto *global = dynamic_cast<GlobalVariable *>(val))
        return "@" + global->name_;
    if (!val->name_.empty())
        return val->name_;
    return "<anon>";
}

static const char *aliasResultNameForLV(AliasResult result) {
    switch (result) {
    case AliasResult::NoAlias: return "NoAlias";
    case AliasResult::MayAlias: return "MayAlias";
    case AliasResult::MustAlias: return "MustAlias";
    }
    return "MayAlias";
}

static Value *getMemoryPointer(Instruction *inst) {
    if (inst->is_load()) return inst->get_operand(0);
    if (inst->is_store()) return inst->get_operand(1);
    return nullptr;
}

bool LoopVectorize::hasSafeMemoryDependencies(const Loop &loop,
                                              const std::vector<MemAccess> &loads,
                                              const std::vector<MemAccess> &stores,
                                              const BasicAliasAnalysis &BAA) {
    struct MemInst {
        Instruction *inst;
        Value *ptr;
        bool isLoad;
        bool isStore;
        int order;
    };

    auto findMemAccess = [&](Instruction *inst) -> const MemAccess * {
        for (const auto &access : loads)
            if (access.inst == inst) return &access;
        for (const auto &access : stores)
            if (access.inst == inst) return &access;
        return nullptr;
    };

    auto isSameLaneAccess = [&](Instruction *a, Instruction *b) {
        const MemAccess *ma = findMemAccess(a);
        const MemAccess *mb = findMemAccess(b);
        return ma && mb &&
               ma->basePtr == mb->basePtr &&
               ma->elementOffset == mb->elementOffset;
    };

    std::vector<MemInst> mems;
    int order = 0;
    Function *func = loop.header ? loop.header->parent_ : nullptr;
    if (!func) return false;
    for (auto *bb : func->basic_blocks_) {
        if (!loop.blocks.count(bb)) continue;
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_load() && !inst->is_store()) {
                ++order;
                continue;
            }
            mems.push_back({inst, getMemoryPointer(inst), inst->is_load(),
                            inst->is_store(), order++});
        }
    }

    for (size_t i = 0; i < mems.size(); ++i) {
        for (size_t j = i + 1; j < mems.size(); ++j) {
            MemInst &a = mems[i];
            MemInst &b = mems[j];
            if (a.isLoad && b.isLoad) continue;

            AliasResult alias = BAA.alias(a.ptr, b.ptr);
            if (alias == AliasResult::NoAlias) {
                if (isLoopVectorizeAADebugEnabled()) {
                    std::cerr << "[LoopVectorize:AA] allow NoAlias ptr_a="
                              << formatLoopVectorizeValue(a.ptr)
                              << " ptr_b=" << formatLoopVectorizeValue(b.ptr)
                              << "\n";
                }
                continue;
            }

            bool sameLane = (a.ptr == b.ptr || isSameLaneAccess(a.inst, b.inst));
            if (sameLane) {
                bool aLoadBStore = (a.isLoad && b.isStore && a.order < b.order);
                bool bLoadAStore = (b.isLoad && a.isStore && b.order < a.order);
                if (aLoadBStore || bLoadAStore) {
                    if (isLoopVectorizeAADebugEnabled()) {
                        std::cerr << "[LoopVectorize:AA] allow forward-dep ptr_a="
                                  << formatLoopVectorizeValue(a.ptr)
                                  << " ptr_b=" << formatLoopVectorizeValue(b.ptr)
                                  << "\n";
                    }
                    continue;
                }
            }

            if (isLoopVectorizeAADebugEnabled()) {
                std::cerr << "[LoopVectorize:AA] reject alias="
                          << aliasResultNameForLV(alias)
                          << " ptr_a=" << formatLoopVectorizeValue(a.ptr)
                          << " ptr_b=" << formatLoopVectorizeValue(b.ptr)
                          << "\n";
            }
            return false;
        }
    }

    return true;
}

bool LoopVectorize::tryVectorize(Loop &loop, Function *func, Module *module,
                                 const BasicAliasAnalysis &BAA) {
    (void)module;
    (void)func;

    // Requirements:
    // 1. Loop must have a preheader
    if (!loop.preheader) { return false; }

    // 1b. 单 latch 且非自环（旧手写检测逐回边 + 跳自环，等价约束）
    if (!loop.singleLatch() || loop.singleLatch() == loop.header)
        return false;

    // 2. Loop must have a unique exit
    if (!loop.singleExit()) { return false; }

    // 3. Find induction variable
    InductionVar iv;
    if (!findInductionVar(loop, iv)) { return false; }

    ReductionGroup reduction;
    if (analyzeReductionLoop(loop, iv, reduction)) {
        if (std::getenv("DEBUG_LOOP_VECTORIZE"))
            std::cerr << "[LoopVectorize] reduction func=" << func->name_
                      << " header=" << loop.header->name_
                      << " iv_step=" << iv.stride << "\n";
        emitReductionVectorizedLoop(loop, iv, reduction, VECTORIZE_FACTOR,
                                    func, func->parent_);
        return true;
    }

    // 4. Store-group path remains restricted to small unit-stride loops.
    if (loop.blocks.size() > 2) { return false; }
    if (iv.stride != 1 && iv.stride != -1) { return false; }

    for (auto inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        if (inst == iv.phi) continue;
        auto *phi = static_cast<PhiInst*>(inst);
        Value *latchVal = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (loop.blocks.count(static_cast<BasicBlock*>(phi->get_operand(i + 1)))) {
                latchVal = phi->get_operand(i); break;
            }
        }
        if (!latchVal) { return false; }
        auto *update = dynamic_cast<Instruction*>(latchVal);
        if (phi->type_->tid_ == Type::PointerTyID) {
            auto *gep = dynamic_cast<GetElementPtrInst*>(update);
            if (!gep || gep->get_operand(0) != phi || gep->num_ops_ != 2)
                return false;
            auto *stride = dynamic_cast<ConstantInt*>(gep->get_operand(1));
            if (!stride || stride->value_ != 1) { return false; }
            continue;
        }
        return false; // 整型 aux phi：见上
    }

    // 5b. live-out 限制：循环内定义、循环外使用的值只允许是 header phi。
    //     header phi（IV/aux）经 remPhi/remAux 与 afterLoop 合流 phi 接力；
    //     其他值在余数循环零次执行时没有定义，无法保证支配性。
    for (auto bb : loop.blocks) {
        for (auto inst : bb->instr_list_) {
            if (inst->is_phi() && inst->parent_ == loop.header) continue;
            for (const auto &u : inst->use_list_) {
                auto *user = dynamic_cast<Instruction*>(u.val_);
                if (user && user->parent_ && !loop.blocks.count(user->parent_))
                    return false;
            }
        }
    }

    // 6. Find memory accesses with unit stride
    std::vector<MemAccess> loads, stores;
    if (!analyzeStrideAccesses(loop, iv, loads, stores)) {
        return false;
    }

    // 6. Check that all instructions in the loop are vectorizable
    for (auto bb : loop.blocks) {
        for (auto inst : bb->instr_list_) {
            if (!isVectorizableInstruction(inst, loop.blocks)) {
                return false;
            }
        }
    }

    // 7. Count total memory accesses - very small loops may not benefit
    //    from vectorization
    if (loads.empty() && stores.empty()) {
        return false;
    }

    // 7b. 最小成本模型（与 reduction 路径对齐）：
    //   - 常量上界且 trip < 2*VF：向量序言 + 余数循环固定开销不值
    //   - 体指令数/内存访问比过高：向量化收益被寄存器压力吃掉
    {
        auto *headerBr = loop.header->get_terminator();
        if (headerBr && headerBr->is_br() && headerBr->num_ops_ == 3) {
            if (auto *cmp = dynamic_cast<ICmpInst*>(headerBr->get_operand(0))) {
                Value *bnd = nullptr;
                if (cmp->get_operand(0) == iv.phi) bnd = cmp->get_operand(1);
                else if (cmp->get_operand(1) == iv.phi) bnd = cmp->get_operand(0);
                if (auto *cb = dynamic_cast<ConstantInt*>(bnd)) {
                    long long trip = (iv.stride > 0)
                                         ? (long long)cb->value_ / iv.stride
                                         : (long long)cb->value_;
                    if (trip < 2 * VECTORIZE_FACTOR)
                        return false;
                }
            }
        }
        size_t memCount = loads.size() + stores.size();
        size_t bodyCount = 0;
        for (auto bb : loop.blocks)
            for (auto inst : bb->instr_list_)
                if (!inst->isTerminator() && !inst->is_phi())
                    ++bodyCount;
        if (memCount > 0 && bodyCount / memCount > 8)
            return false;
    }

    if (!hasSafeMemoryDependencies(loop, loads, stores, BAA)) {
        return false;
    }

    {
        std::vector<Instruction*> bodyInsts;
        for (auto bb : loop.blocks)
            for (auto inst : bb->instr_list_)
                if (!inst->isTerminator() && !inst->is_phi())
                    bodyInsts.push_back(inst);
        for (auto *inst : bodyInsts) {
            if (!inst->is_store()) continue;
            Value *val = inst->get_operand(0);
            // Walk back one level to find the root binop
            if (auto *bi = dynamic_cast<BinaryInst*>(val)) {
                if (bi->op_id_ == Instruction::SDiv ||
                    bi->op_id_ == Instruction::SRem ||
                    bi->op_id_ == Instruction::FDiv) {
                    return false;
                }
            }
        }
    }

    // 临时二分开关：限制成功向量化的次数,用于定位错译点
    if (const char *lim = std::getenv("DEBUG_LOOP_VECTORIZE_LIMIT")) {
        static int vecCount = 0;
        if (vecCount >= atoi(lim))
            return false;
        vecCount++;
    }

    if (std::getenv("DEBUG_LOOP_VECTORIZE"))
        std::cerr << "[LoopVectorize] func=" << func->name_
                  << " header=" << loop.header->name_
                  << " loads=" << loads.size()
                  << " stores=" << stores.size() << "\n";

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

void LoopVectorize::emitReductionVectorizedLoop(
    const Loop &loop, const InductionVar &iv, const ReductionGroup &group,
    int vecWidth, Function *func, Module *module)
{
    const bool debugReduction =
        std::getenv("DEBUG_LOOP_VECTORIZE_REDUCTION") != nullptr;
    BasicBlock *preheader = loop.preheader;
    BasicBlock *origHeader = loop.header;
    BasicBlock *origLatch = loop.singleLatch();
    if (debugReduction) {
        std::cerr << "[LoopVectorize:reduction] emit-start header="
                  << origHeader->name_ << "\n";
    }
    auto *preheaderBr = preheader ? preheader->get_terminator() : nullptr;
    if (!preheaderBr || !preheaderBr->is_br() || preheaderBr->num_ops_ != 1 ||
        preheaderBr->get_operand(0) != origHeader) {
        if (debugReduction) {
            std::cerr << "[LoopVectorize:reduction] emit-bail-preheader header="
                      << origHeader->name_ << " ops="
                      << (preheaderBr ? std::to_string(preheaderBr->num_ops_) : "null")
                      << "\n";
        }
        return;
    }

    auto *headerBr = origHeader->get_terminator();
    if (!headerBr || !headerBr->is_br() || headerBr->num_ops_ != 3) return;
    auto *cmpInst = dynamic_cast<ICmpInst*>(headerBr->get_operand(0));
    if (!cmpInst) return;

    Value *bound = nullptr;
    bool ivIsLeft = true;
    if (cmpInst->get_operand(0) == iv.phi) {
        bound = cmpInst->get_operand(1);
    } else if (cmpInst->get_operand(1) == iv.phi) {
        bound = cmpInst->get_operand(0);
        ivIsLeft = false;
    } else {
        return;
    }
    if (!isLoopInvariant(bound, loop.blocks)) return;

    Type *vecTy = module->get_vector_type(group.accPhi->type_, vecWidth);
    auto *vecTyCast = static_cast<VectorType*>(vecTy);
    auto getVecPtrTy = [&](Type *scalarTy) -> Type * {
        return module->get_pointer_type(module->get_vector_type(scalarTy, vecWidth));
    };
    auto insertBeforeTerm = [&](Instruction *inst, BasicBlock *bb) {
        if (!bb->get_terminator()) return;
        bb->remove_instr(inst);
        bb->add_instruction_before_terminator(inst);
    };
    auto emitSplat = [&](Value *scalar, BasicBlock *bb) -> Value * {
        Value *result = nullptr;
        for (int j = 0; j < vecWidth; ++j) {
            auto *idxConst = new ConstantInt(module->int32_ty_, j);
            Value *base = result ? result : scalar;
            auto *ins = new InsertElementInst(base, scalar, idxConst, bb);
            if (j == 0) ins->type_ = module->get_vector_type(scalar->type_, vecWidth);
            if (bb == preheader) insertBeforeTerm(ins, bb);
            result = ins;
        }
        return result;
    };
    auto emitPack4 = [&](Value *vals[4], BasicBlock *bb) -> Value * {
        Value *result = nullptr;
        for (int j = 0; j < vecWidth; ++j) {
            auto *idxConst = new ConstantInt(module->int32_ty_, j);
            Value *base = result ? result : vals[j];
            auto *ins = new InsertElementInst(base, vals[j], idxConst, bb);
            if (j == 0) ins->type_ = module->get_vector_type(vals[j]->type_, vecWidth);
            result = ins;
        }
        return result;
    };

    BasicBlock *vecHeader = new BasicBlock(module, "vec_red_hdr", func);
    BasicBlock *vecBody = new BasicBlock(module, "vec_red_body", func);
    BasicBlock *vecExit = new BasicBlock(module, "vec_red_exit", func);

    auto *vecIVPhi = PhiInst::create_phi(module->int32_ty_, vecHeader);
    vecHeader->add_instruction_front(vecIVPhi);
    vecIVPhi->addIncoming(iv.initVal, preheader);

    std::vector<Constant*> zeroElems;
    for (int lane = 0; lane < vecWidth; ++lane)
        zeroElems.push_back(new ConstantInt(module->int32_ty_, 0));
    auto *zeroVec = new ConstantVector(vecTyCast, zeroElems);
    auto *zeroLane = new ConstantInt(module->int32_ty_, 0);
    auto *initAccVec = new InsertElementInst(zeroVec, group.initVal, zeroLane, preheader);
    insertBeforeTerm(initAccVec, preheader);
    if (debugReduction)
        std::cerr << "[LoopVectorize:reduction] emit-init header="
                  << origHeader->name_ << "\n";

    auto *vecAccPhi = PhiInst::create_phi(vecTy, vecHeader);
    vecHeader->add_instruction_front(vecAccPhi);
    vecAccPhi->addIncoming(initAccVec, preheader);

    std::unordered_map<PhiInst*, Value*> vecPtrPhis;
    std::unordered_map<PhiInst*, int> laneStrides;
    auto registerPtrOperand = [&](const PackedOperand &packed) {
        auto *phi = dynamic_cast<PhiInst*>(packed.source);
        if (!phi || vecPtrPhis.count(phi)) return;
        Value *initVal = nullptr;
        Value *latchVal = nullptr;
        BasicBlock *latchBB = nullptr;
        if (!getLoopPhiIncoming(loop, phi, initVal, latchVal, latchBB)) return;
        auto *vecPtrPhi = PhiInst::create_phi(phi->type_, vecHeader);
        vecHeader->add_instruction_front(vecPtrPhi);
        vecPtrPhi->addIncoming(initVal, preheader);
        vecPtrPhis[phi] = vecPtrPhi;
        laneStrides[phi] = packed.laneStride;
    };
    if (group.lhs.kind != PackedOperand::INVARIANT) registerPtrOperand(group.lhs);
    if (group.rhs.kind != PackedOperand::INVARIANT) registerPtrOperand(group.rhs);

    int adj = vecWidth - 1;
    Value *boundMain = nullptr;
    if (auto *cb = dynamic_cast<ConstantInt*>(bound)) {
        boundMain = new ConstantInt(module->int32_ty_, cb->value_ - adj);
    } else {
        auto *adjConst = new ConstantInt(module->int32_ty_, adj);
        auto *adjInst = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                       bound, adjConst, preheader, false);
        preheader->add_instruction_before_terminator(adjInst);
        boundMain = adjInst;
    }

    ICmpInst::ICmpOp vecCmpOp;
    if (ivIsLeft) {
        vecCmpOp = cmpInst->icmp_op_;
    } else {
        switch (cmpInst->icmp_op_) {
        case ICmpInst::ICMP_SLT: vecCmpOp = ICmpInst::ICMP_SGT; break;
        case ICmpInst::ICMP_SLE: vecCmpOp = ICmpInst::ICMP_SGE; break;
        case ICmpInst::ICMP_SGT: vecCmpOp = ICmpInst::ICMP_SLT; break;
        case ICmpInst::ICMP_SGE: vecCmpOp = ICmpInst::ICMP_SLE; break;
        default: vecCmpOp = cmpInst->icmp_op_; break;
        }
    }

    ICmpInst *vecCmp = nullptr;
    if (ivIsLeft)
        vecCmp = new ICmpInst(vecCmpOp, vecIVPhi, boundMain, vecHeader);
    else
        vecCmp = new ICmpInst(vecCmpOp, boundMain, vecIVPhi, vecHeader);
    new BranchInst(vecCmp, vecBody, vecExit, vecHeader);
    if (debugReduction)
        std::cerr << "[LoopVectorize:reduction] emit-header header="
                  << origHeader->name_ << "\n";

    std::unordered_map<Value*, Value*> splatCache;
    auto emitPackedOperand = [&](const PackedOperand &packed) -> Value * {
        if (packed.kind == PackedOperand::INVARIANT) {
            auto &entry = splatCache[packed.source];
            if (!entry) entry = emitSplat(packed.source, preheader);
            return entry;
        }

        if (auto *gepTemplate = dynamic_cast<GetElementPtrInst*>(packed.source)) {
            unsigned varyPos = 0;
            bool foundVary = false;
            for (unsigned idx = 1; idx < gepTemplate->num_ops_; ++idx) {
                int off = 0;
                if (!matchIVPlusConstant(gepTemplate->get_operand(idx), iv.phi, off))
                    continue;
                if (off != 0) return nullptr;
                varyPos = idx;
                foundVary = true;
                break;
            }
            if (!foundVary) return nullptr;

            auto buildLanePtr = [&](int lane) -> Value * {
                std::vector<Value*> idxs;
                for (unsigned idx = 1; idx < gepTemplate->num_ops_; ++idx) {
                    if (idx != varyPos) {
                        idxs.push_back(gepTemplate->get_operand(idx));
                        continue;
                    }
                    if (lane == 0) {
                        idxs.push_back(vecIVPhi);
                    } else {
                        auto *off = new ConstantInt(module->int32_ty_, lane);
                        idxs.push_back(new BinaryInst(module->int32_ty_, Instruction::Add,
                                                      vecIVPhi, off, vecBody));
                    }
                }
                return new GetElementPtrInst(gepTemplate->get_operand(0), idxs, vecBody);
            };

            if (packed.kind == PackedOperand::CONTIGUOUS) {
                auto *basePtr = buildLanePtr(0);
                auto *bc = new Bitcast(Instruction::BitCast, basePtr,
                                       getVecPtrTy(packed.scalarTy), vecBody);
                return new LoadInst(bc, vecBody);
            }

            if (packed.kind == PackedOperand::GATHER) {
                Value *lanes[4] = {nullptr, nullptr, nullptr, nullptr};
                for (int lane = 0; lane < vecWidth; ++lane)
                    lanes[lane] = new LoadInst(buildLanePtr(lane), vecBody);
                return emitPack4(lanes, vecBody);
            }
        }

        auto *ptrPhi = dynamic_cast<PhiInst*>(packed.source);
        auto it = vecPtrPhis.find(ptrPhi);
        if (it == vecPtrPhis.end()) return nullptr;
        Value *basePtr = it->second;

        if (packed.kind == PackedOperand::CONTIGUOUS) {
            auto *bc = new Bitcast(Instruction::BitCast, basePtr,
                                   getVecPtrTy(packed.scalarTy), vecBody);
            return new LoadInst(bc, vecBody);
        }

        if (packed.kind == PackedOperand::GATHER) {
            Value *lanes[4] = {nullptr, nullptr, nullptr, nullptr};
            for (int lane = 0; lane < vecWidth; ++lane) {
                Value *ptr = basePtr;
                if (lane != 0) {
                    auto *off = new ConstantInt(module->int32_ty_,
                                                lane * packed.laneStride);
                    ptr = new GetElementPtrInst(basePtr, {off}, vecBody);
                }
                lanes[lane] = new LoadInst(ptr, vecBody);
            }
            return emitPack4(lanes, vecBody);
        }

        return nullptr;
    };

    Value *lhsVec = emitPackedOperand(group.lhs);
    if (!lhsVec) return;
    Value *perLaneVec;
    if (group.noMul) {
        perLaneVec = lhsVec;                       // 求和：每 lane 即载入值，无乘法
    } else {
        Value *rhsVec = emitPackedOperand(group.rhs);
        if (!rhsVec) return;
        perLaneVec = new BinaryInst(vecTy, Instruction::Mul, lhsVec, rhsVec, vecBody);
    }
    if (debugReduction)
        std::cerr << "[LoopVectorize:reduction] emit-operands header="
                  << origHeader->name_ << "\n";

    auto *vecAccNext = new BinaryInst(
        vecTy, group.isAdd ? Instruction::Add : Instruction::Sub,
        vecAccPhi, perLaneVec, vecBody);
    vecAccPhi->addIncoming(vecAccNext, vecBody);

    auto *vecStep = new ConstantInt(module->int32_ty_, vecWidth);
    auto *vecIVNext = new BinaryInst(module->int32_ty_, Instruction::Add,
                                     vecIVPhi, vecStep, vecBody);
    vecIVPhi->addIncoming(vecIVNext, vecBody);

    for (auto &kv : vecPtrPhis) {
        auto *phi = kv.first;
        Value *cur = kv.second;
        int laneStride = laneStrides[phi];
        auto *step = new ConstantInt(module->int32_ty_, vecWidth * laneStride);
        auto *next = new GetElementPtrInst(cur, {step}, vecBody);
        static_cast<PhiInst*>(cur)->addIncoming(next, vecBody);
    }

    new BranchInst(vecHeader, vecBody);
    if (debugReduction)
        std::cerr << "[LoopVectorize:reduction] emit-body header="
                  << origHeader->name_ << "\n";

    BasicBlock *entry = func->basic_blocks_.front();
    auto *accSpill = new AllocaInst(vecTy, entry, true);
    entry->add_instruction_front(accSpill);
    new StoreInst(vecAccPhi, accSpill, vecExit);
    auto *spillI32Ptr = new Bitcast(Instruction::BitCast, accSpill,
                                    module->get_pointer_type(module->int32_ty_), vecExit);
    std::vector<Value*> laneVals;
    for (int lane = 0; lane < vecWidth; ++lane) {
        Value *ptr = spillI32Ptr;
        if (lane != 0) {
            auto *off = new ConstantInt(module->int32_ty_, lane);
            ptr = new GetElementPtrInst(spillI32Ptr, {off}, vecExit);
        }
        laneVals.push_back(new LoadInst(ptr, vecExit));
    }

    Value *foldedAcc = laneVals[0];
    for (int lane = 1; lane < vecWidth; ++lane)
        foldedAcc = new BinaryInst(module->int32_ty_, Instruction::Add,
                                   foldedAcc, laneVals[lane], vecExit);
    new BranchInst(origHeader, vecExit);
    if (debugReduction)
        std::cerr << "[LoopVectorize:reduction] emit-exit header="
                  << origHeader->name_ << "\n";

    for (auto *inst : origHeader->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst*>(inst);
        if (phi == iv.phi) {
            rewritePhiIncoming(phi, preheader, vecIVPhi, vecExit);
            continue;
        }
        if (phi == group.accPhi) {
            rewritePhiIncoming(phi, preheader, foldedAcc, vecExit);
            continue;
        }
        if (phi->type_->tid_ == Type::PointerTyID) {
            auto it = vecPtrPhis.find(phi);
            if (it != vecPtrPhis.end()) {
                rewritePhiIncoming(phi, preheader, it->second, vecExit);
                continue;
            }
        }
    }
    if (debugReduction)
        std::cerr << "[LoopVectorize:reduction] emit-phis header="
                  << origHeader->name_ << "\n";

    preheader->delete_instr(preheaderBr);
    preheader->remove_succ_basic_block(origHeader);
    origHeader->remove_pre_basic_block(preheader);
    new BranchInst(vecHeader, preheader);
    if (debugReduction)
        std::cerr << "[LoopVectorize:reduction] emit-cfg header="
                  << origHeader->name_ << "\n";

    if (debugReduction)
        std::cerr << "[LoopVectorize:reduction] emit-before-rename header="
                  << origHeader->name_ << "\n";
    func->set_instr_name();
    if (debugReduction)
        std::cerr << "[LoopVectorize:reduction] emit-done header="
                  << origHeader->name_ << "\n";
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
    BasicBlock *origLatch  = loop.singleLatch();
    BasicBlock *origExit   = loop.singleExit();

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

    // Preheader must be a dedicated single-successor block branching
    // unconditionally to origHeader.  The CFG rewrite below assumes this
    // (it patches the preheader terminator's only operand); a non-dedicated
    // preheader (conditional branch / multiple successors) would leave the
    // CFG broken.  Mirrors the guard in emitReductionVectorizedLoop.
    auto *preheaderBr = preheader ? preheader->get_terminator() : nullptr;
    if (!preheaderBr || !preheaderBr->is_br() || preheaderBr->num_ops_ != 1 ||
        preheaderBr->get_operand(0) != origHeader)
        return;

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

    int adj = vecWidth - 1;  // VF - 1
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

    bool patternA = true;
    for (auto inst : origHeader->instr_list_) {
        if (!inst->is_phi()) break;
        if (inst != iv.phi) { patternA = false; break; }
    }
    // Non-GEP loads/stores (pointer-phi based) cannot be handled by
    // pattern A — force pattern B so headerNonIVPhis mapping is used.
    if (patternA) {
        for (auto *origInst : bodyInsts) {
            if (origInst->is_load()) {
                if (!dynamic_cast<GetElementPtrInst*>(origInst->get_operand(0)))
                    { patternA = false; break; }
            } else if (origInst->is_store()) {
                if (!dynamic_cast<GetElementPtrInst*>(origInst->get_operand(1)))
                    { patternA = false; break; }
            }
        }
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
        // Reject division: integer SDiv/SRem and float FDiv have no NEON
        // vector form on the target, so they prevent VECTOR_IR packing and
        // scalar unrolling would only increase register pressure.
        // Float add/sub/mul lower to vector fadd/fsub/fmul (.4s) and are kept.
        if (auto *bi = dynamic_cast<BinaryInst*>(origInst)) {
            if (bi->op_id_ == Instruction::SDiv ||
                bi->op_id_ == Instruction::SRem ||
                bi->op_id_ == Instruction::FDiv) {
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

    // Helpers shared by both pattern A and pattern B.
    auto getVecTy = [&](Type *scalarTy) -> Type* {
        return module->get_vector_type(scalarTy, vecWidth);
    };
    auto getVecPtrTy = [&](Type *scalarTy) -> Type* {
        return module->get_pointer_type(getVecTy(scalarTy));
    };
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

    if (patternA) {
        // ── 模式 A: 全向量化 IR ──

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
                        // Scalar value on a vector-gep: splat once in
                        // preheader, reuse every iteration.
                        newVal = emitSplat(newVal, preheader);
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
                // Splat any scalar operand that is paired with a vector operand.
                // Emit in preheader so the splat runs once, not every iteration.
                if (resTy->tid_ == Type::VectorTyID) {
                    auto splat = [&](Value *&op) {
                        if (op->type_->tid_ != Type::VectorTyID)
                            op = emitSplat(op, preheader);
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

            {
                bool allConst = true;
                for (int j = 0; j < vecWidth; j++) {
                    if (!dynamic_cast<ConstantInt*>(vec[j]->storedVal)) { allConst = false; break; }
                }
                if (allConst) {
                    Type *elemTy = vec[0]->storedVal->type_;
                    Type *vecTy  = getVecTy(elemTy);
                    Type *vecPtrTy = getVecPtrTy(elemTy);

                    // Splat the constant ONCE in the preheader instead of
                    // materializing a ConstantVector in the vecBody every
                    // iteration (which emits N×VF "mov v.s[lane]" per iter).
                    Value *splatVal = emitSplat(vec[0]->storedVal, preheader);

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
                    new StoreInst(splatVal, bc, vecBody);

                    for (int j = 0; j < vecWidth; j++) {
                        auto *si = vec[j];
                        si->store->parent_->remove_instr(si->store);
                        si->store->remove_use_of_ops();
                    }
                    continue;
                }
            }

            auto *rootBinop = dynamic_cast<BinaryInst*>(vec[0]->storedVal);
            if (!rootBinop) continue;
            // Integer-only here. Float element-wise loops are vectorized via
            // pattern A (vector load → vector binop → vector store). This
            // scalar-unroll + emitPack4 path packs arbitrary scalar lanes via
            // float InsertElement, which the backend lowers incorrectly under
            // non-deterministic register allocation (lanes land in the wrong
            // NEON register / a stale W reg is reused), so float stays scalar.
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
                        // 初始 lane 向量必须叠加标量 IV 初值：init 非 0
                        // （如并行外提体的 lo 形参）时 <0,1,2,3> 是错的
                        Value *scalarInit = nullptr;
                        for (unsigned pi = 0; pi < vecPhi->num_ops_; pi += 2) {
                            if (vecPhi->get_operand(pi + 1) ==
                                static_cast<Value *>(preheader)) {
                                scalarInit = vecPhi->get_operand(pi);
                                break;
                            }
                        }
                        Value *initLanes = stepVec;
                        if (auto *initCI =
                                dynamic_cast<ConstantInt *>(scalarInit)) {
                            if (initCI->value_ != 0) {
                                std::vector<Constant*> laneElems;
                                for (int j = 0; j < vecWidth; j++)
                                    laneElems.push_back(new ConstantInt(
                                        module->int32_ty_,
                                        initCI->value_ + j));
                                initLanes = new ConstantVector(ivVecTy, laneElems);
                            }
                        } else if (scalarInit) {
                            Value *splat = emitSplat(scalarInit, preheader);
                            auto *add = new BinaryInst(ivVecTy, Instruction::Add,
                                                       splat, stepVec, preheader);
                            preheader->remove_instr(add);
                            preheader->add_instruction_before_terminator(add);
                            initLanes = add;
                        }
                        phi->addIncoming(initLanes, preheader);
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

            auto *vecBinop = new BinaryInst(vecTy, rootBinop->op_id_,
                                             vecOp[0], vecOp[1], vecBody);

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

            // Do NOT remove the GEPs — they may still be used by loads
            // (e.g. C[i][j] += A[i][k]*B[k][j] where C pointer is
            // shared between load and store). DCE will clean them up.
            for (int j = 0; j < vecWidth; j++) {
                auto *si = vec[j];
                // Remove the scalar store
                si->store->parent_->remove_instr(si->store);
                si->store->remove_use_of_ops();
                // Remove the scalar binop that fed this store, but only if
                // no other instruction still uses it (e.g. another store
                // or a condition branch). Otherwise leave it for DCE.
                auto *scalarBinop = static_cast<BinaryInst*>(si->storedVal);
                if (scalarBinop && scalarBinop->parent_ &&
                    scalarBinop->use_list_.empty()) {
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
    //
    // For non-IV integer phis with constant-offset updates, the incoming
    // value must also account for the VF iterations already processed by
    // the vectorized loop body (otherwise they'd restart from their
    // original initVal in the remainder loop).
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
        } else if (phi->type_->tid_ == Type::IntegerTyID) {
            // Non-IV integer phi: adjust initVal by VF × per-iteration stride
            Value *latchVal = nullptr;
            for (unsigned j = 0; j < phi->num_ops_; j += 2) {
                if (loop.blocks.count(
                        static_cast<BasicBlock*>(phi->get_operand(j + 1)))) {
                    latchVal = phi->get_operand(j); break;
                }
            }
            if (auto *update = dynamic_cast<Instruction*>(latchVal)) {
                if (update->is_add() || update->is_sub()) {
                    Value *op0 = update->get_operand(0);
                    Value *op1 = update->get_operand(1);
                    int offset = 0;
                    if (op0 == phi && dynamic_cast<ConstantInt*>(op1))
                        offset = static_cast<ConstantInt*>(op1)->value_;
                    else if (update->is_add() && op1 == phi &&
                             dynamic_cast<ConstantInt*>(op0))
                        offset = static_cast<ConstantInt*>(op0)->value_;
                    if (!update->is_add()) offset = -offset;

                    if (offset != 0) {
                        for (unsigned j = 0; j < phi->num_ops_; j += 2) {
                            if (phi->get_operand(j + 1) == remHeader) {
                                Value *oldVal = phi->get_operand(j);
                                auto *initCI =
                                    dynamic_cast<ConstantInt*>(oldVal);
                                if (initCI) {
                                    int newVal =
                                        initCI->value_ + vecWidth * offset;
                                    oldVal->remove_use(phi->use_pos_[j]);
                                    auto *newCI = new ConstantInt(
                                        module->int32_ty_, newVal);
                                    phi->operands_[j] = newCI;
                                    phi->use_pos_[j] =
                                        newCI->add_use(phi, j);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    // Redirect the preheader branch: preheader -> vecHeader (instead of origHeader)
    // preheaderBr 已在函数开头校验为 dedicated 单后继无条件跳转至 origHeader
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

    // origLatch 不再是 origHeader 的前驱，删除 header phi 中来自它的残留
    // incoming（该值现在经由 remPhi 从 remHeader 流入）
    for (auto inst : origHeader->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst*>(inst);
        for (int i = (int)phi->num_ops_ - 2; i >= 0; i -= 2) {
            if (phi->get_operand(i + 1) == origLatch)
                phi->remove_operands(i, i + 1);
        }
    }

    // Collect loop blocks that actually branch to origExit (their successor edges
    // are rewired to afterLoop below; phis in origExit need their incoming BBs
    // updated from those blocks → afterLoop, otherwise SSA breaks).
    std::set<BasicBlock*> rewiredFromLoop;
    for (auto bb : loop.blocks) {
        auto *term = bb->get_terminator();
        for (unsigned i = 0; i < term->num_ops_; i++) {
            auto *succ = dynamic_cast<BasicBlock*>(term->get_operand(i));
            if (succ && succ == origExit) {
                rewiredFromLoop.insert(bb);
                term->get_operand(i)->remove_use(term->use_pos_[i]);
                term->operands_[i] = afterLoop;
                term->use_pos_[i]  = afterLoop->add_use(term, i);
                succ->remove_pre_basic_block(bb);
                afterLoop->add_pre_basic_block(bb);
                bb->remove_succ_basic_block(origExit);
                bb->add_succ_basic_block(afterLoop);
            }
        }
    }

    new BranchInst(origExit, afterLoop);

    // Patch phis in origExit: any incoming BB that was a rewired loop block
    // is now reached via afterLoop, so the phi must reference afterLoop.
    for (auto inst : origExit->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst*>(inst);
        for (unsigned i = 1; i < phi->num_ops_; i += 2) {
            auto *src = dynamic_cast<BasicBlock*>(phi->get_operand(i));
            if (src && rewiredFromLoop.count(src)) {
                phi->get_operand(i)->remove_use(phi->use_pos_[i]);
                phi->operands_[i] = afterLoop;
                phi->use_pos_[i]  = afterLoop->add_use(phi, i);
            }
        }
    }

    // ── Clean up: set function to renumber instructions ──────────
    func->set_instr_name();
}

// =====================================================================
// 对函数运行向量化
// =====================================================================

void LoopVectorize::runOnFunction(Function *func, const BasicAliasAnalysis &BAA) {
    if (func->basic_blocks_.empty()) return;

    // 变换改 CFG，成功一次就重新分析 LoopInfo 再扫（与旧重启逻辑一致）
    bool changed = true;
    for (int iter = 0; iter < 5 && changed; iter++) {
        changed = false;

        LoopInfo LI;
        LI.analyze(func);
        std::vector<Loop *> loops;
        for (auto &l : LI.allLoops())
            loops.push_back(l.get());
        std::sort(loops.begin(), loops.end(), [](Loop *a, Loop *b) {
            return a->blocks.size() < b->blocks.size();
        });

        for (auto *loop : loops) {
            if (tryVectorize(*loop, func, func->parent_, BAA)) {
                changed = true;
                func->invalidateDominatorInfo();
                break; // restart with fresh LoopInfo
            }
        }
    }

    func->set_instr_name();
}
