#include "../../include/mid/opt/mem2reg.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <queue>
#include <stack>
#include <unordered_set>

void Mem2Reg::execute(Module *module) {
    run(module);
}

PreservedAnalyses Mem2Reg::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    return run(module) ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool Mem2Reg::run(Module *module) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func);
    }
    return changed;
}

bool Mem2Reg::runOnFunction(Function *func) {
    if (func->basic_blocks_.empty()) return false;
    resetFunctionState(func);
    if (!entryBlock_->pre_bbs_.empty()) return false;

    bool changed = runScalarReplacement();

    domInfo_ = &func->getDominatorInfo();
    collectPromotableAllocas();

    size_t kept = 0;
    for (size_t i = 0; i < allocas_.size(); ++i) {
        auto &info = allocas_[i];
        if (tryPromoteTrivialAlloca(info)) {
            changed = true;
            continue;
        }

        placePhiNodes(info);
        if (kept != i)
            allocas_[kept] = std::move(allocas_[i]);
        ++kept;
    }
    allocas_.erase(allocas_.begin() + kept, allocas_.end());

    if (!allocas_.empty()) {
        renamePromotedAllocas();
        changed = true;
    }

    if (!toDelete_.empty()) {
        eraseMarkedInstructions();
        changed = true;
    }

    return changed;
}

void Mem2Reg::resetFunctionState(Function *func) {
    currentFunc_ = func;
    entryBlock_ = func->basic_blocks_.front();
    domInfo_ = nullptr;
    allocas_.clear();
    phiOwners_.clear();
    toDelete_.clear();
}

void Mem2Reg::collectPromotableAllocas() {
    allocas_.clear();

    for (auto *bb : currentFunc_->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (inst->op_id_ != Instruction::Alloca) continue;

            auto *alloca = static_cast<AllocaInst *>(inst);
            AllocaInfo info;
            info.alloca = alloca;
            bool promotable = true;

            for (auto &use : alloca->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (!user) {
                    promotable = false;
                    break;
                }

                if (user->op_id_ == Instruction::Load) {
                    auto *load = static_cast<LoadInst *>(user);
                    info.loads.push_back(load);
                    info.userBlocks[load->parent_].loads.push_back(load);
                    continue;
                }

                if (user->op_id_ == Instruction::Store) {
                    auto *store = static_cast<StoreInst *>(user);
                    if (store->get_operand(1) != alloca) {
                        promotable = false;
                        break;
                    }
                    info.stores.push_back(store);
                    info.userBlocks[store->parent_].stores.push_back(store);
                    continue;
                }

                promotable = false;
                break;
            }

            if (promotable)
                allocas_.push_back(std::move(info));
        }
    }
}

bool Mem2Reg::tryPromoteTrivialAlloca(AllocaInfo &info) {
    return removeUnusedAlloca(info) ||
           rewriteSingleStoreAlloca(info) ||
           promoteSingleBlockAlloca(info);
}

bool Mem2Reg::removeUnusedAlloca(AllocaInfo &info) {
    if (!info.loads.empty()) return false;

    for (auto *store : info.stores)
        toDelete_.insert(store);
    toDelete_.insert(info.alloca);
    return true;
}

bool Mem2Reg::rewriteSingleStoreAlloca(AllocaInfo &info) {
    if (info.stores.size() != 1) return false;

    auto *store = info.stores.front();
    Value *storedValue = store->get_operand(0);
    for (auto *load : info.loads) {
        if (!instructionDominates(store, load))
            return false;
        load->replace_all_use_with(storedValue);
        toDelete_.insert(load);
    }

    toDelete_.insert(store);
    toDelete_.insert(info.alloca);
    return true;
}

bool Mem2Reg::promoteSingleBlockAlloca(AllocaInfo &info) {
    if (info.userBlocks.size() != 1) return false;

    BasicBlock *bb = info.userBlocks.begin()->first;
    auto &uses = info.userBlocks.begin()->second;
    std::unordered_set<Instruction *> stores(uses.stores.begin(),
                                             uses.stores.end());
    std::unordered_set<Instruction *> loads(uses.loads.begin(),
                                            uses.loads.end());

    StoreInst *lastStore = nullptr;
    for (auto *inst : bb->instr_list_) {
        if (stores.count(inst)) {
            lastStore = static_cast<StoreInst *>(inst);
            continue;
        }

        if (!loads.count(inst)) continue;
        if (!lastStore) return false;

        auto *load = static_cast<LoadInst *>(inst);
        load->replace_all_use_with(lastStore->get_operand(0));
        toDelete_.insert(load);
    }

    for (auto *store : uses.stores)
        toDelete_.insert(store);
    toDelete_.insert(info.alloca);
    return true;
}

