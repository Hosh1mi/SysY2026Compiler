// AST → 基础 IR：局部量走 entry alloca 槽，非 void 函数统一经 %retval 返回。
#include "../../include/mid/ir/irGen.hpp"
#include <iostream>
#include <unordered_map>
#include <vector>

#define CONST_INT(num) new ConstantInt(module->int32_ty_, num)
#define CONST_FLOAT(num) new ConstantFloat(module->float32_ty_, num)
#define VOID_T (module->void_ty_)
#define INT1_T (module->int1_ty_)
#define INT32_T  (module->int32_ty_)
#define FLOAT_T  (module->float32_ty_)
#define INT32PTR_T (module->get_pointer_type(module->int32_ty_))
#define FLOATPTR_T (module->get_pointer_type(module->float32_ty_))

Type* curType;                          // 当前 decl 类型
TypeSpec curSourceType;                 // 当前 decl 的源级向量形态
bool isConst;                           // 当前 decl 是否是 const
bool useConst = false;                  // 计算是否使用常量
std::vector<Type *> params;             // 函数形参类型表
std::vector<std::string> paramNames;    // 函数形参名表
Value *retAlloca = nullptr;             // 统一返回值槽（非 void）
BasicBlock *retBB = nullptr;            // 统一返回块
bool isNewFunc = false;                 // 函数体首层 block 不再重复 enter scope
bool requireLVal = false;               // LVal 只需地址，不发射 load
Function *currentFunction = nullptr;
Value *recentVal = nullptr;
BasicBlock *whileCondBB = nullptr;
BasicBlock *trueBB = nullptr;
BasicBlock *falseBB = nullptr;
BasicBlock *whileFalseBB = nullptr;     // break 目标
bool has_br = false;                    // 当前 BB 已发射终止指令
bool is_single_exp = false;             // 形如 "exp;" 的独立表达式语句
Value *vectorLaneBase = nullptr;         // lane 左值所属的向量地址
Value *vectorLaneIndex = nullptr;        // lane 左值的动态/常量下标
VectorType *expectedVectorType = nullptr; // braced literal type hint from context
ScalarizedVectorType *expectedScalarizedVectorType = nullptr;
ScalarizedVectorType *currentScalarizedReturnType = nullptr;
Value *scalarizedReturnPointer = nullptr;

struct ScalarizedFunctionInfo {
    ScalarizedVectorType *returnType = nullptr;
    std::vector<ScalarizedVectorType *> valueParameters;
};

static std::unordered_map<Function *, ScalarizedFunctionInfo>
    scalarizedFunctionInfo;
static std::vector<ScalarizedVectorType *> pendingScalarizedParameters;

class ScalarizedVectorValue final : public Value {
public:
    ScalarizedVectorValue(ScalarizedVectorType *type,
                          std::vector<Value *> lanes)
        : Value(type), lanes_(std::move(lanes)) {}

    std::string print() override { return "<scalarized-vector>"; }

    std::vector<Value *> lanes_;
};

static Type *scalarType(Module *module, TYPE element) {
    return element == TYPE_INT ? module->int32_ty_ : module->float32_ty_;
}

static unsigned resolveVectorLanes(GenIR *gen, const TypeSpec &type) {
    unsigned lanes = type.lanes;
    if (!type.laneConstant.empty()) {
        auto *constant = dynamic_cast<ConstantInt *>(
            gen->scope.find(type.laneConstant));
        if (!constant || constant->value_ <= 0) {
            std::cerr << "vector width '" << type.laneConstant
                      << "' is not a positive integer compiler constant\n";
            std::exit(1);
        }
        lanes = static_cast<unsigned>(constant->value_);
    }
    if (lanes != 2 && lanes != 3 && lanes != 4 && lanes != 8 && lanes != 16) {
        std::cerr << "unsupported fixed vector width " << lanes
                  << "; expected 2, 3, 4, 8, or 16 lanes\n";
        std::exit(1);
    }
    return lanes;
}

static Type *lowerFixedType(GenIR *gen, const TypeSpec &type) {
    if (!type.isFixedVector())
        return scalarType(gen->module.get(), type.element);
    unsigned lanes = resolveVectorLanes(gen, type);
    Type *element = scalarType(gen->module.get(), type.element);
    if (lanes == 4)
        return gen->module->get_vector_type(element, lanes);
    return gen->module->get_scalarized_vector_type(element, lanes);
}

static bool isIntegerValueType(Type *type) {
    if (type->tid_ == Type::IntegerTyID)
        return true;
    auto *vector = dynamic_cast<VectorType *>(type);
    return vector && vector->contained_->tid_ == Type::IntegerTyID;
}

static Constant *zeroScalar(Module *module, Type *type) {
    if (type == module->float32_ty_)
        return new ConstantFloat(module->float32_ty_, 0.0f);
    return new ConstantInt(module->int32_ty_, 0);
}

static Value *coerceScalar(IRStmtBuilder *builder, Module *module, Value *value,
                           Type *target) {
    if (value->type_ == target)
        return value;
    if (value->type_ == module->int32_ty_ && target == module->float32_ty_) {
        if (auto *constant = dynamic_cast<ConstantInt *>(value))
            return new ConstantFloat(module->float32_ty_, constant->value_);
        return builder->create_sitofp(value, target);
    }
    if (value->type_ == module->float32_ty_ && target == module->int32_ty_) {
        if (auto *constant = dynamic_cast<ConstantFloat *>(value))
            return new ConstantInt(module->int32_ty_, (int)constant->value_);
        return builder->create_fptosi(value, target);
    }
    std::cerr << "incompatible scalar/vector value in initializer or assignment\n";
    std::exit(1);
}

static ScalarizedVectorValue *asScalarizedVector(Value *value) {
    return dynamic_cast<ScalarizedVectorValue *>(value);
}

static ScalarizedVectorValue *splatScalarizedVector(
    GenIR *gen, Value *value, ScalarizedVectorType *type) {
    value = coerceScalar(gen->builder, gen->module.get(), value,
                         type->contained_);
    return new ScalarizedVectorValue(
        type, std::vector<Value *>(type->num_elements_, value));
}

static ScalarizedVectorValue *loadScalarizedVector(
    GenIR *gen, Value *pointer, ScalarizedVectorType *type) {
    std::vector<Value *> lanes;
    lanes.reserve(type->num_elements_);
    for (unsigned lane = 0; lane < type->num_elements_; ++lane) {
        Value *lanePointer = gen->builder->create_gep(
            pointer, {new ConstantInt(gen->module->int32_ty_, 0),
                      new ConstantInt(gen->module->int32_ty_, lane)});
        lanes.push_back(gen->builder->create_load(lanePointer));
    }
    return new ScalarizedVectorValue(type, std::move(lanes));
}

static ScalarizedVectorValue *constantScalarizedVector(
    Module *module, ScalarizedVectorType *type, Constant *constant) {
    std::vector<Value *> lanes;
    lanes.reserve(type->num_elements_);
    if (auto *array = dynamic_cast<ConstantArray *>(constant)) {
        for (Constant *lane : array->const_array)
            lanes.push_back(lane);
    } else if (dynamic_cast<ConstantZero *>(constant)) {
        for (unsigned lane = 0; lane < type->num_elements_; ++lane)
            lanes.push_back(zeroScalar(module, type->contained_));
    }
    if (lanes.size() != type->num_elements_) {
        std::cerr << "malformed scalarized vector constant\n";
        std::exit(1);
    }
    return new ScalarizedVectorValue(type, std::move(lanes));
}

static ConstantArray *constantFromScalarizedVector(
    ScalarizedVectorValue *value) {
    std::vector<Constant *> lanes;
    lanes.reserve(value->lanes_.size());
    for (Value *lane : value->lanes_) {
        auto *constant = dynamic_cast<Constant *>(lane);
        if (!constant)
            return nullptr;
        lanes.push_back(constant);
    }
    return new ConstantArray(
        static_cast<ScalarizedVectorType *>(value->type_), lanes);
}

static void storeScalarizedVector(GenIR *gen, Value *pointer,
                                  ScalarizedVectorValue *value,
                                  ScalarizedVectorType *type) {
    if (!value || value->type_ != type) {
        std::cerr << "scalarized vector assignment requires matching element type and width\n";
        std::exit(1);
    }
    for (unsigned lane = 0; lane < type->num_elements_; ++lane) {
        Value *lanePointer = gen->builder->create_gep(
            pointer, {new ConstantInt(gen->module->int32_ty_, 0),
                      new ConstantInt(gen->module->int32_ty_, lane)});
        gen->builder->create_store(value->lanes_[lane], lanePointer);
    }
}

static ScalarizedVectorValue *buildScalarizedVectorInitializer(
    GenIR *gen, InitValAST *init, ScalarizedVectorType *type) {
    if (!init || (init->exp == nullptr && init->initValList.empty())) {
        Value *zero = zeroScalar(gen->module.get(), type->contained_);
        return new ScalarizedVectorValue(
            type, std::vector<Value *>(type->num_elements_, zero));
    }
    if (init->exp) {
        ScalarizedVectorType *saved = expectedScalarizedVectorType;
        expectedScalarizedVectorType = type;
        init->exp->accept(*gen);
        expectedScalarizedVectorType = saved;
        auto *value = asScalarizedVector(recentVal);
        if (!value || value->type_ != type) {
            std::cerr << "a fixed vector copy initializer must have the same type\n";
            std::exit(1);
        }
        return value;
    }
    if (init->initValList.size() > type->num_elements_) {
        std::cerr << "too many elements in fixed vector initializer\n";
        std::exit(1);
    }
    std::vector<Value *> lanes;
    for (auto &item : init->initValList) {
        if (!item->exp) {
            std::cerr << "nested braces are not valid for a one-dimensional vector\n";
            std::exit(1);
        }
        item->exp->accept(*gen);
        if (asScalarizedVector(recentVal)) {
            std::cerr << "a vector lane initializer must be scalar\n";
            std::exit(1);
        }
        lanes.push_back(coerceScalar(gen->builder, gen->module.get(), recentVal,
                                     type->contained_));
    }
    while (lanes.size() < type->num_elements_)
        lanes.push_back(zeroScalar(gen->module.get(), type->contained_));
    return new ScalarizedVectorValue(type, std::move(lanes));
}

static Value *buildVectorInitializer(GenIR *gen, InitValAST *init,
                                     VectorType *vectorType) {
    if (!init || (init->exp == nullptr && init->initValList.empty()))
        return new ConstantZero(vectorType);
    if (init->exp) {
        VectorType *savedExpected = expectedVectorType;
        expectedVectorType = vectorType;
        init->exp->accept(*gen);
        expectedVectorType = savedExpected;
        if (recentVal->type_ != vectorType) {
            std::cerr << "a fixed vector copy initializer must have the same type\n";
            std::exit(1);
        }
        return recentVal;
    }
    if (init->initValList.size() > vectorType->num_elements_) {
        std::cerr << "too many elements in fixed vector initializer\n";
        std::exit(1);
    }

    std::vector<Value *> lanes;
    bool allConstant = true;
    for (auto &item : init->initValList) {
        if (!item->exp) {
            std::cerr << "nested braces are not valid for a one-dimensional vector\n";
            std::exit(1);
        }
        item->exp->accept(*gen);
        Value *lane = coerceScalar(gen->builder, gen->module.get(), recentVal,
                                   vectorType->contained_);
        allConstant &= dynamic_cast<Constant *>(lane) != nullptr;
        lanes.push_back(lane);
    }
    while (lanes.size() < vectorType->num_elements_)
        lanes.push_back(zeroScalar(gen->module.get(), vectorType->contained_));

    if (allConstant) {
        std::vector<Constant *> constants;
        for (auto *lane : lanes)
            constants.push_back(static_cast<Constant *>(lane));
        return new ConstantVector(vectorType, constants);
    }

    Value *result = new ConstantZero(vectorType);
    for (unsigned i = 0; i < lanes.size(); ++i)
        result = new InsertElementInst(result, lanes[i],
                                       new ConstantInt(gen->module->int32_ty_, i),
                                       gen->builder->BB_);
    return result;
}

