#include "../../include/mid/opt/mem2reg.hpp"
#include <algorithm>
#include <cassert>
#include <queue>
#include <stack>
#include <functional>

void Mem2Reg::execute(Module *module) {
    curModule = module;
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func);
    }
}

// -----------------------------------------------------------------------
// 支配树计算
// -----------------------------------------------------------------------
void Mem2Reg::computeDominators(Function *func) {
    std::set<BasicBlock *> allBlocks;
    for (auto bb : func->basic_blocks_) allBlocks.insert(bb);

    // 初始化
    for (auto bb : func->basic_blocks_) {
        if (bb == entryBlock)
            dom[bb] = {entryBlock};
        else
            dom[bb] = allBlocks;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto bb : func->basic_blocks_) {
            if (bb == entryBlock) continue;
            std::set<BasicBlock *> newDom = allBlocks;   // 初始为全集
            bool first = true;
            for (auto pred : bb->pre_bbs_) {
                if (first) {
                    newDom = dom[pred];
                    first = false;
                } else {
                    std::set<BasicBlock *> temp;
                    std::set_intersection(newDom.begin(), newDom.end(),
                                          dom[pred].begin(), dom[pred].end(),
                                          std::inserter(temp, temp.begin()));
                    newDom = temp;
                }
            }
            newDom.insert(bb);
            if (newDom != dom[bb]) {
                dom[bb] = newDom;
                changed = true;
            }
        }
    }

    // 计算 idom
    for (auto bb : func->basic_blocks_) {
        if (bb == entryBlock) {
            idom[bb] = nullptr;
            continue;
        }
        // 在 dom[bb] 中找到严格支配 bb 且不被任何其他严格支配块支配的块
        for (auto d : dom[bb]) {
            if (d == bb) continue;
            bool isIdom = true;
            for (auto other : dom[bb]) {
                if (other == bb || other == d) continue;
                // 若 other 也严格支配 d，则 d 不是立即支配者
                if (dom[d].count(other)) {
                    isIdom = false;
                    break;
                }
            }
            if (isIdom) {
                idom[bb] = d;
                break;
            }
        }
    }
}

void Mem2Reg::computeDominanceFrontiers() {
    for (auto &p : idom) domFront[p.first] = {};
    for (auto &kv : idom) {
        BasicBlock *b = kv.first;
        for (auto pred : b->pre_bbs_) {
            BasicBlock *runner = pred;
            while (runner != idom[b]) {
                domFront[runner].insert(b);
                runner = idom[runner];
            }
        }
    }
}

void Mem2Reg::computeDomTreeChildren() {
    for (auto &kv : idom) {
        if (kv.second)
            domChildren[kv.second].push_back(kv.first);
    }
}

// -----------------------------------------------------------------------
// 指令支配关系（同块比较顺序，跨块用 dom 集合）
// -----------------------------------------------------------------------
bool Mem2Reg::iADomB(Instruction *ia, Instruction *ib) {
    BasicBlock *bbA = ia->parent_;
    BasicBlock *bbB = ib->parent_;
    if (bbA == bbB) {
        // 遍历指令列表，ia 在 ib 之前则支配
        bool foundA = false;
        for (auto inst : bbA->instr_list_) {
            if (inst == ia) foundA = true;
            if (inst == ib) return foundA;
        }
        return false;
    }
    // 跨基本块：bbA 支配 bbB
    return dom[bbB].count(bbA) != 0;
}

// -----------------------------------------------------------------------
// 收集可提升的 alloca
// -----------------------------------------------------------------------
void Mem2Reg::analyseAlloca() {
    allocas.clear();
    for (auto bb : currentFunc->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (inst->op_id_ != Instruction::Alloca) continue;
            AllocaInst *alloca = static_cast<AllocaInst *>(inst);
            AllocaInfo info;
            info.alloca = alloca;
            bool promotable = true;

            for (auto &use : alloca->use_list_) {
                Instruction *user = dynamic_cast<Instruction *>(use.val_);
                if (!user) {
                    promotable = false;
                    break;        // 如果用户不是指令，不可提升
                }

                switch (user->op_id_) {
                    case Instruction::Load: {
                        LoadInst *load = static_cast<LoadInst *>(user);
                        info.loads.push_back(load);
                        info.userBlocks[load->parent_].loads.push_back(load);
                        break;
                    }
                    case Instruction::Store: {
                        StoreInst *store = static_cast<StoreInst *>(user);
                        if (store->get_operand(1) != alloca) {
                            promotable = false;
                            break;
                        }
                        info.stores.push_back(store);
                        info.userBlocks[store->parent_].stores.push_back(store);
                        break;
                    }
                    default:
                        promotable = false;
                        break;
                }
                if (!promotable) break;
            }
            if (promotable) allocas.push_back(info);
        }
    }
}

