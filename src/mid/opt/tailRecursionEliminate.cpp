#include "../../include/mid/opt/tailRecursionEliminate.hpp"

#include <vector>

void TailRecursionEliminate::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        if (isTailRecursive(func)) {
            eliminateTailRecursion(func, module);
        }
    }
}

// Check if a self-call matches Pattern 2:
//   call @func + unconditional br to ret_block, where all call uses are
//   phis in ret_block, and at least one phi is the ret operand.
static bool isPattern2TailCall(CallInst *call, Function *func,
                                BasicBlock *target, BasicBlock *term_bb) {
    auto call_term = term_bb->get_terminator();
    (void)call_term; // terminator already validated by caller
    auto targetTerm = target->get_terminator();
    if (!targetTerm || !targetTerm->is_ret()) return false;

    bool all_in_target = true;
    for (auto &use : call->use_list_) {
        auto phi = dynamic_cast<PhiInst *>(use.val_);
        if (!phi || phi->parent_ != target) {
            all_in_target = false;
            break;
        }
    }
    if (!all_in_target) return false;

    if (targetTerm->num_ops_ > 0) {
        Value *retVal = targetTerm->get_operand(0);
        bool usedByRet = false;
        for (auto &use : call->use_list_) {
            if (use.val_ == retVal) { usedByRet = true; break; }
        }
        if (!usedByRet) return false;
    }
    return true;
}

// 检查函数是否包含尾递归调用（允许 ret <call> 或 call + br + phi + ret 两种形式）
// 现在支持部分转换：只要至少有一个尾调用即可，非尾调用的自调用保留不动。
bool TailRecursionEliminate::isTailRecursive(Function *func) {
    bool hasTailCall = false;
    for (auto bb : func->basic_blocks_) {
        for (auto instr : bb->instr_list_) {
            auto call = dynamic_cast<CallInst *>(instr);
            if (!call) continue;
            auto callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
            if (callee != func) continue;          // 不是自调用，跳过

            auto term = bb->get_terminator();
            // 模式 1：ret <call>
            if (term && term->is_ret() && term->num_ops_ > 0 &&
                term->get_operand(0) == call) {
                hasTailCall = true;
                continue;
            }

            // 模式 2：call 后无条件 br 到返回块，返回块中有 phi 使用 call，并最终由 ret 返回
            if (term && term->is_br() && term->num_ops_ == 1) {
                BasicBlock *target = static_cast<BasicBlock *>(term->get_operand(0));
                if (isPattern2TailCall(call, func, target, bb)) {
                    hasTailCall = true;
                    continue;
                }
            }

            // 非尾调用的自调用：部分转换时保留不动，不拒绝整个函数
        }
    }
    return hasTailCall;
}

