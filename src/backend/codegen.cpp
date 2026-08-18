#include "../include/backend/codegen.hpp"

#include "../include/backend/asm_printer.hpp"
#include "../include/backend/isel.hpp"
#include "../include/backend/machine_pipeline.hpp"
#include "../include/backend/parallelRuntime.hpp"
#include "../include/backend/verifier.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>

namespace backend::aarch64 {
namespace {

bool isAllZeroConstant(Constant *constant) {
	if (dynamic_cast<ConstantZero *>(constant))
		return true;
	if (auto *integer = dynamic_cast<ConstantInt *>(constant))
		return integer->value_ == 0;
	if (auto *floating = dynamic_cast<ConstantFloat *>(constant)) {
		std::uint32_t bits = 0;
		float value = floating->value_;
		std::memcpy(&bits, &value, sizeof(bits));
		return bits == 0;
	}
	if (auto *array = dynamic_cast<ConstantArray *>(constant)) {
		for (Constant *element : array->const_array)
			if (!isAllZeroConstant(element))
				return false;
		return true;
	}
	if (auto *vector = dynamic_cast<ConstantVector *>(constant)) {
		for (Constant *element : vector->elements_)
			if (!isAllZeroConstant(element))
				return false;
		return true;
	}
	return false;
}

void emitConstantValue(std::ostream &output, Constant *constant, Type *type) {
	if (isAllZeroConstant(constant)) {
		output << "\t.zero " << SelectionDAGBuilder::typeSize(type) << '\n';
		return;
	}
	if (auto *integer = dynamic_cast<ConstantInt *>(constant)) {
		auto *integerType = dynamic_cast<IntegerType *>(type);
		if (integerType->num_bits_ == 8)
			output << "\t.byte " << integer->value_ << '\n';
		else if (integerType->num_bits_ == 64)
			output << "\t.quad " << integer->value_ << '\n';
		else
			output << "\t.word " << integer->value_ << '\n';
		return;
	}
	if (auto *floating = dynamic_cast<ConstantFloat *>(constant)) {
		std::uint32_t bits = 0;
		std::memcpy(&bits, &floating->value_, sizeof(bits));
		output << "\t.word 0x" << std::hex << bits << std::dec << '\n';
		return;
	}
	if (auto *array = dynamic_cast<ConstantArray *>(constant)) {
		auto *arrayType = dynamic_cast<ArrayType *>(type);
		for (Constant *element : array->const_array)
			emitConstantValue(output, element, arrayType->contained_);
		return;
	}
	if (auto *vector = dynamic_cast<ConstantVector *>(constant)) {
		auto *vectorType = dynamic_cast<VectorType *>(type);
		for (Constant *element : vector->elements_)
			emitConstantValue(output, element, vectorType->contained_);
		return;
	}
}

} // namespace

void AArch64Backend::generate() {
	SelectionDAGBuilder dagBuilder;
	DAGLegalizer legalizer;
	DAGCombiner combiner;
	AArch64InstructionSelector selector;
	MachineVerifier verifier;
	AArch64AssemblyPrinter printer;

	MachineFunctionPassManager machinePipeline(
	    verifier, options_.verifyMachineIR, options_.dumpMachineIR);
	buildMachinePipeline(machinePipeline, options_);

	output_ << "\t.arch armv8-a\n\t.text\n";
	for (Function *function : module_.function_list_) {
		if (function->is_declaration())
			continue;
		function->set_instr_name();
		auto dag = dagBuilder.build(function);
		legalizer.run(*dag);
		combiner.run(*dag, options_.optimizationLevel >= 1);
		if (options_.dumpSelectionDAG)
			printSelectionDAG(*dag, std::cerr);

		auto machineFunction = selector.select(*dag);
		if (options_.verifyMachineIR)
			verifier.verifyOrAbort(*machineFunction, "instruction-select");
		machinePipeline.run(*machineFunction);
		if (options_.verifyMachineIR)
			verifier.verifyOrAbort(*machineFunction, "pre-emit");
		printer.printFunction(*machineFunction, output_);
	}

	std::vector<GlobalVariable *> data;
	std::vector<GlobalVariable *> bss;
	std::vector<GlobalVariable *> rodata;
	for (GlobalVariable *global : module_.global_list_) {
		if (global->is_const_)
			rodata.push_back(global);
		else if (global->init_val_ &&
		         !dynamic_cast<ConstantZero *>(global->init_val_))
			data.push_back(global);
		else
			bss.push_back(global);
	}
	if (!data.empty()) {
		output_ << "\t.data\n";
		for (GlobalVariable *global : data)
			emitGlobal(global);
	}
	if (!bss.empty()) {
		output_ << "\t.bss\n";
		for (GlobalVariable *global : bss)
			emitGlobal(global);
	}
	if (!rodata.empty()) {
		output_ << "\t.section .rodata\n";
		for (GlobalVariable *global : rodata)
			emitGlobal(global);
	}
	emitParallelRuntime();
}

void AArch64Backend::emitParallelRuntime() {
	std::vector<int> bodyIds;
	for (Function *function : module_.function_list_) {
		if (function->is_declaration())
			continue;
		for (BasicBlock *block : function->basic_blocks_)
			for (Instruction *instruction : block->instr_list_) {
				auto *call = dynamic_cast<CallInst *>(instruction);
				if (!call || call->num_ops() < 2)
					continue;
				auto *callee = dynamic_cast<Function *>(
				    call->get_operand(call->num_ops() - 1));
				if (!callee || callee->name_ != "__sysy_parallel_for")
					continue;
				auto *id = dynamic_cast<ConstantInt *>(call->get_operand(0));
				if (!id || id->value_ < 0 ||
				    id->value_ > std::numeric_limits<int>::max())
					continue;
				bodyIds.push_back(static_cast<int>(id->value_));
			}
	}
	if (bodyIds.empty())
		return;

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
	Type *valueType = pointerType->contained_;
	const unsigned size = SelectionDAGBuilder::typeSize(valueType);
	unsigned alignment = 1;
	if (size >= 16)
		alignment = 16;
	else if (size >= 8)
		alignment = 8;
	else if (size >= 4)
		alignment = 4;
	unsigned alignmentPower = 0;
	while ((1U << alignmentPower) < alignment)
		++alignmentPower;
	output_ << "\t.global " << global->name_ << '\n'
	        << "\t.p2align " << alignmentPower << '\n'
	        << global->name_ << ":\n";

	if (global->init_val_)
		emitConstantValue(output_, global->init_val_, valueType);
	else
		output_ << "\t.zero " << size << '\n';
}

} // namespace backend::aarch64
