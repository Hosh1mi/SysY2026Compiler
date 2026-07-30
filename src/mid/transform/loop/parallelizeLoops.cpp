#include "../../../include/mid/opt/parallelizeLoops.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/analysis/affineAnalysis.hpp"
#include "../../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../../include/mid/analysis/dependenceAnalysis.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>

namespace {

bool isParDebugEnabled() {
    static bool enabled = std::getenv("DEBUG_PARALLEL") != nullptr;
    return enabled;
}

void debugPar(const std::string &msg) {
    if (isParDebugEnabled())
        std::cerr << "[Parallelize] " << msg << "\n";
}

// GEP 链回溯到基址
Value *gepRootBase(Value *ptr) {
    return ArgumentAliasAnalysis::underlyingObject(ptr);
}

bool isAcceptedMemoryRoot(Value *root) {
    if (dynamic_cast<GlobalVariable *>(root)) return true;
    auto *alloca = dynamic_cast<AllocaInst *>(root);
    if (alloca && alloca->isLoopExpansionScratch())
        return true;
    auto *arg = dynamic_cast<Argument *>(root);
    return arg && dynamic_cast<PointerType *>(arg->type_);
}

std::string valueName(Value *v) {
    return v && !v->name_.empty() ? v->name_ : "<unnamed>";
}

std::string loopName(const Loop &loop) {
    return loop.header ? loop.header->name_ : "<no-header>";
}

template <typename T>
bool containsPtr(const std::vector<T *> &values, T *value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename T>
bool addUniquePtr(std::vector<T *> &values, T *value) {
    if (containsPtr(values, value))
        return false;
    values.push_back(value);
    return true;
}

bool isScalarExpansionScratch(Value *root) {
    auto *alloca = dynamic_cast<AllocaInst *>(root);
    return alloca && alloca->isLoopExpansionScratch();
}

bool rootsNoAlias(Value *a, Value *b, const ArgumentAliasAnalysis &argAA) {
    if (!a || !b || a == b) return false;
    if (dynamic_cast<GlobalVariable *>(a) && dynamic_cast<GlobalVariable *>(b))
        return true;
    if (isScalarExpansionScratch(a) || isScalarExpansionScratch(b))
        return true;
    return argAA.noAlias(a, b);
}

bool hasProvenSafeMemoryRoots(
    const std::vector<Instruction *> &stores,
    const std::vector<Instruction *> &accesses,
    const ArgumentAliasAnalysis &argAA,
    std::string *reason) {
    auto accessRoot = [](Instruction *acc) -> Value * {
        Value *ptr = acc->is_store() ? acc->get_operand(1) : acc->get_operand(0);
        auto *gep = dynamic_cast<GetElementPtrInst *>(ptr);
        return gep ? gepRootBase(gep) : nullptr;
    };

    for (auto *store : stores) {
        Value *storeRoot = accessRoot(store);
        for (auto *acc : accesses) {
            Value *root = accessRoot(acc);
            if (!storeRoot || !root || storeRoot == root) continue;
            if (!rootsNoAlias(storeRoot, root, argAA)) {
                if (reason)
                    *reason = "cannot prove distinct memory roots " +
                              valueName(storeRoot) + " and " +
                              valueName(root);
                return false;
            }
        }
    }
    return true;
}

Value *createModuloPartialMerge(IRStmtBuilder *builder, Module *module,
                                Value *p0, Value *p1, ConstantInt *mod) {
    auto *zero = new ConstantInt(module->int32_ty_, 0);
    auto *p1NonNegative = builder->create_icmp_ge(p1, zero);

    auto *posThreshold = builder->create_isub(mod, p1);
    auto *posWrapped = builder->create_isub(p0, posThreshold);
    auto *posPlain = builder->create_iadd(p0, p1);
    auto *posNeedsWrap = builder->create_icmp_ge(p0, posThreshold);
    auto *posMerged = new SelectInst(posNeedsWrap, posWrapped, posPlain,
                                     builder->get_insert_block());

    auto *negMod = builder->create_isub(zero, mod);
    auto *negThreshold = builder->create_isub(negMod, p1);
    auto *negWrapped = builder->create_isub(p0, negThreshold);
    auto *negPlain = builder->create_iadd(p0, p1);
    auto *negNeedsWrap = builder->create_icmp_le(p0, negThreshold);
    auto *negMerged = new SelectInst(negNeedsWrap, negWrapped, negPlain,
                                     builder->get_insert_block());

    auto *merged = new SelectInst(p1NonNegative, posMerged, negMerged,
                                  builder->get_insert_block());
    return builder->create_isrem(merged, mod);
}

bool definedInLoop(Value *v, const std::set<BasicBlock *> &blocks) {
    auto *inst = dynamic_cast<Instruction *>(v);
    return inst && blocks.count(inst->parent_);
}

void replaceUsesOutsideOutlinedLoop(Value *oldValue, Value *newValue,
                                    const std::set<BasicBlock *> &blocks,
                                    Function *outlinedBody) {
    std::vector<std::pair<Instruction *, unsigned>> fixes;
    for (auto &use : oldValue->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (user && !blocks.count(user->parent_) &&
            (!outlinedBody || user->parent_->parent_ != outlinedBody))
            fixes.push_back({user, use.arg_no_});
    }
    for (auto &fix : fixes)
        fix.first->set_operand(fix.second, newValue);
}

// ScalarExpansion scratch：每个父循环迭代先清零、后累加、再写回。
// 若某个并行循环内完整使用该 scratch，可在 worker 内改成线程私有 alloca。
bool isPrivatizableScratch(Value *root, const std::set<BasicBlock *> &blocks) {
    if (!isScalarExpansionScratch(root)) return false;
    for (auto &use : root->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || !blocks.count(user->parent_)) return false;
    }
    return true;
}

long long typeBytes(Type *ty) {
    if (dynamic_cast<IntegerType *>(ty)) return 4;
    if (ty && ty->tid_ == Type::FloatTyID) return 4;
    if (auto *arr = dynamic_cast<ArrayType *>(ty)) {
        long long elem = typeBytes(arr->contained_);
        return elem < 0 ? -1 : elem * arr->num_elements_;
    }
    if (dynamic_cast<PointerType *>(ty)) return 8;
    return -1;
}

long long scratchBytes(Value *root) {
    auto *alloca = dynamic_cast<AllocaInst *>(root);
    if (!alloca) return -1;
    return typeBytes(alloca->alloca_ty_);
}

Type *scratchAllocaType(Value *root) {
    auto *alloca = dynamic_cast<AllocaInst *>(root);
    return alloca ? alloca->alloca_ty_ : nullptr;
}

bool isAllowedReductionTerm(Value *value, const Loop &loop,
                            PhiInst *accumulator, PhiInst *ivPhi,
                            Instruction *ivNext,
                            std::set<Value *> &visiting) {
    if (!value || value == accumulator)
        return false;
    if (value == ivPhi || value == ivNext)
        return true;
    if (dynamic_cast<Constant *>(value) ||
        dynamic_cast<GlobalVariable *>(value) ||
        dynamic_cast<Argument *>(value) ||
        dynamic_cast<Function *>(value) ||
        dynamic_cast<BasicBlock *>(value))
        return true;

    auto *inst = dynamic_cast<Instruction *>(value);
    if (!inst)
        return false;
    if (!loop.blocks.count(inst->parent_))
        return true;
    if (!visiting.insert(value).second)
        return true;

    auto allOperandsAllowed = [&]() {
        for (unsigned i = 0; i < inst->num_ops_; ++i) {
            Value *op = inst->get_operand(i);
            if (dynamic_cast<BasicBlock *>(op) || dynamic_cast<Function *>(op))
                continue;
            if (!isAllowedReductionTerm(op, loop, accumulator, ivPhi, ivNext,
                                        visiting))
                return false;
        }
        return true;
    };

    bool ok = false;
    if (auto *bin = dynamic_cast<BinaryInst *>(inst)) {
        switch (bin->op_id_) {
        case Instruction::Add:
        case Instruction::Sub:
        case Instruction::Mul:
        case Instruction::Shl:
            ok = allOperandsAllowed();
            break;
        default:
            ok = false;
            break;
        }
    } else if (dynamic_cast<ICmpInst *>(inst)) {
        ok = allOperandsAllowed();
    } else if (auto *load = dynamic_cast<LoadInst *>(inst)) {
        Value *ptr = load->get_operand(0);
        if (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr)) {
            ok = isAcceptedMemoryRoot(gepRootBase(gep));
            for (unsigned i = 1; ok && i < gep->num_ops_; ++i)
                ok = isAllowedReductionTerm(gep->get_operand(i), loop,
                                            accumulator, ivPhi, ivNext,
                                            visiting);
        } else {
            ok = dynamic_cast<GlobalVariable *>(ptr) != nullptr;
        }
    } else if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst)) {
        ok = isAcceptedMemoryRoot(gepRootBase(gep));
        for (unsigned i = 1; ok && i < gep->num_ops_; ++i)
            ok = isAllowedReductionTerm(gep->get_operand(i), loop,
                                        accumulator, ivPhi, ivNext, visiting);
    } else if (auto *call = dynamic_cast<CallInst *>(inst)) {
        auto *callee = dynamic_cast<Function *>(
            call->get_operand(call->num_ops_ - 1));
        ok = callee && callee->hasSemFlag(SemFlag::FnPure);
        for (unsigned i = 0; ok && i + 1 < call->num_ops_; ++i)
            ok = isAllowedReductionTerm(call->get_operand(i), loop,
                                        accumulator, ivPhi, ivNext, visiting);
    } else if (auto *phi = dynamic_cast<PhiInst *>(inst)) {
        ok = true;
        for (unsigned i = 0; ok && i < phi->num_ops_; i += 2)
            ok = isAllowedReductionTerm(phi->get_operand(i), loop,
                                        accumulator, ivPhi, ivNext, visiting);
    } else if (inst->op_id_ == Instruction::Select ||
               inst->op_id_ == Instruction::ZExt) {
        ok = allOperandsAllowed();
    }

    visiting.erase(value);
    return ok;
}

} // namespace

