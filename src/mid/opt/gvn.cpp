// GVN：支配树值编号（dominator-tree value numbering，DVNT）。
//
// 沿支配树先序 DFS 维护作用域化的表达式哈希表（签名设施复用 cse_common.hpp），
// 命中即 RAUW 到 leader——leader 在支配树祖先链上，必然支配当前指令。
//
// 与管线最前端的 EarlyCSE 互补：
//   * 跑在循环管线之后，收割 LoopUnroll/IVSR/LoopRotate 暴露的重复 GEP 与标量算术；
//   * phi 处理：平凡 phi 折叠 + 同块同入边 phi 合并；
//   * InlineExpand 之后的纯函数调用 CSE（EarlyCSE 跑在内联之前，管不到）。
//
// load 的内存版本化：load 签名携带单调递增的 mem_gen。进入汇合块
// （≥2 前驱，按 terminator 清点——pre_bbs_ 不可靠）时 mem_gen++，
// 使跨汇合点/跨回边的 load 永不匹配；直线支配链内由 BasicAA 对
// store/非纯 call 做选择性失效。mem_gen 单调不回卷，DFS 访问顺序与
// 执行顺序的差异只会导致漏配（保守），不会误配。

#include "../../include/mid/opt/gvn.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/opt/cse_common.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

const std::unordered_map<Value*, Value*> kEmptyVnMap;

struct GvnState {
    std::unordered_map<ExprSignature, Value*> expr_map;
    std::unordered_map<BasicBlock*, unsigned> pred_count;
    const BasicAliasAnalysis *BAA = nullptr;
    uint64_t mem_gen = 0;
    bool changed = false;
};

// terminator 是 CFG 的事实来源，不信任 pre_bbs_/succ_bbs_。
std::unordered_map<BasicBlock*, unsigned> countPredsFromTerminators(Function *func) {
    std::unordered_map<BasicBlock*, unsigned> cnt;
    for (auto *bb : func->basic_blocks_) {
        Instruction *term = bb->get_terminator();
        if (!term || !term->is_br())
            continue;
        if (term->num_ops_ == 1) {
            cnt[static_cast<BasicBlock*>(term->get_operand(0))]++;
        } else {
            cnt[static_cast<BasicBlock*>(term->get_operand(1))]++;
            cnt[static_cast<BasicBlock*>(term->get_operand(2))]++;
        }
    }
    return cnt;
}

ExprSignature makeLoadSig(Instruction *load, uint64_t gen) {
    ExprSignature sig;
    sig.op_id = Instruction::Load;
    sig.ty = load->type_;
    sig.extra_op = 0;
    // 不变内存（ImmutableLoad）的读与内存状态无关，固定 gen=0，
    // 使同一地址的读跨汇合/回边也能 CSE。
    uint64_t g = load->hasSemFlag(SemFlag::ImmutableLoad) ? 0 : gen;
    sig.ops = {get_canonical_constant(load->get_operand(0)),
               reinterpret_cast<Value*>(static_cast<uintptr_t>(g))};
    return sig;
}

// phi 签名：(入边块, 规范化入边值) 按块指针排序后展开。
// 等价仅在同一块内成立，调用方使用块局部表。
ExprSignature makePhiSig(Instruction *phi) {
    std::vector<std::pair<Value*, Value*>> pairs;
    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2)
        pairs.emplace_back(phi->get_operand(i + 1),
                           get_canonical_constant(phi->get_operand(i)));
    std::sort(pairs.begin(), pairs.end());

    ExprSignature sig;
    sig.op_id = Instruction::PHI;
    sig.ty = phi->type_;
    sig.extra_op = 0;
    for (auto &p : pairs) {
        sig.ops.push_back(p.first);
        sig.ops.push_back(p.second);
    }
    return sig;
}

// store / 可写 call：按 BasicAA 失效可能被改写的 load 缓存。
// 被擦除的祖先作用域条目记入 removed_sigs，子树退出后恢复
// （兄弟路径不经过本块的写操作，恢复是安全的）。
void invalidateLoads(Instruction *modInst, GvnState &st,
                     std::vector<std::pair<ExprSignature, Value*>> &removed_sigs) {
    for (auto si = st.expr_map.begin(); si != st.expr_map.end();) {
        if (si->first.op_id == Instruction::Load &&
            !static_cast<Instruction*>(si->second)->hasSemFlag(SemFlag::ImmutableLoad) &&
            isModSet(st.BAA->getModRefInfo(modInst, si->first.ops[0]))) {
            removed_sigs.push_back({si->first, si->second});
            si = st.expr_map.erase(si);
        } else {
            ++si;
        }
    }
}

bool isPureCall(Function *callee, const BasicAliasAnalysis &BAA) {
    return callee && BAA.isPure(callee);
}

bool isReadOnlyCall(Function *callee, const BasicAliasAnalysis &BAA) {
    if (!callee || BAA.isPure(callee))
        return false;
    if (callee->hasSemFlag(SemFlag::FnReadOnly))
        return true;
    if (callee->is_declaration())
        return false;

    ModRefInfo mr = BAA.getFunctionModRef(callee);
    return isRefSet(mr) && !isModSet(mr);
}

