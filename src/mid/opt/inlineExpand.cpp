#include "../../include/mid/opt/inlineExpand.hpp"
#include <algorithm>
#include <cassert>
#include <functional>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

void InlineExpand::execute(Module *module) {
    // 工作队列：存储所有待内联的 CallInst
    vector<CallInst*> worklist;
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        for (auto bb : func->basic_blocks_) {
            for (auto inst : bb->instr_list_) {
                if (auto *call = dynamic_cast<CallInst*>(inst))
                    worklist.push_back(call);
            }
        }
    }

    // 循环处理，因为内联可能引入新的 call
    while (!worklist.empty()) {
        CallInst *call = worklist.back();
        worklist.pop_back();

        if (!call->parent_) continue;

        Function *callee = dynamic_cast<Function*>(call->get_operand(call->num_ops_ - 1));
        if (!callee || !canInline(callee, call->parent_->parent_))
            continue;

        // 执行内联，可能引入新的 call，将其加入队列
        vector<CallInst*> newCalls = performInline(call);
        for (auto *nc : newCalls)
            worklist.push_back(nc);
    }
}

unsigned InlineExpand::countInstructions(Function *func) {
    unsigned cnt = 0;
    for (auto bb : func->basic_blocks_)
        cnt += bb->instr_list_.size();
    return cnt;
}

bool InlineExpand::canInline(Function *callee, Function *caller) {
    if (callee->is_declaration() || callee == caller)
        return false;
    if (countInstructions(callee) > INLINE_THRESHOLD)
        return false;
    return true;
}

// 获取函数的逆后序（RPO）
vector<BasicBlock*> InlineExpand::getRPO(Function *func) {
    vector<BasicBlock*> rpo;
    unordered_set<BasicBlock*> visited;
    function<void(BasicBlock*)> dfs = [&](BasicBlock *bb) {
        visited.insert(bb);
        for (auto succ : bb->succ_bbs_) {
            if (!visited.count(succ))
                dfs(succ);
        }
        rpo.push_back(bb);   // 后序
    };
    if (!func->basic_blocks_.empty())
        dfs(func->basic_blocks_[0]);
    // 处理不可达块
    for (auto bb : func->basic_blocks_) {
        if (!visited.count(bb))
            dfs(bb);
    }
    reverse(rpo.begin(), rpo.end());  // 逆后序
    return rpo;
}

// 返回内联过程中新产生的所有 call 指令
vector<CallInst*> InlineExpand::performInline(CallInst *callInst) {
    BasicBlock *callBB = callInst->parent_;
    Function *caller = callBB->parent_;
    Function *callee = dynamic_cast<Function*>(callInst->get_operand(callInst->num_ops_ - 1));
    vector<CallInst*> newCalls;

    // 1. 分裂基本块，获得 cont 块
    BasicBlock *contBB = splitBlockAfterCall(callBB, callInst);

    // 2. 收集实参
    vector<Value*> args;
    for (unsigned i = 0; i < callInst->num_ops_ - 1; ++i)
        args.push_back(callInst->get_operand(i));

    // 3. 复制 callee 函数体
    unordered_map<Value*, Value*> valMap;
    unordered_map<BasicBlock*, BasicBlock*> bbMap;
    vector<BasicBlock*> newBBs;
    cloneCalleeIntoCaller(callee, caller, args, valMap, bbMap, newBBs);

    // 收集新产生的 call 指令
    for (auto *bb : newBBs) {
        for (auto *inst : bb->instr_list_) {
            if (auto *c = dynamic_cast<CallInst*>(inst))
                newCalls.push_back(c);
        }
    }

    // 4. 提升 alloca 到调用者入口块
    BasicBlock *entryBB = caller->basic_blocks_[0];
    for (auto *newBB : newBBs) {
        auto it = newBB->instr_list_.begin();
        while (it != newBB->instr_list_.end()) {
            if ((*it)->is_alloca()) {
                auto *alloca = *it++;
                newBB->remove_instr(alloca);
                entryBB->add_instruction_front(alloca);
            } else {
                ++it;
            }
        }
    }

    // 5. 处理所有 ret 指令：替换为 br 到 cont，并构建返回值 phi
    bool hasRetVal = !callInst->is_void();
    PhiInst *retPhi = nullptr;
    if (hasRetVal) {
        retPhi = PhiInst::create_phi(callInst->type_, contBB);
        contBB->add_instruction_front(retPhi);
    }

    for (auto *newBB : newBBs) {
        Instruction *term = newBB->get_terminator();
        if (auto *retInst = dynamic_cast<ReturnInst*>(term)) {
            if (hasRetVal) {
                assert(retInst->num_ops_ > 0 && "return value expected but none given");
                retPhi->addIncoming(retInst->get_operand(0), newBB);
            }
            newBB->delete_instr(retInst);
            new BranchInst(contBB, newBB);
        }
    }

    // 6. 替换 call 结果的所有使用
    if (hasRetVal) {
        callInst->replace_all_use_with(retPhi);
    }

    // 7. 移除 call 指令，并建立 callBB 到 callee 入口的跳转
    callBB->delete_instr(callInst);
    BasicBlock *calleeEntry = bbMap[callee->basic_blocks_[0]];
    IRStmtBuilder builder(callBB, caller->parent_);
    builder.create_br(calleeEntry);

    // 8. 重新设置指令名称，避免冲突
    caller->set_instr_name();

    return newCalls;
}