void ParallelizeLoops::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

bool ParallelizeLoops::matchShape(Loop &loop, LoopShape &shape,
                                  std::string *reason) {
    auto fail = [&](const std::string &why) {
        if (reason) *reason = why;
        return false;
    };

    if (!loop.preheader) return fail("missing preheader");
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *exitBlock = loop.singleExit();
    if (!latch) return fail("missing single latch");
    if (!exitBlock) return fail("missing single exit");
    shape.latch = latch;
    shape.exitBlock = exitBlock;

    auto isOne = [](Value *v) {
        auto *c = dynamic_cast<ConstantInt *>(v);
        return c && c->value_ == 1;
    };

    struct IVCandidate {
        PhiInst *phi = nullptr;
        Value *init = nullptr;
        Instruction *next = nullptr;
    };
    std::vector<IVCandidate> ivCandidates;
    for (auto inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        auto *ivTy = dynamic_cast<IntegerType *>(phi->type_);
        if (!ivTy || ivTy->num_bits_ != 32) continue;

        Value *init = nullptr;
        Instruction *next = nullptr;
        bool badPred = false;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred == loop.preheader) {
                init = phi->get_operand(i);
            } else if (pred == latch) {
                next = dynamic_cast<Instruction *>(phi->get_operand(i));
            } else {
                badPred = true;
                break;
            }
        }
        if (badPred || !init || !next || !next->is_add()) continue;
        Value *a = next->get_operand(0), *b = next->get_operand(1);
        if (!((a == phi && isOne(b)) || (b == phi && isOne(a)))) continue;
        ivCandidates.push_back({phi, init, next});
    }

    if (ivCandidates.empty()) return fail("missing i32 +1 IV phi");

    // 出口：header（while 形）或 latch（do-while 形）的 slt 条件分支
    for (auto &candidate : ivCandidates) {
        for (BasicBlock *cand : {loop.header, latch}) {
            auto *term = cand->get_terminator();
            if (!term || term->num_ops_ != 3) continue; // 非 cond br
            auto *cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
            if (!cmp || cmp->icmp_op_ != ICmpInst::ICMP_SLT) continue;
            Value *lhs = cmp->get_operand(0);
            if (lhs != candidate.phi && lhs != candidate.next) continue;
            auto *tSucc = static_cast<BasicBlock *>(term->get_operand(1));
            auto *fSucc = static_cast<BasicBlock *>(term->get_operand(2));
            if (fSucc != exitBlock || !loop.blocks.count(tSucc)) continue;
            shape.ivPhi = candidate.phi;
            shape.init = candidate.init;
            shape.ivNext = candidate.next;
            shape.exitCmp = cmp;
            shape.bound = cmp->get_operand(1);
            shape.exitingBlock = cand;
            shape.latchComparesIV = cand == latch && lhs == candidate.phi;
            break;
        }
        if (shape.exitCmp) break;
    }
    if (!shape.exitCmp) return fail("missing i < bound exit condition");
    if (definedInLoop(shape.bound, loop.blocks))
        return fail("loop bound is defined in loop");

    // 逃逸边检查：循环内所有后继必须仍在循环内，唯一例外是
    // exitingBlock→exitBlock。dedicated exits（plan 1.1）未实现，
    // break 形循环的多条 exiting 边可能汇入同一 exit 块——若放过，
    // 变换只改写一条出口边，其余会留成跨函数分支。
    for (auto *bb : loop.blocksOrdered) {
        auto *term = bb->get_terminator();
        if (!term) return fail("block without terminator");
        for (unsigned i = 0; i < term->num_ops_; i++) {
            auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
            if (!succ || loop.blocks.count(succ)) continue;
            if (bb == shape.exitingBlock && succ == exitBlock) continue;
            return fail("loop has unsupported escape edge");
        }
    }
    return true;
}

