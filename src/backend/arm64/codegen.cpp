#include "../../include/backend/arm64/codegen.hpp"

#include "../../include/backend/arm64/asm_printer.hpp"
#include "../../include/backend/arm64/isel.hpp"
#include "../../include/backend/arm64/machine_pipeline.hpp"
#include "../../include/backend/arm64/parallelRuntime.hpp"
#include "../../include/backend/arm64/verifier.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace backend::aarch64 {
namespace {

unsigned globalTypeSize(Type *type) {
    return SelectionDAGBuilder::typeSize(type);
}

unsigned globalTypeAlignment(Type *type) {
    unsigned size = globalTypeSize(type);
    if (size >= 16)
        return 16;
    if (size >= 8)
        return 8;
    return size >= 4 ? 4 : 1;
}

unsigned log2Alignment(unsigned alignment) {
    unsigned result = 0;
    while ((1U << result) < alignment)
        ++result;
    return result;
}

} // namespace

void AArch64Backend::generate() {
    if (!module_)
        throw std::invalid_argument("AArch64Backend requires a module");

    SelectionDAGBuilder dagBuilder;
    DAGLegalizer legalizer;
    DAGCombiner combiner;
    AArch64InstructionSelector selector;
    MachineVerifier verifier;
    MachinePipelineServices services;
    AArch64AssemblyPrinter printer;

    MachineFunctionPassManager machinePipeline(
        &verifier, options_.verifyMachineIR);
    machinePipeline.setDump(options_.dumpMachineIR);
    buildMachinePipeline(machinePipeline, services, options_);

    output_ << "\t.arch armv8-a\n\t.text\n";
    for (Function *function : module_->function_list_) {
        if (function->is_declaration())
            continue;
        function->set_instr_name();
        auto dag = dagBuilder.build(function);
        legalizer.run(*dag);
        combiner.run(*dag, options_.optimizationLevel >= 1);
        if (options_.dumpSelectionDAG)
            std::cerr << printSelectionDAG(*dag);

        auto machineFunction = selector.select(*dag);
        if (options_.verifyMachineIR)
            verifier.verifyOrThrow(*machineFunction, "instruction-select");
        machinePipeline.run(*machineFunction);
        if (options_.verifyMachineIR)
            verifier.verifyOrThrow(*machineFunction, "pre-emit");
        printer.printFunction(*machineFunction, output_);
    }

    std::vector<GlobalVariable *> data;
    std::vector<GlobalVariable *> bss;
    std::vector<GlobalVariable *> rodata;
    for (GlobalVariable *global : module_->global_list_) {
        if (global->is_const_)
            rodata.push_back(global);
        else if (global->init_val_ &&
                 !dynamic_cast<ConstantZero *>(global->init_val_))
            data.push_back(global);
        else
            bss.push_back(global);
    }
    auto emitGroup = [&](const char *section,
                         const std::vector<GlobalVariable *> &globals) {
        if (globals.empty())
            return;
        output_ << section << '\n';
        for (GlobalVariable *global : globals)
            emitGlobal(global);
    };
    emitGroup("\t.data", data);
    emitGroup("\t.bss", bss);
    emitGroup("\t.section .rodata", rodata);
    emitParallelRuntime();
}

void AArch64Backend::emitParallelRuntime() {
    auto calledFunction = [](Instruction *instruction) -> Function * {
        auto *call = dynamic_cast<CallInst *>(instruction);
        if (!call || call->num_ops_ == 0)
            return nullptr;
        return dynamic_cast<Function *>(
            call->get_operand(call->num_ops_ - 1));
    };

    bool hasParallelFor = false;
    for (Function *function : module_->function_list_) {
        if (function->is_declaration())
            continue;
        for (BasicBlock *block : function->basic_blocks_)
            for (Instruction *instruction : block->instr_list_) {
                Function *callee = calledFunction(instruction);
                hasParallelFor |=
                    callee && callee->name_ == "__sysy_parallel_for";
            }
    }
    if (!hasParallelFor)
        return;

    std::vector<int> bodyIds;
    const std::string prefix = "__sysy_par_body_";
    for (Function *function : module_->function_list_) {
        if (function->name_.rfind(prefix, 0) != 0)
            continue;
        const std::string suffix = function->name_.substr(prefix.size());
        if (suffix.empty() ||
            !std::all_of(suffix.begin(), suffix.end(), [](char character) {
                return std::isdigit(
                    static_cast<unsigned char>(character));
            }))
            continue;
        bodyIds.push_back(std::stoi(suffix));
    }
    std::sort(bodyIds.begin(), bodyIds.end());
    bodyIds.erase(std::unique(bodyIds.begin(), bodyIds.end()), bodyIds.end());

    output_ << "\n\t.text\n\t.align 2\n"
            << "\t.global __sysy_par_dispatch\n"
            << "__sysy_par_dispatch:\n";
    for (int id : bodyIds)
        output_ << "\tcmp w0, #" << id << "\n"
                << "\tb.eq .Lsysy_disp_" << id << "\n";
    output_ << "\tret\n";
    for (int id : bodyIds)
        output_ << ".Lsysy_disp_" << id << ":\n"
                << "\tmov w0, w1\n"
                << "\tmov w1, w2\n"
                << "\tb __sysy_par_body_" << id << "\n";
    output_ << kSysyParallelRuntimeAsm;
}

