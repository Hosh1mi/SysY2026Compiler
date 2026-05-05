#include "../../include/mid/opt/arraySimplify.hpp"

void DimArrayArgSimplify::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func);
    }
}

void DimArrayArgSimplify::runOnFunction(Function *func) {
    // 先收集所有 alloca，避免边遍历边删除导致迭代器失效
    std::vector<AllocaInst *> allocas;
    for (auto bb : func->basic_blocks_) {
        for (auto &inst : bb->instr_list_) {
            if (auto *alloca = dynamic_cast<AllocaInst *>(inst))
                allocas.push_back(alloca);
        }
    }
    for (auto *alloca : allocas) {
        // 只处理分配单元为指针类型的 alloca
        Type *allocTy = alloca->alloca_ty_;
        if (allocTy->tid_ != Type::PointerTyID) continue;
        // 检查使用模式：只能有 load，且只有一个 store（形参）
        std::vector<LoadInst *> loads;
        StoreInst *store = nullptr;
        bool fail = false;
        for (auto &use : alloca->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (auto *load = dynamic_cast<LoadInst *>(user)) {
                loads.push_back(load);
            } else if (auto *st = dynamic_cast<StoreInst *>(user)) {
                // 确保该 store 是把值存入 alloca（而不是用 alloca 作为值）
                if (store != nullptr || st->get_operand(1) != alloca) {
                    fail = true;
                    break;
                }
                store = st;
            } else {
                fail = true;
                break;
            }
        }
        if (fail || store == nullptr) continue;
        Value *arg = store->get_operand(0);
        // 确认 store 的来源是函数形参
        bool isParam = false;
        for (auto param : func->arguments_) {
            if (param == arg) {
                isParam = true;
                break;
            }
        }
        if (!isParam) continue;
        // 用形参替换所有 load 的使用，并删除 load
        for (auto *load : loads) {
            load->replace_all_use_with(arg);
            load->parent_->delete_instr(load);
        }
        // 删除 store 和 alloca
        store->parent_->delete_instr(store);
        alloca->parent_->delete_instr(alloca);
    }
}
