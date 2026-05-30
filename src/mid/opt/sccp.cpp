#include "../../include/mid/opt/sccp.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include <map>
#include <queue>
#include <set>

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
    switch (op) {
    case Instruction::Add:  return a + b;
    case Instruction::Sub:  return a - b;
    case Instruction::Mul:  return a * b;
    case Instruction::SDiv: return b ? a / b : (valid = false, 0);
    case Instruction::SRem: return b ? a % b : (valid = false, 0);
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
    bool changed = true;
    int maxIter = 10;
    while (changed && maxIter-- > 0) {
        changed = false;

        // BFS reachable blocks (respects constant branches)
        std::set<BasicBlock*> reachable;
        std::queue<BasicBlock*> q;
        if (!func->basic_blocks_.empty()) {
            q.push(func->basic_blocks_[0]); reachable.insert(func->basic_blocks_[0]);
        }
        while (!q.empty()) {
            auto *b = q.front(); q.pop();
            if (b->instr_list_.empty()) continue;
            auto *t = b->get_terminator();
            if (!t || !t->is_br()) continue;
            auto *br = dynamic_cast<BranchInst*>(t);
            if (br && br->num_ops_ == 3) {
                auto itL = lattice.find(br->get_operand(0));
                if (itL != lattice.end() && itL->second.isConstant()) {
                    auto *taken = static_cast<BasicBlock*>(
                        br->get_operand(itL->second.constVal() ? 1 : 2));
                    if (reachable.insert(taken).second) q.push(taken);
                } else {
                    for (unsigned i = 1; i <= 2; i++) {
                        auto *s = static_cast<BasicBlock*>(br->get_operand(i));
                        if (reachable.insert(s).second) q.push(s);
                    }
                }
            } else if (br && br->num_ops_ == 1) {
                auto *s = static_cast<BasicBlock*>(br->get_operand(0));
                if (reachable.insert(s).second) q.push(s);
            }
        }

        for (auto *bb : reachable) {
            for (auto *inst : bb->instr_list_) {
                if (inst->is_phi()) {
                    auto *phi = static_cast<PhiInst*>(inst);
                    LatticeValue val = LV_UNDEF;
                    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                        if (!reachable.count(static_cast<BasicBlock*>(phi->get_operand(i+1))))
                            continue;
                        auto itL = lattice.find(phi->get_operand(i));
                        if (itL != lattice.end()) val = meet(val, itL->second);
                    }
                    auto &cur = lattice[phi];
                    if (cur != val) { cur = val; changed = true; }
                    continue;
                }
                if (inst->isTerminator()) continue;

                auto getLat = [&](Value *v) {
                    if (auto *ci = dynamic_cast<ConstantInt*>(v))
                        return LatticeValue::constant(ci->value_);
                    auto it = lattice.find(v);
                    return it != lattice.end() ? it->second : LV_UNDEF;
                };

                LatticeValue result = LV_OVERDEF;

                if (inst->is_binary()) {
                    auto *bin = static_cast<BinaryInst*>(inst);
                    auto l1 = getLat(bin->get_operand(0));
                    auto l2 = getLat(bin->get_operand(1));
                    if (l1.isOverdef() || l2.isOverdef()) goto done;
                    // Special cases: mul/and by 0, or by -1
                    if (inst->op_id_ == Instruction::Mul || inst->op_id_ == Instruction::And) {
                        if ((l1.isConstant() && l1.constVal() == 0) ||
                            (l2.isConstant() && l2.constVal() == 0))
                            { result = LatticeValue::constant(0); goto done; }
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
                        case ICmpInst::ICMP_EQ:  r=(a==b); break;
                        case ICmpInst::ICMP_NE:  r=(a!=b); break;
                        case ICmpInst::ICMP_SGT: r=(a>b);  break;
                        case ICmpInst::ICMP_SGE: r=(a>=b); break;
                        case ICmpInst::ICMP_SLT: r=(a<b);  break;
                        case ICmpInst::ICMP_SLE: r=(a<=b); break;
                        default: break;
                        }
                        result = LatticeValue::constant(r ? 1 : 0);
                    }
                }
                // calls/loads/stores/alloca → stay OVERDEF (init above)

                done:
                auto &cur = lattice[inst];
                if (cur != result) { cur = result; changed = true; }
            }
        }
    }

    // ── Apply: replace constant instructions (alloc ConstInt here only) ─
    bool changed2 = false;
    for (auto &[val, lat] : lattice) {
        if (!lat.isConstant()) continue;
        auto *inst = dynamic_cast<Instruction*>(val);
        if (!inst || inst->is_phi() || inst->isTerminator()) continue;
        if (dynamic_cast<ConstantInt*>(val)) continue;
        inst->replace_all_use_with(new ConstantInt(inst->type_, lat.constVal()));
        inst->parent_->delete_instr(inst);
        changed2 = true;
    }
    return changed2;
}

void SCCP::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}
