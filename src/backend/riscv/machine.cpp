#include "../../include/backend/riscv/machine.hpp"
#include "../../include/backend/riscv/target.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace riscv {

namespace {

std::string trim(const std::string &s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin])))
        ++begin;
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(begin, end - begin);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::vector<std::string> splitOperands(const std::string &s) {
    std::vector<std::string> result;
    std::string current;
    int parenDepth = 0;
    for (char ch : s) {
        if (ch == '(') ++parenDepth;
        if (ch == ')') --parenDepth;
        if (ch == ',' && parenDepth == 0) {
            result.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!trim(current).empty()) result.push_back(trim(current));
    return result;
}

// 把操作数（"reg" 或 "imm(reg)"）中的物理寄存器规范化为 ABI 名后加入集合。
// 寄存器类别、xN/fN 别名、fp→s0 等统一由目标描述（target.hpp）裁决。
void addRegister(std::set<std::string> &set, const std::string &operand) {
    std::string op = trim(operand);
    if (isReg(op)) {
        set.insert(canonicalReg(op));
        return;
    }
    size_t l = op.find('('), r = op.find(')', l == std::string::npos ? 0 : l + 1);
    if (l != std::string::npos && r != std::string::npos)
        addRegister(set, op.substr(l + 1, r - l - 1));
}

bool isOneOf(const std::string &op, std::initializer_list<const char *> names) {
    for (const char *name : names)
        if (op == name) return true;
    return false;
}

void addCallBoundary(MInst &m) {
    // Calls observe the current stack pointer (including stack-passed
    // arguments) and conservatively read every argument register; they
    // overwrite the full caller-saved set (target description).
    m.uses.insert("sp");
    for (int i = 0; i < 8; ++i) {
        m.uses.insert("a" + std::to_string(i));
        m.uses.insert("fa" + std::to_string(i));
    }
    for (const auto &reg : callClobbers()) m.defs.insert(reg);
}

std::string labelTarget(const MInst &m) {
    return m.hasLabelTarget() ? m.operands.back() : std::string();
}

// 该 opcode 的首操作数是否为定义位置（其余为使用）。算术/搬运/加载/地址类把首
// 操作数计入 defs；store/branch/ret/call 的全部操作数都是使用。
bool firstOperandIsDef(MOpcode op) {
    switch (op) {
    case MOpcode::Move:
    case MOpcode::Alu:
    case MOpcode::Mul:
    case MOpcode::Div:
    case MOpcode::Load:
    case MOpcode::Address:
        return true;
    default:
        return false;
    }
}

// 把单个操作数文本解析为结构化 MOperand。
MOperand parseOperand(const std::string &raw, bool isDef) {
    MOperand o;
    o.text = raw;
    o.isDef = isDef;
    std::string op = trim(raw);

    size_t l = op.find('(');
    if (l != std::string::npos) {
        size_t r = op.find(')', l);
        o.kind = MOperand::Kind::Mem;
        std::string base = r != std::string::npos ? trim(op.substr(l + 1, r - l - 1)) : "";
        o.reg = canonicalReg(base);
        o.regClass = regClassOf(o.reg).value_or(RegClass::GPR);
        return o;
    }
    if (isReg(op)) {
        o.kind = MOperand::Kind::Reg;
        o.reg = canonicalReg(op);
        o.regClass = regClassOf(o.reg).value_or(RegClass::GPR);
        return o;
    }
    bool numeric = !op.empty() &&
                   (std::isdigit(static_cast<unsigned char>(op[0])) ||
                    (op[0] == '-' && op.size() > 1));
    o.kind = numeric ? MOperand::Kind::Imm : MOperand::Kind::Label;
    return o;
}

// 寄存器操作数在该指令中应有的寄存器类；返回 nullopt 表示跳过检查（类混合的
// 转换/搬运/比较指令操作数横跨两类，逐位语义不规则，不做静态裁定）。
std::optional<RegClass> expectedRegClass(const std::string &mnemonic, const MOperand &op) {
    // 访存基址寄存器在 RV 上恒为 GPR。
    if (op.kind == MOperand::Kind::Mem) return RegClass::GPR;
    // 整型（非 'f' 前缀）指令的寄存器操作数一律 GPR。
    if (mnemonic.empty() || mnemonic[0] != 'f') return RegClass::GPR;
    // 跨类的 fmv/fcvt/比较：dest 与 src 分属不同类，跳过。
    static const std::set<std::string> mixed = {
        "fmv.x.w", "fmv.w.x", "fmv.x.d", "fmv.d.x",
        "fcvt.w.s", "fcvt.s.w", "fcvt.wu.s", "fcvt.s.wu",
        "fcvt.l.s", "fcvt.s.l", "fcvt.lu.s", "fcvt.s.lu",
        "fcvt.w.d", "fcvt.d.w", "fcvt.s.d", "fcvt.d.s",
        "feq.s", "flt.s", "fle.s", "feq.d", "flt.d", "fle.d"};
    if (mixed.count(mnemonic)) return std::nullopt;
    // 其余浮点指令（fadd.s/fmul.s/fmv.s/flw 的数据寄存器/fsw 的数据寄存器…）为 FPR。
    return RegClass::FPR;
}

}  // namespace

