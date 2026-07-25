#include "../../include/mid/opt/ifConversion.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <set>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
namespace {

// 判断指令是否可以安全投机执行（在条件不成立时也执行不影响正确性）
// Store / Call / SDiv / SRem 等有副作用或可能 fault 的指令不可投机。
// SysY 数组在边界内，Load / GEP 总是安全的。
static bool isSafeToSpeculate(Instruction *inst) {
    if (inst->is_div()) {
        auto *divisor = dynamic_cast<ConstantInt *>(inst->get_operand(1));
        // Division by a nonzero constant other than -1 cannot trap or hit the
        // signed overflow case, so executing it on both paths is defined.
        return divisor && divisor->value_ != 0 && divisor->value_ != -1;
    }
    if (inst->op_id_ == Instruction::Shl || inst->op_id_ == Instruction::LShr ||
        inst->op_id_ == Instruction::AShr) {
        auto *shift = dynamic_cast<ConstantInt *>(inst->get_operand(1));
        auto *type = dynamic_cast<IntegerType *>(inst->get_operand(0)->type_);
        return shift && type && shift->value_ >= 0 &&
               shift->value_ < type->num_bits_;
    }
    switch (inst->op_id_) {
    case Instruction::Add:  case Instruction::Sub:  case Instruction::Mul:
    case Instruction::And:  case Instruction::Or:   case Instruction::Xor:
    case Instruction::Shl:  case Instruction::LShr: case Instruction::AShr:
    case Instruction::FAdd: case Instruction::FSub: case Instruction::FMul:
    case Instruction::Load: case Instruction::GetElementPtr:
    case Instruction::ZExt: case Instruction::SItoFP: case Instruction::FPtoSI:
    case Instruction::ICmp: case Instruction::FCmp:
        return true;
    default:
        return false; // Store, Call, SRem, Alloca, Ret, Br …
    }
}

// A canonical `phi(init, phi + 1)` induction variable guarded by
// `phi < bound` is non-negative in its loop body when init is non-negative.
// The increment cannot overflow before the next guard because the current
// value is strictly below a signed i32 bound.
static bool isNonNegativeUnitInduction(Value *value, BasicBlock *body) {
    auto *phi = dynamic_cast<PhiInst *>(value);
    if (!phi || phi->num_ops_ != 4 || !phi->parent_ || !body)
        return false;

    bool hasNonNegativeInit = false;
    bool hasUnitUpdate = false;
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        Value *incoming = phi->get_operand(i);
        if (auto *init = dynamic_cast<ConstantInt *>(incoming)) {
            if (init->value_ >= 0) hasNonNegativeInit = true;
            continue;
        }
        auto *update = dynamic_cast<BinaryInst *>(incoming);
        if (!update || !update->is_add()) continue;
        auto *step = dynamic_cast<ConstantInt *>(update->get_operand(1));
        if (update->get_operand(0) == phi && step && step->value_ == 1)
            hasUnitUpdate = true;
    }
    if (!hasNonNegativeInit || !hasUnitUpdate) return false;

    auto *headerTerm = dynamic_cast<BranchInst *>(phi->parent_->get_terminator());
    if (!headerTerm || headerTerm->num_ops_ != 3 ||
        headerTerm->get_operand(1) != body)
        return false;
    auto *compare = dynamic_cast<ICmpInst *>(headerTerm->get_operand(0));
    return compare && compare->icmp_op_ == ICmpInst::ICMP_SLT &&
           compare->get_operand(0) == phi;
}

static bool isKnownOneBitValue(Value *value, unsigned depth = 0) {
    if (!value || depth > 8) return false;
    if (auto *constant = dynamic_cast<ConstantInt *>(value))
        return constant->value_ == 0 || constant->value_ == 1;
    auto *andInst = dynamic_cast<BinaryInst *>(value);
    if (!andInst || andInst->op_id_ != Instruction::And) return false;
    for (unsigned i = 0; i < 2; ++i) {
        auto *constant =
            dynamic_cast<ConstantInt *>(andInst->get_operand(i));
        if (constant && constant->value_ == 1) return true;
    }
    return isKnownOneBitValue(andInst->get_operand(0), depth + 1) &&
           isKnownOneBitValue(andInst->get_operand(1), depth + 1);
}