BasicBlock* InlineExpand::splitBlockAfterCall(BasicBlock *callBB, CallInst *callInst) {
    Module *module = callBB->parent_->parent_;
    Function *func = callBB->parent_;
    BasicBlock *contBB = new BasicBlock(module, "cont_" + func->name_, func);

    Instruction *terminator = callBB->get_terminator();
    assert(terminator && "block must have terminator");

    // 收集 callInst 之后的所有指令（包括终止符）
    vector<Instruction*> toMove;
    bool afterCall = false;
    for (auto *inst : callBB->instr_list_) {
        if (inst == callInst) {
            afterCall = true;
            continue;
        }
        if (afterCall)
            toMove.push_back(inst);
    }

    // 保存原后继关系并清空 callBB 与后继的双向链接
    vector<BasicBlock*> origSuccs = callBB->succ_bbs_;
    for (auto *succ : origSuccs)
        succ->remove_pre_basic_block(callBB);
    callBB->succ_bbs_.clear();

    // 从 callBB 移除这些指令（remove_instr 会清空 pos_in_bb，使 add_instruction 可以重新插入）
    for (auto *inst : toMove)
        callBB->remove_instr(inst);

    // 将它们加入 contBB，并重建 CFG
    for (auto *inst : toMove)
        contBB->add_instruction(inst);

    contBB->succ_bbs_ = origSuccs;
    for (auto *succ : origSuccs)
        succ->add_pre_basic_block(contBB);

    for (auto *succ : origSuccs) {
        for (auto *inst : succ->instr_list_) {
            auto *phi = dynamic_cast<PhiInst*>(inst);
            if (!phi) break;   // PHI 指令必须位于块头部，遇到非 PHI 即可提前退出
            // PHI 操作数布局：[val0, bb0, val1, bb1, ...]，奇数下标为来源基本块
            for (unsigned i = 1; i < phi->num_ops_; i += 2) {
                if (phi->get_operand(i) == callBB) {
                    // 先从 callBB 的 use_list_ 中删除旧条目
                    callBB->remove_use(phi->use_pos_[i]);
                    // 再更新操作数并在 contBB 的 use_list_ 中注册新 use
                    phi->operands_[i] = contBB;
                    phi->use_pos_[i]  = contBB->add_use(phi, i);
                }
            }
        }
    }

    return contBB;
}

static Value* mapValue(Value *val,
                       const unordered_map<Value*, Value*> &valMap,
                       const unordered_map<BasicBlock*, BasicBlock*> &bbMap) {
    if (auto it = valMap.find(val); it != valMap.end())
        return it->second;
    if (auto *bb = dynamic_cast<BasicBlock*>(val)) {
        auto it = bbMap.find(bb);
        assert(it != bbMap.end() && "basic block not mapped");
        return it->second;
    }
    return val;
}