// Infer a fixed vector type for a braced literal when no contextual type is set.
static VectorType *inferVectorLiteralType(Module *module, InitValAST *init) {
    if (expectedVectorType)
        return expectedVectorType;
    bool anyFloat = false;
    if (init) {
        for (auto &item : init->initValList) {
            if (!item || !item->exp || !item->exp->mulExp)
                continue;
            auto *unary = item->exp->mulExp->unaryExp.get();
            // Peel a single unary +/- to reach a number primary.
            while (unary && unary->unaryExp && !unary->primaryExp &&
                   !unary->call && !unary->subscript)
                unary = unary->unaryExp.get();
            if (unary && unary->primaryExp && unary->primaryExp->number &&
                !unary->primaryExp->number->isInt)
                anyFloat = true;
        }
    }
    Type *contained = anyFloat ? module->float32_ty_ : module->int32_ty_;
    return module->get_vector_type(contained, 4);
}

// Build a compile-time constant for a nested array-of-vectors aggregate.
static Constant *buildConstantVectorAggregate(GenIR *gen, InitValAST *init,
                                              Type *type) {
    if (auto *vectorType = dynamic_cast<VectorType *>(type)) {
        Value *value = buildVectorInitializer(gen, init, vectorType);
        auto *constant = dynamic_cast<Constant *>(value);
        if (!constant) {
            std::cerr << "global vector array initializer is not constant\n";
            std::exit(1);
        }
        return constant;
    }

    if (auto *vectorType = dynamic_cast<ScalarizedVectorType *>(type)) {
        auto *value = buildScalarizedVectorInitializer(gen, init, vectorType);
        auto *constant = constantFromScalarizedVector(value);
        if (!constant) {
            std::cerr << "global vector array initializer is not constant\n";
            std::exit(1);
        }
        return constant;
    }

    auto *arrayType = dynamic_cast<ArrayType *>(type);
    if (!arrayType) {
        std::cerr << "unexpected type while initializing a vector array\n";
        std::exit(1);
    }

    unsigned length = arrayType->num_elements_;
    Type *elementType = arrayType->contained_;
    std::vector<Constant *> elements;
    if (init && !init->initValList.empty()) {
        if (init->exp) {
            std::cerr << "a nested vector array initializer must use braces\n";
            std::exit(1);
        }
        if (init->initValList.size() > length) {
            std::cerr << "too many vectors in array initializer\n";
            std::exit(1);
        }
        for (auto &item : init->initValList)
            elements.push_back(
                buildConstantVectorAggregate(gen, item.get(), elementType));
    }
    while (elements.size() < length)
        elements.push_back(new ConstantZero(elementType));
    return new ConstantArray(arrayType, elements);
}

// Store into a local nested array-of-vectors aggregate element by element.
static void storeLocalVectorAggregate(GenIR *gen, Value *slot, InitValAST *init,
                                      Type *type) {
    if (auto *vectorType = dynamic_cast<VectorType *>(type)) {
        Value *value = init ? buildVectorInitializer(gen, init, vectorType)
                            : (Value *)new ConstantZero(vectorType);
        gen->builder->create_store(value, slot);
        return;
    }


    if (auto *vectorType = dynamic_cast<ScalarizedVectorType *>(type)) {
        auto *value = buildScalarizedVectorInitializer(gen, init, vectorType);
        storeScalarizedVector(gen, slot, value, vectorType);
        return;
    }

    auto *arrayType = dynamic_cast<ArrayType *>(type);
    if (!arrayType) {
        std::cerr << "unexpected type while initializing a vector array\n";
        std::exit(1);
    }

    unsigned length = arrayType->num_elements_;
    Type *elementType = arrayType->contained_;
    if (init && init->exp) {
        std::cerr << "a nested vector array initializer must use braces\n";
        std::exit(1);
    }
    if (init && init->initValList.size() > length) {
        std::cerr << "too many vectors in array initializer\n";
        std::exit(1);
    }

    for (unsigned i = 0; i < length; ++i) {
        Value *element = gen->builder->create_gep(
            slot, {new ConstantInt(gen->module->int32_ty_, 0),
                   new ConstantInt(gen->module->int32_ty_, (int)i)});
        InitValAST *child = nullptr;
        if (init && i < init->initValList.size())
            child = init->initValList[i].get();
        storeLocalVectorAggregate(gen, element, child, elementType);
    }
}

static Value *splatScalar(IRStmtBuilder *builder, Module *module, Value *value,
                           VectorType *type) {
    value = coerceScalar(builder, module, value, type->contained_);
    if (auto *constant = dynamic_cast<Constant *>(value)) {
        std::vector<Constant *> lanes(type->num_elements_, constant);
        return new ConstantVector(type, lanes);
    }
    Value *result = new ConstantZero(type);
    for (unsigned lane = 0; lane < type->num_elements_; ++lane)
        result = new InsertElementInst(
            result, value, new ConstantInt(module->int32_ty_, lane),
            builder->BB_);
    return result;
}

static Value *scalarizeIntegerVectorBinary(IRStmtBuilder *builder, Module *module,
                                           Value *lhs, Value *rhs,
                                           Instruction::OpID op,
                                           VectorType *type) {
    Value *result = new ConstantZero(type);
    for (unsigned lane = 0; lane < type->num_elements_; ++lane) {
        auto *index = new ConstantInt(module->int32_ty_, lane);
        Value *left = new ExtractElementInst(lhs, index, builder->BB_);
        Value *right = new ExtractElementInst(rhs, index, builder->BB_);
        Value *value = new BinaryInst(type->contained_, op, left, right,
                                      builder->BB_);
        result = new InsertElementInst(result, value, index, builder->BB_);
    }
    return result;
}

static ConstantVector *foldConstantVector(Module *module,
                                          Instruction::OpID op,
                                          ConstantVector *lhs,
                                          ConstantVector *rhs) {
    auto *type = static_cast<VectorType *>(lhs->type_);
    if (rhs->type_ != type || lhs->elements_.size() != rhs->elements_.size())
        return nullptr;
    std::vector<Constant *> result;
    for (unsigned i = 0; i < lhs->elements_.size(); ++i) {
        if (type->contained_ == module->int32_ty_) {
            auto *a = dynamic_cast<ConstantInt *>(lhs->elements_[i]);
            auto *b = dynamic_cast<ConstantInt *>(rhs->elements_[i]);
            if (!a || !b)
                return nullptr;
            int value = 0;
            switch (op) {
            case Instruction::Add: value = a->value_ + b->value_; break;
            case Instruction::Sub: value = a->value_ - b->value_; break;
            case Instruction::Mul: value = a->value_ * b->value_; break;
            case Instruction::SDiv:
                if (b->value_ == 0) return nullptr;
                value = a->value_ / b->value_;
                break;
            case Instruction::SRem:
                if (b->value_ == 0) return nullptr;
                value = a->value_ % b->value_;
                break;
            default: return nullptr;
            }
            result.push_back(new ConstantInt(type->contained_, value));
        } else {
            auto *a = dynamic_cast<ConstantFloat *>(lhs->elements_[i]);
            auto *b = dynamic_cast<ConstantFloat *>(rhs->elements_[i]);
            if (!a || !b)
                return nullptr;
            float value = 0.0f;
            switch (op) {
            case Instruction::FAdd: value = a->value_ + b->value_; break;
            case Instruction::FSub: value = a->value_ - b->value_; break;
            case Instruction::FMul: value = a->value_ * b->value_; break;
            case Instruction::FDiv: value = a->value_ / b->value_; break;
            default: return nullptr;
            }
            result.push_back(new ConstantFloat(type->contained_, value));
        }
    }
    return new ConstantVector(type, result);
}

static Value *scalarizedLaneBinary(GenIR *gen, Value *lhs, Value *rhs,
                                   Instruction::OpID op, Type *elementType) {
    lhs = coerceScalar(gen->builder, gen->module.get(), lhs, elementType);
    rhs = coerceScalar(gen->builder, gen->module.get(), rhs, elementType);
    if (elementType == gen->module->int32_ty_) {
        auto *left = dynamic_cast<ConstantInt *>(lhs);
        auto *right = dynamic_cast<ConstantInt *>(rhs);
        if (left && right) {
            std::int64_t result = 0;
            switch (op) {
            case Instruction::Add: result = left->value_ + right->value_; break;
            case Instruction::Sub: result = left->value_ - right->value_; break;
            case Instruction::Mul: result = left->value_ * right->value_; break;
            case Instruction::SDiv:
                if (right->value_ == 0) {
                    std::cerr << "division by zero in constant vector expression\n";
                    std::exit(1);
                }
                result = left->value_ / right->value_;
                break;
            case Instruction::SRem:
                if (right->value_ == 0) {
                    std::cerr << "remainder by zero in constant vector expression\n";
                    std::exit(1);
                }
                result = left->value_ % right->value_;
                break;
            default:
                std::cerr << "unsupported integer vector operator\n";
                std::exit(1);
            }
            return new ConstantInt(elementType, result);
        }
    } else {
        auto *left = dynamic_cast<ConstantFloat *>(lhs);
        auto *right = dynamic_cast<ConstantFloat *>(rhs);
        if (left && right) {
            float result = 0.0f;
            switch (op) {
            case Instruction::FAdd: result = left->value_ + right->value_; break;
            case Instruction::FSub: result = left->value_ - right->value_; break;
            case Instruction::FMul: result = left->value_ * right->value_; break;
            case Instruction::FDiv: result = left->value_ / right->value_; break;
            default:
                std::cerr << "unsupported floating vector operator\n";
                std::exit(1);
            }
            return new ConstantFloat(elementType, result);
        }
    }
    return new BinaryInst(elementType, op, lhs, rhs, gen->builder->BB_);
}

static ScalarizedVectorValue *scalarizedVectorBinary(
    GenIR *gen, Value *lhs, Value *rhs, Instruction::OpID integerOp,
    Instruction::OpID floatingOp) {
    auto *left = asScalarizedVector(lhs);
    auto *right = asScalarizedVector(rhs);
    ScalarizedVectorType *type = left
        ? static_cast<ScalarizedVectorType *>(left->type_)
        : right ? static_cast<ScalarizedVectorType *>(right->type_) : nullptr;
    if (!type)
        return nullptr;
    if (!left)
        left = splatScalarizedVector(gen, lhs, type);
    if (!right)
        right = splatScalarizedVector(gen, rhs, type);
    if (left->type_ != type || right->type_ != type) {
        std::cerr << "vector operands must have the same element type and width\n";
        std::exit(1);
    }
    Instruction::OpID op = type->contained_ == gen->module->int32_ty_
                                ? integerOp : floatingOp;
    std::vector<Value *> lanes;
    lanes.reserve(type->num_elements_);
    for (unsigned lane = 0; lane < type->num_elements_; ++lane)
        lanes.push_back(scalarizedLaneBinary(
            gen, left->lanes_[lane], right->lanes_[lane], op,
            type->contained_));
    return new ScalarizedVectorValue(type, std::move(lanes));
}

static ScalarizedVectorValue *scalarizedVectorUnary(GenIR *gen,
                                                     ScalarizedVectorValue *value,
                                                     UOP op) {
    auto *type = static_cast<ScalarizedVectorType *>(value->type_);
    if (op == UOP_ADD)
        return value;
    std::vector<Value *> lanes;
    lanes.reserve(type->num_elements_);
    if (op == UOP_MINUS) {
        Instruction::OpID binaryOp = type->contained_ == gen->module->int32_ty_
                                          ? Instruction::Sub
                                          : Instruction::FSub;
        for (Value *lane : value->lanes_)
            lanes.push_back(scalarizedLaneBinary(
                gen, zeroScalar(gen->module.get(), type->contained_), lane,
                binaryOp, type->contained_));
        return new ScalarizedVectorValue(type, std::move(lanes));
    }

    auto *resultType = gen->module->get_scalarized_vector_type(
        gen->module->int32_ty_, type->num_elements_);
    for (Value *lane : value->lanes_) {
        if (auto *integer = dynamic_cast<ConstantInt *>(lane)) {
            lanes.push_back(new ConstantInt(gen->module->int32_ty_,
                                            integer->value_ == 0));
        } else if (auto *floating = dynamic_cast<ConstantFloat *>(lane)) {
            lanes.push_back(new ConstantInt(gen->module->int32_ty_,
                                            floating->value_ == 0.0f));
        } else if (type->contained_ == gen->module->int32_ty_) {
            lanes.push_back(gen->builder->create_zext(
                gen->builder->create_icmp_eq(
                    lane, new ConstantInt(gen->module->int32_ty_, 0)),
                gen->module->int32_ty_));
        } else {
            lanes.push_back(gen->builder->create_zext(
                gen->builder->create_fcmp_eq(
                    lane, new ConstantFloat(gen->module->float32_ty_, 0.0f)),
                gen->module->int32_ty_));
        }
    }
    return new ScalarizedVectorValue(resultType, std::move(lanes));
}

