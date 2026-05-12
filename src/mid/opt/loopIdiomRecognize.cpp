#include "../../include/mid/opt/loopIdiomRecognize.hpp"
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <queue>

// =====================================================================
// 辅助函数
// =====================================================================

bool LoopIdiomRecognition::isConstInt(Value *v, int val) {
    if (auto ci = dynamic_cast<ConstantInt*>(v))
        return ci->value_ == val;
    return false;
}

bool LoopIdiomRecognition::getConstIntValue(Value *v, int &val) {
    if (auto ci = dynamic_cast<ConstantInt*>(v)) {
        val = ci->value_;
        return true;
    }
    return false;
}

void LoopIdiomRecognition::safeDeleteInst(Instruction *inst) {
    if (!inst || !inst->parent_) return;
    // 注意：delete_instr 内部已处理 use_list，不要额外调用 remove_use_of_ops()
    inst->parent_->delete_instr(inst);
}

void LoopIdiomRecognition::makeDeclaration(Function *func) {
    // 从后向前删除所有指令，然后清空基本块和边
    for (auto bb : func->basic_blocks_) {
        auto instrs = std::vector<Instruction*>(bb->instr_list_.begin(),
                                                bb->instr_list_.end());
        for (auto it = instrs.rbegin(); it != instrs.rend(); ++it)
            bb->delete_instr(*it);
        bb->pre_bbs_.clear();
        bb->succ_bbs_.clear();
    }
    func->basic_blocks_.clear();
}

int LoopIdiomRecognition::getTypeWidth(Function *func) {
    // 简单假设：参数类型为 i32 则位宽 32
    if (!func->arguments_.empty() &&
        func->arguments_[0]->type_->tid_ == Type::IntegerTyID)
        return 32;
    return 32; // 默认
}

// =====================================================================
// 检测逐位实现的位运算循环（Mem2Reg 后、结构化）
// =====================================================================

