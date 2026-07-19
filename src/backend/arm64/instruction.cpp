#include "../../include/backend/arm64/context.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/backend/arm64/magicNumber.hpp"
#include "../../include/mid/analysis/valueFacts.hpp"
#include "../../include/mid/ir/ir.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace {

struct LaneLoadPackPattern {
    std::array<LoadInst*, 4> loads{};
    std::array<InsertElementInst*, 4> inserts{};
};

struct AddvReductionExitPattern {
    StoreInst *store = nullptr;
    Bitcast *spillBitcast = nullptr;
    std::array<GetElementPtrInst*, 3> laneGeps{};
    std::array<LoadInst*, 4> laneLoads{};
    std::array<BinaryInst*, 3> adds{};
    BinaryInst *finalAdd = nullptr;
};

struct VectorMlaMlsPattern {
    BinaryInst *root = nullptr;
    BinaryInst *mul = nullptr;
    Value *acc = nullptr;
    Value *lhs = nullptr;
    Value *rhs = nullptr;
    LoadInst *accLoad = nullptr;
    LoadInst *lhsLoad = nullptr;
    LoadInst *rhsLoad = nullptr;
    const char *opcode = nullptr;
};

static bool isI32Vector4(Type *ty) {
    if (!ty || ty->tid_ != Type::VectorTyID) return false;
    auto *vecTy = static_cast<VectorType *>(ty);
    return vecTy->num_elements_ == 4 &&
           vecTy->contained_->tid_ == Type::IntegerTyID;
}

static bool isF32Vector4(Type *ty) {
    if (!ty || ty->tid_ != Type::VectorTyID) return false;
    auto *vecTy = static_cast<VectorType *>(ty);
    return vecTy->num_elements_ == 4 &&
           vecTy->contained_->tid_ == Type::FloatTyID;
}

static bool isMlaMlsVector4(Type *ty) {
    return isI32Vector4(ty) || isF32Vector4(ty);
}

static bool enableFusedFloatVectorMlaMls() {
    return std::getenv("SYSY_ENABLE_FP_CONTRACT") != nullptr; //默认禁用，需要fast-math或FP contraction以避免浮点精度差异
}

static ConstantInt *getConstIntOperand(Value *v) {
    return dynamic_cast<ConstantInt *>(v);
}


static bool isLoadI32InBlock(Value *v, BasicBlock *bb, LoadInst *&load) {
    load = dynamic_cast<LoadInst *>(v);
    return load && load->parent_ == bb && load->type_->tid_ == Type::IntegerTyID;
}

static bool matchLaneLoadPack(Instruction *inst, LaneLoadPackPattern &pattern) {
    auto *ins3 = dynamic_cast<InsertElementInst *>(inst);
    if (!ins3 || !isI32Vector4(ins3->type_)) return false;
    auto *bb = ins3->parent_;
    if (!bb) return false;

    auto *idx3 = getConstIntOperand(ins3->get_operand(2));
    if (!idx3 || idx3->value_ != 3) return false;

    auto walkLane = [&](InsertElementInst *ins, int lane, InsertElementInst *&prevIns,
                        LoadInst *&load) -> bool {
        auto *idx = getConstIntOperand(ins->get_operand(2));
        if (!idx || idx->value_ != lane) return false;
        if (!isLoadI32InBlock(ins->get_operand(1), bb, load)) return false;
        prevIns = dynamic_cast<InsertElementInst *>(ins->get_operand(0));
        return true;
    };

    pattern.inserts[3] = ins3;
    InsertElementInst *ins2 = nullptr;
    if (!walkLane(ins3, 3, ins2, pattern.loads[3])) return false;
    if (!ins2 || ins2->parent_ != bb) return false;

    pattern.inserts[2] = ins2;
    InsertElementInst *ins1 = nullptr;
    if (!walkLane(ins2, 2, ins1, pattern.loads[2])) return false;
    if (!ins1 || ins1->parent_ != bb) return false;

    pattern.inserts[1] = ins1;
    InsertElementInst *ins0 = nullptr;
    if (!walkLane(ins1, 1, ins0, pattern.loads[1])) return false;
    if (!ins0 || ins0->parent_ != bb) return false;

    pattern.inserts[0] = ins0;
    auto *idx0 = getConstIntOperand(ins0->get_operand(2));
    if (!idx0 || idx0->value_ != 0) return false;
    if (!isLoadI32InBlock(ins0->get_operand(1), bb, pattern.loads[0])) return false;

    return true;
}

static bool matchReductionExitAddv(StoreInst *store,
                                   AddvReductionExitPattern &pattern) {
    auto *vecTy = store->get_operand(0)->type_;
    if (!isI32Vector4(vecTy)) return false;

    auto *bb = store->parent_;
    if (!bb) return false;

    auto nextInst = [&](Instruction *cur) -> Instruction * {
        auto it = std::find(bb->instr_list_.begin(), bb->instr_list_.end(), cur);
        if (it == bb->instr_list_.end()) return nullptr;
        ++it;
        while (it != bb->instr_list_.end() && (*it)->is_phi()) ++it;
        return it == bb->instr_list_.end() ? nullptr : *it;
    };

    auto *bc = dynamic_cast<Bitcast *>(nextInst(store));
    if (!bc || bc->get_operand(0) != store->get_operand(1)) return false;
    if (!bc->dest_ty_ || bc->dest_ty_->tid_ != Type::PointerTyID) return false;
    if (static_cast<PointerType *>(bc->dest_ty_)->contained_->tid_ != Type::IntegerTyID)
        return false;

    auto *load0 = dynamic_cast<LoadInst *>(nextInst(bc));
    if (!load0 || load0->get_operand(0) != bc) return false;
    auto *gep1 = dynamic_cast<GetElementPtrInst *>(nextInst(load0));
    auto *load1 = gep1 ? dynamic_cast<LoadInst *>(nextInst(gep1)) : nullptr;
    auto *gep2 = load1 ? dynamic_cast<GetElementPtrInst *>(nextInst(load1)) : nullptr;
    auto *load2 = gep2 ? dynamic_cast<LoadInst *>(nextInst(gep2)) : nullptr;
    auto *gep3 = load2 ? dynamic_cast<GetElementPtrInst *>(nextInst(load2)) : nullptr;
    auto *load3 = gep3 ? dynamic_cast<LoadInst *>(nextInst(gep3)) : nullptr;
    auto *add01 = load3 ? dynamic_cast<BinaryInst *>(nextInst(load3)) : nullptr;
    auto *add012 = add01 ? dynamic_cast<BinaryInst *>(nextInst(add01)) : nullptr;
    auto *add0123 = add012 ? dynamic_cast<BinaryInst *>(nextInst(add012)) : nullptr;
    if (!load1 || !load2 || !load3 || !add01 || !add012 || !add0123) return false;

    auto matchLaneGep = [&](GetElementPtrInst *gep, int lane) -> bool {
        if (!gep || gep->get_operand(0) != bc || gep->num_ops_ != 2) return false;
        auto *idx = getConstIntOperand(gep->get_operand(1));
        return idx && idx->value_ == lane;
    };
    if (!matchLaneGep(gep1, 1) || !matchLaneGep(gep2, 2) || !matchLaneGep(gep3, 3))
        return false;
    if (load1->get_operand(0) != gep1 || load2->get_operand(0) != gep2 ||
        load3->get_operand(0) != gep3)
        return false;

    auto matchAdd = [](BinaryInst *add, Value *lhs, Value *rhs) {
        return add && add->is_add() &&
               ((add->get_operand(0) == lhs && add->get_operand(1) == rhs) ||
                (add->get_operand(0) == rhs && add->get_operand(1) == lhs));
    };
    if (!matchAdd(add01, load0, load1)) return false;
    if (!matchAdd(add012, add01, load2)) return false;
    if (!matchAdd(add0123, add012, load3)) return false;

    if (load0->use_list_.size() != 1 || load1->use_list_.size() != 1 ||
        load2->use_list_.size() != 1 || load3->use_list_.size() != 1 ||
        gep1->use_list_.size() != 1 || gep2->use_list_.size() != 1 ||
        gep3->use_list_.size() != 1 || add01->use_list_.size() != 1 ||
        add012->use_list_.size() != 1)
        return false;

    pattern.store = store;
    pattern.spillBitcast = bc;
    pattern.laneGeps = {gep1, gep2, gep3};
    pattern.laneLoads = {load0, load1, load2, load3};
    pattern.adds = {add01, add012, add0123};
    pattern.finalAdd = add0123;
    return true;
}

