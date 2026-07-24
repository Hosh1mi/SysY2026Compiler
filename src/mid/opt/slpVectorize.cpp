#include "../../include/mid/opt/slpVectorize.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/module.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

// =====================================================================
// Entry points
// =====================================================================

void SLPVectorize::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses SLPVectorize::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration()) {
            runOnFunction(func, module);
            changed = true;
        }
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

void SLPVectorize::runOnFunction(Function *func, Module *module) {
    if (func->basic_blocks_.empty()) return;

    const bool debug = std::getenv("DEBUG_SLP_VECTORIZE") != nullptr;

    for (auto *bb : func->basic_blocks_) {
        PackSet P = findAdjacentMemoryRefs(bb, module);
        if (P.packs.empty()) continue;

        // Phase 2: Extend pack set along use-def chains
        PackSet P_ext = extendPackSet(bb, P, module);

        // Phase 3: Combine overlapping packs
        PackSet P_combined = combinePacks(P_ext);

        if (debug) {
            std::cerr << "[SLP] func=" << func->name_
                      << " bb=" << bb->name_
                      << " packs=" << P_combined.packs.size()
                      << " (phase1=" << P.packs.size() << ")\n";
        }

        scheduleAndEmit(bb, P_combined, module);
    }
}

// =====================================================================
// PackSet helpers
// =====================================================================

bool SLPVectorize::PackSet::contains(Instruction *s) const {
    for (auto &p : packs)
        for (auto *inst : p.instrs)
            if (inst == s) return true;
    return false;
}

bool SLPVectorize::PackSet::containsAny(const std::vector<Instruction*> &cands) const {
    for (auto &p : packs)
        for (auto *inst : p.instrs)
            for (auto *c : cands)
                if (inst == c) return true;
    return false;
}

void SLPVectorize::PackSet::add(Pack p) {
    packs.push_back(std::move(p));
}

// =====================================================================
// Phase 1: Find adjacent memory references
// =====================================================================

SLPVectorize::PackSet SLPVectorize::findAdjacentMemoryRefs(
    BasicBlock *bb, Module *module)
{
    PackSet P;
    bool dbg = std::getenv("DEBUG_SLP_VECTORIZE") != nullptr;

    std::vector<Instruction*> stores;
    for (auto *inst : bb->instr_list_)
        if (inst->is_store()) stores.push_back(inst);
    if (stores.size() < VF) return P;

    if (dbg)
        std::cerr << "[SLP] phase1: " << stores.size() << " stores in "
                  << bb->name_ << "\n";

    std::vector<bool> used(stores.size(), false);

    for (size_t i = 0; i + VF <= stores.size(); ++i) {
        if (used[i]) continue;

        std::vector<Instruction*> pack;
        pack.push_back(stores[i]);
        used[i] = true;

        for (size_t j = i + 1; j < stores.size() && (int)pack.size() < VF; ++j) {
            if (used[j]) continue;
            if (!isIsomorphic(pack[0], stores[j])) continue;
            if (!isAdjacentStore(pack.back(), stores[j], module)) continue;
            if (!isIndependent(pack[0], stores[j])) continue;
            pack.push_back(stores[j]);
            used[j] = true;
        }

        if ((int)pack.size() == VF) {
            P.add({pack, nullptr, false});
        } else {
            for (size_t k = i; k < stores.size(); ++k)
                for (auto *s : pack)
                    if (stores[k] == s) used[k] = false;
        }
    }

    return P;
}

// =====================================================================
// Phase 2: Extend pack set along use-def chains
// =====================================================================

