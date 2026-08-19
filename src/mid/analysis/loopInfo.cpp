// LoopInfo 利用“header 支配 latch”的回边识别自然循环，合并同头回边并按块集合包含关系
// 建立嵌套树；随后补充 preheader、latch、exit 和规范归纳变量。几乎所有循环优化都以该
// 结构为入口，结构不规范只会让相应高级字段为空，不会影响基本循环集合。
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <sstream>

namespace {

// isDedicatedPreheader：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
bool isDedicatedPreheader(BasicBlock *bb, BasicBlock *header) {
    if (!bb || !header) return false;
    if (bb->succ_bbs_.size() != 1 || bb->succ_bbs_[0] != header)
        return false;

    auto *term = bb->get_terminator();
    return term && term->is_br() && term->num_ops() == 1 &&
           term->get_operand(0) == header;
}

// isLoopInvariant：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
bool isLoopInvariant(Value *value, const Loop *loop) {
    if (!value || !loop) return false;
    if (dynamic_cast<Constant *>(value) ||
        dynamic_cast<Argument *>(value) ||
        dynamic_cast<GlobalVariable *>(value))
        return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    return inst && !loop->blocks.count(inst->parent_);
}

// swapPredicate：返回交换操作数或翻转分支后语义等价的比较谓词。
ICmpInst::ICmpOp swapPredicate(ICmpInst::ICmpOp pred) {
    switch (pred) {
    case ICmpInst::ICMP_UGT: return ICmpInst::ICMP_ULT;
    case ICmpInst::ICMP_UGE: return ICmpInst::ICMP_ULE;
    case ICmpInst::ICMP_ULT: return ICmpInst::ICMP_UGT;
    case ICmpInst::ICMP_ULE: return ICmpInst::ICMP_UGE;
    case ICmpInst::ICMP_SGT: return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SGE: return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_SLT: return ICmpInst::ICMP_SGT;
    case ICmpInst::ICMP_SLE: return ICmpInst::ICMP_SGE;
    default: return pred;
    }
}

// invertPredicate：返回交换操作数或翻转分支后语义等价的比较谓词。
ICmpInst::ICmpOp invertPredicate(ICmpInst::ICmpOp pred) {
    switch (pred) {
    case ICmpInst::ICMP_EQ: return ICmpInst::ICMP_NE;
    case ICmpInst::ICMP_NE: return ICmpInst::ICMP_EQ;
    case ICmpInst::ICMP_UGT: return ICmpInst::ICMP_ULE;
    case ICmpInst::ICMP_UGE: return ICmpInst::ICMP_ULT;
    case ICmpInst::ICMP_ULT: return ICmpInst::ICMP_UGE;
    case ICmpInst::ICMP_ULE: return ICmpInst::ICMP_UGT;
    case ICmpInst::ICMP_SGT: return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_SGE: return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SLT: return ICmpInst::ICMP_SGE;
    case ICmpInst::ICMP_SLE: return ICmpInst::ICMP_SGT;
    }
    return pred;
}

// isOrderedPredicate：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
bool isOrderedPredicate(ICmpInst::ICmpOp pred) {
    switch (pred) {
    case ICmpInst::ICMP_UGT:
    case ICmpInst::ICMP_UGE:
    case ICmpInst::ICMP_ULT:
    case ICmpInst::ICMP_ULE:
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_SGE:
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_SLE:
        return true;
    default:
        return false;
    }
}

// matchInductionUpdate：逐层匹配允许的 IR 形状并提取组成部分，结构或类型不符时返回失败。
bool matchInductionUpdate(Value *value, PhiInst *phi, const Loop *loop,
                          BinaryInst *&update, Value *&step,
                          bool &stepNegated,
                          std::optional<long long> &constantStep) {
    update = dynamic_cast<BinaryInst *>(value);
    BasicBlock *latch = loop ? loop->singleLatch() : nullptr;
    if (!update || !latch || update->parent_ != latch)
        return false;

    Value *lhs = update->get_operand(0);
    Value *rhs = update->get_operand(1);
    if (update->is_add()) {
        if (lhs == phi)
            step = rhs;
        else if (rhs == phi)
            step = lhs;
        else
            return false;
    } else if (update->is_sub() && lhs == phi) {
        step = rhs;
        stepNegated = true;
    } else {
        return false;
    }

    if (!isLoopInvariant(step, loop))
        return false;

    if (auto *constant = dynamic_cast<ConstantInt *>(step)) {
        long long value = constant->value_;
        if (stepNegated) value = -value;
        if (value == 0) return false;
        constantStep = value;
    }
    return true;
}

// normalizeCompare：封装该局部计算，为上层分析或 IR 构造返回所需结果。
bool normalizeCompare(ICmpInst *compare, Value *inductionValue,
                      Value *&bound, ICmpInst::ICmpOp &predicate) {
    if (!compare || !inductionValue) return false;
    if (compare->get_operand(0) == inductionValue) {
        bound = compare->get_operand(1);
        predicate = compare->icmp_op_;
    } else if (compare->get_operand(1) == inductionValue) {
        bound = compare->get_operand(0);
        predicate = swapPredicate(compare->icmp_op_);
    } else {
        return false;
    }
    return isOrderedPredicate(predicate);
}

bool continuationSense(BranchInst *branch, const Loop *loop,
                       bool &continuesWhenTrue);

// matchEqualityGuard：逐层匹配允许的 IR 形状并提取组成部分，结构或类型不符时返回失败。
bool matchEqualityGuard(BasicBlock *guardBlock, Value *inductionValue,
                        const Loop *loop, ICmpInst *&compare, Value *&bound,
                        ICmpInst::ICmpOp &predicate) {
    auto *branch = guardBlock
                       ? dynamic_cast<BranchInst *>(guardBlock->get_terminator())
                       : nullptr;
    if (!branch || branch->num_ops() != 3)
        return false;
    compare = dynamic_cast<ICmpInst *>(branch->get_operand(0));
    if (!compare || compare->parent_ != guardBlock)
        return false;

    if (compare->get_operand(0) == inductionValue) {
        bound = compare->get_operand(1);
    } else if (compare->get_operand(1) == inductionValue) {
        bound = compare->get_operand(0);
    } else {
        return false;
    }
    if (!isLoopInvariant(bound, loop))
        return false;

    predicate = compare->icmp_op_;
    if (predicate != ICmpInst::ICMP_EQ &&
        predicate != ICmpInst::ICMP_NE)
        return false;

    bool continuesWhenTrue = false;
    if (!continuationSense(branch, loop, continuesWhenTrue))
        return false;
    if (!continuesWhenTrue)
        predicate = invertPredicate(predicate);
    return predicate == ICmpInst::ICMP_NE;
}

// continuationSense：封装该局部计算，为上层分析或 IR 构造返回所需结果。
bool continuationSense(BranchInst *branch, const Loop *loop,
                       bool &continuesWhenTrue) {
    if (!branch || branch->num_ops() != 3 || !loop) return false;
    auto *trueBlock = dynamic_cast<BasicBlock *>(branch->get_operand(1));
    auto *falseBlock = dynamic_cast<BasicBlock *>(branch->get_operand(2));
    if (!trueBlock || !falseBlock) return false;
    bool trueInside = loop->blocks.count(trueBlock);
    bool falseInside = loop->blocks.count(falseBlock);
    if (trueInside == falseInside) return false;
    continuesWhenTrue = trueInside;
    return true;
}

// matchGuard：逐层匹配允许的 IR 形状并提取组成部分，结构或类型不符时返回失败。
bool matchGuard(BasicBlock *guardBlock, Value *inductionValue,
                const Loop *loop, ICmpInst *&compare, Value *&bound,
                ICmpInst::ICmpOp &predicate) {
    auto *branch = guardBlock
                       ? dynamic_cast<BranchInst *>(guardBlock->get_terminator())
                       : nullptr;
    if (!branch || branch->num_ops() != 3) return false;
    compare = dynamic_cast<ICmpInst *>(branch->get_operand(0));
    if (!compare || compare->parent_ != guardBlock ||
        !normalizeCompare(compare, inductionValue, bound, predicate) ||
        !isLoopInvariant(bound, loop))
        return false;

    bool continuesWhenTrue = false;
    if (!continuationSense(branch, loop, continuesWhenTrue))
        return false;
    if (!continuesWhenTrue)
        predicate = invertPredicate(predicate);
    return true;
}

// describeControlInduction：封装该局部计算，为上层分析或 IR 构造返回所需结果。
bool describeControlInduction(Loop *loop, PhiInst *phi, Value *start,
                              Value *latchValue,
                              InductionDescriptor &descriptor) {
    BinaryInst *update = nullptr;
    Value *step = nullptr;
    bool stepNegated = false;
    std::optional<long long> constantStep;
    if (!matchInductionUpdate(latchValue, phi, loop, update, step,
                              stepNegated, constantStep))
        return false;

    ICmpInst *compare = nullptr;
    Value *bound = nullptr;
    ICmpInst::ICmpOp predicate = ICmpInst::ICMP_SLT;
    InductionGuardPosition guardPosition = InductionGuardPosition::Header;
    bool comparesUpdate = false;

    if (!matchGuard(loop->header, phi, loop, compare, bound, predicate)) {
        BasicBlock *latch = loop->singleLatch();
        if (!matchGuard(latch, update, loop, compare, bound, predicate))
            return false;
        guardPosition = InductionGuardPosition::Latch;
        comparesUpdate = true;
    }

    descriptor.phi = phi;
    descriptor.start = start;
    descriptor.update = update;
    descriptor.step = step;
    descriptor.stepNegated = stepNegated;
    descriptor.constantStep = constantStep;
    descriptor.compare = compare;
    descriptor.bound = bound;
    descriptor.predicate = predicate;
    descriptor.guardPosition = guardPosition;
    descriptor.comparesUpdate = comparesUpdate;
    return true;
}

// isAddOneOf：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
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

// isStepFromLatch：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
bool isStepFromLatch(Value *value, PhiInst *phi, BasicBlock *latch) {
    if (isAddOneOf(value, phi)) return true;

    auto *merge = dynamic_cast<PhiInst *>(value);
    if (!merge || merge->parent_ != latch || merge->num_ops() == 0)
        return false;

    for (unsigned i = 0; i + 1 < merge->num_ops(); i += 2)
        if (!isAddOneOf(merge->get_operand(i), phi))
            return false;
    return true;
}

// headerGuardTripCount：封装该局部计算，为上层分析或 IR 构造返回所需结果。
bool headerGuardTripCount(Loop *loop, PhiInst *iv, Value *&bound) {
    auto *term = loop->header->get_terminator();
    if (!term || !term->is_br() || term->num_ops() != 3) return false;
    auto *cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cmp || cmp->icmp_op_ != ICmpInst::ICMP_SLT) return false;
    if (cmp->get_operand(0) != iv) return false;
    bound = cmp->get_operand(1);
    return true;
}

