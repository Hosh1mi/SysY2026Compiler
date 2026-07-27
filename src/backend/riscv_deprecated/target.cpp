#include "../../include/backend/riscv/target.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>

namespace riscv {

namespace {

// x0-x31 / f0-f31 的 ABI 名（索引即寄存器号）。
const std::array<const char *, 32> kXAbi = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
const std::array<const char *, 32> kFAbi = {
    "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
    "fs0", "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5",
    "fa6", "fa7", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
    "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};

// ABI 名 → 寄存器类。fp 作为 s0 的别名归入 GPR。
const std::unordered_map<std::string, RegClass> &abiClass() {
    static const std::unordered_map<std::string, RegClass> m = [] {
        std::unordered_map<std::string, RegClass> t;
        for (const char *n : kXAbi) t[n] = RegClass::GPR;
        for (const char *n : kFAbi) t[n] = RegClass::FPR;
        t["fp"] = RegClass::GPR;
        return t;
    }();
    return m;
}

// 解析 xN / fN 数字名为 [0,31] 的寄存器号；非该形式返回 -1。
int numericRegIndex(const std::string &name, char prefix) {
    if (name.size() < 2 || name[0] != prefix) return -1;
    if (!std::all_of(name.begin() + 1, name.end(),
                     [](unsigned char c) { return std::isdigit(c); }))
        return -1;
    int v = std::stoi(name.substr(1));
    return (v >= 0 && v < 32) ? v : -1;
}

const std::set<std::string> &callerSavedSet() {
    static const std::set<std::string> s = {
        "ra", "t0", "t1", "t2", "t3", "t4", "t5", "t6",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
        "ft8", "ft9", "ft10", "ft11",
        "fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7"};
    return s;
}

const std::set<std::string> &calleeSavedSet() {
    static const std::set<std::string> s = {
        "s0", "s1", "s2", "s3", "s4", "s5", "s6",
        "s7", "s8", "s9", "s10", "s11",
        "fs0", "fs1", "fs2", "fs3", "fs4", "fs5",
        "fs6", "fs7", "fs8", "fs9", "fs10", "fs11"};
    return s;
}

}  // namespace

std::optional<RegClass> regClassOf(const std::string &name) {
    auto it = abiClass().find(name);
    if (it != abiClass().end()) return it->second;
    if (numericRegIndex(name, 'x') >= 0) return RegClass::GPR;
    if (numericRegIndex(name, 'f') >= 0) return RegClass::FPR;
    return std::nullopt;
}

bool isReg(const std::string &name) { return regClassOf(name).has_value(); }

std::string canonicalReg(const std::string &name) {
    if (name == "fp") return "s0";
    if (int x = numericRegIndex(name, 'x'); x >= 0) return kXAbi[x];
    if (int f = numericRegIndex(name, 'f'); f >= 0) return kFAbi[f];
    return name;  // 已是 ABI 名或非寄存器
}

const std::set<std::string> &callClobbers() { return callerSavedSet(); }

bool isCallerSaved(const std::string &name) {
    return callerSavedSet().count(canonicalReg(name)) != 0;
}

bool isCalleeSaved(const std::string &name) {
    return calleeSavedSet().count(canonicalReg(name)) != 0;
}

}  // namespace riscv
