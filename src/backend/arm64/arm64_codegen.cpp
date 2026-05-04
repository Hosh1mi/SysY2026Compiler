#include "../../include/backend/arm64/arm64_codegen.hpp"
#include "../../include/backend/arm64/arm64_context.hpp"
#include "../../include/mid/ir/ir.hpp"
#include <iostream>

void Arm64CodeGen::generate() {
    os_ << "\t.text\n\n";

    for (auto gv : m_->global_list_) {
        emitGlobal(gv);
    }

    for (auto f : m_->function_list_) {
        if (f->is_declaration()) {
            emitExtern(f);
        }
    }

    os_ << "\n\t.text\n\n";
    for (auto f : m_->function_list_) {
        if (!f->is_declaration()) {
            emitFunction(f);
        }
    }
}

void Arm64CodeGen::emitGlobal(GlobalVariable *gv) {
    auto pointee = static_cast<PointerType*>(gv->type_)->contained_;

    if (gv->is_const_) {
        os_ << "\t.section .rodata\n";
    } else if (gv->init_val_ && !dynamic_cast<ConstantZero*>(gv->init_val_)) {
        os_ << "\t.data\n";
    } else {
        os_ << "\t.bss\n";
    }

    os_ << "\t.global " << gv->name_ << "\n";
    os_ << "\t.p2align 2\n";
    os_ << gv->name_ << ":\n";

    if (auto cz = dynamic_cast<ConstantZero*>(gv->init_val_)) {
        int size = 4; // default i32/float
        auto ty = pointee;
        if (ty->tid_ == Type::ArrayTyID) {
            auto at = static_cast<ArrayType*>(ty);
            auto elem = at->contained_;
            while (elem->tid_ == Type::ArrayTyID) {
                elem = static_cast<ArrayType*>(elem)->contained_;
            }
            int elemSize = (elem->tid_ == Type::FloatTyID ||
                           (elem->tid_ == Type::IntegerTyID && static_cast<IntegerType*>(elem)->num_bits_ == 32)) ? 4 : 4;
            int total = at->num_elements_;
            while (ty->tid_ == Type::ArrayTyID) {
                auto arr = static_cast<ArrayType*>(ty);
                total *= arr->num_elements_;
                ty = arr->contained_;
            }
            size = total * elemSize;
        }
        os_ << "\t.zero " << size << "\n";
    } else if (auto ci = dynamic_cast<ConstantInt*>(gv->init_val_)) {
        os_ << "\t.word " << ci->value_ << "\n";
    } else if (auto cf = dynamic_cast<ConstantFloat*>(gv->init_val_)) {
        int bits;
        float val = cf->value_;
        os_ << "\t.word 0x" << std::hex << *reinterpret_cast<int*>(&val) << std::dec << "\n";
    } else if (auto ca = dynamic_cast<ConstantArray*>(gv->init_val_)) {
        for (auto elem : ca->const_array) {
            if (auto eci = dynamic_cast<ConstantInt*>(elem)) {
                os_ << "\t.word " << eci->value_ << "\n";
            } else if (auto ecf = dynamic_cast<ConstantFloat*>(elem)) {
                float val = ecf->value_;
                os_ << "\t.word 0x" << std::hex << *reinterpret_cast<int*>(&val) << std::dec << "\n";
            } else if (auto eca = dynamic_cast<ConstantArray*>(elem)) {
                for (auto sub : eca->const_array) {
                    if (auto sci = dynamic_cast<ConstantInt*>(sub)) {
                        os_ << "\t.word " << sci->value_ << "\n";
                    } else if (auto scf = dynamic_cast<ConstantFloat*>(sub)) {
                        float val = scf->value_;
                        os_ << "\t.word 0x" << std::hex << *reinterpret_cast<int*>(&val) << std::dec << "\n";
                    }
                }
            } else if (dynamic_cast<ConstantZero*>(elem)) {
                os_ << "\t.word 0\n";
            }
        }
    }
    os_ << "\n";
}

void Arm64CodeGen::emitExtern(Function *f) {
    // __aeabi_* 函数会作为普通外部函数处理
    (void)f;
}

void Arm64CodeGen::emitFunction(Function *f) {
    Arm64FuncContext ctx(f, *this, os_);
    ctx.generate();
}
