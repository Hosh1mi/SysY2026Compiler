#include "../../../include/backend/arm64/rewrite/codegen.hpp"

#include "../../../include/backend/arm64/rewrite/asm_printer.hpp"
#include "../../../include/backend/arm64/rewrite/frame_lowering.hpp"
#include "../../../include/backend/arm64/rewrite/isel.hpp"
#include "../../../include/backend/arm64/rewrite/machine_passes.hpp"
#include "../../../include/backend/arm64/rewrite/regalloc.hpp"
#include "../../../include/backend/arm64/rewrite/scheduler.hpp"
#include "../../../include/backend/arm64/rewrite/verifier.hpp"

#include <algorithm>
#include <cstdlib>
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
    PreRAMachinePeephole preRAPeephole;
    DeadMachineInstructionElimination machineDCE;
    MachineLICM machineLICM;
    AArch64ConditionOptimizer conditionOptimizer;
    PhiElimination phiElimination;
    PreRACFGOptimizer preRACFGOptimizer;
    A53MachineScheduler scheduler;
    GraphColoringRegisterAllocator registerAllocator;
    PostRAParallelCopyResolver parallelCopyResolver;
    PostRAInstructionExpansion instructionExpansion;
    PostRACopyPropagation copyPropagation;
    PreRAAddressingFolder addressingFolder;
    PostRAAddressingOptimizer addressingOptimizer;
    MachineBlockPlacement blockPlacement;
    AArch64FrameLowering frameLowering;
    AArch64AssemblyPrinter printer;

    output_ << "\t.arch armv8-a\n\t.text\n";
    const bool tracePhases =
        std::getenv("DEBUG_AARCH64_REWRITE_PHASES") != nullptr;
    const bool verifyEachMachinePass =
        std::getenv("DEBUG_AARCH64_REWRITE_VERIFY_EACH_PASS") != nullptr;
    auto trace = [&](const Function *function, const char *phase) {
        if (tracePhases)
            std::cerr << "[aarch64-rewrite] " << function->name_
                      << ": " << phase << '\n';
    };
    for (Function *function : module_->function_list_) {
        if (function->is_declaration())
            continue;
        function->set_instr_name();
        trace(function, "dag");
        auto dag = dagBuilder.build(function);
        legalizer.run(*dag);
        combiner.run(*dag, options_.optimizationLevel >= 1);
        if (options_.dumpSelectionDAG)
            std::cerr << printSelectionDAG(*dag);

        trace(function, "isel");
        auto machineFunction = selector.select(*dag);
        if (options_.verifyMachineIR)
            verifier.verifyOrThrow(*machineFunction, "instruction-select");
        if (std::getenv("DEBUG_AARCH64_REWRITE_BEFORE_MACHINE_SSA"))
            std::cerr << printMachineIR(*machineFunction);
        if (options_.optimizationLevel >= 1) {
            if (!options_.disablePeephole) {
                preRAPeephole.run(*machineFunction);
                addressingFolder.run(*machineFunction);
                if (options_.verifyMachineIR && verifyEachMachinePass)
                    verifier.verifyOrThrow(*machineFunction,
                                           "pre-ra-peephole");
            }
            conditionOptimizer.run(*machineFunction);
            if (std::getenv("DEBUG_AARCH64_REWRITE_AFTER_CONDITION"))
                std::cerr << printMachineIR(*machineFunction);
            if (options_.verifyMachineIR && verifyEachMachinePass)
                verifier.verifyOrThrow(*machineFunction,
                                       "condition-optimization");
            for (unsigned iteration = 0; iteration < 4; ++iteration) {
                if (!machineDCE.run(*machineFunction))
                    break;
                if (options_.verifyMachineIR && verifyEachMachinePass)
                    verifier.verifyOrThrow(*machineFunction,
                                           "machine-dce");
            }
            if (options_.verifyMachineIR)
                verifier.verifyOrThrow(
                    *machineFunction, "machine-ssa-optimization");
        }
        trace(function, "phi-elimination");
        phiElimination.run(*machineFunction);
        if (options_.optimizationLevel >= 1 &&
            !options_.disablePeephole) {
            preRAPeephole.run(*machineFunction);
            // Critical-edge splitting during PHI elimination creates the
            // canonical preheaders needed by constant-only Machine LICM.
            machineLICM.run(*machineFunction);
            preRAPeephole.run(*machineFunction);
            preRACFGOptimizer.run(*machineFunction);
        }
        if (options_.verifyMachineIR)
            verifier.verifyOrThrow(*machineFunction, "phi-elimination");
        if (std::getenv("DEBUG_AARCH64_REWRITE_AFTER_PHI"))
            std::cerr << printMachineIR(*machineFunction);
        if (options_.optimizationLevel >= 1 &&
            !options_.disablePreSchedule)
            scheduler.run(*machineFunction);
        trace(function, "regalloc");
        registerAllocator.run(*machineFunction);
        trace(function, "parallel-copies");
        parallelCopyResolver.run(*machineFunction);
        instructionExpansion.run(*machineFunction);
        if (options_.optimizationLevel >= 1 &&
            !options_.disablePeephole)
            copyPropagation.run(*machineFunction);
        if (options_.verifyMachineIR)
            verifier.verifyOrThrow(*machineFunction, "register-allocation");
        trace(function, "frame-lowering");
        frameLowering.run(*machineFunction);
        if (options_.optimizationLevel >= 1) {
            if (!options_.disablePeephole)
                copyPropagation.run(*machineFunction);
            if (!options_.disablePeephole)
                addressingOptimizer.run(*machineFunction);
            blockPlacement.run(*machineFunction);
        }
        // Expand integer immediates into MOVZ/MOVK before scheduling and
        // printing so the scheduler can interleave independent pieces.
        instructionExpansion.expandConstantMaterializations(
            *machineFunction);
        if (options_.optimizationLevel >= 1 &&
            !options_.disableSchedule)
            scheduler.run(*machineFunction);
        if (options_.verifyMachineIR)
            verifier.verifyOrThrow(*machineFunction, "frame-lowering");
        if (options_.dumpMachineIR)
            std::cerr << printMachineIR(*machineFunction);
        trace(function, "assembly");
        printer.printFunction(*machineFunction, output_);
        trace(function, "done");
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