// latchGuardTripCount：封装该局部计算，为上层分析或 IR 构造返回所需结果。
bool latchGuardTripCount(Loop *loop, Value *stepValue, Value *&bound) {
    BasicBlock *latch = loop->singleLatch();
    if (!latch || !stepValue) return false;
    auto *term = latch->get_terminator();
    if (!term || !term->is_br() || term->num_ops() != 3) return false;
    auto *cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cmp || cmp->icmp_op_ != ICmpInst::ICMP_SLT) return false;
    if (cmp->get_operand(0) != stepValue) return false;
    bound = cmp->get_operand(1);
    return true;
}

} // namespace

// describeEqualityControlInduction：封装该局部计算，为上层分析或 IR 构造返回所需结果。
bool describeEqualityControlInduction(const Loop &loop,
                                      InductionDescriptor &descriptor) {
    BasicBlock *latch = loop.singleLatch();
    if (!loop.preheader || !latch)
        return false;

    for (auto *instruction : loop.header->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(instruction);
        if (!phi)
            break;
        if (phi->num_ops() != 4 ||
            phi->type_->tid_ != Type::IntegerTyID)
            continue;

        Value *start = nullptr;
        Value *latchValue = nullptr;
        for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
            auto *source =
                dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (source == loop.preheader)
                start = phi->get_operand(i);
            else if (source == latch)
                latchValue = phi->get_operand(i);
        }
        if (!start || !latchValue || !isLoopInvariant(start, &loop))
            continue;

        BinaryInst *update = nullptr;
        Value *step = nullptr;
        bool stepNegated = false;
        std::optional<long long> constantStep;
        if (!matchInductionUpdate(latchValue, phi, &loop, update, step,
                                  stepNegated, constantStep))
            continue;

        ICmpInst *compare = nullptr;
        Value *bound = nullptr;
        ICmpInst::ICmpOp predicate = ICmpInst::ICMP_NE;
        InductionGuardPosition guardPosition =
            InductionGuardPosition::Header;
        bool comparesUpdate = false;

        if (!matchEqualityGuard(loop.header, phi, &loop, compare, bound,
                                predicate)) {
            guardPosition = InductionGuardPosition::Latch;
            if (matchEqualityGuard(latch, update, &loop, compare, bound,
                                   predicate)) {
                comparesUpdate = true;
            } else if (!matchEqualityGuard(latch, phi, &loop, compare, bound,
                                           predicate)) {
                continue;
            }
        }

        descriptor.phi = phi;
        descriptor.start = start;
        descriptor.update = update;
        descriptor.step = step;
        descriptor.stepNegated = stepNegated;
        descriptor.constantStep = constantStep;
        descriptor.compare = compare;
        descriptor.bound = bound;
        descriptor.predicate = predicate;
        descriptor.guardPosition = guardPosition;
        descriptor.comparesUpdate = comparesUpdate;
        return true;
    }
    return false;
}

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
}