static Value *vectorLanePointer(GenIR *gen, Value *vectorPointer,
                                Value *index, VectorType *type) {
    Type *elementPointerType =
        gen->module->get_pointer_type(type->contained_);
    Value *elements = gen->builder->create_bitcast(vectorPointer,
                                                   elementPointerType);
    return gen->builder->create_gep(elements, {index});
}

// 在函数内创建带语义名的基本块；同名冲突时加数字后缀（if.then → if.then1）。
static BasicBlock *createNamedBB(Module *m, Function *func,
                                 const std::string &base) {
    return new BasicBlock(m, func->uniqueBasicBlockName(base), func);
}

// 将返回值写到 %retval 前做必要的 i32/float 转换。
static Value *coerceToReturnType(IRStmtBuilder *builder, Value *val, Type *retTy,
                                 Type *intTy, Type *floatTy) {
    if (val->type_ == floatTy && retTy == intTy)
        return builder->create_fptosi(val, intTy);
    if (val->type_ == intTy && retTy == floatTy)
        return builder->create_sitofp(val, floatTy);
    return val;
}

// 若 %retval 已无 load，删掉其上全部 store 与 alloca（避免留下死 store）。
static void eraseDeadRetvalSlot(Value *&slot) {
    if (!slot) return;
    for (auto &use : slot->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (user && user->is_load())
            return;
    }
    std::vector<Instruction *> deadStores;
    for (auto &use : slot->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (user && user->is_store())
            deadStores.push_back(user);
    }
    for (auto *st : deadStores) {
        if (st->parent_)
            st->parent_->delete_instr(st);
    }
    auto *alloca = dynamic_cast<AllocaInst *>(slot);
    if (alloca && alloca->parent_)
        alloca->parent_->delete_instr(alloca);
    slot = nullptr;
}

// store %v, %retval ; %t = load %retval → 用 %v 替换 %t；
// 若槽上再无 load，连同死 store / alloca 一并清掉。
static void foldRetvalStoreLoad(BasicBlock *bb, Value *&slot,
                                const std::vector<Instruction *> &moved) {
    if (!slot) return;
    for (auto *inst : moved) {
        if (!inst->is_load() || inst->get_operand(0) != slot)
            continue;
        if (!inst->parent_)
            continue;
        Value *storedVal = nullptr;
        Instruction *srcStore = nullptr;
        for (auto rit = bb->instr_list_.rbegin(); rit != bb->instr_list_.rend(); ++rit) {
            if (*rit == inst) continue;
            if ((*rit)->is_store() && (*rit)->get_operand(1) == slot) {
                storedVal = (*rit)->get_operand(0);
                srcStore = *rit;
                break;
            }
            if ((*rit)->is_load() && (*rit)->get_operand(0) == slot)
                break;
        }
        if (!storedVal) continue;
        inst->replace_all_use_with(storedVal);
        bb->delete_instr(inst);
        // 转发后该 store 若已无其它用途，随整槽清理一并删除
        (void)srcStore;
    }
    eraseDeadRetvalSlot(slot);
}

// 若 retBB 只有一个无条件前驱，把它并入该前驱并折叠 %retval 转发。
static void tryMergeTrivialRetBlock(Function *func, BasicBlock *retBlock,
                                    Value *&slot) {
    if (!retBlock || retBlock->pre_bbs_.size() != 1)
        return;
    BasicBlock *pred = retBlock->pre_bbs_[0];
    auto *term = pred->get_terminator();
    auto *br = dynamic_cast<BranchInst *>(term);
    if (!br || br->num_ops_ != 1 || br->get_operand(0) != retBlock)
        return;

    pred->delete_instr(br);
    std::vector<Instruction *> moved(retBlock->instr_list_.begin(),
                                     retBlock->instr_list_.end());
    for (auto *inst : moved) {
        retBlock->remove_instr(inst);
        if (inst->is_alloca())
            pred->add_instruction_front(inst);
        else
            pred->add_instruction(inst);
    }
    func->remove_bb(retBlock);
    foldRetvalStoreLoad(pred, slot, moved);
}


//判断得到的赋值与声明类型是否一致，并做转换
void GenIR::checkInitType() const {
    if (curType == INT32_T) {
        if (dynamic_cast<ConstantFloat*>(recentVal)) {
            auto temp = dynamic_cast<ConstantFloat*>(recentVal);
            recentVal = CONST_INT((int)temp->value_);
        } else if (recentVal->type_->tid_ == Type::FloatTyID) {
            recentVal = builder->create_fptosi(recentVal, INT32_T);
        }
    } else if (curType == FLOAT_T) {
        if (dynamic_cast<ConstantInt*>(recentVal)) {
            auto temp = dynamic_cast<ConstantInt*>(recentVal);
            recentVal = CONST_FLOAT(temp->value_);
        } else if (recentVal->type_->tid_ == Type::IntegerTyID) {
            recentVal = builder->create_sitofp(recentVal, FLOAT_T);
        }
    }
}

void GenIR::visit(CompUnitAST &ast) {
    for (const auto &def : ast.declDefList) {
        def->accept(*this);
    }
}

void GenIR::visit(DeclDefAST &ast) {
    if (ast.Decl != nullptr) {
        ast.Decl->accept(*this);
    } else {
        ast.funcDef->accept(*this);
    }
}

void GenIR::visit(DeclAST &ast) {
    isConst = ast.isConst;
    curSourceType = ast.bType;
    curType = ast.bType.isDynamicVector()
                  ? scalarType(module.get(), ast.bType.element)
                  : lowerFixedType(this, ast.bType);
    for (auto &def : ast.defList) {
        if (curSourceType.isDynamicVector() && def->arrays.empty()) {
            std::cerr << "a dynamic vector object requires an explicit storage length, "
                         "for example vector<int> a[n]\n";
            std::exit(1);
        }
        def->accept(*this);
    }
}

void GenIR::visit(DefAST &ast) {
    string varName = *ast.id;
    if ((dynamic_cast<VectorType *>(curType) ||
         dynamic_cast<ScalarizedVectorType *>(curType)) &&
        !ast.arrays.empty()) {
        bool wasUseConst = useConst;
        useConst = true;
        std::vector<unsigned> dimensions;
        dimensions.reserve(ast.arrays.size());
        for (auto &exp : ast.arrays) {
            exp->accept(*this);
            auto *lengthValue = dynamic_cast<ConstantInt *>(recentVal);
            if (!lengthValue || lengthValue->value_ <= 0) {
                std::cerr << "fixed vector array length must be a positive constant\n";
                std::exit(1);
            }
            dimensions.push_back((unsigned)lengthValue->value_);
        }
        useConst = wasUseConst;

        // Innermost element remains the fixed vector; outer dims nest ArrayType.
        Type *aggregateType = curType;
        for (int i = (int)dimensions.size() - 1; i >= 0; --i)
            aggregateType =
                module->get_array_type(aggregateType, dimensions[i]);

        if (scope.in_global()) {
            Constant *initializer = nullptr;
            if (!ast.initVal ||
                (ast.initVal->exp == nullptr && ast.initVal->initValList.empty())) {
                initializer = new ConstantZero(aggregateType);
            } else {
                bool oldConst = useConst;
                useConst = true;
                initializer = buildConstantVectorAggregate(
                    this, ast.initVal.get(), aggregateType);
                useConst = oldConst;
            }
            auto *global = new GlobalVariable(varName, module.get(), aggregateType,
                                              isConst, initializer);
            if (isConst)
                global->setSemFlag(SemFlag::ImmutableObject);
            scope.push(varName, global);
            return;
        }

        auto *slot = builder->create_alloca(aggregateType);
        slot->name_ = varName;
        if (isConst)
            slot->setSemFlag(SemFlag::SrcConstArray | SemFlag::ImmutableObject);
        scope.push(varName, slot);
        storeLocalVectorAggregate(this, slot, ast.initVal.get(), aggregateType);
        return;
    }
    //全局变量或常量
    if (scope.in_global()) {
        if (ast.arrays.empty()) {   //不是数组，即全局量
            if (auto *vectorType = dynamic_cast<VectorType *>(curType)) {
                bool oldConst = useConst;
                useConst = true;
                Value *initializer = buildVectorInitializer(this, ast.initVal.get(), vectorType);
                useConst = oldConst;
                auto *constant = dynamic_cast<Constant *>(initializer);
                if (!constant) {
                    std::cerr << "global vector initializer is not constant\n";
                    std::exit(1);
                }
                if (isConst)
                    scope.push(varName, constant);
                else
                    scope.push(varName, new GlobalVariable(varName, module.get(),
                                                           curType, false, constant));
                return;
            }
            if (auto *vectorType =
                    dynamic_cast<ScalarizedVectorType *>(curType)) {
                bool oldConst = useConst;
                useConst = true;
                auto *initializer = buildScalarizedVectorInitializer(
                    this, ast.initVal.get(), vectorType);
                useConst = oldConst;
                auto *constant = constantFromScalarizedVector(initializer);
                if (!constant) {
                    std::cerr << "global vector initializer is not constant\n";
                    std::exit(1);
                }
                if (isConst)
                    scope.push(varName, constant);
                else
                    scope.push(varName, new GlobalVariable(
                        varName, module.get(), curType, false, constant));
                return;
            }
            if (ast.initVal == nullptr) { //无初始化
                if (isConst) cout << "no initVal when define const!" << endl; //无初始化全局常量报错
                //无初始化全局量一定是变量
                GlobalVariable* var;
                if (curType == INT32_T)
                    var = new GlobalVariable(varName, module.get(), curType, false, CONST_INT(0));
                else var = new GlobalVariable(varName, module.get(), curType, false, CONST_FLOAT(0));
                scope.push(varName, var);
            } else { //有初始化
                useConst = true;
                ast.initVal->accept(*this);
                useConst = false;
                checkInitType();
                if (isConst) {
                    scope.push(varName, recentVal);   //单个常量定义不用new GlobalVariable
                } else { //全局变量
                    auto initializer = static_cast<Constant *>(recentVal);
                    GlobalVariable* var;
                    var = new GlobalVariable(varName, module.get(), curType, false, initializer);
                    scope.push(varName, var);
                }
            }
        } else {        //是数组，即全局数组量
            vector<int> dimensions;  //数组各维度; [2][3][4]对应
            useConst = true;
            //获取数组各维度
            for (auto &exp : ast.arrays) {
                exp->accept(*this);
                int dimension = dynamic_cast<ConstantInt*>(recentVal)->value_;
                dimensions.push_back(dimension);
            }
            useConst = false;
            vector<ArrayType*> arrayTys(dimensions.size()); //数组类型, {[2 x [3 x [4 x i32]]], [3 x [4 x i32]], [4 x i32]}
            for (int i = dimensions.size() - 1; i >= 0; i--) {
                if (i == dimensions.size() - 1) arrayTys[i] = module->get_array_type(curType, dimensions[i]);
                else arrayTys[i] = module->get_array_type(arrayTys[i + 1], dimensions[i]);
            }
            //无初始化或者初始化仅为大括号
            if (ast.initVal == nullptr || ast.initVal->initValList.empty()) {
                auto init = new ConstantZero(arrayTys[0]);
                auto var = new GlobalVariable(varName, module.get(), arrayTys[0], isConst, init);
                if (isConst) var->setSemFlag(SemFlag::ImmutableObject);
                scope.push(varName, var);
            } else {
                useConst = true; //全局数组量的初始值必为常量
                auto init = globalInit(dimensions, arrayTys, 0, ast.initVal->initValList);
                useConst = false;
                auto var = new GlobalVariable(varName, module.get(), arrayTys[0], isConst, init);
                if (isConst) var->setSemFlag(SemFlag::ImmutableObject);
                scope.push(varName, var);
            }
        }
        return;
    }


    //局部变量或常量
    if (ast.arrays.empty()) {   //不是数组，即普通局部量
        if (auto *vectorType = dynamic_cast<VectorType *>(curType)) {
            Value *initializer = buildVectorInitializer(this, ast.initVal.get(), vectorType);
            if (isConst) {
                scope.push(varName, initializer);
            } else {
                auto *slot = builder->create_alloca(curType);
                slot->name_ = varName;
                scope.push(varName, slot);
                builder->create_store(initializer, slot);
            }
            return;
        }
        if (auto *vectorType = dynamic_cast<ScalarizedVectorType *>(curType)) {
            auto *initializer = buildScalarizedVectorInitializer(
                this, ast.initVal.get(), vectorType);
            if (isConst) {
                scope.push(varName, initializer);
            } else {
                auto *slot = builder->create_alloca(curType);
                slot->name_ = varName;
                scope.push(varName, slot);
                storeScalarizedVector(this, slot, initializer, vectorType);
            }
            return;
        }
        if (ast.initVal == nullptr) {   //无初始化
            if (isConst) cout << "no initVal when define const!" << endl;   //无初始化局部常量报错
            else { //无初始化变量
                AllocaInst *varAlloca;
                varAlloca = builder->create_alloca(curType);
                varAlloca->name_ = varName;
                scope.push(varName, varAlloca);
            }
        } else { //有初始化
            ast.initVal->accept(*this);
            checkInitType();
            if (isConst) {
                scope.push(varName, recentVal);  //单个常量定义不用create_alloca
            } else {
                AllocaInst *varAlloca;
                varAlloca = builder->create_alloca(curType);
                varAlloca->name_ = varName;
                scope.push(varName, varAlloca);
                builder->create_store(recentVal, varAlloca);
            }
        }
    } else {    //局部数组量
        vector<int> dimensions(ast.arrays.size()), dimensionsCnt((ast.arrays.size()));  //数组各维度, [2][3][4]对应; 次维度数组元素个数, [24][12][4]
        int totalByte = 1; //存储总共的字节数
        useConst = true;
        //获取数组各维度
        for (int i = dimensions.size() - 1; i>= 0; i--) {
            ast.arrays[i]->accept(*this);
            int dimension = dynamic_cast<ConstantInt*>(recentVal)->value_;
            totalByte *= dimension;
            dimensions[i] = dimension;
            dimensionsCnt[i] = totalByte;
        }
        totalByte *= 4; //计算字节数
        useConst = false;
        ArrayType *arrayTy; //数组类型
        for (int i = dimensions.size() - 1; i >= 0; i--) {
            if (i == dimensions.size() - 1) arrayTy = module->get_array_type(curType, dimensions[i]);
            else arrayTy = module->get_array_type(arrayTy, dimensions[i]);
        }
        auto arrayAlloc = builder->create_alloca(arrayTy);
        arrayAlloc->name_ = varName;
        // 源级 const 数组：内容初始化后不再改写，打上不变性标记供后续优化消费。
        if (isConst)
            arrayAlloc->setSemFlag(SemFlag::SrcConstArray | SemFlag::ImmutableObject);
        scope.push(varName, arrayAlloc);
        if (ast.initVal == nullptr) { //无初始化
            if (isConst) cout << "no initVal when define const!" << endl;   //无初始化局部常量报错
            return; //无初始化变量数组无需再做处理
        }
        Value* i32P = builder->create_bitcast(arrayAlloc, INT32PTR_T);
        int elemCnt = totalByte / 4;

        // 对于大数组，使用 IR 循环逐元素清零，避免生成百万级指令导致编译超时
        // （19 维 [2][2]...[2] 有 2^19 = 524288 个元素）
        if (elemCnt > 256) {
            auto zeroCondBB = createNamedBB(module.get(), currentFunction, "zero.cond");
            auto zeroBodyBB = createNamedBB(module.get(), currentFunction, "zero.body");
            auto zeroEndBB  = createNamedBB(module.get(), currentFunction, "zero.end");

            // 在 entry 块中分配循环计数器并初始化
            auto idxAlloca = builder->create_alloca(INT32_T);
            builder->create_store(CONST_INT(0), idxAlloca);
            builder->create_br(zeroCondBB);

            // 循环条件块：idx < elemCnt ?
            builder->BB_ = zeroCondBB;
            auto idxLoad = builder->create_load(idxAlloca);
            auto cond = builder->create_icmp_lt(idxLoad, CONST_INT(elemCnt));
            builder->create_cond_br(cond, zeroBodyBB, zeroEndBB);

            // 循环体块：i32P[idx] = 0; idx = idx + 1;
            builder->BB_ = zeroBodyBB;
            auto idxLoad2 = builder->create_load(idxAlloca);
            auto gep = builder->create_gep(i32P, {idxLoad2});
            builder->create_store(CONST_INT(0), gep);
            auto idxLoad3 = builder->create_load(idxAlloca);
            auto idxInc = builder->create_iadd(idxLoad3, CONST_INT(1));
            builder->create_store(idxInc, idxAlloca);
            builder->create_br(zeroCondBB);

            // 清零完成，继续在 zeroEndBB 中初始化
            builder->BB_ = zeroEndBB;
        } else {
            for (int i = 0; i < elemCnt; i++) {
                auto gep = builder->create_gep(i32P, {CONST_INT(i)});
                builder->create_store(CONST_INT(0), gep);
            }
        }
        //数组初始化时，成员exp一定是空，若initValList也是空，即是大括号，已经置零了直接返回
        if (ast.initVal->initValList.empty()) return;
        vector<Value*> idxs(dimensions.size() + 1);
        for (int i = 0; i < dimensions.size() + 1; i++) {
            idxs[i] = CONST_INT(0);
        }
        Value* ptr = builder->create_gep(arrayAlloc, idxs); //获取数组开头地址
        localInit(ptr, ast.initVal->initValList, dimensionsCnt, 1);
    }
}