void AArch64Backend::emitGlobal(GlobalVariable *global) {
    auto *pointerType = dynamic_cast<PointerType *>(global->type_);
    if (!pointerType)
        throw std::logic_error("global value is not a pointer");
    Type *valueType = pointerType->contained_;
    output_ << "\t.global " << global->name_ << '\n'
            << "\t.p2align "
            << log2Alignment(globalTypeAlignment(valueType)) << '\n'
            << global->name_ << ":\n";

    auto emitFloat = [&](float value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        output_ << "\t.word 0x" << std::hex << bits << std::dec << '\n';
    };
    std::function<bool(Constant *)> isAllZero =
        [&](Constant *constant) {
            if (dynamic_cast<ConstantZero *>(constant))
                return true;
            if (auto *integer =
                    dynamic_cast<ConstantInt *>(constant))
                return integer->value_ == 0;
            if (auto *floating =
                    dynamic_cast<ConstantFloat *>(constant)) {
                std::uint32_t bits = 0;
                float value = floating->value_;
                std::memcpy(&bits, &value, sizeof(bits));
                return bits == 0;
            }
            if (auto *array =
                    dynamic_cast<ConstantArray *>(constant))
                return std::all_of(
                    array->const_array.begin(),
                    array->const_array.end(),
                    [&](Constant *element) {
                        return isAllZero(element);
                    });
            if (auto *vector =
                    dynamic_cast<ConstantVector *>(constant))
                return std::all_of(
                    vector->elements_.begin(), vector->elements_.end(),
                    [&](Constant *element) {
                        return isAllZero(element);
                    });
            return false;
        };
    std::function<void(Constant *, Type *)> emitConstant =
        [&](Constant *constant, Type *type) {
            if (isAllZero(constant)) {
                output_ << "\t.zero " << globalTypeSize(type) << '\n';
            } else if (auto *integer =
                           dynamic_cast<ConstantInt *>(constant)) {
                output_ << "\t.word " << integer->value_ << '\n';
            } else if (auto *floating =
                           dynamic_cast<ConstantFloat *>(constant)) {
                emitFloat(floating->value_);
            } else if (auto *array =
                           dynamic_cast<ConstantArray *>(constant)) {
                auto *arrayType = dynamic_cast<ArrayType *>(type);
                if (!arrayType)
                    throw std::logic_error(
                        "constant array has non-array type");
                for (Constant *element : array->const_array)
                    emitConstant(element, arrayType->contained_);
            } else if (auto *vector =
                           dynamic_cast<ConstantVector *>(constant)) {
                auto *vectorType = dynamic_cast<VectorType *>(type);
                if (!vectorType || vector->elements_.size() !=
                                       vectorType->num_elements_)
                    throw std::logic_error(
                        "constant vector has incompatible vector type");
                for (Constant *element : vector->elements_)
                    emitConstant(element, vectorType->contained_);
            } else if (dynamic_cast<ConstantZero *>(constant)) {
                output_ << "\t.zero " << globalTypeSize(type) << '\n';
            } else {
                throw std::logic_error(
                    "unsupported SysY global initializer");
            }
        };
    if (global->init_val_)
        emitConstant(global->init_val_, valueType);
    else
        output_ << "\t.zero " << globalTypeSize(valueType) << '\n';
}

} // namespace backend::aarch64
