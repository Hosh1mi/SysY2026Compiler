// LoopInterchangeAnalysis 为循环交换筛选候选。它检查规范形、非归纳 PHI、副作用和依赖
// 方向是否合法，再比较交换前后的访存连续性与并行收益；这里只生成建议，不直接修改 CFG。
#include "../../include/mid/analysis/loopInterchangeAnalysis.hpp"

#include <functional>

namespace {

// hasHeaderIVGuard：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
bool hasHeaderIVGuard(Loop *loop) {
    if (!loop || !loop->header || !loop->canonicalIV) return false;
    auto *term = loop->header->get_terminator();
    if (!term || !term->is_br() || term->num_ops() != 3) return false;
    auto *cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cmp || cmp->icmp_op_ != ICmpInst::ICMP_SLT) return false;
    return cmp->get_operand(0) == loop->canonicalIV;
}

// containsWavefrontCoincidentLoop：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
bool containsWavefrontCoincidentLoop(Loop *loop) {
    if (!loop) return false;
    if (loop->header &&
        loop->header->hasSemFlag(SemFlag::WavefrontCoincident))
        return true;
    for (Loop *child : loop->children)
        if (containsWavefrontCoincidentLoop(child)) return true;
    return false;
}

} // namespace

// isInterchangeLegal：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
bool LoopInterchangeAnalysis::isInterchangeLegal(
    Loop *outer, Loop *inner,
    const std::vector<Instruction *> &accesses) const {
    return DA_->isInterchangeLegal(outer, inner, accesses);
}

// estimateCost：封装该局部计算，为上层分析或 IR 构造返回所需结果。
LoopInterchangeCost LoopInterchangeAnalysis::estimateCost(
    const std::vector<GetElementPtrInst *> &geps,
    PhiInst *beforeInnerIV,
    PhiInst *afterInnerIV) const {
    LoopInterchangeCost cost;
    cost.before = CM_->totalStride(geps, beforeInnerIV);
    cost.after = CM_->totalStride(geps, afterInnerIV);
    return cost;
}

// deepestCanonicalDescendant：封装该局部计算，为上层分析或 IR 构造返回所需结果。
Loop *LoopInterchangeAnalysis::deepestCanonicalDescendant(Loop *loop) const {
    Loop *best = nullptr;
    int bestDepth = -1;
    std::function<void(Loop *)> dfs = [&](Loop *cur) {
        for (auto *child : cur->children) {
            if (child->hasCanonicalIV() && child->depth > bestDepth) {
                best = child;
                bestDepth = child->depth;
            }
            dfs(child);
        }
    };
    if (loop) dfs(loop);
    return best;
}

// hasNonIVHeaderPhi：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
bool LoopInterchangeAnalysis::hasNonIVHeaderPhi(Loop *loop) const {
    if (!loop || !loop->header) return true;
    for (auto *inst : loop->header->instr_list_) {
        if (!inst->is_phi()) break;
        if (inst != loop->canonicalIV) return true;
    }
    return false;
}

ParallelSinkAnalysisResult
// analyzeParallelSink：遍历相关指令或控制流汇总事实；合流处只保留安全结论。
LoopInterchangeAnalysis::analyzeParallelSink(Loop *loop) const {
    ParallelSinkAnalysisResult result;

    if (!loop) {
        result.reason = "null loop";
        return result;
    }
    if (containsWavefrontCoincidentLoop(loop)) {
        result.reason = "wavefront schedule is fixed";
        return result;
    }
    if (loop->children.empty()) {
        result.reason = "no child loop";
        return result;
    }
    if (!loop->hasCanonicalIV()) {
        result.reason = "no canonical IV";
        return result;
    }
    if (!loop->preheader || !loop->singleLatch() || !loop->singleExit()) {
        result.reason = "not single pre/latch/exit";
        return result;
    }
    if (!hasHeaderIVGuard(loop)) {
        result.reason = "not header-guarded";
        return result;
    }
    if (hasNonIVHeaderPhi(loop)) {
        result.reason = "carries scalar reduction";
        return result;
    }

    result.access_info = LA_->collect(loop);
    if (!DA_->isLoopParallel(loop, result.access_info.memory_instructions)) {
        result.reason = "not parallel";
        return result;
    }

    result.cost_loop = deepestCanonicalDescendant(loop);
    if (!result.cost_loop) {
        result.reason = "no canonical descendant";
        return result;
    }

    result.cost = estimateCost(result.access_info.memory_geps,
                               result.cost_loop->canonicalIV,
                               loop->canonicalIV);
    if (!result.cost.known()) {
        result.reason = "stride unknown";
        return result;
    }
    if (!result.cost.profitable()) {
        result.reason = "not profitable";
        return result;
    }

    result.accepted = true;
    result.reason = "accepted";
    return result;
}

ParallelFloatAnalysisResult
// analyzeParallelFloat：遍历相关指令或控制流汇总事实；合流处只保留安全结论。
LoopInterchangeAnalysis::analyzeParallelFloat(Loop *loop) const {
    ParallelFloatAnalysisResult result;

    if (!loop) {
        result.reason = "null loop";
        return result;
    }
    if (containsWavefrontCoincidentLoop(loop)) {
        result.reason = "wavefront schedule is fixed";
        return result;
    }
    if (loop->children.empty()) {
        result.reason = "no child loop";
        return result;
    }
    if (!loop->hasCanonicalIV()) {
        result.reason = "no canonical IV";
        return result;
    }
    if (!loop->preheader || !loop->singleLatch() || !loop->singleExit()) {
        result.reason = "not single pre/latch/exit";
        return result;
    }
    if (hasNonIVHeaderPhi(loop)) {
        result.reason = "carries scalar reduction";
        return result;
    }

    result.access_info = LA_->collect(loop);
    if (DA_->isLoopParallel(loop, result.access_info.memory_instructions)) {
        result.reason = "already parallel";
        return result;
    }

    if (loop->children.size() != 1) {
        result.reason = "not a single-child nest";
        return result;
    }
    result.inner = loop->children[0];
    if (!result.inner->preheader || !result.inner->singleLatch()) {
        result.reason = "inner not single pre/latch";
        return result;
    }
    if (!isInterchangeLegal(loop, result.inner,
                            result.access_info.memory_instructions)) {
        result.reason = "not legal";
        return result;
    }

    if (!result.inner->children.empty()) {
        result.accepted = true;
        result.reason = "accepted";
        return result;
    }

    result.cost_loop = deepestCanonicalDescendant(loop);
    if (!result.cost_loop || result.cost_loop == loop) {
        result.reason = "no canonical descendant";
        return result;
    }
    result.cost = estimateCost(result.access_info.memory_geps,
                               result.cost_loop->canonicalIV,
                               loop->canonicalIV);
    if (!result.cost.known()) {
        result.reason = "stride unknown";
        return result;
    }
    if (!result.cost.profitable()) {
        result.reason = "not profitable";
        return result;
    }

    result.accepted = true;
    result.reason = "accepted";
    return result;
}
