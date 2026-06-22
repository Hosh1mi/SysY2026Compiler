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
    for (auto &bb : func.blocks) {
        for (auto it = bb.insts.begin(); it != bb.insts.end();) {
            bool selfMove = it->opcode == MOpcode::Move && it->operands.size() == 2 &&
                            it->operands[0] == it->operands[1];
            if (selfMove) {
                it = bb.insts.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    return changed;
}

bool forwardAdjacentStoreLoads(MFunction &func) {
    bool changed = false;
    for (auto &bb : func.blocks) {
        for (size_t i = 0; i + 1 < bb.insts.size(); ++i) {
            const MInst &store = bb.insts[i];
            const MInst &load = bb.insts[i + 1];
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
            bb.insts[i + 1] = MInst::inst(std::move(move));
            changed = true;
        }
    }
    return changed;
}

bool propagateCopies(MFunction &func) {
    bool changed = false;
    for (auto &bb : func.blocks) {
        // 每个块独立：块首副本关系为空（标签段头可能有多前驱）。
        std::map<std::string, std::string> canon;
        for (auto &mi : bb.insts) {
            if (mi.isDirective) continue;
            // 终结点（j/ret）处副本关系不再可靠，且不改写其操作数。
            if (mi.isTerminator) {
                canon.clear();
                continue;
            }

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
    }
    return changed;
}

bool redirectProducers(MFunction &func) {
    bool changedAny = false;
    bool changed = true;
    while (changed) {
        changed = false;
        LivenessResult liveness = analyzeLiveness(func);
        for (auto &bb : func.blocks) {
            const int n = static_cast<int>(bb.insts.size());
            for (int i = 0; i < n; ++i) {
                MInst &move = bb.insts[i];
                if (!isPlainRegMove(move)) continue;
                const std::string dst = move.operands[0];
                const std::string src = move.operands[1];
                if (dst == src || dst == "zero" || src == "zero") continue;

                // src 必须在该 mv 处死亡（之后不再活跃），否则重定向会丢失它的值。
                auto lo = liveness.liveOut.find(&move);
                if (lo != liveness.liveOut.end() && lo->second.count(src)) continue;

                // 向上在同一块内找 src 的生产者（最近一次定义）。块首即段边界。
                int p = -1;
                for (int j = i - 1; j >= 0; --j) {
                    if (bb.insts[j].defs.count(src)) { p = j; break; }
                }
                if (p < 0) continue;
                MInst &prod = bb.insts[p];
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
                    const MInst &mid = bb.insts[k];
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
                bb.insts.erase(bb.insts.begin() + i);
                changed = changedAny = true;
                break;
            }
            if (changed) break;
        }
    }
    return changedAny;
}

bool forwardBranches(MFunction &func) {
    // 蹦床块：仅含一条无条件 j 的带标签块。记录 标签 → 该 j 的目标。
    std::map<std::string, std::string> tramp;
    for (const auto &bb : func.blocks) {
        if (!bb.hasLabel() || bb.insts.size() != 1) continue;
        const MInst &j = bb.insts[0];
        if (j.opcode == MOpcode::Branch && j.mnemonic == "j" && !j.operands.empty())
            tramp[bb.label] = j.operands.back();
    }
    if (tramp.empty()) return false;

    auto resolve = [&](std::string label) -> std::string {
        std::set<std::string> seen;
        while (tramp.count(label) && seen.insert(label).second) label = tramp[label];
        return label;
    };

    bool changed = false;
    for (auto &bb : func.blocks) {
        for (auto &mi : bb.insts) {
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
    }
    return changed;
}

bool removeDeadBlocks(MFunction &func) {
    const int nb = static_cast<int>(func.blocks.size());
    if (nb == 0) return false;

    std::map<std::string, int> labelToBlock;
    for (int b = 0; b < nb; ++b)
        if (func.blocks[b].hasLabel()) labelToBlock[func.blocks[b].label] = b;

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
        const MBasicBlock &bb = func.blocks[b];
        // 块内可能含中途的条件分支：扫描全部分支目标，而非只看末条指令。
        for (const MInst &mi : bb.insts) {
            if (mi.opcode == MOpcode::Branch && !mi.operands.empty()) {
                auto it = labelToBlock.find(mi.operands.back());
                if (it != labelToBlock.end()) mark(it->second);
            }
        }
        bool noFallthrough = !bb.insts.empty() && bb.insts.back().isTerminator;
        if (!noFallthrough) mark(b + 1);
    }

    bool anyDead = false;
    for (int b = 0; b < nb; ++b)
        if (!reach[b]) { anyDead = true; break; }
    if (!anyDead) return false;

    std::vector<MBasicBlock> kept;
    kept.reserve(func.blocks.size());
    for (int b = 0; b < nb; ++b)
        if (reach[b]) kept.push_back(std::move(func.blocks[b]));
    func.blocks = std::move(kept);
    buildCFG(func);  // 索引随删除而变，刷新邻接
    return true;
}

bool removeFallthroughJumps(MFunction &func) {
    bool changed = false;
    const int nb = static_cast<int>(func.blocks.size());
    for (int i = 0; i + 1 < nb; ++i) {
        MBasicBlock &bb = func.blocks[i];
        if (bb.insts.empty()) continue;
        const MInst &last = bb.insts.back();
        if (last.opcode == MOpcode::Branch && last.mnemonic == "j" && !last.operands.empty() &&
            func.blocks[i + 1].hasLabel() &&
            func.blocks[i + 1].label == last.operands.back()) {
            bb.insts.pop_back();
            changed = true;
        }
    }
    return changed;
}

}  // namespace riscv
