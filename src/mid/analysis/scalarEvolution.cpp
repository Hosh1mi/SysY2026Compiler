#include "../../include/mid/analysis/scalarEvolution.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

namespace {

// ScalarEvolution is a proving analysis, so declining an excessively large
// expression is always safe.  Bounding flattened commutative expressions
// prevents a left-deep chain from repeatedly copying and sorting every prefix
// (quadratic construction time).  Downstream clients treat CouldNotCompute as
// unknown and fall back to their local reasoning.
constexpr std::size_t kMaxSCEVNAryOperands = 64;

} // namespace

std::string SCEVConstant::print() const {
    return std::to_string(value_);
}

std::string SCEVUnknown::print() const {
    if (!value_) return "unknown(<null>)";
    std::string name = value_->name_.empty() ? "<anon>" : value_->name_;
    return "unknown(" + name + ")";
}

std::string SCEVAddExpr::print() const {
    std::ostringstream oss;
    oss << "(";
    for (size_t i = 0; i < operands_.size(); i++) {
        if (i) oss << " + ";
        oss << operands_[i]->print();
    }
    oss << ")";
    return oss.str();
}

std::string SCEVMulExpr::print() const {
    std::ostringstream oss;
    oss << "(";
    for (size_t i = 0; i < operands_.size(); i++) {
        if (i) oss << " * ";
        oss << operands_[i]->print();
    }
    oss << ")";
    return oss.str();
}

std::string SCEVAddRecExpr::print() const {
    std::ostringstream oss;
    oss << "{" << start_->print() << ",+," << step_->print() << "}<";
    oss << (loop_ && loop_->header ? loop_->header->name_ : "?") << ">";
    return oss.str();
}

std::string SCEVCouldNotCompute::print() const {
    return "<could-not-compute>";
}

const SCEV *ScalarEvolution::getSCEV(Value *v) {
    visiting_.clear();
    return getSCEVImpl(v);
}

const SCEV *ScalarEvolution::getSCEVAtScope(Value *v, Loop *scope) {
    (void)scope;
    return getSCEV(v);
}

const SCEV *ScalarEvolution::getTripCount(Loop *loop) {
    if (!loop || !loop->hasCanonicalIV() || !loop->tripCount)
        return getCouldNotCompute(nullptr);
    return getSCEV(loop->tripCount);
}

static bool multiplyNoOverflow(long long a, long long b, long long &out) {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a > std::numeric_limits<long long>::max() / b)
        return false;
    out = a * b;
    return true;
}

SCEVGEPInfo ScalarEvolution::getLinearizedGEP(GetElementPtrInst *gep) {
    SCEVGEPInfo info;
    if (!gep || gep->num_ops_ < 2)
        return info;

    Value *base = gep->get_operand(0);
    auto *ptrTy = dynamic_cast<PointerType *>(base->type_);
    if (!ptrTy)
        return info;

    for (unsigned i = 1; i < gep->num_ops_; i++) {
        auto *idxTy = dynamic_cast<IntegerType *>(gep->get_operand(i)->type_);
        if (!idxTy || idxTy->num_bits_ != 32)
            return info;
    }

    std::vector<long long> shape;
    Type *elementTy = ptrTy->contained_;
    while (auto *arrTy = dynamic_cast<ArrayType *>(elementTy)) {
        shape.push_back(arrTy->num_elements_);
        elementTy = arrTy->contained_;
    }

    Type *offsetTy = gep->get_operand(1)->type_;
    std::vector<const SCEV *> terms;

    if (shape.empty()) {
        if (gep->num_ops_ != 2)
            return info;
        terms.push_back(getSCEV(gep->get_operand(1)));
    } else {
        auto *leading = dynamic_cast<ConstantInt *>(gep->get_operand(1));
        if (!leading || leading->value_ != 0)
            return info;

        unsigned dataIndexCount = gep->num_ops_ - 2;
        if (dataIndexCount > shape.size())
            return info;

        for (unsigned idxNo = 0; idxNo < dataIndexCount; idxNo++) {
            long long stride = 1;
            for (size_t dim = idxNo + 1; dim < shape.size(); dim++) {
                if (!multiplyNoOverflow(stride, shape[dim], stride))
                    return info;
            }

            const SCEV *idxS = getSCEV(gep->get_operand(idxNo + 2));
            if (!idxS || idxS->kind() == SCEVKind::CouldNotCompute)
                return info;

            if (stride == 1) {
                terms.push_back(idxS);
            } else {
                terms.push_back(getMulExpr({getConstant(offsetTy, stride), idxS}, offsetTy));
            }
        }
    }

    const SCEV *offset = nullptr;
    if (terms.empty()) {
        offset = getConstant(offsetTy, 0);
    } else {
        offset = getAddExpr(terms, offsetTy);
    }

    if (!offset || offset->kind() == SCEVKind::CouldNotCompute)
        return info;

    info.valid = true;
    info.basePtr = base;
    info.elementType = elementTy;
    info.shape = std::move(shape);
    info.elementOffset = offset;
    return info;
}