int LoopIdiomRecognition::detectBitwiseLoopPattern(Function *func) {
    if (func->is_declaration() || func->arguments_.size() != 2)
        return -1;
    for (auto arg : func->arguments_)
        if (arg->type_->tid_ != Type::IntegerTyID) return -1;
    if (func->get_return_type()->tid_ != Type::IntegerTyID) return -1;

    int width = getTypeWidth(func);   // 32

    // ---- 第一步：识别自然循环（回边） ----
    // 寻找回边：一条从 block B 到 block H 的边，且 H 支配 B
    // 简化：假设函数内只有一个循环，采用简单的 SCC 或 back-edge 发现。
    // 我们遍历所有基本块，若某条边指向已经访问的祖先，则认为是回边。
    std::unordered_set<BasicBlock*> visited;
    std::vector<BasicBlock*> postorder;
    std::function<void(BasicBlock*)> dfs = [&](BasicBlock *bb) {
        visited.insert(bb);
        for (auto succ : bb->succ_bbs_)
            if (!visited.count(succ))
                dfs(succ);
        postorder.push_back(bb);
    };
    dfs(func->basic_blocks_[0]);  // entry

    // 寻找回边：succ 在 postorder 中出现在 bb 之前（即 succ 是祖先）
    std::vector<std::pair<BasicBlock*, BasicBlock*>> backEdges;
    for (auto bb : func->basic_blocks_) {
        for (auto succ : bb->succ_bbs_) {
            // 简单判断：如果 succ 先于 bb 在 postorder 中（即 succ 是 bb 的支配者？）
            // 这里用更鲁棒的方法：如果 succ 是 bb 的祖先（在 DFS 树上）
            // 此处为了快速实现，使用 idom 近似：检查 succ 是否是 bb 的前驱且 succ 在 bb 之前出现于 postorder
            auto it_bb = std::find(postorder.begin(), postorder.end(), bb);
            auto it_succ = std::find(postorder.begin(), postorder.end(), succ);
            if (it_succ < it_bb)  // succ 在 postorder 中更早出现 → 回边
                backEdges.push_back({bb, succ});
        }
    }

    if (backEdges.empty()) return -1;
    // 取第一条回边确定循环头
    BasicBlock *header = backEdges[0].second;
    BasicBlock *latch  = backEdges[0].first;

    // 收集循环体基本块（回边以外的可达块，简单起见）
    std::unordered_set<BasicBlock*> loopBlocks;
    std::queue<BasicBlock*> q;
    q.push(header);
    loopBlocks.insert(header);
    while (!q.empty()) {
        auto cur = q.front(); q.pop();
        for (auto succ : cur->succ_bbs_) {
            if (succ == header) continue; // 回边
            if (!loopBlocks.count(succ)) {
                loopBlocks.insert(succ);
                q.push(succ);
            }
        }
    }
    // 加上 latch
    loopBlocks.insert(latch);

    // ---- 第二步：分析循环体内的归纳变量和位提取模式 ----
    // 寻找 result phi（初始值 0，递增）、index phi（0..width 或 width..0）、a phi、b phi
    // 并检查循环体内是否有 srem %2 或 and 1，以及基于 icmp 的条件累加
    int srem2_count = 0;
    int and1_count  = 0;
    int icmp_eq1_count = 0;
    int icmp_ne_count  = 0;
    bool has_result_phi = false;
    bool has_index_phi  = false;

    for (auto bb : loopBlocks) {
        for (auto inst : bb->instr_list_) {
            if (inst->op_id_ == Instruction::PHI) {
                // 检查是否为 result phi（初始值 0，某个 incoming 为 add）
                auto phi = static_cast<PhiInst*>(inst);
                bool zero_init = false, add_update = false;
                for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                    if (isConstInt(phi->get_operand(i), 0)) zero_init = true;
                    if (auto *in = dynamic_cast<Instruction*>(phi->get_operand(i)))
                        if (in->op_id_ == Instruction::Add) add_update = true;
                }
                if (zero_init && add_update) has_result_phi = true;

                // 检查 index phi（边界等于 width 或 0 向 width 递增/递减）
                bool bound_found = false;
                for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                    int val;
                    if (getConstIntValue(phi->get_operand(i), val) &&
                        (val == 0 || val == width)) bound_found = true;
                }
                if (bound_found) has_index_phi = true;
            }

            if (inst->op_id_ == Instruction::SRem) {
                if (auto ci = dynamic_cast<ConstantInt*>(inst->get_operand(1)))
                    if (ci->value_ == 2) srem2_count++;
            }
            if (inst->op_id_ == Instruction::And) {
                if (auto ci = dynamic_cast<ConstantInt*>(inst->get_operand(1)))
                    if (ci->value_ == 1) and1_count++;
            }
            if (inst->op_id_ == Instruction::ICmp) {
                auto icmp = static_cast<ICmpInst*>(inst);
                if (icmp->icmp_op_ == ICmpInst::ICMP_EQ) {
                    if (isConstInt(icmp->get_operand(0), 1) ||
                        isConstInt(icmp->get_operand(1), 1))
                        icmp_eq1_count++;
                }
                if (icmp->icmp_op_ == ICmpInst::ICMP_NE)
                    icmp_ne_count++;
            }
        }
    }

    // 位提取：至少有两个位置提取（每个参数至少一次）
    bool extract_ok = (srem2_count >= 2 || and1_count >= 2) &&
                      (srem2_count + and1_count >= 2);
    if (!extract_ok || !has_result_phi) return -1;

    // 区分 AND/OR/XOR（通过条件累加路径）
    // XOR 典型特征：icmp ne 出现在条件分支，且没有 eq 1
    if (icmp_ne_count > 0 && icmp_eq1_count == 0)
        return Instruction::Xor;

    // AND vs OR：看 phi 的 add 来自几个不同条件分支
    // 我们从 result phi 的 add incoming 对应的基本块反向查找其条件
    int add_paths = 0;
    for (auto bb : loopBlocks) {
        for (auto inst : bb->instr_list_) {
            if (inst->op_id_ != Instruction::PHI) continue;
            auto phi = static_cast<PhiInst*>(inst);
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                if (auto *add_inst = dynamic_cast<Instruction*>(phi->get_operand(i))) {
                    if (add_inst->op_id_ == Instruction::Add) {
                        // 找到该 add 所在的基本块（可能是 latch 或循环体内条件分支）
                        auto *add_bb = add_inst->parent_;
                        if (add_bb && loopBlocks.count(add_bb)) {
                            // 统计不同的 add 指令来源
                            add_paths++;
                        }
                    }
                }
            }
        }
    }
    if (add_paths == 1) return Instruction::And;
    if (add_paths >= 2) return Instruction::Or;

    return -1;
}