bool ParallelizeLoops::isLegalDoall(Loop &loop, const LoopShape &shape,
                                    Function *func, AnalysisManager *AM,
                                    const ArgumentAliasAnalysis &argAA,
                                    std::set<Value *> *privatize,
                                    std::vector<Reduction> *reductions,
                                    std::vector<ScalarReduction> *scalarReductions) {
    auto fail = [&](const std::string &why) {
        debugPar("reject func=" + func->name_ + " loop=" + loopName(loop) +
                 ": " + why);
        return false;
    };

    std::vector<Instruction *> stores, accesses;
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            if (auto *call = dynamic_cast<CallInst *>(inst)) {
                auto *callee = dynamic_cast<Function *>(
                    call->get_operand(call->num_ops_ - 1));
                if (!callee || !callee->hasSemFlag(SemFlag::FnPure))
                    return fail("call in loop");
            }
            if (dynamic_cast<AllocaInst *>(inst)) return fail("alloca in loop");
            if (inst->is_store()) {
                Value *ptr = inst->get_operand(1);
                auto *gep = dynamic_cast<GetElementPtrInst *>(ptr);
                if (!gep) return fail("store target is not GEP");
                Value *root = gepRootBase(gep);
                if (!isAcceptedMemoryRoot(root))
                    return fail("store has unsupported memory root " +
                                valueName(root));
                stores.push_back(inst);
                accesses.push_back(inst);
            } else if (inst->is_load()) {
                Value *ptr = inst->get_operand(0);
                // 标量全局只读 load 允许；GEP 必须全局数组基址
                if (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr)) {
                    Value *root = gepRootBase(gep);
                    if (!isAcceptedMemoryRoot(root))
                        return fail("load has unsupported memory root " +
                                    valueName(root));
                } else if (!dynamic_cast<GlobalVariable *>(ptr)) {
                    return fail("load target is not GEP/global");
                }
                accesses.push_back(inst);
            }
        }
    }

    std::vector<ScalarReduction> localScalarReductions;
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi == shape.ivPhi) continue;

        auto *phiTy = dynamic_cast<IntegerType *>(phi->type_);
        if (!phiTy || phiTy->num_bits_ != 32)
            return fail("unsupported non-IV header phi");

        Value *init = nullptr;
        Value *latchVal = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred == loop.preheader)
                init = phi->get_operand(i);
            else if (pred == shape.latch)
                latchVal = phi->get_operand(i);
            else
                return fail("non-IV phi has unexpected predecessor");
        }

        auto *identity = dynamic_cast<ConstantInt *>(init);
        if (!identity || identity->value_ != 0)
            return fail("non-IV phi is not zero-init scalar reduction");

        auto *rem = dynamic_cast<BinaryInst *>(latchVal);
        ConstantInt *mod = nullptr;
        Value *updateValue = latchVal;
        ScalarModuloSource moduloSource = ScalarModuloSource::None;
        if (rem && rem->op_id_ == Instruction::SRem) {
            mod = dynamic_cast<ConstantInt *>(rem->get_operand(1));
            if (!mod || mod->value_ <= 0)
                return fail("modulo reduction divisor is not positive constant");
            updateValue = rem->get_operand(0);
            moduloSource = ScalarModuloSource::InlineModulo;
        } else {
            rem = nullptr;
        }

        auto *update = dynamic_cast<BinaryInst *>(updateValue);
        if (!update || !(update->is_add() || update->is_sub()))
            return fail("scalar reduction is not add/sub update");

        Value *term = nullptr;
        bool isSub = update->is_sub();
        if (update->is_add()) {
            if (update->get_operand(0) == phi)
                term = update->get_operand(1);
            else if (update->get_operand(1) == phi)
                term = update->get_operand(0);
        } else if (update->get_operand(0) == phi) {
            term = update->get_operand(1);
        }
        if (!term)
            return fail("scalar reduction update does not use accumulator");

        std::set<Value *> visitingTerm;
        if (!isAllowedReductionTerm(term, loop, phi, shape.ivPhi,
                                    shape.ivNext, visitingTerm))
            return fail("unsupported scalar reduction term");

        localScalarReductions.push_back(
            {phi, update, rem, {}, {}, {}, term, mod, identity, isSub,
             moduloSource});
    }

    if (localScalarReductions.size() > 1)
        return fail("multiple scalar reductions");
    if (!localScalarReductions.empty()) {
        if (!stores.empty())
            return fail("mixed scalar reduction and memory stores");
    }
    if (stores.empty() && localScalarReductions.empty())
        return fail("no stores or scalar reductions");

    for (auto &red : localScalarReductions) {
        red.liveOutUpdateValues.push_back(red.update);
        red.liveOutFinalValues.push_back(red.phi);
        if (red.rem) {
            red.liveOutRems.push_back(red.rem);
            red.liveOutFinalValues.push_back(red.rem);
        }
        bool sawRawLiveOutUse = false;
        std::string rawLiveOutUser;
        bool grew = true;
        while (grew) {
            grew = false;
            for (auto *bb : func->basic_blocks_) {
                if (loop.blocks.count(bb)) continue;
                for (auto *user : bb->instr_list_) {
                    bool usesLiveOutUpdate = false;
                    bool usesLiveOutFinal = false;
                    bool usesLiveOutModuloInput = false;
                    for (unsigned i = 0; i < user->num_ops_; ++i) {
                        Value *op = user->get_operand(i);
                        usesLiveOutUpdate |=
                            std::find(red.liveOutUpdateValues.begin(),
                                      red.liveOutUpdateValues.end(),
                                      op) !=
                            red.liveOutUpdateValues.end();
                        usesLiveOutFinal |=
                            std::find(red.liveOutFinalValues.begin(),
                                      red.liveOutFinalValues.end(),
                                      op) !=
                            red.liveOutFinalValues.end();
                        auto *opRem = dynamic_cast<BinaryInst *>(op);
                        if (containsPtr(red.liveOutUpdateValues, op) ||
                            (red.moduloSource !=
                                 ScalarModuloSource::InlineModulo &&
                             containsPtr(red.liveOutFinalValues, op) &&
                             !containsPtr(red.liveOutRems, opRem)))
                            usesLiveOutModuloInput = true;
                    }
                    if (!usesLiveOutUpdate && !usesLiveOutFinal) continue;

                    if (auto *phi = dynamic_cast<PhiInst *>(user)) {
                        bool onlyForwardsReductionUpdate = true;
                        bool onlyForwardsReductionFinal = true;
                        bool hasLoopIncoming = false;
                        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                            auto *pred =
                                dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
                            if (!loop.blocks.count(pred))
                                continue;
                            hasLoopIncoming = true;
                            onlyForwardsReductionUpdate &=
                                containsPtr(red.liveOutUpdateValues,
                                            phi->get_operand(i));
                            onlyForwardsReductionFinal &=
                                containsPtr(red.liveOutFinalValues,
                                            phi->get_operand(i));
                        }
                        if (hasLoopIncoming && !onlyForwardsReductionUpdate &&
                            !onlyForwardsReductionFinal)
                            return fail("scalar reduction update has mixed live-out phi");
                        if (hasLoopIncoming && onlyForwardsReductionUpdate &&
                            addUniquePtr(red.liveOutUpdateValues,
                                         static_cast<Value *>(phi))) {
                            grew = true;
                        }
                        if (hasLoopIncoming && onlyForwardsReductionFinal &&
                            addUniquePtr(red.liveOutFinalValues,
                                         static_cast<Value *>(phi))) {
                            grew = true;
                        }
                        continue;
                    }

                    bool finalMayNeedModulo =
                        red.moduloSource != ScalarModuloSource::InlineModulo;
                    if (!usesLiveOutModuloInput)
                        continue;

                    auto *rem = dynamic_cast<BinaryInst *>(user);
                    auto *remMod =
                        rem ? dynamic_cast<ConstantInt *>(rem->get_operand(1))
                            : nullptr;
                    bool remUsesLiveOutValue =
                        rem && (containsPtr(red.liveOutUpdateValues,
                                            rem->get_operand(0)) ||
                                (finalMayNeedModulo &&
                                 containsPtr(red.liveOutFinalValues,
                                             rem->get_operand(0)) &&
                                 !containsPtr(
                                     red.liveOutRems,
                                     dynamic_cast<BinaryInst *>(
                                         rem->get_operand(0)))));
                    bool isPositiveConstRem =
                        rem && rem->op_id_ == Instruction::SRem &&
                        remUsesLiveOutValue && remMod && remMod->value_ > 0;
                    if (isPositiveConstRem) {
                        if (!red.mod) {
                            red.mod = remMod;
                            red.moduloSource = ScalarModuloSource::LiveOutModulo;
                        } else if (remMod->value_ != red.mod->value_) {
                            return fail("scalar modulo reduction has mixed live-out moduli");
                        }
                        addUniquePtr(red.liveOutRems, rem);
                        addUniquePtr(red.liveOutFinalValues,
                                     static_cast<Value *>(rem));
                        continue;
                    }

                    if (!sawRawLiveOutUse) {
                        sawRawLiveOutUse = true;
                        rawLiveOutUser = valueName(user);
                    }
                }
            }
        }
        if (red.mod && sawRawLiveOutUse) {
            return fail("scalar modulo reduction has mixed modulo and naked live-out use by " +
                        rawLiveOutUser);
        }
        if (red.mod) {
            debugPar("scalar modulo reduction source=" +
                     std::string(red.moduloSource ==
                                         ScalarModuloSource::InlineModulo
                                     ? "inline"
                                     : "liveout"));
        }
    }

    std::string aliasReason;
    if (!hasProvenSafeMemoryRoots(stores, accesses, argAA, &aliasReason))
        return fail(aliasReason);

    // 标量 live-out：循环内定义被循环外使用 → bail
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            for (auto *userBB : func->basic_blocks_) {
                if (loop.blocks.count(userBB)) continue;
                for (auto *user : userBB->instr_list_) {
                    bool usesInst = false;
                    for (unsigned i = 0; i < user->num_ops_; ++i)
                        usesInst |= user->get_operand(i) == inst;
                    if (!usesInst) continue;

                    bool allowedScalarReductionLiveOut = false;
                    for (auto &red : localScalarReductions) {
                        allowedScalarReductionLiveOut |=
                            std::find(red.liveOutFinalValues.begin(),
                                      red.liveOutFinalValues.end(), inst) !=
                            red.liveOutFinalValues.end();
                        if (inst == red.update) {
                            auto *userInst = dynamic_cast<BinaryInst *>(user);
                            if (red.mod) {
                                allowedScalarReductionLiveOut |=
                                    std::find(red.liveOutRems.begin(),
                                              red.liveOutRems.end(), userInst) !=
                                    red.liveOutRems.end();
                            } else {
                                allowedScalarReductionLiveOut = true;
                            }
                            allowedScalarReductionLiveOut |=
                                std::find(red.liveOutUpdateValues.begin(),
                                          red.liveOutUpdateValues.end(), user) !=
                                red.liveOutUpdateValues.end();
                        }
                    }
                    if (!allowedScalarReductionLiveOut)
                        return fail("loop-defined value is used outside loop: " +
                                    valueName(inst) + " by " + valueName(user));
                }
            }
        }
    }

    // live-in 类型限制：i32 和指针可通过 ctx 传递；其它类型仍保守拒绝。
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            for (unsigned i = 0; i < inst->num_ops_; i++) {
                Value *op = inst->get_operand(i);
                if (dynamic_cast<Constant *>(op) ||
                    dynamic_cast<GlobalVariable *>(op) ||
                    dynamic_cast<Function *>(op) ||
                    dynamic_cast<BasicBlock *>(op))
                    continue;
                if (definedInLoop(op, loop.blocks)) continue;
                if (op == shape.init || op == shape.bound) continue; // 形参化
                auto *ity = dynamic_cast<IntegerType *>(op->type_);
                if (ity && ity->num_bits_ == 32) continue;
                if (dynamic_cast<PointerType *>(op->type_)) continue;
                return fail("unsupported live-in type for " + valueName(op));
            }
        }
    }

    // 归约检测：在 store 必须随 IV 变化的硬规则之前，找出归约 store，
    // 以便为其豁免 DIR_EQ 依赖检查。仅当 store/load 所在块的最内层
    // 循环恰为当前循环时才视为本循环归约（避免内层循环的归约误判给外层）。
    std::vector<Reduction> localReductions;
    {
        LoopInfo &LI2 = AM->getLoopInfo(func);
        ScalarEvolution &SE2 = AM->getScalarEvolution(func);
        for (auto *s : stores) {
            // store 所在块的最内层循环必须是当前循环
            if (LI2.getLoopFor(s->parent_) != &loop) continue;
            auto *sVal = s->get_operand(0);
            auto *bin = dynamic_cast<BinaryInst *>(sVal);
            if (!bin || !(bin->is_add() || bin->is_sub() || bin->is_mul()))
                continue;
            Value *sPtr = s->get_operand(1);
            auto *sGep = dynamic_cast<GetElementPtrInst *>(sPtr);
            if (!sGep) continue;
            // 确认 store 地址随本循环 IV 变化
            bool variesWithThisIV = false;
            for (unsigned i = 1; i < sGep->num_ops_; i++) {
                auto *rec = dynamic_cast<const SCEVAddRecExpr *>(
                    SE2.getSCEV(sGep->get_operand(i)));
                if (rec && rec->loop() == &loop) { variesWithThisIV = true; break; }
            }
            if (!variesWithThisIV) continue;
            Value *sRoot = gepRootBase(sGep);
            for (auto *a : accesses) {
                if (!a->is_load()) continue;
                // load 所在块的最内层循环也必须匹配
                if (LI2.getLoopFor(a->parent_) != &loop) continue;
                Value *aPtr = a->get_operand(0);
                auto *aGep = dynamic_cast<GetElementPtrInst *>(aPtr);
                if (!aGep || gepRootBase(aGep) != sRoot) continue;
                bool usesLoad = false;
                for (unsigned i = 0; i < bin->num_ops_; i++)
                    if (bin->get_operand(i) == a) { usesLoad = true; break; }
                if (!usesLoad) continue;
                localReductions.push_back({s, a, sRoot});
                debugPar("reduction detected: store=" + valueName(s) + " load=" +
                         valueName(a));
                break;
            }
        }
        debugPar("found " + std::to_string(localReductions.size()) +
                 " reductions in loop " + loopName(loop));
    }

    // 硬规则：store 地址必须随本循环 IV 变化（某下标是本循环的 AddRec）。
    // 否则两线程会命中同一地址。ScalarExpansion scratch 若完整局限在
    // 当前循环内，则可改成 worker 私有 alloca。
    ScalarEvolution &SE = AM->getScalarEvolution(func);
    long long privBytes = 0;
    for (auto *s : stores) {
        auto *gep = dynamic_cast<GetElementPtrInst *>(s->get_operand(1));
        Value *base = gep ? gepRootBase(gep) : nullptr;

        bool variesWithIV = false;
        for (unsigned i = 1; gep && i < gep->num_ops_; i++) {
            auto *rec = dynamic_cast<const SCEVAddRecExpr *>(
                SE.getSCEV(gep->get_operand(i)));
            if (rec && rec->loop() == &loop) { variesWithIV = true; break; }
        }
        if (!variesWithIV && isPrivatizableScratch(base, loop.blocks)) {
            // 私有 scratch 放 worker 栈（静态 1MB），总量限 64KB 防溢出。
            long long bytes = scratchBytes(base);
            if (bytes < 0) return fail("unknown privatized scratch size");
            if (!privatize->count(base)) {
                privBytes += bytes;
                if (privBytes > 64 * 1024)
                    return fail("privatized scratch exceeds stack budget");
            }
            privatize->insert(base);
            continue;
        }
        if (!variesWithIV)
            return fail("store address does not vary with loop IV");
    }

    // 依赖：每个 (store, access) 对需证明独立或仅同迭代依赖

    LoopInfo &LI = AM->getLoopInfo(func);
    AffineAnalysis AA(LI);
    DependenceAnalysis DA(LI, AA);
    DA.setArgAlias(&argAA);
    auto basePriv = [&](Instruction *acc) {
        Value *ptr = acc->is_store() ? acc->get_operand(1) : acc->get_operand(0);
        return privatize->count(gepRootBase(ptr)) != 0;
    };
    for (auto *s : stores) {
        if (basePriv(s)) continue;
        for (auto *a : accesses) {
            if (basePriv(a)) continue;
            auto r = DA.test(s, a);
            if (r.provably_independent) continue;
            int idx = -1;
            for (size_t i = 0; i < r.commonLoops.size(); i++) {
                if (r.commonLoops[i] == &loop) { idx = (int)i; break; }
            }
            if (idx < 0 || idx >= (int)r.direction.size())
                return fail("dependence direction missing for loop");
            if (r.direction[idx] != DependenceAnalysis::DIR_EQ) {
                // 归约：store→load 跨迭代依赖（DIR_LT）是可结合的
                bool isReduction = false;
                for (auto &red : localReductions) {
                    if ((red.store == s && red.load == a) ||
                        (red.load == s && red.store == a))
                        { isReduction = true; break; }
                }
                if (!isReduction)
                    return fail("loop carries memory dependence");
            }
        }
    }

    // 编译期可知的小 trip count 不值得
    auto *ci = dynamic_cast<ConstantInt *>(shape.init);
    auto *cb = dynamic_cast<ConstantInt *>(shape.bound);
    if (ci && cb && cb->value_ - ci->value_ < 64)
        return fail("constant trip count below parallel threshold");

    if (reductions)
        *reductions = std::move(localReductions);
    if (scalarReductions)
        *scalarReductions = std::move(localScalarReductions);

    return true;
}

