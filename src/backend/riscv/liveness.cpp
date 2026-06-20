#include "../../include/backend/riscv/liveness.hpp"

#include <algorithm>
#include <vector>

namespace riscv {

namespace {

std::string labelName(std::string text) {
    if (!text.empty() && text.back() == ':') text.pop_back();
    return text;
}

bool isConditionalBranch(const MInst &m) {
    return m.opcode == MOpcode::Branch && m.mnemonic != "j";
}

}  // namespace

LivenessResult analyzeLiveness(const MFunction &func) {
    LivenessResult result;
    const size_t n = func.insts.size();
    std::map<std::string, size_t> labels;
    for (size_t i = 0; i < n; ++i)
        if (func.insts[i].isLabel) labels[labelName(func.insts[i].text)] = i;

    std::vector<std::vector<size_t>> succ(n);
    for (size_t i = 0; i < n; ++i) {
        const MInst &m = func.insts[i];
        if (m.opcode == MOpcode::Ret) continue;
        if (m.opcode == MOpcode::Branch) {
            if (!m.operands.empty()) {
                const std::string &target = m.operands.back();
                auto found = labels.find(target);
                if (found != labels.end()) succ[i].push_back(found->second);
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

            std::set<std::string> in = func.insts[i].uses;
            for (const auto &reg : out)
                if (!func.insts[i].defs.count(reg)) in.insert(reg);
            if (in != liveIn[i] || out != liveOut[i]) {
                liveIn[i] = std::move(in);
                liveOut[i] = std::move(out);
                changed = true;
            }
        }
    } while (changed);

    for (size_t i = 0; i < n; ++i) result.liveOut[&func.insts[i]] = liveOut[i];
    return result;
}

}  // namespace riscv
