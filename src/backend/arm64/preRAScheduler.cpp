#include "../../include/backend/arm64/preRAScheduler.hpp"

#include "../../include/backend/arm64/scheduler.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

namespace {

bool isVoidInst(Instruction *inst) {
    return inst->is_void() || inst->is_store() || inst->is_br() || inst->is_ret();
}

MachineRegClass regClassForType(Type *ty) {
    if (!ty)
        return MachineRegClass::None;
    switch (ty->tid_) {
        case Type::IntegerTyID: {
            auto *it = static_cast<IntegerType *>(ty);
            return it->num_bits_ <= 32 ? MachineRegClass::GPR32
                                       : MachineRegClass::GPR64;
        }
        case Type::FloatTyID:
            return MachineRegClass::FPR32;
        case Type::PointerTyID:
        case Type::ArrayTyID:
            return MachineRegClass::GPR64;
        case Type::VectorTyID:
            return MachineRegClass::NEON128;
        default:
            return MachineRegClass::None;
    }
}

std::string classSuffix(MachineRegClass rc) {
    switch (rc) {
        case MachineRegClass::GPR32:   return "g32";
        case MachineRegClass::GPR64:   return "g64";
        case MachineRegClass::FPR32:   return "f32";
        case MachineRegClass::FPR64:   return "f64";
        case MachineRegClass::NEON128: return "v128";
        default:                       return "none";
    }
}

bool isMovableBoundary(Instruction *inst) {
    return inst->is_phi() || inst->is_alloca() || inst->is_call() ||
           inst->is_br() || inst->is_ret() || inst->is_cmp() ||
           inst->is_fcmp() || inst->op_id_ == Instruction::Select;
}

int latencyFor(Instruction *inst) {
    switch (inst->op_id_) {
        case Instruction::Load:
            return 4;
        case Instruction::Mul:
            return 3;
        case Instruction::FMul:
            return 5;
        case Instruction::SDiv:
        case Instruction::UDiv:
        case Instruction::SRem:
        case Instruction::URem:
        case Instruction::FDiv:
            return 12;
        case Instruction::FAdd:
        case Instruction::FSub:
        case Instruction::FNeg:
        case Instruction::FPtoSI:
        case Instruction::SItoFP:
            return 4;
        default:
            return 1;
    }
}

MOpcode opcodeFor(Instruction *inst) {
    switch (inst->op_id_) {
        case Instruction::Load:
            return MOpcode::Load;
        case Instruction::Store:
            return MOpcode::Store;
        case Instruction::Mul:
        case Instruction::FMul:
            return MOpcode::Mul;
        case Instruction::SDiv:
        case Instruction::UDiv:
        case Instruction::SRem:
        case Instruction::URem:
        case Instruction::FDiv:
            return MOpcode::Div;
        case Instruction::InsertElement:
            return MOpcode::Neon;
        default:
            return MOpcode::Alu;
    }
}

bool supportedMovable(Instruction *inst) {
    if (!inst || isMovableBoundary(inst))
        return false;
    switch (inst->op_id_) {
        case Instruction::Add:
        case Instruction::Sub:
        case Instruction::Mul:
        case Instruction::SDiv:
        case Instruction::SRem:
        case Instruction::UDiv:
        case Instruction::URem:
        case Instruction::FNeg:
        case Instruction::FAdd:
        case Instruction::FSub:
        case Instruction::FMul:
        case Instruction::FDiv:
        case Instruction::Shl:
        case Instruction::LShr:
        case Instruction::AShr:
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Xor:
        case Instruction::Load:
        case Instruction::Store:
        case Instruction::GetElementPtr:
        case Instruction::ZExt:
        case Instruction::FPtoSI:
        case Instruction::SItoFP:
        case Instruction::BitCast:
        case Instruction::Clz:
        case Instruction::InsertElement:
            return true;
        default:
            return false;
    }
}

std::optional<MachineInstr>
lowerToPreRA(Instruction *inst,
             const std::map<Value *, PreRAScheduler::VRegInfo> &vregs,
             int originalIndex) {
    if (!supportedMovable(inst))
        return std::nullopt;

    MachineInstr mi;
    mi.text = "\tpreRA." + inst->name_;
    mi.opcodeText = "preRA";
    mi.opcode = opcodeFor(inst);
    mi.latency = latencyFor(inst);
    mi.originalIndex = originalIndex;
    mi.mayLoad = inst->is_load();
    mi.mayStore = inst->is_store();

    if (!isVoidInst(inst)) {
        auto it = vregs.find(inst);
        if (it == vregs.end())
            return std::nullopt;
        mi.defs.insert(it->second.name);
        mi.operands.push_back(MachineOperand::vreg(
            it->second.name, it->second.regClass, true));
    }

    for (unsigned i = 0; i < inst->num_ops_; ++i) {
        Value *op = inst->get_operand(i);
        auto it = vregs.find(op);
        if (it == vregs.end())
            continue;
        mi.uses.insert(it->second.name);
        mi.operands.push_back(MachineOperand::vreg(
            it->second.name, it->second.regClass, false));
    }

    return mi;
}

int classIndex(MachineRegClass rc) {
    switch (rc) {
        case MachineRegClass::GPR32:
        case MachineRegClass::GPR64:
            return 0;
        case MachineRegClass::FPR32:
        case MachineRegClass::FPR64:
            return 1;
        case MachineRegClass::NEON128:
            return 2;
        default:
            return 3;
    }
}

std::vector<int> pressureForOrder(
    const std::vector<int> &order,
    const std::vector<MachineInstr> &instrs,
    const std::map<std::string, MachineRegClass> &classes) {
    std::set<std::string> live;
    std::vector<int> maxPressure(4, 0);

    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const auto &mi = instrs[*it];
        for (const auto &def : mi.defs)
            live.erase(def);
        for (const auto &use : mi.uses) {
            if (!use.empty() && use[0] == '%')
                live.insert(use);
        }

        std::vector<int> cur(4, 0);
        for (const auto &v : live) {
            auto clsIt = classes.find(v);
            if (clsIt == classes.end())
                continue;
            cur[classIndex(clsIt->second)]++;
        }
        for (size_t i = 0; i < maxPressure.size(); ++i)
            maxPressure[i] = std::max(maxPressure[i], cur[i]);
    }
    return maxPressure;
}