static bool matchVectorMlaMls(Instruction *inst, VectorMlaMlsPattern &pattern) {
    auto *root = dynamic_cast<BinaryInst *>(inst);
    if (!root || !isMlaMlsVector4(root->type_)) return false;

    const bool isFloat = isF32Vector4(root->type_);
    if (isFloat && !enableFusedFloatVectorMlaMls())
        return false;

    const bool isAdd = isFloat ? root->is_fadd() : root->is_add();
    const bool isSub = isFloat ? root->is_fsub() : root->is_sub();
    if (!isAdd && !isSub) return false;

    auto *bb = root->parent_;
    if (!bb) return false;

    auto tryMatch = [&](Value *acc, Value *mulVal, const char *opcode) {
        auto *mul = dynamic_cast<BinaryInst *>(mulVal);
        if (!mul || mul->parent_ != bb || mul->type_ != root->type_)
            return false;
        if (isFloat) {
            if (!mul->is_fmul()) return false;
        } else {
            if (!mul->is_mul()) return false;
        }
        if (mul->use_list_.size() != 1) return false;
        if (acc->type_ != root->type_) return false;

        LoadInst *accLoad = nullptr;
        LoadInst *lhsLoad = nullptr;
        LoadInst *rhsLoad = nullptr;
        if (isFloat || std::string(opcode) == "mla") {
            auto captureLoad = [&](Value *val, LoadInst *&load) {
                load = dynamic_cast<LoadInst *>(val);
                if (!load) return true;
                if (load->parent_ != bb || load->type_ != root->type_) return false;
                return load->use_list_.size() == 1;
            };
            if (!captureLoad(acc, accLoad) ||
                !captureLoad(mul->get_operand(0), lhsLoad) ||
                !captureLoad(mul->get_operand(1), rhsLoad))
                return false;
        }

        pattern.root = root;
        pattern.mul = mul;
        pattern.acc = acc;
        pattern.lhs = mul->get_operand(0);
        pattern.rhs = mul->get_operand(1);
        pattern.accLoad = accLoad;
        pattern.lhsLoad = lhsLoad;
        pattern.rhsLoad = rhsLoad;
        pattern.opcode = opcode;
        return true;
    };

    if (isSub)
        return tryMatch(root->get_operand(0), root->get_operand(1),
                        isFloat ? "fmls" : "mls");

    return tryMatch(root->get_operand(0), root->get_operand(1),
                    isFloat ? "fmla" : "mla") ||
           tryMatch(root->get_operand(1), root->get_operand(0),
                    isFloat ? "fmla" : "mla");
}

} // namespace

namespace {

InsertElementInst *singleInsertElementUser(Instruction *inst) {
    if (!inst || inst->use_list_.size() != 1) return nullptr;
    return dynamic_cast<InsertElementInst*>(inst->use_list_.front().val_);
}

bool isCompleteIntSplatInsertChain(Instruction *inst, Value *&value) {
    auto *insert = dynamic_cast<InsertElementInst*>(inst);
    if (!insert) return false;

    auto *vecTy = dynamic_cast<VectorType*>(insert->type_);
    if (!vecTy || vecTy->num_elements_ != 4 ||
        vecTy->contained_->tid_ != Type::IntegerTyID) {
        return false;
    }

    bool seen[4] = {false, false, false, false};
    Value *commonValue = nullptr;

    while (insert) {
        auto *idx = dynamic_cast<ConstantInt*>(insert->get_operand(2));
        Value *elem = insert->get_operand(1);
        if (!idx || !elem || elem->type_->tid_ != Type::IntegerTyID ||
            idx->value_ < 0 || idx->value_ >= 4)
            return false;
        if (seen[idx->value_])
            return false;
        seen[idx->value_] = true;

        if (!commonValue) {
            commonValue = elem;
        } else if (commonValue != elem) {
            auto *commonConst = dynamic_cast<ConstantInt*>(commonValue);
            auto *elemConst = dynamic_cast<ConstantInt*>(elem);
            if (!commonConst || !elemConst || commonConst->value_ != elemConst->value_)
                return false;
        }

        auto *baseInst = dynamic_cast<InsertElementInst*>(insert->get_operand(0));
        if (!baseInst) break;
        insert = baseInst;
    }

    for (bool laneSeen : seen)
        if (!laneSeen) return false;

    value = commonValue;
    return true;
}

bool canUseMoviSplat(int value) {
    return value == 0 || (value > 0 && value <= 255);
}

bool isSplatInsertChainIntermediate(Instruction *inst) {
    auto *user = singleInsertElementUser(inst);
    if (!user) return false;

    Instruction *terminal = user;
    while (auto *next = singleInsertElementUser(terminal))
        terminal = next;

    Value *value = nullptr;
    return isCompleteIntSplatInsertChain(terminal, value);
}

} // namespace

