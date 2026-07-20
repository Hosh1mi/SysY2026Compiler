#include "pairVector.hpp"

#include "../../../../include/backend/arm64/machineTransforms/utils.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>
#include <string>

namespace {

struct PointerExpr {
    std::string root;
    int offset = 0;
    bool valid = false;
};

bool parseImmediate(const std::string &operand, int &value) {
    std::string text = peephTrim(operand);
    if (text.size() < 2 || text[0] != '#') return false;
    char *end = nullptr;
    long parsed = std::strtol(text.c_str() + 1, &end, 0);
    if (!end || *end != '\0' || parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max())
        return false;
    value = static_cast<int>(parsed);
    return true;
}

PointerExpr pointerExprBefore(const MachineBasicBlock &block, size_t end,
                              const std::string &reg) {
    std::map<std::string, PointerExpr> values;
    auto get = [&](const std::string &source) {
        auto found = values.find(source);
        if (found != values.end()) return found->second;
        return PointerExpr{source, 0, peephRegClass(source) == 'x'};
    };

    for (size_t i = 0; i < end; ++i) {
        const MachineInstr &line = block.instrs[i];
        if (line.isLabelLike) {
            values.clear();
            continue;
        }
        if (line.asmOperands.empty() ||
            peephRegClass(line.asmOperands[0]) != 'x')
            continue;

        const std::string &dst = line.asmOperands[0];
        PointerExpr result;
        if (line.opcodeText == "mov" && line.asmOperands.size() == 2 &&
            peephRegClass(line.asmOperands[1]) == 'x') {
            result = get(line.asmOperands[1]);
        } else if ((line.opcodeText == "add" || line.opcodeText == "sub") &&
                   line.asmOperands.size() == 3 &&
                   peephRegClass(line.asmOperands[1]) == 'x') {
            int amount = 0;
            if (parseImmediate(line.asmOperands[2], amount)) {
                result = get(line.asmOperands[1]);
                if (result.valid)
                    result.offset += line.opcodeText == "add" ? amount : -amount;
            }
        }
        if (!result.valid && peephLineWritesReg(line, dst))
            result = {dst, 0, true};
        values[dst] = result;
    }
    return get(reg);
}

bool isPlainVectorAccess(const MachineInstr &line, bool &isLoad,
                         std::string &valueReg, MemOperand &address) {
    if (line.isLabelLike || line.asmOperands.size() != 2) return false;
    if (line.opcodeText == "ldr")
        isLoad = true;
    else if (line.opcodeText == "str")
        isLoad = false;
    else
        return false;
    valueReg = line.asmOperands[0];
    if (peephRegClass(valueReg) != 'q') return false;
    address = peephParseMemOp(line.asmOperands[1]);
    return address.valid && peephRegClass(address.base) == 'x' &&
           address.base != "sp";
}

bool safeToPairAcross(const MachineBasicBlock &block, size_t first,
                      size_t second, bool isLoad,
                      const std::string &secondValue) {
    for (size_t i = first + 1; i < second; ++i) {
        const MachineInstr &line = block.instrs[i];
        if (line.isLabelLike || line.isCall || line.isBarrier ||
            peephLineUsesReg(line, secondValue))
            return false;
        if (isLoad) {
            if (line.mayStore || line.opcode == MOpcode::Div)
                return false;
        } else {
            if (line.mayLoad || line.mayStore || line.opcode == MOpcode::Div)
                return false;
            if (line.opcode != MOpcode::Mov && line.opcode != MOpcode::Alu &&
                line.opcode != MOpcode::Mul && line.opcode != MOpcode::Neon)
                return false;
        }
    }
    return true;
}

} // namespace

bool tryMachinePairVectorAccesses(MachineBasicBlock &block, size_t idx) {
    bool firstIsLoad = false;
    std::string firstValue;
    MemOperand firstAddress;
    if (!isPlainVectorAccess(block.instrs[idx], firstIsLoad, firstValue,
                             firstAddress))
        return false;

    PointerExpr firstExpr = pointerExprBefore(block, idx, firstAddress.base);
    if (!firstExpr.valid) return false;
    firstExpr.offset += firstAddress.offset;

    const size_t scanEnd = std::min(block.instrs.size(), idx + 12);
    for (size_t secondIdx = idx + 1; secondIdx < scanEnd; ++secondIdx) {
        bool secondIsLoad = false;
        std::string secondValue;
        MemOperand secondAddress;
        const MachineInstr &line = block.instrs[secondIdx];
        if (line.isLabelLike || line.isCall || line.isBarrier) break;
        if (!isPlainVectorAccess(line, secondIsLoad, secondValue,
                                 secondAddress))
            continue;
        if (secondIsLoad != firstIsLoad ||
            peephSamePhysicalReg(firstValue, secondValue))
            continue;

        PointerExpr secondExpr = pointerExprBefore(block, secondIdx,
                                                   secondAddress.base);
        if (!secondExpr.valid) continue;
        secondExpr.offset += secondAddress.offset;
        if (firstExpr.root != secondExpr.root ||
            secondExpr.offset - firstExpr.offset != 16)
            continue;
        if (!safeToPairAcross(block, idx, secondIdx, firstIsLoad,
                              secondValue))
            continue;
        if (firstAddress.offset < -1024 || firstAddress.offset > 1008 ||
            firstAddress.offset % 16 != 0)
            continue;

        const std::string opcode = firstIsLoad ? "ldp" : "stp";
        peephReplaceInstr(block.instrs[idx],
                          peephMakeInsn(opcode,
                                       {firstValue, secondValue,
                                        block.instrs[idx].asmOperands[1]}));
        block.instrs.erase(block.instrs.begin() + secondIdx);
        return true;
    }
    return false;
}
