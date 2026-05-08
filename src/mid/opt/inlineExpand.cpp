#include "../../include/mid/opt/inlineExpand.hpp"
#include <map>
#include <set>
#include <vector>

void InlineExpand::execute(Module *module) {
    bool changed = true;
    int iteration = 0;
    const int MAX_ITERATIONS = 10;

    while (changed && iteration < MAX_ITERATIONS) {
        changed = false;
        iteration++;

        for (auto func : module->function_list_) {
            if (func->is_declaration()) continue;
            changed |= runOnFunction(func, module);
        }
    }
}

bool InlineExpand::runOnFunction(Function *func, Module *module) {
    bool changed = false;

    // 收集所有可以内联的调用指令
    // 注意：必须先收集再处理，因为内联会修改基本块列表
    std::vector<std::pair<CallInst*, Function*>> callsToInline;

    // 创建基本块的快照，避免迭代时修改
    std::vector<BasicBlock*> bbSnapshot(func->basic_blocks_.begin(), func->basic_blocks_.end());

    for (auto bb : bbSnapshot) {
        // 创建指令列表的快照
        std::vector<Instruction*> instrSnapshot(bb->instr_list_.begin(), bb->instr_list_.end());

        for (auto instr : instrSnapshot) {
            // 检查指令是否还在基本块中（可能已被删除）
            if (instr->parent_ != bb) continue;

            auto call = dynamic_cast<CallInst*>(instr);
            if (!call) continue;

            Function *callee = dynamic_cast<Function*>(call->get_operand(call->num_ops_ - 1));
            if (!callee) continue;

            if (shouldInline(callee, func)) {
                callsToInline.push_back({call, callee});
            }
        }
    }

    // 执行内联
    for (auto &pair : callsToInline) {
        CallInst *call = pair.first;
        Function *callee = pair.second;

        // 再次检查call是否还有效
        if (call->parent_ == nullptr) continue;

        if (inlineCall(call, callee, module)) {
            changed = true;
        }
    }

    return changed;
}

bool InlineExpand::shouldInline(Function *callee, Function *caller) {
    if (callee->is_declaration()) return false;
    if (callee == caller) return false;

    // 指令数阈值
    int instrCount = 0;
    for (auto bb : callee->basic_blocks_) {
        instrCount += bb->instr_list_.size();
    }
    const int INLINE_THRESHOLD = 30;
    if (instrCount > INLINE_THRESHOLD) return false;

    // 检查 callee 中是否存在对 caller 的直接或间接调用（避免 A→B→A）
    std::set<Function*> visited;
    std::vector<Function*> worklist;
    worklist.push_back(callee);
    visited.insert(callee);

    while (!worklist.empty()) {
        Function *func = worklist.back();
        worklist.pop_back();
        for (auto bb : func->basic_blocks_) {
            for (auto instr : bb->instr_list_) {
                auto call = dynamic_cast<CallInst*>(instr);
                if (!call) continue;
                Function *called = dynamic_cast<Function*>(call->get_operand(call->num_ops_ - 1));
                if (!called || called->is_declaration()) continue;
                if (called == caller) return false;          // 直接或间接调用了 caller
                if (visited.insert(called).second) {
                    worklist.push_back(called);
                }
            }
        }
    }
    return true;
}