void Arm64FuncContext::emitBlock(BasicBlock *bb) {
    if (blockSkipped_.count(bb)) return;

    // Lazily collect branch targets to detect dead entry-block labels.
    if (branchTargets_.empty() && !func_->basic_blocks_.empty()) {
        for (auto b : func_->basic_blocks_) {
            auto term = b->get_terminator();
            if (!term || !term->is_br()) continue;
            for (unsigned i = 0; i < term->num_ops_; ++i) {
                if (auto *tgt = dynamic_cast<BasicBlock*>(term->get_operand(i)))
                    branchTargets_.insert(tgt);
            }
        }
    }

    // Emit block label only if this block is a branch target or not the entry block.
    // The entry block is reached via the function name, not its label.
    bool isEntry = (bb == func_->basic_blocks_[0]);
    if (!isEntry || branchTargets_.count(bb))
        emitMachineLine(bbLabel(func_, bb) + ":");

    resetRegs();

    auto &instrs = bb->instr_list_;
    std::map<Instruction *, LaneLoadPackPattern> laneLoadPacks;
    std::set<Instruction *> laneLoadSkipped;
    std::map<Instruction *, AddvReductionExitPattern> addvReductions;
    std::set<Instruction *> addvSkipped;
    std::map<Instruction *, VectorMlaMlsPattern> vectorMlaMlsRoots;
    std::set<Instruction *> vectorMlaMlsSkipped;

    for (auto *candidate : instrs) {
        LaneLoadPackPattern pack;
        if (!matchLaneLoadPack(candidate, pack)) continue;
        laneLoadPacks[candidate] = pack;
        for (auto *load : pack.loads) laneLoadSkipped.insert(load);
        laneLoadSkipped.insert(pack.inserts[0]);
        laneLoadSkipped.insert(pack.inserts[1]);
        laneLoadSkipped.insert(pack.inserts[2]);
    }

    for (auto *candidate : instrs) {
        auto *store = dynamic_cast<StoreInst *>(candidate);
        if (!store) continue;
        AddvReductionExitPattern pattern;
        if (!matchReductionExitAddv(store, pattern)) continue;
        addvReductions[candidate] = pattern;
        addvSkipped.insert(pattern.spillBitcast);
        for (auto *gep : pattern.laneGeps) addvSkipped.insert(gep);
        for (auto *load : pattern.laneLoads) addvSkipped.insert(load);
        for (auto *add : pattern.adds) addvSkipped.insert(add);
    }

    for (auto *candidate : instrs) {
        VectorMlaMlsPattern pattern;
        if (!matchVectorMlaMls(candidate, pattern)) continue;
        vectorMlaMlsRoots[candidate] = pattern;
        vectorMlaMlsSkipped.insert(pattern.mul);
        if (pattern.accLoad) vectorMlaMlsSkipped.insert(pattern.accLoad);
        if (pattern.lhsLoad) vectorMlaMlsSkipped.insert(pattern.lhsLoad);
        if (pattern.rhsLoad) vectorMlaMlsSkipped.insert(pattern.rhsLoad);
    }

    for (auto it = instrs.begin(); it != instrs.end(); ++it) {
        auto inst = *it;
        if (inst->is_phi()) continue;
        if (laneLoadSkipped.count(inst) || addvSkipped.count(inst) ||
            vectorMlaMlsSkipped.count(inst))
            continue;

        auto lanePackIt = laneLoadPacks.find(inst);
        if (lanePackIt != laneLoadPacks.end()) {
            const auto &pack = lanePackIt->second;
            std::function<void(Value *, std::set<Instruction *> &)> rematAddrValue =
                [&](Value *val, std::set<Instruction *> &visited) {
                    auto *def = dynamic_cast<Instruction *>(val);
                    if (!def || def->parent_ != bb || def->is_phi()) return;
                    if (!visited.insert(def).second) return;
                    if (!def->is_add() && !def->is_sub() && !def->is_gep() &&
                        def->op_id_ != Instruction::BitCast)
                        return;
                    for (unsigned i = 0; i < def->num_ops_; ++i)
                        rematAddrValue(def->get_operand(i), visited);
                    emitInstruction(def);
                };
            std::string vd =
                hasAssignedReg(pack.inserts[3]) ? assignedReg(pack.inserts[3]) : allocNEONReg();
            for (int lane = 0; lane < 4; ++lane) {
                std::set<Instruction *> rematVisited;
                Value *ptrVal = pack.loads[lane]->get_operand(0);
                rematAddrValue(ptrVal, rematVisited);
                std::string addr = loadAddr(ptrVal);
                MachineInstr ld = MachineInstr::make(
                    "\tld1 {" + vd + ".s}[" + std::to_string(lane) + "], [" + addr + "]",
                    MOpcode::Load, {vd},
                    lane == 0 ? std::initializer_list<std::string>{addr}
                              : std::initializer_list<std::string>{vd, addr},
                    4);
                ld.mayLoad = true;
                emitMachineInstr(std::move(ld));
            }
            if (!hasAssignedReg(pack.inserts[3])) storeVector(pack.inserts[3], vd);
            continue;
        }

        auto addvIt = addvReductions.find(inst);
        if (addvIt != addvReductions.end()) {
            const auto &pattern = addvIt->second;
            std::string srcVec = loadVector(pattern.store->get_operand(0));
            std::string vTmp = allocNEONReg();
            std::string sTmp = "s" + vTmp.substr(1);
            emitRawAluMachine("\taddv " + sTmp + ", " + srcVec + ".4s",
                              sTmp, {srcVec}, MOpcode::Neon, 4);
            std::string wDst =
                hasAssignedReg(pattern.finalAdd) ? assignedReg(pattern.finalAdd)
                                                 : allocIntReg();
            emitMoveMachine(wDst, sTmp, "fmov");
            storeInt(pattern.finalAdd, wDst);
            continue;
        }

        auto mlaIt = vectorMlaMlsRoots.find(inst);
        if (mlaIt != vectorMlaMlsRoots.end()) {
            const auto &pattern = mlaIt->second;
            resetRegs();

            std::string rd = hasAssignedReg(pattern.root) ? assignedReg(pattern.root)
                                                          : allocNEONReg();
            if (hasAssignedReg(pattern.root) && rd.size() >= 2 && rd[0] == 'v')
                usedNEONRegs_.insert(std::stoi(rd.substr(1)));

            auto emitVectorLoadInto = [&](LoadInst *load, const std::string &vd) {
                std::string addr = loadAddr(load->get_operand(0));
                MachineInstr ld = MachineInstr::make(
                    "\tld1 {" + vd + ".4s}, [" + addr + "]",
                    MOpcode::Load, {vd}, {addr}, 4);
                ld.mayLoad = true;
                emitMachineInstr(std::move(ld));
            };
            auto pinMulOperand = [&](Value *val, LoadInst *load) {
                if (load) {
                    std::string tmp = allocNEONReg();
                    emitVectorLoadInto(load, tmp);
                    return tmp;
                }
                std::string reg = loadVector(val);

                // An allocated SSA source is already pinned for the duration
                // of this instruction.  Reuse it directly unless it aliases
                // the destructive accumulator destination; the interference
                // graph normally prevents that alias, while the explicit
                // check keeps instruction selection correct independently of
                // allocator details.
                if (hasAssignedReg(val) && reg != rd)
                    return reg;

                std::string tmp = allocNEONReg();
                emitRawAluMachine("\tmov " + tmp + ".16b, " + reg + ".16b",
                                  tmp, {reg}, MOpcode::Neon);
                return tmp;
            };
            std::string lhsReg = pinMulOperand(pattern.lhs, pattern.lhsLoad);
            std::string rhsReg = pinMulOperand(pattern.rhs, pattern.rhsLoad);

            if (pattern.accLoad) {
                emitVectorLoadInto(pattern.accLoad, rd);
            } else {
                std::string accReg = loadVector(pattern.acc);
                if (rd != accReg) {
                    emitRawAluMachine("\tmov " + rd + ".16b, " + accReg + ".16b",
                                      rd, {accReg}, MOpcode::Neon);
                }
            }

            emitRawAluMachine("\t" + std::string(pattern.opcode) + " " + rd + ".4s, " +
                                  lhsReg + ".4s, " + rhsReg + ".4s",
                              rd, {rd, lhsReg, rhsReg}, MOpcode::Neon, 3);
            if (!hasAssignedReg(pattern.root)) storeVector(pattern.root, rd);
            continue;
        }

        // Skip compare if its only user is a Select (Select emits its own cmp)
        if ((inst->op_id_ == Instruction::ICmp || inst->op_id_ == Instruction::FCmp) &&
            inst->use_list_.size() == 1) {
            auto *user = dynamic_cast<SelectInst*>((*inst->use_list_.begin()).val_);
            if (user) continue; // Select will emit cmp + csel
        }

        // ICmp + Br fusion: csel / ccmp+csel / cmp + b.cond
        if (inst->op_id_ == Instruction::ICmp) {
            auto *icmp = static_cast<ICmpInst*>(inst);
            auto next = std::next(it);
            if (next != instrs.end()) {
                auto *br = dynamic_cast<BranchInst*>(*next);
                if (br && br->num_ops_ == 3 && br->get_operand(0) == icmp && icmp->use_list_.size() == 1) {
                    if (tryEmitCSel(icmp, br)) { ++it; continue; }
                    // if (tryEmitCCmpCSel(icmp, br)) { ++it; continue; }
                    emitFusedCmpBranch(icmp, br);
                    ++it; // skip Br
                    continue;
                }
            }
        }

        // Mul + Add/Sub fusion → madd / msub / mneg
        //
        //   mul %m, %a, %b   then  add/sub using %m  →  fused madd/msub/mneg
        //
        // The graph-coloring allocator may reuse the same physical register
        // for %a and the Add's other operand (non-overlapping liveness before
        // fusion).  To avoid clobbering, we load the mul operands and pin
        // them to scratch registers BEFORE emitting any intervening instructions.
        if (inst->op_id_ == Instruction::Mul && inst->use_list_.size() == 1 && !isVector(inst->type_)) {
            auto mulIt = it;
            bool fused = false;
            auto scan = std::next(it);
            int skip = 0;
            for (; scan != instrs.end() && skip < 3; ++scan, ++skip) {
                Instruction *sInst = *scan;
                if (sInst->is_phi()) continue;
                bool usesMul = false;
                for (unsigned i = 0; i < sInst->num_ops_; ++i)
                    if (sInst->get_operand(i) == inst) { usesMul = true; break; }
                if (usesMul && !dynamic_cast<BinaryInst*>(sInst)) break;

                auto *addSub = dynamic_cast<BinaryInst*>(sInst);
                if (addSub) {
                    Value *op0 = addSub->get_operand(0);
                    Value *op1 = addSub->get_operand(1);
                    bool canMAdd = (addSub->op_id_ == Instruction::Add && (op0 == inst || op1 == inst));
                    bool canMSub = (addSub->op_id_ == Instruction::Sub && op1 == inst);
                    if (!canMAdd && !canMSub) break; // Add/Sub found but unrelated

                    // --- Fusion confirmed ---
                    BinaryInst *mulInst = static_cast<BinaryInst*>(inst);

                    // When the Add/Sub immediately follows the mul (nothing in
                    // between), the accumulator and both mul operands are all
                    // live at the fused point, so the allocator has already
                    // given them distinct registers — we can feed the fused op
                    // straight from the operands' assigned registers and skip
                    // the two defensive `mov w8/w16` copies entirely. Pinning to
                    // scratch is only needed when intervening instructions
                    // (emitted in step 2) might clobber a reused operand reg.
                    bool adjacent = (std::next(mulIt) == scan);

                    // 1. Load mul operands.  When pinning, copy into w8 / w16
                    //    (w8 is never returned by allocIntReg, pool is w10-w15;
                    //    w16 is re-marked after each intervening instruction).
                    resetRegs();
                    std::string rA = loadInt(mulInst->get_operand(0));
                    std::string rB = loadInt(mulInst->get_operand(1));
                    std::string rA2, rB2;
                    if (adjacent) {
                        // loadInt marked rA/rB used → the accumulator and result
                        // allocations below cannot collide with them.
                        rA2 = rA;
                        rB2 = rB;
                    } else {
                        emitMoveMachine("w8", rA);
                        emitMoveMachine("w16", rB);

                        // 2. Emit intervening instructions.  Each calls
                        //    resetRegs(), so re-pin w16 to keep it alive.
                        for (auto mid = std::next(mulIt); mid != scan; ++mid) {
                            if ((*mid)->is_phi()) continue;
                            emitInstruction(*mid);
                            if (!usedIntRegs_.count(16))
                                usedIntRegs_.insert(16);
                        }
                        resetRegs();
                        rA2 = "w8";
                        rB2 = "w16";
                    }

                    // 3. Emit fused madd / msub / mneg.
                    {
                        Value *accOp = (op0 == inst) ? op1 : op0;
                        std::string rAcc = loadInt(accOp);
                        std::string rd = allocIntReg();
                        if (canMAdd) {
                            emitRawAluMachine("\tmadd " + rd + ", " + rA2 + ", " + rB2
                                              + ", " + rAcc,
                                              rd, {rA2, rB2, rAcc}, MOpcode::Mul, 3);
                        } else {
                            Value *minuend = addSub->get_operand(0);
                            if (auto *ci = dynamic_cast<ConstantInt*>(minuend)) {
                                if (ci->value_ == 0) {
                                    emitRawAluMachine("\tmneg " + rd + ", " + rA2
                                                      + ", " + rB2,
                                                      rd, {rA2, rB2}, MOpcode::Mul, 3);
                                    storeInt(addSub, rd);
                                    it = scan; fused = true; break;
                                }
                            }
                            std::string rMin = loadInt(minuend);
                            emitRawAluMachine("\tmsub " + rd + ", " + rA2 + ", " + rB2
                                              + ", " + rMin,
                                              rd, {rA2, rB2, rMin}, MOpcode::Mul, 3);
                        }
                        storeInt(addSub, rd);
                    }

                    it = scan; fused = true; break;
                }
            }
            if (fused) continue;
        }

        emitInstruction(inst);
    }
}