SLPVectorize::PackSet SLPVectorize::extendPackSet(
    BasicBlock *bb, PackSet P, Module *module)
{
    bool dbg = std::getenv("DEBUG_SLP_VECTORIZE") != nullptr;
    size_t prevSize;

    do {
        prevSize = P.packs.size();
        std::vector<Pack> newPacks;

        // follow_use_defs: pack producers of operands
        for (auto &pack : P.packs) {
            if (pack.instrs.empty()) continue;

            if (pack.instrs[0]->is_store()) {
                // For stores: follow the stored VALUE producers
                std::vector<Value*> storedVals;
                for (auto *inst : pack.instrs)
                    storedVals.push_back(inst->get_operand(0));

                // Check if all stored values are produced by instructions
                std::vector<Instruction*> producers;
                bool allGood = true;
                for (auto *sv : storedVals) {
                    auto *p = dynamic_cast<Instruction*>(sv);
                    if (!p || p->parent_ != bb || !isVectorizable(p))
                        { allGood = false; break; }
                    if (producers.empty())
                        producers.push_back(p);
                    else if (!isIsomorphic(producers[0], p))
                        { allGood = false; break; }
                    else
                        producers.push_back(p);
                }
                if (!allGood || (int)producers.size() != VF) continue;
                if (P.containsAny(producers)) continue;
                for (auto &np : newPacks)
                    if (np.instrs[0] == producers[0]) { allGood = false; break; }
                if (!allGood) continue;

                // Check independence
                bool indep = true;
                for (size_t a = 0; a < producers.size() && indep; ++a)
                    for (size_t b = a + 1; b < producers.size() && indep; ++b)
                        if (!isIndependent(producers[a], producers[b]))
                            indep = false;
                if (!indep) continue;

                newPacks.push_back({producers, nullptr, false});
                if (dbg)
                    std::cerr << "[SLP] phase2 follow_use_defs: packed "
                              << producers.size() << " producers\n";
            }
        }

        // follow_use_defs: for binary packs, pack adjacent load operands
        for (auto &pack : P.packs) {
            if (pack.instrs.empty()) continue;
            if (!dynamic_cast<BinaryInst*>(pack.instrs[0])) continue;

            for (unsigned opIdx = 0; opIdx < pack.instrs[0]->num_ops_; ++opIdx) {
                std::vector<Instruction*> loads;
                bool allLoads = true;
                for (auto *inst : pack.instrs) {
                    Value *op = inst->get_operand(opIdx);
                    auto *ld = dynamic_cast<LoadInst*>(op);
                    if (!ld || ld->parent_ != bb)
                        { allLoads = false; break; }
                    if (loads.empty())
                        loads.push_back(ld);
                    else if (!isIsomorphic(loads[0], ld))
                        { allLoads = false; break; }
                    else {
                        // Check adjacency
                        if (!isAdjacentLoad(loads.back(), ld, module))
                            { allLoads = false; break; }
                        loads.push_back(ld);
                    }
                }
                if (!allLoads || (int)loads.size() != VF) continue;
                if (P.containsAny(loads)) continue;
                bool dup = false;
                for (auto &np : newPacks)
                    if (np.instrs[0] == loads[0]) { dup = true; break; }
                if (dup) continue;

                newPacks.push_back({loads, nullptr, false});
                if (dbg)
                    std::cerr << "[SLP] phase2 follow_use_defs: packed "
                              << loads.size() << " loads\n";
            }
        }

        // follow_def_uses: for binary packs, find isomorphic users
        for (auto &pack : P.packs) {
            if (pack.instrs.empty()) continue;
            Instruction *p0 = pack.instrs[0];
            if (p0->is_store() || p0->is_load()) continue;
            if (!isVectorizable(p0)) continue;

            for (auto &use : p0->use_list_) {
                auto *user0 = dynamic_cast<Instruction*>(use.val_);
                if (!user0 || user0->parent_ != bb) continue;
                if (!isVectorizable(user0)) continue;
                if (P.contains(user0)) continue;

                unsigned argNo = use.arg_no_;
                std::vector<Instruction*> users;
                bool allMatch = true;
                for (size_t k = 0; k < pack.instrs.size(); ++k) {
                    Instruction *matching = nullptr;
                    for (auto &u : pack.instrs[k]->use_list_) {
                        if (u.arg_no_ != argNo) continue;
                        auto *cand = dynamic_cast<Instruction*>(u.val_);
                        if (!cand || cand->parent_ != bb) continue;
                        if (!isVectorizable(cand)) continue;
                        if (k == 0 || isIsomorphic(users[0], cand)) {
                            matching = cand;
                            break;
                        }
                    }
                    if (!matching) { allMatch = false; break; }
                    users.push_back(matching);
                }
                if (!allMatch || (int)users.size() != VF) continue;
                if (P.containsAny(users)) continue;
                for (auto &np : newPacks)
                    if (np.instrs[0] == users[0]) { allMatch = false; break; }
                if (!allMatch) continue;

                bool indep = true;
                for (size_t a = 0; a < users.size() && indep; ++a)
                    for (size_t b = a + 1; b < users.size() && indep; ++b)
                        if (!isIndependent(users[a], users[b]))
                            indep = false;
                if (!indep) continue;

                newPacks.push_back({users, nullptr, false});
                if (dbg)
                    std::cerr << "[SLP] phase2 follow_def_uses: packed "
                              << users.size() << " users\n";
            }
        }

        for (auto &np : newPacks)
            P.add(std::move(np));
    } while (P.packs.size() != prevSize);

    return P;
}