bool InlineExpand::inlineCall(CallInst *call, Function *callee, Module *module) {
    BasicBlock *callBB = call->parent_;
    Function *caller = callBB->parent_;
    inlineIdCounter++;                 // 当前内联操作编号

    // 生成唯一后缀，避免基本块重名
    std::string suffix = ".ix" + std::to_string(inlineIdCounter);

    bool hasReturnValue = !call->use_list_.empty();
    Type *retType = call->type_;

    // 1. 参数映射
    std::map<Value*, Value*> valueMap;
    for (size_t i = 0; i < callee->arguments_.size(); i++) {
        valueMap[callee->arguments_[i]] = call->get_operand(i);
    }

    // 2. 提前记录 callBB 在函数基本块列表中的位置（索引）
    auto &funcBBs = caller->basic_blocks_;
    int callBBIndex = -1;
    for (int i = 0; i < (int)funcBBs.size(); i++) {
        if (funcBBs[i] == callBB) {
            callBBIndex = i;
            break;
        }
    }
    assert(callBBIndex != -1 && "callBB not found in function");

    // 3. 创建被克隆的基本块（不自动加入函数列表，由我们手动管理）
    // 注意：假设 BasicBlock 的构造函数会自动插入到 caller 末尾。
    // 我们立即将其从 caller 列表中移出，保存在数组中，最后统一插入。
    std::map<BasicBlock*, BasicBlock*> bbMap;
    std::vector<BasicBlock*> clonedBBs;   // 按原顺序存放克隆块

    for (auto bb : callee->basic_blocks_) {
        // 使用唯一名称
        std::string newName = bb->name_ + suffix;
        BasicBlock *newBB = new BasicBlock(module, newName, caller);
        // 从自动添加的位置移除（位于末尾），稍后统一插入
        funcBBs.pop_back();
        bbMap[bb] = newBB;
        clonedBBs.push_back(newBB);
    }

    // 4. 分割 callBB：将 call 之后的指令移到 afterCallBB
    BasicBlock *afterCallBB = new BasicBlock(module, callBB->name_ + ".ac" + suffix, caller);
    funcBBs.pop_back();   // 同样从末尾移除，手工管理

    auto callIt = std::find(callBB->instr_list_.begin(), callBB->instr_list_.end(), call);
    assert(callIt != callBB->instr_list_.end() && "call not in block");
    std::vector<Instruction*> toMove(std::next(callIt), callBB->instr_list_.end());
    for (auto instr : toMove) {
        callBB->remove_instr(instr);
        afterCallBB->add_instruction(instr);
        instr->parent_ = afterCallBB;
    }

    // 5. 更新 CFG：afterCallBB 继承 callBB 的后继
    for (auto succ : callBB->succ_bbs_) {
        succ->remove_pre_basic_block(callBB);
        succ->add_pre_basic_block(afterCallBB);
        afterCallBB->add_succ_basic_block(succ);
        // 更新 phi 节点
        for (auto instr : succ->instr_list_) {
            auto phi = dynamic_cast<PhiInst*>(instr);
            if (!phi) break;
            for (size_t i = 1; i < phi->num_ops_; i += 2) {
                if (phi->get_operand(i) == callBB) {
                    phi->set_operand(i, afterCallBB);
                }
            }
        }
    }
    callBB->succ_bbs_.clear();

    // 6. 克隆指令（到新基本块）
    for (auto bb : callee->basic_blocks_) {
        BasicBlock *newBB = bbMap[bb];
        for (auto instr : bb->instr_list_) {
            Instruction *newInstr = cloneInstruction(instr, newBB, valueMap, bbMap, module);
            if (newInstr) {
                valueMap[instr] = newInstr;
            }
        }
    }

    // 7. 处理返回指令，收集返回值
    struct ReturnInfo {
        Value *value;
        BasicBlock *block;
    };
    std::vector<ReturnInfo> returnValues;

    for (auto bb : callee->basic_blocks_) {
        BasicBlock *newBB = bbMap[bb];
        // 查找并移除 ReturnInst，替换为跳转到 afterCallBB 的无条件分支
        for (auto it = newBB->instr_list_.begin(); it != newBB->instr_list_.end(); ++it) {
            auto ret = dynamic_cast<ReturnInst*>(*it);
            if (ret) {
                if (ret->num_ops_ > 0) {
                    Value *retVal = ret->get_operand(0);
                    returnValues.push_back({retVal, newBB});
                }
                // 删除 return
                newBB->delete_instr(ret);
                // 创建无条件分支
                IRStmtBuilder builder(newBB, module);
                builder.create_br(afterCallBB);
                afterCallBB->add_pre_basic_block(newBB);
                newBB->add_succ_basic_block(afterCallBB);
                break;  // 一个基本块最多一个返回指令
            }
        }
    }

    // 8. 根据返回值的数量生成替换值
    Value *callReplacement = nullptr;
    if (hasReturnValue) {
        if (returnValues.empty()) {
            // 函数无返回值，但调用结果被使用了，属于 IR 错误，酌情处理
        } else if (returnValues.size() == 1) {
            // 只有一个返回块（可能有多个基本块但都汇合到同一值），直接复用该值
            callReplacement = returnValues[0].value;
        } else {
            // 多个返回块返回不同值，需要 phi 节点
            PhiInst *phi = PhiInst::create_phi(retType, afterCallBB);
            for (auto &ri : returnValues) {
                phi->add_phi_pair_operand(ri.value, ri.block);
            }
            afterCallBB->add_instruction_front(phi);
            callReplacement = phi;
        }
    }

    // 9. 替换 call 的使用并删除 call 指令
    if (callReplacement) {
        call->replace_all_use_with(callReplacement);
    }
    callBB->delete_instr(call);   // call 被移除

    // 10. 连接 callBB 到内联入口，并创建终止指令
    BasicBlock *inlineEntry = bbMap[callee->basic_blocks_.front()];
    {
        // 如果 callBB 已有终结指令，先移除（比如 br 或 ret 可能残存）
        if (auto oldTerm = callBB->get_terminator()) {
            callBB->delete_instr(oldTerm);
        }
        IRStmtBuilder builder(callBB, module);
        builder.create_br(inlineEntry);
        inlineEntry->add_pre_basic_block(callBB);
        callBB->add_succ_basic_block(inlineEntry);
    }

    // 11. 将新块插入到函数的 basic_blocks_ 列表中正确位置
    // 顺序：callBB, 然后 clonedBBs..., 然后 afterCallBB
    // 注意 afterCallBB 之后的块已在原列表排列。
    // 我们从 callBB 之后的位置开始插入
    auto insertPos = funcBBs.begin() + callBBIndex + 1;
    // 插入所有克隆块
    insertPos = funcBBs.insert(insertPos, clonedBBs.begin(), clonedBBs.end());
    // 插入 afterCallBB
    funcBBs.insert(insertPos + clonedBBs.size(), afterCallBB);

    return true;
}