// Turn a select introduced by if-conversion into lane-wise integer arithmetic
// when its exact one-bit predicate proves an all-zero/all-one mask:
//
//   select (bit == 0), (base + delta), base
//     -> base + (delta & (bit - 1))
//
// Returns null when the proof or value shape is not exact.
static Value *tryCreateMaskedConditionalAdd(Value *condition, Value *trueValue,
                                            Value *falseValue, BasicBlock *bb,
                                            Instruction *before,
                                            Instruction *&deadSum) {
    deadSum = nullptr;
    auto *type = dynamic_cast<IntegerType *>(trueValue->type_);
    auto *compare = dynamic_cast<ICmpInst *>(condition);
    if (!type || type->num_bits_ != 32 || !compare ||
        compare->icmp_op_ != ICmpInst::ICMP_EQ)
        return nullptr;

    Value *bit = nullptr;
    for (unsigned i = 0; i < 2; ++i) {
        auto *zero =
            dynamic_cast<ConstantInt *>(compare->get_operand(1 - i));
        if (zero && zero->value_ == 0) {
            bit = compare->get_operand(i);
            break;
        }
    }
    if (!bit || bit->type_ != type || !isKnownOneBitValue(bit))
        return nullptr;

    auto *sum = dynamic_cast<BinaryInst *>(trueValue);
    if (!sum || !sum->is_add()) return nullptr;
    Value *delta = nullptr;
    if (sum->get_operand(0) == falseValue)
        delta = sum->get_operand(1);
    else if (sum->get_operand(1) == falseValue)
        delta = sum->get_operand(0);
    if (!delta) return nullptr;

    auto *mask = new BinaryInst(type, Instruction::Sub, bit,
                                new ConstantInt(type, 1), bb, true);
    bb->add_instruction_before_inst(mask, before);
    auto *masked = new BinaryInst(type, Instruction::And, delta, mask,
                                  bb, true);
    bb->add_instruction_before_inst(masked, before);
    auto *result = new BinaryInst(type, Instruction::Add, falseValue, masked,
                                  bb, true);
    bb->add_instruction_before_inst(result, before);
    deadSum = sum;
    return result;
}

