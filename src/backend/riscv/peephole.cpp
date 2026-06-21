#include "../../include/backend/riscv/peephole.hpp"
#include "../../include/backend/riscv/liveness.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace riscv {

namespace {

// 可被重定向输出寄存器的生产者（首操作数为唯一定义）。store/call/branch 排除。
bool isRedirectableProducer(MOpcode op) {
    switch (op) {
    case MOpcode::Move:
    case MOpcode::Load:
    case MOpcode::Alu:
    case MOpcode::Mul:
    case MOpcode::Div:
    case MOpcode::Address:
        return true;
    default:
        return false;
    }
}

// operand[0] 是否为定义位置（其余为使用）。与 MInst 解析一致：算术/搬运/加载/
// 地址类把首操作数计入 defs；store/branch/ret/call 的全部操作数都是使用。
bool firstOperandIsDef(MOpcode op) {
    switch (op) {
    case MOpcode::Move:
    case MOpcode::Load:
    case MOpcode::Alu:
    case MOpcode::Mul:
    case MOpcode::Div:
    case MOpcode::Address:
        return true;
    default:
        return false;
    }
}

// 把单个操作数（"reg" 或 "imm(reg)"）中的寄存器经 canon 改写。
std::string rewriteOperand(const std::string &operand,
                           const std::map<std::string, std::string> &canon) {
    auto subst = [&](const std::string &reg) -> std::string {
        auto it = canon.find(reg);
        return it == canon.end() ? reg : it->second;
    };
    size_t l = operand.find('(');
    if (l == std::string::npos) return subst(operand);
    size_t r = operand.find(')', l);
    if (r == std::string::npos) return operand;
    return operand.substr(0, l + 1) + subst(operand.substr(l + 1, r - l - 1)) +
           operand.substr(r);
}

bool isPlainRegMove(const MInst &m) {
    return (m.mnemonic == "mv" || m.mnemonic == "fmv.s" || m.mnemonic == "fmv.d") &&
           m.operands.size() == 2;
}

std::string blockLabelName(const std::string &text) {
    std::string s = text;
    if (!s.empty() && s.back() == ':') s.pop_back();
    return s;
}

// 用 mnemonic + operands 重建分支文本。
std::string rebuildBranch(const std::string &mnemonic,
                          const std::vector<std::string> &operands) {
    std::string text = mnemonic;
    for (size_t k = 0; k < operands.size(); ++k) text += (k ? ", " : " ") + operands[k];
    return text;
}

}  // namespace

