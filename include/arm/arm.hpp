#pragma once
#include "../ir/ir.hpp"
#include <sstream>
#include <unordered_map>

class ArmBuilder {
public:
    std::string build(Module* m);

private:
    struct ValueInfo {
        bool in_reg = false;
        std::string reg;
        int stack_offset = -1;
    };

    Module* module_ = nullptr;
    std::ostringstream out_;
    int next_stack_offset_ = 0;
    std::unordered_map<Value*, ValueInfo> locations_;
    std::unordered_map<BasicBlock*, std::string> bb_labels_;

    void emit(const std::string& s);
    void emitLine(const std::string& s);
    void emitFunction(Function* f);
    void emitGlobal(GlobalVariable* g);
    void emitPrologue(int stack_size);
    void emitEpilogue();
    void lowerBlock(BasicBlock* bb);
    void lowerInstr(Instruction* inst);

    std::string regFor(Value* v);
    std::string regFor(Value* v, const std::string& preferred);
    std::string materialize(Value* v, const std::string& preferred = "x9");
    int stackSlot(Value* v, int size = 8);
    int stackSlotForType(Type* ty) const;
    std::string lowerConstantInt(ConstantInt* c, const std::string& preferred);
    std::string lowerValue(Value* v, const std::string& preferred = "x9");
    std::string lowerCmpCond(ICmpInst::ICmpOp op);
    std::string lowerFcmpCond(FCmpInst::FCmpOp op);
    bool fitsImm12(int64_t v) const;
    bool isPow2(int64_t v) const;
    std::string escapeLabel(const std::string& s) const;
    int typeSize(Type* ty) const;
    void ensureStackFrame(Function* f);
    void spillAll();
    bool isVoidCallResult(Instruction* inst) const;
};
