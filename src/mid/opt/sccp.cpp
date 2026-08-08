#include "../../include/mid/opt/sccp.hpp"
#include "../../include/mid/analysis/constantEvaluator.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <queue>
#include <set>
// #define SCCP_DEBUG

// ── helpers ─────────────────────────────────────────────────────────

static void removeBBFromPhi(BasicBlock *deadBlock, BasicBlock *succ) {
    for (auto *instr : succ->instr_list_) {
        if (!instr->is_phi()) continue;
        auto *phi = static_cast<PhiInst *>(instr);
        for (int i = phi->num_ops_ - 1; i >= 0; i -= 2) {
            if (phi->get_operand(i) == deadBlock)
                phi->remove_operands(i - 1, i);
        }
    }
}

// 折叠分支后，被切断的子图（含自环/环）从入口不可达。必须立即删除：
// 留下的不可达循环体的 phi 会被后续 pass 收缩成自引用指令，
// 使 InstCombine 等基于"def 链有限"假设的改写无法终止。
static void removeUnreachableBlocks(Function *func) {
    if (func->basic_blocks_.empty()) return;
    auto *entry = func->basic_blocks_.front();

    // 可达性沿 terminator 的基本块操作数传播，不走 succ_bbs_：
    // 个别 pass 改写分支目标时遗漏维护 succ/pre 链表，terminator 才是事实。
    std::set<BasicBlock *> reachable;
    std::queue<BasicBlock *> worklist;
    reachable.insert(entry);
    worklist.push(entry);
    while (!worklist.empty()) {
        auto *bb = worklist.front();
        worklist.pop();
        auto *term = bb->get_terminator();
        if (!term) continue;
        for (unsigned i = 0; i < term->num_ops_; ++i) {
            auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
            if (succ && reachable.insert(succ).second)
                worklist.push(succ);
        }
    }

    std::vector<BasicBlock *> dead;
    for (auto *bb : func->basic_blocks_) {
        if (!reachable.count(bb))
            dead.push_back(bb);
    }

    for (auto *bb : dead) {
        auto *term = bb->get_terminator();
        if (term) {
            for (unsigned i = 0; i < term->num_ops_; ++i) {
                auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
                if (succ && reachable.count(succ)) {
                    removeBBFromPhi(bb, succ);
                    succ->remove_pre_basic_block(bb);
                }
            }
        }
        std::vector<Instruction *> instrs(bb->instr_list_.begin(), bb->instr_list_.end());
        for (auto *instr : instrs)
            bb->delete_instr(instr);
    }

    // 清掉存活块中残留的指向死块的链接，再统一移除死块。
    // 死块自身的链表先清空，避免 remove_bb 解引用其中可能已失效的指针。
    std::set<BasicBlock *> deadSet(dead.begin(), dead.end());
    for (auto *bb : func->basic_blocks_) {
        if (deadSet.count(bb)) continue;
        auto isDead = [&](BasicBlock *b) { return deadSet.count(b) > 0; };
        bb->pre_bbs_.erase(
            std::remove_if(bb->pre_bbs_.begin(), bb->pre_bbs_.end(), isDead),
            bb->pre_bbs_.end());
        bb->succ_bbs_.erase(
            std::remove_if(bb->succ_bbs_.begin(), bb->succ_bbs_.end(), isDead),
            bb->succ_bbs_.end());
    }
    for (auto *bb : dead) {
        bb->pre_bbs_.clear();
        bb->succ_bbs_.clear();
        func->remove_bb(bb);
    }
}

// ── Lattice value (stores int directly, no allocation) ─────────────

class LatticeValue {
public:
    enum State : unsigned char { UNDEF, CONSTANT, OVERDEF };
private:
    State state_;
    int val_;  // valid only for CONSTANT
public:
    LatticeValue() : state_(UNDEF), val_(0) {}
    LatticeValue(State s, int v = 0) : state_(s), val_(v) {}
    static LatticeValue constant(int v) { return {CONSTANT, v}; }

    bool isUndef()    const { return state_ == UNDEF; }
    bool isConstant() const { return state_ == CONSTANT; }
    bool isOverdef()  const { return state_ == OVERDEF; }
    int  constVal()   const { return val_; }

    bool operator==(const LatticeValue &o) const {
        if (state_ != o.state_) return false;
        if (state_ == CONSTANT) return val_ == o.val_;
        return true;
    }
    bool operator!=(const LatticeValue &o) const { return !(*this == o); }
};

static LatticeValue LV_UNDEF(LatticeValue::UNDEF);
static LatticeValue LV_OVERDEF(LatticeValue::OVERDEF);