// Convert a store-only diamond by selecting its destination address.  Both
// arms are evaluated before the select, so all non-store instructions must be
// safe to speculate.  A shared load value is accepted as well: it is moved to
// the predecessor once, retaining the original single load per iteration.
static bool tryConvertStoreDiamond(Function *func) {
    for (auto *B : func->basic_blocks_) {
        auto *br = dynamic_cast<BranchInst *>(B->get_terminator());
        if (!br || br->num_ops_ != 3) continue;

        auto *T = static_cast<BasicBlock *>(br->get_operand(1));
        auto *F = static_cast<BasicBlock *>(br->get_operand(2));
        if (T == F || T->pre_bbs_.size() != 1 || F->pre_bbs_.size() != 1)
            continue;

        auto *tBr = dynamic_cast<BranchInst *>(T->get_terminator());
        auto *fBr = dynamic_cast<BranchInst *>(F->get_terminator());
        if (!tBr || !fBr || tBr->num_ops_ != 1 || fBr->num_ops_ != 1)
            continue;
        auto *M = static_cast<BasicBlock *>(tBr->get_operand(0));
        if (M != fBr->get_operand(0) || M == B || M == T || M == F)
            continue;

        auto collectArm = [](BasicBlock *bb, StoreInst *&store,
                             std::vector<Instruction *> &insts) {
            store = nullptr;
            for (auto *inst : bb->instr_list_) {
                if (inst->is_br()) continue;
                if (inst->is_store()) {
                    if (store) return false;
                    store = static_cast<StoreInst *>(inst);
                    continue;
                }
                if (store || !isSafeToSpeculate(inst)) return false;
                insts.push_back(inst);
            }
            return store != nullptr;
        };

        StoreInst *tStore = nullptr;
        StoreInst *fStore = nullptr;
        std::vector<Instruction *> tInsts;
        std::vector<Instruction *> fInsts;
        if (!collectArm(T, tStore, tInsts) ||
            !collectArm(F, fStore, fInsts) ||
            tStore->get_operand(0)->type_ != fStore->get_operand(0)->type_)
            continue;

        Value *storedValue = tStore->get_operand(0);
        if (storedValue != fStore->get_operand(0)) {
            auto *tLoad = dynamic_cast<LoadInst *>(storedValue);
            auto *fLoad = dynamic_cast<LoadInst *>(fStore->get_operand(0));
            if (!tLoad || !fLoad || tLoad->get_operand(0) != fLoad->get_operand(0) ||
                tLoad->use_list_.size() != 1 || fLoad->use_list_.size() != 1)
                continue;
            fLoad->replace_all_use_with(tLoad);
            F->delete_instr(fLoad);
            fInsts.erase(std::remove(fInsts.begin(), fInsts.end(), fLoad),
                         fInsts.end());
        }

        std::set<Instruction *> exactInstructions;
        for (auto *inst : tInsts)
            if (inst->hasSemFlag(SemFlag::Exact))
                exactInstructions.insert(inst);

        // Moving an operation out of its guarded arm makes its wrapping and
        // exactness promises invalid on the formerly unexecuted path.  The
        // plain operation has the same result on the selected path.
        auto makeSpeculatable = [](Instruction *inst) {
            inst->clearSemFlag(SemFlag::NoSignedWrap);
            inst->clearSemFlag(SemFlag::NoUnsignedWrap);
            inst->clearSemFlag(SemFlag::Exact);
            inst->clearSemFlag(SemFlag::Disjoint);
        };
        for (auto *inst : tInsts) {
            makeSpeculatable(inst);
            T->remove_instr(inst);
            B->add_instruction_before_inst(inst, br);
        }
        for (auto *inst : fInsts) {
            makeSpeculatable(inst);
            F->remove_instr(inst);
            B->add_instruction_before_inst(inst, br);
        }

        Value *selectedPtr = nullptr;
        auto *tGep = dynamic_cast<GetElementPtrInst *>(tStore->get_operand(1));
        auto *fGep = dynamic_cast<GetElementPtrInst *>(fStore->get_operand(1));
        if (tGep && fGep && tGep->get_operand(0) == fGep->get_operand(0) &&
            tGep->num_ops_ == fGep->num_ops_) {
            auto sameIndex = [](Value *lhs, Value *rhs) {
                if (lhs == rhs) return true;
                auto *lhsConst = dynamic_cast<ConstantInt *>(lhs);
                auto *rhsConst = dynamic_cast<ConstantInt *>(rhs);
                return lhsConst && rhsConst && lhsConst->type_ == rhsConst->type_ &&
                       lhsConst->value_ == rhsConst->value_;
            };
            unsigned differingIndex = 0;
            for (unsigned i = 1; i < tGep->num_ops_; ++i) {
                if (sameIndex(tGep->get_operand(i), fGep->get_operand(i))) continue;
                if (differingIndex != 0) {
                    differingIndex = 0;
                    break;
                }
                differingIndex = i;
            }
            if (differingIndex != 0) {
                Value *trueIndex = tGep->get_operand(differingIndex);
                Value *falseIndex = fGep->get_operand(differingIndex);
                Value *selectedIndex = nullptr;

                // `ashr exact x, 1` is equal to `sdiv x, 2` on its defined
                // path.  When the other path is `offset + sdiv x, 2`, compute
                // that quotient once and select only the offset.  This is a
                // general conditional-index identity, not a source-pattern
                // special case.
                auto *exactShift = dynamic_cast<BinaryInst *>(trueIndex);
                auto *offsetAdd = dynamic_cast<BinaryInst *>(falseIndex);
                Value *offset = nullptr;
                if (exactShift && exactShift->op_id_ == Instruction::AShr &&
                    exactInstructions.count(exactShift) && offsetAdd &&
                    offsetAdd->is_add()) {
                    auto *shiftAmount = dynamic_cast<ConstantInt *>(
                        exactShift->get_operand(1));
                    for (unsigned i = 0; i < 2; ++i) {
                        auto *division = dynamic_cast<BinaryInst *>(
                            offsetAdd->get_operand(i));
                        auto *divisor = division ? dynamic_cast<ConstantInt *>(
                            division->get_operand(1)) : nullptr;
                        if (!division || division->op_id_ != Instruction::SDiv ||
                            !divisor || divisor->value_ != 2 ||
                            division->get_operand(0) != exactShift->get_operand(0))
                            continue;
                        offset = offsetAdd->get_operand(1 - i);
                        break;
                    }
                    if (shiftAmount && shiftAmount->value_ == 1 && offset) {
                        Type *indexType = trueIndex->type_;
                        Instruction::OpID quotientOp =
                            isNonNegativeUnitInduction(
                                exactShift->get_operand(0), B)
                                ? Instruction::LShr
                                : Instruction::SDiv;
                        int quotientAmount = quotientOp == Instruction::LShr ? 1 : 2;
                        auto *quotient = new BinaryInst(
                            indexType, quotientOp,
                            exactShift->get_operand(0),
                            new ConstantInt(indexType, quotientAmount), B, true);
                        B->add_instruction_before_inst(quotient, br);
                        auto *selectedOffset = new SelectInst(
                            br->get_operand(0), new ConstantInt(indexType, 0),
                            offset, indexType);
                        B->add_instruction_before_inst(selectedOffset, br);
                        auto *indexAdd = new BinaryInst(
                            indexType, Instruction::Add, quotient,
                            selectedOffset, B, true);
                        B->add_instruction_before_inst(indexAdd, br);
                        selectedIndex = indexAdd;
                    }
                }
                if (!selectedIndex) {
                    auto *indexSelect = new SelectInst(
                        br->get_operand(0), trueIndex, falseIndex,
                        trueIndex->type_);
                    B->add_instruction_before_inst(indexSelect, br);
                    selectedIndex = indexSelect;
                }
                std::vector<Value *> indices;
                for (unsigned i = 1; i < tGep->num_ops_; ++i)
                    indices.push_back(i == differingIndex ? selectedIndex
                                                          : tGep->get_operand(i));
                auto *mergedGep = new GetElementPtrInst(tGep->get_operand(0),
                                                         indices, B, true);
                B->add_instruction_before_inst(mergedGep, br);
                selectedPtr = mergedGep;
            }
        }
        if (!selectedPtr) {
            auto *ptrSelect = new SelectInst(br->get_operand(0),
                                              tStore->get_operand(1),
                                              fStore->get_operand(1),
                                              tStore->get_operand(1)->type_);
            B->add_instruction_before_inst(ptrSelect, br);
            selectedPtr = ptrSelect;
        }
        auto *selectedStore = new StoreInst(tStore->get_operand(0), selectedPtr,
                                            B, true);
        B->add_instruction_before_inst(selectedStore, br);

        T->delete_instr(tStore);
        F->delete_instr(fStore);
        T->delete_instr(tBr);
        F->delete_instr(fBr);
        B->remove_succ_basic_block(T);
        B->remove_succ_basic_block(F);
        T->remove_pre_basic_block(B);
        F->remove_pre_basic_block(B);
        B->delete_instr(br);
        new BranchInst(M, B);
        func->remove_bb(T);
        func->remove_bb(F);
        return true;
    }
    return false;
}