Instruction* InlineExpand::cloneInstruction(Instruction *instr, BasicBlock *newBB,
                                            std::map<Value*, Value*> &valueMap,
                                            std::map<BasicBlock*, BasicBlock*> &bbMap,
                                            Module *module) {
    auto mapValue = [&](Value *v) -> Value* {
        if (valueMap.count(v)) return valueMap[v];
        if (dynamic_cast<Constant*>(v) || dynamic_cast<GlobalVariable*>(v)) return v;
        return v;
    };

    if (auto bin = dynamic_cast<BinaryInst*>(instr)) {
        return new BinaryInst(bin->type_, bin->op_id_,
                             mapValue(bin->get_operand(0)),
                             mapValue(bin->get_operand(1)), newBB);
    }

    if (auto unary = dynamic_cast<UnaryInst*>(instr)) {
        return new UnaryInst(unary->type_, unary->op_id_,
                            mapValue(unary->get_operand(0)), newBB);
    }

    if (auto icmp = dynamic_cast<ICmpInst*>(instr)) {
        return new ICmpInst(icmp->icmp_op_,
                           mapValue(icmp->get_operand(0)),
                           mapValue(icmp->get_operand(1)), newBB);
    }

    if (auto fcmp = dynamic_cast<FCmpInst*>(instr)) {
        return new FCmpInst(fcmp->fcmp_op_,
                           mapValue(fcmp->get_operand(0)),
                           mapValue(fcmp->get_operand(1)), newBB);
    }

    if (auto callInst = dynamic_cast<CallInst*>(instr)) {
        std::vector<Value*> args;
        for (size_t i = 0; i < callInst->num_ops_ - 1; i++) {
            args.push_back(mapValue(callInst->get_operand(i)));
        }
        Function *func = dynamic_cast<Function*>(callInst->get_operand(callInst->num_ops_ - 1));
        return new CallInst(func, args, newBB);
    }

    if (auto br = dynamic_cast<BranchInst*>(instr)) {
        if (br->num_ops_ == 1) {
            BasicBlock *target = bbMap[dynamic_cast<BasicBlock*>(br->get_operand(0))];
            return new BranchInst(target, newBB);
        } else if (br->num_ops_ == 3) {
            Value *cond = mapValue(br->get_operand(0));
            BasicBlock *trueTarget = bbMap[dynamic_cast<BasicBlock*>(br->get_operand(1))];
            BasicBlock *falseTarget = bbMap[dynamic_cast<BasicBlock*>(br->get_operand(2))];
            return new BranchInst(cond, trueTarget, falseTarget, newBB);
        }
    }

    if (auto ret = dynamic_cast<ReturnInst*>(instr)) {
        if (ret->num_ops_ > 0) {
            return new ReturnInst(mapValue(ret->get_operand(0)), newBB);
        } else {
            return new ReturnInst(newBB);
        }
    }

    if (auto gep = dynamic_cast<GetElementPtrInst*>(instr)) {
        Value *ptr = mapValue(gep->get_operand(0));
        std::vector<Value*> idxs;
        for (size_t i = 1; i < gep->num_ops_; i++) {
            idxs.push_back(mapValue(gep->get_operand(i)));
        }
        return new GetElementPtrInst(ptr, idxs, newBB);
    }

    if (auto store = dynamic_cast<StoreInst*>(instr)) {
        return new StoreInst(mapValue(store->get_operand(0)),
                            mapValue(store->get_operand(1)), newBB);
    }

    if (auto load = dynamic_cast<LoadInst*>(instr)) {
        return new LoadInst(mapValue(load->get_operand(0)), newBB);
    }

    if (auto alloca = dynamic_cast<AllocaInst*>(instr)) {
        return new AllocaInst(alloca->alloca_ty_, newBB);
    }

    if (auto zext = dynamic_cast<ZextInst*>(instr)) {
        return new ZextInst(zext->op_id_, mapValue(zext->get_operand(0)),
                           zext->dest_ty_, newBB);
    }

    if (auto fptosi = dynamic_cast<FpToSiInst*>(instr)) {
        return new FpToSiInst(fptosi->op_id_, mapValue(fptosi->get_operand(0)),
                             fptosi->dest_ty_, newBB);
    }

    if (auto sitofp = dynamic_cast<SiToFpInst*>(instr)) {
        return new SiToFpInst(sitofp->op_id_, mapValue(sitofp->get_operand(0)),
                             sitofp->dest_ty_, newBB);
    }

    if (auto bitcast = dynamic_cast<Bitcast*>(instr)) {
        return new Bitcast(bitcast->op_id_, mapValue(bitcast->get_operand(0)),
                          bitcast->dest_ty_, newBB);
    }

    if (auto phi = dynamic_cast<PhiInst*>(instr)) {
        PhiInst *newPhi = PhiInst::create_phi(phi->type_, newBB);
        for (size_t i = 0; i < phi->num_ops_; i += 2) {
            Value *val = mapValue(phi->get_operand(i));
            BasicBlock *bb = bbMap[dynamic_cast<BasicBlock*>(phi->get_operand(i + 1))];
            newPhi->add_phi_pair_operand(val, bb);
        }
        return newPhi;
    }

    return nullptr;
}