static LatticeValue meet(const LatticeValue &a, const LatticeValue &b) {
    if (a.isOverdef() || b.isOverdef()) return LV_OVERDEF;
    if (a.isUndef()) return b;
    if (b.isUndef()) return a;
    if (a.constVal() == b.constVal()) return a;
    return LV_OVERDEF;
}

// ── Transfer helper (no allocation) ──────────────────────

static int evalBinOp(Instruction::OpID op, int a, int b, bool &valid) {
    valid = true;
    int result = 0;
    if (ConstantEvaluator::foldIntegerBinary(op, a, b, result))
        return result;
    switch (op) {
    case Instruction::And:  return a & b;
    case Instruction::Or:   return a | b;
    case Instruction::Xor:  return a ^ b;
    case Instruction::Shl:  return a << b;
    case Instruction::AShr: return a >> b;
    default: valid = false; return 0;
    }
}

// ── SCCP core ───────────────────────────────────────────────────────

bool SCCP::runOnFunction(Function *func) {
    std::map<Value*, LatticeValue> lattice;

    // Init: constants are known, args are overdef, rest starts undef
    for (auto *bb : func->basic_blocks_)
        for (auto *inst : bb->instr_list_)
            lattice[inst] = LV_UNDEF;
    for (auto *ci : func->arguments_)
        lattice[ci] = LV_OVERDEF;

    // ── Fixpoint propagation ──────────────────────────────────────
    auto getLat = [&](Value *v) {
        if (auto *ci = dynamic_cast<ConstantInt*>(v))
            return LatticeValue::constant(ci->value_);
        auto it = lattice.find(v);
        return it != lattice.end() ? it->second : LV_UNDEF;
    };

    std::set<BasicBlock*> reachable;
    if (!func->basic_blocks_.empty())
        reachable.insert(func->basic_blocks_[0]);

    bool changed = true;
    while (changed) {
        changed = false;

        // BFS reachable blocks (respects constant branches)
        std::queue<BasicBlock*> q;
        for (auto *bb : reachable)
            q.push(bb);
        while (!q.empty()) {
            auto *b = q.front(); q.pop();
            if (b->instr_list_.empty()) continue;
            auto *t = b->get_terminator();
            if (!t || !t->is_br()) continue;
            auto *br = dynamic_cast<BranchInst*>(t);
            if (br && br->num_ops_ == 3) {
                auto cond = getLat(br->get_operand(0));
                if (cond.isConstant()) {
                    auto *taken = static_cast<BasicBlock*>(
                        br->get_operand(cond.constVal() ? 1 : 2));
                    if (reachable.insert(taken).second) {
                        q.push(taken);
                        changed = true;
                    }
                } else {
                    for (unsigned i = 1; i <= 2; i++) {
                        auto *s = static_cast<BasicBlock*>(br->get_operand(i));
                        if (reachable.insert(s).second) {
                            q.push(s);
                            changed = true;
                        }
                    }
                }
            } else if (br && br->num_ops_ == 1) {
                auto *s = static_cast<BasicBlock*>(br->get_operand(0));
                if (reachable.insert(s).second) {
                    q.push(s);
                    changed = true;
                }
            }
        }

        for (auto *bb : reachable) {
            for (auto *inst : bb->instr_list_) {
                if (inst->is_phi()) {
                    auto *phi = static_cast<PhiInst*>(inst);
                    LatticeValue val = LV_UNDEF;
                    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                        if (!reachable.count(static_cast<BasicBlock*>(phi->get_operand(i + 1))))
                            continue;
                        val = meet(val, getLat(phi->get_operand(i)));
                    }
                    auto &cur = lattice[phi];
                    if (cur != val) { cur = val; changed = true; }
                    continue;
                }
                if (inst->isTerminator()) continue;

                LatticeValue result = LV_OVERDEF;

                if (inst->is_binary()) {
                    auto *bin = static_cast<BinaryInst*>(inst);
                    auto l1 = getLat(bin->get_operand(0));
                    auto l2 = getLat(bin->get_operand(1));
                    if (l1.isOverdef() || l2.isOverdef()) goto done;
                    if (l1.isUndef() || l2.isUndef()) { result = LV_UNDEF; goto done; }
                    // Special cases: mul/and by 0, or by -1
                    if (inst->op_id_ == Instruction::Mul || inst->op_id_ == Instruction::And) {
                        if ((l1.isConstant() && l1.constVal() == 0) ||
                            (l2.isConstant() && l2.constVal() == 0))
                            { result = LatticeValue::constant(0); goto done; }
                    }
                    if (inst->op_id_ == Instruction::Or) {
                        if ((l1.isConstant() && l1.constVal() == -1) ||
                            (l2.isConstant() && l2.constVal() == -1))
                            { result = LatticeValue::constant(-1); goto done; }
                    }
                    if (l1.isConstant() && l2.isConstant()) {
                        bool ok;
                        int r = evalBinOp(inst->op_id_, l1.constVal(), l2.constVal(), ok);
                        if (ok) result = LatticeValue::constant(r);
                    }
                }
                else if (auto *icmp = dynamic_cast<ICmpInst*>(inst)) {
                    auto l1 = getLat(icmp->get_operand(0));
                    auto l2 = getLat(icmp->get_operand(1));
                    if (l1.isConstant() && l2.isConstant()) {
                        int a = l1.constVal(), b = l2.constVal();
                        bool r = false;
                        switch (icmp->icmp_op_) {
                        case ICmpInst::ICMP_EQ:  r = (a == b); break;
                        case ICmpInst::ICMP_NE:  r = (a != b); break;
                        case ICmpInst::ICMP_SGT: r = (a > b);  break;
                        case ICmpInst::ICMP_SGE: r = (a >= b); break;
                        case ICmpInst::ICMP_SLT: r = (a < b);  break;
                        case ICmpInst::ICMP_SLE: r = (a <= b); break;
                        default: result = LV_OVERDEF; goto done;
                        }
                        result = LatticeValue::constant(r ? 1 : 0);
                    } else if (l1.isUndef() || l2.isUndef()) {
                        result = LV_UNDEF;
                    }
                }
                else if (inst->op_id_ == Instruction::ZExt) {
                    auto l = getLat(inst->get_operand(0));
                    if (l.isConstant()) result = LatticeValue::constant(l.constVal());
                    else if (l.isUndef()) result = LV_UNDEF;
                }
                else if (inst->op_id_ == Instruction::Select) {
                    auto cond = getLat(inst->get_operand(0));
                    if (cond.isConstant()) {
                        result = getLat(inst->get_operand(cond.constVal() ? 1 : 2));
                    } else {
                        auto tv = getLat(inst->get_operand(1));
                        auto fv = getLat(inst->get_operand(2));
                        if (tv == fv && tv.isConstant()) result = tv;
                        else if (cond.isUndef() || tv.isUndef() || fv.isUndef()) result = LV_UNDEF;
                    }
                }
                // calls/loads/stores/alloca and unsupported operations are overdefined

                done:
                auto &cur = lattice[inst];
                if (cur != result) { cur = result; changed = true; }
            }
        }
    }
    // ── Apply: replace constant instructions ────────────────────
    bool changed2 = false;
    int replaced = 0;
    for (auto &[val, lat] : lattice) {
        if (!lat.isConstant()) continue;
        auto *inst = dynamic_cast<Instruction*>(val);
        if (!inst || inst->is_phi() || inst->isTerminator()) continue;
        if (dynamic_cast<ConstantInt*>(val)) continue;
        inst->replace_all_use_with(new ConstantInt(inst->type_, lat.constVal()));
        inst->parent_->delete_instr(inst);
        changed2 = true;
        replaced++;
    }

    // ── Apply: fold constant branches with proper CFG cleanup ────
    int brFolded = 0;
    for (auto *bb : func->basic_blocks_) {
        auto *term = bb->get_terminator();
        auto *br = dynamic_cast<BranchInst*>(term);
        if (!br || br->num_ops_ != 3) continue;
        Value *cond = br->get_operand(0);
        auto condLat = getLat(cond);
        if (!condLat.isConstant()) continue;

        auto *trueDest  = static_cast<BasicBlock*>(br->get_operand(1));
        auto *falseDest = static_cast<BasicBlock*>(br->get_operand(2));
        auto *taken     = condLat.constVal() ? trueDest : falseDest;
        auto *nonTarget = (taken == trueDest) ? falseDest : trueDest;

        if (nonTarget != taken)
            removeBBFromPhi(bb, nonTarget);
        bb->remove_succ_basic_block(trueDest);
        bb->remove_succ_basic_block(falseDest);
        trueDest->remove_pre_basic_block(bb);
        falseDest->remove_pre_basic_block(bb);
        bb->delete_instr(br);
        new BranchInst(taken, bb);
        changed2 = true;
        brFolded++;
    }

    if (brFolded > 0)
        removeUnreachableBlocks(func);


#ifdef SCCP_DEBUG
    if (replaced > 0 || brFolded > 0)
        std::cerr << "[SCCP] " << func->name_ << ": replaced " << replaced
                  << " insts, folded " << brFolded << " branches\n";
#endif
    return changed2;
}

void SCCP::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

PreservedAnalyses SCCP::execute(Module *module, AnalysisManager &AM) {
    return runPass(module, AM).preserved;
}

PassRunResult SCCP::runPass(Module *module, AnalysisManager &) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return {changed, changed ? PreservedAnalyses::none()
                             : PreservedAnalyses::all()};
}