void ParallelizeLoops::transform(Loop &loop, const LoopShape &shape,
                                 Function *func, Module *module,
                                 const std::set<Value *> &privatize,
                                 const std::vector<Reduction> &reductions,
                                 const std::vector<ScalarReduction> &scalarReductions) {
    (void)reductions;  // IV-varying reductions need no privatization
    int id = (int)bodies_.size();
    std::vector<ScalarReduction> scalarReds = scalarReductions;

    if (!parallelForDecl_) {
        auto *fty = new FunctionType(
            module->void_ty_,
            {module->int32_ty_, module->int32_ty_, module->int32_ty_});
        parallelForDecl_ = new Function(fty, "__sysy_parallel_for", module);
    }

    auto *bodyTy = new FunctionType(module->void_ty_,
                                    {module->int32_ty_, module->int32_ty_});
    auto *bodyFn = new Function(
        bodyTy, "__sysy_par_body_" + std::to_string(id), module);
    Value *lo = bodyFn->arguments_[0];
    Value *hi = bodyFn->arguments_[1];

    auto *entry = new BasicBlock(module, "label_par_entry", bodyFn);
    auto *retbb = new BasicBlock(module, "label_par_ret", bodyFn);
    auto *builder = new IRStmtBuilder(retbb, module);

    for (auto &red : scalarReds) {
        if (!red.mod || red.moduloSource != ScalarModuloSource::LiveOutModulo)
            continue;
        auto *rem = new BinaryInst(module->int32_ty_, Instruction::SRem,
                                   red.update, red.mod, shape.latch, true);
        shape.latch->add_instruction_before_terminator(rem);
        red.rem = rem;
        for (unsigned i = 0; i < red.phi->num_ops_; i += 2) {
            if (red.phi->get_operand(i + 1) ==
                static_cast<Value *>(shape.latch)) {
                red.phi->set_operand(i, rem);
                break;
            }
        }
    }

    // live-in 收集（与 isLegalDoall 同口径）→ ctx 全局
    std::vector<Value *> liveIns;
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            for (unsigned i = 0; i < inst->num_ops_; i++) {
                Value *op = inst->get_operand(i);
                if (dynamic_cast<Constant *>(op) ||
                    dynamic_cast<GlobalVariable *>(op) ||
                    dynamic_cast<Function *>(op) ||
                    dynamic_cast<BasicBlock *>(op))
                    continue;
                if (definedInLoop(op, loop.blocks)) continue;
                if (inst == shape.ivPhi && op == shape.init) continue;
                if (inst == shape.exitCmp && op == shape.bound) continue;
                if (privatize.count(op)) continue;
                if (std::find(liveIns.begin(), liveIns.end(), op) ==
                    liveIns.end())
                    liveIns.push_back(op);
            }
        }
    }
    std::vector<GlobalVariable *> ctxSlots;
    builder->set_insert_point(entry);
    // scratch 私有化：外提体内用栈分配替换原 entry alloca（每线程一份）。
    for (auto *scratch : privatize) {
        Type *allocTy = scratchAllocaType(scratch);
        if (!allocTy) continue;
        auto *priv = builder->create_alloca(allocTy);
        std::vector<std::pair<Instruction *, unsigned>> fixes;
        for (auto &use : scratch->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && loop.blocks.count(user->parent_))
                fixes.push_back({user, use.arg_no_});
        }
        for (auto &f : fixes)
            f.first->set_operand(f.second, priv);
    }
    std::vector<Value *> ctxLoads;
    for (size_t k = 0; k < liveIns.size(); k++) {
        auto *gv = new GlobalVariable(
            "__sysy_par_ctx_" + std::to_string(id) + "_" + std::to_string(k),
            module, liveIns[k]->type_, false,
            new ConstantZero(liveIns[k]->type_));
        ctxSlots.push_back(gv);
        ctxLoads.push_back(builder->create_load(gv));
    }

    GlobalVariable *scalarStartSlot = nullptr;
    GlobalVariable *scalarBoundSlot = nullptr;
    GlobalVariable *scalarPartial0 = nullptr;
    GlobalVariable *scalarPartial1 = nullptr;
    Value *scalarBodyBound = hi;
    Value *scalarSlotPtr = nullptr;
    if (!scalarReds.empty()) {
        scalarStartSlot = new GlobalVariable(
            "__sysy_par_scalar_start_" + std::to_string(id),
            module, module->int32_ty_, false,
            new ConstantZero(module->int32_ty_));
        scalarBoundSlot = new GlobalVariable(
            "__sysy_par_scalar_bound_" + std::to_string(id),
            module, module->int32_ty_, false,
            new ConstantZero(module->int32_ty_));
        scalarPartial0 = new GlobalVariable(
            "__sysy_par_scalar_partial_" + std::to_string(id) + "_0",
            module, module->int32_ty_, false,
            new ConstantZero(module->int32_ty_));
        scalarPartial1 = new GlobalVariable(
            "__sysy_par_scalar_partial_" + std::to_string(id) + "_1",
            module, module->int32_ty_, false,
            new ConstantZero(module->int32_ty_));

        auto *ctxStart = builder->create_load(scalarStartSlot);
        auto *isFirstChunk = builder->create_icmp_eq(lo, ctxStart);
        if (shape.latchComparesIV) {
            auto *ctxBound = builder->create_load(scalarBoundSlot);
            auto *isWholeRange = builder->create_icmp_eq(hi, ctxBound);
            auto *isSplitFirst = builder->create_icmp_ne(
                isWholeRange, new ConstantInt(module->int1_ty_, 1));
            auto *needsTrim = new BinaryInst(module->int1_ty_, Instruction::And,
                                             isFirstChunk, isSplitFirst, entry);
            auto *trimmedHi = builder->create_isub(
                hi, new ConstantInt(module->int32_ty_, 1));
            scalarBodyBound = new SelectInst(needsTrim, trimmedHi, hi, entry);
        }
        scalarSlotPtr = new SelectInst(isFirstChunk, scalarPartial0,
                                       scalarPartial1, entry);
    }
    builder->create_br(loop.header);
    entry->add_succ_basic_block(loop.header);

    // 环内对 live-in 的引用 → ctx load
    for (size_t k = 0; k < liveIns.size(); k++) {
        Value *v = liveIns[k];
        std::vector<std::pair<Instruction *, unsigned>> fixes;
        for (auto &use : v->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && loop.blocks.count(user->parent_))
                fixes.push_back({user, use.arg_no_});
        }
        for (auto &f : fixes)
            f.first->set_operand(f.second, ctxLoads[k]);
    }

    // IV init → lo（入边块 preheader → entry）；出口比较 bound → hi
    for (unsigned i = 0; i < shape.ivPhi->num_ops_; i += 2) {
        if (shape.ivPhi->get_operand(i + 1) ==
            static_cast<Value *>(loop.preheader)) {
            shape.ivPhi->set_operand(i, lo);
            shape.ivPhi->set_operand(i + 1, entry);
        }
    }
    for (auto &red : scalarReds) {
        for (unsigned i = 0; i < red.phi->num_ops_; i += 2) {
            if (red.phi->get_operand(i + 1) ==
                static_cast<Value *>(loop.preheader))
                red.phi->set_operand(i + 1, entry);
        }
    }
    shape.exitCmp->set_operand(1, scalarBodyBound);

    builder->set_insert_point(retbb);
    for (auto &red : scalarReds) {
        Value *partial = nullptr;
        if (red.mod) {
            partial = shape.exitingBlock == loop.header
                          ? static_cast<Value *>(red.phi)
                          : static_cast<Value *>(red.rem);
        } else {
            partial = shape.exitingBlock == loop.header
                          ? static_cast<Value *>(red.phi)
                          : static_cast<Value *>(red.update);
        }
        builder->create_store(partial, scalarSlotPtr);
    }
    builder->create_void_ret();

    // 循环块迁移到 bodyFn
    for (auto *bb : loop.blocksOrdered) {
        auto &bbs = func->basic_blocks_;
        bbs.erase(std::remove(bbs.begin(), bbs.end(), bb), bbs.end());
        bb->parent_ = bodyFn;
        bodyFn->basic_blocks_.push_back(bb);
    }
    loop.header->remove_pre_basic_block(loop.preheader);
    loop.header->add_pre_basic_block(entry);

    // 出口边 → ret 块
    auto *exitTerm = shape.exitingBlock->get_terminator();
    for (unsigned i = 0; i < exitTerm->num_ops_; i++) {
        if (exitTerm->get_operand(i) == static_cast<Value *>(shape.exitBlock))
            exitTerm->set_operand(i, retbb);
    }
    shape.exitingBlock->remove_succ_basic_block(shape.exitBlock);
    shape.exitingBlock->add_succ_basic_block(retbb);
    retbb->add_pre_basic_block(shape.exitingBlock);
    shape.exitBlock->remove_pre_basic_block(shape.exitingBlock);

    // 调用点：preheader 可能是双后继 guard，不动它——新建 par_call 块
    auto *parCall = new BasicBlock(module, "label_par_call", func);
    builder->set_insert_point(parCall);
    for (size_t k = 0; k < liveIns.size(); k++)
        builder->create_store(liveIns[k], ctxSlots[k]);
    for (auto &red : scalarReds) {
        builder->create_store(shape.init, scalarStartSlot);
        builder->create_store(shape.bound, scalarBoundSlot);
        builder->create_store(red.identity, scalarPartial0);
        builder->create_store(red.identity, scalarPartial1);
    }
    builder->create_call(parallelForDecl_,
                         {new ConstantInt(module->int32_ty_, id), shape.init,
                          shape.bound});
    for (auto &red : scalarReds) {
        auto *p0 = builder->create_load(scalarPartial0);
        auto *p1 = builder->create_load(scalarPartial1);
        Value *merged = red.mod
                            ? createModuloPartialMerge(builder, module, p0, p1,
                                                       red.mod)
                            : static_cast<Value *>(builder->create_iadd(p0, p1));
        std::vector<Value *> exitForwardValues = red.liveOutUpdateValues;
        for (auto *value : red.liveOutFinalValues) {
            if (std::find(exitForwardValues.begin(), exitForwardValues.end(),
                          value) == exitForwardValues.end())
                exitForwardValues.push_back(value);
        }
        for (auto *value : exitForwardValues) {
            auto *phi = dynamic_cast<PhiInst *>(value);
            if (!phi || phi->parent_ != shape.exitBlock) continue;
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                if (phi->get_operand(i + 1) !=
                    static_cast<Value *>(shape.exitingBlock))
                    continue;
                phi->set_operand(i, merged);
                phi->set_operand(i + 1, parCall);
            }
        }
        if (red.mod) {
            for (auto *value : red.liveOutFinalValues)
                replaceUsesOutsideOutlinedLoop(value, merged, loop.blocks, bodyFn);
        } else {
            for (auto *value : red.liveOutUpdateValues)
                replaceUsesOutsideOutlinedLoop(value, merged, loop.blocks, bodyFn);
            for (auto *value : red.liveOutFinalValues)
                replaceUsesOutsideOutlinedLoop(value, merged, loop.blocks, bodyFn);
        }
    }
    builder->create_br(shape.exitBlock);
    parCall->add_succ_basic_block(shape.exitBlock);
    shape.exitBlock->add_pre_basic_block(parCall);

    auto *preTerm = loop.preheader->get_terminator();
    for (unsigned i = 0; i < preTerm->num_ops_; i++) {
        if (preTerm->get_operand(i) == static_cast<Value *>(loop.header))
            preTerm->set_operand(i, parCall);
    }
    loop.preheader->remove_succ_basic_block(loop.header);
    loop.preheader->add_succ_basic_block(parCall);
    parCall->add_pre_basic_block(loop.preheader);

    delete builder;
    bodies_.push_back(bodyFn);
    debugPar("parallelized func=" + func->name_ +
             " header=" + loop.header->name_ + " id=" + std::to_string(id));
}