// -----------------------------------------------------------------------
// 三种简单场景处理
// -----------------------------------------------------------------------
bool Mem2Reg::removeUnusedAlloca(AllocaInfo &info) {
    if (info.loads.empty()) {
        for (auto store : info.stores) toDelete.insert(store);
        toDelete.insert(info.alloca);
        return true;
    }
    return false;
}

bool Mem2Reg::rewriteSingleStoreAlloca(AllocaInfo &info) {
    if (info.stores.size() == 1) {
        StoreInst *store = info.stores[0];
        Value *rval = store->get_operand(0);
        for (auto load : info.loads) {
            if (!iADomB(store, load)) return false; // 无法保证 store 在 load 前
            load->replace_all_use_with(rval);
            toDelete.insert(load);
        }
        toDelete.insert(store);
        toDelete.insert(info.alloca);
        return true;
    }
    return false;
}

bool Mem2Reg::promoteSingleBlockAlloca(AllocaInfo &info) {
    if (info.userBlocks.size() == 1) {
        BasicBlock *bb = info.userBlocks.begin()->first;
        // 将块内指令按原顺序排序 store 和 load
        auto &stores = info.userBlocks[bb].stores;
        auto &loads  = info.userBlocks[bb].loads;
        // 为每条 load 寻找块内最近的前驱 store
        for (auto load : loads) {
            StoreInst *lastStore = nullptr;
            // 遍历指令列表，记录在 load 之前的最后一个 store
            for (auto inst : bb->instr_list_) {
                if (inst == load) break;
                if (std::find(stores.begin(), stores.end(), inst) != stores.end())
                    lastStore = static_cast<StoreInst *>(inst);
            }
            if (!lastStore) return false; // load 前无 store
            Value *rval = lastStore->get_operand(0);
            load->replace_all_use_with(rval);
            toDelete.insert(load);
        }
        for (auto store : stores) toDelete.insert(store);
        toDelete.insert(info.alloca);
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------
// 插入 phi 节点
// -----------------------------------------------------------------------
void Mem2Reg::insertPhiNodes(AllocaInfo &info) {
    // 1. 找出 define 块和有需要 live-in 的块
    info.defBlocks.clear();
    info.liveInBlocks.clear();
    for (auto &p : info.userBlocks) {
        BasicBlock *b = p.first;
        if (!p.second.stores.empty())
            info.defBlocks.push_back(b);
        if (!p.second.loads.empty()) {
            // 如果第一个 load 前没有 store，则需要 live-in
            bool needLiveIn = false;
            if (p.second.stores.empty()) {
                needLiveIn = true;
            } else {
                // 比较第一个 load 和第一个 store 的顺序
                bool seenStore = false;
                for (auto inst : b->instr_list_) {
                    if (std::find(p.second.stores.begin(), p.second.stores.end(), inst) != p.second.stores.end())
                        seenStore = true;
                    if (std::find(p.second.loads.begin(), p.second.loads.end(), inst) != p.second.loads.end()) {
                        if (!seenStore) needLiveIn = true;
                        break;
                    }
                }
            }
            if (needLiveIn) info.liveInBlocks.insert(b);
        }
    }

    // 2. 传播 live-in：若某块需要 live-in，其某些前驱也可能需要，直到遇到 def 块
    std::queue<BasicBlock *> q;
    for (auto b : info.liveInBlocks) q.push(b);
    while (!q.empty()) {
        BasicBlock *b = q.front(); q.pop();
        for (auto pred : b->pre_bbs_) {
            if (info.liveInBlocks.count(pred)) continue;
            if (info.userBlocks.count(pred) && !info.userBlocks[pred].stores.empty())
                continue; // pred 是 define 块，停止
            info.liveInBlocks.insert(pred);
            q.push(pred);
        }
    }

    // 3. 计算 define 块的迭代支配边界，得到 phi 块
    std::set<BasicBlock *> defSet(info.defBlocks.begin(), info.defBlocks.end());
    std::set<BasicBlock *> phiSet;
    std::queue<BasicBlock *> workQ;
    std::set<BasicBlock *> visited;
    for (auto b : info.defBlocks) workQ.push(b);
    while (!workQ.empty()) {
        BasicBlock *b = workQ.front(); workQ.pop();
        // 迭代支配边界
        for (auto f : domFront[b]) {
            if (visited.count(f)) continue;
            visited.insert(f);
            if (info.liveInBlocks.count(f))
                phiSet.insert(f);
            if (!defSet.count(f))
                workQ.push(f);
        }
    }
    info.phiBlocks = phiSet;

    // 4. 在 phi 块开头插入 phi 指令
    for (auto bb : info.phiBlocks) {
        // 修正 2 : 使用 PhiInst::create_phi 创建空 phi，再加入基本块
        auto phi = PhiInst::create_phi(info.alloca->alloca_ty_, bb);
        bb->add_instruction_front(phi);   // 插入到基本块最前面
        phiToAlloca[phi] = info.alloca;
    }
}

// -----------------------------------------------------------------------
// Rename（按支配树先序遍历）
// -----------------------------------------------------------------------
void Mem2Reg::rename() {
    // 每个 alloca 的值栈，初始压入 undef 代表（我们以 0 常量代替）
    std::map<AllocaInst *, std::stack<Value *>> stacks;
    for (auto &info : allocas) {
        Type *ty = info.alloca->alloca_ty_;
        if (ty->tid_ == Type::IntegerTyID) {
            stacks[info.alloca].push(new ConstantInt(ty, 0));
        } else if (ty->tid_ == Type::FloatTyID) {
            stacks[info.alloca].push(new ConstantFloat(ty, 0.0f));
        } else {
            continue;
        }
    }

    // 支配树先序遍历
    std::function<void(BasicBlock *)> renameBlock = [&](BasicBlock *bb) {
        // 记录栈深度，以便退出时恢复
        std::map<AllocaInst *, size_t> savedSizes;
        for (auto &info : allocas)
            savedSizes[info.alloca] = stacks[info.alloca].size();

        // 1. 处理块开头的 phi（它们是新定义）
        for (auto inst : bb->instr_list_) {
            if (inst->op_id_ != Instruction::PHI) break;
            PhiInst *phi = static_cast<PhiInst *>(inst);
            auto it = phiToAlloca.find(phi);
            if (it != phiToAlloca.end())
                stacks[it->second].push(phi);
        }

        // 2. 重写普通 load/store
        for (auto inst : bb->instr_list_) {
            if (toDelete.count(inst)) continue;
            if (inst->op_id_ == Instruction::Load) {
                LoadInst *load = static_cast<LoadInst *>(inst);
                AllocaInst *alloca = dynamic_cast<AllocaInst *>(load->get_operand(0));
                if (!alloca || !stacks.count(alloca)) continue;
                Value *top = stacks[alloca].top();
                load->replace_all_use_with(top);
                toDelete.insert(load);
            } else if (inst->op_id_ == Instruction::Store) {
                StoreInst *store = static_cast<StoreInst *>(inst);
                AllocaInst *alloca = dynamic_cast<AllocaInst *>(store->get_operand(1));
                if (!alloca || !stacks.count(alloca)) continue;
                stacks[alloca].push(store->get_operand(0));
                toDelete.insert(store);
            }
        }

        // 3. 为后继块的 phi 填充参数
        for (auto succ : bb->succ_bbs_) {
            for (auto inst : succ->instr_list_) {
                if (inst->op_id_ != Instruction::PHI) break;
                PhiInst *phi = static_cast<PhiInst *>(inst);
                auto it = phiToAlloca.find(phi);
                if (it != phiToAlloca.end()) {
                    Value *incomingVal = stacks[it->second].top();
                    phi->addIncoming(incomingVal, bb);
                }
            }
        }

        // 4. 递归支配树子节点
        for (auto child : domChildren[bb]) {
            renameBlock(child);
        }

        // 5. 恢复栈
        for (auto &info : allocas) {
            auto &stk = stacks[info.alloca];
            while (stk.size() > savedSizes[info.alloca])
                stk.pop();
        }
    };

    renameBlock(entryBlock);

    // 所有 alloca 加入删除队列
    for (auto &info : allocas)
        toDelete.insert(info.alloca);
}

// -----------------------------------------------------------------------
// 入口
// -----------------------------------------------------------------------
void Mem2Reg::runOnFunction(Function *func) {
    currentFunc = func;
    entryBlock = func->basic_blocks_.front();
    if (entryBlock->pre_bbs_.size() != 0) return; // 入口块不应有前驱

    // 清理旧数据
    dom.clear();
    idom.clear();
    domFront.clear();
    domChildren.clear();
    toDelete.clear();
    phiToAlloca.clear();

    // 支配信息
    computeDominators(func);
    computeDominanceFrontiers();
    computeDomTreeChildren();

    // 收集 alloca
    analyseAlloca();

    // 先处理简单情况
    for (auto it = allocas.begin(); it != allocas.end(); ) {
        auto &info = *it;
        if (removeUnusedAlloca(info) || rewriteSingleStoreAlloca(info) || promoteSingleBlockAlloca(info)) {
            it = allocas.erase(it);
        } else {
            insertPhiNodes(info);
            ++it;
        }
    }

    // 全局重命名
    rename();

    // 物理删除标记的指令
    for (auto inst : toDelete) {
        if (inst->parent_ == nullptr) continue;
        inst->parent_->delete_instr(inst);
    }
}