bool ScalarEvolution::isLoopInvariant(const SCEV *s, Loop *loop) const {
    if (!s || !loop) return false;

    switch (s->kind()) {
    case SCEVKind::Constant:
        return true;
    case SCEVKind::CouldNotCompute:
        return false;
    case SCEVKind::Unknown: {
        auto *unknown = static_cast<const SCEVUnknown*>(s);
        auto *inst = dynamic_cast<Instruction*>(unknown->value());
        return !inst || !loop->isInLoop(inst);
    }
    case SCEVKind::AddExpr:
    case SCEVKind::MulExpr: {
        auto *nary = static_cast<const SCEVNAryExpr*>(s);
        for (auto *op : nary->operands()) {
            if (!isLoopInvariant(op, loop)) return false;
        }
        return true;
    }
    case SCEVKind::AddRecExpr: {
        auto *addrec = static_cast<const SCEVAddRecExpr*>(s);
        if (isSameOrDescendantLoop(addrec->loop(), loop)) return false;
        return isLoopInvariant(addrec->start(), loop) &&
               isLoopInvariant(addrec->step(), loop);
    }
    }
    return false;
}

const SCEVAddRecExpr *ScalarEvolution::getAddRecForLoop(const SCEV *s, Loop *loop) const {
    if (!s || !loop) return nullptr;
    if (auto *addrec = dynamic_cast<const SCEVAddRecExpr*>(s)) {
        return addrec->loop() == loop ? addrec : nullptr;
    }
    if (auto *nary = dynamic_cast<const SCEVNAryExpr*>(s)) {
        for (auto *op : nary->operands()) {
            if (auto *found = getAddRecForLoop(op, loop)) return found;
        }
    }
    return nullptr;
}

void ScalarEvolution::clear() {
    nodes_.clear();
    scevOrder_.clear();
    unique_.clear();
    value_cache_.clear();
    visiting_.clear();
}

const SCEV *ScalarEvolution::getSCEVImpl(Value *v) {
    if (!v) return getCouldNotCompute(nullptr);

    auto cached = value_cache_.find(v);
    if (cached != value_cache_.end()) return cached->second;

    if (!visiting_.insert(v).second)
        return getCouldNotCompute(v->type_);

    const SCEV *result = nullptr;

    if (auto *ci = dynamic_cast<ConstantInt*>(v)) {
        result = getConstant(ci->type_, ci->value_);
    } else if (auto *phi = dynamic_cast<PhiInst*>(v)) {
        result = tryCreateAddRec(phi);
        if (!result) result = getUnknown(v);
    } else if (auto *bin = dynamic_cast<BinaryInst*>(v)) {
        if (bin->type_->tid_ != Type::IntegerTyID) {
            result = getUnknown(v);
        } else if (bin->op_id_ == Instruction::Add) {
            result = getAddExpr({getSCEVImpl(bin->get_operand(0)),
                                 getSCEVImpl(bin->get_operand(1))}, bin->type_);
        } else if (bin->op_id_ == Instruction::Sub) {
            const SCEV *rhs = getNegative(getSCEVImpl(bin->get_operand(1)), bin->type_);
            result = getAddExpr({getSCEVImpl(bin->get_operand(0)), rhs}, bin->type_);
        } else if (bin->op_id_ == Instruction::Mul) {
            result = getMulExpr({getSCEVImpl(bin->get_operand(0)),
                                 getSCEVImpl(bin->get_operand(1))}, bin->type_);
        } else {
            result = getUnknown(v);
        }
    } else if (dynamic_cast<Constant*>(v) ||
               dynamic_cast<Argument*>(v) ||
               dynamic_cast<GlobalVariable*>(v) ||
               dynamic_cast<Instruction*>(v)) {
        result = getUnknown(v);
    } else {
        result = getCouldNotCompute(v->type_);
    }

    visiting_.erase(v);
    value_cache_[v] = result;
    return result;
}

