#pragma once

#include "loopInfo.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

enum class SCEVKind {
    Constant,
    Unknown,
    AddExpr,
    MulExpr,
    AddRecExpr,
    CouldNotCompute,
};

class SCEV {
public:
    SCEV(SCEVKind kind, Type *type) : kind_(kind), type_(type) {}
    virtual ~SCEV() = default;

    SCEVKind kind() const { return kind_; }
    Type *type() const { return type_; }
    virtual std::string print() const = 0;

private:
    SCEVKind kind_;
    Type *type_;
};

class SCEVConstant : public SCEV {
public:
    SCEVConstant(Type *type, long long value)
        : SCEV(SCEVKind::Constant, type), value_(value) {}

    long long value() const { return value_; }
    std::string print() const override;

private:
    long long value_;
};

class SCEVUnknown : public SCEV {
public:
    explicit SCEVUnknown(Value *value)
        : SCEV(SCEVKind::Unknown, value ? value->type_ : nullptr), value_(value) {}

    Value *value() const { return value_; }
    std::string print() const override;

private:
    Value *value_;
};

class SCEVNAryExpr : public SCEV {
public:
    SCEVNAryExpr(SCEVKind kind, Type *type, std::vector<const SCEV*> operands)
        : SCEV(kind, type), operands_(std::move(operands)) {}

    const std::vector<const SCEV*> &operands() const { return operands_; }

protected:
    std::vector<const SCEV*> operands_;
};

class SCEVAddExpr : public SCEVNAryExpr {
public:
    SCEVAddExpr(Type *type, std::vector<const SCEV*> operands)
        : SCEVNAryExpr(SCEVKind::AddExpr, type, std::move(operands)) {}

    std::string print() const override;
};

class SCEVMulExpr : public SCEVNAryExpr {
public:
    SCEVMulExpr(Type *type, std::vector<const SCEV*> operands)
        : SCEVNAryExpr(SCEVKind::MulExpr, type, std::move(operands)) {}

    std::string print() const override;
};

class SCEVAddRecExpr : public SCEV {
public:
    SCEVAddRecExpr(Type *type, const SCEV *start, const SCEV *step, Loop *loop, PhiInst *phi)
        : SCEV(SCEVKind::AddRecExpr, type), start_(start), step_(step), loop_(loop), phi_(phi) {}

    const SCEV *start() const { return start_; }
    const SCEV *step() const { return step_; }
    Loop *loop() const { return loop_; }
    PhiInst *phi() const { return phi_; }
    std::string print() const override;

private:
    const SCEV *start_;
    const SCEV *step_;
    Loop *loop_;
    PhiInst *phi_;
};

class SCEVCouldNotCompute : public SCEV {
public:
    explicit SCEVCouldNotCompute(Type *type)
        : SCEV(SCEVKind::CouldNotCompute, type) {}

    std::string print() const override;
};

struct SCEVGEPInfo {
    bool valid = false;
    Value *basePtr = nullptr;
    Type *elementType = nullptr;
    std::vector<long long> shape;
    const SCEV *elementOffset = nullptr;
};

class ScalarEvolution {
public:
    explicit ScalarEvolution(const LoopInfo &LI) : LI_(&LI) {}

    const SCEV *getSCEV(Value *v);
    const SCEV *getSCEVAtScope(Value *v, Loop *scope);
    const SCEV *getTripCount(Loop *loop);
    SCEVGEPInfo getLinearizedGEP(GetElementPtrInst *gep);

    bool isLoopInvariant(const SCEV *s, Loop *loop) const;
    const SCEVAddRecExpr *getAddRecForLoop(const SCEV *s, Loop *loop) const;

    void clear();

private:
    const SCEV *getSCEVImpl(Value *v);
    const SCEV *tryCreateAddRec(PhiInst *phi);

    const SCEV *getConstant(Type *type, long long value);
    const SCEV *getUnknown(Value *value);
    const SCEV *getCouldNotCompute(Type *type);
    const SCEV *getAddExpr(std::vector<const SCEV*> operands, Type *type);
    const SCEV *getMulExpr(std::vector<const SCEV*> operands, Type *type);
    const SCEV *getAddRecExpr(const SCEV *start, const SCEV *step, Loop *loop, PhiInst *phi, Type *type);
    const SCEV *getNegative(const SCEV *s, Type *type);

    bool isSameOrDescendantLoop(Loop *candidate, Loop *ancestor) const;
    bool containsVaryingAddRec(const SCEV *s, Loop *loop) const;
    static std::string keyForPointer(const void *ptr);
    static std::string keyForSCEV(const SCEV *s);

    const LoopInfo *LI_;
    std::vector<std::unique_ptr<SCEV>> nodes_;
    std::map<std::string, const SCEV*> unique_;
    std::map<Value*, const SCEV*> value_cache_;
    std::set<Value*> visiting_;
};