void TailRecursionEliminate::eliminateTailRecursion(Function *func, Module *module) {
    // 1. 创建循环头块，插入 entry 之后。entry 本身充当 preheader，其内容将融入 header
    auto entry_bb = func->basic_blocks_.front();
    auto *header = new BasicBlock(module, "label_tailrec_header", func);

    auto &bbs = func->basic_blocks_;
    // header 构造时被 add_basic_block 追加到尾部，移出后插入 entry_bb 之后
    bbs.erase(std::remove(bbs.begin(), bbs.end(), header), bbs.end());
    auto entry_it = std::find(bbs.begin(), bbs.end(), entry_bb);
    bbs.insert(entry_it + 1, header);

    auto *builder = new IRStmtBuilder(entry_bb, module);

    // 2. 为每个参数在 header 中创建 phi，初始值来自 entry_bb，并替换所有使用
    std::vector<PhiInst *> arg_phis;
    for (auto arg : func->arguments_) {
        auto phi = PhiInst::create_phi(arg->type_, header);
        phi->name_ = arg->name_;                 // 保留原名
        arg->replace_all_use_with(phi);
        phi->add_phi_pair_operand(arg, entry_bb);

        // 将 phi 插入 header 指令列表头部
        header->add_instruction_front(phi);
        arg_phis.push_back(phi);
    }

    // 3. 将 entry_bb 的内容融入 header：移动所有指令到 header，entry_bb 改为跳转到 header
    //    先保存 entry_bb 的终止指令及后继列表
    auto entry_term = entry_bb->get_terminator();
    std::vector<BasicBlock *> entry_succs = entry_bb->succ_bbs_;

    if (entry_term) {
        entry_bb->remove_instr(entry_term);
    }

    //    清理 entry_bb 的 CFG 出边
    for (auto succ : entry_succs) {
        succ->remove_pre_basic_block(entry_bb);
    }
    entry_bb->succ_bbs_.clear();

    //    将后继块中所有引用 entry_bb 的 phi 操作数替换为 header
    for (auto succ : entry_succs) {
        for (auto &inst : succ->instr_list_) {
            auto *phi = dynamic_cast<PhiInst *>(inst);
            if (!phi) break;  // phi 总是在块头部，遇非 phi 即停止
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                if (phi->get_operand(i + 1) == entry_bb) {
                    phi->set_operand(i + 1, header);
                }
            }
        }
    }

    //    将 entry_bb 的非终止指令移到 header（phi 之后）
    std::vector<Instruction *> instrs_to_move;
    for (auto instr : entry_bb->instr_list_) {
        instrs_to_move.push_back(instr);
    }
    for (auto instr : instrs_to_move) {
        entry_bb->remove_instr(instr);
        header->add_instruction(instr);
    }

    //    将原终止指令加入 header，并建立 CFG 边
    if (entry_term) {
        header->add_instruction(entry_term);
        for (auto succ : entry_succs) {
            succ->add_pre_basic_block(header);
            header->add_succ_basic_block(succ);
        }
    }

    //    entry_bb 变为 preheader：仅一条无条件跳转到 header
    builder->set_insert_point(entry_bb);
    builder->create_br(header);

    // 4. 收集所有尾调用块的信息（bb, call, target_ret_bb），只收集经模式1/2验证的尾调用
    struct TailCallSite {
        BasicBlock *bb;
        CallInst *call;
        BasicBlock *ret_bb;   // 模式1为nullptr
        bool is_ret_form;
    };
    std::vector<TailCallSite> sites;

    for (auto bb : func->basic_blocks_) {
        auto term = bb->get_terminator();
        if (!term) continue;

        CallInst *call = nullptr;
        BasicBlock *target = nullptr;
        bool ret_form = false;

        if (term->is_ret() && term->num_ops_ > 0) {
            // 模式 1: ret <call>
            call = dynamic_cast<CallInst *>(term->get_operand(0));
            if (call) ret_form = true;
        } else if (term->is_br() && term->num_ops_ == 1) {
            // 模式 2: call + br ret_bb, verify with isPattern2TailCall
            target = static_cast<BasicBlock *>(term->get_operand(0));
            for (auto rit = bb->instr_list_.rbegin(); rit != bb->instr_list_.rend(); ++rit) {
                if (*rit == term) continue;
                auto *c = dynamic_cast<CallInst *>(*rit);
                if (c) {
                    auto *callee = dynamic_cast<Function *>(c->get_operand(c->num_ops_ - 1));
                    if (callee == func && isPattern2TailCall(c, func, target, bb)) {
                        call = c;
                        ret_form = false;
                        break;  // 只取最后一个匹配的尾调用
                    }
                }
            }
        }

        if (!call) continue;
        auto *callee = dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
        if (callee != func) continue;

        sites.push_back({bb, call, target, ret_form});
    }

    // 5. 对每个尾调用块：添加实参到 header 的 phi，并转换为跳转到 header 的循环
    for (auto &site : sites) {
        auto bb = site.bb;
        auto call = site.call;

        // 添加实参到 header 的 phi
        for (size_t i = 0; i < func->arguments_.size(); ++i) {
            arg_phis[i]->add_phi_pair_operand(call->get_operand(i), bb);
        }

        // 转换控制流
        if (site.is_ret_form) {
            auto term = bb->get_terminator();
            bb->delete_instr(term);
            bb->delete_instr(call);
            builder->set_insert_point(bb);
            builder->create_br(header);
        } else {
            auto term = bb->get_terminator();
            bb->delete_instr(call);
            BasicBlock *ret_bb = site.ret_bb;
            bb->delete_instr(term);
            bb->remove_succ_basic_block(ret_bb);
            ret_bb->remove_pre_basic_block(bb);
            builder->set_insert_point(bb);
            builder->create_br(header);
        }
    }

    // 6. 统一处理返回块中的 phi（仅对 br+phi 模式）
    std::set<BasicBlock *> processed_retbb;
    for (auto &site : sites) {
        if (site.is_ret_form || !site.ret_bb) continue;
        BasicBlock *ret_bb = site.ret_bb;
        if (processed_retbb.count(ret_bb)) continue;
        processed_retbb.insert(ret_bb);

        // 收集该返回块中所有 phi
        std::vector<PhiInst *> phis;
        for (auto &inst : ret_bb->instr_list_) {
            if (auto *phi = dynamic_cast<PhiInst *>(inst))
                phis.push_back(phi);
        }

        for (auto *phi : phis) {
            // 找出所有来自已转换尾调用块的前驱边
            std::vector<unsigned> indices_to_remove;  // 存放 value 索引（偶数）
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                BasicBlock *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
                // 如果这个前驱已经改为跳转到 header（即前驱是 sites 中的某个 bb）
                bool is_tailcall_pred = false;
                for (auto &s : sites) {
                    if (!s.is_ret_form && s.bb == pred) {
                        is_tailcall_pred = true;
                        break;
                    }
                }
                if (is_tailcall_pred) {
                    indices_to_remove.push_back(i);
                }
            }

            // 按从大到小顺序移除，避免索引偏移
            for (auto it = indices_to_remove.rbegin(); it != indices_to_remove.rend(); ++it) {
                unsigned i = *it;
                phi->remove_operands(i, i + 1);
            }

            // 如果 phi 只剩一个输入，用该值替换并删除 phi
            if (phi->num_ops_ == 2) {
                Value *replacement = phi->get_operand(0);
                phi->replace_all_use_with(replacement);
                ret_bb->delete_instr(phi);
            }
        }
    }

    func->set_instr_name();
    delete builder;
}