//嵌套大括号数组的维度，即倒数连续0的第一个。 如[0,1,0,0]，返回2；[0,0,0,1]，返回4；
//若全是0，[0,0,0,0],返回1
int GenIR::getNextDim(vector<int> &elementsCnts, int up) {
    for (int i = elementsCnts.size() - 1; i > up; i--) {
        if (elementsCnts[i] != 0) return i + 1;
    }
    return up + 1;
}

//增加元素后，合并所有能合并的数组元素，即对齐
void GenIR::mergeElements(vector<int> &dimensions, vector<ArrayType*> &arrayTys, int up, int dimAdd, vector<Constant*> &elements, vector<int> &elementsCnts) {
    for (int i = dimAdd; i > up; i--) {
        if (elementsCnts[i] % dimensions[i] == 0) {
            vector<Constant*> temp;
            temp.assign(elements.end() - dimensions[i], elements.end());
            elements.erase(elements.end() - dimensions[i], elements.end());
            elements.push_back(new ConstantArray(arrayTys[i], temp));
            elementsCnts[i] = 0;
            elementsCnts[i - 1]++;
        } else break;
    }
}

//最后合并所有元素，不足合并则填0元素，使得elements只剩下一个arrayTys[up]类型的最终数组
void GenIR::finalMerge(vector<int> &dimensions, vector<ArrayType*> &arrayTys, int up, vector<Constant*> &elements, vector<int> &elementsCnts) const {
    for (int i = dimensions.size() - 1; i >= up; i--) {
        while (elementsCnts[i] % dimensions[i] != 0) { //补充当前数组类型所需0元素
            if (i == dimensions.size() - 1) {
                if (curType == INT32_T) {
                    elements.push_back(CONST_INT(0));
                } else {
                    elements.push_back(CONST_FLOAT(0));
                }
            } else {
                elements.push_back(new ConstantZero(arrayTys[i + 1]));
            }
            elementsCnts[i]++;
        }
        if (elementsCnts[i] != 0) {
            vector<Constant*> temp;
            temp.assign(elements.end() - dimensions[i], elements.end());
            elements.erase(elements.end() - dimensions[i], elements.end());
            elements.push_back(new ConstantArray(arrayTys[i], temp));
            elementsCnts[i] = 0;
            if (i != up) elementsCnts[i - 1]++;
        }
    }
}

//生成变量数组的初始化
ConstantArray* GenIR::globalInit(vector<int> &dimensions, vector<ArrayType*> &arrayTys, int up, vector<unique_ptr<InitValAST>> &list) {
    vector<int> elementsCnts(dimensions.size()); //对应各个维度的子数组的元素个数
    vector<Constant*> elements; //各个元素
    int dimAdd;
    for (auto &val : list) {
        if (val->exp != nullptr) {
            dimAdd = dimensions.size() - 1;
            val->exp->accept(*this);
            checkInitType();
            elements.push_back((ConstantInt*)recentVal);
        } else {
            auto nextUp = getNextDim(elementsCnts, up); //该嵌套数组的维度
            dimAdd = nextUp - 1; //比他高一维度的数组需要添加一个元素
            if (nextUp == dimensions.size()) cout << "initial invalid" << endl;//没有连续0，没对齐，不合法
            if (val->initValList.empty()) {
                elements.push_back(new ConstantZero(arrayTys[nextUp]));
            } else {
                auto temp = globalInit(dimensions, arrayTys, nextUp, val->initValList);
                elements.push_back(temp);
            }
        }
        elementsCnts[dimAdd]++;
        mergeElements(dimensions, arrayTys, up, dimAdd, elements, elementsCnts);
    }
    finalMerge(dimensions, arrayTys, up, elements, elementsCnts);
    return static_cast<ConstantArray*>(elements[0]);
}


//根据初始化的量决定嵌套数组的维度
int GenIR::getNextDim(vector<int> &dimensionsCnt, int up, int cnt) {
    for (int i = up; i < dimensionsCnt.size(); i++) {
        if (cnt % dimensionsCnt[i] == 0) return i;
    }
    return 0;
}

//根据首指针递归初始化数组,up表示子数组的最高对齐位置，比如[4][2][4]，子数组最高对齐[2][4],up为1
void GenIR::localInit(Value* ptr, vector<unique_ptr<InitValAST>> &list, vector<int> &dimensionsCnt, int up) {
    int cnt = 0;
    Value* tempPtr = ptr;
    for (auto &initVal : list) {
        if (initVal->exp) {
            if (cnt == 0) cnt++; //第一次赋值时可以少一次create_gep
            else tempPtr = builder->create_gep(ptr, {CONST_INT(cnt++)});
            initVal->exp->accept(*this);
            checkInitType();
            builder->create_store(recentVal, tempPtr);
        } else {
            auto nextUp = getNextDim(dimensionsCnt, up, cnt);
            if (nextUp == 0) cout << "initial invalid!" << endl;
            if (!initVal->initValList.empty()) {
                if (cnt != 0) tempPtr = builder->create_gep(ptr, {CONST_INT(cnt)}); //没赋值过，那tempPtr实际就是ptr
                localInit(tempPtr, initVal->initValList, dimensionsCnt, nextUp);
            }
            cnt += dimensionsCnt[nextUp]; //数组初始化量一定增加这么多
        }
    }
}

void GenIR::visit(InitValAST &ast) {
    //不是数组则求exp的值，若是数组不会进入此函数
    if (ast.exp != nullptr) {
        ast.exp->accept(*this);
    }
}