// =====================================================================
// Phase 3: Combine overlapping packs
// =====================================================================

SLPVectorize::PackSet SLPVectorize::combinePacks(PackSet P)
{
    if (P.packs.size() < 2) return P;

    bool dbg = std::getenv("DEBUG_SLP_VECTORIZE") != nullptr;
    size_t prevSize;

    do {
        prevSize = P.packs.size();

        for (size_t i = 0; i < P.packs.size(); ++i) {
            for (size_t j = i + 1; j < P.packs.size(); ++j) {
                auto &pi = P.packs[i];
                auto &pj = P.packs[j];
                if (pi.instrs.empty() || pj.instrs.empty()) continue;

                // Merge if the last instruction of pi equals the first of pj
                if (pi.instrs.back() == pj.instrs.front() &&
                    (int)(pi.instrs.size() + pj.instrs.size() - 1) <= VF) {
                    Pack merged;
                    merged.instrs = pi.instrs;
                    merged.instrs.insert(merged.instrs.end(),
                        pj.instrs.begin() + 1, pj.instrs.end());

                    // Remove overwritten packs and add merged
                    auto first = std::min(i, j);
                    auto second = std::max(i, j);
                    P.packs.erase(P.packs.begin() + second);
                    P.packs.erase(P.packs.begin() + first);
                    P.add(merged);

                    if (dbg)
                        std::cerr << "[SLP] phase3: merged packs, size="
                                  << merged.instrs.size() << "\n";
                    goto restart;
                }
            }
        }
        restart:;
    } while (P.packs.size() != prevSize);

    return P;
}

// =====================================================================
// Phase 4: Schedule and emit vector instructions
// =====================================================================

void SLPVectorize::scheduleAndEmit(BasicBlock *bb, PackSet P,
                                    Module *module)
{
    const bool debug = std::getenv("DEBUG_SLP_VECTORIZE") != nullptr;

    // Emit packs in dependency order: loads first, then binary, then stores
    std::vector<Pack*> ordered;
    for (auto &p : P.packs) {
        if (!p.instrs.empty() && p.instrs[0]->is_load())
            ordered.push_back(&p);
    }
    for (auto &p : P.packs) {
        if (!p.instrs.empty() && dynamic_cast<BinaryInst*>(p.instrs[0]))
            ordered.push_back(&p);
    }
    for (auto &p : P.packs) {
        if (!p.instrs.empty() && p.instrs[0]->is_store())
            ordered.push_back(&p);
    }

    for (auto *pack : ordered) {
        if (pack->instrs.empty() || pack->emitted) continue;
        Instruction *first = pack->instrs[0];

        if (first->is_load()) {
            emitVectorLoad(bb, *pack, module);
        } else if (first->is_store()) {
            // Try to connect with upstream binary pack's vector value
            if (!pack->vecValue) {
                Value *storedVal = first->get_operand(0);
                for (auto &other : P.packs) {
                    if (other.emitted && other.vecValue &&
                        other.instrs[0] == storedVal) {
                        pack->vecValue = other.vecValue;
                        break;
                    }
                }
            }
            emitVectorStore(bb, *pack, module);
        } else if (auto *lb = dynamic_cast<BinaryInst*>(first)) {
            emitVectorBinary(bb, *pack, module, P);
        }
    }
}

// ── Emit: vector load ─────────────────────────────────────────────────