MInst MInst::inst(std::string text) {
    MInst m;
    m.text = trim(text);
    if (m.text.empty() || m.text[0] == '#') {
        m.isBarrier = true;
        return m;
    }

    size_t split = m.text.find_first_of(" \t");
    m.mnemonic = lower(m.text.substr(0, split));
    if (split != std::string::npos)
        m.operands = splitOperands(m.text.substr(split + 1));

    const std::string &op = m.mnemonic;
    if (isOneOf(op, {"call"})) {
        m.opcode = MOpcode::Call;
        m.isCall = m.isBarrier = true;
        addCallBoundary(m);
    } else if (op == "ret") {
        m.opcode = MOpcode::Ret;
        m.isTerminator = m.isBarrier = true;
        m.uses.insert("a0");
        m.uses.insert("fa0");
        m.uses.insert("sp");
        m.uses.insert("ra");
    } else if (isOneOf(op, {"j", "beq", "bne", "blt", "bge", "bltu", "bgeu", "beqz", "bnez"})) {
        m.opcode = MOpcode::Branch;
        m.isTerminator = op == "j";
        for (const auto &operand : m.operands) addRegister(m.uses, operand);
    } else if (isOneOf(op, {"lw", "ld", "flw", "fld"})) {
        m.opcode = MOpcode::Load;
        m.mayLoad = true;
        if (!m.operands.empty()) addRegister(m.defs, m.operands[0]);
        if (m.operands.size() > 1) addRegister(m.uses, m.operands[1]);
    } else if (isOneOf(op, {"sw", "sd", "fsw", "fsd"})) {
        m.opcode = MOpcode::Store;
        m.mayStore = true;
        for (const auto &operand : m.operands) addRegister(m.uses, operand);
    } else if (isOneOf(op, {"mv", "fmv.s", "fmv.d", "fmv.w.x", "fmv.x.w"})) {
        m.opcode = MOpcode::Move;
        if (!m.operands.empty()) addRegister(m.defs, m.operands[0]);
        for (size_t i = 1; i < m.operands.size(); ++i) addRegister(m.uses, m.operands[i]);
    } else if (isOneOf(op, {"li", "la"})) {
        m.opcode = op == "la" ? MOpcode::Address : MOpcode::Move;
        if (!m.operands.empty()) addRegister(m.defs, m.operands[0]);
    } else {
        m.opcode = op.find("div") != std::string::npos || op.find("rem") != std::string::npos
                       ? MOpcode::Div
                       : (op.find("mul") != std::string::npos ? MOpcode::Mul : MOpcode::Alu);
        if (!m.operands.empty()) addRegister(m.defs, m.operands[0]);
        for (size_t i = 1; i < m.operands.size(); ++i) addRegister(m.uses, m.operands[i]);
        if (m.defs.empty() && m.uses.empty()) {
            m.opcode = MOpcode::Unknown;
            m.isBarrier = true;
        }
    }
    m.defs.erase("zero");

    // 结构化操作数：首操作数按 opcode 决定是否为定义位。
    bool firstDef = firstOperandIsDef(m.opcode);
    for (size_t i = 0; i < m.operands.size(); ++i)
        m.ops.push_back(parseOperand(m.operands[i], i == 0 && firstDef));
    return m;
}

MInst MInst::directive(std::string directive) {
    MInst m;
    m.text = std::move(directive);
    m.opcode = MOpcode::Directive;
    m.isDirective = m.isBarrier = true;
    return m;
}

// ── 发射 API ────────────────────────────────────────────────────────────────

void MFunction::addDirective(std::string text) {
    directives.push_back(MInst::directive(std::move(text)));
}

void MFunction::startBlock(std::string label) {
    MBasicBlock bb;
    bb.label = std::move(label);
    blocks.push_back(std::move(bb));
}

MBasicBlock &MFunction::cur() {
    if (blocks.empty()) blocks.push_back(MBasicBlock{});
    return blocks.back();
}

void MFunction::push(MInst m) { cur().insts.push_back(std::move(m)); }

// ── CFG ──────────────────────────────────────────────────────────────────────

void buildCFG(MFunction &func) {
    std::map<std::string, int> labelToBlock;
    for (int i = 0; i < static_cast<int>(func.blocks.size()); ++i)
        if (func.blocks[i].hasLabel()) labelToBlock[func.blocks[i].label] = i;

    for (auto &bb : func.blocks) {
        bb.succ.clear();
        bb.pred.clear();
    }

    const int n = static_cast<int>(func.blocks.size());
    for (int i = 0; i < n; ++i) {
        MBasicBlock &bb = func.blocks[i];
        auto addSucc = [&](int t) {
            if (t < 0 || t >= n) return;
            if (std::find(bb.succ.begin(), bb.succ.end(), t) == bb.succ.end())
                bb.succ.push_back(t);
        };
        for (const MInst &mi : bb.insts) {
            std::string tgt = labelTarget(mi);
            if (!tgt.empty()) {
                auto it = labelToBlock.find(tgt);
                if (it != labelToBlock.end()) addSucc(it->second);
            }
        }
        bool noFallthrough = !bb.insts.empty() && bb.insts.back().isTerminator;
        if (!noFallthrough) addSucc(i + 1);
    }

    for (int i = 0; i < n; ++i)
        for (int s : func.blocks[i].succ)
            func.blocks[s].pred.push_back(i);
}