const SCEV *ScalarEvolution::tryCreateAddRec(PhiInst *phi) {
    if (!phi || phi->type_->tid_ != Type::IntegerTyID) return nullptr;

    Loop *loop = LI_->getLoopFor(phi->parent_);
    if (!loop || loop->header != phi->parent_) return nullptr;

    Value *outsideVal = nullptr;
    Value *insideVal = nullptr;

    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        auto *pred = dynamic_cast<BasicBlock*>(phi->get_operand(i + 1));
        if (!pred) return nullptr;

        if (loop->blocks.count(pred)) {
            if (insideVal) return nullptr;
            insideVal = phi->get_operand(i);
        } else {
            if (outsideVal) return nullptr;
            outsideVal = phi->get_operand(i);
        }
    }
    if (!outsideVal || !insideVal) return nullptr;

    auto *update = dynamic_cast<BinaryInst*>(insideVal);
    if (!update || update->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *stepVal = nullptr;
    bool negateStep = false;

    if (update->op_id_ == Instruction::Add) {
        if (update->get_operand(0) == phi) {
            stepVal = update->get_operand(1);
        } else if (update->get_operand(1) == phi) {
            stepVal = update->get_operand(0);
        }
    } else if (update->op_id_ == Instruction::Sub) {
        if (update->get_operand(0) == phi) {
            stepVal = update->get_operand(1);
            negateStep = true;
        }
    }
    if (!stepVal) return nullptr;

    const SCEV *step = getSCEVImpl(stepVal);
    if (negateStep) step = getNegative(step, phi->type_);
    if (!isLoopInvariant(step, loop)) return nullptr;

    const SCEV *start = getSCEVImpl(outsideVal);
    if (start->kind() == SCEVKind::CouldNotCompute ||
        step->kind() == SCEVKind::CouldNotCompute)
        return nullptr;

    return getAddRecExpr(start, step, loop, phi, phi->type_);
}

const SCEV *ScalarEvolution::getConstant(Type *type, long long value) {
    std::string key = "C:" + keyForPointer(type) + ":" + std::to_string(value);
    auto it = unique_.find(key);
    if (it != unique_.end()) return it->second;

    nodes_.push_back(std::make_unique<SCEVConstant>(type, value));
    const SCEV *s = nodes_.back().get();
    scevOrder_[s] = scevOrder_.size();
    unique_[key] = s;
    return s;
}

const SCEV *ScalarEvolution::getUnknown(Value *value) {
    std::string key = "U:" + keyForPointer(value);
    auto it = unique_.find(key);
    if (it != unique_.end()) return it->second;

    nodes_.push_back(std::make_unique<SCEVUnknown>(value));
    const SCEV *s = nodes_.back().get();
    scevOrder_[s] = scevOrder_.size();
    unique_[key] = s;
    return s;
}

const SCEV *ScalarEvolution::getCouldNotCompute(Type *type) {
    std::string key = "X:" + keyForPointer(type);
    auto it = unique_.find(key);
    if (it != unique_.end()) return it->second;

    nodes_.push_back(std::make_unique<SCEVCouldNotCompute>(type));
    const SCEV *s = nodes_.back().get();
    scevOrder_[s] = scevOrder_.size();
    unique_[key] = s;
    return s;
}

const SCEV *ScalarEvolution::getAddExpr(std::vector<const SCEV*> operands, Type *type) {
    std::vector<const SCEV*> flat;
    long long constant = 0;

    auto collect = [&](auto &&self, const SCEV *op) -> bool {
        if (!op) return false;
        if (!type) type = op->type();
        if (op->kind() == SCEVKind::CouldNotCompute) return false;
        if (auto *c = dynamic_cast<const SCEVConstant*>(op)) {
            constant += c->value();
        } else if (auto *add = dynamic_cast<const SCEVAddExpr*>(op)) {
            for (auto *nested : add->operands()) {
                if (!self(self, nested)) return false;
            }
        } else {
            if (flat.size() >= kMaxSCEVNAryOperands)
                return false;
            flat.push_back(op);
        }
        return true;
    };

    for (auto *op : operands) {
        if (!collect(collect, op)) return getCouldNotCompute(type);
    }

    if (constant != 0 || flat.empty())
        flat.push_back(getConstant(type, constant));

    std::sort(flat.begin(), flat.end(),
              [this](const SCEV *a, const SCEV *b) {
                  if (a->kind() != b->kind())
                      return a->kind() < b->kind();
                  return scevOrder_.at(a) < scevOrder_.at(b);
              });

    std::vector<const SCEV*> cleaned;
    cleaned.reserve(flat.size());
    for (auto *op : flat) {
        auto *c = dynamic_cast<const SCEVConstant*>(op);
        if (c && c->value() == 0 && flat.size() > 1) continue;
        cleaned.push_back(op);
    }

    if (cleaned.empty()) return getConstant(type, 0);
    if (cleaned.size() == 1) return cleaned.front();

    std::string key = "A:" + keyForPointer(type);
    for (auto *op : cleaned) key += ":" + keyForSCEV(op);
    auto it = unique_.find(key);
    if (it != unique_.end()) return it->second;

    nodes_.push_back(std::make_unique<SCEVAddExpr>(type, cleaned));
    const SCEV *s = nodes_.back().get();
    scevOrder_[s] = scevOrder_.size();
    unique_[key] = s;
    return s;
}