bool pressureAcceptable(const std::vector<int> &original,
                        const std::vector<int> &scheduled) {
    for (size_t i = 0; i < original.size() && i < scheduled.size(); ++i) {
        if (scheduled[i] > original[i])
            return false;
    }
    return true;
}

} // namespace

PreRAScheduler::PreRAScheduler(bool dump, std::ostream *dumpOut)
    : dump_(dump), dumpOut_(dumpOut) {
    if (dump_ && !dumpOut_)
        dumpOut_ = &std::cerr;
}

bool PreRAScheduler::run(Module *module) {
    if (!module)
        return false;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func || func->is_declaration())
            continue;
        changed |= scheduleFunction(func);
    }
    return changed;
}

bool PreRAScheduler::scheduleFunction(Function *func) {
    std::map<Value *, VRegInfo> vregs;
    int nextVReg = 0;

    auto getVReg = [&](Value *v) -> VRegInfo {
        auto it = vregs.find(v);
        if (it != vregs.end())
            return it->second;
        MachineRegClass rc = regClassForType(v->type_);
        VRegInfo info{"%v" + std::to_string(nextVReg++) + "." + classSuffix(rc), rc};
        vregs[v] = info;
        return info;
    };

    for (auto *arg : func->arguments_)
        getVReg(arg);
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (!isVoidInst(inst) && regClassForType(inst->type_) != MachineRegClass::None)
                getVReg(inst);
        }
    }

    bool changed = false;
    MachineFunction dumpFunc;
    dumpFunc.name = func->name_ + ".preRA";
    for (auto *bb : func->basic_blocks_) {
        changed |= scheduleBlock(bb, vregs, nextVReg);
        if (dump_) {
            MachineBasicBlock dumpBlock;
            dumpBlock.label = bb->name_;
            int idx = 0;
            for (auto *inst : bb->instr_list_) {
                auto mi = lowerToPreRA(inst, vregs, idx++);
                if (mi)
                    dumpBlock.instrs.push_back(*mi);
            }
            dumpFunc.blocks.push_back(std::move(dumpBlock));
        }
    }
    if (dump_)
        dumpMachineFunction(dumpFunc);
    return changed;
}