void GenIR::visit(FuncDefAST &ast) {
    isNewFunc = true;
    params.clear();
    paramNames.clear();
    pendingScalarizedParameters.clear();
    currentScalarizedReturnType = nullptr;
    scalarizedReturnPointer = nullptr;
    Type *retType;
    if (ast.funcType.isDynamicVector()) {
        std::cerr << "dynamic vectors are non-owning views and cannot be returned by value\n";
        std::exit(1);
    } else if (ast.funcType == TYPE_VOID) {
        retType = VOID_T;
    } else {
        Type *lowered = lowerFixedType(this, ast.funcType);
        if (auto *scalarized =
                dynamic_cast<ScalarizedVectorType *>(lowered)) {
            currentScalarizedReturnType = scalarized;
            retType = VOID_T;
            params.push_back(module->get_pointer_type(scalarized));
            paramNames.push_back("$vector.return");
            pendingScalarizedParameters.push_back(nullptr);
        } else {
            retType = lowered;
        }
    }

    for (auto &funcFParam : ast.funcFParamList)
        funcFParam->accept(*this);

    auto funTy = new FunctionType(retType, params);
    auto func = new Function(funTy, *ast.id, module.get());
    currentFunction = func;
    ScalarizedFunctionInfo info;
    info.returnType = currentScalarizedReturnType;
    unsigned hiddenParameters = currentScalarizedReturnType ? 1U : 0U;
    info.valueParameters.assign(
        pendingScalarizedParameters.begin() + hiddenParameters,
        pendingScalarizedParameters.end());
    scalarizedFunctionInfo[func] = std::move(info);
    scope.push(*ast.id, func);
    scope.enter();

    std::vector<Value *> args;
    for (auto arg = func->arguments_.begin(); arg != func->arguments_.end(); ++arg)
        args.push_back(*arg);

    auto *entry = createNamedBB(module.get(), func, "entry");
    builder->BB_ = entry;

    // 标量和原生向量形参保存在局部槽中；展开向量先复制各 lane，保持按值语义。
    for (int i = 0; i < (int)paramNames.size(); i++) {
        if (currentScalarizedReturnType && i == 0) {
            scalarizedReturnPointer = args[i];
            Value *zero = zeroScalar(module.get(),
                                     currentScalarizedReturnType->contained_);
            storeScalarizedVector(
                this, scalarizedReturnPointer,
                new ScalarizedVectorValue(
                    currentScalarizedReturnType,
                    std::vector<Value *>(
                        currentScalarizedReturnType->num_elements_, zero)),
                currentScalarizedReturnType);
            continue;
        }
        if (pendingScalarizedParameters[i]) {
            auto *type = pendingScalarizedParameters[i];
            auto *alloc = builder->create_alloca(type);
            alloc->name_ = paramNames[i];
            scope.push(paramNames[i], alloc);
            storeScalarizedVector(this, alloc,
                                  loadScalarizedVector(this, args[i], type),
                                  type);
            continue;
        }
        auto *alloc = builder->create_alloca(params[i]);
        alloc->name_ = paramNames[i];
        scope.push(paramNames[i], alloc);
        builder->create_store(args[i], alloc);
    }

    // 统一返回块；非 void 在 entry 建 %retval 并默认置 0
    retBB = createNamedBB(module.get(), func, "return");
    retAlloca = nullptr;
    if (retType == VOID_T) {
        builder->BB_ = retBB;
        builder->create_void_ret();
    } else {
        retAlloca = builder->create_alloca(retType);
        retAlloca->name_ = "retval";
        Value *zero;
        if (retType->tid_ == Type::VectorTyID)
            zero = new ConstantZero(retType);
        else
            zero = (retType == FLOAT_T) ? (Value *)CONST_FLOAT(0.0f)
                                        : (Value *)CONST_INT(0);
        builder->create_store(zero, retAlloca);
        builder->BB_ = retBB;
        builder->create_ret(builder->create_load(retAlloca));
    }

    builder->BB_ = entry;
    has_br = false;
    ast.block->accept(*this);

    if (!builder->BB_->get_terminator())
        builder->create_br(retBB);

    tryMergeTrivialRetBlock(func, retBB, retAlloca);
    retBB = nullptr;
    retAlloca = nullptr;
    currentScalarizedReturnType = nullptr;
    scalarizedReturnPointer = nullptr;
}

void GenIR::visit(FuncFParamAST &ast) {
    //获取参数类型
    Type *paramType;
    if (ast.bType.isDynamicVector()) {
        if (!ast.isArray) {
            std::cerr << "a dynamic vector parameter must use view syntax name[]\n";
            std::exit(1);
        }
        paramType = scalarType(module.get(), ast.bType.element);
    } else {
        paramType = lowerFixedType(this, ast.bType);
    }
    ScalarizedVectorType *scalarizedValueParameter = nullptr;
    if (auto *scalarized = dynamic_cast<ScalarizedVectorType *>(paramType);
        scalarized && !ast.isArray) {
        scalarizedValueParameter = scalarized;
        paramType = module->get_pointer_type(scalarized);
    }
    //是否为数组
    if (ast.isArray) {
        useConst = true; //数组维度是整型常量
        for (int i = ast.arrays.size() - 1; i >= 0; i--) {
            ast.arrays[i]->accept(*this);
            paramType = module->get_array_type(paramType, ((ConstantInt *)recentVal)->value_);
        }
        useConst = false;
        //如int a[][2]，则参数为[2 x i32]* ;  int a[],参数为i32 *
        paramType = module->get_pointer_type(paramType);
    }
    params.push_back(paramType);
    paramNames.push_back(*ast.id);
    pendingScalarizedParameters.push_back(scalarizedValueParameter);
}

void GenIR::visit(BlockAST &ast) {
    //如果是一个新的函数，则不用再进入一个新的作用域
    if (isNewFunc)
        isNewFunc = false;
    //其它情况，需要进入一个新的作用域
    else {
        scope.enter();
    }
    //遍历每一个语句块
    for (auto &item : ast.blockItemList) {
        if (has_br) break;     //此BB已经出现了br，后续指令无效
        item->accept(*this);
    }

    scope.exit();
}

void GenIR::visit(BlockItemAST &ast) {
    if (ast.decl != nullptr) {
        ast.decl->accept(*this);
    } else {
        ast.stmt->accept(*this);
    }
}

void GenIR::visit(StmtAST &ast) {
    switch (ast.sType) {
        case SEMI:
            break;
        case ASS: {
            is_single_exp = true;
            vectorLaneBase = nullptr;
            vectorLaneIndex = nullptr;
            requireLVal = true;  
            ast.lVal->accept(*this);
            auto var = recentVal;
            ScalarizedVectorType *scalarizedType = nullptr;
            if (var && var->type_->tid_ == Type::PointerTyID)
                scalarizedType = dynamic_cast<ScalarizedVectorType *>(
                    static_cast<PointerType *>(var->type_)->contained_);
            if (scalarizedType) {
                ScalarizedVectorValue *initializer = nullptr;
                if (ast.initVal) {
                    initializer = buildScalarizedVectorInitializer(
                        this, ast.initVal.get(), scalarizedType);
                } else {
                    ScalarizedVectorType *saved =
                        expectedScalarizedVectorType;
                    expectedScalarizedVectorType = scalarizedType;
                    ast.exp->accept(*this);
                    expectedScalarizedVectorType = saved;
                    initializer = asScalarizedVector(recentVal);
                }
                storeScalarizedVector(this, var, initializer, scalarizedType);
                break;
            }
            if (ast.initVal) {
                if (vectorLaneBase || !var ||
                    var->type_->tid_ != Type::PointerTyID) {
                    std::cerr << "a braced initializer requires a whole fixed vector lvalue\n";
                    std::exit(1);
                }
                auto *vectorType = dynamic_cast<VectorType *>(
                    static_cast<PointerType *>(var->type_)->contained_);
                if (!vectorType) {
                    std::cerr << "a braced initializer requires a whole fixed vector lvalue\n";
                    std::exit(1);
                }
                Value *initializer =
                    buildVectorInitializer(this, ast.initVal.get(), vectorType);
                builder->create_store(initializer, var);
                vectorLaneBase = nullptr;
                vectorLaneIndex = nullptr;
                break;
            }
            {
                VectorType *savedExpected = expectedVectorType;
                if (var && var->type_->tid_ == Type::PointerTyID)
                    expectedVectorType = dynamic_cast<VectorType *>(
                        static_cast<PointerType *>(var->type_)->contained_);
                ast.exp->accept(*this);
                expectedVectorType = savedExpected;
            }
            auto expval = recentVal;
            if (vectorLaneBase) {
                auto *vectorType = static_cast<VectorType *>(
                    static_cast<PointerType *>(vectorLaneBase->type_)->contained_);
                expval = coerceScalar(builder, module.get(), expval,
                                      vectorType->contained_);
                auto *oldVector = builder->create_load(vectorLaneBase);
                auto *newVector = new InsertElementInst(oldVector, expval,
                                                        vectorLaneIndex,
                                                        builder->BB_);
                builder->create_store(newVector, vectorLaneBase);
                vectorLaneBase = nullptr;
                vectorLaneIndex = nullptr;
                break;
            }
            Type* varElemType = static_cast<PointerType*>(var->type_)->contained_;
            if (varElemType == FLOAT_T && expval->type_ == INT32_T) {
                expval = builder->create_sitofp(expval, FLOAT_T);
            } else if (varElemType == INT32_T && expval->type_ == FLOAT_T) {
                expval = builder->create_fptosi(expval, INT32_T);
            }
            builder->create_store(expval, var);
            break;
        }
        case EXP:
            is_single_exp = true;
            ast.exp->accept(*this);
            is_single_exp = false;
            break;
        case CONT:
            builder->create_br(whileCondBB);
            has_br = true;
            break;
        case BRE:
            builder->create_br(whileFalseBB);
            has_br = true;
            break;
        case RET:
            ast.returnStmt->accept(*this);
            break;
        case BLK:
            ast.block->accept(*this);
            break;
        case SEL:
            ast.selectStmt->accept(*this);
            break;
        case ITER:
            ast.iterationStmt->accept(*this);
            break;
    }

}

void GenIR::visit(ReturnStmtAST &ast) {
    if (currentScalarizedReturnType) {
        if (ast.exp) {
            ScalarizedVectorType *saved = expectedScalarizedVectorType;
            expectedScalarizedVectorType = currentScalarizedReturnType;
            ast.exp->accept(*this);
            expectedScalarizedVectorType = saved;
            storeScalarizedVector(this, scalarizedReturnPointer,
                                  asScalarizedVector(recentVal),
                                  currentScalarizedReturnType);
        }
        recentVal = builder->create_br(retBB);
        has_br = true;
        return;
    }
    if (ast.exp != nullptr) {
        VectorType *savedExpected = expectedVectorType;
        expectedVectorType =
            dynamic_cast<VectorType *>(currentFunction->get_return_type());
        ast.exp->accept(*this);
        expectedVectorType = savedExpected;
        Value *retVal = coerceToReturnType(builder, recentVal,
                                           currentFunction->get_return_type(),
                                           INT32_T, FLOAT_T);
        builder->create_store(retVal, retAlloca);
    }
    recentVal = builder->create_br(retBB);
    has_br = true;
}

void GenIR::visit(SelectStmtAST &ast) {
    //先保存trueBB和falseBB，防止嵌套导致返回上一层后丢失块的地址
    auto tempTrue = trueBB;
    auto tempFalse = falseBB;

    trueBB = createNamedBB(module.get(), currentFunction, "if.then");
    BasicBlock *nextIf;
    if (ast.elseStmt == nullptr) {
        // 无 else：假出口就是汇合点
        falseBB = createNamedBB(module.get(), currentFunction, "if.end");
        nextIf = falseBB;
    } else {
        falseBB = createNamedBB(module.get(), currentFunction, "if.else");
        nextIf = createNamedBB(module.get(), currentFunction, "if.end");
    }
    bool nextIfReachable = false;
    ast.cond->accept(*this);
    //检查是否是i1，不是则进行比较
    if (recentVal->type_ == INT32_T) {
        recentVal = builder->create_icmp_ne(recentVal, CONST_INT(0));
    } else if (recentVal->type_ == FLOAT_T) {
        recentVal = builder->create_fcmp_ne(recentVal, CONST_FLOAT(0));
    }
    builder->create_cond_br(recentVal, trueBB, falseBB);

    builder->BB_ = trueBB; //开始构建trueBB
    has_br = false;
    ast.ifStmt->accept(*this);
    if (!builder->BB_->get_terminator()) {
        builder->create_br(nextIf);
        nextIfReachable = true;
    }

    if (ast.elseStmt != nullptr) { // 开始构建falseBB
        builder->BB_ = falseBB;
        has_br = false;
        ast.elseStmt->accept(*this);
        if (!builder->BB_->get_terminator()) {
            builder->create_br(nextIf);
            nextIfReachable = true;
        }
    }

    // 检查 bb 的分支指令是否跳转到 target
    auto branchesTo = [](BasicBlock *bb, BasicBlock *target) -> bool {
        auto *term = bb->get_terminator();
        if (!term) return false;
        for (unsigned i = 0; i < term->num_ops_; i++)
            if (term->get_operand(i) == target) return true;
        return false;
    };

    // 如果两个分支都提前终止（没有 br nextIf）且 nextIf 是独立块，则 nextIf 不可达
    if (ast.elseStmt != nullptr && !nextIfReachable &&
        !branchesTo(trueBB, nextIf) && !branchesTo(falseBB, nextIf)) {
        currentFunction->remove_bb(nextIf);
        has_br = true;
    } else {
        builder->BB_ = nextIf;
        has_br = false;
    }
    // 还原trueBB和falseBB
    trueBB = tempTrue;
    falseBB = tempFalse;
}

void GenIR::visit(IterationStmtAST &ast) {
    //先保存trueBB和falseBB，防止嵌套导致返回上一层后丢失块的地址
    auto tempTrue = trueBB;
    auto tempFalse = falseBB; //即while的next block
    auto tempCond = whileCondBB;
    auto tempWhileFalseBB = whileFalseBB; //break只跳while的false，而不跳全局false

    whileCondBB = createNamedBB(module.get(), currentFunction, "while.cond");
    trueBB = createNamedBB(module.get(), currentFunction, "while.body");
    falseBB = createNamedBB(module.get(), currentFunction, "while.end");
    whileFalseBB = falseBB;

    builder->create_br(whileCondBB);
    builder->BB_ = whileCondBB; //条件也是一个基本块
    has_br = false;
    ast.cond->accept(*this);
    if (recentVal->type_ == INT32_T) {
        recentVal = builder->create_icmp_ne(recentVal, CONST_INT(0));
    } else if (recentVal->type_ == FLOAT_T) {
        recentVal = builder->create_fcmp_ne(recentVal, CONST_FLOAT(0.0));
    }
    builder->create_cond_br(recentVal, trueBB, falseBB);

    builder->BB_ = trueBB;
    has_br = false;
    ast.stmt->accept(*this);
    //while语句体一定是跳回cond
    if (!builder->BB_->get_terminator()) {
        builder->create_br(whileCondBB);
    }

    builder->BB_ = falseBB;
    has_br = false;

    //还原trueBB，falseBB，tempCond
    trueBB = tempTrue;
    falseBB = tempFalse;
    whileCondBB = tempCond;
    whileFalseBB = tempWhileFalseBB;
}

