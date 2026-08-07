#include "../../include/mid/opt/phiOpSink.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

void PhiOpSink::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses PhiOpSink::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, AM);
    }
    return changed ? PreservedAnalyses::cfgAnalyses()
                   : PreservedAnalyses::all();
}

static bool isCommutative(Instruction::OpID op) {
    return op == Instruction::Add ||
           op == Instruction::Mul ||
           op == Instruction::And ||
           op == Instruction::Or ||
           op == Instruction::Xor ||
           op == Instruction::FAdd ||
           op == Instruction::FMul;
}

static bool sameValue(Value *a, Value *b) {
    if (a == b)
        return true;
    auto *ai = dynamic_cast<ConstantInt *>(a);
    auto *bi = dynamic_cast<ConstantInt *>(b);
    if (ai && bi)
        return ai->value_ == bi->value_ && ai->type_ == bi->type_;
    auto *af = dynamic_cast<ConstantFloat *>(a);
    auto *bf = dynamic_cast<ConstantFloat *>(b);
    if (af && bf)
        return af->value_ == bf->value_ && af->type_ == bf->type_;
    return false;
}

static bool sameOperands(BinaryInst *inst, Instruction::OpID op,
                         Value *lhs, Value *rhs) {
    if (!inst || inst->op_id_ != op)
        return false;
    Value *a = inst->get_operand(0);
    Value *b = inst->get_operand(1);
    if (sameValue(a, lhs) && sameValue(b, rhs))
        return true;
    return isCommutative(op) && sameValue(a, rhs) && sameValue(b, lhs);
}

static bool valueDominatesBlock(Value *value,
                                const DominatorTreeAnalysis &DT,
                                BasicBlock *bb) {
    auto *inst = dynamic_cast<Instruction *>(value);
    if (!inst)
        return true;
    if (!inst->parent_)
        return false;
    return DT.dominates(inst->parent_, bb);
}

bool PhiOpSink::trySinkPhi(PhiInst *phi, Function *func, LoopInfo &LI,
                           const DominatorTreeAnalysis &DT) {
    if (!phi || phi->num_ops_ < 2)
        return false;

    for (auto &use : phi->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (auto *userPhi = dynamic_cast<PhiInst *>(user)) {
            if (use.arg_no_ + 1 >= userPhi->num_ops_ ||
                userPhi->get_operand(use.arg_no_ + 1) != phi->parent_)
                return false;
        }
    }

    auto *first = dynamic_cast<BinaryInst *>(phi->get_operand(0));
    if (!first)
        return false;

    Instruction::OpID op = first->op_id_;
    Type *ty = first->type_;
    Value *lhs = first->get_operand(0);
    Value *rhs = first->get_operand(1);
    if (lhs == phi || rhs == phi)
        return false;
    if (!valueDominatesBlock(lhs, DT, phi->parent_) ||
        !valueDominatesBlock(rhs, DT, phi->parent_))
        return false;

    for (unsigned i = 2; i < phi->num_ops_; i += 2) {
        auto *inst = dynamic_cast<BinaryInst *>(phi->get_operand(i));
        if (!sameOperands(inst, op, lhs, rhs))
            return false;
    }

    for (Value *opnd : {lhs, rhs}) {
        auto *p = dynamic_cast<PhiInst *>(opnd);
        if (!p || !p->parent_)
            continue;
        Loop *l = LI.getLoopFor(p->parent_);
        if (l && !l->isInLoop(phi->parent_))
            return false;
    }

    // 临时二分开关：限制成功 sink 的次数,用于定位错译 sink
    if (const char *lim = std::getenv("DEBUG_PHI_OP_SINK_LIMIT")) {
        static int sinkCount = 0;
        if (sinkCount >= atoi(lim))
            return false;
        sinkCount++;
    }

    auto *common = new BinaryInst(ty, op, lhs, rhs, phi->parent_, true);
    Instruction *insertBefore = nullptr;
    for (auto *inst : phi->parent_->instr_list_) {
        if (!inst->is_phi()) {
            insertBefore = inst;
            break;
        }
    }

    bool inserted = insertBefore
        ? phi->parent_->add_instruction_before_inst(common, insertBefore)
        : phi->parent_->add_instruction_before_terminator(common);
    if (!inserted) {
        common->remove_use_of_ops();
        return false;
    }

    if (std::getenv("DEBUG_PHI_OP_SINK"))
        std::cerr << "[PhiOpSink] func=" << func->name_
                  << " block=" << phi->parent_->name_
                  << " phi=%" << phi->name_
                  << " op=" << (int)op << "\n";
    phi->replace_all_use_with(common);
    phi->parent_->delete_instr(phi);
    return true;
}

bool PhiOpSink::runOnFunction(Function *func, AnalysisManager &AM) {
    bool changed = false;
    bool localChanged = true;
    while (localChanged) {
        localChanged = false;
        // sink 不改 CFG,LoopInfo 在 while 轮间仍有效;每轮重建求稳
        LoopInfo &LI = AM.getLoopInfo(func);
        DominatorTreeAnalysis &DT = AM.getDominatorTree(func);
        for (auto *bb : func->basic_blocks_) {
            std::vector<PhiInst *> phis;
            for (auto *inst : bb->instr_list_) {
                if (!inst->is_phi())
                    break;
                phis.push_back(static_cast<PhiInst *>(inst));
            }

            for (auto *phi : phis) {
                if (trySinkPhi(phi, func, LI, DT)) {
                    changed = true;
                    localChanged = true;
                    func->set_instr_name();
                    break;
                }
            }
            if (localChanged)
                break;
        }
    }
    return changed;
}
