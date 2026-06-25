#include "../../include/backend/riscv/liveness.hpp"

#include <map>
#include <vector>

namespace riscv {

namespace {

bool isConditionalBranch(const MInst &m) {
    return m.opcode == MOpcode::Branch && m.mnemonic != "j";
}

}  // namespace

// 在显式块 CFG 上做逐指令活跃性：把所有块按布局顺序展平为指令序列，标签解析为
// 块首指令位置；每条指令的后继与扁平时代一致（分支目标 + 条件分支贯穿 i+1 +
// 普通指令贯穿 i+1），因此结果与旧实现等价。
LivenessResult analyzeLiveness(const MFunction &func) {
    LivenessResult result;

    std::vector<const MInst *> seq;
    std::map<std::string, size_t> labelPos;  // 标签 → 其块首指令在 seq 中的位置
    for (const auto &bb : func.blocks) {
        if (bb.hasLabel()) labelPos[bb.label] = seq.size();
        for (const auto &mi : bb.insts) seq.push_back(&mi);
    }
    const size_t n = seq.size();

    std::vector<std::vector<size_t>> succ(n);
    for (size_t i = 0; i < n; ++i) {
        const MInst &m = *seq[i];
        if (m.opcode == MOpcode::Ret) continue;
        if (m.opcode == MOpcode::Branch) {
            if (!m.operands.empty()) {
                auto found = labelPos.find(m.operands.back());
                if (found != labelPos.end()) succ[i].push_back(found->second);
            }
            if (isConditionalBranch(m) && i + 1 < n) succ[i].push_back(i + 1);
            continue;
        }
        if (i + 1 < n) succ[i].push_back(i + 1);
    }

    std::vector<std::set<std::string>> liveIn(n), liveOut(n);
    bool changed;
    do {
        changed = false;
        for (size_t reverse = n; reverse > 0; --reverse) {
            size_t i = reverse - 1;
            std::set<std::string> out;
            for (size_t next : succ[i])
                out.insert(liveIn[next].begin(), liveIn[next].end());

            std::set<std::string> in = seq[i]->uses;
            for (const auto &reg : out)
                if (!seq[i]->defs.count(reg)) in.insert(reg);
            if (in != liveIn[i] || out != liveOut[i]) {
                liveIn[i] = std::move(in);
                liveOut[i] = std::move(out);
                changed = true;
            }
        }
    } while (changed);

    for (size_t i = 0; i < n; ++i) result.liveOut[seq[i]] = liveOut[i];
    return result;
}

}  // namespace riscv
