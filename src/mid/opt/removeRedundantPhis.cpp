#include "../../include/mid/opt/removeRedundantPhis.hpp"

void RemoveRedundantPhis::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func);
    }
}

bool RemoveRedundantPhis::eliminateTrivialPhi(PhiInst *phi) {
    // 收集非自引用的 incoming value
    Value *common = nullptr;
    bool hasSelfRef = false;

    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        Value *v = phi->get_operand(i);
        // 跳过自引用（phi 使用自己）
        if (v == phi) {
            hasSelfRef = true;
            continue;
        }
        if (common == nullptr) {
            common = v;
        } else if (common != v) {
            // 发现两个不同的外部值，非平凡
            return false;
        }
    }

    // 如果所有非自引用值都相同（或只有自引用而无其他外部值），则是平凡的
    // 当 common == nullptr 时，说明所有操作数都是自引用，这是未定义行为，保留原 phi
    if (common == nullptr) {
        return false;   // 无法确定替代值，保留
    }

    // 处理“单前驱”特殊情况：只有一个 incoming value 且非自引用
    // 这里已被上述逻辑覆盖，common 即为唯一外部值

    // 替换所有使用，并标记删除
    phi->replace_all_use_with(common);
    // 若 common 也是 phi，可能在后续迭代中再次被消除，此处不做额外处理
    phi->parent_->delete_instr(phi);
    return true;
}

void RemoveRedundantPhis::runOnFunction(Function *func) {
    std::set<PhiInst *> worklist;
    // 初始收集所有 phi 节点
    for (auto bb : func->basic_blocks_) {
        for (auto &inst : bb->instr_list_) {
            if (inst->op_id_ == Instruction::PHI) {
                worklist.insert(static_cast<PhiInst *>(inst));
            }
        }
    }

    while (!worklist.empty()) {
        auto it = worklist.begin();
        auto phi = *it;
        worklist.erase(it);

        // phi 可能已被之前的替换删除
        if (phi->parent_ == nullptr) continue;

        std::vector<PhiInst *> phiUsers;
        for (auto use : phi->use_list_) {
            auto *user = dynamic_cast<PhiInst *>(use.val_);
            if (user && user->parent_ != nullptr)
                phiUsers.push_back(user);
        }

        if (eliminateTrivialPhi(phi)) {
            for (auto *user : phiUsers) {
                if (user->parent_ != nullptr)
                    worklist.insert(user);
            }
        }
    }
}
