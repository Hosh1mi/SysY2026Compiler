#include "../../include/backend/arm_builder.hpp"

std::string ArmBuilder::buildArm(Module *module) {
    std::ostringstream out;
    out << emitModule(module);
    out << emitRuntimeHelpers();
    out << emitData(module);
    return out.str();
}

std::string ArmBuilder::emitModule(Module *module) {
    std::ostringstream out;
    out << ".arch armv8-a\n.text\n";
    if (!module) return out.str();
    for (auto *f : module->function_list_) out << emitFunction(f);
    return out.str();
}

std::string ArmBuilder::emitData(Module *module) {
    std::ostringstream out;
    if (!module) return out.str();
    out << ".section .rodata\n";
    for (auto *g : module->global_list_) {
        if (g->is_const_) continue;
        out << ".data\n.align 3\n" << globalName(g) << ":\n";
        auto *init = g->init_val_;
        auto *pt = static_cast<PointerType *>(g->type_);
        auto *cty = pt->contained_;
        if (auto *ci = dynamic_cast<ConstantInt *>(init)) out << "    .word " << ci->value_ << "\n";
        else if (auto *cf = dynamic_cast<ConstantFloat *>(init)) out << "    .word " << floatBits(cf->value_) << "\n";
        else if (dynamic_cast<ConstantZero *>(init)) out << "    .zero " << typeSize(cty) << "\n";
        else if (auto *ca = dynamic_cast<ConstantArray *>(init)) {
            for (auto *elem : ca->const_array) {
                if (auto *eci = dynamic_cast<ConstantInt *>(elem)) out << "    .word " << eci->value_ << "\n";
                else if (auto *ecf = dynamic_cast<ConstantFloat *>(elem)) out << "    .word " << floatBits(ecf->value_) << "\n";
            }
        } else out << "    .zero " << typeSize(cty) << "\n";
    }
    return out.str();
}

std::string ArmBuilder::emitRuntimeHelpers() {
    return std::string();
}