void gvnDfs(BasicBlock *bb,
            const std::map<BasicBlock*, std::vector<BasicBlock*>> &dom_children,
            GvnState &st) {
    // 汇合块：多条路径带着不同内存状态进入，旧版本 load 一律失效
    auto pc = st.pred_count.find(bb);
    if (pc != st.pred_count.end() && pc->second >= 2)
        st.mem_gen++;

    std::vector<ExprSignature> added_sigs;
    std::vector<std::pair<ExprSignature, Value*>> removed_sigs;
    std::unordered_map<ExprSignature, Value*> phi_table;
    std::vector<Instruction*> to_delete;

    // LoopRotate 拆分出口边时（".from." 块）把 header 指令克隆到出口路径，
    // 是刻意的尾复制；跨块合并会撤销旋转成果、扰动下游块布局（h-1 +22%）。
    const bool keep_duplicates = bb->name_.find(".from.") != std::string::npos;

    auto tryCSE = [&](Instruction *inst, const ExprSignature &sig) {
        auto exist = st.expr_map.find(sig);
        if (exist != st.expr_map.end() && keep_duplicates &&
            static_cast<Instruction*>(exist->second)->parent_ != bb)
            return;
        if (exist != st.expr_map.end()) {
            inst->replace_all_use_with(exist->second);
            to_delete.push_back(inst);
            st.changed = true;
        } else {
            st.expr_map[sig] = inst;
            added_sigs.push_back(sig);
        }
    };

    for (auto it = bb->instr_list_.begin(); it != bb->instr_list_.end();) {
        Instruction *inst = *it;
        ++it;

        if (inst->is_phi()) {
            // 平凡 phi：忽略自引用后所有入边同值（常量按规范化对象比较）
            Value *common = nullptr;
            Value *replacement = nullptr;
            bool trivial = true;
            for (unsigned i = 0; i + 1 < inst->num_ops_; i += 2) {
                Value *v = inst->get_operand(i);
                if (v == inst)
                    continue;
                Value *cv = get_canonical_constant(v);
                if (!common) {
                    common = cv;
                    replacement = v;  // RAUW 用原始值，规范化常量仅作比较
                } else if (common != cv) {
                    trivial = false;
                    break;
                }
            }
            if (trivial && replacement) {
                inst->replace_all_use_with(replacement);
                to_delete.push_back(inst);
                st.changed = true;
                continue;
            }
            ExprSignature sig = makePhiSig(inst);
            auto exist = phi_table.find(sig);
            if (exist != phi_table.end()) {
                inst->replace_all_use_with(exist->second);
                to_delete.push_back(inst);
                st.changed = true;
            } else {
                phi_table[sig] = inst;
            }
            continue;
        }

        if (inst->is_store()) {
            invalidateLoads(inst, st, removed_sigs);
            continue;
        }

        if (inst->is_load()) {
            tryCSE(inst, makeLoadSig(inst, st.mem_gen));
            continue;
        }

        if (inst->is_call()) {
            auto *call = static_cast<CallInst*>(inst);
            auto *callee =
                dynamic_cast<Function*>(call->get_operand(call->num_ops_ - 1));
            if (isPureCall(callee, *st.BAA)) {
                tryCSE(inst, compute_signature(inst, kEmptyVnMap));
            } else if (!isReadOnlyCall(callee, *st.BAA)) {
                invalidateLoads(inst, st, removed_sigs);
            }
            continue;
        }

        // icmp/fcmp 不做 CSE：后端 sinkCmpToBranch 只能下沉单用途比较，
        // 跨块合并会迫使 cset 物化布尔值（cmp+b.lt → movz+cmp+cset+mov+cbnz）
        if (inst->is_cmp() || inst->is_fcmp())
            continue;

        if (is_safe_to_eliminate(inst))
            tryCSE(inst, compute_signature(inst, kEmptyVnMap));
    }

    for (auto *inst : to_delete)
        bb->delete_instr(inst);

    auto itc = dom_children.find(bb);
    if (itc != dom_children.end()) {
        // 单前驱子块先访问：它们紧随本块执行，赶在汇合块子树把
        // mem_gen 推高之前完成匹配（顺序只影响命中率，不影响正确性）
        std::vector<BasicBlock*> kids = itc->second;
        std::stable_partition(kids.begin(), kids.end(), [&](BasicBlock *k) {
            auto kc = st.pred_count.find(k);
            return kc == st.pred_count.end() || kc->second < 2;
        });
        for (auto *child : kids)
            gvnDfs(child, dom_children, st);
    }

    for (auto &sig : added_sigs)
        st.expr_map.erase(sig);
    for (auto &[sig, val] : removed_sigs) {
        auto *inst_val = dynamic_cast<Instruction*>(val);
        if (!inst_val || inst_val->parent_ != bb)
            st.expr_map[sig] = val;
    }
}

void gvnOnFunction(Function *func, const BasicAliasAnalysis &BAA, bool &changed) {
    if (func->basic_blocks_.empty())
        return;

    GvnState st;
    st.BAA = &BAA;
    st.pred_count = countPredsFromTerminators(func);

    auto &dom_children = func->getDominatorInfo().domChildren;
    gvnDfs(func->basic_blocks_.front(), dom_children, st);
    changed |= st.changed;
}

} // namespace

void GVN::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses GVN::execute(Module *module, AnalysisManager &AM) {
    if (std::getenv("GVN_DISABLE"))
        return PreservedAnalyses::all();
    BasicAliasAnalysis &BAA = AM.getBasicAA(module);
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            gvnOnFunction(func, BAA, changed);
    }
    for (auto &p : canonical_constants)
        delete p.second;
    canonical_constants.clear();
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
