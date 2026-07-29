#include "../../../include/mid/opt/loopVectorize.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <functional>

// Cortex-A53 has 128-bit NEON registers, hence four i32/f32 lanes.
static const int VECTORIZE_FACTOR = 4;

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

    if (loop.header->hasSemFlag(SemFlag::VectorizedEpilogue))
        return reject("already-vector-epilogue");

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
    if (cmpInst->icmp_op_ != ICmpInst::ICMP_SLT ||
        cmpInst->get_operand(0) != iv.phi)
        return reject("non-canonical-loop-condition");

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
    if (cmpInst->get_operand(0) == iv.phi) {
        bound = cmpInst->get_operand(1);
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
    vecHeader->setSemFlag(SemFlag::TargetPointerRecurrenceLoop);

    // Two contiguous vector parts halve loop-control overhead on the A53.
    // Keep a single accumulator chain: it also keeps one logical address
    // stream and has materially lower register pressure in the ARM64 backend.
    // Scalarized gathers already have a large body and stay at UF=1.
    const bool hasGather = group.lhs.kind == PackedOperand::GATHER ||
                           group.rhs.kind == PackedOperand::GATHER;
    const int reductionUF = hasGather ? 1 : 2;
    const int vectorTrip = vecWidth * reductionUF;

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
    std::unordered_map<GetElementPtrInst*, PhiInst*> gepPtrPhis;
    std::unordered_map<GetElementPtrInst*, int> gepLaneStrides;
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
    auto registerGEPOperand = [&](const PackedOperand &packed) {
        auto *gep = dynamic_cast<GetElementPtrInst*>(packed.source);
        if (!gep || gepPtrPhis.count(gep)) return;
        std::vector<Value*> initIndices;
        bool foundVary = false;
        for (unsigned idx = 1; idx < gep->num_ops_; ++idx) {
            int offset = 0;
            if (matchIVPlusConstant(gep->get_operand(idx), iv.phi, offset) &&
                offset == 0) {
                if (foundVary) return;
                initIndices.push_back(iv.initVal);
                foundVary = true;
            } else {
                initIndices.push_back(gep->get_operand(idx));
            }
        }
        if (!foundVary) return;
        auto *initPointer = new GetElementPtrInst(gep->get_operand(0),
                                                  initIndices, preheader);
        insertBeforeTerm(initPointer, preheader);
        auto *pointerPhi = PhiInst::create_phi(gep->type_, vecHeader);
        vecHeader->add_instruction_front(pointerPhi);
        pointerPhi->addIncoming(initPointer, preheader);
        gepPtrPhis[gep] = pointerPhi;
        gepLaneStrides[gep] = packed.laneStride;
    };
    auto registerOperand = [&](const PackedOperand &packed) {
        if (packed.kind == PackedOperand::INVARIANT) return;
        registerPtrOperand(packed);
        registerGEPOperand(packed);
    };
    registerOperand(group.lhs);
    registerOperand(group.rhs);

    auto *maxStart = new ConstantInt(module->int32_ty_,
                                     INT_MAX - (vectorTrip - 1));
    auto *addSafe = new ICmpInst(ICmpInst::ICMP_SLE, vecIVPhi, maxStart,
                                 vecHeader);
    auto *lastOffset = new ConstantInt(module->int32_ty_, vectorTrip - 1);
    auto *lastLane = new BinaryInst(module->int32_ty_, Instruction::Add,
                                    vecIVPhi, lastOffset, vecHeader);
    auto *lastInRange = new ICmpInst(ICmpInst::ICMP_SLT, lastLane, bound,
                                     vecHeader);
    auto *hasFullVector = new BinaryInst(module->int1_ty_, Instruction::And,
                                         addSafe, lastInRange, vecHeader);
    new BranchInst(hasFullVector, vecBody, vecExit, vecHeader);
    if (debugReduction)
        std::cerr << "[LoopVectorize:reduction] emit-header header="
                  << origHeader->name_ << "\n";

    std::unordered_map<Value*, Value*> splatCache;
    std::unordered_map<PhiInst*, Value*> lastPartPointers;
    std::unordered_map<GetElementPtrInst*, Value*> lastGEPPartPointers;
    auto emitPackedOperand = [&](const PackedOperand &packed,
                                 int part) -> Value * {
        if (packed.kind == PackedOperand::INVARIANT) {
            auto &entry = splatCache[packed.source];
            if (!entry) entry = emitSplat(packed.source, preheader);
            return entry;
        }

        if (auto *gepTemplate = dynamic_cast<GetElementPtrInst*>(packed.source)) {
            auto found = gepPtrPhis.find(gepTemplate);
            if (found == gepPtrPhis.end()) return nullptr;
            Value *partBase = found->second;
            if (part != 0) {
                auto *offset = new ConstantInt(
                    module->int32_ty_,
                    part * vecWidth * packed.laneStride);
                partBase = new GetElementPtrInst(partBase, {offset}, vecBody);
                lastGEPPartPointers[gepTemplate] = partBase;
            }

            if (packed.kind == PackedOperand::CONTIGUOUS) {
                auto *bc = new Bitcast(Instruction::BitCast, partBase,
                                       getVecPtrTy(packed.scalarTy), vecBody);
                return new LoadInst(bc, vecBody);
            }

            if (packed.kind == PackedOperand::GATHER) {
                Value *lanes[4] = {nullptr, nullptr, nullptr, nullptr};
                for (int lane = 0; lane < vecWidth; ++lane) {
                    Value *lanePointer = partBase;
                    if (lane != 0) {
                        auto *offset = new ConstantInt(
                            module->int32_ty_, lane * packed.laneStride);
                        lanePointer = new GetElementPtrInst(
                            partBase, {offset}, vecBody);
                    }
                    lanes[lane] = new LoadInst(lanePointer, vecBody);
                }
                return emitPack4(lanes, vecBody);
            }
        }

        auto *ptrPhi = dynamic_cast<PhiInst*>(packed.source);
        auto it = vecPtrPhis.find(ptrPhi);
        if (it == vecPtrPhis.end()) return nullptr;
        Value *basePtr = it->second;
        if (part != 0) {
            auto *partOffset = new ConstantInt(
                module->int32_ty_, part * vecWidth * packed.laneStride);
            basePtr = new GetElementPtrInst(basePtr, {partOffset}, vecBody);
            lastPartPointers[ptrPhi] = basePtr;
        }

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

    std::unordered_map<LoadInst *, PackedOperand> expressionLoadMap;
    for (const auto &entry : group.expressionLoads)
        expressionLoadMap.emplace(entry.first, entry.second);

    auto emitReductionPart = [&](Value *accumulator, int part) -> Value * {
        if (!group.expressionReduction) {
            Value *lhsVec = emitPackedOperand(group.lhs, part);
            if (!lhsVec) return nullptr;
            Value *perLaneVec = lhsVec;
            if (!group.noMul) {
                Value *rhsVec = emitPackedOperand(group.rhs, part);
                if (!rhsVec) return nullptr;
                perLaneVec = new BinaryInst(vecTy, Instruction::Mul, lhsVec,
                                            rhsVec, vecBody);
            }
            return new BinaryInst(
                vecTy, group.isAdd ? Instruction::Add : Instruction::Sub,
                accumulator, perLaneVec, vecBody);
        }

        std::unordered_map<Value *, Value *> expressionCache;
        std::function<Value *(Value *)> emitExpression = [&](Value *value)
            -> Value * {
            auto cached = expressionCache.find(value);
            if (cached != expressionCache.end()) return cached->second;

            Value *result = nullptr;
            if (isLoopInvariant(value, loop.blocks)) {
                auto &entry = splatCache[value];
                if (!entry) entry = emitSplat(value, preheader);
                result = entry;
            } else if (value == iv.phi) {
                Value *base = nullptr;
                for (int lane = 0; lane < vecWidth; ++lane) {
                    auto *index = new ConstantInt(module->int32_ty_, lane);
                    Value *insertBase = base ? base : vecIVPhi;
                    auto *insert = new InsertElementInst(
                        insertBase, vecIVPhi, index, vecBody);
                    if (lane == 0) insert->type_ = vecTy;
                    base = insert;
                }
                std::vector<Constant *> offsets;
                for (int lane = 0; lane < vecWidth; ++lane)
                    offsets.push_back(new ConstantInt(module->int32_ty_,
                                                       part * vecWidth + lane));
                auto *offsetVector = new ConstantVector(vecTyCast, offsets);
                result = new BinaryInst(vecTy, Instruction::Add, base,
                                        offsetVector, vecBody);
            } else if (auto *load = dynamic_cast<LoadInst *>(value)) {
                auto descriptor = expressionLoadMap.find(load);
                if (descriptor == expressionLoadMap.end()) return nullptr;
                result = emitPackedOperand(descriptor->second, part);
            } else if (auto *binary = dynamic_cast<BinaryInst *>(value)) {
                Value *lhs = emitExpression(binary->get_operand(0));
                Value *rhs = emitExpression(binary->get_operand(1));
                if (!lhs || !rhs) return nullptr;
                if (binary->op_id_ == Instruction::SDiv ||
                    binary->op_id_ == Instruction::SRem) {
                    Value *lanes[4] = {nullptr, nullptr, nullptr, nullptr};
                    for (int lane = 0; lane < vecWidth; ++lane) {
                        auto *index = new ConstantInt(module->int32_ty_, lane);
                        auto *scalarLHS = new ExtractElementInst(lhs, index,
                                                                 vecBody);
                        auto *scalarRHS = new ExtractElementInst(rhs, index,
                                                                 vecBody);
                        lanes[lane] = new BinaryInst(
                            module->int32_ty_, binary->op_id_, scalarLHS,
                            scalarRHS, vecBody);
                    }
                    result = emitPack4(lanes, vecBody);
                } else {
                    result = new BinaryInst(vecTy, binary->op_id_, lhs, rhs,
                                            vecBody);
                }
            }
            if (result) expressionCache[value] = result;
            return result;
        };

        Value *contribution = nullptr;
        for (auto *term : group.expressionTerms) {
            Value *termVector = emitExpression(term);
            if (!termVector) return nullptr;
            contribution = contribution
                               ? static_cast<Value *>(new BinaryInst(
                                     vecTy, Instruction::Add, contribution,
                                     termVector, vecBody))
                               : termVector;
        }
        if (!contribution) return nullptr;
        return new BinaryInst(vecTy, Instruction::Add, accumulator,
                              contribution, vecBody);
    };

    Value *vecAccNext = emitReductionPart(vecAccPhi, 0);
    if (!vecAccNext) return;
    if (reductionUF == 2) {
        vecAccNext = emitReductionPart(vecAccNext, 1);
        if (!vecAccNext) return;
    }
    if (debugReduction)
        std::cerr << "[LoopVectorize:reduction] emit-operands header="
                  << origHeader->name_ << "\n";

    vecAccPhi->addIncoming(vecAccNext, vecBody);

    auto *vecStep = new ConstantInt(module->int32_ty_, vectorTrip);
    auto *vecIVNext = new BinaryInst(module->int32_ty_, Instruction::Add,
                                     vecIVPhi, vecStep, vecBody);
    vecIVPhi->addIncoming(vecIVNext, vecBody);

    for (auto &kv : vecPtrPhis) {
        auto *phi = kv.first;
        Value *cur = kv.second;
        int laneStride = laneStrides[phi];
        Value *updateBase = cur;
        int updateLanes = vectorTrip;
        auto advanced = lastPartPointers.find(phi);
        if (advanced != lastPartPointers.end()) {
            updateBase = advanced->second;
            updateLanes = vecWidth;
        }
        auto *step = new ConstantInt(module->int32_ty_,
                                     updateLanes * laneStride);
        auto *next = new GetElementPtrInst(updateBase, {step}, vecBody);
        static_cast<PhiInst*>(cur)->addIncoming(next, vecBody);
    }
    for (auto &kv : gepPtrPhis) {
        auto *gep = kv.first;
        auto *phi = kv.second;
        Value *updateBase = phi;
        int updateLanes = vectorTrip;
        auto advanced = lastGEPPartPointers.find(gep);
        if (advanced != lastGEPPartPointers.end()) {
            updateBase = advanced->second;
            updateLanes = vecWidth;
        }
        auto *step = new ConstantInt(module->int32_ty_,
                                     updateLanes * gepLaneStrides[gep]);
        auto *next = new GetElementPtrInst(updateBase, {step}, vecBody);
        phi->addIncoming(next, vecBody);
    }

    new BranchInst(vecHeader, vecBody);
    if (debugReduction)
        std::cerr << "[LoopVectorize:reduction] emit-body header="
                  << origHeader->name_ << "\n";

    std::vector<Value*> laneVals;
    for (int lane = 0; lane < vecWidth; ++lane) {
        auto *index = new ConstantInt(module->int32_ty_, lane);
        laneVals.push_back(new ExtractElementInst(vecAccPhi, index, vecExit));
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
    origHeader->setSemFlag(SemFlag::VectorizedEpilogue);
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

namespace {

void replacePhiIncoming(PhiInst *phi, BasicBlock *oldPred,
                        Value *newValue, BasicBlock *newPred) {
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        if (phi->get_operand(i + 1) != oldPred) continue;
        phi->get_operand(i)->remove_use(phi->use_pos_[i]);
        phi->get_operand(i + 1)->remove_use(phi->use_pos_[i + 1]);
        phi->operands_[i] = newValue;
        phi->use_pos_[i] = newValue->add_use(phi, i);
        phi->operands_[i + 1] = newPred;
        phi->use_pos_[i + 1] = newPred->add_use(phi, i + 1);
        return;
    }
}

} // namespace

bool LoopVectorize::emitVectorizedLoop(
    const LoopVectorizationAnalysis::Plan &plan, Function *func,
    Module *module) {
    using AddressKind = LoopVectorizationAnalysis::AddressKind;

    BasicBlock *preheader = plan.preheader;
    BasicBlock *origHeader = plan.header;
    auto *preheaderBr = preheader->get_terminator();
    if (!preheaderBr || !preheaderBr->is_br() || preheaderBr->num_ops_ != 1 ||
        preheaderBr->get_operand(0) != origHeader)
        return false;

    const int VF = plan.vectorWidth;
    const int UF = plan.unrollFactor;
    const int vectorTrip = VF * UF;
    BasicBlock *vecHeader = new BasicBlock(module, "vector.header", func);
    BasicBlock *vecBody = new BasicBlock(module, "vector.body", func);
    BasicBlock *scalarPH = new BasicBlock(module, "scalar.ph", func);
    auto insertBeforePreheaderTerminator = [&](Instruction *inst) {
        preheader->remove_instr(inst);
        preheader->add_instruction_before_terminator(inst);
    };

    auto *vecIV = PhiInst::create_phi(module->int32_ty_, vecHeader);
    vecHeader->add_instruction_front(vecIV);
    vecIV->addIncoming(plan.induction.init, preheader);

    std::unordered_map<PhiInst *, PhiInst *> vectorPointers;
    for (const auto &recurrence : plan.pointerRecurrences) {
        auto *phi = PhiInst::create_phi(recurrence.phi->type_, vecHeader);
        vecHeader->add_instruction_front(phi);
        phi->addIncoming(recurrence.init, preheader);
        vectorPointers[recurrence.phi] = phi;
    }

    // Materialize one scalar pointer induction per normalized address group.
    // UF parts are fixed offsets from this recurrence, so equal load/store
    // addresses and adjacent parts cannot turn into parallel address phis in
    // the later strength-reduction pass.
    std::vector<PhiInst *> vectorAddressPhis(plan.memoryAccesses.size(),
                                              nullptr);
    std::vector<Value *> initialAddressForGroup(plan.memoryAccesses.size(),
                                                nullptr);
    for (const auto &access : plan.memoryAccesses) {
        if (access.addressKind != AddressKind::InductionGEP ||
            initialAddressForGroup[access.addressGroup])
            continue;

        std::vector<Value *> indices;
        for (unsigned i = 1; i < access.gep->num_ops_; ++i) {
            if (i != access.varyingIndex) {
                indices.push_back(access.gep->get_operand(i));
                continue;
            }
            Value *index = plan.induction.init;
            if (access.ivOffset != 0) {
                auto *offset =
                    new ConstantInt(module->int32_ty_, access.ivOffset);
                auto *adjusted = new BinaryInst(module->int32_ty_,
                                                Instruction::Add, index,
                                                offset, preheader, true);
                insertBeforePreheaderTerminator(adjusted);
                index = adjusted;
            }
            indices.push_back(index);
        }
        auto *initialPointer = new GetElementPtrInst(
            access.gep->get_operand(0), indices, preheader, true);
        insertBeforePreheaderTerminator(initialPointer);
        initialAddressForGroup[access.addressGroup] = initialPointer;
        auto *phi = PhiInst::create_phi(initialPointer->type_, vecHeader);
        vecHeader->add_instruction_front(phi);
        phi->addIncoming(initialPointer, preheader);
        vectorAddressPhis[access.addressGroup] = phi;
    }

    // Pointer recurrences already carry their scalar-loop entry pointer.
    // Materialize normalized offsets here so runtime range checks use the
    // exact first address touched by each access group.
    for (const auto &access : plan.memoryAccesses) {
        if (access.addressKind != AddressKind::PointerRecurrence ||
            initialAddressForGroup[access.addressGroup])
            continue;
        Value *initialPointer = nullptr;
        for (const auto &recurrence : plan.pointerRecurrences) {
            if (recurrence.phi != access.pointerPhi) continue;
            initialPointer = recurrence.init;
            break;
        }
        if (!initialPointer) return false;
        if (access.pointerOffset != 0) {
            auto *offset = new ConstantInt(module->int32_ty_,
                                           access.pointerOffset);
            auto *adjusted = new GetElementPtrInst(
                initialPointer, {offset}, preheader, true);
            insertBeforePreheaderTerminator(adjusted);
            initialPointer = adjusted;
        }
        initialAddressForGroup[access.addressGroup] = initialPointer;
    }

    // Hoist the signed-overflow proof and first full-vector test out of the
    // vector loop.  Proving `init + vectorTrip - 1 < bound` also proves that
    // `bound - vectorTrip` is representable.  Once entered, therefore,
    // `iv <= bound - vectorTrip` is exactly the scalar `iv < bound` condition
    // for every lane.  Constant initial values make the entry test a single
    // comparison, which is the common canonical-loop form.
    Value *canEnterVector = nullptr;
    if (auto *constantInit =
            dynamic_cast<ConstantInt *>(plan.induction.init)) {
        if (constantInit->value_ <= INT_MAX - (vectorTrip - 1)) {
            auto *lastInitialLane = new ConstantInt(
                module->int32_ty_,
                constantInit->value_ + vectorTrip - 1);
            auto *initialFull = new ICmpInst(ICmpInst::ICMP_SLT,
                                             lastInitialLane,
                                             plan.induction.bound, preheader);
            insertBeforePreheaderTerminator(initialFull);
            canEnterVector = initialFull;
        } else {
            canEnterVector = new ConstantInt(module->int1_ty_, 0);
        }
    } else {
        auto *maxStart = new ConstantInt(
            module->int32_ty_, INT_MAX - (vectorTrip - 1));
        auto *initAddSafe = new ICmpInst(ICmpInst::ICMP_SLE,
                                         plan.induction.init, maxStart,
                                         preheader);
        insertBeforePreheaderTerminator(initAddSafe);
        auto *lastOffset = new ConstantInt(module->int32_ty_,
                                           vectorTrip - 1);
        auto *lastInitialLane = new BinaryInst(
            module->int32_ty_, Instruction::Add, plan.induction.init,
            lastOffset, preheader, true);
        insertBeforePreheaderTerminator(lastInitialLane);
        auto *initialFull = new ICmpInst(ICmpInst::ICMP_SLT,
                                         lastInitialLane,
                                         plan.induction.bound, preheader);
        insertBeforePreheaderTerminator(initialFull);
        auto *entry = new BinaryInst(module->int1_ty_, Instruction::And,
                                     initAddSafe, initialFull, preheader,
                                     true);
        insertBeforePreheaderTerminator(entry);
        canEnterVector = entry;
    }

    // Wrapping subtraction is defined even on the bypassed path.  The entry
    // proof above guarantees a mathematical (non-wrapping) value whenever
    // vecHeader is reached.
    auto *trip = new ConstantInt(module->int32_ty_, vectorTrip);
    auto *vectorEnd = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                     plan.induction.bound, trip, preheader,
                                     true);
    insertBeforePreheaderTerminator(vectorEnd);

    if (!plan.runtimeMemoryChecks.empty()) {
        // The access range is [start, start + (bound - init)).  Comparing
        // half-open ranges with unsigned pointer order is sufficient for the
        // target's flat address space.  These GEPs are deliberately not
        // inbounds: on the vector-bypass path a wrapped trip value must remain
        // defined, while canEnterVector prevents that path reaching vecHeader.
        auto *span = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                    plan.induction.bound,
                                    plan.induction.init, preheader, true);
        insertBeforePreheaderTerminator(span);
        Type *checkPointerType =
            module->get_pointer_type(module->int32_ty_);

        Value *allDisjoint = nullptr;
        for (const auto &check : plan.runtimeMemoryChecks) {
            const auto &first = plan.memoryAccesses[check.firstAccess];
            const auto &second = plan.memoryAccesses[check.secondAccess];
            Value *firstStart = initialAddressForGroup[first.addressGroup];
            Value *secondStart = initialAddressForGroup[second.addressGroup];
            if (!firstStart || !secondStart) return false;

            auto *firstEnd = new GetElementPtrInst(
                firstStart, {span}, preheader, true);
            auto *secondEnd = new GetElementPtrInst(
                secondStart, {span}, preheader, true);
            insertBeforePreheaderTerminator(firstEnd);
            insertBeforePreheaderTerminator(secondEnd);

            auto castForCheck = [&](Value *pointer) -> Value * {
                if (pointer->type_ == checkPointerType) return pointer;
                auto *cast = new Bitcast(Instruction::BitCast, pointer,
                                         checkPointerType, preheader);
                insertBeforePreheaderTerminator(cast);
                return cast;
            };
            Value *firstStartCast = castForCheck(firstStart);
            Value *firstEndCast = castForCheck(firstEnd);
            Value *secondStartCast = castForCheck(secondStart);
            Value *secondEndCast = castForCheck(secondEnd);

            auto *firstBeforeSecond = new ICmpInst(
                ICmpInst::ICMP_ULE, firstEndCast, secondStartCast,
                preheader, true);
            auto *secondBeforeFirst = new ICmpInst(
                ICmpInst::ICMP_ULE, secondEndCast, firstStartCast,
                preheader, true);
            insertBeforePreheaderTerminator(firstBeforeSecond);
            insertBeforePreheaderTerminator(secondBeforeFirst);
            auto *disjoint = new BinaryInst(
                module->int1_ty_, Instruction::Or, firstBeforeSecond,
                secondBeforeFirst, preheader, true);
            insertBeforePreheaderTerminator(disjoint);
            if (!allDisjoint) {
                allDisjoint = disjoint;
            } else {
                auto *combined = new BinaryInst(
                    module->int1_ty_, Instruction::And, allDisjoint,
                    disjoint, preheader, true);
                insertBeforePreheaderTerminator(combined);
                allDisjoint = combined;
            }
        }
        auto *safeEntry = new BinaryInst(
            module->int1_ty_, Instruction::And, canEnterVector,
            allDisjoint, preheader, true);
        insertBeforePreheaderTerminator(safeEntry);
        canEnterVector = safeEntry;
    }

    auto *hasFullVector = new ICmpInst(ICmpInst::ICMP_SLE, vecIV,
                                       vectorEnd, vecHeader);
    new BranchInst(hasFullVector, vecBody, scalarPH, vecHeader);

    auto vectorType = [&](Type *scalar) -> Type * {
        return module->get_vector_type(scalar, VF);
    };
    auto vectorPointerType = [&](Type *scalar) -> Type * {
        return module->get_pointer_type(vectorType(scalar));
    };

    std::unordered_map<Value *, Value *> splats;
    std::unordered_map<Instruction *, Value *> uniformVectors;

    auto emitSplat = [&](Value *scalar) -> Value * {
        auto found = splats.find(scalar);
        if (found != splats.end()) return found->second;
        Value *result = nullptr;
        for (int lane = 0; lane < VF; ++lane) {
            auto *index = new ConstantInt(module->int32_ty_, lane);
            Value *base = result ? result : scalar;
            auto *insert = new InsertElementInst(base, scalar, index, preheader);
            if (lane == 0) insert->type_ = vectorType(scalar->type_);
            insertBeforePreheaderTerminator(insert);
            result = insert;
        }
        splats[scalar] = result;
        return result;
    };

    struct PendingStore {
        Value *value = nullptr;
        Value *pointer = nullptr;
    };
    std::vector<PendingStore> pendingStores;

    std::vector<Value *> partIVs(UF, vecIV);
    std::vector<Value *> vectorIVs(UF, nullptr);
    std::vector<std::unordered_map<Value *, Value *>> partVectorValues(UF);
    std::vector<std::unordered_map<size_t, Value *>> partVectorAddresses(UF);
    for (int part = 1; part < UF; ++part) {
        auto *partOffset = new ConstantInt(module->int32_ty_, part * VF);
        partIVs[part] = new BinaryInst(module->int32_ty_, Instruction::Add,
                                      vecIV, partOffset, vecBody);
    }

    auto buildVectorPointer = [&](
        int part,
        const LoopVectorizationAnalysis::MemoryAccess &access) -> Value * {
        Value *scalarPointer = nullptr;
        if (access.addressKind == AddressKind::Uniform) return nullptr;
        if (access.addressKind == AddressKind::PointerRecurrence) {
            auto found = vectorPointers.find(access.pointerPhi);
            if (found == vectorPointers.end()) return nullptr;
            scalarPointer = found->second;
            int offset = access.pointerOffset;
            // Pointer recurrence descriptors carry the element step; use it
            // rather than byte arithmetic when selecting a UF part.
            for (const auto &recurrence : plan.pointerRecurrences) {
                if (recurrence.phi == access.pointerPhi) {
                    offset = access.pointerOffset +
                             part * VF * recurrence.step;
                    break;
                }
            }
            if (offset != 0) {
                auto *constant = new ConstantInt(module->int32_ty_, offset);
                scalarPointer = new GetElementPtrInst(
                    scalarPointer, {constant}, vecBody);
            }
        } else {
            scalarPointer = vectorAddressPhis[access.addressGroup];
            if (!scalarPointer) return nullptr;
            if (part != 0) {
                auto *offset = new ConstantInt(module->int32_ty_, part * VF);
                scalarPointer = new GetElementPtrInst(
                    scalarPointer, {offset}, vecBody);
            }
        }
        return new Bitcast(Instruction::BitCast, scalarPointer,
                           vectorPointerType(access.scalarType), vecBody);
    };

    // Keep independent UF-part loads together.  On an in-order target this
    // exposes enough work to cover load-to-use latency before arithmetic, and
    // it also keeps the values distinct through register allocation so the
    // machine scheduler can retain that ordering.  Dependence legality and
    // terminal-store ordering were proved when the plan was built.
    for (int part = 0; part < UF; ++part) {
        auto &vectorValues = partVectorValues[part];
        auto &vectorAddresses = partVectorAddresses[part];
        for (auto *inst : plan.recipes) {
            auto accessIt = plan.accessForInst.find(inst);
            if (accessIt == plan.accessForInst.end() || !inst->is_load())
                continue;
            const auto &access = plan.memoryAccesses[accessIt->second];
            if (access.addressKind == AddressKind::Uniform) {
                auto found = uniformVectors.find(inst);
                if (found == uniformVectors.end()) {
                    Value *pointer = inst->get_operand(0);
                    if (auto *gep = dynamic_cast<GetElementPtrInst *>(pointer)) {
                        std::vector<Value *> indices;
                        for (unsigned i = 1; i < gep->num_ops_; ++i)
                            indices.push_back(gep->get_operand(i));
                        auto *clone = new GetElementPtrInst(
                            gep->get_operand(0), indices, preheader, true);
                        preheader->add_instruction_before_terminator(clone);
                        pointer = clone;
                    }
                    auto *scalarLoad = new LoadInst(pointer, preheader, true);
                    preheader->add_instruction_before_terminator(scalarLoad);
                    found = uniformVectors.emplace(inst,
                                                   emitSplat(scalarLoad)).first;
                }
                vectorValues[inst] = found->second;
                continue;
            }
            Value *pointer = nullptr;
            auto pointerIt = vectorAddresses.find(access.addressGroup);
            if (pointerIt != vectorAddresses.end()) {
                pointer = pointerIt->second;
            } else {
                pointer = buildVectorPointer(part, access);
                if (!pointer) return false;
                vectorAddresses.emplace(access.addressGroup, pointer);
            }
            vectorValues[inst] = new LoadInst(pointer, vecBody);
        }
    }

    for (int part = 0; part < UF; ++part) {
        Value *partIV = partIVs[part];
        auto &vectorValues = partVectorValues[part];
        auto &vectorAddresses = partVectorAddresses[part];
        auto getVectorIV = [&]() -> Value * {
            if (vectorIVs[part]) return vectorIVs[part];
            Value *base = nullptr;
            for (int lane = 0; lane < VF; ++lane) {
                auto *index = new ConstantInt(module->int32_ty_, lane);
                Value *insertBase = base ? base : partIV;
                auto *insert = new InsertElementInst(insertBase, partIV, index,
                                                      vecBody);
                if (lane == 0)
                    insert->type_ = vectorType(module->int32_ty_);
                base = insert;
            }
            std::vector<Constant *> offsets;
            for (int lane = 0; lane < VF; ++lane)
                offsets.push_back(new ConstantInt(module->int32_ty_, lane));
            auto *offsetVector = new ConstantVector(
                static_cast<VectorType *>(vectorType(module->int32_ty_)),
                offsets);
            vectorIVs[part] = new BinaryInst(
                vectorType(module->int32_ty_), Instruction::Add, base,
                offsetVector, vecBody);
            return vectorIVs[part];
        };

        auto getVectorOperand = [&](Value *value) -> Value * {
            if (value == plan.induction.phi) return getVectorIV();
            auto found = vectorValues.find(value);
            if (found != vectorValues.end() && found->second)
                return found->second;
            auto *inst = dynamic_cast<Instruction *>(value);
            if (!inst || !plan.loop->blocks.count(inst->parent_))
                return emitSplat(value);
            return nullptr;
        };

        for (auto *inst : plan.recipes) {
            if (inst == plan.induction.update ||
                inst == plan.induction.compare)
                continue;
            bool pointerUpdate = false;
            for (const auto &recurrence : plan.pointerRecurrences)
                pointerUpdate |= inst == recurrence.update;
            if (pointerUpdate || inst->is_gep()) continue;

            auto accessIt = plan.accessForInst.find(inst);
            if (accessIt != plan.accessForInst.end()) {
                const auto &access = plan.memoryAccesses[accessIt->second];
                if (access.addressKind == AddressKind::Uniform) {
                    if (!inst->is_load()) return false;
                    continue;
                }
                Value *pointer = nullptr;
                auto pointerIt = vectorAddresses.find(access.addressGroup);
                if (pointerIt != vectorAddresses.end()) {
                    pointer = pointerIt->second;
                } else {
                    pointer = buildVectorPointer(part, access);
                    if (!pointer) return false;
                    vectorAddresses.emplace(access.addressGroup, pointer);
                }
                if (inst->is_load()) {
                    continue;
                } else {
                    Value *stored = getVectorOperand(inst->get_operand(0));
                    if (!stored) return false;
                    if (UF > 1 && plan.canDeferStoresAcrossParts)
                        pendingStores.push_back({stored, pointer});
                    else
                        new StoreInst(stored, pointer, vecBody);
                }
                continue;
            }

            auto *binary = dynamic_cast<BinaryInst *>(inst);
            if (!binary) return false;
            Value *lhs = getVectorOperand(binary->get_operand(0));
            Value *rhs = getVectorOperand(binary->get_operand(1));
            if (!lhs || !rhs) return false;
            vectorValues[inst] = new BinaryInst(vectorType(binary->type_),
                                                binary->op_id_, lhs, rhs,
                                                vecBody);
        }
    }

    for (const auto &store : pendingStores)
        new StoreInst(store.value, store.pointer, vecBody);

    auto *vectorStep = new ConstantInt(module->int32_ty_, vectorTrip);
    auto *vecIVNext = new BinaryInst(module->int32_ty_, Instruction::Add,
                                     vecIV, vectorStep, vecBody);
    vecIV->addIncoming(vecIVNext, vecBody);
    for (const auto &recurrence : plan.pointerRecurrences) {
        auto *phi = vectorPointers[recurrence.phi];
        auto *step = new ConstantInt(module->int32_ty_,
                                     vectorTrip * recurrence.step);
        auto *next = new GetElementPtrInst(phi, {step}, vecBody);
        phi->addIncoming(next, vecBody);
    }
    for (auto *phi : vectorAddressPhis) {
        if (!phi) continue;
        auto *step = new ConstantInt(module->int32_ty_, vectorTrip);
        auto *next = new GetElementPtrInst(phi, {step}, vecBody);
        phi->addIncoming(next, vecBody);
    }
    new BranchInst(vecHeader, vecBody);

    // The original scalar loop is the epilogue.  The preheader can bypass the
    // vector loop when the subtraction is unsafe or fewer than vectorTrip
    // iterations remain, so merge both entry states explicitly in scalarPH.
    auto *scalarIV = PhiInst::create_phi(module->int32_ty_, scalarPH);
    scalarPH->add_instruction_front(scalarIV);
    scalarIV->addIncoming(plan.induction.init, preheader);
    scalarIV->addIncoming(vecIV, vecHeader);
    replacePhiIncoming(plan.induction.phi, preheader, scalarIV, scalarPH);
    for (const auto &recurrence : plan.pointerRecurrences) {
        auto *scalarPointer = PhiInst::create_phi(recurrence.phi->type_,
                                                  scalarPH);
        scalarPH->add_instruction_front(scalarPointer);
        scalarPointer->addIncoming(recurrence.init, preheader);
        scalarPointer->addIncoming(vectorPointers[recurrence.phi], vecHeader);
        replacePhiIncoming(recurrence.phi, preheader, scalarPointer, scalarPH);
    }
    new BranchInst(origHeader, scalarPH);

    preheader->delete_instr(preheaderBr);
    preheader->remove_succ_basic_block(origHeader);
    origHeader->remove_pre_basic_block(preheader);
    new BranchInst(canEnterVector, vecHeader, scalarPH, preheader);
    vecHeader->setSemFlag(SemFlag::TargetPointerRecurrenceLoop);
    origHeader->setSemFlag(SemFlag::VectorizedEpilogue);

    func->invalidateDominatorInfo();
    func->set_instr_name();
    return true;
}

