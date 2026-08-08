#pragma once

#include "../ir/ir.hpp"
#include "../ir/instruction.hpp"

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

class LoopInfo;

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

    bool operator==(const MemoryLocation &other) const {
        return ptr == other.ptr &&
               elemType == other.elemType &&
               sizeBytes == other.sizeBytes;
    }
};

class BasicAliasAnalysis {
public:
    void analyze(Module *module);

    MemoryLocation getMemoryLocation(Value *ptr) const;
    AliasResult alias(const MemoryLocation &a, const MemoryLocation &b) const;
    AliasResult alias(Value *a, Value *b) const;
    ModRefInfo getModRefInfo(Instruction *inst, Value *ptr) const;
    ModRefInfo getCallModRef(CallInst *call, Value *ptr) const;
    ModRefInfo getFunctionModRef(Function *func, Value *ptrOrGlobal = nullptr) const;

    bool isPure(Function *func) const;
    bool mayHaveSideEffect(Function *func) const;
    bool isNoCapture(Function *func, Argument *arg) const;
    bool isLocalArrayPointer(Value *ptr) const;
    bool isImmutableLoad(Instruction *load, const LoopInfo &LI) const;

    // 解析指针的底层内存对象（穿透 bitcast / GEP 链），返回 alloca / global / argument 等。
    Value *getUnderlyingObject(Value *ptr) const;

private:
    struct PointerInfo {
        Value *base = nullptr;
        bool hasConstantOffset = true;
        long long offsetBytes = 0;
    };

    struct FunctionSummary {
        struct LocationEffect {
            MemoryLocation loc;
            ModRefInfo effect = ModRefInfo::NoModRef;

            bool operator==(const LocationEffect &other) const {
                return loc == other.loc && effect == other.effect;
            }
        };

        bool pure = false;
        bool sideEffect = true;
        ModRefInfo overall = ModRefInfo::ModRef;
        bool hasUnknownMemoryEffect = true;
        std::vector<bool> argNoCapture;
        std::vector<LocationEffect> locationEffects;
    };

    PointerInfo getPointerInfo(Value *ptr) const;
    long long typeSize(Type *ty) const;
    void addLocationEffect(FunctionSummary &summary, MemoryLocation loc,
                           ModRefInfo effect) const;
    FunctionSummary computeFunctionSummary(Function *func) const;
    bool valueDoesNotCapture(Value *value,
                             std::unordered_set<Value *> &visited) const;
    bool immutableObjectHasSafeUses(
        Value *value, std::unordered_set<Value *> &visited) const;
    bool isTrackedMemoryObject(Value *value) const;
    std::string aliasResultName(AliasResult result) const;

    Module *module_ = nullptr;
    std::map<Function *, FunctionSummary> summaries_;
};