void Mem2Reg::placePhiNodes(AllocaInfo &info) {
    info.defBlocks.clear();
    info.liveInBlocks.clear();

    for (auto &entry : info.userBlocks) {
        BasicBlock *bb = entry.first;
        const BlockInfo &uses = entry.second;

        if (!uses.stores.empty())
            info.defBlocks.push_back(bb);
        if (!uses.loads.empty() && !hasStoreBeforeFirstLoad(uses, bb))
            info.liveInBlocks.insert(bb);
    }

    std::queue<BasicBlock *> liveInWorklist;
    for (auto *bb : info.liveInBlocks)
        liveInWorklist.push(bb);

    while (!liveInWorklist.empty()) {
        BasicBlock *bb = liveInWorklist.front();
        liveInWorklist.pop();

        for (auto *pred : bb->pre_bbs_) {
            if (info.liveInBlocks.count(pred)) continue;
            auto it = info.userBlocks.find(pred);
            if (it != info.userBlocks.end() && !it->second.stores.empty())
                continue;

            info.liveInBlocks.insert(pred);
            liveInWorklist.push(pred);
        }
    }

    std::set<BasicBlock *> defSet(info.defBlocks.begin(),
                                  info.defBlocks.end());
    std::set<BasicBlock *> visitedFrontiers;
    std::queue<BasicBlock *> defWorklist;
    for (auto *bb : info.defBlocks)
        defWorklist.push(bb);

    info.phiBlocks.clear();
    while (!defWorklist.empty()) {
        BasicBlock *bb = defWorklist.front();
        defWorklist.pop();

        auto frontierIt = domInfo_->domFront.find(bb);
        if (frontierIt == domInfo_->domFront.end()) continue;

        for (auto *frontier : frontierIt->second) {
            if (visitedFrontiers.count(frontier)) continue;
            visitedFrontiers.insert(frontier);

            if (info.liveInBlocks.count(frontier))
                info.phiBlocks.insert(frontier);
            if (!defSet.count(frontier))
                defWorklist.push(frontier);
        }
    }

    for (auto *bb : info.phiBlocks) {
        auto *phi = PhiInst::create_phi(info.alloca->alloca_ty_, bb);
        bb->add_instruction_front(phi);
        phiOwners_[phi] = info.alloca;
    }
}

bool Mem2Reg::hasStoreBeforeFirstLoad(const BlockInfo &blockInfo,
                                      BasicBlock *bb) const {
    if (blockInfo.stores.empty()) return false;

    bool seenStore = false;
    for (auto *inst : bb->instr_list_) {
        if (std::find(blockInfo.stores.begin(), blockInfo.stores.end(), inst) !=
            blockInfo.stores.end()) {
            seenStore = true;
        }
        if (std::find(blockInfo.loads.begin(), blockInfo.loads.end(), inst) !=
            blockInfo.loads.end()) {
            return seenStore;
        }
    }

    return true;
}

void Mem2Reg::renamePromotedAllocas() {
    std::map<AllocaInst *, std::stack<Value *>> valueStacks;
    for (auto &info : allocas_) {
        if (Value *zero = zeroValueFor(info.alloca->alloca_ty_))
            valueStacks[info.alloca].push(zero);
    }

    std::function<void(BasicBlock *)> renameBlock = [&](BasicBlock *bb) {
        std::map<AllocaInst *, size_t> savedDepths;
        for (auto &entry : valueStacks)
            savedDepths[entry.first] = entry.second.size();

        for (auto *inst : bb->instr_list_) {
            if (inst->op_id_ != Instruction::PHI) break;
            auto *phi = static_cast<PhiInst *>(inst);
            auto owner = phiOwners_.find(phi);
            if (owner != phiOwners_.end())
                valueStacks[owner->second].push(phi);
        }

        for (auto *inst : bb->instr_list_) {
            if (toDelete_.count(inst)) continue;

            if (inst->op_id_ == Instruction::Load) {
                auto *load = static_cast<LoadInst *>(inst);
                auto *alloca =
                    dynamic_cast<AllocaInst *>(load->get_operand(0));
                auto stackIt = valueStacks.find(alloca);
                if (!alloca || stackIt == valueStacks.end()) continue;

                load->replace_all_use_with(stackIt->second.top());
                toDelete_.insert(load);
                continue;
            }

            if (inst->op_id_ == Instruction::Store) {
                auto *store = static_cast<StoreInst *>(inst);
                auto *alloca =
                    dynamic_cast<AllocaInst *>(store->get_operand(1));
                auto stackIt = valueStacks.find(alloca);
                if (!alloca || stackIt == valueStacks.end()) continue;

                stackIt->second.push(store->get_operand(0));
                toDelete_.insert(store);
            }
        }

        for (auto *succ : bb->succ_bbs_) {
            for (auto *inst : succ->instr_list_) {
                if (inst->op_id_ != Instruction::PHI) break;
                auto *phi = static_cast<PhiInst *>(inst);
                auto owner = phiOwners_.find(phi);
                if (owner == phiOwners_.end()) continue;

                auto stackIt = valueStacks.find(owner->second);
                if (stackIt != valueStacks.end())
                    phi->addIncoming(stackIt->second.top(), bb);
            }
        }

        auto children = domInfo_->domChildren.find(bb);
        if (children != domInfo_->domChildren.end()) {
            for (auto *child : children->second)
                renameBlock(child);
        }

        for (auto &entry : savedDepths) {
            auto &stack = valueStacks[entry.first];
            while (stack.size() > entry.second)
                stack.pop();
        }
    };

    renameBlock(entryBlock_);

    for (auto &info : allocas_) {
        if (valueStacks.count(info.alloca))
            toDelete_.insert(info.alloca);
    }
}

