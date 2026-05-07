#include "../../include/mid/opt/inlineExpand.hpp"
#include <map>
#include <vector>

// 辅助：根据旧值获取映射后的新值；若不存在则保持原值（用于常量、全局变量等）
static Value* mapValue(Value* val, const std::map<Value*, Value*>& valMap) {
    auto it = valMap.find(val);
    if (it != valMap.end()) return it->second;
    return val;
}

// 辅助：克隆一条指令（除了 Ret）到目标基本块中，并建立映射
static Instruction* cloneInstruction(Instruction* inst, BasicBlock* bb,
                                     std::map<Value*, Value*>& valMap) {
    switch (inst->op_id_) {
        // 二元运算指令
        case Instruction::Add: case Instruction::Sub: case Instruction::Mul:
        case Instruction::SDiv: case Instruction::SRem: case Instruction::UDiv:
        case Instruction::URem: case Instruction::FAdd: case Instruction::FSub:
        case Instruction::FMul: case Instruction::FDiv:
        case Instruction::And: case Instruction::Or: case Instruction::Xor:
        case Instruction::Shl: case Instruction::LShr: case Instruction::AShr: {
            auto* bin = static_cast<BinaryInst*>(inst);
            return new BinaryInst(bin->type_, bin->op_id_,
                                  mapValue(bin->operands_[0], valMap),
                                  mapValue(bin->operands_[1], valMap), bb);
        }
        // 一元运算指令（包含类型转换）
        case Instruction::FNeg: case Instruction::ZExt: case Instruction::FPtoSI:
        case Instruction::SItoFP: case Instruction::BitCast: {
            auto* un = static_cast<UnaryInst*>(inst);
            // 对于具体转换指令需要保留目标类型
            if (inst->op_id_ == Instruction::ZExt) {
                auto* z = static_cast<ZextInst*>(inst);
                return new ZextInst(inst->op_id_, mapValue(inst->operands_[0], valMap),
                                    z->dest_ty_, bb);
            } else if (inst->op_id_ == Instruction::FPtoSI) {
                auto* f2i = static_cast<FpToSiInst*>(inst);
                return new FpToSiInst(inst->op_id_, mapValue(inst->operands_[0], valMap),
                                      f2i->dest_ty_, bb);
            } else if (inst->op_id_ == Instruction::SItoFP) {
                auto* i2f = static_cast<SiToFpInst*>(inst);
                return new SiToFpInst(inst->op_id_, mapValue(inst->operands_[0], valMap),
                                      i2f->dest_ty_, bb);
            } else if (inst->op_id_ == Instruction::BitCast) {
                auto* bc = static_cast<Bitcast*>(inst);
                return new Bitcast(inst->op_id_, mapValue(inst->operands_[0], valMap),
                                   bc->dest_ty_, bb);
            } else { // FNeg 等作为普通一元指令
                return new UnaryInst(un->type_, un->op_id_,
                                     mapValue(un->operands_[0], valMap), bb);
            }
        }
        // 比较指令
        case Instruction::ICmp: {
            auto* ic = static_cast<ICmpInst*>(inst);
            return new ICmpInst(ic->icmp_op_,
                                mapValue(ic->operands_[0], valMap),
                                mapValue(ic->operands_[1], valMap), bb);
        }
        case Instruction::FCmp: {
            auto* fc = static_cast<FCmpInst*>(inst);
            return new FCmpInst(fc->fcmp_op_,
                                mapValue(fc->operands_[0], valMap),
                                mapValue(fc->operands_[1], valMap), bb);
        }
        // 调用指令
        case Instruction::Call: {
            auto* call = static_cast<CallInst*>(inst);
            Function* func = static_cast<Function*>(call->operands_.back());
            std::vector<Value*> mappedArgs;
            for (unsigned i = 0; i < call->num_ops_ - 1; ++i) {
                mappedArgs.push_back(mapValue(call->operands_[i], valMap));
            }
            return new CallInst(func, mappedArgs, bb);
        }
        // 分支指令
        case Instruction::Br: {
            auto* br = static_cast<BranchInst*>(inst);
            if (br->num_ops_ == 1) { // 无条件跳转
                BasicBlock* target = static_cast<BasicBlock*>(mapValue(br->operands_[0], valMap));
                return new BranchInst(target, bb);
            } else { // 条件跳转
                Value* cond = mapValue(br->operands_[0], valMap);
                BasicBlock* trueBB = static_cast<BasicBlock*>(mapValue(br->operands_[1], valMap));
                BasicBlock* falseBB = static_cast<BasicBlock*>(mapValue(br->operands_[2], valMap));
                return new BranchInst(cond, trueBB, falseBB, bb);
            }
        }
        // 内存相关指令
        case Instruction::Alloca: {
            auto* alloca = static_cast<AllocaInst*>(inst);
            return new AllocaInst(alloca->alloca_ty_, bb);
        }
        case Instruction::Load: {
            Value* ptr = mapValue(inst->operands_[0], valMap);
            return new LoadInst(ptr, bb);
        }
        case Instruction::Store: {
            Value* val = mapValue(inst->operands_[0], valMap);
            Value* ptr = mapValue(inst->operands_[1], valMap);
            return new StoreInst(val, ptr, bb);
        }
        case Instruction::GetElementPtr: {
            auto* gep = static_cast<GetElementPtrInst*>(inst);
            Value* ptr = mapValue(gep->operands_[0], valMap);
            std::vector<Value*> idxs;
            for (unsigned i = 1; i < gep->num_ops_; ++i) {
                idxs.push_back(mapValue(gep->operands_[i], valMap));
            }
            return new GetElementPtrInst(ptr, idxs, bb);
        }
        // Phi 指令
        case Instruction::PHI: {
            auto* phi = static_cast<PhiInst*>(inst);
            // 构造映射后的值-基本块对
            std::vector<Value*> vals;
            std::vector<BasicBlock*> bbs;
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                vals.push_back(mapValue(phi->operands_[i], valMap));
                bbs.push_back(static_cast<BasicBlock*>(mapValue(phi->operands_[i+1], valMap)));
            }
            auto* newPhi = new PhiInst(Instruction::PHI, vals, bbs, phi->type_, bb);
            bb->instr_list_.push_front(newPhi);
            newPhi->pos_in_bb.emplace_back(bb->instr_list_.begin());
            return newPhi;
        }
        default:
            // 不应该到达这里
            assert(false && "unhandled instruction type in clone");
    }
    return nullptr;
}

