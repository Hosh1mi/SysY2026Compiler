#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/constant.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <sstream>

namespace {

bool isDedicatedPreheader(BasicBlock *bb, BasicBlock *header) {
    if (!bb || !header) return false;
    if (bb->succ_bbs_.size() != 1 || bb->succ_bbs_[0] != header)
        return false;

    auto *term = bb->get_terminator();
    return term && term->is_br() && term->num_ops_ == 1 &&
           term->get_operand(0) == header;
}

bool isAddOneOf(Value *value, PhiInst *phi) {
    auto *add = dynamic_cast<BinaryInst *>(value);
    if (!add || !add->is_add()) return false;
    auto *op0 = add->get_operand(0);
    auto *op1 = add->get_operand(1);
    auto *c0  = dynamic_cast<ConstantInt *>(op0);
    auto *c1  = dynamic_cast<ConstantInt *>(op1);
    return (op0 == phi && c1 && c1->value_ == 1) ||
           (op1 == phi && c0 && c0->value_ == 1);
}

bool isStepFromLatch(Value *value, PhiInst *phi, BasicBlock *latch) {
    if (isAddOneOf(value, phi)) return true;

    auto *merge = dynamic_cast<PhiInst *>(value);
    if (!merge || merge->parent_ != latch || merge->num_ops_ == 0)
        return false;

    for (unsigned i = 0; i + 1 < merge->num_ops_; i += 2)
        if (!isAddOneOf(merge->get_operand(i), phi))
            return false;
    return true;
}

bool headerGuardTripCount(Loop *loop, PhiInst *iv, Value *&bound) {
    auto *term = loop->header->get_terminator();
    if (!term || !term->is_br() || term->num_ops_ != 3) return false;
    auto *cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cmp || cmp->icmp_op_ != ICmpInst::ICMP_SLT) return false;
    if (cmp->get_operand(0) != iv) return false;
    bound = cmp->get_operand(1);
    return true;
}

bool latchGuardTripCount(Loop *loop, Value *stepValue, Value *&bound) {
    BasicBlock *latch = loop->singleLatch();
    if (!latch || !stepValue) return false;
    auto *term = latch->get_terminator();
    if (!term || !term->is_br() || term->num_ops_ != 3) return false;
    auto *cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cmp || cmp->icmp_op_ != ICmpInst::ICMP_SLT) return false;
    if (cmp->get_operand(0) != stepValue) return false;
    bound = cmp->get_operand(1);
    return true;
}

} // namespace

// ── Loop ───────────────────────────────────────────────────────────────────

std::string Loop::print() const {
    std::ostringstream oss;
    oss << "Loop@" << (header ? header->name_ : "?")
        << " depth=" << depth
        << " blocks=" << blocks.size()
        << " preheader=" << (preheader ? preheader->name_ : "<none>")
        << " latches=" << latches.size()
        << " exits=" << exits.size();
    oss << " iv=" << (canonicalIV
                          ? (canonicalIV->name_.empty() ? "<anon>" : canonicalIV->name_)
                          : "<none>")
        << " bound=" << (tripCount
                             ? (tripCount->name_.empty() ? "<anon>" : tripCount->name_)
                             : "<none>");
    return oss.str();
}

// ── LoopInfo: top-level ────────────────────────────────────────────────────

void LoopInfo::reset() {
    loops_.clear();
    top_.clear();
    bb2innermost_.clear();
    idom_.clear();
    rpoIdx_.clear();
}

void LoopInfo::analyze(Function *func) {
    reset();
    if (!func || func->basic_blocks_.empty()) return;

    auto rpo = computeRPO(func);
    computeDominators(rpo);
    findLoops(func);
    for (auto &loop : loops_) enrichLoop(loop.get());
    buildNestTree();
    for (auto &loop : loops_) analyzeIV(loop.get());
}

// ── RPO ────────────────────────────────────────────────────────────────────

std::vector<BasicBlock *> LoopInfo::computeRPO(Function *func) {
    std::vector<BasicBlock *> post;
    std::set<BasicBlock *>    visited;

    std::function<void(BasicBlock *)> dfs = [&](BasicBlock *bb) {
        visited.insert(bb);
        for (auto succ : bb->succ_bbs_)
            if (!visited.count(succ)) dfs(succ);
        post.push_back(bb);
    };
    dfs(func->basic_blocks_[0]);
    std::reverse(post.begin(), post.end());
    return post;
}

// ── Dominators (iterative Cooper-Harvey-Kennedy) ───────────────────────────

void LoopInfo::computeDominators(const std::vector<BasicBlock *> &rpo) {
    idom_.clear();
    rpoIdx_.clear();
    if (rpo.empty()) return;

    for (int i = 0; i < (int)rpo.size(); i++) rpoIdx_[rpo[i]] = i;
    BasicBlock *entry = rpo[0];
    idom_[entry]      = entry;

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto bb : rpo) {
            if (bb == entry) continue;
            BasicBlock *new_idom = nullptr;
            for (auto pred : bb->pre_bbs_) {
                if (!idom_.count(pred)) continue;
                new_idom = new_idom ? intersect(pred, new_idom) : pred;
            }
            if (new_idom && idom_[bb] != new_idom) {
                idom_[bb] = new_idom;
                changed   = true;
            }
        }
    }
}