bool PreRAScheduler::scheduleBlock(BasicBlock *bb, std::map<Value *, VRegInfo> &vregs,
                                   int &nextVReg) {
    (void)nextVReg;
    if (!bb || bb->instr_list_.empty())
        return false;

    std::vector<Instruction *> original(bb->instr_list_.begin(), bb->instr_list_.end());
    std::vector<Instruction *> reordered;
    bool changed = false;

    auto flushSegment = [&](std::vector<Instruction *> &segment) {
        if (segment.empty())
            return;
        if (segment.size() <= 1) {
            reordered.insert(reordered.end(), segment.begin(), segment.end());
            segment.clear();
            return;
        }

        std::vector<MachineInstr> instrs;
        std::map<std::string, MachineRegClass> classes;
        bool hasStore = false;
        bool hasLongLatency = false;
        instrs.reserve(segment.size());
        for (size_t i = 0; i < segment.size(); ++i) {
            auto mi = lowerToPreRA(segment[i], vregs, static_cast<int>(i));
            if (!mi) {
                reordered.insert(reordered.end(), segment.begin(), segment.end());
                segment.clear();
                return;
            }
            for (const auto &op : mi->operands) {
                if (op.kind == MachineOperand::Kind::VReg)
                    classes[op.text] = op.regClass;
            }
            hasStore = hasStore || mi->mayStore;
            hasLongLatency = hasLongLatency || mi->latency > 1;
            instrs.push_back(*mi);
        }

        if (hasStore || !hasLongLatency) {
            reordered.insert(reordered.end(), segment.begin(), segment.end());
            segment.clear();
            return;
        }

        MachineFunction mf;
        mf.name = "preRA.segment";
        mf.blocks.push_back({});
        mf.blocks.back().instrs = instrs;
        MachineScheduler scheduler;
        scheduler.schedule(mf);

        if (mf.blocks.empty() || mf.blocks.back().instrs.size() != instrs.size()) {
            reordered.insert(reordered.end(), segment.begin(), segment.end());
            segment.clear();
            return;
        }

        std::vector<int> origOrder;
        std::vector<int> schedOrder;
        for (size_t i = 0; i < instrs.size(); ++i)
            origOrder.push_back(static_cast<int>(i));
        for (const auto &mi : mf.blocks.back().instrs)
            schedOrder.push_back(mi.originalIndex);

        auto origPressure = pressureForOrder(origOrder, instrs, classes);
        auto schedPressure = pressureForOrder(schedOrder, instrs, classes);
        if (!pressureAcceptable(origPressure, schedPressure)) {
            reordered.insert(reordered.end(), segment.begin(), segment.end());
            segment.clear();
            return;
        }

        for (int idx : schedOrder) {
            if (idx < 0 || idx >= static_cast<int>(segment.size())) {
                reordered.insert(reordered.end(), segment.begin(), segment.end());
                segment.clear();
                return;
            }
            reordered.push_back(segment[idx]);
        }
        if (schedOrder != origOrder)
            changed = true;
        segment.clear();
    };

    std::vector<Instruction *> segment;
    for (auto *inst : original) {
        if (supportedMovable(inst)) {
            segment.push_back(inst);
        } else {
            flushSegment(segment);
            reordered.push_back(inst);
        }
    }
    flushSegment(segment);

    if (!changed)
        return false;

    bb->instr_list_.clear();
    for (auto *inst : reordered) {
        inst->pos_in_bb.clear();
        bb->add_instruction(inst);
    }
    return true;
}

void PreRAScheduler::dumpMachineFunction(const MachineFunction &func) const {
    if (!dumpOut_)
        return;
    *dumpOut_ << "=== PreRA MachineFunction: " << func.name << " ===\n";
    for (const auto &block : func.blocks) {
        *dumpOut_ << "  BB [" << block.label << "] (" << block.instrs.size()
                  << " instrs)\n";
        for (const auto &inst : block.instrs) {
            *dumpOut_ << "    [" << inst.originalIndex << "] "
                      << inst.text << " defs={";
            bool first = true;
            for (const auto &def : inst.defs) {
                if (!first) *dumpOut_ << ", ";
                *dumpOut_ << def;
                first = false;
            }
            *dumpOut_ << "} uses={";
            first = true;
            for (const auto &use : inst.uses) {
                if (!first) *dumpOut_ << ", ";
                *dumpOut_ << use;
                first = false;
            }
            *dumpOut_ << "} lat=" << inst.latency;
            if (inst.mayLoad) *dumpOut_ << " LOAD";
            if (inst.mayStore) *dumpOut_ << " STORE";
            *dumpOut_ << "\n";
        }
    }
}
