#include "../../include/backend/riscv/peephole.hpp"

#include <map>
#include <string>
#include <vector>

namespace riscv {

namespace {

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

}  // namespace riscv