BasicBlock *LoopInfo::intersect(BasicBlock *a, BasicBlock *b) {
    while (a != b) {
        while (rpoIdx_[a] > rpoIdx_[b]) a = idom_[a];
        while (rpoIdx_[b] > rpoIdx_[a]) b = idom_[b];
    }
    return a;
}

bool LoopInfo::dominates(BasicBlock *a, BasicBlock *b) const {
    auto it = idom_.find(b);
    if (it == idom_.end()) return false;
    while (b != it->second) {
        if (b == a) return true;
        b  = it->second;
        it = idom_.find(b);
        if (it == idom_.end()) return false;
    }
    return b == a;
}

BasicBlock *LoopInfo::getIDom(BasicBlock *bb) const {
    auto it = idom_.find(bb);
    return it == idom_.end() ? nullptr : it->second;
}

// ── Natural loop detection ─────────────────────────────────────────────────
// 回边 bb→succ：succ 支配 bb。把同一 header 的多条回边合并成一个 Loop。

void LoopInfo::findLoops(Function *func) {
    std::map<BasicBlock *, Loop *> headerToLoop;

    for (auto bb : func->basic_blocks_) {
        for (auto succ : bb->succ_bbs_) {
            if (!idom_.count(succ)) continue;            // 不可达
            if (!dominates(succ, bb)) continue;          // 不是回边

            Loop *loop;
            auto  it = headerToLoop.find(succ);
            if (it == headerToLoop.end()) {
                loops_.push_back(std::make_unique<Loop>());
                loop          = loops_.back().get();
                loop->header  = succ;
                headerToLoop[succ] = loop;
                loop->blocks.insert(succ);
            } else {
                loop = it->second;
            }
            loop->latches.push_back(bb);

            // 从 latch 反向 BFS 收集循环体（任何能到 latch 但不经过 header 的块都属于 loop）
            std::queue<BasicBlock *> wl;
            wl.push(bb);
            while (!wl.empty()) {
                auto cur = wl.front();
                wl.pop();
                if (!loop->blocks.insert(cur).second) continue;
                for (auto pred : cur->pre_bbs_)
                    if (!loop->blocks.count(pred)) wl.push(pred);
            }
        }
    }
}

// ── 填充 preheader / exiting / exits ───────────────────────────────────────

void LoopInfo::enrichLoop(Loop *loop) {
    // preheader: header 的唯一循环外前驱，且该前驱必须是 dedicated 的
    // 单后继无条件跳转块。只有这种块才适合作为 LICM/IVSR/vectorize 等
    // pass 的循环入口插入点或重定向点。
    BasicBlock *cand    = nullptr;
    int         ext_cnt = 0;
    for (auto pred : loop->header->pre_bbs_) {
        if (!loop->blocks.count(pred)) {
            cand = pred;
            ext_cnt++;
        }
    }
    loop->preheader =
        (ext_cnt == 1 && isDedicatedPreheader(cand, loop->header))
            ? cand
            : nullptr;

    // blocks 的 RPO 确定序视图（不可达块兜底排在最后，按指针仅作稳定性兜底）
    loop->blocksOrdered.assign(loop->blocks.begin(), loop->blocks.end());
    std::sort(loop->blocksOrdered.begin(), loop->blocksOrdered.end(),
              [this](BasicBlock *a, BasicBlock *b) {
                  auto ia = rpoIdx_.find(a), ib = rpoIdx_.find(b);
                  int ra = ia == rpoIdx_.end() ? INT32_MAX : ia->second;
                  int rb = ib == rpoIdx_.end() ? INT32_MAX : ib->second;
                  if (ra != rb) return ra < rb;
                  return a < b;
              });

    // exiting: 循环内、后继有循环外的块；exits: 那些循环外的后继（去重）
    // 均按 RPO 序产出，保证消费方的处理/产出顺序跨进程稳定。
    std::set<BasicBlock *> exit_set;
    for (auto bb : loop->blocksOrdered) {
        bool is_exiting = false;
        for (auto succ : bb->succ_bbs_) {
            if (!loop->blocks.count(succ)) {
                is_exiting = true;
                exit_set.insert(succ);
            }
        }
        if (is_exiting) loop->exiting.push_back(bb);
    }
    loop->exits.assign(exit_set.begin(), exit_set.end());
    std::sort(loop->exits.begin(), loop->exits.end(),
              [this](BasicBlock *a, BasicBlock *b) {
                  auto ia = rpoIdx_.find(a), ib = rpoIdx_.find(b);
                  int ra = ia == rpoIdx_.end() ? INT32_MAX : ia->second;
                  int rb = ib == rpoIdx_.end() ? INT32_MAX : ib->second;
                  if (ra != rb) return ra < rb;
                  return a < b;
              });
}

