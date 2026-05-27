#include "../../include/mid/opt/sroa.hpp"
#include <cassert>
#include <map>

void SROA::execute(Module *module) {
    module_ = module;
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func);
    }
}

void SROA::runOnFunction(Function *func) {
    curFunc_ = func;
    toDelete_.clear();

    // ---- 第一遍：收集所有候选 alloca ----
    // 不在遍历 basic_blocks 时直接修改，避免迭代器失效
    std::vector<AllocaInst *> candidates;
    for (auto bb : func->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (inst->op_id_ != Instruction::Alloca) continue;
            auto alloca = static_cast<AllocaInst *>(inst);
            // IMPORTANT : 只对聚合类型（数组）alloca 做 SROA，标量 alloca 交给 Mem2Reg
            if (alloca->alloca_ty_->tid_ == Type::ArrayTyID) {
                if (isSROACandidate(alloca)) {
                    candidates.push_back(alloca);
                }
            }
        }
    }

    // ---- 第二遍：重写每个候选 alloca ----
    for (auto alloca : candidates) {
        rewriteAlloca(alloca);
    }

    // ---- 第三遍：清理已标记的指令 ----
    for (auto inst : toDelete_) {
        if (inst->parent_ == nullptr) continue; // 可能已被其他清理流程删除
        inst->parent_->delete_instr(inst);
    }
}

bool SROA::isSROACandidate(AllocaInst *alloca) {
    // 遍历 alloca 的所有 use
    for (auto &use : alloca->use_list_) {
        auto user = dynamic_cast<Instruction *>(use.val_);
        if (!user) return false; // use 不是指令 → 不安全（不太可能出现）

        // 只接受 GEP 作为 alloca 的直接使用者
        if (user->op_id_ != Instruction::GetElementPtr) {
            return false; // alloca 被非 GEP 方式使用（直接 load/store/call 等）→ 不安全
        }

        auto gep = static_cast<GetElementPtrInst *>(user);

        // 检查 GEP 的结果类型是否为标量指针
        // 只有直达标量元素的 GEP 才可处理，部分 GEP（结果仍为聚合指针）暂不支持
        assert(gep->type_->tid_ == Type::PointerTyID);
        Type *resultTy = static_cast<PointerType *>(gep->type_)->contained_;
        if (!isScalarType(resultTy)) {
            return false; // 部分 GEP，结果仍指向聚合类型 → 跳过
        }

        // 检查所有索引是否为编译期常量
        // 若有变量下标，无法静态确定访问哪个元素 → 跳过
        std::vector<int> indices;
        if (!getConstantIndices(gep, indices)) {
            return false;
        }

        // 检查 GEP 的每个使用者是否为 Load 或 Store
        // 若 GEP 的结果被用于 call 参数、bitcast 等 → 地址可能逃逸 → 跳过
        for (auto &gepUse : gep->use_list_) {
            auto gepUser = dynamic_cast<Instruction *>(gepUse.val_);
            if (!gepUser) return false;
            if (gepUser->op_id_ != Instruction::Load &&
                gepUser->op_id_ != Instruction::Store) {
                return false;
            }
        }
    }

    return true;
}

void SROA::rewriteAlloca(AllocaInst *alloca) {
    BasicBlock *entryBB = curFunc_->basic_blocks_.front();

    // 索引元组 → 新标量 alloca 的映射
    // 例如 [10 x i32] 中 arr[3] 的 key 为 {3}
    // 例如 [2 x [3 x i32]] 中 arr[1][2] 的 key 为 {1, 2}
    std::map<std::vector<int>, AllocaInst *> newAllocas;

    // 复制 use_list 以避免在遍历中修改时迭代器失效
    auto allocaUses = alloca->use_list_;
    for (auto &use : allocaUses) {
        auto gep = static_cast<GetElementPtrInst *>(use.val_);

        // 提取常量下标元组
        std::vector<int> indices;
        bool ok = getConstantIndices(gep, indices);
        assert(ok && "indices should be constant (checked in isSROACandidate)");
        (void)ok;

        // 若该下标元组尚未分配标量 alloca，则创建
        if (newAllocas.find(indices) == newAllocas.end()) {
            // GEP 的结果类型是 PointerType(scalar)，取其 contained 即为标量类型
            Type *scalarTy =
                static_cast<PointerType *>(gep->type_)->contained_;
            // 使用不自动插入 BB 的构造函数，手动插入到入口块最前面
            auto newAlloca = new AllocaInst(scalarTy, entryBB, true);
            entryBB->add_instruction_front(newAlloca);
            newAllocas[indices] = newAlloca;
        }

        AllocaInst *scalarAlloca = newAllocas[indices];

        auto gepUses = gep->use_list_;
        for (auto &gepUse : gepUses) {
            auto gepUser = dynamic_cast<Instruction *>(gepUse.val_);
            int argNo = gepUse.arg_no_;

            // Load: operand 0 是指针, Store: operand 1 是指针
            // 直接修改指针操作数，无需创建新指令
            gepUser->set_operand(argNo, scalarAlloca);
        }
        toDelete_.insert(gep);
    }
    toDelete_.insert(alloca);
}

bool SROA::getConstantIndices(GetElementPtrInst *gep,
                              std::vector<int> &indices) {
    // GEP 的操作数布局：
    //   [0] = 基指针（alloca）
    //   [1] = 首层解引用，固定为 ConstantInt(0)
    //   [2] = 第一维下标（如果是 1D 数组，到此即为标量元素）
    //   [3] = 次层解引用（若有多维）
    //   [4] = 第二维下标
    //   ...依此类推
    //
    // 提取 operand[2], operand[4], ... 即所有实际维度下标
    for (unsigned i = 2; i < gep->num_ops_; i += 2) {
        auto idx = dynamic_cast<ConstantInt *>(gep->get_operand(i));
        if (!idx) return false; // 存在变量下标 → 不可静态分析
        indices.push_back(static_cast<int>(idx->value_));
    }
    return true;
}

bool SROA::isScalarType(Type *ty) {
    return ty->tid_ == Type::IntegerTyID || ty->tid_ == Type::FloatTyID;
}
