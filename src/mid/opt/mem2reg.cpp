// 典型示例：
//   优化前：%p = alloca i32；then 中 store 1, %p；else 中 store 2, %p；
//           merge 中 %x = load %p。
//   优化后：merge 中 %x = phi [1, %then], [2, %else]，并删除 %p 及其 load/store。
// 该 Pass 面向地址未逃逸的函数局部 alloca，在整张 CFG 上依据支配边界插入 PHI，
// 再通过 SSA 重命名把每次读取连接到对应路径上的定义，最终消除这组栈内存访问。

#include "../../include/mid/opt/mem2reg.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <queue>
#include <stack>
#include <unordered_set>

// Mem2Reg 将只被直接 load/store 使用的局部栈槽提升为 SSA 值。
// 流程包含小数组标量拆分、三类快速提升、最小 PHI 放置和沿支配树的重命名。

// 兼容旧式入口：创建临时 AnalysisManager 后运行提升。
void Mem2Reg::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

// 新式入口；变换不会改 CFG，因此成功后保留支配树等 CFG 分析。
PreservedAnalyses Mem2Reg::execute(Module *module, AnalysisManager &AM) {
    return run(module, AM) ? PreservedAnalyses::cfgAnalyses()
                           : PreservedAnalyses::all();
}

// 遍历模块中的函数，跳过仅有声明的外部函数。
bool Mem2Reg::run(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func, AM);
    }
    return changed;
}

// 完成单个函数的完整提升流水线：SROA、快速路径、PHI 放置、rename 和延迟删除。
bool Mem2Reg::runOnFunction(Function *func, AnalysisManager &AM) {
    if (func->basic_blocks_.empty()) return false;
    resetFunctionState(func);
    if (!entryBlock_->pre_bbs_.empty()) return false;

    // 先把常量下标数组拆成标量槽，后续逻辑即可统一按普通 alloca 处理。
    bool changed = runScalarReplacement();

    domTree_ = &AM.getDominatorTree(func);
    domFrontier_ = &AM.getDominanceFrontier(func);
    collectPromotableAllocas();

    // 快速路径处理完的槽从 allocas_ 中压缩移除，剩余槽进入通用 SSA 构造。
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

// 清理上一函数的临时状态，并记录本函数入口块。
void Mem2Reg::resetFunctionState(Function *func) {
    currentFunc_ = func;
    entryBlock_ = func->basic_blocks_.front();
    domTree_ = nullptr;
    domFrontier_ = nullptr;
    allocas_.clear();
    phiOwners_.clear();
    toDelete_.clear();
}

// 收集地址未逃逸的 alloca；合法 use 仅允许直接 load 和以该槽为目的地址的 store。
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
                auto *user = use.user_;
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

// 按成本从低到高尝试无读取、单次写入和单块三类快速路径。
bool Mem2Reg::tryPromoteTrivialAlloca(AllocaInfo &info) {
    return removeUnusedAlloca(info) ||
           rewriteSingleStoreAlloca(info) ||
           promoteSingleBlockAlloca(info);
}

// 没有 load 的槽对程序结果无贡献，标记其 store 与 alloca 待删除。
bool Mem2Reg::removeUnusedAlloca(AllocaInfo &info) {
    if (!info.loads.empty()) return false;

    for (auto *store : info.stores)
        toDelete_.insert(store);
    toDelete_.insert(info.alloca);
    return true;
}

// 单 store 支配全部 load 时，直接用写入值替换所有读取。
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

// 单块内按指令顺序维护最近一次 store，将每个 load 替换为当前值。
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

// 根据活跃入口与迭代支配边界放置最小 PHI 集合，并记录每个 PHI 所属的 alloca。
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

    // 从“首个访问是 load”的块向前传播 live-in，遇到本地定义即停止。
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

    // 从定义块遍历迭代支配边界，只在变量 live-in 的汇合块真正放置 PHI。
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

        for (auto *frontier : domFrontier_->getFrontier(bb)) {
            if (visitedFrontiers.count(frontier)) continue;
            visitedFrontiers.insert(frontier);

            if (info.liveInBlocks.count(frontier))
                info.phiBlocks.insert(frontier);
            if (!defSet.count(frontier))
                defWorklist.push(frontier);
        }
    }

    for (auto *bb : info.phiBlocks) {
        auto *phi = PhiInst::create_phi(info.alloca->allocated_type(), bb);
        bb->add_instruction_front(phi);
        phiOwners_[phi] = info.alloca;
    }
}