bool Mem2Reg::instructionDominates(Instruction *def, Instruction *use) const {
    BasicBlock *defBB = def->parent_;
    BasicBlock *useBB = use->parent_;

    if (defBB != useBB)
        return domInfo_->dominates(defBB, useBB);

    bool sawDef = false;
    for (auto *inst : defBB->instr_list_) {
        if (inst == def) sawDef = true;
        if (inst == use) return sawDef;
    }
    return false;
}

Value *Mem2Reg::zeroValueFor(Type *ty) const {
    if (ty->tid_ == Type::IntegerTyID)
        return new ConstantInt(ty, 0);
    if (ty->tid_ == Type::FloatTyID)
        return new ConstantFloat(ty, 0.0f);
    if (ty->tid_ == Type::VectorTyID)
        return new ConstantZero(ty);
    return nullptr;
}

void Mem2Reg::eraseMarkedInstructions() {
    for (auto *inst : toDelete_) {
        if (!inst->parent_) continue;
        inst->parent_->delete_instr(inst);
    }
}

bool Mem2Reg::runScalarReplacement() {
    std::vector<AllocaInst *> candidates;
    for (auto *bb : currentFunc_->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (inst->op_id_ != Instruction::Alloca) continue;

            auto *alloca = static_cast<AllocaInst *>(inst);
            if (alloca->alloca_ty_->tid_ != Type::ArrayTyID) continue;
            if (isScalarReplacementCandidate(alloca))
                candidates.push_back(alloca);
        }
    }

    for (auto *alloca : candidates)
        rewriteAlloca(alloca);
    return !candidates.empty();
}

bool Mem2Reg::isScalarReplacementCandidate(AllocaInst *alloca) {
    for (auto &use : alloca->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || user->op_id_ != Instruction::GetElementPtr)
            return false;

        auto *gep = static_cast<GetElementPtrInst *>(user);
        assert(gep->type_->tid_ == Type::PointerTyID);

        Type *resultTy = static_cast<PointerType *>(gep->type_)->contained_;
        if (!isScalarType(resultTy))
            return false;

        std::vector<int> indices;
        if (!getConstantIndices(gep, indices))
            return false;

        for (auto &gepUse : gep->use_list_) {
            auto *gepUser = dynamic_cast<Instruction *>(gepUse.val_);
            if (!gepUser) return false;
            if (gepUser->op_id_ != Instruction::Load &&
                gepUser->op_id_ != Instruction::Store)
                return false;
        }
    }

    return true;
}

void Mem2Reg::rewriteAlloca(AllocaInst *alloca) {
    BasicBlock *entryBB = currentFunc_->basic_blocks_.front();
    std::map<std::vector<int>, AllocaInst *> scalarSlots;

    auto allocaUses = alloca->use_list_;
    for (auto &use : allocaUses) {
        auto *gep = static_cast<GetElementPtrInst *>(use.val_);

        std::vector<int> indices;
        bool ok = getConstantIndices(gep, indices);
        assert(ok && "SROA candidate should only contain constant indices");
        (void)ok;

        auto slot = scalarSlots.find(indices);
        if (slot == scalarSlots.end()) {
            Type *scalarTy = static_cast<PointerType *>(gep->type_)->contained_;
            auto *newAlloca = new AllocaInst(scalarTy, entryBB, true);
            entryBB->add_instruction_front(newAlloca);
            slot = scalarSlots.emplace(indices, newAlloca).first;
        }

        AllocaInst *scalarAlloca = slot->second;
        auto gepUses = gep->use_list_;
        for (auto &gepUse : gepUses) {
            auto *gepUser = dynamic_cast<Instruction *>(gepUse.val_);
            gepUser->set_operand(gepUse.arg_no_, scalarAlloca);
        }
        toDelete_.insert(gep);
    }

    toDelete_.insert(alloca);
}

bool Mem2Reg::getConstantIndices(GetElementPtrInst *gep,
                                 std::vector<int> &indices) {
    for (unsigned i = 2; i < gep->num_ops_; i += 2) {
        auto *idx = dynamic_cast<ConstantInt *>(gep->get_operand(i));
        if (!idx) return false;
        indices.push_back(static_cast<int>(idx->value_));
    }
    return true;
}

bool Mem2Reg::isScalarType(Type *ty) {
    return ty->tid_ == Type::IntegerTyID || ty->tid_ == Type::FloatTyID;
}