void Arm64FuncContext::emitInstruction(Instruction *inst) {
    resetRegs();
    for (unsigned i = 0; i < inst->num_ops_; ++i) {
        Value *op = inst->get_operand(i);
        if (!hasAssignedReg(op)) continue;

        std::string reg = assignedReg(op, isPtr(op->type_));
        if (reg.size() < 2) continue;

        int regNo = std::stoi(reg.substr(1));
        if (reg[0] == 'w' || reg[0] == 'x') {
            usedIntRegs_.insert(regNo);
        } else if (reg[0] == 's' || reg[0] == 'd') {
            usedFloatRegs_.insert(regNo);
        } else if (reg[0] == 'v') {
            usedNEONRegs_.insert(regNo);
        }
    }

    switch (inst->op_id_) {

    // ---- Alloca ----
    case Instruction::Alloca: {
        // nothing to emit; slot already allocated in prologue
        break;
    }

    // ---- Store ----
    case Instruction::Store: {
        auto val = inst->get_operand(0);
        auto ptr = inst->get_operand(1);
        if (isVector(val->type_)) {
            std::string addr = loadAddr(ptr);
            std::string vs = loadVector(val);
            MachineInstr st = MachineInstr::make("\tst1 {" + vs + ".4s}, [" + addr + "]",
                                                 MOpcode::Store, {}, {vs, addr});
            st.mayStore = true;
            emitMachineInstr(std::move(st));
            break;
        }
        if (auto *gv = dynamic_cast<GlobalVariable*>(ptr)) {
            std::string base = allocAddrReg();
            emitMachineInstrLine("\tadrp " + base + ", " + gv->name_,
                                 MOpcode::Adr, {base});
            if (isFloat(val->type_)) {
                std::string r = loadFloat(val);
                emitStoreMemMachine(r, "[" + base + ", :lo12:" + gv->name_ + "]", {r, base});
            } else if (isPtr(val->type_)) {
                std::string r = loadAddr(val);
                emitStoreMemMachine(r, "[" + base + ", :lo12:" + gv->name_ + "]", {r, base});
            } else {
                std::string r = loadInt(val);
                emitStoreMemMachine(r, "[" + base + ", :lo12:" + gv->name_ + "]", {r, base});
            }
        } else {
            std::string addr = loadAddr(ptr);
            if (isFloat(val->type_)) {
                std::string r = loadFloat(val);
                emitStoreMemMachine(r, "[" + addr + "]", {r, addr});
            } else if (isPtr(val->type_)) {
                std::string r = loadAddr(val);
                emitStoreMemMachine(r, "[" + addr + "]", {r, addr});
            } else {
                std::string r = loadInt(val);
                emitStoreMemMachine(r, "[" + addr + "]", {r, addr});
            }
        }
        break;
    }

    // ---- Load ----
    case Instruction::Load: {
        auto ptr = inst->get_operand(0);
        if (isVector(inst->type_)) {
            std::string addr = loadAddr(ptr);
            std::string vd = hasAssignedReg(inst) ? assignedReg(inst) : allocNEONReg();
            MachineInstr ld = MachineInstr::make("\tld1 {" + vd + ".4s}, [" + addr + "]",
                                                MOpcode::Load, {vd}, {addr}, 4);
            ld.mayLoad = true;
            emitMachineInstr(std::move(ld));
            if (!hasAssignedReg(inst)) storeVector(inst, vd);
            break;
        }
        if (auto *gv = dynamic_cast<GlobalVariable*>(ptr)) {
            std::string base = allocAddrReg();
            emitMachineInstrLine("\tadrp " + base + ", " + gv->name_,
                                 MOpcode::Adr, {base});
            if (isFloat(inst->type_)) {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
                emitLoadMemMachine(r, "[" + base + ", :lo12:" + gv->name_ + "]", {base});
                storeFloat(inst, r);
            } else if (isPtr(inst->type_)) {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst, true) : allocAddrReg();
                emitLoadMemMachine(r, "[" + base + ", :lo12:" + gv->name_ + "]", {base});
                storeAddr(inst, r);
            } else {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                emitLoadMemMachine(r, "[" + base + ", :lo12:" + gv->name_ + "]", {base});
                storeInt(inst, r);
            }
        } else {
            std::string addr = loadAddr(ptr);
            if (isFloat(inst->type_)) {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
                emitLoadMemMachine(r, "[" + addr + "]", {addr});
                storeFloat(inst, r);
            } else if (isPtr(inst->type_)) {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst, true) : allocAddrReg();
                emitLoadMemMachine(r, "[" + addr + "]", {addr});
                storeAddr(inst, r);
            } else {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                emitLoadMemMachine(r, "[" + addr + "]", {addr});
                storeInt(inst, r);
            }
        }
        break;
    }

    // ---- Integer Binary ----
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::SDiv: {
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);

        // Vector path: Add/Sub/Mul on <4 x i32>
        if (isVector(inst->type_)) {
            std::string r1 = loadVector(v1);
            std::string r2 = loadVector(v2);
            std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocNEONReg();
            const char *opcode = nullptr;
            switch (inst->op_id_) {
                case Instruction::Add: opcode = "add"; break;
                case Instruction::Sub: opcode = "sub"; break;
                case Instruction::Mul: opcode = "mul"; break;
                default: break;
            }
            emitRawAluMachine("\t" + std::string(opcode) + " " + rd + ".4s, " + r1
                                  + ".4s, " + r2 + ".4s",
                              rd, {r1, r2}, MOpcode::Neon,
                              inst->op_id_ == Instruction::Mul ? 3 : 1);
            if (!hasAssignedReg(inst)) storeVector(inst, rd);
            break;
        }

        bool emitted = false;

        // 常量强度削减集中在 constStrengthReduce.cpp；此处仅按 opcode 分派。
        if (inst->op_id_ == Instruction::SDiv)
            emitted = tryEmitSDivConst(inst, v1, v2);
        else if (inst->op_id_ == Instruction::Mul)
            emitted = tryEmitMulConst(inst, v1, v2);

        // =====================================================================
        // 通用路径（Add / Sub / Mul 无法优化，或 SDiv 非常量 / 非 2^k 除数）
        // =====================================================================
        if (!emitted) {
            std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
            // 先试 Add/Sub 立即数形（细节见 constStrengthReduce.cpp）
            bool usedImm = tryEmitAddSubImm(inst, v1, v2, rd);

            if (!usedImm) {
                std::string r1 = loadInt(v1);
                std::string r2 = loadInt(v2);
                const char* opcode = nullptr;
                switch (inst->op_id_) {
                    case Instruction::Add:  opcode = "add";  break;
                    case Instruction::Sub:  opcode = "sub";  break;
                    case Instruction::Mul:  opcode = "mul";  break;
                    case Instruction::SDiv: opcode = "sdiv"; break;
                    default: break;
                }
                MOpcode mop = (inst->op_id_ == Instruction::Mul) ? MOpcode::Mul :
                              (inst->op_id_ == Instruction::SDiv) ? MOpcode::Div :
                              MOpcode::Alu;
                int latency = (inst->op_id_ == Instruction::Mul) ? 3 :
                              (inst->op_id_ == Instruction::SDiv) ? 12 : 1;
                emitMachineInstrLine(
                    "\t" + std::string(opcode) + " " + rd + ", " + r1 + ", " + r2,
                    mop, {rd}, {r1, r2}, latency);
            }
            storeInt(inst, rd);
        }
        break;
    }

    // ---- SRem: a % b = a - (a/b) * b ----
    // Be cautious modifying this
    case Instruction::SRem: {
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);
        // 常量除数取余的强度削减集中在 constStrengthReduce.cpp
        if (tryEmitSRemConst(inst, v1, v2))
            break;

        // ---- 通用 SRem (变量除数或未优化情况) ----
        std::string ra = loadInt(v1);
        std::string rb = loadInt(v2);
        std::string rq = allocIntReg();
        // 末条 msub 同时是 ra/rb 的最后一次读，直接写分配寄存器安全
        std::string rr = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitBinaryMachine("sdiv", rq, ra, rb, MOpcode::Div, 12);
        emitRawAluMachine("\tmsub " + rr + ", " + rq + ", " + rb + ", " + ra,
                          rr, {rq, rb, ra}, MOpcode::Mul, 3);
        storeInt(inst, rr);
        break;
    }

    // ---- Integer Bitwise Logical (And / Or / Xor) ----
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor: {
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);

        // 常量短路 + logical-immediate 折叠集中在 constStrengthReduce.cpp
        if (tryEmitLogicalConst(inst, v1, v2))
            break;

        // 通用：两操作数均非常量
        const char *opcode;
        if (inst->op_id_ == Instruction::And)      opcode = "and";
        else if (inst->op_id_ == Instruction::Or)   opcode = "orr";
        else                                        opcode = "eor";

        std::string r1 = loadInt(v1);
        std::string r2 = loadInt(v2);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitBinaryMachine(opcode, rd, r1, r2);
        storeInt(inst, rd);
        break;
    }

    // ---- Integer Shift ----
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr: {
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);

        // Vector path
        if (isVector(inst->type_)) {
            std::string r1 = loadVector(v1);
            std::string r2 = loadVector(v2);
            std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocNEONReg();
            const char *opcode = nullptr;
            switch (inst->op_id_) {
                case Instruction::Shl:  opcode = "sshl"; break;
                case Instruction::AShr: opcode = "sshr"; break;
                case Instruction::LShr: opcode = "ushr"; break;
                default: break;
            }
            emitRawAluMachine("\t" + std::string(opcode) + " " + rd + ".4s, " + r1
                                  + ".4s, " + r2 + ".4s",
                              rd, {r1, r2}, MOpcode::Neon);
            if (!hasAssignedReg(inst)) storeVector(inst, rd);
            break;
        }

        std::string r1 = loadInt(v1);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();

        const char *opcode;
        if (inst->op_id_ == Instruction::Shl)      opcode = "lsl";
        else if (inst->op_id_ == Instruction::LShr) opcode = "lsr";
        else                                        opcode = "asr";

        if (auto *ci = dynamic_cast<ConstantInt*>(v2)) {
            emitMachineInstrLine(
                "\t" + std::string(opcode) + " " + rd + ", " + r1 + ", #" + std::to_string(ci->value_),
                MOpcode::Alu, {rd}, {r1});
        } else {
            std::string r2 = loadInt(v2);
            emitMachineInstrLine(
                "\t" + std::string(opcode) + " " + rd + ", " + r1 + ", " + r2,
                MOpcode::Alu, {rd}, {r1, r2});
        }
        storeInt(inst, rd);
        break;
    }

    // ---- Float Binary ----
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FDiv: {
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);

        // Vector path: FAdd/FSub/FMul on <4 x float>
        if (isVector(inst->type_)) {
            std::string r1 = loadVector(v1);
            std::string r2 = loadVector(v2);
            std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocNEONReg();
            const char *opcode = nullptr;
            switch (inst->op_id_) {
                case Instruction::FAdd: opcode = "fadd"; break;
                case Instruction::FSub: opcode = "fsub"; break;
                case Instruction::FMul: opcode = "fmul"; break;
                default: break;
            }
            emitRawAluMachine("\t" + std::string(opcode) + " " + rd + ".4s, " + r1
                                  + ".4s, " + r2 + ".4s",
                              rd, {r1, r2}, MOpcode::Neon,
                              inst->op_id_ == Instruction::FMul ? 4 : 1);
            if (!hasAssignedReg(inst)) storeVector(inst, rd);
            break;
        }

        std::string r1 = loadFloat(v1);
        std::string r2 = loadFloat(v2);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
        const char *opcode = nullptr;
        switch (inst->op_id_) {
        case Instruction::FAdd: opcode = "fadd"; break;
        case Instruction::FSub: opcode = "fsub"; break;
        case Instruction::FMul: opcode = "fmul"; break;
        case Instruction::FDiv: opcode = "fdiv"; break;
        default: break;
        }
        MOpcode mop = (inst->op_id_ == Instruction::FMul) ? MOpcode::Mul :
                      (inst->op_id_ == Instruction::FDiv) ? MOpcode::Div :
                      MOpcode::Alu;
        int latency = (inst->op_id_ == Instruction::FMul) ? 5 :
                      (inst->op_id_ == Instruction::FDiv) ? 12 : 4;
        emitMachineInstrLine(
            "\t" + std::string(opcode) + " " + rd + ", " + r1 + ", " + r2,
            mop, {rd}, {r1, r2}, latency);
        storeFloat(inst, rd);
        break;
    }

    // ---- InsertElement ----
    case Instruction::InsertElement: {
        if (isSplatInsertChainIntermediate(inst))
            break;

        Value *splatValue = nullptr;
        if (isCompleteIntSplatInsertChain(inst, splatValue)) {
            std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocNEONReg();
            auto *constant = dynamic_cast<ConstantInt*>(splatValue);
            if (constant && canUseMoviSplat(constant->value_)) {
                emitRawAluMachine("\tmovi " + rd + ".4s, #" +
                                      std::to_string(constant->value_),
                                  rd, {}, MOpcode::Neon);
            } else {
                std::string ws = loadInt(splatValue);
                emitRawAluMachine("\tdup " + rd + ".4s, " + ws,
                                  rd, {ws}, MOpcode::Neon);
            }
            if (!hasAssignedReg(inst)) storeVector(inst, rd);
            break;
        }

        auto *vec = inst->get_operand(0);
        auto *val = inst->get_operand(1);
        auto *idx = inst->get_operand(2);
        auto *ci = dynamic_cast<ConstantInt*>(idx);
        if (!ci) break; // index must be constant
        int lane = ci->value_;
        // mov v.s[lane] requires a W register; floats need fmov first
        std::string ws;
        if (isFloat(val->type_)) {
            std::string sr = loadFloat(val);
            ws = allocIntReg();
            emitMoveMachine(ws, sr, "fmov");
        } else {
            ws = loadInt(val);
        }
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocNEONReg();
        // If base is not a vector (first insert in construction chain),
        // skip the copy and initialize the vector fresh.
        if (isVector(vec->type_)) {
            std::string vs = loadVector(vec);
            if (rd != vs)
                emitRawAluMachine("\tmov " + rd + ".16b, " + vs + ".16b",
                                  rd, {vs}, MOpcode::Neon);
        }
        emitRawAluMachine("\tmov " + rd + ".s[" + std::to_string(lane) + "], " + ws,
                          rd, {ws}, MOpcode::Neon);
        if (!hasAssignedReg(inst)) storeVector(inst, rd);
        break;
    }

    // ---- ICmp ----
    case Instruction::ICmp: {
        auto *icmp = static_cast<ICmpInst*>(inst);
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);
        std::string r1 = isPtr(v1->type_) ? loadAddr(v1) : loadInt(v1);
        std::string r2 = isPtr(v2->type_) ? loadAddr(v2) : loadInt(v2);
        std::string rd = allocIntReg();
        emitMachineInstrLine("\tcmp " + r1 + ", " + r2,
                             MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true);
        emitMachineInstrLine("\tcset " + rd + ", " + icmpCond(icmp->icmp_op_),
                             MOpcode::FlagUse, {rd}, {}, 1, false, true, true);
        storeInt(inst, rd);
        break;
    }

    // ---- Select ----
    case Instruction::Select: {
        auto *condVal = inst->get_operand(0);
        auto *tv = inst->get_operand(1);
        auto *fv = inst->get_operand(2);
        const char *cond = nullptr;
        if (auto *icmp = dynamic_cast<ICmpInst*>(condVal)) {
            auto *cv1 = icmp->get_operand(0);
            auto *cv2 = icmp->get_operand(1);
            std::string r1 = isPtr(cv1->type_) ? loadAddr(cv1) : loadInt(cv1);
            cond = icmpCond(icmp->icmp_op_);
            if (auto *ci = dynamic_cast<ConstantInt*>(cv2)) {
                int val = ci->value_;
                if (val >= 0 && val <= 4095)
                    emitMachineInstrLine("\tcmp " + r1 + ", #" + std::to_string(val),
                                         MOpcode::Cmp, {}, {r1}, 1, true, false, true);
                else { std::string r2 = allocIntReg(); emitIntConst(val, r2);
                    emitMachineInstrLine("\tcmp " + r1 + ", " + r2,
                                         MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true); }
            } else {
                std::string r2 = isPtr(cv2->type_) ? loadAddr(cv2) : loadInt(cv2);
                emitMachineInstrLine("\tcmp " + r1 + ", " + r2,
                                     MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true);
            }
        } else if (auto *fcmp = dynamic_cast<FCmpInst*>(condVal)) {
            auto *cv1 = fcmp->get_operand(0);
            auto *cv2 = fcmp->get_operand(1);
            std::string r1 = loadFloat(cv1);
            std::string r2 = loadFloat(cv2);
            cond = fcmpCond(fcmp->fcmp_op_);
            emitMachineInstrLine("\tfcmp " + r1 + ", " + r2,
                                 MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true);
        } else {
            break; // fallback: can't emit conditional select without flags
        }

        std::string dstReg;
        // If this Select's only user is a Ret, write directly to the return reg
        // to avoid a redundant mov in the Ret emission.
        bool directRet = false;
        if ((isInt(inst->type_) || isPtr(inst->type_) || isFloat(inst->type_)) &&
            inst->use_list_.size() == 1) {
            auto *user = dynamic_cast<ReturnInst*>((*inst->use_list_.begin()).val_);
            if (user) directRet = true;
        }
        if (directRet) {
            dstReg = isFloat(inst->type_) ? "s0" : (isPtr(inst->type_) ? "x0" : "w0");
            bindValueToReg(inst, dstReg);
        } else {
            if (isFloat(inst->type_))
                dstReg = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
            else if (isPtr(inst->type_))
                dstReg = hasAssignedReg(inst) ? assignedReg(inst, true) : allocAddrReg();
            else
                dstReg = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        }

        auto loadSelectOperand = [&](Value *v) -> std::string {
            if (isFloat(inst->type_))
                return hasAssignedReg(v) ? assignedReg(v) : loadFloat(v);
            if (isPtr(inst->type_))
                return hasAssignedReg(v) ? assignedReg(v, true) : loadAddr(v);
            return hasAssignedReg(v) ? assignedReg(v) : loadInt(v);
        };

        std::string trueReg = loadSelectOperand(tv);
        std::string falseReg = loadSelectOperand(fv);
        if (isFloat(inst->type_)) {
            emitMachineInstrLine("\tfcsel " + dstReg + ", " + trueReg + ", " + falseReg + ", " + cond,
                                 MOpcode::FlagUse, {dstReg}, {trueReg, falseReg}, 1,
                                 false, true, true);
            if (!directRet && !hasAssignedReg(inst)) storeFloat(inst, dstReg);
        } else {
            emitMachineInstrLine("\tcsel " + dstReg + ", " + trueReg + ", " + falseReg + ", " + cond,
                                 MOpcode::FlagUse, {dstReg}, {trueReg, falseReg}, 1,
                                 false, true, true);
            if (!directRet && !hasAssignedReg(inst)) {
                if (isPtr(inst->type_)) storeAddr(inst, dstReg);
                else storeInt(inst, dstReg);
            }
        }
        break;
    }

    // ---- FCmp ----
    case Instruction::FCmp: {
        auto *fcmp = static_cast<FCmpInst*>(inst);
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);
        std::string r1 = loadFloat(v1);
        std::string r2 = loadFloat(v2);
        std::string rd = allocIntReg();
        emitMachineInstrLine("\tfcmp " + r1 + ", " + r2,
                             MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true);
        emitMachineInstrLine("\tcset " + rd + ", " + fcmpCond(fcmp->fcmp_op_),
                             MOpcode::FlagUse, {rd}, {}, 1, false, true, true);
        storeInt(inst, rd);
        break;
    }

    // ---- GEP ----
    case Instruction::GetElementPtr: {
        auto *gep = static_cast<GetElementPtrInst*>(inst);
        auto ptr = gep->get_operand(0);

        std::string addr;
        auto samePhysReg = [](const std::string &a, const std::string &b) {
            if (a.size() < 2 || b.size() < 2) return a == b;
            bool aInt = a[0] == 'w' || a[0] == 'x';
            bool bInt = b[0] == 'w' || b[0] == 'x';
            bool aFloat = a[0] == 's' || a[0] == 'd';
            bool bFloat = b[0] == 's' || b[0] == 'd';
            return ((aInt && bInt) || (aFloat && bFloat)) && a.substr(1) == b.substr(1);
        };

        bool useAssignedAddr = hasAssignedReg(inst);
        std::string assignedAddr = useAssignedAddr ? assignedReg(inst, true) : "";
        if (useAssignedAddr) {
            for (unsigned i = 1; i < gep->num_ops_; ++i) {
                auto idx = gep->get_operand(i);
                if (dynamic_cast<ConstantInt*>(idx) || !hasAssignedReg(idx))
                    continue;
                if (samePhysReg(assignedAddr, assignedReg(idx))) {
                    useAssignedAddr = false;
                    break;
                }
            }
        }

        if (useAssignedAddr) {
            addr = assignedAddr;
            if (auto *gv = dynamic_cast<GlobalVariable*>(ptr)) {
                emitGlobalAddr(gv, addr);
            } else if (auto *ci = dynamic_cast<ConstantInt*>(ptr)) {
                emitIntConst(ci->value_, addr);
            } else if (hasAssignedReg(ptr)) {
                std::string base = assignedReg(ptr, true);
                if (addr != base)
                    emitMoveMachine(addr, base);
            } else if (dynamic_cast<AllocaInst*>(ptr)) {
                int off = getSlot(ptr);
                if (off < 0) {
                    int absOff = -off;
                    if (absOff <= 4095) {
                        emitRawAluMachine("\tsub " + addr + ", x29, #" + std::to_string(absOff),
                                          addr, {"x29"});
                    } else {
                        emitIntConst(absOff, "x17");
                        emitBinaryMachine("sub", addr, "x29", "x17");
                    }
                } else {
                    if (off <= 4095) {
                        emitRawAluMachine("\tadd " + addr + ", x29, #" + std::to_string(off),
                                          addr, {"x29"});
                    } else {
                        emitIntConst(off, "x17");
                        emitBinaryMachine("add", addr, "x29", "x17");
                    }
                }
            } else {
                emitLoadRegMachine(addr, getSlot(ptr));
            }
        } else {
            std::string base = loadAddr(ptr);
            if (hasAssignedReg(ptr)) {
                addr = allocAddrReg();
                emitMoveMachine(addr, base);
            } else {
                addr = base;
            }
        }

        unsigned numIdx = gep->num_ops_ - 1;
        auto *srcTy = static_cast<PointerType*>(ptr->type_)->contained_;
        Type *curTy = srcTy;

        for (unsigned i = 1; i < gep->num_ops_; i++) {
            auto idx = gep->get_operand(i);

            // 1. 当前层级元素的大小（一定是 typeSize(curTy)）
            int elemSize = typeSize(curTy);

            // 2. 更新 curTy 到下一层（为下一次迭代准备）
            if (curTy->tid_ == Type::ArrayTyID) {
                auto *at = static_cast<ArrayType*>(curTy);
                curTy = at->contained_;
            } else if (curTy->tid_ == Type::PointerTyID) {
                auto *pt = static_cast<PointerType*>(curTy);
                curTy = pt->contained_;
            }
            // 否则是基本类型，不再更新（后续索引非法，但一般不会出现）

            // 单层下标的常量驱动地址削减集中在 constStrengthReduce.cpp
            emitGepIndexStep(addr, idx, elemSize, inst);
        }
        if (!useAssignedAddr)
            storeAddr(inst, addr);
        break;
    }

    // ---- ZExt (i1 → i32) ----
    case Instruction::ZExt: {
        auto val = inst->get_operand(0);
        std::string r = loadInt(val);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitRawAluMachine("\tand " + rd + ", " + r + ", #1", rd, {r});
        storeInt(inst, rd);
        break;
    }

    // ---- FPtoSI (float → i32) ----
    case Instruction::FPtoSI: {
        auto val = inst->get_operand(0);
        std::string r = loadFloat(val);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitUnaryMachine("fcvtzs", rd, r, MOpcode::Alu, 4);
        storeInt(inst, rd);
        break;
    }

    // ---- SItoFP (i32 → float) ----
    case Instruction::SItoFP: {
        auto val = inst->get_operand(0);
        std::string r = loadInt(val);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
        emitUnaryMachine("scvtf", rd, r, MOpcode::Alu, 4);
        storeFloat(inst, rd);
        break;
    }

    // ---- BitCast ----
    case Instruction::BitCast: {
        auto val = inst->get_operand(0);
        if (isFloat(inst->type_) && isInt(val->type_)) {
            std::string r = loadInt(val);
            std::string rd = allocFloatReg();
            emitMoveMachine(rd, r, "fmov");
            storeFloat(inst, rd);
        } else if (isInt(inst->type_) && isFloat(val->type_)) {
            std::string r = loadFloat(val);
            std::string rd = allocIntReg();
            emitMoveMachine(rd, r, "fmov");
            storeInt(inst, rd);
        } else {
            // pointer bitcasts: just copy
            std::string r = loadAddr(val);
            storeAddr(inst, r);
        }
        break;
    }

    // ---- Clz (count leading zeros) ----
    case Instruction::Clz: {
        auto val = inst->get_operand(0);
        std::string r = loadInt(val);
        std::string rd = allocIntReg();
        emitUnaryMachine("clz", rd, r);
        storeInt(inst, rd);
        break;
    }

    // ---- Br ----
    case Instruction::Br: {
        auto parentBB = inst->parent_;

        if (inst->num_ops_ == 1) {
            // Unconditional branch: emit copies for the single edge
            auto *target = static_cast<BasicBlock*>(inst->get_operand(0));
            emitPhiCopies(parentBB, target);
            emitBranchMachine("\tb " + bbLabel(func_, target));
        } else {
            // Conditional branch: evaluate condition FIRST, then edge-specific copies
            auto cond = inst->get_operand(0);
            auto *trueBB = static_cast<BasicBlock*>(inst->get_operand(1));
            auto *falseBB = static_cast<BasicBlock*>(inst->get_operand(2));

            // Check if either edge has phi copies
            bool hasTrue = false, hasFalse = false;
            for (const auto &pc : phiCopies_) {
                if (pc.pred != parentBB) continue;
                if (pc.succ == trueBB) hasTrue = true;
                if (pc.succ == falseBB) hasFalse = true;
            }

            std::string cr = loadInt(cond);

            if (!hasTrue && !hasFalse) {
                emitBranchMachine("\tcbnz " + cr + ", " + bbLabel(func_, trueBB), {cr});
                emitBranchMachine("\tb " + bbLabel(func_, falseBB));
            } else if (hasTrue && !hasFalse) {
                emitBranchMachine("\tcbz " + cr + ", " + bbLabel(func_, falseBB), {cr});
                emitPhiCopies(parentBB, trueBB);
                emitBranchMachine("\tb " + bbLabel(func_, trueBB));
            } else if (!hasTrue && hasFalse) {
                emitBranchMachine("\tcbnz " + cr + ", " + bbLabel(func_, trueBB), {cr});
                emitPhiCopies(parentBB, falseBB);
                emitBranchMachine("\tb " + bbLabel(func_, falseBB));
            } else {
                // Both edges have copies: use edge label
                std::string edgeLbl = ".L" + func_->name_ + "_edge_" +
                    std::to_string(edgeCounter_++);
                emitBranchMachine("\tcbz " + cr + ", " + edgeLbl, {cr});
                emitPhiCopies(parentBB, trueBB);
                emitBranchMachine("\tb " + bbLabel(func_, trueBB));
                emitMachineLine(edgeLbl + ":");
                emitPhiCopies(parentBB, falseBB);
                emitBranchMachine("\tb " + bbLabel(func_, falseBB));
            }
        }
        break;
    }

    // ---- Ret ----
    case Instruction::Ret: {
        if (inst->num_ops_ > 0) {
            auto val = inst->get_operand(0);
            if (isFloat(val->type_))
                materializeFloatInReg(val, "s0");
            else if (isPtr(val->type_))
                materializeAddrInReg(val, "x0");
            else
                materializeIntInReg(val, "w0");
        }
        if (needsFrame_)
            emitBranchMachine("\tb .L" + func_->name_ + "_epilogue");
        else
            emitRetMachine();
        break;
    }

    // ---- Call ----
    case Instruction::Call: {
        auto *call = static_cast<CallInst*>(inst);
        unsigned numArgs = call->num_ops_ - 1;
        auto *callee = static_cast<Function*>(call->get_operand(numArgs));

        struct RegArg {
            unsigned index;
            Value *value;
            std::string dst;
            bool isFloat;
            bool isPtr;
        };

        std::vector<RegArg> regArgs;
        std::set<int> intArgDests;
        std::set<int> floatArgDests;

        int scanIntArg = 0, scanFloatArg = 0;
        for (unsigned i = 0; i < numArgs; i++) {
            auto arg = call->get_operand(i);
            if (isFloat(arg->type_)) {
                if (scanFloatArg < 8) {
                    regArgs.push_back({i, arg, "s" + std::to_string(scanFloatArg), true, false});
                    floatArgDests.insert(scanFloatArg);
                }
                scanFloatArg++;
            } else {
                bool ptr = isPtr(arg->type_);
                if (scanIntArg < 8) {
                    regArgs.push_back({i, arg,
                                      (ptr ? "x" : "w") + std::to_string(scanIntArg),
                                      false, ptr});
                    intArgDests.insert(scanIntArg);
                }
                scanIntArg++;
            }
        }

        // Register argument assignment is a parallel copy: evaluating it
        // sequentially can clobber a later source already sitting in x0-x7/s0-s7.
        // Pre-copy those sources to scratch registers so regalloc does not need
        // to force every call operand into callee-saved registers.
        std::map<Value*, std::string> callArgTemps;
        auto regNo = [](const std::string &reg) -> int {
            if (reg.size() < 2) return -1;
            return std::stoi(reg.substr(1));
        };
        auto regPrefix = [](const std::string &reg) -> char {
            return reg.empty() ? '\0' : reg[0];
        };
        for (unsigned i = 0; i < numArgs; i++) {
            auto arg = call->get_operand(i);
            if (!hasAssignedReg(arg) || callArgTemps.count(arg))
                continue;

            bool argIsFloat = isFloat(arg->type_);
            bool argIsPtr = isPtr(arg->type_);
            std::string src = assignedReg(arg, argIsPtr);
            int srcNo = regNo(src);
            if (srcNo < 0)
                continue;

            bool sourceCanBeClobbered = false;
            if (argIsFloat) {
                sourceCanBeClobbered = floatArgDests.count(srcNo) > 0;
            } else {
                sourceCanBeClobbered = intArgDests.count(srcNo) > 0;
            }
            if (!sourceCanBeClobbered)
                continue;

            bool hasSameRegisterDest = false;
            for (const auto &ra : regArgs) {
                if (ra.index != i) continue;
                hasSameRegisterDest = (regNo(ra.dst) == srcNo && regPrefix(ra.dst) == regPrefix(src));
                break;
            }
            if (hasSameRegisterDest)
                continue;

            if (argIsFloat) {
                usedFloatRegs_.insert(srcNo);
                std::string tmp = allocFloatReg();
                emitMoveMachine(tmp, src, "fmov");
                callArgTemps[arg] = tmp;
            } else if (argIsPtr) {
                usedIntRegs_.insert(srcNo);
                std::string tmp = allocAddrReg();
                emitMoveMachine(tmp, src);
                callArgTemps[arg] = tmp;
            } else {
                usedIntRegs_.insert(srcNo);
                std::string tmp = allocIntReg();
                emitMoveMachine(tmp, src);
                callArgTemps[arg] = tmp;
            }
        }

        // 计算参数分配信息
        int intArg = 0, floatArg = 0;
        int stackArgsCount = 0;
        for (unsigned i = 0; i < numArgs; i++) {
            auto arg = call->get_operand(i);
            if (isFloat(arg->type_)) {
                if (floatArg++ >= 8) stackArgsCount++;
            } else {
                if (intArg++ >= 8) stackArgsCount++;
            }
        }
        int stackBytes = align16(stackArgsCount * 8);

        // 分配栈参数空间
        if (stackBytes > 0) {
            if (stackBytes <= 4095)
                emitStackAdjustMachine("sub", stackBytes);
            else {
                emitIntConst(stackBytes, "x17");
                emitStackAdjustMachine("sub", "x17");
            }
        }

        // Materialize constants and addresses directly in their ABI register.
        // Memory-resident SSA values are staged first, then participate in the
        // same parallel-copy boundary as register-resident values.  Keeping
        // fixed-register assignment separate from value materialization avoids
        // extending ABI-register definitions through unrelated computations.
        // 传递参数 (寄存器 + 栈)
        intArg = 0; floatArg = 0;
        int stackIdx = 0;   // 栈参数写入偏移 (相对于 sp)
        for (unsigned i = 0; i < numArgs; i++) {
            auto arg = call->get_operand(i);
            if (isFloat(arg->type_)) {
                if (floatArg < 8) {
                    std::string dst = "s" + std::to_string(floatArg++);
                    auto temp = callArgTemps.find(arg);
                    if (temp != callArgTemps.end())
                        emitMoveMachine(dst, temp->second, "fmov");
                    else if (dynamic_cast<ConstantFloat*>(arg))
                        materializeFloatInReg(arg, dst);
                    else {
                        std::string src = loadFloat(arg);
                        emitMoveMachine(dst, src, "fmov");
                    }
                } else {
                    std::string r = callArgTemps.count(arg) ? callArgTemps[arg] : loadFloat(arg);
                    emitStoreMemMachine(r, "[sp, #" + std::to_string(stackIdx * 8) + "]", {r, "sp"});
                    stackIdx++;
                }
            } else if (isPtr(arg->type_)) {
                if (intArg < 8) {
                    std::string dst = "x" + std::to_string(intArg++);
                    auto temp = callArgTemps.find(arg);
                    if (temp != callArgTemps.end())
                        emitMoveMachine(dst, temp->second);
                    else if (dynamic_cast<GlobalVariable*>(arg) ||
                             dynamic_cast<ConstantInt*>(arg) ||
                             dynamic_cast<AllocaInst*>(arg))
                        materializeAddrInReg(arg, dst);
                    else {
                        std::string src = loadAddr(arg);
                        emitMoveMachine(dst, src);
                    }
                } else {
                    std::string r = callArgTemps.count(arg) ? callArgTemps[arg] : loadAddr(arg);
                    emitStoreMemMachine(r, "[sp, #" + std::to_string(stackIdx * 8) + "]", {r, "sp"});
                    stackIdx++;
                }
            } else {
                if (intArg < 8) {
                    std::string dst = "w" + std::to_string(intArg++);
                    auto temp = callArgTemps.find(arg);
                    if (temp != callArgTemps.end())
                        emitMoveMachine(dst, temp->second);
                    else if (dynamic_cast<ConstantInt*>(arg))
                        materializeIntInReg(arg, dst);
                    else {
                        std::string src = loadInt(arg);
                        emitMoveMachine(dst, src);
                    }
                } else {
                    std::string r = callArgTemps.count(arg) ? callArgTemps[arg] : loadInt(arg);
                    std::string tmp = allocAddrReg();
                    emitMoveMachine(tmp, r, indexExtendOpcode(arg, inst->parent_));
                    emitStoreMemMachine(tmp, "[sp, #" + std::to_string(stackIdx * 8) + "]", {tmp, "sp"});
                    freeAddrReg(tmp);
                    stackIdx++;
                }
            }
        }

        // 执行调用
        emitCallMachine(callee->name_, call);

        // 回收栈参数空间
        if (stackBytes > 0) {
            if (stackBytes <= 4095)
                emitStackAdjustMachine("add", stackBytes);
            else {
                emitIntConst(stackBytes, "x17");
                emitStackAdjustMachine("add", "x17");
            }
        }

        // 处理返回值
        if (!isVoid(inst->type_)) {
            if (isFloat(inst->type_)) {
                storeFloat(inst, "s0");
            } else if (isPtr(inst->type_)) {
                storeAddr(inst, "x0");
            } else {
                storeInt(inst, "w0");
            }
        }
        break;
    }

    // ---- PHI: handled by phiCopies, nothing to emit ----
    case Instruction::PHI:
        break;

    // ---- FNeg ----
    case Instruction::FNeg: {
        auto val = inst->get_operand(0);
        std::string r = loadFloat(val);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
        emitMachineInstrLine("\tfneg " + rd + ", " + r,
                             MOpcode::Alu, {rd}, {r}, 4);
        storeFloat(inst, rd);
        break;
    }

    default:
        emitMachineLine("\t// unsupported op_id: " + std::to_string((int)inst->op_id_));
        break;
    }
}