PreservedAnalyses ParallelizeLoops::execute(Module *module,
                                            AnalysisManager &AM) {
    bodies_.clear();
    parallelForDecl_ = nullptr;

    ArgumentAliasAnalysis argAA;
    argAA.analyze(module);

    std::vector<Function *> funcs;
    for (auto f : module->function_list_)
        if (!f->is_declaration()) funcs.push_back(f);

    // 调试钩子：PAR_LIMIT=N 只并行化前 N 个循环（错例二分定位用，
    // 与 DEBUG_PARALLEL 配合；缺省上限 32）
    size_t parLimit = 32;
    if (const char *lim = std::getenv("PAR_LIMIT"))
        parLimit = (size_t)atoi(lim);

    bool changed = false;
    for (auto *func : funcs) {
        // 每个函数最多反复尝试（变换后 LoopInfo 失效需重查）
        bool localChanged = true;
        while (localChanged && bodies_.size() < parLimit) {
            localChanged = false;
            LoopInfo &LI = AM.getLoopInfo(func);
            for (auto &lptr : LI.allLoops()) {
                Loop *loop = lptr.get();
                LoopShape shape;
                std::string shapeReason;
                if (!matchShape(*loop, shape, &shapeReason)) {
                    debugPar("reject func=" + func->name_ + " loop=" +
                             loopName(*loop) + ": " + shapeReason);
                    continue;
                }
                std::set<Value *> privatize;
                std::vector<Reduction> reductions;
                std::vector<ScalarReduction> scalarReductions;
                if (!isLegalDoall(*loop, shape, func, &AM, argAA, &privatize,
                                   &reductions, &scalarReductions))
                    continue;
                // A nested leaf loop would invoke the persistent-worker
                // protocol once per parent iteration.  Besides overwhelming
                // useful work with synchronization, rapidly publishing live-in
                // contexts is a poor fit for this whole-region outliner.  Keep
                // the existing reduction case, and otherwise require a nested
                // loop *nest*: its child-loop work amortizes one dispatch and
                // the complete region has already passed the same dependence
                // and live-out checks as a top-level DOALL loop.
                if (loop->depth != 0 && reductions.empty() &&
                    scalarReductions.empty() &&
                    loop->children.empty()) {
                    debugPar("skip func=" + func->name_ + " loop=" +
                             loopName(*loop) +
                             ": nested leaf loop without reduction");
                    continue;
                }
                transform(*loop, shape, func, module, privatize, reductions,
                          scalarReductions);
                AM.clear(func);
                changed = true;
                localChanged = true;
                break;
            }
        }
    }

    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