void SLPVectorize::emitVectorLoad(BasicBlock *bb, Pack &pack, Module *module)
{
    const bool debug = std::getenv("DEBUG_SLP_VECTORIZE") != nullptr;
    if ((int)pack.instrs.size() != VF) return;

    // Verify contiguous loads with same base
    Value *basePtr = nullptr;
    int baseOffset = -1;
    for (size_t i = 0; i < pack.instrs.size(); ++i) {
        Value *ptr = pack.instrs[i]->get_operand(0);
        auto *gep = dynamic_cast<GetElementPtrInst*>(ptr);
        if (!gep) return;

        Value *gepBase = gep->get_operand(0);
        unsigned last = gep->num_ops_ - 1;
        auto *ci = dynamic_cast<ConstantInt*>(gep->get_operand(last));
        if (!ci) return;

        if (i == 0) { basePtr = gepBase; baseOffset = ci->value_; }
        else if (gepBase != basePtr || ci->value_ != baseOffset + (int)i) return;

        auto *firstGep = dynamic_cast<GetElementPtrInst*>(
            pack.instrs[0]->get_operand(0));
        for (unsigned k = 1; k < last; ++k) {
            Value *a = gep->get_operand(k);
            Value *b = firstGep->get_operand(k);
            if (a != b) {
                auto *ca = dynamic_cast<ConstantInt*>(a);
                auto *cb = dynamic_cast<ConstantInt*>(b);
                if (!ca || !cb || ca->value_ != cb->value_) return;
            }
        }
    }

    Type *scalarTy = pack.instrs[0]->type_;
    if (scalarTy->tid_ != Type::IntegerTyID &&
        scalarTy->tid_ != Type::FloatTyID) return;

    if (debug)
        std::cerr << "[SLP] emit vector load, offset=" << baseOffset << "\n";

    auto *firstGep = dynamic_cast<GetElementPtrInst*>(
        pack.instrs[0]->get_operand(0));
    std::vector<Value*> idxs;
    for (unsigned k = 1; k < firstGep->num_ops_; ++k)
        idxs.push_back(firstGep->get_operand(k));
    idxs.back() = new ConstantInt(module->int32_ty_, baseOffset);

    auto *vecGep = new GetElementPtrInst(firstGep->get_operand(0), idxs, bb);
    Type *vecTy = module->get_vector_type(scalarTy, VF);
    auto *vecPtr = new Bitcast(Instruction::BitCast, vecGep,
        module->get_pointer_type(vecTy), bb);
    auto *vecLoad = new LoadInst(vecPtr, bb);

    bb->remove_instr(vecGep);
    bb->add_instruction_before_inst(vecGep, pack.instrs[0]);
    bb->remove_instr(vecPtr);
    bb->add_instruction_before_inst(vecPtr, pack.instrs[0]);
    bb->remove_instr(vecLoad);
    bb->add_instruction_before_inst(vecLoad, pack.instrs[0]);

    pack.vecValue = vecLoad;

    for (auto *ld : pack.instrs)
        bb->delete_instr(ld);
    pack.emitted = true;
}

// ── Emit: vector store ────────────────────────────────────────────────