void InlineExpand::cloneCalleeIntoCaller(Function *callee, Function *caller,
                                         vector<Value*> &args,
                                         unordered_map<Value*, Value*> &valMap,
                                         unordered_map<BasicBlock*, BasicBlock*> &bbMap,
                                         vector<BasicBlock*> &newBBs) {
    // 形参映射
    for (unsigned i = 0; i < callee->arguments_.size(); ++i)
        valMap[callee->arguments_[i]] = args[i];

    // 步骤一：先创建所有基本块（空壳），建立 bbMap
    auto rpo = getRPO(callee);
    for (auto *oldBB : rpo) {
        BasicBlock *newBB = new BasicBlock(caller->parent_, "inline_" + oldBB->name_, caller);
        bbMap[oldBB] = newBB;
        newBBs.push_back(newBB);
    }

    // 步骤二：复制每个基本块内的指令
    struct PhiFixup { PhiInst *newPhi; PhiInst *oldPhi; };
    vector<PhiFixup> phiFixups;

    for (auto *oldBB : rpo) {
        BasicBlock *newBB = bbMap[oldBB];
        for (auto *oldInst : oldBB->instr_list_) {
            if (auto *oldPhi = dynamic_cast<PhiInst*>(oldInst)) {
                auto *newPhi = PhiInst::create_phi(oldPhi->type_, newBB);
                newBB->add_instruction(newPhi);  
                valMap[oldPhi] = newPhi;
                phiFixups.push_back({newPhi, oldPhi});
            } else if (auto *oldBr = dynamic_cast<BranchInst*>(oldInst)) {
                Value *cond = oldBr->num_ops_ == 3
                              ? mapValue(oldBr->get_operand(0), valMap, bbMap)
                              : nullptr;
                BasicBlock *trueBB = dynamic_cast<BasicBlock*>(
                    mapValue(oldBr->get_operand(oldBr->num_ops_ == 3 ? 1 : 0), valMap, bbMap));
                BasicBlock *falseBB = oldBr->num_ops_ == 3
                                      ? dynamic_cast<BasicBlock*>(mapValue(oldBr->get_operand(2), valMap, bbMap))
                                      : nullptr;
                if (cond)
                    new BranchInst(cond, trueBB, falseBB, newBB);
                else
                    new BranchInst(trueBB, newBB);
            } else if (auto *oldRet = dynamic_cast<ReturnInst*>(oldInst)) {
                if (oldRet->num_ops_ > 0)
                    new ReturnInst(mapValue(oldRet->get_operand(0), valMap, bbMap), newBB);
                else
                    new ReturnInst(newBB);
            } else if (auto *oldAlloca = dynamic_cast<AllocaInst*>(oldInst)) {
                auto *newAlloca = new AllocaInst(oldAlloca->alloca_ty_, newBB);
                valMap[oldAlloca] = newAlloca;
            } else if (auto *oldLoad = dynamic_cast<LoadInst*>(oldInst)) {
                new LoadInst(mapValue(oldLoad->get_operand(0), valMap, bbMap), newBB);
                valMap[oldLoad] = newBB->instr_list_.back();
            } else if (auto *oldStore = dynamic_cast<StoreInst*>(oldInst)) {
                new StoreInst(mapValue(oldStore->get_operand(0), valMap, bbMap),
                              mapValue(oldStore->get_operand(1), valMap, bbMap), newBB);
            } else if (auto *oldGEP = dynamic_cast<GetElementPtrInst*>(oldInst)) {
                vector<Value*> idxs;
                for (unsigned i = 1; i < oldGEP->num_ops_; ++i)
                    idxs.push_back(mapValue(oldGEP->get_operand(i), valMap, bbMap));
                new GetElementPtrInst(mapValue(oldGEP->get_operand(0), valMap, bbMap), idxs, newBB);
                valMap[oldGEP] = newBB->instr_list_.back();
            } else if (auto *oldCall = dynamic_cast<CallInst*>(oldInst)) {
                vector<Value*> newArgs;
                for (unsigned i = 0; i < oldCall->num_ops_ - 1; ++i)
                    newArgs.push_back(mapValue(oldCall->get_operand(i), valMap, bbMap));
                auto *calleeFunc = dynamic_cast<Function*>(
                    mapValue(oldCall->get_operand(oldCall->num_ops_ - 1), valMap, bbMap));
                new CallInst(calleeFunc, newArgs, newBB);
                if (!oldCall->is_void())
                    valMap[oldCall] = newBB->instr_list_.back();
            } else if (auto *oldBin = dynamic_cast<BinaryInst*>(oldInst)) {
                new BinaryInst(oldBin->type_, oldBin->op_id_,
                               mapValue(oldBin->get_operand(0), valMap, bbMap),
                               mapValue(oldBin->get_operand(1), valMap, bbMap), newBB);
                valMap[oldBin] = newBB->instr_list_.back();
            } else if (auto *oldICmp = dynamic_cast<ICmpInst*>(oldInst)) {
                new ICmpInst(oldICmp->icmp_op_,
                             mapValue(oldICmp->get_operand(0), valMap, bbMap),
                             mapValue(oldICmp->get_operand(1), valMap, bbMap), newBB);
                valMap[oldICmp] = newBB->instr_list_.back();
            } else if (auto *oldFCmp = dynamic_cast<FCmpInst*>(oldInst)) {
                new FCmpInst(oldFCmp->fcmp_op_,
                             mapValue(oldFCmp->get_operand(0), valMap, bbMap),
                             mapValue(oldFCmp->get_operand(1), valMap, bbMap), newBB);
                valMap[oldFCmp] = newBB->instr_list_.back();
            } else if (auto *oldZExt = dynamic_cast<ZextInst*>(oldInst)) {
                new ZextInst(oldZExt->op_id_,
                             mapValue(oldZExt->get_operand(0), valMap, bbMap),
                             oldZExt->dest_ty_, newBB);
                valMap[oldZExt] = newBB->instr_list_.back();
            } else if (auto *oldFpToSi = dynamic_cast<FpToSiInst*>(oldInst)) {
                new FpToSiInst(oldFpToSi->op_id_,
                               mapValue(oldFpToSi->get_operand(0), valMap, bbMap),
                               oldFpToSi->dest_ty_, newBB);
                valMap[oldFpToSi] = newBB->instr_list_.back();
            } else if (auto *oldSiToFp = dynamic_cast<SiToFpInst*>(oldInst)) {
                new SiToFpInst(oldSiToFp->op_id_,
                               mapValue(oldSiToFp->get_operand(0), valMap, bbMap),
                               oldSiToFp->dest_ty_, newBB);
                valMap[oldSiToFp] = newBB->instr_list_.back();
            } else if (auto *oldBitCast = dynamic_cast<Bitcast*>(oldInst)) {
                new Bitcast(oldBitCast->op_id_,
                            mapValue(oldBitCast->get_operand(0), valMap, bbMap),
                            oldBitCast->dest_ty_, newBB);
                valMap[oldBitCast] = newBB->instr_list_.back();
            // } else if (auto *oldUnary = dynamic_cast<UnaryInst*>(oldInst)) {
            //     new UnaryInst(oldUnary->type_, oldUnary->op_id_,
            //                   mapValue(oldUnary->get_operand(0), valMap, bbMap), newBB);
            //     valMap[oldUnary] = newBB->instr_list_.back();
            } else {
                assert(0 && "unhandled instruction type during inlining");
            }
        }
    }

    // 填充 phi 指令的操作数（延迟处理以支持跨块的前向引用）
    for (auto &fix : phiFixups) {
        for (unsigned i = 0; i < fix.oldPhi->num_ops_; i += 2) {
            Value    *v  = mapValue(fix.oldPhi->get_operand(i),   valMap, bbMap);
            BasicBlock *bb = dynamic_cast<BasicBlock*>(
                mapValue(fix.oldPhi->get_operand(i + 1), valMap, bbMap));
            fix.newPhi->addIncoming(v, bb);
        }
    }
}