//根据待计算的两个Constant的类型，求出对应的值赋值到intVal，floatVal中，返回计算结果是否为int
bool GenIR::checkCalType(Value* val[],int intVal[], float floatVal[]) {
    bool resultIsInt = false;
    if (dynamic_cast<ConstantInt*>(val[0]) && dynamic_cast<ConstantInt*>(val[1])) {
        resultIsInt = true;
        intVal[0] = dynamic_cast<ConstantInt*>(val[0])->value_;
        intVal[1] = dynamic_cast<ConstantInt*>(val[1])->value_;
    } else { //操作结果一定是float
        if (dynamic_cast<ConstantInt*>(val[0])) floatVal[0] = dynamic_cast<ConstantInt*>(val[0])->value_;
        else floatVal[0] = dynamic_cast<ConstantFloat*>(val[0])->value_;
        if (dynamic_cast<ConstantInt*>(val[1])) floatVal[1] = dynamic_cast<ConstantInt*>(val[1])->value_;
        else floatVal[1] = dynamic_cast<ConstantFloat*>(val[1])->value_;
    }
    return resultIsInt;
}

//根据待计算的两个寄存器数的类型，若需要转换类型输出转换指令
void GenIR::checkCalType(Value* val[]) {
    if (val[0]->type_ == INT1_T) {
        val[0] = builder->create_zext(val[0], INT32_T);
    }
    if (val[1]->type_ == INT1_T) {
        val[1] = builder->create_zext(val[1], INT32_T);
    }
    if (val[0]->type_ == INT32_T && val[1]->type_ == FLOAT_T) {
        val[0] = builder->create_sitofp(val[0], FLOAT_T);
    }
    if (val[1]->type_ == INT32_T && val[0]->type_ == FLOAT_T) {
        val[1] = builder->create_sitofp(val[1], FLOAT_T);
    }
    auto *leftVector = dynamic_cast<VectorType *>(val[0]->type_);
    auto *rightVector = dynamic_cast<VectorType *>(val[1]->type_);
    if (leftVector && !rightVector) {
        val[1] = splatScalar(builder, module.get(), val[1], leftVector);
        rightVector = leftVector;
    } else if (!leftVector && rightVector) {
        val[0] = splatScalar(builder, module.get(), val[0], rightVector);
        leftVector = rightVector;
    }
    if (leftVector && rightVector && leftVector != rightVector) {
        std::cerr << "vector operands must have the same element type and width\n";
        std::exit(1);
    }
}

void GenIR::visit(AddExpAST &ast) {
    if (ast.addExp == nullptr) {
        ast.mulExp->accept(*this);
        return;
    }

    Value* val[2]; //lVal, rVal
    ast.addExp->accept(*this);
    val[0] = recentVal;
    VectorType *savedExpected = expectedVectorType;
    ScalarizedVectorType *savedScalarizedExpected =
        expectedScalarizedVectorType;
    if (auto *lhsVector = dynamic_cast<VectorType *>(val[0]->type_))
        expectedVectorType = lhsVector;
    if (auto *lhsVector = asScalarizedVector(val[0]))
        expectedScalarizedVectorType =
            static_cast<ScalarizedVectorType *>(lhsVector->type_);
    ast.mulExp->accept(*this);
    expectedVectorType = savedExpected;
    expectedScalarizedVectorType = savedScalarizedExpected;
    val[1] = recentVal;

    if (asScalarizedVector(val[0]) || asScalarizedVector(val[1])) {
        recentVal = scalarizedVectorBinary(
            this, val[0], val[1],
            ast.op == AOP_ADD ? Instruction::Add : Instruction::Sub,
            ast.op == AOP_ADD ? Instruction::FAdd : Instruction::FSub);
        return;
    }

    //若都是常量
    if (useConst && val[0]->type_->tid_ != Type::VectorTyID &&
        val[1]->type_->tid_ != Type::VectorTyID) {
        int intVal[3]; //lInt, rInt, relInt;
        float floatVal[3]; // lFloat, rFloat, relFloat;
        bool resultIsInt = checkCalType(val, intVal, floatVal);
        switch (ast.op) {
            case AOP_ADD:
                intVal[2] = intVal[0] + intVal[1];
                floatVal[2] = floatVal[0] + floatVal[1];
                break;
            case AOP_MINUS:
                intVal[2] = intVal[0] - intVal[1];
                floatVal[2] = floatVal[0] - floatVal[1];
                break;
        }
        if (resultIsInt) recentVal = CONST_INT(intVal[2]);
        else recentVal = CONST_FLOAT(floatVal[2]);
        return;
    }

    //若不是常量，进行计算，输出指令
    checkCalType(val);
    if (useConst) {
        auto *lhs = dynamic_cast<ConstantVector *>(val[0]);
        auto *rhs = dynamic_cast<ConstantVector *>(val[1]);
        Instruction::OpID op = ast.op == AOP_ADD ? Instruction::Add
                                                  : Instruction::Sub;
        if (lhs && lhs->type_->tid_ == Type::VectorTyID &&
            static_cast<VectorType *>(lhs->type_)->contained_ == FLOAT_T)
            op = ast.op == AOP_ADD ? Instruction::FAdd : Instruction::FSub;
        if (lhs && rhs) {
            recentVal = foldConstantVector(module.get(), op, lhs, rhs);
            if (recentVal)
                return;
        }
    }
    if (isIntegerValueType(val[0]->type_)) {
        switch (ast.op) {
            case AOP_ADD:
                recentVal = builder->create_iadd(val[0], val[1]);
                break;
            case AOP_MINUS:
                recentVal = builder->create_isub(val[0], val[1]);
                break;
        }
    } else {
        switch (ast.op) {
            case AOP_ADD:
                recentVal = builder->create_fadd(val[0], val[1]);
                break;
            case AOP_MINUS:
                recentVal = builder->create_fsub(val[0], val[1]);
                break;
        }
    }
}

void GenIR::visit(MulExpAST &ast) {
    if (ast.mulExp == nullptr) {
        ast.unaryExp->accept(*this);
        return;
    }

    Value* val[2]; //lVal, rVal
    ast.mulExp->accept(*this);
    val[0] = recentVal;
    VectorType *savedExpected = expectedVectorType;
    ScalarizedVectorType *savedScalarizedExpected =
        expectedScalarizedVectorType;
    if (auto *lhsVector = dynamic_cast<VectorType *>(val[0]->type_))
        expectedVectorType = lhsVector;
    if (auto *lhsVector = asScalarizedVector(val[0]))
        expectedScalarizedVectorType =
            static_cast<ScalarizedVectorType *>(lhsVector->type_);
    ast.unaryExp->accept(*this);
    expectedVectorType = savedExpected;
    expectedScalarizedVectorType = savedScalarizedExpected;
    val[1] = recentVal;

    if (asScalarizedVector(val[0]) || asScalarizedVector(val[1])) {
        Instruction::OpID integerOp = Instruction::Mul;
        Instruction::OpID floatingOp = Instruction::FMul;
        if (ast.op == MOP_DIV) {
            integerOp = Instruction::SDiv;
            floatingOp = Instruction::FDiv;
        } else if (ast.op == MOP_MOD) {
            integerOp = Instruction::SRem;
            if (auto *vector = asScalarizedVector(val[0]);
                (vector && static_cast<ScalarizedVectorType *>(vector->type_)
                               ->contained_ != INT32_T) ||
                (!vector && static_cast<ScalarizedVectorType *>(
                                asScalarizedVector(val[1])->type_)
                                ->contained_ != INT32_T)) {
                std::cerr << "remainder is only valid for integer vectors\n";
                std::exit(1);
            }
        }
        recentVal = scalarizedVectorBinary(this, val[0], val[1], integerOp,
                                           floatingOp);
        return;
    }

    //若都是常量
    if (useConst && val[0]->type_->tid_ != Type::VectorTyID &&
        val[1]->type_->tid_ != Type::VectorTyID) {
        int intVal[3]; //lInt, rInt, relInt;
        float floatVal[3]; // lFloat, rFloat, relFloat;
        bool resultIsInt = checkCalType(val, intVal, floatVal);
        switch (ast.op) {
            case MOP_MUL:
                intVal[2] = intVal[0] * intVal[1];
                floatVal[2] = floatVal[0] * floatVal[1];
                break;
            case MOP_DIV:
                intVal[2] = intVal[0] / intVal[1];
                floatVal[2] = floatVal[0] / floatVal[1];
                break;
            case MOP_MOD:
                intVal[2] = intVal[0] % intVal[1];
                break;
        }
        if (resultIsInt) recentVal = CONST_INT(intVal[2]);
        else recentVal = CONST_FLOAT(floatVal[2]);
        return;
    }

    //若不是常量，进行计算，输出指令
    checkCalType(val);
    if (useConst) {
        auto *lhs = dynamic_cast<ConstantVector *>(val[0]);
        auto *rhs = dynamic_cast<ConstantVector *>(val[1]);
        Instruction::OpID op = Instruction::Mul;
        if (lhs && static_cast<VectorType *>(lhs->type_)->contained_ == FLOAT_T) {
            op = ast.op == MOP_MUL ? Instruction::FMul : Instruction::FDiv;
        } else if (ast.op == MOP_DIV) {
            op = Instruction::SDiv;
        } else if (ast.op == MOP_MOD) {
            op = Instruction::SRem;
        }
        if (lhs && rhs) {
            recentVal = foldConstantVector(module.get(), op, lhs, rhs);
            if (recentVal)
                return;
        }
    }
    if (isIntegerValueType(val[0]->type_)) {
        auto *vectorType = dynamic_cast<VectorType *>(val[0]->type_);
        if (vectorType && (ast.op == MOP_DIV || ast.op == MOP_MOD)) {
            recentVal = scalarizeIntegerVectorBinary(
                builder, module.get(), val[0], val[1],
                ast.op == MOP_DIV ? Instruction::SDiv : Instruction::SRem,
                vectorType);
            return;
        }
        switch (ast.op) {
            case MOP_MUL:
                recentVal = builder->create_imul(val[0], val[1]);
                break;
            case MOP_DIV:
                recentVal = builder->create_isdiv(val[0], val[1]);
                break;
            case MOP_MOD:
                recentVal = builder->create_isrem(val[0], val[1]);
                break;
        }
    }
    else {
        switch (ast.op) {
            case MOP_MUL:
                recentVal = builder->create_fmul(val[0], val[1]);
                break;
            case MOP_DIV:
                recentVal = builder->create_fdiv(val[0], val[1]);
                break;
            case MOP_MOD://never occur
                break;
        }
    }
}