// 判断块内第一次 load 之前是否已有 store，用于计算变量在块入口处是否活跃。
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

// 沿支配树执行 SSA rename；每个 alloca 的值栈表示当前支配路径上的最新定义。
void Mem2Reg::renamePromotedAllocas() {
    std::map<AllocaInst *, std::stack<Value *>> valueStacks;
    for (auto &info : allocas_) {
        if (Value *zero = zeroValueFor(info.alloca->allocated_type()))
            valueStacks[info.alloca].push(zero);
    }

    std::function<void(BasicBlock *)> renameBlock = [&](BasicBlock *bb) {
        // 保存入块时的栈深，递归返回后恢复，隔离不同支配树分支的定义。
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

        // 当前栈顶就是从 bb 流向每个后继 PHI 的 incoming value。
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

        for (auto *child : domTree_->getChildren(bb))
            renameBlock(child->block());

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

// 查询单 store 快速路径所需的指令级支配关系。
bool Mem2Reg::instructionDominates(Instruction *def, Instruction *use) const {
    return domTree_ && domTree_->dominates(def, use);
}

// 为尚未出现显式定义的路径构造源语言默认零值。
Value *Mem2Reg::zeroValueFor(Type *ty) const {
    if (ty->tid_ == Type::IntegerTyID)
        return new ConstantInt(ty, 0);
    if (ty->tid_ == Type::FloatTyID)
        return new ConstantFloat(ty, 0.0f);
    if (ty->tid_ == Type::VectorTyID)
        return new ConstantZero(ty);
    return nullptr;
}

// 统一删除已完成替换的 alloca/load/store/GEP，保持前面遍历过程稳定。
void Mem2Reg::eraseMarkedInstructions() {
    for (auto *inst : toDelete_) {
        if (!inst->parent_) continue;
        inst->parent_->delete_instr(inst);
    }
}

// 对满足条件的小数组执行标量替换，为每组常量下标创建独立 alloca。
bool Mem2Reg::runScalarReplacement() {
    std::vector<AllocaInst *> candidates;
    for (auto *bb : currentFunc_->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (inst->op_id_ != Instruction::Alloca) continue;

            auto *alloca = static_cast<AllocaInst *>(inst);
            if (alloca->allocated_type()->tid_ != Type::ArrayTyID) continue;
            if (isScalarReplacementCandidate(alloca))
                candidates.push_back(alloca);
        }
    }

    for (auto *alloca : candidates)
        rewriteAlloca(alloca);
    return !candidates.empty();
}

// 验证数组槽的全部 use 都是常量下标 GEP，且 GEP 结果只被标量 load/store 使用。
bool Mem2Reg::isScalarReplacementCandidate(AllocaInst *alloca) {
    for (auto &use : alloca->use_list_) {
        auto *user = use.user_;
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
            auto *gepUser = gepUse.user_;
            if (!gepUser) return false;
            if (gepUser->op_id_ != Instruction::Load &&
                gepUser->op_id_ != Instruction::Store)
                return false;
        }
    }

    return true;
}

// 按下标元组复用标量槽，并把原 GEP 的所有 use 改接到对应槽。
void Mem2Reg::rewriteAlloca(AllocaInst *alloca) {
    BasicBlock *entryBB = currentFunc_->basic_blocks_.front();
    std::map<std::vector<int>, AllocaInst *> scalarSlots;

    auto allocaUses = alloca->use_list_;
    for (auto &use : allocaUses) {
        auto *gep = static_cast<GetElementPtrInst *>(use.user_);

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
            auto *gepUser = gepUse.user_;
            gepUser->set_operand(gepUse.operand_index_, scalarAlloca);
        }
        toDelete_.insert(gep);
    }

    toDelete_.insert(alloca);
}

// 从 GEP 的“类型、索引”操作数对中提取全部常量索引。
bool Mem2Reg::getConstantIndices(GetElementPtrInst *gep,
                                 std::vector<int> &indices) {
    for (unsigned i = 2; i < gep->num_ops(); i += 2) {
        auto *idx = dynamic_cast<ConstantInt *>(gep->get_operand(i));
        if (!idx) return false;
        indices.push_back(static_cast<int>(idx->value_));
    }
    return true;
}

// 标量替换当前只拆分整数和浮点元素。
bool Mem2Reg::isScalarType(Type *ty) {
    return ty->tid_ == Type::IntegerTyID || ty->tid_ == Type::FloatTyID;
}