void SLPVectorize::emitVectorStore(BasicBlock *bb, Pack &pack, Module *module)
{
    const bool debug = std::getenv("DEBUG_SLP_VECTORIZE") != nullptr;
    if ((int)pack.instrs.size() != VF) return;

    if (!pack.vecValue) {
        std::map<Instruction *, size_t> order;
        size_t position = 0;
        for (auto *inst : bb->instr_list_)
            order[inst] = position++;
        auto firstIt = order.find(pack.instrs[0]);
        if (firstIt == order.end())
            return;
        for (auto *store : pack.instrs) {
            auto *def = dynamic_cast<Instruction *>(store->get_operand(0));
            if (!def || def->parent_ != bb)
                continue;
            auto defIt = order.find(def);
            if (defIt == order.end() || defIt->second >= firstIt->second)
                return;
        }
    }

    // Verify contiguous stores with same base
    Value *basePtr = nullptr;
    int baseOffset = -1;
    for (size_t i = 0; i < pack.instrs.size(); ++i) {
        Value *ptr = pack.instrs[i]->get_operand(1);
        auto *gep = dynamic_cast<GetElementPtrInst*>(ptr);
        if (!gep) return;

        Value *gepBase = gep->get_operand(0);
        unsigned last = gep->num_ops_ - 1;
        auto *ci = dynamic_cast<ConstantInt*>(gep->get_operand(last));
        if (!ci) return;

        if (i == 0) { basePtr = gepBase; baseOffset = ci->value_; }
        else if (gepBase != basePtr || ci->value_ != baseOffset + (int)i) return;

        auto *firstGep = dynamic_cast<GetElementPtrInst*>(
            pack.instrs[0]->get_operand(1));
        for (unsigned k = 1; k < last; ++k) {
            Value *a = gep->get_operand(k);
            Value *b = firstGep->get_operand(k);
            if (a != b) {
                auto *ca = dynamic_cast<ConstantInt*>(a);
                auto *cb = dynamic_cast<ConstantInt*>(b);
                if (!ca || !cb || ca->value_ != cb->value_) return;
            }
        }
    }

    Type *scalarTy = pack.instrs[0]->get_operand(0)->type_;
    if (scalarTy->tid_ != Type::IntegerTyID &&
        scalarTy->tid_ != Type::FloatTyID) return;

    if (debug)
        std::cerr << "[SLP] emit vector store, offset=" << baseOffset << "\n";

    // Build vector value: use pack's vecValue if already set by
    // an upstream emitVectorBinary, otherwise pack scalar values
    Value *vecVal = pack.vecValue;
    if (!vecVal) {
        Type *vecTy = module->get_vector_type(scalarTy, VF);
        for (int lane = 0; lane < VF; ++lane) {
            auto *laneIdx = new ConstantInt(module->int32_ty_, lane);
            Value *base = vecVal ? vecVal : pack.instrs[lane]->get_operand(0);
            auto *ins = new InsertElementInst(base,
                pack.instrs[lane]->get_operand(0), laneIdx, bb);
            if (lane == 0) ins->type_ = vecTy;
            vecVal = ins;
            bb->remove_instr(ins);
            bb->add_instruction_before_inst(ins, pack.instrs[0]);
        }
        pack.vecValue = vecVal;
    }

    auto *firstGep = dynamic_cast<GetElementPtrInst*>(
        pack.instrs[0]->get_operand(1));
    std::vector<Value*> idxs;
    for (unsigned k = 1; k < firstGep->num_ops_; ++k)
        idxs.push_back(firstGep->get_operand(k));
    idxs.back() = new ConstantInt(module->int32_ty_, baseOffset);

    auto *vecGep = new GetElementPtrInst(firstGep->get_operand(0), idxs, bb);
    auto *vecPtr = new Bitcast(Instruction::BitCast, vecGep,
        module->get_pointer_type(vecVal->type_), bb);
    auto *vecStore = new StoreInst(vecVal, vecPtr, bb);

    bb->remove_instr(vecGep);
    bb->add_instruction_before_inst(vecGep, pack.instrs[0]);
    bb->remove_instr(vecPtr);
    bb->add_instruction_before_inst(vecPtr, pack.instrs[0]);
    bb->remove_instr(vecStore);
    bb->add_instruction_before_inst(vecStore, pack.instrs[0]);

    for (auto *store : pack.instrs)
        bb->delete_instr(store);
    pack.emitted = true;
}

// ── Emit: vector binary operation ─────────────────────────────────────