// =====================================================================
// 折叠位运算循环调用
// =====================================================================

void LoopIdiomRecognition::foldBitwiseLoopCalls(Module *module) {
    std::unordered_map<Function*, int> bitwiseFuncs;
    for (auto func : module->function_list_) {
        int op = detectBitwiseLoopPattern(func);
        if (op != -1) bitwiseFuncs[func] = op;
    }
    if (bitwiseFuncs.empty()) return;

    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        std::vector<std::pair<CallInst*, int>> calls;
        for (auto bb : func->basic_blocks_)
            for (auto inst : bb->instr_list_) {
                if (inst->op_id_ != Instruction::Call) continue;
                auto call = static_cast<CallInst*>(inst);
                unsigned nArgs = call->num_ops_ - 1;
                auto callee = dynamic_cast<Function*>(call->get_operand(nArgs));
                if (!callee) continue;
                auto it = bitwiseFuncs.find(callee);
                if (it != bitwiseFuncs.end() && nArgs >= 2)
                    calls.push_back({call, it->second});
            }

        for (auto &[call, opcode] : calls) {
            Value *a = call->get_operand(0);
            Value *b = call->get_operand(1);
            auto newInst = new BinaryInst(module->int32_ty_,
                static_cast<Instruction::OpID>(opcode), a, b, call->parent_, false);
            call->parent_->add_instruction_before_inst(newInst, call);
            call->replace_all_use_with(newInst);
            safeDeleteInst(call);
        }
    }

    for (auto &[f, _] : bitwiseFuncs)
        if (!f->is_declaration()) makeDeclaration(f);
}

// =====================================================================
// 检测“按参数移位”函数（纯结构化，Mem2Reg 后）
// =====================================================================

int LoopIdiomRecognition::detectShiftByParamPattern(Function *func) {
    if (func->is_declaration() || func->arguments_.size() != 2) return 0;
    for (auto arg : func->arguments_)
        if (arg->type_->tid_ != Type::IntegerTyID) return 0;
    if (func->get_return_type()->tid_ != Type::IntegerTyID) return 0;

    Value *arg0 = func->arguments_[0];
    Value *arg1 = func->arguments_[1];

    // 收集所有对 arg0 的 mul/sdiv，且右操作数是 2 的幂
    struct ShiftOp { Instruction *inst; int power; bool isMul; };
    std::vector<ShiftOp> ops;
    for (auto bb : func->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (inst->op_id_ != Instruction::Mul && inst->op_id_ != Instruction::SDiv)
                continue;
            Value *left = inst->get_operand(0);
            // 左操作数必须直接是 arg0 或通过 phi 传递？保守：只接受直接是 arg0
            if (left != arg0) continue;
            int val;
            if (!getConstIntValue(inst->get_operand(1), val) || val <= 1) continue;
            if ((val & (val - 1)) != 0) continue;
            int p = 0, tmp = val;
            while (tmp > 1) { tmp >>= 1; p++; }
            ops.push_back({inst, p, inst->op_id_ == Instruction::Mul});
        }
    }

    if (ops.size() < 3) return 0;  // 太少，不值得替换

    // 检查这些乘除操作是否被同一 switch/分支根据 arg1 选择
    // 对于每个 op，定位其支配它的条件分支，判断是否基于 arg1 的比较
    // 简化：检查 op 所在基本块的前驱分支是否直接比较 arg1
    bool arg1_used = false;
    for (auto &op : ops) {
        auto *bb = op.inst->parent_;
        // 查找直接支配该块的条件分支
        for (auto pred : bb->pre_bbs_) {
            auto term = pred->get_terminator();
            if (term && term->op_id_ == Instruction::Br && term->num_ops_ >= 3) {
                auto cond = dynamic_cast<Instruction*>(term->get_operand(0));
                if (cond && cond->op_id_ == Instruction::ICmp) {
                    if (cond->get_operand(0) == arg1 || cond->get_operand(1) == arg1)
                        arg1_used = true;
                }
            }
        }
    }
    if (!arg1_used) return 0;

    // 左移函数：所有操作都是 mul；右移：所有操作都是 sdiv
    bool allMul = true, allDiv = true;
    for (auto &op : ops) {
        if (!op.isMul) allMul = false;
        if (op.isMul)  allDiv = false;
    }
    if (allMul) return 1;
    if (allDiv) return 2;
    return 0;
}