bool removeSelfMoves(MFunction &func) {
    bool changed = false;
    for (auto it = func.insts.begin(); it != func.insts.end();) {
        bool selfMove = it->opcode == MOpcode::Move && it->operands.size() == 2 &&
                        it->operands[0] == it->operands[1];
        if (selfMove) {
            it = func.insts.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    return changed;
}

bool forwardAdjacentStoreLoads(MFunction &func) {
    bool changed = false;
    for (size_t i = 0; i + 1 < func.insts.size(); ++i) {
        const MInst &store = func.insts[i];
        const MInst &load = func.insts[i + 1];
        if (store.opcode != MOpcode::Store || load.opcode != MOpcode::Load ||
            store.operands.size() != 2 || load.operands.size() != 2 ||
            store.operands[1] != load.operands[1])
            continue;

        std::string move;
        if (store.mnemonic == "sd" && load.mnemonic == "ld") {
            move = "mv " + load.operands[0] + ", " + store.operands[0];
        } else if (store.mnemonic == "sw" && load.mnemonic == "lw") {
            // lw sign-extends the stored low word; addiw with zero preserves
            // that behavior even if the source register is not canonical i32.
            move = "addiw " + load.operands[0] + ", " + store.operands[0] + ", 0";
        } else if (store.mnemonic == "fsw" && load.mnemonic == "flw") {
            move = "fmv.s " + load.operands[0] + ", " + store.operands[0];
        } else if (store.mnemonic == "fsd" && load.mnemonic == "fld") {
            move = "fmv.d " + load.operands[0] + ", " + store.operands[0];
        } else {
            continue;
        }
        func.insts[i + 1] = MInst::inst(std::move(move));
        changed = true;
    }
    return changed;
}

bool propagateCopies(MFunction &func) {
    bool changed = false;
    // canon[reg] = 当前 reg 镜像的源寄存器（已完全解析，无需再链式查找）。
    std::map<std::string, std::string> canon;

    for (auto &mi : func.insts) {
        // 基本块边界与控制流终结点处副本关系不再可靠，清空。
        if (mi.isLabel || mi.isTerminator) {
            canon.clear();
            continue;
        }
        if (mi.isDirective) continue;

        // 1. 改写使用位置的操作数（跳过 def 首操作数）。
        bool firstDef = firstOperandIsDef(mi.opcode);
        if (!mi.operands.empty() && !canon.empty()) {
            std::vector<std::string> newOps = mi.operands;
            bool opChanged = false;
            for (size_t i = 0; i < newOps.size(); ++i) {
                if (firstDef && i == 0) continue;
                std::string nw = rewriteOperand(newOps[i], canon);
                if (nw != newOps[i]) { newOps[i] = nw; opChanged = true; }
            }
            if (opChanged) {
                std::string text = mi.mnemonic;
                for (size_t i = 0; i < newOps.size(); ++i)
                    text += (i ? ", " : " ") + newOps[i];
                MInst rebuilt = MInst::inst(text);
                rebuilt.mayLoad = mi.mayLoad;
                rebuilt.mayStore = mi.mayStore;
                rebuilt.isCall = mi.isCall;
                rebuilt.isTerminator = mi.isTerminator;
                mi = std::move(rebuilt);
                changed = true;
            }
        }

        // 2. 本指令的定义（含 call 对 caller-saved 的钳制）使相关副本失效。
        if (!mi.defs.empty()) {
            for (auto it = canon.begin(); it != canon.end();) {
                if (mi.defs.count(it->first) || mi.defs.count(it->second))
                    it = canon.erase(it);
                else
                    ++it;
            }
        }

        // 3. 记录新副本（源经现有 canon 解析到根）。
        if (isPlainRegMove(mi)) {
            const std::string &dst = mi.operands[0];
            std::string src = mi.operands[1];
            auto it = canon.find(src);
            if (it != canon.end()) src = it->second;
            if (dst != src) canon[dst] = src;
        } else if (mi.mnemonic == "li" && mi.operands.size() == 2 &&
                   mi.operands[1] == "0") {
            // 持 0 的寄存器可统一规范到 zero(x0)：对其使用改写为 zero 后，原 li 0
            // 成为死代码被 DCE 删除。用 zero 顶替任意源操作数恒等价。
            const std::string &dst = mi.operands[0];
            if (dst != "zero") canon[dst] = "zero";
        }
    }
    return changed;
}

bool redirectProducers(MFunction &func) {
    bool changedAny = false;
    bool changed = true;
    while (changed) {
        changed = false;
        LivenessResult liveness = analyzeLiveness(func);
        const int n = static_cast<int>(func.insts.size());
        for (int i = 0; i < n; ++i) {
            MInst &move = func.insts[i];
            if (!isPlainRegMove(move)) continue;
            const std::string dst = move.operands[0];
            const std::string src = move.operands[1];
            if (dst == src || dst == "zero" || src == "zero") continue;

            // src 必须在该 mv 处死亡（之后不再活跃），否则重定向会丢失它的值。
            auto lo = liveness.liveOut.find(&move);
            if (lo != liveness.liveOut.end() && lo->second.count(src)) continue;

            // 向上在同一标签段内找 src 的生产者（最近一次定义）。
            int p = -1;
            for (int j = i - 1; j >= 0; --j) {
                if (func.insts[j].isLabel) break;  // 段边界：生产者不支配该 mv
                if (func.insts[j].defs.count(src)) { p = j; break; }
            }
            if (p < 0) continue;
            MInst &prod = func.insts[p];
            if (!isRedirectableProducer(prod.opcode) || prod.defs.size() != 1 ||
                prod.operands.empty() || prod.operands[0] != src)
                continue;
            // dst 在生产者处不得活跃：重定向会在此新建一处 dst 定义，若 dst 的旧值
            // 仍被下游（含 mv 之外的分支路径）需要就会被覆盖。该条件也覆盖生产者与
            // mv 之间对 dst 的线性使用。生产者可读 dst（同条指令读旧值先于写新值）。
            auto plo = liveness.liveOut.find(&prod);
            if (plo != liveness.liveOut.end() && plo->second.count(dst)) continue;
            // 生产者读取 dst 时不重定向：把输出改写为 dst 会就地覆盖 dst 旧值，在
            // 并行拷贝/链式拷贝里若其它拷贝仍需 dst 旧值（迭代折叠后才显现）即出错。
            if (prod.uses.count(dst)) continue;

            // 生产者与 mv 之间，src/dst 均不得被使用或重定义（call 钳制计入定义）。
            bool ok = true;
            for (int k = p + 1; k < i && ok; ++k) {
                const MInst &mid = func.insts[k];
                if (mid.uses.count(dst) || mid.defs.count(dst)) ok = false;
                if (mid.uses.count(src) || mid.defs.count(src)) ok = false;
            }
            if (!ok) continue;

            // 重写生产者输出寄存器 src→dst，删除该 mv。
            std::vector<std::string> newOps = prod.operands;
            newOps[0] = dst;
            std::string text = prod.mnemonic;
            for (size_t t = 0; t < newOps.size(); ++t)
                text += (t ? ", " : " ") + newOps[t];
            MInst rebuilt = MInst::inst(text);
            rebuilt.mayLoad = prod.mayLoad;
            rebuilt.mayStore = prod.mayStore;
            rebuilt.isCall = prod.isCall;
            rebuilt.isTerminator = prod.isTerminator;
            prod = std::move(rebuilt);
            func.insts.erase(func.insts.begin() + i);
            changed = changedAny = true;
            break;
        }
    }
    return changedAny;
}

bool forwardBranches(MFunction &func) {
    auto &insts = func.insts;
    // 蹦床块：标签后紧跟唯一一条无条件 j。记录 标签 → 该 j 的目标。
    std::map<std::string, std::string> tramp;
    for (size_t i = 0; i + 1 < insts.size(); ++i) {
        if (!insts[i].isLabel) continue;
        const MInst &j = insts[i + 1];
        bool lone = j.opcode == MOpcode::Branch && j.mnemonic == "j" &&
                    !j.operands.empty() &&
                    (i + 2 >= insts.size() || insts[i + 2].isLabel);
        if (lone) tramp[blockLabelName(insts[i].text)] = j.operands.back();
    }
    if (tramp.empty()) return false;

    auto resolve = [&](std::string label) -> std::string {
        std::set<std::string> seen;
        while (tramp.count(label) && seen.insert(label).second) label = tramp[label];
        return label;
    };

    bool changed = false;
    for (auto &mi : insts) {
        if (mi.opcode != MOpcode::Branch || mi.operands.empty()) continue;
        const std::string tgt = mi.operands.back();
        if (!tramp.count(tgt)) continue;
        std::string fin = resolve(tgt);
        if (fin == tgt) continue;  // 自环等无法前进
        std::vector<std::string> ops = mi.operands;
        ops.back() = fin;
        mi = MInst::inst(rebuildBranch(mi.mnemonic, ops));
        changed = true;
    }
    return changed;
}

bool removeDeadBlocks(MFunction &func) {
    auto &insts = func.insts;
    const int n = static_cast<int>(insts.size());
    if (n == 0) return false;

    // 块起点：索引 0 及每个标签。
    std::vector<int> starts;
    for (int i = 0; i < n; ++i)
        if (i == 0 || insts[i].isLabel) starts.push_back(i);
    const int nb = static_cast<int>(starts.size());
    auto blockEnd = [&](int b) { return b + 1 < nb ? starts[b + 1] : n; };

    std::map<std::string, int> labelBlock;
    for (int b = 0; b < nb; ++b)
        if (insts[starts[b]].isLabel)
            labelBlock[blockLabelName(insts[starts[b]].text)] = b;

    std::vector<char> reach(nb, 0);
    std::vector<int> stack;
    reach[0] = 1;
    stack.push_back(0);
    auto mark = [&](int blk) {
        if (blk >= 0 && blk < nb && !reach[blk]) { reach[blk] = 1; stack.push_back(blk); }
    };
    while (!stack.empty()) {
        int b = stack.back();
        stack.pop_back();
        const int s = starts[b], e = blockEnd(b);
        // 标签段内可能含中途的条件分支：扫描全部分支目标，而非只看末条指令。
        for (int i = s; i < e; ++i) {
            const MInst &mi = insts[i];
            if (mi.opcode == MOpcode::Branch && !mi.operands.empty()) {
                auto it = labelBlock.find(mi.operands.back());
                if (it != labelBlock.end()) mark(it->second);
            }
        }
        const MInst &last = insts[e - 1];
        bool noFallthrough = last.opcode == MOpcode::Ret ||
                             (last.opcode == MOpcode::Branch && last.mnemonic == "j");
        if (!noFallthrough) mark(b + 1);
    }

    bool anyDead = false;
    for (int b = 0; b < nb; ++b)
        if (!reach[b]) { anyDead = true; break; }
    if (!anyDead) return false;  // 无死块时不得移动原指令（否则留下空指令）

    std::vector<MInst> kept;
    kept.reserve(insts.size());
    for (int b = 0; b < nb; ++b)
        if (reach[b])
            for (int i = starts[b]; i < blockEnd(b); ++i) kept.push_back(std::move(insts[i]));
    func.insts = std::move(kept);
    return true;
}

bool removeFallthroughJumps(MFunction &func) {
    auto &insts = func.insts;
    bool changed = false;
    for (size_t i = 0; i + 1 < insts.size(); ++i) {
        const MInst &mi = insts[i];
        if (mi.opcode == MOpcode::Branch && mi.mnemonic == "j" && !mi.operands.empty() &&
            insts[i + 1].isLabel &&
            blockLabelName(insts[i + 1].text) == mi.operands.back()) {
            insts.erase(insts.begin() + i);
            changed = true;
            --i;
        }
    }
    return changed;
}

}  // namespace riscv