void SLPVectorize::emitVectorBinary(BasicBlock *bb, Pack &pack,
                                     Module *module, PackSet &packs)
{
    const bool debug = std::getenv("DEBUG_SLP_VECTORIZE") != nullptr;
    if ((int)pack.instrs.size() != VF) return;

    auto *firstBin = dynamic_cast<BinaryInst*>(pack.instrs[0]);
    if (!firstBin) return;

    // New vector instructions are inserted before the first scalar lane.
    // Require that point to dominate every scalar operand and every other
    // lane; otherwise packing interleaved definitions would create same-block
    // uses before their definitions.
    std::map<Instruction *, size_t> order;
    size_t position = 0;
    for (auto *inst : bb->instr_list_)
        order[inst] = position++;
    auto firstIt = order.find(firstBin);
    if (firstIt == order.end())
        return;
    std::set<Instruction *> laneSet(pack.instrs.begin(), pack.instrs.end());
    for (auto *lane : pack.instrs) {
        auto laneIt = order.find(lane);
        if (laneIt == order.end() || laneIt->second < firstIt->second)
            return;
        for (unsigned op = 0; op < lane->num_ops_; ++op) {
            auto *def = dynamic_cast<Instruction *>(lane->get_operand(op));
            if (!def || def->parent_ != bb || laneSet.count(def))
                continue;
            auto defIt = order.find(def);
            if (defIt == order.end() || defIt->second >= firstIt->second)
                return;
        }
    }

    // Gather operands per lane
    int numOps = firstBin->num_ops_;
    Type *scalarTy = firstBin->type_;
    if (scalarTy->tid_ != Type::IntegerTyID &&
        scalarTy->tid_ != Type::FloatTyID) return;

    Type *vecTy = module->get_vector_type(scalarTy, VF);
    std::vector<Value*> vecOperands(numOps, nullptr);

    for (unsigned opIdx = 0; opIdx < (unsigned)numOps; ++opIdx) {
        Value *op0 = pack.instrs[0]->get_operand(opIdx);

        // Check if all lanes have the SAME operand (loop-invariant)
        bool allSame = true;
        for (size_t lane = 1; lane < pack.instrs.size(); ++lane) {
            Value *opLane = pack.instrs[lane]->get_operand(opIdx);
            if (opLane != op0) {
                // Also check constant equality
                auto *c0 = dynamic_cast<ConstantInt*>(op0);
                auto *cL = dynamic_cast<ConstantInt*>(opLane);
                if (!c0 || !cL || c0->value_ != cL->value_) {
                    allSame = false;
                    break;
                }
            }
        }

        if (allSame) {
            // Splat the invariant value
            Value *splat = nullptr;
            for (int lane = 0; lane < VF; ++lane) {
                auto *idx = new ConstantInt(module->int32_ty_, lane);
                Value *base = splat ? splat : op0;
                auto *ins = new InsertElementInst(base, op0, idx, bb);
                if (lane == 0) ins->type_ = vecTy;
                splat = ins;
                bb->remove_instr(ins);
                bb->add_instruction_before_inst(ins, pack.instrs[0]);
            }
            vecOperands[opIdx] = splat;
        } else {
            // Check if lane values come from a previously emitted pack
            Instruction *srcPackFirst = dynamic_cast<Instruction*>(op0);
            if (srcPackFirst && srcPackFirst != firstBin) {
                // Find a pack containing srcPackFirst
                for (auto &other : packs.packs) {
                    if (!other.emitted) continue;
                    if (other.instrs[0] == srcPackFirst) {
                        bool allMatch = true;
                        for (size_t lane = 1; lane < pack.instrs.size(); ++lane) {
                            if (pack.instrs[lane]->get_operand(opIdx) !=
                                other.instrs[lane]) {
                                allMatch = false; break;
                            }
                        }
                        if (allMatch && other.vecValue) {
                            // Check type compatibility
                            if (other.vecValue->type_->tid_ == Type::VectorTyID)
                                { vecOperands[opIdx] = other.vecValue; break; }
                        }
                    }
                }
            }

            if (!vecOperands[opIdx]) {
                // Fallback: pack scalar operands lane by lane
                for (int lane = 0; lane < VF; ++lane) {
                    Value *laneVal = pack.instrs[lane]->get_operand(opIdx);
                    auto *idx = new ConstantInt(module->int32_ty_, lane);
                    Value *base = vecOperands[opIdx] ? vecOperands[opIdx] : laneVal;
                    auto *ins = new InsertElementInst(base, laneVal, idx, bb);
                    if (lane == 0) ins->type_ = vecTy;
                    vecOperands[opIdx] = ins;
                    bb->remove_instr(ins);
                    bb->add_instruction_before_inst(ins, pack.instrs[0]);
                }
            }
        }
    }

    if (debug)
        std::cerr << "[SLP] emit vector binary " << firstBin->name_ << "\n";

    // Create the vector binary operation
    auto *vecBin = new BinaryInst(vecTy, firstBin->op_id_,
        vecOperands[0], vecOperands[1], bb);
    bb->remove_instr(vecBin);
    bb->add_instruction_before_inst(vecBin, pack.instrs[0]);
    pack.vecValue = vecBin;

    // Delete original scalar instructions
    for (auto *inst : pack.instrs)
        bb->delete_instr(inst);
    pack.emitted = true;
}

// =====================================================================
// Helpers
// =====================================================================

bool SLPVectorize::isIsomorphic(Instruction *a, Instruction *b) {
    if (a->op_id_ != b->op_id_) return false;
    if (a->num_ops_ != b->num_ops_) return false;

    if (a->is_store()) {
        Value *va = a->get_operand(0);
        Value *vb = b->get_operand(0);
        if (va->type_->tid_ != vb->type_->tid_) return false;
    }
    if (a->is_load()) {
        if (a->type_->tid_ != b->type_->tid_) return false;
    }
    return true;
}

