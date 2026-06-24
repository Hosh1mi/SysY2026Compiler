#include "../../include/mid/opt/ifConversion.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
namespace {

// 判断指令是否可以安全投机执行（在条件不成立时也执行不影响正确性）
// Store / Call / SDiv / SRem 等有副作用或可能 fault 的指令不可投机。
// SysY 数组在边界内，Load / GEP 总是安全的。
static bool isSafeToSpeculate(Instruction *inst) {
    switch (inst->op_id_) {
    case Instruction::Add:  case Instruction::Sub:  case Instruction::Mul:
    case Instruction::And:  case Instruction::Or:   case Instruction::Xor:
    case Instruction::Shl:  case Instruction::LShr: case Instruction::AShr:
    case Instruction::FAdd: case Instruction::FSub: case Instruction::FMul:
    case Instruction::Load: case Instruction::GetElementPtr:
    case Instruction::ZExt: case Instruction::SItoFP: case Instruction::FPtoSI:
    case Instruction::ICmp: case Instruction::FCmp:
        return true;
    default:
        return false; // Store, Call, SDiv, SRem, Alloca, Ret, Br …
    }
}

// 尝试对一个循环做 if-conversion，成功返回 true
static bool tryConvert(Loop &loop, Function *func) {
    // 只处理最内层循环
    if (!loop.children.empty()) return false;
    // 只处理恰好 4 块的循环
    if (loop.blocks.size() != 4) return false;

    BasicBlock *H = loop.header;
    BasicBlock *L = loop.singleLatch();
    if (!L) return false;

    // H 必须有条件分支（到循环体或出口）
    auto *H_br = dynamic_cast<BranchInst *>(H->get_terminator());
    if (!H_br || H_br->num_ops_ != 3) return false;

    // 从 H 的分支找到 B（在循环内的那个后继）
    BasicBlock *B = nullptr;
    {
        auto *td = static_cast<BasicBlock *>(H_br->get_operand(1));
        auto *fd = static_cast<BasicBlock *>(H_br->get_operand(2));
        bool td_in = loop.blocks.count(td), fd_in = loop.blocks.count(fd);
        if (td_in && !fd_in)       B = td;
        else if (fd_in && !td_in)  B = fd;
        else return false;
    }
    if (B == H || B == L) return false;

    // B 必须有条件分支，两个目标分别为 T（if-body）和 L（latch）
    auto *B_br = dynamic_cast<BranchInst *>(B->get_terminator());
    if (!B_br || B_br->num_ops_ != 3) return false;

    BasicBlock *T = nullptr;
    bool true_is_T = false; // B_cond==true 时跳到 T？
    {
        auto *td = static_cast<BasicBlock *>(B_br->get_operand(1));
        auto *fd = static_cast<BasicBlock *>(B_br->get_operand(2));
        if (td == L && loop.blocks.count(fd) && fd != H && fd != B) {
            T = fd; true_is_T = false; // false 分支是 T
        } else if (fd == L && loop.blocks.count(td) && td != H && td != B) {
            T = td; true_is_T = true;  // true  分支是 T
        } else return false;
    }

    // T 必须无条件跳到 L
    auto *T_br = dynamic_cast<BranchInst *>(T->get_terminator());
    if (!T_br || T_br->num_ops_ != 1) return false;
    if (static_cast<BasicBlock *>(T_br->get_operand(0)) != L) return false;

    // L 必须无条件跳回 H
    auto *L_br = dynamic_cast<BranchInst *>(L->get_terminator());
    if (!L_br || L_br->num_ops_ != 1) return false;
    if (static_cast<BasicBlock *>(L_br->get_operand(0)) != H) return false;

    // 安全检查：T 的所有非终止指令必须可投机执行
    for (auto *inst : T->instr_list_) {
        if (inst->is_br()) continue;
        if (!isSafeToSpeculate(inst)) return false;
    }

    // 收集 T 的非终止指令（复制一份，因为迭代过程中会修改 instr_list_）
    std::vector<Instruction *> t_insts;
    for (auto *inst : T->instr_list_) {
        if (!inst->is_br()) t_insts.push_back(inst);
    }

    // L 的 phi 节点：每个必须恰好有来自 B 和 T 的入边
    std::vector<PhiInst *> latch_phis;
    for (auto *inst : L->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        bool hasB = false, hasT = false;
        for (unsigned i = 1; i < phi->num_ops_; i += 2) {
            auto *src = static_cast<BasicBlock *>(phi->get_operand(i));
            if (src == B) hasB = true;
            if (src == T) hasT = true;
        }
        if (!hasB || !hasT) return false;
        latch_phis.push_back(phi);
    }

    // ─── 变换开始 ───────────────────────────────────────────────────────────

    // 1. 把 T 的指令（不含终止指令）移到 B 的分支之前
    for (auto *inst : t_insts) {
        T->remove_instr(inst);
        B->add_instruction_before_inst(inst, B_br);
    }

    // 2. 用 select 替换 L 中的每个 phi，插入到 B 的分支之前
    Value *B_cond = B_br->get_operand(0);
    for (auto *phi : latch_phis) {
        Value *val_B = nullptr, *val_T = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *src = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (src == B) val_B = phi->get_operand(i);
            if (src == T) val_T = phi->get_operand(i);
        }
        // select(cond, tv, fv): cond==true 时取 tv
        Value *tv = true_is_T ? val_T : val_B;
        Value *fv = true_is_T ? val_B : val_T;
        auto *sel = new SelectInst(B_cond, tv, fv, phi->type_);
        B->add_instruction_before_inst(sel, B_br);
        phi->replace_all_use_with(sel);
        L->delete_instr(phi);
    }

    // 3. 更新 B 的分支：去掉 B→T 的 CFG 边，改为无条件跳 L
    B->remove_succ_basic_block(T);
    T->remove_pre_basic_block(B);
    B->delete_instr(B_br);
    // BranchInst(L, B) 内部会调用 L->add_pre(B) 和 B->add_succ(L)，
    // 两者均有去重检查（L 已是 B 的 succ），所以不会产生重复边。
    new BranchInst(L, B);

    // 4. 清理 T 并从函数中移除
    T->delete_instr(T_br); // 清除 T_br 对 L 的 use_list_ 引用
    func->remove_bb(T);    // 从 basic_blocks_ 移除，并通过 succ_bbs_ 清理 L 的 pre 列表

    return true;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

void IfConversion::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

PreservedAnalyses IfConversion::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool IfConversion::runOnFunction(Function *func) {
    bool changed = false;
    bool any = true;
    while (any) {
        any = false;
        LoopInfo LI;
        LI.analyze(func);
        for (auto &lp : LI.allLoops()) {
            if (tryConvert(*lp, func)) {
                any = true;
                changed = true;
                break; // CFG 已变，重新分析
            }
        }
    }
    return changed;
}