// ── 嵌套树 ─────────────────────────────────────────────────────────────────
// L1 是 L2 的父：L1.blocks ⊇ L2.blocks，且不存在 L3 严格介于其中。
// 简单做法：按 blocks 数升序，每个 loop 向上找最小的真包含它的 loop。

void LoopInfo::buildNestTree() {
    std::vector<Loop *> sorted;
    sorted.reserve(loops_.size());
    for (auto &l : loops_) sorted.push_back(l.get());
    std::sort(sorted.begin(), sorted.end(),
              [](Loop *a, Loop *b) { return a->blocks.size() < b->blocks.size(); });

    for (Loop *child : sorted) {
        // 找最小的、严格真包含 child 的 loop
        Loop *best = nullptr;
        for (Loop *cand : sorted) {
            if (cand == child) continue;
            if (cand->blocks.size() <= child->blocks.size()) continue;
            if (!cand->isInLoop(child->header)) continue;
            if (!best || cand->blocks.size() < best->blocks.size()) best = cand;
        }
        child->parent = best;
        if (best)
            best->children.push_back(child);
        else
            top_.push_back(child);
    }

    // depth：从顶层向下 BFS
    std::queue<Loop *> wl;
    for (Loop *l : top_) {
        l->depth = 0;
        wl.push(l);
    }
    while (!wl.empty()) {
        Loop *cur = wl.front();
        wl.pop();
        for (Loop *ch : cur->children) {
            ch->depth = cur->depth + 1;
            wl.push(ch);
        }
    }

    // bb2innermost: 每个 BB 找深度最大的 loop
    for (auto &l : loops_) {
        for (auto bb : l->blocks) {
            auto it = bb2innermost_.find(bb);
            if (it == bb2innermost_.end() || it->second->depth < l->depth) {
                bb2innermost_[bb] = l.get();
            }
        }
    }
}

Loop *LoopInfo::getLoopFor(BasicBlock *bb) const {
    auto it = bb2innermost_.find(bb);
    return it == bb2innermost_.end() ? nullptr : it->second;
}

// ── 规范 IV 识别 ───────────────────────────────────────────────────────────
// 形如 for(i=0; i<N; i+=1)：
//   header 中存在 phi i32 [ 0, preheader ], [ i_next, latch ]
//   latch 处有 i_next = add i, 1
//   header terminator 是 br icmp_slt(i, N), body, exit

void LoopInfo::analyzeIV(Loop *loop) {
    if (!loop->preheader) return;

    // 1. 找 header 中所有规范 phi（init=0 from preheader, step=+1 from some latch）
    PhiInst *iv = nullptr;
    Value *ivLatchValue = nullptr;
    for (auto *inst : loop->header->instr_list_) {
        if (!inst->is_phi()) break;
        if (inst->type_->tid_ != Type::IntegerTyID) continue;

        auto *phi = static_cast<PhiInst *>(inst);
        // 期望 2 对 (val, BB)：一对来自 preheader 一对来自 latch
        if (phi->num_ops_ != 4) continue;

        Value *pre_val = nullptr, *latch_val = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *src = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (src == loop->preheader)
                pre_val = phi->get_operand(i);
            else if (std::find(loop->latches.begin(), loop->latches.end(), src) != loop->latches.end())
                latch_val = phi->get_operand(i);
        }
        if (!pre_val || !latch_val) continue;

        auto *ci_init = dynamic_cast<ConstantInt *>(pre_val);
        if (!ci_init || ci_init->value_ != 0) continue;

        BasicBlock *singleLatch = loop->singleLatch();
        if (!singleLatch || !isStepFromLatch(latch_val, phi, singleLatch))
            continue;

        iv = phi;
        ivLatchValue = latch_val;
        break;
    }
    if (!iv) return;

    // 2. 支持 while-form header guard，也支持 LoopRotate 后的 latch guard。
    Value *bound = nullptr;
    if (!headerGuardTripCount(loop, iv, bound) &&
        !latchGuardTripCount(loop, ivLatchValue, bound))
        return;

    loop->canonicalIV = iv;
    loop->tripCount   = bound;
    loop->predicate   = ICmpInst::ICMP_SLT;
}

// ── 调试输出 ───────────────────────────────────────────────────────────────

std::string LoopInfo::print() const {
    std::ostringstream oss;
    oss << "LoopInfo: " << loops_.size() << " loops, "
        << top_.size() << " top-level\n";

    std::function<void(Loop *, int)> dump = [&](Loop *l, int indent) {
        for (int i = 0; i < indent; i++) oss << "  ";
        oss << l->print() << "\n";
        for (Loop *ch : l->children) dump(ch, indent + 1);
    };
    for (Loop *l : top_) dump(l, 0);
    return oss.str();
}