bool SLPVectorize::isIndependent(Instruction *a, Instruction *b) {
    for (auto &use : a->use_list_)
        if (use.val_ == b) return false;
    for (auto &use : b->use_list_)
        if (use.val_ == a) return false;
    for (unsigned i = 0; i < a->num_ops_; ++i)
        if (a->get_operand(i) == b) return false;
    for (unsigned i = 0; i < b->num_ops_; ++i)
        if (b->get_operand(i) == a) return false;
    return true;
}

bool SLPVectorize::isVectorizable(Instruction *inst) {
    if (inst->is_load() || inst->is_store()) return true;
    if (auto *bi = dynamic_cast<BinaryInst*>(inst)) {
        if (bi->is_add() || bi->is_sub() || bi->is_mul()) return true;
        if (bi->is_fadd() || bi->is_fsub() || bi->is_fmul()) return true;
        if (bi->op_id_ == Instruction::Shl ||
            bi->op_id_ == Instruction::AShr ||
            bi->op_id_ == Instruction::LShr ||
            bi->op_id_ == Instruction::And ||
            bi->op_id_ == Instruction::Or ||
            bi->op_id_ == Instruction::Xor) return true;
    }
    return false;
}

bool SLPVectorize::isAdjacentStore(Instruction *a, Instruction *b,
                                    Module *module)
{
    if (!a->is_store() || !b->is_store()) return false;
    Value *ptrA = a->get_operand(1), *ptrB = b->get_operand(1);
    auto *gepA = dynamic_cast<GetElementPtrInst*>(ptrA);
    auto *gepB = dynamic_cast<GetElementPtrInst*>(ptrB);
    if (!gepA || !gepB) return false;
    if (gepA->num_ops_ != gepB->num_ops_) return false;
    if (gepA->get_operand(0) != gepB->get_operand(0)) return false;

    for (unsigned i = 1; i < gepA->num_ops_ - 1; ++i) {
        Value *aOp = gepA->get_operand(i);
        Value *bOp = gepB->get_operand(i);
        if (aOp != bOp) {
            auto *ciA = dynamic_cast<ConstantInt*>(aOp);
            auto *ciB = dynamic_cast<ConstantInt*>(bOp);
            if (!ciA || !ciB || ciA->value_ != ciB->value_)
                return false;
        }
    }

    unsigned lastA = gepA->num_ops_ - 1;
    unsigned lastB = gepB->num_ops_ - 1;
    auto *ciA = dynamic_cast<ConstantInt*>(gepA->get_operand(lastA));
    auto *ciB = dynamic_cast<ConstantInt*>(gepB->get_operand(lastB));
    if (!ciA || !ciB) return false;
    return (ciB->value_ - ciA->value_) == 1;
}

bool SLPVectorize::isAdjacentLoad(Instruction *a, Instruction *b,
                                   Module *module)
{
    if (!a->is_load() || !b->is_load()) return false;
    Value *ptrA = a->get_operand(0), *ptrB = b->get_operand(0);
    auto *gepA = dynamic_cast<GetElementPtrInst*>(ptrA);
    auto *gepB = dynamic_cast<GetElementPtrInst*>(ptrB);
    if (!gepA || !gepB) return false;
    if (gepA->num_ops_ != gepB->num_ops_) return false;
    if (gepA->get_operand(0) != gepB->get_operand(0)) return false;

    for (unsigned i = 1; i < gepA->num_ops_ - 1; ++i) {
        Value *aOp = gepA->get_operand(i);
        Value *bOp = gepB->get_operand(i);
        if (aOp != bOp) {
            auto *ciA = dynamic_cast<ConstantInt*>(aOp);
            auto *ciB = dynamic_cast<ConstantInt*>(bOp);
            if (!ciA || !ciB || ciA->value_ != ciB->value_)
                return false;
        }
    }

    unsigned lastA = gepA->num_ops_ - 1;
    unsigned lastB = gepB->num_ops_ - 1;
    auto *ciA = dynamic_cast<ConstantInt*>(gepA->get_operand(lastA));
    auto *ciB = dynamic_cast<ConstantInt*>(gepB->get_operand(lastB));
    if (!ciA || !ciB) return false;
    return (ciB->value_ - ciA->value_) == 1;
}

Value *SLPVectorize::getStoredValue(Instruction *store) {
    return store->is_store() ? store->get_operand(0) : nullptr;
}

Value *SLPVectorize::getStorePointer(Instruction *store) {
    return store->is_store() ? store->get_operand(1) : nullptr;
}
