#include "../../include/mid/opt/constSubexprEliminate.hpp"
#include <algorithm>
#include <functional>
#include <cstring>

struct PairHash {
    template<typename T1, typename T2>
    size_t operator()(const std::pair<T1, T2>& p) const {
        auto h1 = std::hash<T1>()(p.first);
        auto h2 = std::hash<T2>()(p.second);
        return h1 ^ (h2 << 1);
    }
};

// 修改常量规范化映射的声明
static std::unordered_map<std::pair<Type*, unsigned long long>, Constant*, PairHash> canonical_constants;

static Value* get_canonical_constant(Value *v) {
    if (auto *ci = dynamic_cast<ConstantInt*>(v)) {
        unsigned long long key = static_cast<unsigned long long>(ci->value_);
        auto &entry = canonical_constants[{ci->type_, key}];
        if (!entry) entry = new ConstantInt(ci->type_, ci->value_);
        return entry;
    } else if (auto *cf = dynamic_cast<ConstantFloat*>(v)) {
        // 将 float 按位转 unsigned long long 作为键值
        unsigned long long key;
        memcpy(&key, &cf->value_, sizeof(key));
        auto &entry = canonical_constants[{cf->type_, key}];
        if (!entry) entry = new ConstantFloat(cf->type_, cf->value_);
        return entry;
    } else if (dynamic_cast<ConstantZero*>(v)) {
        auto &entry = canonical_constants[{v->type_, 0}];
        if (!entry) entry = new ConstantZero(v->type_);
        return entry;
    }
    return v;
}

// ---------- 辅助：表达式签名 ----------
struct ExprSignature {
    Instruction::OpID op_id;
    Type* ty;
    unsigned extra_op;          // 用于 icmp/fcmp 的比较类型
    std::vector<Value*> ops;   // 已经过值编号替代的操作数

    bool operator==(const ExprSignature &other) const {
        return op_id == other.op_id && extra_op == other.extra_op &&
               ty == other.ty && ops == other.ops;
    }
};

namespace std {
    template<> struct hash<ExprSignature> {
        size_t operator()(const ExprSignature &s) const {
            size_t h = hash<unsigned>()(s.op_id);
            h ^= hash<unsigned>()(s.extra_op) + 0x9e3779b9 + (h<<6) + (h>>2);
            h ^= std::hash<void*>()(s.ty) + 0x9e3779b9 + (h<<6) + (h>>2); // 类型hash
            for (auto *v : s.ops) {
                size_t ph = hash<void*>()(v);
                h ^= ph + 0x9e3779b9 + (h<<6) + (h>>2);
            }
            return h;
        }
    };
}

// 判断指令是否可安全消除（无副作用、非终结）
static bool is_safe_to_eliminate(Instruction *inst) {
    if (inst->is_void()) return false;          // store / br / ret / void call
    if (inst->is_call()) return false;
    if (inst->is_store()) return false;
    if (inst->is_alloca()) return false;
    if (inst->is_phi()) return false;
    return true;  // Load/Binary/Unary/ICmp/FCmp/GetElementPtr/ZExt/FPtoSI/SItoFP/BitCast
}

// 判断二元运算是否可交换
static bool is_commutative(Instruction::OpID op) {
    switch (op) {
        case Instruction::Add:
        case Instruction::Mul:
        case Instruction::FAdd:
        case Instruction::FMul:
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Xor:
            return true;
        default: return false;
    }
}

// 判断 icmp/fcmp 的操作是否可交换
static bool icmp_commutative(ICmpInst::ICmpOp op) {
    return op == ICmpInst::ICMP_EQ || op == ICmpInst::ICMP_NE;
}
static bool fcmp_commutative(FCmpInst::FCmpOp op) {
    return op == FCmpInst::FCMP_UEQ || op == FCmpInst::FCMP_UNE ||
           op == FCmpInst::FCMP_OEQ || op == FCmpInst::FCMP_ONE;
}

// 生成表达式的哈希签名（操作数使用全局值编号）
static ExprSignature compute_signature(Instruction *inst,
                                       const std::unordered_map<Value*, Value*> &vn_map) {
    ExprSignature sig;
    sig.op_id = inst->op_id_;
    sig.ty = inst->type_;
    sig.extra_op = 0;

    // 收集操作数（替换为值编号后的代表值）
    std::vector<Value*> ops;
    ops.reserve(inst->num_ops_);
    for (unsigned i = 0; i < inst->num_ops_; i++) {
        Value *op = inst->get_operand(i);
        auto it = vn_map.find(op);
        Value *rep = (it != vn_map.end()) ? it->second : op;
        rep = get_canonical_constant(rep);
        ops.push_back(rep);
    }

    // 设置额外比较类型并规范化可交换操作数
    if (auto *icmp = dynamic_cast<ICmpInst*>(inst)) {
        sig.extra_op = static_cast<unsigned>(icmp->icmp_op_);
        if (icmp_commutative(icmp->icmp_op_)) {
            if (ops[0] > ops[1]) std::swap(ops[0], ops[1]);
        }
    } else if (auto *fcmp = dynamic_cast<FCmpInst*>(inst)) {
        sig.extra_op = static_cast<unsigned>(fcmp->fcmp_op_);
        if (fcmp_commutative(fcmp->fcmp_op_)) {
            if (ops[0] > ops[1]) std::swap(ops[0], ops[1]);
        }
    } else if (is_commutative(inst->op_id_)) {
        if (ops.size() == 2 && ops[0] > ops[1])
            std::swap(ops[0], ops[1]);
    }

    sig.ops = std::move(ops);
    return sig;
}