// ── 验证 ──────────────────────────────────────────────────────────────────────

bool verifyMFunction(const MFunction &func, std::string &error) {
    std::map<std::string, int> labelToBlock;
    for (int i = 0; i < static_cast<int>(func.blocks.size()); ++i)
        if (func.blocks[i].hasLabel()) labelToBlock[func.blocks[i].label] = i;

    const int n = static_cast<int>(func.blocks.size());
    for (int i = 0; i < n; ++i) {
        const MBasicBlock &bb = func.blocks[i];
        for (const MInst &mi : bb.insts) {
            if (mi.opcode == MOpcode::Unknown && !mi.isBarrier && !mi.isDirective) {
                error = func.name + ": 不完整的指令副作用描述: '" + mi.text + "'";
                return false;
            }
            // defs/uses 必须均为已知寄存器类的物理寄存器（规范 ABI 名）。
            for (const auto *side : {&mi.defs, &mi.uses})
                for (const auto &reg : *side)
                    if (!regClassOf(reg)) {
                        error = func.name + ": 未知寄存器类 '" + reg + "' 于 '" + mi.text + "'";
                        return false;
                    }
            // 寄存器类混用：每个寄存器操作数的类须与该指令期望的类一致。
            for (const MOperand &op : mi.ops) {
                if (!op.isRegLike()) continue;
                auto want = expectedRegClass(mi.mnemonic, op);
                if (want && *want != op.regClass) {
                    error = func.name + ": 寄存器类混用 '" + op.reg + "' 于 '" + mi.text + "'";
                    return false;
                }
            }
            std::string tgt = labelTarget(mi);
            if (!tgt.empty() && !labelToBlock.count(tgt)) {
                error = func.name + ": 未解析的分支目标 '" + tgt + "'";
                return false;
            }
        }
        bool noFallthrough = !bb.insts.empty() && bb.insts.back().isTerminator;
        if (!noFallthrough && i + 1 >= n) {
            error = func.name + ": 末块未以 terminator 结束，将贯穿出函数";
            return false;
        }
    }
    return true;
}

// ── 打印 ──────────────────────────────────────────────────────────────────────

std::string printMFunction(const MFunction &func) {
    std::ostringstream os;
    for (const auto &d : func.directives) os << d.text << "\n";
    for (const auto &bb : func.blocks) {
        if (bb.hasLabel()) os << bb.label << ":\n";
        for (const auto &mi : bb.insts) {
            if (mi.isDirective)
                os << mi.text << "\n";
            else
                os << "\t" << mi.text << "\n";
        }
    }
    return os.str();
}

std::string dumpMFunction(const MFunction &func) {
    std::ostringstream os;
    os << "machine-function " << func.name << "\n";
    for (const auto &d : func.directives) os << "  " << d.text << "\n";
    for (int i = 0; i < static_cast<int>(func.blocks.size()); ++i) {
        const MBasicBlock &bb = func.blocks[i];
        os << "block#" << i;
        if (bb.hasLabel()) os << " " << bb.label;
        os << "  succ={";
        for (size_t k = 0; k < bb.succ.size(); ++k) os << (k ? "," : "") << bb.succ[k];
        os << "} pred={";
        for (size_t k = 0; k < bb.pred.size(); ++k) os << (k ? "," : "") << bb.pred[k];
        os << "}\n";
        for (const auto &mi : bb.insts) {
            os << "    " << mi.text;
            os << "  ; defs={";
            bool first = true;
            for (const auto &reg : mi.defs) { os << (first ? "" : ",") << reg; first = false; }
            os << "} uses={";
            first = true;
            for (const auto &reg : mi.uses) { os << (first ? "" : ",") << reg; first = false; }
            os << "}";
            if (mi.mayLoad) os << " load";
            if (mi.mayStore) os << " store";
            if (mi.isCall) os << " call";
            if (mi.isTerminator) os << " term";
            if (!mi.ops.empty()) {
                os << " ops=[";
                for (size_t k = 0; k < mi.ops.size(); ++k) {
                    const MOperand &op = mi.ops[k];
                    if (k) os << ", ";
                    switch (op.kind) {
                    case MOperand::Kind::Reg:
                        os << (op.regClass == RegClass::FPR ? "fpr:" : "gpr:") << op.reg
                           << (op.isDef ? "(def)" : "");
                        break;
                    case MOperand::Kind::Mem: os << "mem:" << op.reg; break;
                    case MOperand::Kind::Imm: os << "imm"; break;
                    case MOperand::Kind::Label: os << "label"; break;
                    }
                }
                os << "]";
            }
            os << "\n";
        }
    }
    return os.str();
}

}  // namespace riscv