void GenIR::visit(UnaryExpAST &ast) {
    // Postfix lane extract on any vector-producing unary expression.
    if (ast.subscript) {
        ast.unaryExp->accept(*this);
        Value *base = recentVal;
        if (auto *vector = asScalarizedVector(base)) {
            auto *type = static_cast<ScalarizedVectorType *>(vector->type_);
            ast.subscript->accept(*this);
            Value *index = recentVal;
            if (auto *constantIndex = dynamic_cast<ConstantInt *>(index)) {
                if (constantIndex->value_ < 0 ||
                    static_cast<unsigned>(constantIndex->value_) >=
                        type->num_elements_) {
                    std::cerr << "constant vector lane index is out of range\n";
                    std::exit(1);
                }
                recentVal = vector->lanes_[constantIndex->value_];
                return;
            }
            auto *slot = builder->create_alloca(type);
            storeScalarizedVector(this, slot, vector, type);
            Value *lanePointer = builder->create_gep(
                slot, {CONST_INT(0), index});
            recentVal = builder->create_load(lanePointer);
            return;
        }
        if (auto *pointer = dynamic_cast<PointerType *>(base->type_)) {
            if (pointer->contained_->tid_ == Type::VectorTyID)
                base = builder->create_load(base);
        }
        auto *vectorType = dynamic_cast<VectorType *>(base->type_);
        if (!vectorType) {
            std::cerr << "lane extract requires a fixed vector value\n";
            std::exit(1);
        }
        ast.subscript->accept(*this);
        Value *index = recentVal;
        if (auto *constantIndex = dynamic_cast<ConstantInt *>(index)) {
            if (constantIndex->value_ < 0 ||
                (unsigned)constantIndex->value_ >= vectorType->num_elements_) {
                std::cerr << "constant vector lane index is out of range\n";
                std::exit(1);
            }
            if (auto *constantVector = dynamic_cast<ConstantVector *>(base)) {
                recentVal = constantVector->elements_[constantIndex->value_];
                return;
            }
        }
        recentVal = new ExtractElementInst(base, index, builder->BB_);
        return;
    }

    // 为常量算式
    if (useConst) {
        if (ast.primaryExp) {
            ast.primaryExp->accept(*this);
        } else if (ast.unaryExp) {
            ast.unaryExp->accept(*this);
            if (auto *vector = asScalarizedVector(recentVal)) {
                recentVal = scalarizedVectorUnary(this, vector, ast.op);
                return;
            }
            if (ast.op == UOP_MINUS) {
                if (auto *vector = dynamic_cast<ConstantVector *>(recentVal)) {
                    auto *type = static_cast<VectorType *>(vector->type_);
                    std::vector<Constant *> lanes;
                    for (auto *lane : vector->elements_) {
                        if (auto *integer = dynamic_cast<ConstantInt *>(lane)) {
                            lanes.push_back(new ConstantInt(type->contained_,
                                                            -integer->value_));
                        } else {
                            auto *floating = dynamic_cast<ConstantFloat *>(lane);
                            lanes.push_back(new ConstantFloat(
                                type->contained_, -floating->value_));
                        }
                    }
                    recentVal = new ConstantVector(type, lanes);
                } else if (dynamic_cast<ConstantInt*>(recentVal)) {
                    auto temp = (ConstantInt*)recentVal;
                    temp->value_ = -temp->value_;
                    recentVal = temp;
                } else {
                    auto temp = (ConstantFloat*)recentVal;
                    temp->value_ = -temp->value_;
                    recentVal = temp;
                }
            }
        } else {
            cout << "Function call in ConstExp!" << endl;
        }
        return;
    }


    //不是常量算式
    if (ast.primaryExp != nullptr) {
        ast.primaryExp->accept(*this);
    } else if (ast.call != nullptr) {
        ast.call->accept(*this);
    } else {
        ast.unaryExp->accept(*this);
        if (auto *vector = asScalarizedVector(recentVal)) {
            recentVal = scalarizedVectorUnary(this, vector, ast.op);
            return;
        }
        if (recentVal->type_ == VOID_T)
            return;
        else if (recentVal->type_ == INT1_T) // INT1-->INT32
            recentVal = builder->create_zext(recentVal, INT32_T);

        if (isIntegerValueType(recentVal->type_)) {
            switch (ast.op) {
                case UOP_MINUS:
                    recentVal = builder->create_isub(
                        recentVal->type_->tid_ == Type::VectorTyID
                            ? (Value *)new ConstantZero(recentVal->type_)
                            : (Value *)CONST_INT(0),
                        recentVal);
                    break;
                case UOP_NOT:
                    recentVal = builder->create_icmp_eq(recentVal, CONST_INT(0));
                    break;
                case UOP_ADD:
                    break;
            }
        } else {
            switch (ast.op) {
                case UOP_MINUS:
                    recentVal = builder->create_fsub(
                        recentVal->type_->tid_ == Type::VectorTyID
                            ? (Value *)new ConstantZero(recentVal->type_)
                            : (Value *)CONST_FLOAT(0),
                        recentVal);
                    break;
                case UOP_NOT:
                    recentVal = builder->create_fcmp_eq(recentVal, CONST_FLOAT(0));
                    break;
                case UOP_ADD:
                    break;
            }
        }
    }
}

void GenIR::visit(PrimaryExpAST &ast) {
    if (ast.exp) {
        ast.exp->accept(*this);
    } else if (ast.lval) {
        ast.lval->accept(*this);
    } else if (ast.number) {
        ast.number->accept(*this);
    } else if (ast.initVal) {
        if (expectedScalarizedVectorType) {
            recentVal = buildScalarizedVectorInitializer(
                this, ast.initVal.get(), expectedScalarizedVectorType);
            return;
        }
        VectorType *vectorType = inferVectorLiteralType(module.get(),
                                                        ast.initVal.get());
        recentVal = buildVectorInitializer(this, ast.initVal.get(), vectorType);
    }
}

void GenIR::visit(LValAST &ast) {
    bool isTrueLVal = requireLVal; //是否真是作为左值
    requireLVal = false;
    auto var = scope.find(*ast.id);
    if (auto *vector = asScalarizedVector(var)) {
        if (ast.arrays.empty()) {
            recentVal = vector;
            return;
        }
        if (ast.arrays.size() != 1) {
            std::cerr << "a fixed vector requires exactly one lane index\n";
            std::exit(1);
        }
        ast.arrays[0]->accept(*this);
        auto *index = dynamic_cast<ConstantInt *>(recentVal);
        if (index && (index->value_ < 0 ||
            static_cast<unsigned>(index->value_) >= vector->lanes_.size())) {
            std::cerr << "constant vector lane index is out of range\n";
            std::exit(1);
        }
        if (index) {
            recentVal = vector->lanes_[index->value_];
            return;
        }
        if (isTrueLVal) {
            std::cerr << "cannot assign to a compiler-constant vector\n";
            std::exit(1);
        }
        Value *dynamicIndex = recentVal;
        auto *type = static_cast<ScalarizedVectorType *>(vector->type_);
        auto *slot = builder->create_alloca(type);
        storeScalarizedVector(this, slot, vector, type);
        recentVal = builder->create_load(
            builder->create_gep(slot, {CONST_INT(0), dynamicIndex}));
        return;
    }
    if (auto *constant = dynamic_cast<ConstantArray *>(var)) {
        if (auto *type = dynamic_cast<ScalarizedVectorType *>(constant->type_)) {
            auto *vector = constantScalarizedVector(module.get(), type, constant);
            if (ast.arrays.empty()) {
                recentVal = vector;
                return;
            }
            if (ast.arrays.size() != 1) {
                std::cerr << "a fixed vector requires exactly one lane index\n";
                std::exit(1);
            }
            ast.arrays[0]->accept(*this);
            auto *index = dynamic_cast<ConstantInt *>(recentVal);
            if (index && (index->value_ < 0 ||
                static_cast<unsigned>(index->value_) >= vector->lanes_.size())) {
                std::cerr << "constant vector lane index is out of range\n";
                std::exit(1);
            }
            if (index) {
                recentVal = vector->lanes_[index->value_];
                return;
            }
            if (scope.in_global() || isTrueLVal) {
                std::cerr << "a global constant expression requires a constant lane index\n";
                std::exit(1);
            }
            Value *dynamicIndex = recentVal;
            auto *slot = builder->create_alloca(type);
            storeScalarizedVector(this, slot, vector, type);
            recentVal = builder->create_load(
                builder->create_gep(slot, {CONST_INT(0), dynamicIndex}));
            return;
        }
    }
    //全局作用域内，一定使用常量，全局作用域下访问到LValAST，那么use_const一定被置为了true
    if (scope.in_global()) {
        //不是数组，直接返回该常量
        if (ast.arrays.empty()) {
            recentVal = var;
            return;
        }
        if (auto *vector = dynamic_cast<ConstantVector *>(var)) {
            if (ast.arrays.size() != 1) {
                std::cerr << "a fixed vector requires exactly one lane index\n";
                std::exit(1);
            }
            ast.arrays[0]->accept(*this);
            auto *index = dynamic_cast<ConstantInt *>(recentVal);
            if (!index || index->value_ < 0 ||
                (unsigned)index->value_ >= vector->elements_.size()) {
                std::cerr << "constant vector lane index is out of range\n";
                std::exit(1);
            }
            recentVal = vector->elements_[index->value_];
            return;
        }
        //若是数组，则var一定是全局常量数组
        vector<int> index;
        for (auto &exp : ast.arrays) {
            exp->accept(*this);
            index.push_back(dynamic_cast<ConstantInt*>(recentVal)->value_);
        }
        recentVal = ((GlobalVariable *)var)->init_val_; //使用var的初始化数组查找常量元素
        for (int i : index) {
            //某数组元素为ConstantZero，则该数一定是0
            if (dynamic_cast<ConstantZero*>(recentVal)) {
                Type* arrayTy = recentVal->type_;
                //找数组元素标签
                while (dynamic_cast<ArrayType*>(arrayTy)) {
                    arrayTy = dynamic_cast<ArrayType*>(arrayTy)->contained_;
                }
                if (arrayTy == INT32_T) recentVal = CONST_INT(0);
                else recentVal = CONST_FLOAT(0);
                return;
            }
            if (dynamic_cast<ConstantArray*>(recentVal)) {
                recentVal = ((ConstantArray*)recentVal)->const_array[i];
            } else if (auto *vector = dynamic_cast<ConstantVector *>(recentVal)) {
                if (i < 0 || (unsigned)i >= vector->elements_.size()) {
                    std::cerr << "constant vector lane index is out of range\n";
                    std::exit(1);
                }
                recentVal = vector->elements_[i];
            }
        }
        return;
    }

    //局部作用域
    if (var->type_->tid_ == Type::IntegerTyID || var->type_->tid_ == Type::FloatTyID) { //说明为局部常量
        recentVal = var;
        return;
    }
    if (auto *constantVector = dynamic_cast<ConstantVector *>(var)) {
        if (ast.arrays.empty()) {
            recentVal = constantVector;
            return;
        }
        if (ast.arrays.size() != 1) {
            std::cerr << "a fixed vector requires exactly one lane index\n";
            std::exit(1);
        }
        ast.arrays[0]->accept(*this);
        auto *constantIndex = dynamic_cast<ConstantInt *>(recentVal);
        if (constantIndex) {
            if (constantIndex->value_ < 0 ||
                (unsigned)constantIndex->value_ >= constantVector->elements_.size()) {
                std::cerr << "constant vector lane index is out of range\n";
                std::exit(1);
            }
            recentVal = constantVector->elements_[constantIndex->value_];
        } else {
            auto *slot = builder->create_alloca(constantVector->type_);
            builder->create_store(constantVector, slot);
            auto *type = static_cast<VectorType *>(constantVector->type_);
            Value *lanePointer = vectorLanePointer(this, slot, recentVal, type);
            recentVal = builder->create_load(lanePointer);
        }
        return;
    }
    // 不是常量那么var一定是指针类型
    Type* varType = static_cast<PointerType*>(var->type_)->contained_; //所指的类型
    if (auto *type = dynamic_cast<ScalarizedVectorType *>(varType)) {
        if (ast.arrays.empty()) {
            recentVal = isTrueLVal ? var
                                   : (Value *)loadScalarizedVector(this, var, type);
            return;
        }
        if (ast.arrays.size() != 1) {
            std::cerr << "a fixed vector requires exactly one lane index\n";
            std::exit(1);
        }
        ast.arrays[0]->accept(*this);
        Value *index = recentVal;
        if (auto *constantIndex = dynamic_cast<ConstantInt *>(index);
            constantIndex &&
            (constantIndex->value_ < 0 ||
             static_cast<unsigned>(constantIndex->value_) >=
                 type->num_elements_)) {
            std::cerr << "constant vector lane index is out of range\n";
            std::exit(1);
        }
        recentVal = builder->create_gep(var, {CONST_INT(0), index});
        if (!isTrueLVal)
            recentVal = builder->create_load(recentVal);
        return;
    }
    if (varType->tid_ == Type::VectorTyID) {
        if (ast.arrays.empty()) {
            recentVal = isTrueLVal ? var : (Value *)builder->create_load(var);
            return;
        }
        if (ast.arrays.size() != 1) {
            std::cerr << "a fixed vector requires exactly one lane index\n";
            std::exit(1);
        }
        ast.arrays[0]->accept(*this);
        Value *index = recentVal;
        auto *constantIndex = dynamic_cast<ConstantInt *>(index);
        auto *type = static_cast<VectorType *>(varType);
        if (constantIndex &&
            (constantIndex->value_ < 0 ||
             (unsigned)constantIndex->value_ >= type->num_elements_)) {
            std::cerr << "constant vector lane index is out of range\n";
            std::exit(1);
        }
        if (!constantIndex) {
            Value *lanePointer = vectorLanePointer(this, var, index, type);
            recentVal = isTrueLVal ? lanePointer
                                   : (Value *)builder->create_load(lanePointer);
            return;
        }
        if (isTrueLVal) {
            vectorLaneBase = var;
            vectorLaneIndex = index;
            recentVal = var;
        } else {
            recentVal = new ExtractElementInst(builder->create_load(var), index,
                                               builder->BB_);
        }
        return;
    }
    if (!ast.arrays.empty()) {
        bool parameterArray = varType->tid_ == Type::PointerTyID;
        Value *base = var;
        Type *aggregateType = varType;
        if (parameterArray) {
            base = builder->create_load(var);
            aggregateType = static_cast<PointerType *>(varType)->contained_;
        }
        unsigned arrayDepth = 0;
        Type *tail = aggregateType;
        while (tail->tid_ == Type::ArrayTyID &&
               !dynamic_cast<ScalarizedVectorType *>(tail)) {
            ++arrayDepth;
            tail = static_cast<ArrayType *>(tail)->contained_;
        }
        if (tail->tid_ == Type::VectorTyID ||
            dynamic_cast<ScalarizedVectorType *>(tail)) {
            std::vector<Value *> sourceIndices;
            for (auto &exp : ast.arrays) {
                exp->accept(*this);
                sourceIndices.push_back(recentVal);
            }
            unsigned objectIndices = arrayDepth + (parameterArray ? 1 : 0);
            if (sourceIndices.size() != objectIndices &&
                sourceIndices.size() != objectIndices + 1) {
                std::cerr << "fixed vector array access has the wrong number of indices\n";
                std::exit(1);
            }
            std::vector<Value *> gepIndices(sourceIndices.begin(),
                                            sourceIndices.begin() + objectIndices);
            if (!parameterArray)
                gepIndices.insert(gepIndices.begin(), CONST_INT(0));
            Value *vectorPointer = builder->create_gep(base, gepIndices);
            if (sourceIndices.size() == objectIndices) {
                if (auto *scalarized =
                        dynamic_cast<ScalarizedVectorType *>(tail)) {
                    recentVal = isTrueLVal
                        ? vectorPointer
                        : (Value *)loadScalarizedVector(
                              this, vectorPointer, scalarized);
                } else {
                    recentVal = isTrueLVal
                        ? vectorPointer
                        : (Value *)builder->create_load(vectorPointer);
                }
                return;
            }
            Value *laneIndex = sourceIndices.back();
            if (auto *scalarized =
                    dynamic_cast<ScalarizedVectorType *>(tail)) {
                auto *constantIndex =
                    dynamic_cast<ConstantInt *>(laneIndex);
                if (constantIndex &&
                    (constantIndex->value_ < 0 ||
                     static_cast<unsigned>(constantIndex->value_) >=
                         scalarized->num_elements_)) {
                    std::cerr << "constant vector lane index is out of range\n";
                    std::exit(1);
                }
                recentVal = builder->create_gep(
                    vectorPointer, {CONST_INT(0), laneIndex});
                if (!isTrueLVal)
                    recentVal = builder->create_load(recentVal);
                return;
            }
            auto *vectorType = static_cast<VectorType *>(tail);
            auto *constantIndex = dynamic_cast<ConstantInt *>(laneIndex);
            if (constantIndex &&
                (constantIndex->value_ < 0 ||
                 (unsigned)constantIndex->value_ >=
                     vectorType->num_elements_)) {
                std::cerr << "constant vector lane index is out of range\n";
                std::exit(1);
            }
            if (!constantIndex) {
                Value *lanePointer = vectorLanePointer(
                    this, vectorPointer, laneIndex, vectorType);
                recentVal = isTrueLVal
                                ? lanePointer
                                : (Value *)builder->create_load(lanePointer);
                return;
            }
            if (isTrueLVal) {
                vectorLaneBase = vectorPointer;
                vectorLaneIndex = laneIndex;
                recentVal = vectorPointer;
            } else {
                recentVal = new ExtractElementInst(
                    builder->create_load(vectorPointer), laneIndex, builder->BB_);
            }
            return;
        }
    }
    if (!ast.arrays.empty()) { //说明是数组
        vector<Value *> idxs;
        for (auto &exp : ast.arrays) {
            exp->accept(*this);
            idxs.push_back(recentVal);
        }
        // 当函数传入参数i32 *，会生成类型为i32 **的局部变量，即此情况
        if (varType->tid_ == Type::PointerTyID) {
            var = builder->create_load(var);
        } else if (varType->tid_ == Type::ArrayTyID) {
            idxs.insert(idxs.begin(), CONST_INT(0));
        }
        var = builder->create_gep(var, idxs); //获取的一定是指针类型
        varType = ((PointerType*)var->type_)->contained_;
    }

    //指向的还是数组,那么一定是传数组参,数组若为x[2], 参数为int a[]，需要传i32 *
    if (varType->tid_ == Type::ArrayTyID) {
        recentVal = builder->create_gep(var, {CONST_INT(0), CONST_INT(0)});
    } else if (!isTrueLVal) { //如果不是取左值，那么load
        recentVal = builder->create_load(var);
    } else { //否则返回地址值
        recentVal = var;
    }
}