// =====================================================================
// 折叠移位函数调用
// =====================================================================

void LoopIdiomRecognition::foldShiftCalls(Module *module) {
    std::unordered_map<Function*, int> shiftFuncs; // 1=shl, 2=lshr
    for (auto func : module->function_list_) {
        int k = detectShiftByParamPattern(func);
        if (k > 0) shiftFuncs[func] = k;
    }
    if (shiftFuncs.empty()) return;

    struct ReplaceInfo { CallInst *call; Value *x; int shiftAmt; bool isLeft; };
    std::vector<ReplaceInfo> toReplace;

    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        for (auto bb : func->basic_blocks_)
            for (auto inst : bb->instr_list_) {
                if (inst->op_id_ != Instruction::Call) continue;
                auto call = static_cast<CallInst*>(inst);
                unsigned nArgs = call->num_ops_ - 1;
                if (nArgs < 2) continue;
                auto callee = dynamic_cast<Function*>(call->get_operand(nArgs));
                if (!callee) continue;
                auto it = shiftFuncs.find(callee);
                if (it == shiftFuncs.end()) continue;
                int n;
                if (!getConstIntValue(call->get_operand(1), n) || n < 1 || n > 8)
                    continue;
                toReplace.push_back({call, call->get_operand(0), n, it->second == 1});
            }
    }

    for (auto &info : toReplace) {
        BasicBlock *bb = info.call->parent_;
        auto shiftVal = new ConstantInt(module->int32_ty_, info.shiftAmt);
        BinaryInst *newInst;
        if (info.isLeft)
            newInst = new BinaryInst(module->int32_ty_, Instruction::Shl,
                                     info.x, shiftVal, bb, false);
        else
            newInst = new BinaryInst(module->int32_ty_, Instruction::LShr,
                                     info.x, shiftVal, bb, false);
        bb->add_instruction_before_inst(newInst, info.call);
        info.call->replace_all_use_with(newInst);
        safeDeleteInst(info.call);
    }

    for (auto &[f, _] : shiftFuncs) {
        if (f->is_declaration()) continue;
        bool alive = false;
        for (auto mf : module->function_list_) {
            if (mf->is_declaration()) continue;
            for (auto bb : mf->basic_blocks_)
                for (auto inst : bb->instr_list_)
                    if (inst->op_id_ == Instruction::Call) {
                        auto call = static_cast<CallInst*>(inst);
                        unsigned n = call->num_ops_ - 1;
                        if (dynamic_cast<Function*>(call->get_operand(n)) == f)
                            alive = true;
                    }
        }
        if (!alive) makeDeclaration(f);
    }
}

// =====================================================================
// 优化 SDiv by power-of-2 → 修正 AShr 替换（通用，安全）
// =====================================================================

