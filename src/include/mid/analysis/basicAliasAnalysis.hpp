#pragma once

#include "../ir/ir.hpp"
#include "../ir/instruction.hpp"

#include <map>
#include <set>

enum class AliasResult {
    NoAlias,
    MayAlias,
    MustAlias,
};

enum class ModRefInfo {
    NoModRef = 0,
    Ref = 1,
    Mod = 2,
    ModRef = 3,
};

inline ModRefInfo combineModRef(ModRefInfo a, ModRefInfo b) {
    return static_cast<ModRefInfo>(static_cast<int>(a) | static_cast<int>(b));
}

inline bool isModSet(ModRefInfo mr) {
    return (static_cast<int>(mr) & static_cast<int>(ModRefInfo::Mod)) != 0;
}

inline bool isRefSet(ModRefInfo mr) {
    return (static_cast<int>(mr) & static_cast<int>(ModRefInfo::Ref)) != 0;
}

struct MemoryLocation {
    Value *ptr = nullptr;
    Type *elemType = nullptr;
    long long sizeBytes = -1;
};

class BasicAliasAnalysis {
public:
    void analyze(Module *module);

    AliasResult alias(Value *a, Value *b) const;
    ModRefInfo getModRefInfo(Instruction *inst, Value *ptr) const;
    ModRefInfo getFunctionModRef(Function *func, Value *ptrOrGlobal = nullptr) const;

    bool isPure(Function *func) const;
    bool mayHaveSideEffect(Function *func) const;
    bool isLocalArrayPointer(Value *ptr) const;

private:
    struct PointerInfo {
        Value *base = nullptr;
        bool hasConstantOffset = true;
        long long offsetBytes = 0;
    };

    struct FunctionSummary {
        bool pure = false;
        bool sideEffect = true;
        ModRefInfo overall = ModRefInfo::ModRef;
        std::map<Value *, ModRefInfo> objectEffects;
    };

    PointerInfo getPointerInfo(Value *ptr) const;
    long long typeSize(Type *ty) const;
    void addObjectEffect(FunctionSummary &summary, Value *ptr, ModRefInfo effect) const;
    FunctionSummary computeFunctionSummary(Function *func) const;
    bool isTrackedMemoryObject(Value *value) const;

    Module *module_ = nullptr;
    std::map<Function *, FunctionSummary> summaries_;
};