// 尝试对一个循环做 if-conversion，成功返回 true
static bool tryConvert(Loop &loop, Function *func) {
    // 只处理最内层循环
    if (!loop.children.empty()) return false;
    // 只处理恰好 4 块的循环
    if (loop.blocks.size() != 4) return false;

    BasicBlock *H = loop.header;
    BasicBlock *L = loop.singleLatch();
    if (!L) return false;

    // H 必须有条件分支（到循环体或出口）
    auto *H_br = dynamic_cast<BranchInst *>(H->get_terminator());
    if (!H_br || H_br->num_ops_ != 3) return false;

    // 从 H 的分支找到 B（在循环内的那个后继）
    BasicBlock *B = nullptr;
    {
        auto *td = static_cast<BasicBlock *>(H_br->get_operand(1));
        auto *fd = static_cast<BasicBlock *>(H_br->get_operand(2));
        bool td_in = loop.blocks.count(td), fd_in = loop.blocks.count(fd);
        if (td_in && !fd_in)       B = td;
        else if (fd_in && !td_in)  B = fd;
        else return false;
    }
    if (B == H || B == L) return false;

    // B 必须有条件分支，两个目标分别为 T（if-body）和 L（latch）
    auto *B_br = dynamic_cast<BranchInst *>(B->get_terminator());
    if (!B_br || B_br->num_ops_ != 3) return false;

    BasicBlock *T = nullptr;
    bool true_is_T = false; // B_cond==true 时跳到 T？
    {
        auto *td = static_cast<BasicBlock *>(B_br->get_operand(1));
        auto *fd = static_cast<BasicBlock *>(B_br->get_operand(2));
        if (td == L && loop.blocks.count(fd) && fd != H && fd != B) {
            T = fd; true_is_T = false; // false 分支是 T
        } else if (fd == L && loop.blocks.count(td) && td != H && td != B) {
            T = td; true_is_T = true;  // true  分支是 T
        } else return false;
    }

    // T 必须无条件跳到 L
    auto *T_br = dynamic_cast<BranchInst *>(T->get_terminator());
    if (!T_br || T_br->num_ops_ != 1) return false;
    if (static_cast<BasicBlock *>(T_br->get_operand(0)) != L) return false;

    // L 必须无条件跳回 H
    auto *L_br = dynamic_cast<BranchInst *>(L->get_terminator());
    if (!L_br || L_br->num_ops_ != 1) return false;
    if (static_cast<BasicBlock *>(L_br->get_operand(0)) != H) return false;

    // 安全检查：T 的所有非终止指令必须可投机执行
    for (auto *inst : T->instr_list_) {
        if (inst->is_br()) continue;
        if (!isSafeToSpeculate(inst)) return false;
    }

    // 收集 T 的非终止指令（复制一份，因为迭代过程中会修改 instr_list_）
    std::vector<Instruction *> t_insts;
    for (auto *inst : T->instr_list_) {
        if (!inst->is_br()) t_insts.push_back(inst);
    }

    // L 的 phi 节点：每个必须恰好有来自 B 和 T 的入边
    std::vector<PhiInst *> latch_phis;
    for (auto *inst : L->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        bool hasB = false, hasT = false;
        for (unsigned i = 1; i < phi->num_ops_; i += 2) {
            auto *src = static_cast<BasicBlock *>(phi->get_operand(i));
            if (src == B) hasB = true;
            if (src == T) hasT = true;
        }
        if (!hasB || !hasT) return false;
        latch_phis.push_back(phi);
    }

    // ─── 变换开始 ───────────────────────────────────────────────────────────

    // 1. 把 T 的指令（不含终止指令）移到 B 的分支之前
    for (auto *inst : t_insts) {
        T->remove_instr(inst);
        B->add_instruction_before_inst(inst, B_br);
    }

    // 2. 用 select 替换 L 中的每个 phi，插入到 B 的分支之前
    Value *B_cond = B_br->get_operand(0);
    std::vector<Instruction *> maybeDead;
    for (auto *phi : latch_phis) {
        Value *val_B = nullptr, *val_T = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *src = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (src == B) val_B = phi->get_operand(i);
            if (src == T) val_T = phi->get_operand(i);
        }
        // select(cond, tv, fv): cond==true 时取 tv
        Value *tv = true_is_T ? val_T : val_B;
        Value *fv = true_is_T ? val_B : val_T;
        Instruction *deadSum = nullptr;
        Value *replacement = tryCreateMaskedConditionalAdd(
            B_cond, tv, fv, B, B_br, deadSum);
        if (!replacement) {
            auto *sel = new SelectInst(B_cond, tv, fv, phi->type_);
            B->add_instruction_before_inst(sel, B_br);
            replacement = sel;
        } else if (deadSum) {
            maybeDead.push_back(deadSum);
        }
        phi->replace_all_use_with(replacement);
        L->delete_instr(phi);
    }

    // 3. 更新 B 的分支：去掉 B→T 的 CFG 边，改为无条件跳 L
    B->remove_succ_basic_block(T);
    T->remove_pre_basic_block(B);
    B->delete_instr(B_br);
    // BranchInst(L, B) 内部会调用 L->add_pre(B) 和 B->add_succ(L)，
    // 两者均有去重检查（L 已是 B 的 succ），所以不会产生重复边。
    new BranchInst(L, B);

    maybeDead.push_back(dynamic_cast<Instruction *>(B_cond));
    for (auto *candidate : maybeDead) {
        if (candidate && candidate->parent_ == B &&
            candidate->use_list_.empty())
            B->delete_instr(candidate);
    }

    // 4. 清理 T 并从函数中移除
    T->delete_instr(T_br); // 清除 T_br 对 L 的 use_list_ 引用
    func->remove_bb(T);    // 从 basic_blocks_ 移除，并通过 succ_bbs_ 清理 L 的 pre 列表

    return true;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

void IfConversion::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

PreservedAnalyses IfConversion::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool IfConversion::runOnFunction(Function *func) {
    bool changed = false;
    bool any = true;
    while (any) {
        any = false;
        if (tryConvertStoreDiamond(func)) {
            any = true;
            changed = true;
            continue;
        }
        LoopInfo LI;
        LI.analyze(func);
        for (auto &lp : LI.allLoops()) {
            if (tryConvert(*lp, func)) {
                any = true;
                changed = true;
                break; // CFG 已变，重新分析
            }
        }
    }
    return changed;
}
