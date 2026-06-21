#include "../../include/backend/riscv/codegen.hpp"
#include "../../include/backend/riscv/context.hpp"
#include "../../include/backend/riscv/machine.hpp"
#include "../../include/backend/riscv/machineDCE.hpp"
#include "../../include/backend/riscv/peephole.hpp"
#include "../../include/mid/ir/ir.hpp"

#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// 进入代码生成前，按 terminator 重建 CFG 前驱/后继链。中端个别 pass 改写分支
// 目标时可能遗漏维护 pre_bbs_/succ_bbs_，而后端的 phi/活跃性分析依赖它们。
static void rebuildCfgLinks(Module *m) {
    for (auto *f : m->function_list_) {
        if (f->is_declaration()) continue;
        for (auto *bb : f->basic_blocks_) {
            bb->pre_bbs_.clear();
            bb->succ_bbs_.clear();
        }
        for (auto *bb : f->basic_blocks_) {
            auto *term = bb->get_terminator();
            if (!term) continue;
            for (unsigned i = 0; i < term->num_ops_; ++i) {
                if (auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i))) {
                    bb->add_succ_basic_block(succ);
                    succ->add_pre_basic_block(bb);
                }
            }
        }
    }
}

// 全局聚合类型的字节大小。
static int globalTypeSize(Type *ty) {
    if (!ty) return 4;
    switch (ty->tid_) {
    case Type::IntegerTyID: {
        int bits = static_cast<IntegerType *>(ty)->num_bits_;
        return bits <= 1 ? 1 : bits / 8;
    }
    case Type::FloatTyID:
        return 4;
    case Type::PointerTyID:
        return 8;
    case Type::ArrayTyID: {
        auto *arr = static_cast<ArrayType *>(ty);
        return arr->num_elements_ * globalTypeSize(arr->contained_);
    }
    default:
        return 4;
    }
}

void RiscvCodeGen::generate() {
    rebuildCfgLinks(m_);

    // 1. 全局变量分类：rodata / data / bss
    std::vector<GlobalVariable *> rodata, data, bss;
    for (auto *gv : m_->global_list_) {
        if (gv->is_const_)
            rodata.push_back(gv);
        else if (gv->init_val_ && !dynamic_cast<ConstantZero *>(gv->init_val_))
            data.push_back(gv);
        else
            bss.push_back(gv);
    }

    // 2. 函数代码（朴素路径，逐个生成）
    std::vector<Function *> funcs;
    for (auto *f : m_->function_list_)
        if (!f->is_declaration()) funcs.push_back(f);

    for (auto *f : funcs)
        f->set_instr_name();

    std::ostringstream body;
    if (!funcs.empty()) {
        body << "\t.text\n";
        for (auto *f : funcs) {
            riscv::MFunction mfunc;
            mfunc.name = f->name_;
            RiscvFuncContext ctx(f, mfunc, enable_regalloc_);
            ctx.generate();
            if (dump_pre_machine_instr_)
                std::cerr << riscv::dumpMFunction(mfunc);
            if (!no_peephole_) {
                bool changed = true;
                for (int round = 0; changed && round < 8; ++round) {
                    changed = false;
                    changed |= riscv::forwardAdjacentStoreLoads(mfunc);
                    changed |= riscv::propagateCopies(mfunc);
                    changed |= riscv::removeSelfMoves(mfunc);
                    changed |= riscv::eliminateDeadMachineInstructions(mfunc);
                }
            }
            if (dump_machine_instr_)
                std::cerr << riscv::dumpMFunction(mfunc);
            body << riscv::printMFunction(mfunc);
        }
    }
    os_ << body.str();

    // 3. 数据段：.data → .bss → .rodata
    auto emitGroup = [&](const char *sec, const std::vector<GlobalVariable *> &gvs) {
        if (gvs.empty()) return;
        os_ << sec << "\n";
        for (auto *gv : gvs)
            emitGlobal(gv);
    };
    emitGroup("\t.data", data);
    emitGroup("\t.bss", bss);
    emitGroup("\t.section .rodata", rodata);
}

static int p2AlignForBytes(int align) {
    if (align >= 16) return 4;
    if (align >= 8) return 3;
    return 2;
}

static int globalTypeAlign(Type *ty) {
    if (!ty) return 4;
    if (ty->tid_ == Type::ArrayTyID)
        return globalTypeAlign(static_cast<ArrayType *>(ty)->contained_);
    if (ty->tid_ == Type::PointerTyID)
        return 8;
    return 4;
}

void RiscvCodeGen::emitGlobal(GlobalVariable *gv) {
    auto *pointee = static_cast<PointerType *>(gv->type_)->contained_;

    os_ << "\t.globl " << gv->name_ << "\n";
    os_ << "\t.p2align " << p2AlignForBytes(globalTypeAlign(pointee)) << "\n";
    os_ << gv->name_ << ":\n";

    auto emitWordHex = [&](int bits) {
        std::ostringstream line;
        line << "\t.word 0x" << std::hex << bits << "\n";
        os_ << line.str();
    };

    if (dynamic_cast<ConstantZero *>(gv->init_val_)) {
        os_ << "\t.zero " << globalTypeSize(pointee) << "\n";
    } else if (auto *ci = dynamic_cast<ConstantInt *>(gv->init_val_)) {
        os_ << "\t.word " << ci->value_ << "\n";
    } else if (auto *cf = dynamic_cast<ConstantFloat *>(gv->init_val_)) {
        float val = cf->value_;
        int bits;
        std::memcpy(&bits, &val, sizeof(bits));
        emitWordHex(bits);
    } else if (auto *ca = dynamic_cast<ConstantArray *>(gv->init_val_)) {
        std::function<bool(Constant *)> allZero = [&](Constant *elem) -> bool {
            if (auto *eci = dynamic_cast<ConstantInt *>(elem)) return eci->value_ == 0;
            if (auto *ecf = dynamic_cast<ConstantFloat *>(elem)) return ecf->value_ == 0.0f;
            if (auto *eca = dynamic_cast<ConstantArray *>(elem)) {
                for (auto *sub : eca->const_array)
                    if (!allZero(sub)) return false;
                return true;
            }
            return dynamic_cast<ConstantZero *>(elem) != nullptr;
        };
        bool isAllZero = true;
        for (auto *elem : ca->const_array)
            if (!allZero(elem)) { isAllZero = false; break; }

        if (isAllZero) {
            os_ << "\t.zero " << globalTypeSize(pointee) << "\n";
        } else {
            std::function<void(Constant *)> emitElem = [&](Constant *elem) {
                if (auto *eci = dynamic_cast<ConstantInt *>(elem)) {
                    os_ << "\t.word " << eci->value_ << "\n";
                } else if (auto *ecf = dynamic_cast<ConstantFloat *>(elem)) {
                    float val = ecf->value_;
                    int bits;
                    std::memcpy(&bits, &val, sizeof(bits));
                    emitWordHex(bits);
                } else if (auto *eca = dynamic_cast<ConstantArray *>(elem)) {
                    for (auto *sub : eca->const_array) emitElem(sub);
                } else if (dynamic_cast<ConstantZero *>(elem)) {
                    os_ << "\t.word 0\n";
                }
            };
            for (auto *elem : ca->const_array) emitElem(elem);
        }
    }
    os_ << "\n";
}