const SCEV *ScalarEvolution::getMulExpr(std::vector<const SCEV*> operands, Type *type) {
    std::vector<const SCEV*> flat;
    long long constant = 1;

    auto collect = [&](auto &&self, const SCEV *op) -> bool {
        if (!op) return false;
        if (!type) type = op->type();
        if (op->kind() == SCEVKind::CouldNotCompute) return false;
        if (auto *c = dynamic_cast<const SCEVConstant*>(op)) {
            constant *= c->value();
        } else if (auto *mul = dynamic_cast<const SCEVMulExpr*>(op)) {
            for (auto *nested : mul->operands()) {
                if (!self(self, nested)) return false;
            }
        } else {
            if (flat.size() >= kMaxSCEVNAryOperands)
                return false;
            flat.push_back(op);
        }
        return true;
    };

    for (auto *op : operands) {
        if (!collect(collect, op)) return getCouldNotCompute(type);
    }

    if (constant == 0) return getConstant(type, 0);
    if (constant != 1 || flat.empty())
        flat.push_back(getConstant(type, constant));

    std::sort(flat.begin(), flat.end(),
              [this](const SCEV *a, const SCEV *b) {
                  if (a->kind() != b->kind())
                      return a->kind() < b->kind();
                  return scevOrder_.at(a) < scevOrder_.at(b);
              });

    std::vector<const SCEV*> cleaned;
    cleaned.reserve(flat.size());
    for (auto *op : flat) {
        auto *c = dynamic_cast<const SCEVConstant*>(op);
        if (c && c->value() == 1 && flat.size() > 1) continue;
        cleaned.push_back(op);
    }

    if (cleaned.empty()) return getConstant(type, 1);
    if (cleaned.size() == 1) return cleaned.front();

    std::string key = "M:" + keyForPointer(type);
    for (auto *op : cleaned) key += ":" + keyForSCEV(op);
    auto it = unique_.find(key);
    if (it != unique_.end()) return it->second;

    nodes_.push_back(std::make_unique<SCEVMulExpr>(type, cleaned));
    const SCEV *s = nodes_.back().get();
    scevOrder_[s] = scevOrder_.size();
    unique_[key] = s;
    return s;
}

const SCEV *ScalarEvolution::getAddRecExpr(const SCEV *start, const SCEV *step,
                                           Loop *loop, PhiInst *phi, Type *type) {
    if (!start || !step || !loop) return getCouldNotCompute(type);

    std::string key = "R:" + keyForPointer(type) + ":" + keyForPointer(loop) +
                      ":" + keyForPointer(phi) +
                      ":" + keyForSCEV(start) + ":" + keyForSCEV(step);
    auto it = unique_.find(key);
    if (it != unique_.end()) return it->second;

    nodes_.push_back(std::make_unique<SCEVAddRecExpr>(type, start, step, loop, phi));
    const SCEV *s = nodes_.back().get();
    scevOrder_[s] = scevOrder_.size();
    unique_[key] = s;
    return s;
}

const SCEV *ScalarEvolution::getNegative(const SCEV *s, Type *type) {
    if (!s) return getCouldNotCompute(type);
    if (!type) type = s->type();
    return getMulExpr({getConstant(type, -1), s}, type);
}

bool ScalarEvolution::isSameOrDescendantLoop(Loop *candidate, Loop *ancestor) const {
    for (Loop *cur = candidate; cur; cur = cur->parent) {
        if (cur == ancestor) return true;
    }
    return false;
}

bool ScalarEvolution::containsVaryingAddRec(const SCEV *s, Loop *loop) const {
    if (!s || !loop) return false;
    if (auto *addrec = dynamic_cast<const SCEVAddRecExpr*>(s))
        return isSameOrDescendantLoop(addrec->loop(), loop);
    if (auto *nary = dynamic_cast<const SCEVNAryExpr*>(s)) {
        for (auto *op : nary->operands()) {
            if (containsVaryingAddRec(op, loop)) return true;
        }
    }
    return false;
}

std::string ScalarEvolution::keyForPointer(const void *ptr) {
    std::ostringstream oss;
    oss << ptr;
    return oss.str();
}

std::string ScalarEvolution::keyForSCEV(const SCEV *s) const {
    if (!s) return "<null>";
    auto it = scevOrder_.find(s);
    if (it == scevOrder_.end()) return "<unknown-scev>";
    return std::to_string(static_cast<int>(s->kind())) + "#" +
           std::to_string(it->second);
}
