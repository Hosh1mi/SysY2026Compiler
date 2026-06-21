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

}  // namespace riscv