// 对一个调用点执行内联
static void inlineCall(CallInst* call) {
    Function* callee = static_cast<Function*>(call->operands_.back());
    Function* caller = call->parent_->parent_;
    Module* m = caller->parent_;

    // 提前保存实际参数（call 将在分裂基本块时被移除）
    std::vector<Value*> callArgs;
    for (unsigned i = 0; i < call->num_ops_ - 1; ++i)
        callArgs.push_back(call->operands_[i]);

    // 1. 分裂原基本块
    BasicBlock* origBB = call->parent_;
    BasicBlock* afterBB = new BasicBlock(m, "", caller); // 自动加入 caller

    // 将调用指令之后的所有指令移动到 afterBB
    auto callIt = call->pos_in_bb.back();
    std::vector<Instruction*> instsAfter;
    for (auto it = std::next(callIt); it != origBB->instr_list_.end(); ++it)
        instsAfter.push_back(*it);
    for (auto* inst : instsAfter) {
        origBB->remove_instr(inst);
        afterBB->add_instruction(inst);
    }

    // 移除 call 指令
    origBB->delete_instr(call);

    // 2. 如果需要返回值，在 caller 入口块分配返回值空间
    Type* retTy = static_cast<FunctionType*>(callee->type_)->result_;
    AllocaInst* retAlloca = nullptr;
    if (retTy->tid_ != Type::VoidTyID) {
        BasicBlock* entryBB = caller->basic_blocks_.front();
        retAlloca = new AllocaInst(retTy, entryBB, true);
        // 插入到 entryBB 最前面
        entryBB->instr_list_.push_front(retAlloca);
        retAlloca->pos_in_bb.emplace_back(entryBB->instr_list_.begin());
        retAlloca->parent_ = entryBB;
    }

    // 3. 克隆被调用函数的基本块与指令
    std::map<Value*, Value*> valMap;
    // 映射形参 -> 实参
    for (unsigned i = 0; i < callee->arguments_.size(); ++i) {
        valMap[callee->arguments_[i]] = callArgs[i];
    }

    // 克隆基本块
    for (auto* bb : callee->basic_blocks_) {
        BasicBlock* newBB = new BasicBlock(m, bb->name_ + "_inl", caller);
        valMap[bb] = newBB;
    }

    // 克隆指令（除 Ret 外）
    for (auto* bb : callee->basic_blocks_) {
        BasicBlock* newBB = static_cast<BasicBlock*>(valMap[bb]);
        for (auto* inst : bb->instr_list_) {
            if (inst->is_ret()) {
                // 将返回替换为 store + br
                Value* retVal = nullptr;
                if (inst->num_ops_ == 1)
                    retVal = mapValue(inst->operands_[0], valMap);
                if (retAlloca && retVal) {
                    StoreInst* store = new StoreInst(retVal, retAlloca, newBB);
                    // store 已自动插入到基本块末尾，作为非 terminator 它将在 br 之前
                }
                // 创建跳转到 afterBB 的指令
                new BranchInst(afterBB, newBB);
            } else {
                Instruction* cloned = cloneInstruction(inst, newBB, valMap);
                valMap[inst] = cloned;
            }
        }
    }

    // 4. 在原基本块末尾插入跳转到被内联函数的入口块
    BasicBlock* entryClone = static_cast<BasicBlock*>(valMap[callee->basic_blocks_.front()]);
    new BranchInst(entryClone, origBB);

    // 5. 处理原调用指令的返回值引用
    if (retAlloca) {
        // 创建 Load 时它会被自动加到 afterBB 尾部，我们将其移动到开头
        LoadInst* load = new LoadInst(retAlloca, afterBB);
        // splice 移动指令到开头，避免重复插入
        afterBB->instr_list_.splice(afterBB->instr_list_.begin(), afterBB->instr_list_, load->pos_in_bb.back());
        // 替换所有对 call 的引用
        call->replace_all_use_with(load);
    }
    // 销毁 call 对象（已从基本块中移除）
    delete call;
}

void InlineExpand::execute(Module *module) {
    // 遍历所有函数
    for (auto* func : module->function_list_) {
        if (func->is_declaration()) continue; // 跳过声明

        // 收集所有可内联的调用点
        std::vector<CallInst*> callWorklist;
        for (auto* bb : func->basic_blocks_) {
            for (auto* inst : bb->instr_list_) {
                if (inst->is_call()) {
                    auto* call = static_cast<CallInst*>(inst);
                    Function* callee = static_cast<Function*>(call->operands_.back());
                    // 被调用函数必须有实现，且不能是当前函数（避免直接递归自旋）
                    if (!callee->is_declaration() && callee != func)
                        callWorklist.push_back(call);
                }
            }
        }

        // 依次内联
        for (auto* call : callWorklist) {
            // 调用点可能在之前的处理中已经被移动或删除（很少见，但确保安全）
            if (call->parent_ == nullptr) continue;
            inlineCall(call);
        }

        // 重新为指令命名，保证输出美观
        func->set_instr_name();
    }
}