void GenIR::visit(NumberAST &ast) {
    if (ast.isInt) recentVal = CONST_INT(ast.intval);
    else recentVal = CONST_FLOAT(ast.floatval);
}

void GenIR::visit(CallAST &ast) {
    // 将 C 宏名映射到 SysY 运行时函数
    if (*ast.id == "starttime") *ast.id = "_sysy_starttime";
    else if (*ast.id == "stoptime") *ast.id = "_sysy_stoptime";

    auto fun = (Function *)scope.find(*ast.id);
    if (!fun) {
        std::cerr << "Error: undefined function '" << *ast.id << "'\n";
        exit(1);
    }
    //引用函数返回值
    if (fun->basic_blocks_.size() && !is_single_exp)
        fun->use_ret_cnt ++ ;
    is_single_exp = false;
    vector<Value *> args;
    auto infoIt = scalarizedFunctionInfo.find(fun);
    ScalarizedFunctionInfo *info =
        infoIt == scalarizedFunctionInfo.end() ? nullptr : &infoIt->second;
    Value *scalarizedResultSlot = nullptr;
    unsigned argumentOffset = 0;
    if (info && info->returnType) {
        scalarizedResultSlot = builder->create_alloca(info->returnType);
        args.push_back(scalarizedResultSlot);
        argumentOffset = 1;
    }
    for (int i = 0; i < ast.funcCParamList.size(); i++) {
        VectorType *savedExpected = expectedVectorType;
        ScalarizedVectorType *savedScalarizedExpected =
            expectedScalarizedVectorType;
        ScalarizedVectorType *scalarizedParameter =
            info && static_cast<unsigned>(i) < info->valueParameters.size()
                ? info->valueParameters[i] : nullptr;
        expectedScalarizedVectorType = scalarizedParameter;
        expectedVectorType = dynamic_cast<VectorType *>(
            fun->arguments_[i + argumentOffset]->type_);
        ast.funcCParamList[i]->accept(*this);
        expectedVectorType = savedExpected;
        expectedScalarizedVectorType = savedScalarizedExpected;
        if (scalarizedParameter) {
            auto *value = asScalarizedVector(recentVal);
            auto *slot = builder->create_alloca(scalarizedParameter);
            storeScalarizedVector(this, slot, value, scalarizedParameter);
            args.push_back(slot);
            continue;
        }
        //检查函数形参与实参类型是否匹配
        if (recentVal->type_ == INT32_T &&
            fun->arguments_[i + argumentOffset]->type_ == FLOAT_T) {
            recentVal = builder->create_sitofp(recentVal, FLOAT_T);
        } else if (recentVal->type_ == FLOAT_T &&
                   fun->arguments_[i + argumentOffset]->type_ == INT32_T) {
            recentVal = builder->create_fptosi(recentVal, INT32_T);
        }
        args.push_back(recentVal);
    }
    // starttime/stoptime 在源码中无实参，但运行时函数需要一个行号参数
    if (ast.funcCParamList.empty() &&
        (*ast.id == "_sysy_starttime" || *ast.id == "_sysy_stoptime")) {
        args.push_back(new ConstantInt(INT32_T, ast.lineno));
    }
    recentVal = builder->create_call(fun, args);
    if (info && info->returnType)
        recentVal = loadScalarizedVector(this, scalarizedResultSlot,
                                         info->returnType);
}

void GenIR::visit(RelExpAST &ast) {
    if (ast.relExp == nullptr) {
        ast.addExp->accept(*this);
        return;
    }
    Value* val[2];
    ast.relExp->accept(*this);
    val[0] = recentVal;
    ast.addExp->accept(*this);
    val[1] = recentVal;
    checkCalType(val);
    if (val[0]->type_ == INT32_T) {
        switch (ast.op) {
            case ROP_LTE:
                recentVal = builder->create_icmp_le(val[0], val[1]);
                break;
            case ROP_LT:
                recentVal = builder->create_icmp_lt(val[0], val[1]);
                break;
            case ROP_GT:
                recentVal = builder->create_icmp_gt(val[0], val[1]);
                break;
            case ROP_GTE:
                recentVal = builder->create_icmp_ge(val[0], val[1]);
                break;
        }
    } else {
        switch (ast.op) {
            case ROP_LTE:
                recentVal = builder->create_fcmp_le(val[0], val[1]);
                break;
            case ROP_LT:
                recentVal = builder->create_fcmp_lt(val[0], val[1]);
                break;
            case ROP_GT:
                recentVal = builder->create_fcmp_gt(val[0], val[1]);
                break;
            case ROP_GTE:
                recentVal = builder->create_fcmp_ge(val[0], val[1]);
                break;
        }
    }
}

void GenIR::visit(EqExpAST &ast) {
    if (ast.eqExp == nullptr) {
        ast.relExp->accept(*this);
        return;
    }
    Value* val[2];
    ast.eqExp->accept(*this);
    val[0] = recentVal;
    ast.relExp->accept(*this);
    val[1] = recentVal;
    checkCalType(val);
    if (val[0]->type_ == INT32_T) {
        switch (ast.op) {
            case EOP_EQ:
                recentVal = builder->create_icmp_eq(val[0], val[1]);
                break;
            case EOP_NEQ:
                recentVal = builder->create_icmp_ne(val[0], val[1]);
                break;
        }
    } else {
        switch (ast.op) {
            case EOP_EQ:
                recentVal = builder->create_fcmp_eq(val[0], val[1]);
                break;
            case EOP_NEQ:
                recentVal = builder->create_fcmp_ne(val[0], val[1]);
                break;
        }
    }
}

void GenIR::visit(LAndExpAST &ast) {
    if (ast.lAndExp == nullptr) {
        ast.eqExp->accept(*this);
        return;
    }
    auto tempTrue = trueBB; //防止嵌套and导致原trueBB丢失。用于生成短路模块
    trueBB = createNamedBB(module.get(), currentFunction, "land.rhs");
    ast.lAndExp->accept(*this);

    if (recentVal->type_ == INT32_T) {
        recentVal = builder->create_icmp_ne(recentVal, CONST_INT(0));
    } else if (recentVal->type_ == FLOAT_T) {
        recentVal = builder->create_fcmp_ne(recentVal, CONST_FLOAT(0));
    }
    builder->create_cond_br(recentVal, trueBB, falseBB);
    builder->BB_ = trueBB;
    has_br = false;
    trueBB = tempTrue; //还原原来的true模块

    ast.eqExp->accept(*this);
}

void GenIR::visit(LOrExpAST &ast) {
    if (ast.lOrExp == nullptr) {
        ast.lAndExp->accept(*this);
        return;
    }
    auto tempFalse = falseBB; //防止嵌套and导致原trueBB丢失。用于生成短路模块
    falseBB = createNamedBB(module.get(), currentFunction, "lor.rhs");
    ast.lOrExp->accept(*this);
    if (recentVal->type_ == INT32_T) {
        recentVal = builder->create_icmp_ne(recentVal, CONST_INT(0));
    } else if (recentVal->type_ == FLOAT_T) {
        recentVal = builder->create_fcmp_ne(recentVal, CONST_FLOAT(0));
    }
    builder->create_cond_br(recentVal, trueBB, falseBB);
    builder->BB_ = falseBB;
    has_br = false;
    falseBB = tempFalse;

    ast.lAndExp->accept(*this);
}