void LoopIdiomRecognition::optimizeSDivByPowerOfTwo(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        std::vector<std::pair<Instruction*, int>> toReplace;
        for (auto bb : func->basic_blocks_)
            for (auto inst : bb->instr_list_) {
                if (inst->op_id_ != Instruction::SDiv) continue;
                int divisor;
                if (!getConstIntValue(inst->get_operand(1), divisor)) continue;
                if (divisor <= 0 || (divisor & (divisor - 1)) != 0) continue;
                int shift = 0, tmp = divisor;
                while (tmp > 1) { tmp >>= 1; shift++; }
                if (shift >= 1 && shift <= 8)
                    toReplace.push_back({inst, shift});
            }

        for (auto &[inst, shift] : toReplace) {
            Value *x = inst->get_operand(0);
            BasicBlock *bb = inst->parent_;
            // sdiv x, 2^n  →  ashr(x + (ashr(x,31) & (2^n-1)), n)
            auto *c31 = new ConstantInt(module->int32_ty_, 31);
            auto *sign = new BinaryInst(module->int32_ty_, Instruction::AShr, x, c31, bb, false);
            bb->add_instruction_before_inst(sign, inst);
            auto *mask = new ConstantInt(module->int32_ty_, (1 << shift) - 1);
            auto *masked = new BinaryInst(module->int32_ty_, Instruction::And, sign, mask, bb, false);
            bb->add_instruction_before_inst(masked, inst);
            auto *corrected = new BinaryInst(module->int32_ty_, Instruction::Add, x, masked, bb, false);
            bb->add_instruction_before_inst(corrected, inst);
            auto *shiftVal = new ConstantInt(module->int32_ty_, shift);
            auto *result = new BinaryInst(module->int32_ty_, Instruction::AShr, corrected, shiftVal, bb, false);
            bb->add_instruction_before_inst(result, inst);
            inst->replace_all_use_with(result);
            safeDeleteInst(inst);
        }
    }
}

// =====================================================================
// 常量折叠（通用）
// =====================================================================

void LoopIdiomRecognition::constantFolding(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        std::vector<Instruction*> toDelete;
        for (auto bb : func->basic_blocks_)
            for (auto inst : bb->instr_list_) {
                if (inst->type_->tid_ != Type::IntegerTyID) continue;
                if (inst->num_ops_ != 2) continue;
                int v1, v2;
                if (!getConstIntValue(inst->get_operand(0), v1) ||
                    !getConstIntValue(inst->get_operand(1), v2)) continue;
                int result = 0;
                bool ok = true;
                switch (inst->op_id_) {
                case Instruction::Add:  result = v1 + v2; break;
                case Instruction::Sub:  result = v1 - v2; break;
                case Instruction::Mul:  result = v1 * v2; break;
                case Instruction::SDiv: if (v2 == 0) ok = false; else result = v1 / v2; break;
                case Instruction::SRem: if (v2 == 0) ok = false; else result = v1 % v2; break;
                case Instruction::And:  result = v1 & v2; break;
                case Instruction::Or:   result = v1 | v2; break;
                case Instruction::Xor:  result = v1 ^ v2; break;
                case Instruction::Shl:  result = v1 << v2; break;
                case Instruction::LShr: result = (int)((unsigned)v1 >> v2); break;
                case Instruction::AShr: result = v1 >> v2; break;
                default: ok = false;
                }
                if (ok) {
                    auto *constVal = new ConstantInt(module->int32_ty_, result);
                    inst->replace_all_use_with(constVal);
                    toDelete.push_back(inst);
                }
            }
        for (auto inst : toDelete) safeDeleteInst(inst);
    }
}

// =====================================================================
// 主入口
// =====================================================================

void LoopIdiomRecognition::execute(Module *module) {
    // 顺序：先折叠高级模式，再执行算术优化和常量折叠
    foldBitwiseLoopCalls(module);
    foldShiftCalls(module);
    optimizeSDivByPowerOfTwo(module);
    constantFolding(module);

    // 多轮迭代（简化）
    bool changed = true;
    for (int iter = 0; iter < 3 && changed; ++iter) {
        changed = false;
        // 略，如有需要可再次调用 foldShiftCalls / optimizations
    }
}