// analyze：清空旧结果后遍历当前分析单元，建立后续查询所需的完整摘要。
void LoopInfo::analyze(Function *func) {
    DominatorTreeAnalysis DT;
    DT.analyze(func);
    analyze(func, DT);
}

// analyze：清空旧结果后遍历当前分析单元，建立后续查询所需的完整摘要。
void LoopInfo::analyze(Function *func, const DominatorTreeAnalysis &DT) {
    reset();
    if (!func || func->basic_blocks_.empty()) return;

    // 先建立纯 CFG 意义上的循环集合，再补结构字段和嵌套关系。归纳变量最后分析，因为它
    // 依赖 preheader、latch 和 exit 已经确定。
    findLoops(func, DT);
    for (auto &loop : loops_) enrichLoop(loop.get(), DT);
    buildNestTree();
    for (auto &loop : loops_) analyzeIV(loop.get());
}

// ── Natural loop detection ─────────────────────────────────────────────────
// 回边 bb→succ：succ 支配 bb。把同一 header 的多条回边合并成一个 Loop。

void LoopInfo::findLoops(Function *func, const DominatorTreeAnalysis &DT) {
    std::map<BasicBlock *, Loop *> headerToLoop;

    for (auto bb : func->basic_blocks_) {
        for (auto succ : bb->succ_bbs_) {
            if (!DT.isReachableFromEntry(succ)) continue; // 不可达
            if (!DT.dominates(succ, bb)) continue;        // 不是回边

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

void LoopInfo::enrichLoop(Loop *loop, const DominatorTreeAnalysis &DT) {
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
              [&DT](BasicBlock *a, BasicBlock *b) {
                  unsigned ra = DT.getRPOIndex(a);
                  unsigned rb = DT.getRPOIndex(b);
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
              [&DT](BasicBlock *a, BasicBlock *b) {
                  unsigned ra = DT.getRPOIndex(a);
                  unsigned rb = DT.getRPOIndex(b);
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

// getLoopFor：从 IR 和已有分析结果取得目标信息；缺少可靠结论时返回空值或保守结果。
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

    // Record a general control recurrence first.  This descriptor is
    // analysis-only and does not change the legacy +1 fields consumed by
    // existing loop transforms.
    for (auto *inst : loop->header->instr_list_) {
        if (!inst->is_phi()) break;
        if (inst->type_->tid_ != Type::IntegerTyID) continue;

        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->num_ops() != 4) continue;

        Value *start = nullptr;
        Value *latchValue = nullptr;
        for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
            auto *source =
                dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (source == loop->preheader)
                start = phi->get_operand(i);
            else if (source == loop->singleLatch())
                latchValue = phi->get_operand(i);
        }
        if (!start || !latchValue || !isLoopInvariant(start, loop))
            continue;

        InductionDescriptor descriptor;
        if (!describeControlInduction(loop, phi, start, latchValue,
                                      descriptor))
            continue;
        loop->controlInduction = descriptor;
        break;
    }

    // 1. 找 header 中步长为 +1 的归纳 phi。初值只需在本循环外可用；
    //    零初值形式另记为 canonicalIV，供依赖分析中 tripCount 的旧语义使用。
    PhiInst *iv = nullptr;
    Value *ivInit = nullptr;
    Value *bound = nullptr;
    for (auto *inst : loop->header->instr_list_) {
        if (!inst->is_phi()) break;
        if (inst->type_->tid_ != Type::IntegerTyID) continue;

        auto *phi = static_cast<PhiInst *>(inst);
        // 期望 2 对 (val, BB)：一对来自 preheader 一对来自 latch
        if (phi->num_ops() != 4) continue;

        Value *pre_val = nullptr, *latch_val = nullptr;
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            auto *src = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (src == loop->preheader)
                pre_val = phi->get_operand(i);
            else if (std::find(loop->latches.begin(), loop->latches.end(), src) != loop->latches.end())
                latch_val = phi->get_operand(i);
        }
        if (!pre_val || !latch_val) continue;

        BasicBlock *singleLatch = loop->singleLatch();
        if (!singleLatch || !isStepFromLatch(latch_val, phi, singleLatch))
            continue;

        // A loop may carry several +1 recurrences.  Only the recurrence used
        // by the loop guard is the control induction variable; an earlier
        // header phi may instead be an unrelated counter or reduction.  Do
        // not stop at such a recurrence, or canonicalIV would depend on phi
        // order and downstream loop transforms could see the wrong shape.
        Value *candidateBound = nullptr;
        if (!headerGuardTripCount(loop, phi, candidateBound) &&
            !latchGuardTripCount(loop, latch_val, candidateBound))
            continue;

        iv = phi;
        ivInit = pre_val;
        bound = candidateBound;
        break;
    }
    if (!iv) return;

    loop->inductionIV   = iv;
    loop->inductionInit = ivInit;
    if (auto *ci_init = dynamic_cast<ConstantInt *>(ivInit);
        ci_init && ci_init->value_ == 0)
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