// ---------- 支配树计算 ----------
static std::map<BasicBlock*, BasicBlock*> compute_dominators(Function *func) {
    std::vector<BasicBlock*> all_bb(func->basic_blocks_.begin(), func->basic_blocks_.end());
    if (all_bb.empty()) return {};

    BasicBlock *entry = all_bb.front();
    std::set<BasicBlock*> all_set(all_bb.begin(), all_bb.end());

    std::map<BasicBlock*, std::set<BasicBlock*>> dom;
    dom[entry] = {entry};
    for (auto *bb : all_bb) {
        if (bb != entry) dom[bb] = all_set;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto *bb : all_bb) {
            if (bb == entry) continue;
            std::set<BasicBlock*> new_dom = all_set;
            for (auto *pred : bb->pre_bbs_) {
                std::set<BasicBlock*> temp;
                std::set_intersection(new_dom.begin(), new_dom.end(),
                                      dom[pred].begin(), dom[pred].end(),
                                      std::inserter(temp, temp.begin()));
                new_dom = std::move(temp);
            }
            new_dom.insert(bb);
            if (new_dom != dom[bb]) {
                dom[bb] = std::move(new_dom);
                changed = true;
            }
        }
    }

    // 计算 idom：选择 dom[B]\{B} 中 dom 集大小最大的节点
    std::map<BasicBlock*, BasicBlock*> idom;
    for (auto *bb : all_bb) {
        if (bb == entry) continue;
        const auto &ds = dom[bb];
        BasicBlock *best = nullptr;
        size_t best_sz = 0;
        for (auto *d : ds) {
            if (d == bb) continue;
            size_t sz = dom[d].size();
            if (sz > best_sz) {
                best_sz = sz;
                best = d;
            }
        }
        if (best) idom[bb] = best;
    }
    return idom;
}

// 构建支配树子节点映射
static std::map<BasicBlock*, std::vector<BasicBlock*>>
build_dom_children(const std::map<BasicBlock*, BasicBlock*> &idom) {
    std::map<BasicBlock*, std::vector<BasicBlock*>> children;
    for (auto &kv : idom) {
        children[kv.second].push_back(kv.first);
    }
    return children;
}

// ---------- 全局 CSE 主过程 ----------
static void global_cse_on_function(Function *func) {
    if (func->basic_blocks_.empty()) return;

    auto idom = compute_dominators(func);
    auto dom_children = build_dom_children(idom);
    BasicBlock *entry = func->basic_blocks_.front();

    // 清空上次可能残留的规范化常量（注意：最后需释放）
    // for (auto &p : canonical_constants) delete p.second;
    // canonical_constants.clear();

    std::unordered_map<Value*, Value*> vn_map;
    std::unordered_map<ExprSignature, Value*> scope_map;

    std::function<void(BasicBlock*)> dfs = [&](BasicBlock *bb) {
        // 进入新基本块时，清除父块缓存的 load 条目。
        // 跨块 load CSE 需要正确的别名/存储分析，仅靠支配树作用域不够：
        // 兄弟块中的 store 可能尚未处理，却已错误消除另一兄弟块中的 load。
        if (bb != entry) {
            for (auto si = scope_map.begin(); si != scope_map.end(); ) {
                if (si->first.op_id == Instruction::Load)
                    si = scope_map.erase(si);
                else
                    ++si;
            }
        }

        std::vector<Instruction*> to_delete;
        std::vector<ExprSignature> added_sigs;   // 本块添加的签名，用于退出时擦除

        for (auto it = bb->instr_list_.begin(); it != bb->instr_list_.end(); ) {
            Instruction *inst = *it;
            ++it;

            if (!is_safe_to_eliminate(inst)) {
                vn_map[inst] = inst;
                if (inst->is_store() || inst->is_call()) {
                    for (auto si = scope_map.begin(); si != scope_map.end(); ) {
                        if (si->first.op_id == Instruction::Load)
                            si = scope_map.erase(si);
                        else
                            ++si;
                    }
                }
                continue;
            }

            ExprSignature sig = compute_signature(inst, vn_map);
            auto exist = scope_map.find(sig);
            if (exist != scope_map.end()) {
                Value *repl = exist->second;
                vn_map[inst] = repl;
                inst->replace_all_use_with(repl);
                to_delete.push_back(inst);
            } else {
                vn_map[inst] = inst;
                // 立即插入 scope_map，使同块后续指令可见
                scope_map[sig] = inst;
                added_sigs.push_back(sig);
            }
        }

        // 删除被消除的指令
        for (auto *inst : to_delete) {
            bb->remove_instr(inst);
        }

        // 递归处理支配树子节点
        auto itc = dom_children.find(bb);
        if (itc != dom_children.end()) {
            for (auto *child : itc->second) {
                dfs(child);
            }
        }

        // 离开基本块：撤销本块添加的表达式
        for (auto &sig : added_sigs) {
            scope_map.erase(sig);
        }
    };

    dfs(entry);

    // 清理规范化常量
    // for (auto &p : canonical_constants) delete p.second;
    // canonical_constants.clear();
}

// ---------- CSE::execute ----------
void CSE::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration()) {
            global_cse_on_function(func);
        }
    }
    for (auto &p : canonical_constants) delete p.second;
    canonical_constants.clear();
}