bool LoopVectorize::tryVectorize(Loop &loop, Function *func, Module *module,
                                 const BasicAliasAnalysis &BAA) {
    InductionVar reductionIV;
    if (findInductionVar(loop, reductionIV)) {
        ReductionGroup reduction;
        if (analyzeReductionLoop(loop, reductionIV, reduction)) {
            if (std::getenv("DEBUG_LOOP_VECTORIZE"))
                std::cerr << "[LoopVectorize] reduction func=" << func->name_
                          << " header=" << loop.header->name_ << "\n";
            emitReductionVectorizedLoop(loop, reductionIV, reduction,
                                        VECTORIZE_FACTOR, func, module);
            return true;
        }
    }

    LoopVectorizationAnalysis analysis(BAA);
    LoopVectorizationAnalysis::Plan plan;
    std::string reason;
    if (!analysis.buildPlan(loop, plan, &reason)) {
        if (std::getenv("DEBUG_LOOP_VECTORIZE_REJECT"))
            std::cerr << "[LoopVectorize] reject func=" << func->name_
                      << " header=" << loop.header->name_
                      << " reason=" << reason << "\n";
        return false;
    }
    if (std::getenv("DEBUG_LOOP_VECTORIZE"))
        std::cerr << "[LoopVectorize] element-wise func=" << func->name_
                  << " header=" << loop.header->name_
                  << " accesses=" << plan.memoryAccesses.size()
                  << " vf=" << plan.vectorWidth
                  << " uf=" << plan.unrollFactor
                  << " scalar-cost=" << plan.scalarCost
                  << " vector-cost=" << plan.vectorCost << "\n";
    return emitVectorizedLoop(plan, func, module);
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
