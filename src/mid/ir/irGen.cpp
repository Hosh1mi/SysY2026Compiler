// AST → 基础 IR：局部量走 entry alloca 槽，非 void 函数统一经 %retval 返回。
#include "../../include/mid/ir/irGen.hpp"
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using std::cout;
using std::endl;
using std::string;
using std::unique_ptr;
using std::vector;

#define CONST_INT(num) new ConstantInt(module->int32_ty_, num)
#define CONST_FLOAT(num) new ConstantFloat(module->float32_ty_, num)
#define VOID_T (module->void_ty_)
#define INT1_T (module->int1_ty_)
#define INT32_T  (module->int32_ty_)
#define FLOAT_T  (module->float32_ty_)
#define INT32PTR_T (module->get_pointer_type(module->int32_ty_))
#define FLOATPTR_T (module->get_pointer_type(module->float32_ty_))

ArrayType* expectedTensorType = nullptr;
Value* expectedTensorTarget = nullptr;
bool currentTensorReturn = false;
Value* tensorReturnPointer = nullptr;
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

struct FunctionLoweringInfo {
    ScalarizedVectorType* returnType;
    bool tensorReturn = false;
    ArrayType* tensorReturnType = nullptr;
    vector<ScalarizedVectorType> valueParameters;
    vector<bool> tensorParameters;
};

static std::unordered_map<Function *, FunctionLoweringInfo>
    functionLoweringInfo;
static std::unordered_set<std::string> pendingTensorParameters;
static std::unordered_map<Value*, Value*> tensorFirstDimensions;
static std::unordered_map<Value*, Value*> heapTensorStorage;
static Function* tensorMalloc = nullptr;
static Function* tensorFree = nullptr;
static BasicBlock* createNamedBB(Module* m, Function* func, const std::string &base);

static ArrayType* tensorType(Value *val){
    return dynamic_cast<ArrayType *>(static_cast<PointerType *>(val->type_)->contained_);
}

static ArrayType* staticTensorType(Value *val){
    if(tensorFirstDimensions.find(val) != tensorFirstDimensions.end()){
        return nullptr;
    }
    return tensorType(val);
}

static Type* tensorElementType(Type *type){
    while(type->tid_==Type::ArrayTyID){
        type = static_cast<ArrayType *>(type)->contained_;
    }
    return type;
}

static unsigned tensorElementCount(ArrayType *type){
    unsigned count = 1;
    Type *current = type;
    while(current->tid_==Type::ArrayTyID){
        auto *array = static_cast<ArrayType *>(current);
        count = array->num_elements_;
        current = array->contained_;
    }
    return count;
}

static Value* tensorElementCount(GenIR *gen, Value *value){
    auto found = tensorFirstDimensions.find(value);
    if(found == tensorFirstDimensions.end()) 
        return new ConstantInt(gen->module->int32_ty_, tensorElementCount(tensorType(value)));
    ArrayType* trailing = tensorType(value);
    if(!trailing) return found->second;
    unsigned cnt = tensorElementCount(trailing);
    if(cnt == 1) return found->second;
    return gen->builder->create_imul(found->second, new ConstantInt(gen->module->int32_ty_, cnt));
}

static Value* tensorFirstDimension(GenIR *gen, Value *value){
    auto found = tensorFirstDimensions.find(value);
    if(found == tensorFirstDimensions.end())
        return found->second;
    return new ConstantInt(gen->module->int32_ty_, tensorType(value) -> num_elements_);
}

static Constant *zeroScalar(Module *module, Type *type) {
    if (type == module->float32_ty_)
        return new ConstantFloat(module->float32_ty_, 0.0f);
    return new ConstantInt(module->int32_ty_, 0);
}

static Value* tensorData(GenIR *gen, Value *value, Type *elementType){
    Type* pointerType = gen->module->get_pointer_type(elementType);
    if(value->type_ == pointerType) 
        return value;
    return gen->builder->create_bitcast(value, pointerType);
}

static Value* createDynamicTensor(GenIR *gen, Value *model, Value *count, Type *pointerType = nullptr){
    if(!tensorMalloc){
        Type* bytePointer = gen->module->get_pointer_type(gen->module->int8_ty_);//?
        tensorMalloc = new Function(new FunctionType(bytePointer, {gen->module->int32_ty_}), "malloc", gen->module.get());
        tensorFree = new Function(new FunctionType(gen->module->void_ty_, {bytePointer}), "free", gen->module.get());
    }
    Value *bytes = gen->builder->create_imul(count, new ConstantInt(gen->module->int32_ty_, 4));
    Value *storage = gen->builder->create_call(tensorMalloc, {bytes});
    Value *result = gen->builder->create_bitcast(storage, pointerType ? pointerType : model->type_);
    result -> setSemFlag(SemFlag::SrcTensor);
    tensorFirstDimensions[result] = tensorFirstDimension(gen, model);
    heapTensorStorage[result] = storage;
    return result;
}

static void releaseTensor(GenIR *gen, Value *value){
    auto found = heapTensorStorage.find(value);
    if(found == heapTensorStorage.end()) return;
    gen->builder->create_call(tensorFree, {found->second});
    heapTensorStorage.erase(found), tensorFirstDimensions.erase(value);
}

static void moveTensorStorage(Value* from, Value* to){
    auto found = heapTensorStorage.find(from);
    if(found == heapTensorStorage.end()) return;
    heapTensorStorage[to] = found->second;
    heapTensorStorage.erase(found);
}

static void copyTensor(GenIR *gen, Value* target, Value* source, ArrayType* type){
    Type *elementType = type ? tensorElementType(type) : tensorElementType(static_cast<PointerType *>(source->type_)->contained_);
    Value *targetData = tensorData(gen, target, elementType);
    Value *sourceData = tensorData(gen, source, elementType);
    Value *count = type ? (Value *) new ConstantInt(gen->module->int32_ty_, tensorElementCount(type)) : tensorElementCount(gen, source);
    Value *indexSlot = gen->builder->create_alloca(gen->module->int32_ty_);
    gen->builder->create_store(new ConstantInt(gen->module->int32_ty_, 0), indexSlot);
    BasicBlock *cond = createNamedBB(gen->module.get(), currentFunction, "tensor.copy.cond");
    BasicBlock *body = createNamedBB(gen->module.get(), currentFunction, "tensor.copy.body");
    BasicBlock *end = createNamedBB(gen->module.get(), currentFunction, "tensor.copy.end");
    gen->builder->create_br(cond);
    gen->builder->BB_ = cond;
    Value *index = gen->builder->create_load(indexSlot);
    gen->builder->create_cond_br(gen->builder->create_icmp_lt(index, count), body, end);
    gen->builder->BB_ = body;
    index = gen->builder->create_load(indexSlot);
    Value *value = gen->builder->create_load(gen->builder->create_gep(sourceData, {index}));
    gen->builder->create_store(value, gen->builder->create_gep(targetData, {index}));
    index = gen->builder->create_iadd(index, new ConstantInt(gen->module->int32_ty_, 1));
    gen->builder->create_store(index, indexSlot);
    gen->builder->create_br(cond);
    gen->builder->BB_ = end;
}

static Value* lowerTensorElement(GenIR *gen, Value *lhs, Value *rhs, BinaryOp op, Type *type){
    if(type == gen->module->int32_ty_){
        if(op == BinaryOp::Add) return gen->builder->create_iadd(lhs, rhs);
        if(op == BinaryOp::Subtract) return gen->builder->create_isub(lhs, rhs);
        if(op == BinaryOp::Multiply) return gen->builder->create_imul(lhs, rhs);
        if(op == BinaryOp::Divide) return gen->builder->create_isdiv(lhs, rhs);
        return gen->builder->create_isrem(lhs, rhs);
    }
    if(op == BinaryOp::Add) return gen->builder->create_fadd(lhs, rhs);
    if(op == BinaryOp::Subtract) return gen->builder->create_fsub(lhs, rhs);
    if(op == BinaryOp::Multiply) return gen->builder->create_fmul(lhs, rhs);
    return gen->builder->create_fdiv(lhs, rhs);
}

static Value* lowerTensorBinary(GenIR *gen, Value *lhs, Value *rhs, BinaryOp op){
    bool lhsTensor = lhs->hasSemFlag(SemFlag::SrcTensor);
    bool rhsTensor = rhs->hasSemFlag(SemFlag::SrcTensor);
    Value *model = lhsTensor ? lhs : rhs;
    ArrayType *type = expectedTensorType;
    if(!type && lhsTensor) type = staticTensorType(lhs);
    if(!type && rhsTensor) type = staticTensorType(rhs);
    Type *elementType = type ? tensorElementType(type) : tensorElementType(static_cast<PointerType *>(model->type_)->contained_);
    Value *count = type ? (Value *)new ConstantInt(gen->module->int32_ty_, tensorElementCount(type)) : tensorElementCount(gen, model);
    Value *result;
    if(type){
        result = gen->builder->create_alloca(type);
        result->setSemFlag(SemFlag::SrcTensor);
    }
    else {
        result = createDynamicTensor(gen, model, count);
    }
    Value *resultData = tensorData(gen, result, elementType);
    Value *lhsData = lhsTensor ? tensorData(gen, lhs, elementType): nullptr;
    Value *rhsData = rhsTensor ? tensorData(gen, rhs, elementType): nullptr;
    Value *indexSlot = gen->builder->create_alloca(gen->module->int32_ty_);
    gen->builder->create_store(new ConstantInt(gen->module->int32_ty_, 0), indexSlot);
    BasicBlock *cond = createNamedBB(gen->module.get(), currentFunction, "tensor.op.cond");
    BasicBlock *body = createNamedBB(gen->module.get(), currentFunction, "tensor.op.body");
    BasicBlock *end = createNamedBB(gen->module.get(), currentFunction, "tensor.op.end");
    gen->builder->create_br(cond);
    gen->builder->BB_ = cond;
    Value *index = gen->builder->create_load(indexSlot);
    gen->builder->create_cond_br(gen->builder->create_icmp_lt(index, count), body, end);
    gen->builder->BB_ = body;
    index = gen->builder->create_load(indexSlot);
    Value *left = lhsData ? (Value *)gen->builder->create_load(gen->builder->create_gep(lhsData, {index})) : lhs;
    Value *right = rhsData ? (Value *)gen->builder->create_load(gen->builder->create_gep(rhsData, {index})) : rhs;
    Value *value = lowerTensorElement(gen, left, right, op, elementType);
    gen->builder->create_store(value, gen->builder->create_gep(resultData, {index}));
    index = gen->builder->create_iadd(index, new ConstantInt(gen->module->int32_ty_, 1));
    gen->builder->create_store(index, indexSlot);
    gen->builder->create_br(cond);
    gen->builder->BB_ = end;
    releaseTensor(gen, lhs);
    releaseTensor(gen, rhs);
    return result;
}

static bool containsTensorValue(GenIR *gen, ExprAST *expression)
{
    if(auto *lvalue = dynamic_cast<LValueAST*>(expression)){
        Value *value = gen->scope.find(lvalue->name);
        return lvalue->indices.empty() && value && value->hasSemFlag(SemFlag::SrcTensor);
    }
    if(auto *call = dynamic_cast<CallExprAST*>(expression)){
        auto *function = dynamic_cast<Function*>(gen->scope.find(call->callee));
        auto info = functionLoweringInfo.find(function);
        return info != functionLoweringInfo.end() && info->second.tensorReturn;
    }
    if(auto *unary = dynamic_cast<UnaryExprAST*>(expression))
        return containsTensorValue(gen,unary->operand.get());
    if(auto *binary = dynamic_cast<BinaryExprAST*>(expression))
        return containsTensorValue(gen,binary->left.get()) || containsTensorValue(gen,binary->right.get());
    return false;

}

static Value *lowerTensorMatMul(GenIR *gen, Value *lhs, Value *rhs){
    ArrayType *leftType = tensorType(lhs);
    ArrayType *rightType = tensorType(rhs);
    ArrayType *leftRowType = dynamic_cast<ArrayType *>(leftType->contained_);
    ArrayType *rightRowType = dynamic_cast<ArrayType *>(rightType->contained_);
    ArrayType *resultType = expectedTensorType;
    Value *rows = resultType ? (Value *)new ConstantInt(gen->module->int32_ty_, resultType->num_elements_) : tensorFirstDimension(gen, lhs);
    unsigned common = leftRowType ? leftRowType->num_elements_ : leftType->num_elements_;
    unsigned columns;
    Type *elementType;
    if(resultType){
        auto *resultRowType = static_cast<ArrayType *>(resultType->contained_);
        columns = resultRowType->num_elements_;
        elementType = resultRowType->contained_;
    }else{
        columns = rightRowType ? rightRowType -> num_elements_ : rightType -> num_elements_;
        elementType = leftRowType ? leftRowType -> contained_ : leftType -> contained_;
        auto *constantRows = dynamic_cast<ConstantInt *> (rows);
        if(constantRows){
            ArrayType *resultRowType = gen->module->get_array_type(elementType, columns);
            resultType = gen->module->get_array_type(resultRowType, constantRows->value_);
        }
    }
    Value * resultCount = gen->builder->create_imul(rows, new ConstantInt(gen->module->int32_ty_, columns));
    Value * result;
    if(resultType){
        result = gen->builder->create_alloca(resultType);
        result->setSemFlag(SemFlag::SrcTensor);
    }else{
        ArrayType * rowType = rightRowType ? rightRowType: rightType;
        result = createDynamicTensor(gen, lhs, resultCount, gen->module->get_pointer_type(rowType));
        tensorFirstDimensions[result] = rows;
    }
    Value *leftData = tensorData(gen, lhs, elementType);
    Value *rightData = tensorData(gen, rhs, elementType);
    Value *resultData = tensorData(gen, result, elementType);
    Value *rowIndexSlot = gen->builder->create_alloca(gen->module->int32_ty_);
    Value *columnIndexSlot = gen->builder->create_alloca(gen->module->int32_ty_);
    Value *commonIndexSlot = gen->builder->create_alloca(gen->module->int32_ty_);
    Value *sumSlot = gen->builder->create_alloca(elementType);
    gen->builder->create_store(new ConstantInt(gen->module->int32_ty_, 0), rowIndexSlot);
    BasicBlock *rowCond = createNamedBB(gen->module.get(), currentFunction, "tensor.matmul.row.cond");
    BasicBlock *rowBody = createNamedBB(gen->module.get(), currentFunction, "tensor.matmul.row.body");
    BasicBlock *rowEnd = createNamedBB(gen->module.get(), currentFunction, "tensor.matmul.row.end");
    BasicBlock *columnCond = createNamedBB(gen->module.get(), currentFunction, "tensor.matmul.column.cond");
    BasicBlock *columnBody = createNamedBB(gen->module.get(), currentFunction, "tensor.matmul.column.body");
    BasicBlock *commonCond = createNamedBB(gen->module.get(), currentFunction, "tensor.matmul.common.cond");
    BasicBlock *commonBody = createNamedBB(gen->module.get(), currentFunction, "tensor.matmul.common.body");
    BasicBlock *commonEnd = createNamedBB(gen->module.get(), currentFunction, "tensor.matmul.common.end");
    BasicBlock *end = createNamedBB(gen->module.get(), currentFunction, "tensor.matmul.end");
    gen->builder->create_br(rowCond);
    gen->builder->BB_ = rowCond;
    Value *rowIndex = gen->builder->create_load(rowIndexSlot);
    gen->builder->create_cond_br(gen->builder->create_icmp_lt(rowIndex, rows), rowBody, end);
    gen->builder->BB_ = rowBody;
    gen->builder->create_store(new ConstantInt(gen->module->int32_ty_, 0), columnIndexSlot);

    gen->builder->create_br(columnCond);
    gen->builder->BB_ = columnCond;
    Value *columnIndex = gen->builder->create_load(columnIndexSlot);
    gen->builder->create_cond_br(gen->builder->create_icmp_lt(columnIndex, new ConstantInt(gen->module->int32_ty_, columns)), columnBody, rowEnd);
    gen->builder->BB_ = columnBody;
    gen->builder->create_store(new ConstantInt(gen->module->int32_ty_, 0), commonIndexSlot);
    gen->builder->create_store(zeroScalar(gen->module.get(), elementType), sumSlot);
    gen->builder->create_br(commonCond);
    gen->builder->BB_ = commonCond;
    Value *commonIndex = gen->builder->create_load(commonIndexSlot);
    gen->builder->create_cond_br(gen->builder->create_icmp_lt(commonIndex, new ConstantInt(gen->module->int32_ty_, common)), commonBody, commonEnd);
    gen->builder->BB_ = commonBody;
    rowIndex = gen->builder->create_load(rowIndexSlot);
    columnIndex = gen->builder->create_load(columnIndexSlot);
    commonIndex = gen->builder->create_load(commonIndexSlot);
    Value* leftIndex = gen->builder->create_iadd(gen->builder->create_imul(rowIndex, new ConstantInt(gen->module->int32_ty_, common)), commonIndex);
    Value* rightIndex = gen->builder->create_iadd(gen->builder->create_imul(commonIndex, new ConstantInt(gen->module->int32_ty_, columns)), columnIndex);
    Value* left = gen->builder->create_load(gen->builder->create_gep(leftData, {leftIndex}));
    Value* right = gen->builder->create_load(gen->builder->create_gep(rightData, {rightIndex}));
    Value* product = elementType == gen->module->int32_ty_ ? (Value*) gen->builder->create_imul(left, right) : (Value*) gen->builder->create_fmul(left, right);
    Value* sum = gen->builder->create_load(sumSlot);
    sum = elementType == gen->module->int32_ty_ ? (Value*) gen->builder->create_iadd(sum, product) : (Value*) gen->builder->create_fadd(sum, product);
    gen->builder->create_store(sum, sumSlot);
    
    commonIndex = gen->builder->create_iadd(commonIndex, new ConstantInt(gen->module->int32_ty_, 1));
    gen->builder->create_store(commonIndex, commonIndexSlot);
    gen->builder->create_br(commonCond);
    gen->builder->BB_ = commonEnd;
    
    rowIndex = gen->builder->create_load(rowIndexSlot);
    columnIndex = gen->builder->create_load(columnIndexSlot);
    Value *resultIndex = gen->builder->create_iadd(gen->builder->create_imul(rowIndex, new ConstantInt(gen->module->int32_ty_, columns)), columnIndex);
    gen->builder->create_store(gen->builder->create_load(sumSlot), gen->builder->create_gep(resultData, {resultIndex}));
    
    columnIndex = gen->builder->create_iadd(columnIndex, new ConstantInt(gen->module->int32_ty_, 1));
    gen->builder->create_store(columnIndex, columnIndexSlot);
    gen->builder->create_br(columnCond);
    gen->builder->BB_ = rowEnd;
    rowIndex = gen->builder->create_load(rowIndexSlot);
    rowIndex = gen->builder->create_iadd(rowIndex, new ConstantInt(gen->module->int32_ty_, 1));
    gen->builder->create_store(rowIndex, rowIndexSlot);
    gen->builder->create_br(rowCond);
    gen->builder->BB_ = end;

    releaseTensor(gen, lhs);
    releaseTensor(gen, rhs);
    return result;
}

static bool containsCall(ExprAST *expression)
{
    if(dynamic_cast<CallExprAST *>(expression)) return true;
    if(auto *unary = dynamic_cast<UnaryExprAST*>(expression))
        return containsCall(unary->operand.get());
    if(auto *binary = dynamic_cast<BinaryExprAST*>(expression))
        return containsCall(binary->left.get()) || containsCall(binary->right.get());
    if(auto *subscript = dynamic_cast<SubscriptExprAST*>(expression))
        return containsCall(subscript->base.get()) || (subscript->index.get());
    if(auto *lvalue = dynamic_cast<LValueAST*>(expression)){
        for(auto &index: lvalue->indices)
            if(containsCall(index.get()))
                return true;
    }
    return false;
}

// 529
struct TensorExpression {
    enum Kind { Leaf, Unary, Binary} kind;
    Value *value = nullptr;
    bool tensor = false;
    UnaryOp unaryOp = UnaryOp::Plus;
    BinaryOp binaryOp = BinaryOp::Add;
    std::unique_ptr<TensorExpression> left;
    std::unique_ptr<TensorExpression> right;

    TensorExpression(Value *value, bool tensor):kind(Leaf),value(value),tensor(tensor) {}
    TensorExpression(UnaryOp op, std::unique_ptr<TensorExpression> operand): kind(Unary), unaryOp(op), left (std::move(operand)) {}
    TensorExpression(BinaryOp op, std::unique_ptr<TensorExpression> lhs, std::unique_ptr<TensorExpression> rhs): kind(Binary), binaryOp(op), left(std::move(lhs)), right(std::move(rhs)) {}
};

// 548
static bool isTensorElementwise(BinaryOp op){
    return op == BinaryOp::Add || op == BinaryOp::Subtract ||
           op == BinaryOp::Multiply || op == BinaryOp::Divide ||
           op == BinaryOp::Remainder;
}


// 620
static Value *tensorExpressionModel(TensorExpression *expression){
    if(expression -> kind == TensorExpression::Leaf)
        return expression->tensor ? expression -> value : nullptr;

    Value *model = tensorExpressionModel(expression -> left.get());
    if(!model && expression -> right)
        model = tensorExpressionModel(expression -> right.get());
    return model;
}

static std::unique_ptr<TensorExpression> buildTensorExpression(GenIR *gen, ExprAST *expression)
{
    if(containsTensorValue(gen, expression)){
        if(auto *unary = dynamic_cast<UnaryExprAST*>(expression)){
            if(unary->op == UnaryOp::Plus || unary->op == UnaryOp::Minus)
                return std::unique_ptr<TensorExpression>(new TensorExpression(unary->op, buildTensorExpression(gen,unary->operand.get())));
        }
        if(auto *binary = dynamic_cast<BinaryExprAST*>(expression)){
            if(isTensorElementwise(binary->op)){
                std::unique_ptr<TensorExpression>left = buildTensorExpression(gen, binary->left.get());
                std::unique_ptr<TensorExpression>right = buildTensorExpression(gen, binary->right.get());
                return std::unique_ptr<TensorExpression>(new TensorExpression(binary->op, std::move(left), std::move(right)));
            }
        }
    }
    Value *savedTarget = expectedTensorTarget;
    expectedTensorTarget = nullptr;
    expression->accept(*gen);
    expectedTensorTarget = savedTarget;
    return std::unique_ptr<TensorExpression>(new TensorExpression(recentVal, recentVal->hasSemFlag(SemFlag::SrcTensor)));
}



static Value* emitTensorExpression(GenIR *gen, TensorExpression *expression, Value *index, Type *elementType){
    if(expression->kind == TensorExpression::Leaf){
        if(!expression ->tensor) return expression ->value;
        Value *data = tensorData(gen, expression->value, elementType);
        return gen->builder->create_load(gen->builder->create_gep(data, {index}));
    }
    Value *left = emitTensorExpression(gen, expression->left.get(), index, elementType);
    if(expression->kind == TensorExpression::Unary){
        if(expression -> unaryOp == UnaryOp::Plus) return left;
        return lowerTensorElement(gen, zeroScalar(gen->module.get(), elementType), left, BinaryOp::Subtract, elementType);
    }
    Value *right = emitTensorExpression(gen, expression->right.get(), index, elementType);
        return lowerTensorElement(gen, left, right, expression->binaryOp, elementType);
}

static void releaseTensorExpression(GenIR *gen, TensorExpression *expression){
    if(expression -> kind == TensorExpression::Leaf){
        if(expression -> tensor) releaseTensor(gen, expression->value);
        return;
    }
    releaseTensorExpression(gen, expression->left.get());
    if(expression->right) releaseTensorExpression(gen, expression->right.get());
}

static Value *lowerFusedTensorExpression(GenIR *gen, ExprAST *expression){
    std::unique_ptr<TensorExpression> tree = buildTensorExpression(gen, expression);
    Value *model = tensorExpressionModel(tree.get());
    ArrayType *type = expectedTensorType;
    if(!type) type = staticTensorType(model);
    Type *elementType = type ? tensorElementType(type) : tensorElementType(static_cast<PointerType *> (model -> type_) -> contained_);
    Value * count = type ? (Value *) new ConstantInt(gen->module->int32_ty_, tensorElementCount(type)):tensorElementCount(gen, model);
    Value *result = expectedTensorTarget;
    if(!result && type) {
        result = gen->builder->create_alloca(type);
        result->setSemFlag(SemFlag::SrcTensor);
    }
    else if(!result){
        result = createDynamicTensor(gen, model, count);
    }
    Value *resultData = tensorData(gen, result, elementType);
    Value *indexSlot = gen->builder->create_alloca(gen->module->int32_ty_);
    gen->builder->create_store(new ConstantInt(gen->module->int32_ty_, 0), indexSlot);
    BasicBlock * cond = createNamedBB(gen->module.get(), currentFunction, "tensor.fused.cond");
    BasicBlock * body = createNamedBB(gen->module.get(), currentFunction, "tensor.fused.body");
    BasicBlock * end = createNamedBB(gen->module.get(), currentFunction, "tensor.fused.end");
    gen->builder->create_br(cond);
    gen->builder->BB_= cond;
    Value *index = gen->builder->create_load(indexSlot);
    gen->builder->create_cond_br(gen->builder->create_icmp_lt(index, count), body, end);
    gen->builder->BB_ = body;
    index = gen->builder->create_load(indexSlot);
    Value *value = emitTensorExpression(gen, tree.get(), index, elementType);
    gen->builder->create_store(value, gen->builder->create_gep(resultData, {index}));
    index = gen->builder->create_iadd(index, new ConstantInt(gen->module->int32_ty_, 1));
    gen->builder->create_store(index, indexSlot);
    gen->builder->create_br(cond);
    gen->builder->BB_ = end;
    releaseTensorExpression(gen, tree.get());
    return result;
}

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
    if (!init || (!init->isExpression() && init->elements().empty())) {
        Value *zero = zeroScalar(gen->module.get(), type->contained_);
        return new ScalarizedVectorValue(
            type, std::vector<Value *>(type->num_elements_, zero));
    }
    if (init->isExpression()) {
        ScalarizedVectorType *saved = expectedScalarizedVectorType;
        expectedScalarizedVectorType = type;
        init->expression()->accept(*gen);
        expectedScalarizedVectorType = saved;
        auto *value = asScalarizedVector(recentVal);
        if (!value || value->type_ != type) {
            std::cerr << "a fixed vector copy initializer must have the same type\n";
            std::exit(1);
        }
        return value;
    }
    if (init->elements().size() > type->num_elements_) {
        std::cerr << "too many elements in fixed vector initializer\n";
        std::exit(1);
    }
    std::vector<Value *> lanes;
    for (auto &item : init->elements()) {
        if (!item->isExpression()) {
            std::cerr << "nested braces are not valid for a one-dimensional vector\n";
            std::exit(1);
        }
        item->expression()->accept(*gen);
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
    if (!init || (!init->isExpression() && init->elements().empty()))
        return new ConstantZero(vectorType);
    if (init->isExpression()) {
        VectorType *savedExpected = expectedVectorType;
        expectedVectorType = vectorType;
        init->expression()->accept(*gen);
        expectedVectorType = savedExpected;
        if (recentVal->type_ != vectorType) {
            std::cerr << "a fixed vector copy initializer must have the same type\n";
            std::exit(1);
        }
        return recentVal;
    }
    if (init->elements().size() > vectorType->num_elements_) {
        std::cerr << "too many elements in fixed vector initializer\n";
        std::exit(1);
    }

    std::vector<Value *> lanes;
    bool allConstant = true;
    for (auto &item : init->elements()) {
        if (!item->isExpression()) {
            std::cerr << "nested braces are not valid for a one-dimensional vector\n";
            std::exit(1);
        }
        item->expression()->accept(*gen);
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

static bool isFloatLiteral(const ExprAST *expr) {
    if (const auto *literal = dynamic_cast<const LiteralExprAST *>(expr))
        return std::holds_alternative<float>(literal->value);
    if (const auto *unary = dynamic_cast<const UnaryExprAST *>(expr))
        return isFloatLiteral(unary->operand.get());
    return false;
}

// Infer a fixed vector type for a braced literal when no contextual type is set.
static VectorType *inferVectorLiteralType(Module *module, InitValAST *init) {
    if (expectedVectorType)
        return expectedVectorType;
    bool anyFloat = false;
    if (init && !init->isExpression())
        for (auto &item : init->elements())
            anyFloat |= item && item->isExpression() &&
                        isFloatLiteral(item->expression());
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
    if (init && init->isExpression()) {
        std::cerr << "a nested vector array initializer must use braces\n";
        std::exit(1);
    }
    if (init && !init->elements().empty()) {
        if (init->elements().size() > length) {
            std::cerr << "too many vectors in array initializer\n";
            std::exit(1);
        }
        for (auto &item : init->elements())
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
    if (init && init->isExpression()) {
        std::cerr << "a nested vector array initializer must use braces\n";
        std::exit(1);
    }
    if (init && init->elements().size() > length) {
        std::cerr << "too many vectors in array initializer\n";
        std::exit(1);
    }

    for (unsigned i = 0; i < length; ++i) {
        Value *element = gen->builder->create_gep(
            slot, {new ConstantInt(gen->module->int32_ty_, 0),
                   new ConstantInt(gen->module->int32_ty_, (int)i)});
        InitValAST *child = nullptr;
        if (init && !init->isExpression() && i < init->elements().size())
            child = init->elements()[i].get();
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
                                                     UnaryOp op) {
    auto *type = static_cast<ScalarizedVectorType *>(value->type_);
    if (op == UnaryOp::Plus)
        return value;
    std::vector<Value *> lanes;
    lanes.reserve(type->num_elements_);
    if (op == UnaryOp::Minus) {
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
        auto *user = use.user_;
        if (user && user->is_load())
            return;
    }
    std::vector<Instruction *> deadStores;
    for (auto &use : slot->use_list_) {
        auto *user = use.user_;
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
    if (!br || br->num_ops() != 1 || br->get_operand(0) != retBlock)
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
    for (auto &item : ast.items)
        std::visit([this](auto &node) { node->accept(*this); }, item);
}

void GenIR::visit(DeclAST &ast) {
    isConst = ast.isConst;
    curSourceType = ast.type;
    curType = ast.type.isDynamicVector()
                  ? scalarType(module.get(), ast.type.element)
                  : lowerFixedType(this, ast.type);
    for (auto &object : ast.objects) {
        if (curSourceType.isDynamicVector() &&
            object->dimensions.empty()) {
            std::cerr << "a dynamic vector object requires an explicit storage length, "
                         "for example vector<int> a[n]\n";
            std::exit(1);
        }
        object->accept(*this);
    }
}

void GenIR::visit(ObjectDefAST &ast) {
    string varName = ast.name;
    if ((dynamic_cast<VectorType *>(curType) ||
         dynamic_cast<ScalarizedVectorType *>(curType)) &&
        !ast.dimensions.empty()) {
        bool wasUseConst = useConst;
        useConst = true;
        std::vector<unsigned> dimensions;
        dimensions.reserve(ast.dimensions.size());
        for (auto &exp : ast.dimensions) {
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
            if (!ast.initializer ||
                (!ast.initializer->isExpression() &&
                 ast.initializer->elements().empty())) {
                initializer = new ConstantZero(aggregateType);
            } else {
                bool oldConst = useConst;
                useConst = true;
                initializer = buildConstantVectorAggregate(
                    this, ast.initializer.get(), aggregateType);
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
        storeLocalVectorAggregate(this, slot, ast.initializer.get(),
                                  aggregateType);
        return;
    }
    //全局变量或常量
    if (scope.in_global()) {
        if (ast.dimensions.empty()) {   //不是数组，即全局量
            if (auto *vectorType = dynamic_cast<VectorType *>(curType)) {
                bool oldConst = useConst;
                useConst = true;
                Value *initializer = buildVectorInitializer(
                    this, ast.initializer.get(), vectorType);
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
                    this, ast.initializer.get(), vectorType);
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
            if (ast.initializer == nullptr) { //无初始化
                if (isConst) cout << "no initVal when define const!" << endl; //无初始化全局常量报错
                //无初始化全局量一定是变量
                GlobalVariable* var;
                if (curType == INT32_T)
                    var = new GlobalVariable(varName, module.get(), curType, false, CONST_INT(0));
                else var = new GlobalVariable(varName, module.get(), curType, false, CONST_FLOAT(0));
                scope.push(varName, var);
            } else { //有初始化
                useConst = true;
                ast.initializer->accept(*this);
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
            for (auto &exp : ast.dimensions) {
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
            if (ast.initializer == nullptr ||
                (!ast.initializer->isExpression() &&
                 ast.initializer->elements().empty())) {
                auto init = new ConstantZero(arrayTys[0]);
                auto var = new GlobalVariable(varName, module.get(), arrayTys[0], isConst, init);
                if (isConst) var->setSemFlag(SemFlag::ImmutableObject);
                if (curSourceType.isTensor()) var -> setSemFlag(SemFlag::SrcTensor);
                scope.push(varName, var);
            } else {
                useConst = true; //全局数组量的初始值必为常量
                auto init = globalInit(dimensions, arrayTys, 0,
                                       ast.initializer->elements());
                useConst = false;
                auto var = new GlobalVariable(varName, module.get(), arrayTys[0], isConst, init);
                if (isConst) var->setSemFlag(SemFlag::ImmutableObject);
                if (curSourceType.isTensor()) var -> setSemFlag(SemFlag::SrcTensor);
                scope.push(varName, var);
            }
        }
        return;
    }


    //局部变量或常量
    if (ast.dimensions.empty()) {   //不是数组，即普通局部量
        if (auto *vectorType = dynamic_cast<VectorType *>(curType)) {
            Value *initializer = buildVectorInitializer(
                this, ast.initializer.get(), vectorType);
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
                this, ast.initializer.get(), vectorType);
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
        if (ast.initializer == nullptr) {   //无初始化
            if (isConst) cout << "no initVal when define const!" << endl;   //无初始化局部常量报错
            else { //无初始化变量
                AllocaInst *varAlloca;
                varAlloca = builder->create_alloca(curType);
                varAlloca->name_ = varName;
                scope.push(varName, varAlloca);
            }
        } else { //有初始化
            ast.initializer->accept(*this);
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
        vector<int> dimensions(ast.dimensions.size()), dimensionsCnt((ast.dimensions.size()));  //数组各维度, [2][3][4]对应; 次维度数组元素个数, [24][12][4]
        int totalByte = 1; //存储总共的字节数
        useConst = true;
        //获取数组各维度
        for (int i = dimensions.size() - 1; i>= 0; i--) {
            ast.dimensions[i]->accept(*this);
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
        if (curSourceType.isTensor()) arrayAlloc->setSemFlag(SemFlag::SrcTensor);
        scope.push(varName, arrayAlloc);
        if (curSourceType.isTensor() && ast.initializer != nullptr && ast.initializer->isExpression()){
            ArrayType* savedExpected = expectedTensorType;
            Value *savedTarget = expectedTensorTarget;
            expectedTensorType = arrayTy;
            expectedTensorTarget = arrayAlloc;
            ast.initializer->expression()->accept(*this);
            expectedTensorType = savedExpected;
            expectedTensorTarget = savedTarget;
            if(recentVal != arrayAlloc){
                copyTensor(this, arrayAlloc, recentVal, arrayTy);
                releaseTensor(this, recentVal);
            }
            return;
        }
        if (ast.initializer == nullptr) { //无初始化
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
        if (!ast.initializer->isExpression() &&
            ast.initializer->elements().empty()) return;
        vector<Value*> idxs(dimensions.size() + 1);
        for (int i = 0; i < dimensions.size() + 1; i++) {
            idxs[i] = CONST_INT(0);
        }
        Value* ptr = builder->create_gep(arrayAlloc, idxs); //获取数组开头地址
        localInit(ptr, ast.initializer->elements(), dimensionsCnt, 0);
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
        if (val->isExpression()) {
            dimAdd = dimensions.size() - 1;
            val->expression()->accept(*this);
            checkInitType();
            elements.push_back((ConstantInt*)recentVal);
        } else {
            auto nextUp = getNextDim(elementsCnts, up); //该嵌套数组的维度
            dimAdd = nextUp - 1; //比他高一维度的数组需要添加一个元素
            if (nextUp == dimensions.size()) cout << "initial invalid" << endl;//没有连续0，没对齐，不合法
            if (val->elements().empty()) {
                elements.push_back(new ConstantZero(arrayTys[nextUp]));
            } else {
                auto temp = globalInit(dimensions, arrayTys, nextUp,
                                       val->elements());
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
    for (int i = up + 1; i < dimensionsCnt.size(); i++) {
        if (cnt % dimensionsCnt[i] == 0) return i;
    }
    return 0;
}

//根据首指针递归初始化数组,up表示子数组的最高对齐位置，比如[4][2][4]，子数组最高对齐[2][4],up为1
void GenIR::localInit(Value* ptr, vector<unique_ptr<InitValAST>> &list, vector<int> &dimensionsCnt, int up) {
    int cnt = 0;
    Value* tempPtr = ptr;
    for (auto &initVal : list) {
        if (initVal->isExpression()) {
            if (cnt == 0) cnt++; //第一次赋值时可以少一次create_gep
            else tempPtr = builder->create_gep(ptr, {CONST_INT(cnt++)});
            initVal->expression()->accept(*this);
            checkInitType();
            builder->create_store(recentVal, tempPtr);
        } else {
            auto nextUp = getNextDim(dimensionsCnt, up, cnt);
            if (nextUp == 0) cout << "initial invalid!" << endl;
            if (!initVal->elements().empty()) {
                if (cnt != 0) tempPtr = builder->create_gep(ptr, {CONST_INT(cnt)}); //没赋值过，那tempPtr实际就是ptr
                localInit(tempPtr, initVal->elements(), dimensionsCnt, nextUp);
            }
            cnt += dimensionsCnt[nextUp]; //数组初始化量一定增加这么多
        }
    }
}

void GenIR::visit(InitValAST &ast) {
    //不是数组则求exp的值，若是数组不会进入此函数
    if (ast.isExpression())
        ast.expression()->accept(*this);
}

void GenIR::visit(FuncDefAST &ast) {
    isNewFunc = true;
    params.clear();
    paramNames.clear();
    pendingScalarizedParameters.clear();
    pendingTensorParameters.clear();
    currentTensorReturn = ast.returnType.isTensor();
    tensorReturnPointer = nullptr;
    currentScalarizedReturnType = nullptr;
    scalarizedReturnPointer = nullptr;
    Type *retType;
    if (ast.returnType.isDynamicVector()) {
        std::cerr << "dynamic vectors are non-owning views and cannot be returned by value\n";
        std::exit(1);
    }else if(currentTensorReturn){
        retType = VOID_T;
        params.push_back(module->get_pointer_type(scalarType(module.get(), ast.returnType.element)));
        paramNames.push_back("$tensor.return");
        pendingScalarizedParameters.push_back(nullptr);
    } else if (ast.returnType == TYPE_VOID) {
        retType = VOID_T;
    } else {
        Type *lowered = lowerFixedType(this, ast.returnType);
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

    for (auto &parameter : ast.parameters)
        parameter->accept(*this);

    auto funTy = new FunctionType(retType, params);
    auto func = new Function(funTy, ast.name, module.get());
    currentFunction = func;
    FunctionLoweringInfo info;
    info.returnType = currentScalarizedReturnType;
    info.tensorReturn = currentTensorReturn;
    for(auto &p: ast.parameters)
        info.tensorParameters.push_back(p->type.isTensor());
    unsigned hiddenParameters = currentScalarizedReturnType || currentTensorReturn ? 1U : 0U;
    // info.valueParameters.assign(
    //     pendingScalarizedParameters.begin() + hiddenParameters,
    //     pendingScalarizedParameters.end());
    functionLoweringInfo[func] = std::move(info);
    scope.push(ast.name, func);
    scope.enter();

    std::vector<Value *> args;
    for (auto arg = func->arguments_.begin(); arg != func->arguments_.end(); ++arg)
        args.push_back(*arg);

    auto *entry = createNamedBB(module.get(), func, "entry");
    builder->BB_ = entry;

    // 标量和原生向量形参保存在局部槽中；展开向量先复制各 lane，保持按值语义。
    for (int i = 0; i < (int)paramNames.size(); i++) {
        if(currentTensorReturn && i == 0){
            tensorReturnPointer = args[i];
            continue;
        }
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
        if(pendingTensorParameters.find(paramNames[i])!=pendingTensorParameters.end()){
            alloc->setSemFlag(SemFlag::SrcTensor);
            tensorFirstDimensions[alloc] = args[i + 1];
        }
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
    ast.body->accept(*this);

    if (!builder->BB_->get_terminator())
        builder->create_br(retBB);

    tryMergeTrivialRetBlock(func, retBB, retAlloca);
    retBB = nullptr;
    retAlloca = nullptr;
    currentScalarizedReturnType = nullptr;
    scalarizedReturnPointer = nullptr;
    currentTensorReturn = false;
    tensorReturnPointer = nullptr;
}

void GenIR::visit(FuncParamAST &ast) {
    //获取参数类型
    Type *paramType;
    if (ast.type.isDynamicVector()) {
        if (!ast.isArray) {
            std::cerr << "a dynamic vector parameter must use view syntax name[]\n";
            std::exit(1);
        }
        paramType = scalarType(module.get(), ast.type.element);
    } else {
        paramType = lowerFixedType(this, ast.type);
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
        for (int i = ast.trailingDimensions.size() - 1; i >= 0; i--) {
            ast.trailingDimensions[i]->accept(*this);
            paramType = module->get_array_type(paramType, ((ConstantInt *)recentVal)->value_);
        }
        useConst = false;
        //如int a[][2]，则参数为[2 x i32]* ;  int a[],参数为i32 *
        paramType = module->get_pointer_type(paramType);
    }
    params.push_back(paramType);
    paramNames.push_back(ast.name);
    pendingScalarizedParameters.push_back(scalarizedValueParameter);
    if(ast.type.isTensor()){
        pendingTensorParameters.insert(ast.name);
        params.push_back(INT32_T);
        paramNames.push_back("$" + ast.name + ".dim0");
        pendingScalarizedParameters.push_back(nullptr);
    }
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
    for (auto &item : ast.items) {
        if (has_br) break;     //此BB已经出现了br，后续指令无效
        std::visit([this](auto &node) { node->accept(*this); }, item);
    }

    scope.exit();
}

void GenIR::visit(EmptyStmtAST &) {}

void GenIR::visit(AssignStmtAST &ast) {
    is_single_exp = true;
    vectorLaneBase = nullptr;
    vectorLaneIndex = nullptr;
    requireLVal = true;  
    ast.target->accept(*this);
    auto var = recentVal;
    if(var->hasSemFlag(SemFlag::SrcTensor)){
        ArrayType* type = staticTensorType(var);
        ArrayType* savedExpected = expectedTensorType;
        Value* savedTarget = expectedTensorTarget;
        expectedTensorType = type ? type : savedExpected;
        bool directTarget = !containsCall(ast.value.get()) || dynamic_cast<CallExprAST*>(ast.value.get());
        expectedTensorTarget = directTarget ? var : nullptr;
        ast.value->accept(*this);
        expectedTensorType = savedExpected;
        expectedTensorTarget = savedTarget;
        if(!type){
            type = staticTensorType(recentVal);
        }
        if(recentVal != var){
            copyTensor(this, var, recentVal, type);
            releaseTensor(this, recentVal);
        }
        return;
    }
    ScalarizedVectorType *scalarizedType = nullptr;
    if (var && var->type_->tid_ == Type::PointerTyID)
        scalarizedType = dynamic_cast<ScalarizedVectorType *>(
            static_cast<PointerType *>(var->type_)->contained_);
    if (scalarizedType) {
        ScalarizedVectorValue *initializer = nullptr;
        ScalarizedVectorType *saved = expectedScalarizedVectorType;
        expectedScalarizedVectorType = scalarizedType;
        ast.value->accept(*this);
        expectedScalarizedVectorType = saved;
        initializer = asScalarizedVector(recentVal);
        storeScalarizedVector(this, var, initializer, scalarizedType);
        return;
    }
    {
        VectorType *savedExpected = expectedVectorType;
        if (var && var->type_->tid_ == Type::PointerTyID)
            expectedVectorType = dynamic_cast<VectorType *>(
                static_cast<PointerType *>(var->type_)->contained_);
        ast.value->accept(*this);
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
        return;
    }
    Type* varElemType = static_cast<PointerType*>(var->type_)->contained_;
    if (varElemType == FLOAT_T && expval->type_ == INT32_T) {
        expval = builder->create_sitofp(expval, FLOAT_T);
    } else if (varElemType == INT32_T && expval->type_ == FLOAT_T) {
        expval = builder->create_fptosi(expval, INT32_T);
    }
    builder->create_store(expval, var);
}

void GenIR::visit(ExprStmtAST &ast) {
    is_single_exp = true;
    ast.expression->accept(*this);
    releaseTensor(this, recentVal);
    is_single_exp = false;
}

void GenIR::visit(ContinueStmtAST &) {
    builder->create_br(whileCondBB);
    has_br = true;
}

void GenIR::visit(BreakStmtAST &) {
    builder->create_br(whileFalseBB);
    has_br = true;
}

void GenIR::visit(BlockStmtAST &ast) { ast.block->accept(*this); }

void GenIR::visit(ReturnStmtAST &ast) {
    if(currentTensorReturn){
        if(ast.value){
            Value* savedTarget = expectedTensorTarget;
            ArrayType* directReturnType = nullptr;
            auto *call = dynamic_cast<CallExprAST*>(ast.value.get());
            if(call){
                expectedTensorTarget = tensorReturnPointer;
                auto* callee =static_cast<Function*>(scope.find(call->callee));
                auto info = functionLoweringInfo.find(callee);
                if(info != functionLoweringInfo.end()){
                    directReturnType = info->second.tensorReturnType;
                }
            }
            ast.value->accept(*this);
            expectedTensorTarget = savedTarget;
            ArrayType* type = staticTensorType(recentVal);
            if(!type){
                type = directReturnType;
            }
            if(type){
                functionLoweringInfo[currentFunction].tensorReturnType = type;
            }
            if(recentVal != tensorReturnPointer){
                copyTensor(this, tensorReturnPointer, recentVal, type);
            }
            releaseTensor(this, recentVal);
        }
        recentVal = builder->create_br(retBB);
        has_br = true;
        return;
    }
    if (currentScalarizedReturnType) {
        if (ast.value) {
            ScalarizedVectorType *saved = expectedScalarizedVectorType;
            expectedScalarizedVectorType = currentScalarizedReturnType;
            ast.value->accept(*this);
            expectedScalarizedVectorType = saved;
            storeScalarizedVector(this, scalarizedReturnPointer,
                                  asScalarizedVector(recentVal),
                                  currentScalarizedReturnType);
        }
        recentVal = builder->create_br(retBB);
        has_br = true;
        return;
    }
    if (ast.value != nullptr) {
        VectorType *savedExpected = expectedVectorType;
        expectedVectorType =
            dynamic_cast<VectorType *>(currentFunction->get_return_type());
        ast.value->accept(*this);
        expectedVectorType = savedExpected;
        Value *retVal = coerceToReturnType(builder, recentVal,
                                           currentFunction->get_return_type(),
                                           INT32_T, FLOAT_T);
        builder->create_store(retVal, retAlloca);
    }
    recentVal = builder->create_br(retBB);
    has_br = true;
}

void GenIR::visit(IfStmtAST &ast) {
    //先保存trueBB和falseBB，防止嵌套导致返回上一层后丢失块的地址
    auto tempTrue = trueBB;
    auto tempFalse = falseBB;

    trueBB = createNamedBB(module.get(), currentFunction, "if.then");
    BasicBlock *nextIf;
    if (ast.elseBranch == nullptr) {
        // 无 else：假出口就是汇合点
        falseBB = createNamedBB(module.get(), currentFunction, "if.end");
        nextIf = falseBB;
    } else {
        falseBB = createNamedBB(module.get(), currentFunction, "if.else");
        nextIf = createNamedBB(module.get(), currentFunction, "if.end");
    }
    bool nextIfReachable = false;
    ast.condition->accept(*this);
    //检查是否是i1，不是则进行比较
    if (recentVal->type_ == INT32_T) {
        recentVal = builder->create_icmp_ne(recentVal, CONST_INT(0));
    } else if (recentVal->type_ == FLOAT_T) {
        recentVal = builder->create_fcmp_ne(recentVal, CONST_FLOAT(0));
    }
    builder->create_cond_br(recentVal, trueBB, falseBB);

    builder->BB_ = trueBB; //开始构建trueBB
    has_br = false;
    ast.thenBranch->accept(*this);
    if (!builder->BB_->get_terminator()) {
        builder->create_br(nextIf);
        nextIfReachable = true;
    }

    if (ast.elseBranch != nullptr) { // 开始构建falseBB
        builder->BB_ = falseBB;
        has_br = false;
        ast.elseBranch->accept(*this);
        if (!builder->BB_->get_terminator()) {
            builder->create_br(nextIf);
            nextIfReachable = true;
        }
    }

    // 检查 bb 的分支指令是否跳转到 target
    auto branchesTo = [](BasicBlock *bb, BasicBlock *target) -> bool {
        auto *term = bb->get_terminator();
        if (!term) return false;
        for (unsigned i = 0; i < term->num_ops(); i++)
            if (term->get_operand(i) == target) return true;
        return false;
    };

    // 如果两个分支都提前终止（没有 br nextIf）且 nextIf 是独立块，则 nextIf 不可达
    if (ast.elseBranch != nullptr && !nextIfReachable &&
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

void GenIR::visit(WhileStmtAST &ast) {
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
    ast.condition->accept(*this);
    if (recentVal->type_ == INT32_T) {
        recentVal = builder->create_icmp_ne(recentVal, CONST_INT(0));
    } else if (recentVal->type_ == FLOAT_T) {
        recentVal = builder->create_fcmp_ne(recentVal, CONST_FLOAT(0.0));
    }
    builder->create_cond_br(recentVal, trueBB, falseBB);

    builder->BB_ = trueBB;
    has_br = false;
    ast.body->accept(*this);
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

void GenIR::visit(BinaryExprAST &ast) {
    if(isTensorElementwise(ast.op) && containsTensorValue(this, &ast) && !containsCall(&ast)){
        recentVal = lowerFusedTensorExpression(this,&ast);
        return;
    }
    if (ast.op == BinaryOp::LogicalAnd) {
        auto savedTrue = trueBB;
        trueBB = createNamedBB(module.get(), currentFunction, "land.rhs");
        ast.left->accept(*this);
        if (recentVal->type_ == INT32_T)
            recentVal = builder->create_icmp_ne(recentVal, CONST_INT(0));
        else if (recentVal->type_ == FLOAT_T)
            recentVal = builder->create_fcmp_ne(recentVal, CONST_FLOAT(0));
        builder->create_cond_br(recentVal, trueBB, falseBB);
        builder->BB_ = trueBB;
        has_br = false;
        trueBB = savedTrue;
        ast.right->accept(*this);
        return;
    }
    if (ast.op == BinaryOp::LogicalOr) {
        auto savedFalse = falseBB;
        falseBB = createNamedBB(module.get(), currentFunction, "lor.rhs");
        ast.left->accept(*this);
        if (recentVal->type_ == INT32_T)
            recentVal = builder->create_icmp_ne(recentVal, CONST_INT(0));
        else if (recentVal->type_ == FLOAT_T)
            recentVal = builder->create_fcmp_ne(recentVal, CONST_FLOAT(0));
        builder->create_cond_br(recentVal, trueBB, falseBB);
        builder->BB_ = falseBB;
        has_br = false;
        falseBB = savedFalse;
        ast.right->accept(*this);
        return;
    }
    if (ast.op == BinaryOp::Multiply || ast.op == BinaryOp::Divide ||
        ast.op == BinaryOp::Remainder || ast.op == BinaryOp::Matmul) {
        lowerMultiplicative(ast);
        return;
    }

    Value* val[2]; //lVal, rVal
    ast.left->accept(*this);
    val[0] = recentVal;
    VectorType *savedExpected = expectedVectorType;
    ScalarizedVectorType *savedScalarizedExpected =
        expectedScalarizedVectorType;
    if (auto *lhsVector = dynamic_cast<VectorType *>(val[0]->type_))
        expectedVectorType = lhsVector;
    if (auto *lhsVector = asScalarizedVector(val[0]))
        expectedScalarizedVectorType =
            static_cast<ScalarizedVectorType *>(lhsVector->type_);
    ast.right->accept(*this);
    expectedVectorType = savedExpected;
    expectedScalarizedVectorType = savedScalarizedExpected;
    val[1] = recentVal;

    if (ast.op == BinaryOp::Less || ast.op == BinaryOp::LessEqual ||
        ast.op == BinaryOp::Greater || ast.op == BinaryOp::GreaterEqual ||
        ast.op == BinaryOp::Equal || ast.op == BinaryOp::NotEqual) {
        checkCalType(val);
        const bool integer = val[0]->type_ == INT32_T;
        switch (ast.op) {
          case BinaryOp::Less:
            recentVal = integer
                ? static_cast<Value *>(builder->create_icmp_lt(val[0], val[1]))
                : static_cast<Value *>(builder->create_fcmp_lt(val[0], val[1]));
            break;
          case BinaryOp::LessEqual:
            recentVal = integer
                ? static_cast<Value *>(builder->create_icmp_le(val[0], val[1]))
                : static_cast<Value *>(builder->create_fcmp_le(val[0], val[1]));
            break;
          case BinaryOp::Greater:
            recentVal = integer
                ? static_cast<Value *>(builder->create_icmp_gt(val[0], val[1]))
                : static_cast<Value *>(builder->create_fcmp_gt(val[0], val[1]));
            break;
          case BinaryOp::GreaterEqual:
            recentVal = integer
                ? static_cast<Value *>(builder->create_icmp_ge(val[0], val[1]))
                : static_cast<Value *>(builder->create_fcmp_ge(val[0], val[1]));
            break;
          case BinaryOp::Equal:
            recentVal = integer
                ? static_cast<Value *>(builder->create_icmp_eq(val[0], val[1]))
                : static_cast<Value *>(builder->create_fcmp_eq(val[0], val[1]));
            break;
          case BinaryOp::NotEqual:
            recentVal = integer
                ? static_cast<Value *>(builder->create_icmp_ne(val[0], val[1]))
                : static_cast<Value *>(builder->create_fcmp_ne(val[0], val[1]));
            break;
          default: break;
        }
        return;
    }

    if (asScalarizedVector(val[0]) || asScalarizedVector(val[1])) {
        recentVal = scalarizedVectorBinary(
            this, val[0], val[1],
            ast.op == BinaryOp::Add ? Instruction::Add : Instruction::Sub,
            ast.op == BinaryOp::Add ? Instruction::FAdd : Instruction::FSub);
        return;
    }

    //若都是常量
    if (useConst && val[0]->type_->tid_ != Type::VectorTyID &&
        val[1]->type_->tid_ != Type::VectorTyID) {
        int intVal[3]; //lInt, rInt, relInt;
        float floatVal[3]; // lFloat, rFloat, relFloat;
        bool resultIsInt = checkCalType(val, intVal, floatVal);
        switch (ast.op) {
            case BinaryOp::Add:
                intVal[2] = intVal[0] + intVal[1];
                floatVal[2] = floatVal[0] + floatVal[1];
                break;
            case BinaryOp::Subtract:
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
        Instruction::OpID op = ast.op == BinaryOp::Add ? Instruction::Add
                                                        : Instruction::Sub;
        if (lhs && (lhs->type_->tid_ == Type::VectorTyID) &&
            static_cast<VectorType *>(lhs->type_)->contained_ == FLOAT_T)
            op = ast.op == BinaryOp::Add ? Instruction::FAdd
                                          : Instruction::FSub;
        if (lhs && rhs) {
            recentVal = foldConstantVector(module.get(), op, lhs, rhs);
            if (recentVal)
                return;
        }
    }
    if (isIntegerValueType(val[0]->type_)) {
        switch (ast.op) {
            case BinaryOp::Add:
                recentVal = builder->create_iadd(val[0], val[1]);
                break;
            case BinaryOp::Subtract:
                recentVal = builder->create_isub(val[0], val[1]);
                break;
        }
    } else {
        switch (ast.op) {
            case BinaryOp::Add:
                recentVal = builder->create_fadd(val[0], val[1]);
                break;
            case BinaryOp::Subtract:
                recentVal = builder->create_fsub(val[0], val[1]);
                break;
        }
    }
}

void GenIR::lowerMultiplicative(BinaryExprAST &ast) {
    Value* val[2]; //lVal, rVal
    ArrayType* savedTensorExpected = expectedTensorType;
    Value* savedTensorTarget = expectedTensorTarget;
    if(ast.op == BinaryOp::Matmul){
        expectedTensorType = nullptr;
        expectedTensorTarget = nullptr;
    }
    ast.left->accept(*this);
    val[0] = recentVal;
    VectorType *savedExpected = expectedVectorType;
    ScalarizedVectorType *savedScalarizedExpected =
        expectedScalarizedVectorType;
    if (auto *lhsVector = dynamic_cast<VectorType *>(val[0]->type_))
        expectedVectorType = lhsVector;
    if (auto *lhsVector = asScalarizedVector(val[0]))
        expectedScalarizedVectorType =
            static_cast<ScalarizedVectorType *>(lhsVector->type_);
    ast.right->accept(*this);
    expectedVectorType = savedExpected;
    expectedScalarizedVectorType = savedScalarizedExpected;
    if(ast.op == BinaryOp::Matmul){
        expectedTensorType = savedTensorExpected;
        expectedTensorTarget = savedTensorTarget;
    }
    val[1] = recentVal;
    if(ast.op == BinaryOp::Matmul){
        recentVal = lowerTensorMatMul(this, val[0], val[1]);
        return;
    }
    if(val[0]->hasSemFlag(SemFlag::SrcTensor) || val[1]->hasSemFlag(SemFlag::SrcTensor)){
        recentVal = lowerTensorBinary(this,val[0],val[1],ast.op);
        return;
    }
    if (asScalarizedVector(val[0]) || asScalarizedVector(val[1])) {
        Instruction::OpID integerOp = Instruction::Mul;
        Instruction::OpID floatingOp = Instruction::FMul;
        if (ast.op == BinaryOp::Divide) {
            integerOp = Instruction::SDiv;
            floatingOp = Instruction::FDiv;
        } else if (ast.op == BinaryOp::Remainder) {
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
            case BinaryOp::Multiply:
                intVal[2] = intVal[0] * intVal[1];
                floatVal[2] = floatVal[0] * floatVal[1];
                break;
            case BinaryOp::Divide:
                intVal[2] = intVal[0] / intVal[1];
                floatVal[2] = floatVal[0] / floatVal[1];
                break;
            case BinaryOp::Remainder:
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
            op = ast.op == BinaryOp::Multiply ? Instruction::FMul
                                               : Instruction::FDiv;
        } else if (ast.op == BinaryOp::Divide) {
            op = Instruction::SDiv;
        } else if (ast.op == BinaryOp::Remainder) {
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
        if (vectorType && (ast.op == BinaryOp::Divide ||
                           ast.op == BinaryOp::Remainder)) {
            recentVal = scalarizeIntegerVectorBinary(
                builder, module.get(), val[0], val[1],
                ast.op == BinaryOp::Divide ? Instruction::SDiv
                                            : Instruction::SRem,
                vectorType);
            return;
        }
        switch (ast.op) {
            case BinaryOp::Multiply:
                recentVal = builder->create_imul(val[0], val[1]);
                break;
            case BinaryOp::Divide:
                recentVal = builder->create_isdiv(val[0], val[1]);
                break;
            case BinaryOp::Remainder:
                recentVal = builder->create_isrem(val[0], val[1]);
                break;
        }
    }
    else {
        switch (ast.op) {
            case BinaryOp::Multiply:
                recentVal = builder->create_fmul(val[0], val[1]);
                break;
            case BinaryOp::Divide:
                recentVal = builder->create_fdiv(val[0], val[1]);
                break;
            case BinaryOp::Remainder://never occur
                break;
        }
    }
}

void GenIR::visit(UnaryExprAST &ast) {
    if((ast.op == UnaryOp::Plus || ast.op == UnaryOp::Minus) && !containsCall(&ast) && containsTensorValue(this, &ast)){
        recentVal = lowerFusedTensorExpression(this, &ast);
        return;
    }
    ast.operand->accept(*this);
    if(recentVal->hasSemFlag(SemFlag::SrcTensor)&&(ast.op == UnaryOp::Plus || ast.op == UnaryOp::Minus)){
        if(ast.op == UnaryOp::Minus){
            ArrayType* type =expectedVectorType ? expectedTensorType : tensorType(recentVal);
            Value* zero = zeroScalar(module.get(),tensorElementType(type));
            recentVal = lowerTensorBinary(this, zero, recentVal, BinaryOp::Subtract);
        }
        return;
    }
    if (auto *vector = asScalarizedVector(recentVal)) {
        recentVal = scalarizedVectorUnary(this, vector, ast.op);
        return;
    }
    if (useConst) {
        if (ast.op == UnaryOp::Minus) {
            if (auto *vector = dynamic_cast<ConstantVector *>(recentVal)) {
                auto *type = static_cast<VectorType *>(vector->type_);
                std::vector<Constant *> lanes;
                for (auto *lane : vector->elements_) {
                    if (auto *integer = dynamic_cast<ConstantInt *>(lane))
                        lanes.push_back(new ConstantInt(type->contained_,
                                                        -integer->value_));
                    else
                        lanes.push_back(new ConstantFloat(
                            type->contained_,
                            -static_cast<ConstantFloat *>(lane)->value_));
                }
                recentVal = new ConstantVector(type, lanes);
            } else if (auto *integer = dynamic_cast<ConstantInt *>(recentVal)) {
                integer->value_ = -integer->value_;
            } else {
                auto *floating = static_cast<ConstantFloat *>(recentVal);
                floating->value_ = -floating->value_;
            }
        } else if (ast.op == UnaryOp::LogicalNot) {
            if (auto *integer = dynamic_cast<ConstantInt *>(recentVal))
                recentVal = CONST_INT(integer->value_ == 0);
            else if (auto *floating =
                         dynamic_cast<ConstantFloat *>(recentVal))
                recentVal = CONST_INT(floating->value_ == 0.0f);
        }
        return;
    }
    if (recentVal->type_ == VOID_T) return;
    if (recentVal->type_ == INT1_T)
        recentVal = builder->create_zext(recentVal, INT32_T);
    if (isIntegerValueType(recentVal->type_)) {
        if (ast.op == UnaryOp::Minus)
            recentVal = builder->create_isub(
                recentVal->type_->tid_ == Type::VectorTyID
                    ? (Value *)new ConstantZero(recentVal->type_)
                    : (Value *)CONST_INT(0),
                recentVal);
        else if (ast.op == UnaryOp::LogicalNot)
            recentVal = builder->create_zext(
                builder->create_icmp_eq(recentVal, CONST_INT(0)), INT32_T);
    } else {
        if (ast.op == UnaryOp::Minus)
            recentVal = builder->create_fsub(
                recentVal->type_->tid_ == Type::VectorTyID
                    ? (Value *)new ConstantZero(recentVal->type_)
                    : (Value *)CONST_FLOAT(0),
                recentVal);
        else if (ast.op == UnaryOp::LogicalNot)
            recentVal = builder->create_zext(
                builder->create_fcmp_eq(recentVal, CONST_FLOAT(0)), INT32_T);
    }
}

void GenIR::visit(SubscriptExprAST &ast) {
    ast.base->accept(*this);
    Value *base = recentVal;
    if(base->hasSemFlag(SemFlag::SrcTensor)){
        ArrayType* type = tensorType(base);
        ast.index->accept(*this);
        auto dynamicDimension = tensorFirstDimensions.find(base);
        if(dynamicDimension != tensorFirstDimensions.end()){
            Value* element = builder->create_gep(base,{recentVal});
            if(type){
                element->setSemFlag(SemFlag::SrcTensor);
                moveTensorStorage(base,element);
                tensorFirstDimensions.erase(dynamicDimension);
                recentVal = element;
            }
            else{
                recentVal = builder->create_load(element);
                releaseTensor(this, base);
            }
            return;
        }
        // not found
        Value* element = builder->create_gep(base, {CONST_INT(0), recentVal});
        if(type->contained_->tid_ == Type::ArrayTyID){
            element->setSemFlag(SemFlag::SrcTensor);
            moveTensorStorage(base, element);
            recentVal = element;
        }
        else{
            recentVal = builder->create_load(element);
            releaseTensor(this, base);
        }
        return;
    }
    if (auto *vector = asScalarizedVector(base)) {
        auto *type = static_cast<ScalarizedVectorType *>(vector->type_);
        ast.index->accept(*this);
        Value *index = recentVal;
        if (auto *constant = dynamic_cast<ConstantInt *>(index)) {
            if (constant->value_ < 0 ||
                static_cast<unsigned>(constant->value_) >=
                    type->num_elements_) {
                std::cerr << "constant vector lane index is out of range\n";
                std::exit(1);
            }
            recentVal = vector->lanes_[constant->value_];
            return;
        }
        auto *slot = builder->create_alloca(type);
        storeScalarizedVector(this, slot, vector, type);
        recentVal = builder->create_load(
            builder->create_gep(slot, {CONST_INT(0), index}));
        return;
    }
    if (auto *pointer = dynamic_cast<PointerType *>(base->type_))
        if (pointer->contained_->tid_ == Type::VectorTyID)
            base = builder->create_load(base);
    auto *type = dynamic_cast<VectorType *>(base->type_);
    if (!type) {
        std::cerr << "lane extract requires a fixed vector value\n";
        std::exit(1);
    }
    ast.index->accept(*this);
    Value *index = recentVal;
    if (auto *constant = dynamic_cast<ConstantInt *>(index)) {
        if (constant->value_ < 0 ||
            static_cast<unsigned>(constant->value_) >= type->num_elements_) {
            std::cerr << "constant vector lane index is out of range\n";
            std::exit(1);
        }
        if (auto *constantVector = dynamic_cast<ConstantVector *>(base)) {
            recentVal = constantVector->elements_[constant->value_];
            return;
        }
    }
    recentVal = new ExtractElementInst(base, index, builder->BB_);
}

void GenIR::visit(AggregateExprAST &ast) {
    if (expectedScalarizedVectorType) {
        recentVal = buildScalarizedVectorInitializer(
            this, ast.initializer.get(), expectedScalarizedVectorType);
        return;
    }
    VectorType *type =
        inferVectorLiteralType(module.get(), ast.initializer.get());
    recentVal = buildVectorInitializer(this, ast.initializer.get(), type);
}

void GenIR::visit(LValueAST &ast) {
    bool isTrueLVal = requireLVal; //是否真是作为左值
    requireLVal = false;
    auto var = scope.find(ast.name);
    if (auto *vector = asScalarizedVector(var)) {
        if (ast.indices.empty()) {
            recentVal = vector;
            return;
        }
        if (ast.indices.size() != 1) {
            std::cerr << "a fixed vector requires exactly one lane index\n";
            std::exit(1);
        }
        ast.indices[0]->accept(*this);
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
            if (ast.indices.empty()) {
                recentVal = vector;
                return;
            }
            if (ast.indices.size() != 1) {
                std::cerr << "a fixed vector requires exactly one lane index\n";
                std::exit(1);
            }
            ast.indices[0]->accept(*this);
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
    //全局作用域内，一定使用常量，全局作用域下访问左值时useConst已置为true
    if (scope.in_global()) {
        //不是数组，直接返回该常量
        if (ast.indices.empty()) {
            recentVal = var;
            return;
        }
        if (auto *vector = dynamic_cast<ConstantVector *>(var)) {
            if (ast.indices.size() != 1) {
                std::cerr << "a fixed vector requires exactly one lane index\n";
                std::exit(1);
            }
            ast.indices[0]->accept(*this);
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
        for (auto &exp : ast.indices) {
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
        if (ast.indices.empty()) {
            recentVal = constantVector;
            return;
        }
        if (ast.indices.size() != 1) {
            std::cerr << "a fixed vector requires exactly one lane index\n";
            std::exit(1);
        }
        ast.indices[0]->accept(*this);
        auto *constantIndex = dynamic_cast<ConstantInt *>(recentVal);
        if (constantIndex) {
            if (constantIndex->value_ < 0 ||
                (unsigned)constantIndex->value_ >= constantVector->elements_.size()) {
                std::cerr << "constant vector lane index is out of range\n";
                std::exit(1);
            }
            recentVal = constantVector->elements_[constantIndex->value_];
        } else {
            recentVal = new ExtractElementInst(constantVector, recentVal,
                                               builder->BB_);
        }
        return;
    }
    // 不是常量那么var一定是指针类型
    Type* varType = static_cast<PointerType*>(var->type_)->contained_; //所指的类型
    if(var->hasSemFlag(SemFlag::SrcTensor) && ast.indices.empty()){
        recentVal = varType->tid_ == Type::PointerTyID ? (Value *)builder->create_load(var) : var;
        recentVal->setSemFlag(SemFlag::SrcTensor);
        auto dimension = tensorFirstDimensions.find(var);
        if(dimension != tensorFirstDimensions.end()){
            tensorFirstDimensions[recentVal] = dimension->second;
        }
        return;
    }
    if (auto *type = dynamic_cast<ScalarizedVectorType *>(varType)) {
        if (ast.indices.empty()) {
            recentVal = isTrueLVal ? var
                                   : (Value *)loadScalarizedVector(this, var, type);
            return;
        }
        if (ast.indices.size() != 1) {
            std::cerr << "a fixed vector requires exactly one lane index\n";
            std::exit(1);
        }
        ast.indices[0]->accept(*this);
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
        if (ast.indices.empty()) {
            recentVal = isTrueLVal ? var : (Value *)builder->create_load(var);
            return;
        }
        if (ast.indices.size() != 1) {
            std::cerr << "a fixed vector requires exactly one lane index\n";
            std::exit(1);
        }
        ast.indices[0]->accept(*this);
        Value *index = recentVal;
        auto *constantIndex = dynamic_cast<ConstantInt *>(index);
        auto *type = static_cast<VectorType *>(varType);
        if (constantIndex &&
            (constantIndex->value_ < 0 ||
             (unsigned)constantIndex->value_ >= type->num_elements_)) {
            std::cerr << "constant vector lane index is out of range\n";
            std::exit(1);
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
    if (!ast.indices.empty()) {
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
            for (auto &exp : ast.indices) {
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
    if (!ast.indices.empty()) { //说明是数组
        vector<Value *> idxs;
        for (auto &exp : ast.indices) {
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

void GenIR::visit(LiteralExprAST &ast) {
    if (const auto *integer = std::get_if<int>(&ast.value))
        recentVal = CONST_INT(*integer);
    else
        recentVal = CONST_FLOAT(std::get<float>(ast.value));
}

static Value *lowerRuntimeString(GenIR *gen, const string &text) {
    auto *byteType = gen->module->int8_ty_;
    auto *arrayType = gen->module->get_array_type(byteType, text.size() + 1);
    std::vector<Constant *> bytes;
    bytes.reserve(text.size() + 1);
    for (unsigned char byte : text)
        bytes.push_back(new ConstantInt(byteType, byte));
    bytes.push_back(new ConstantInt(byteType, 0));
    auto *initializer = new ConstantArray(arrayType, bytes);
    // '$' cannot occur in a SysY identifier, so a user global cannot collide
    // with this compiler-owned symbol.
    auto name = "__sysy_string$" +
                std::to_string(gen->stringLiteralCounter++);
    auto *global = new GlobalVariable(name, gen->module.get(), arrayType,
                                      true, initializer);
    std::vector<Value *> indices{
        new ConstantInt(gen->module->int32_ty_, 0),
        new ConstantInt(gen->module->int32_ty_, 0)};
    return gen->builder->create_gep(
        global, indices);
}

void GenIR::visit(CallExprAST &ast) {
    // 将 C 宏名映射到 SysY 运行时函数
    if (ast.callee == "starttime") ast.callee = "_sysy_starttime";
    else if (ast.callee == "stoptime") ast.callee = "_sysy_stoptime";

    auto fun = (Function *)scope.find(ast.callee);
    if (!fun) {
        std::cerr << "Error: undefined function '" << ast.callee << "'\n";
        exit(1);
    }
    is_single_exp = false;
    vector<Value *> args;
    vector<Value *> tensorArgumentsToRelease;
    auto *functionType = static_cast<FunctionType *>(fun->type_);
    auto infoIt = functionLoweringInfo.find(fun);
    FunctionLoweringInfo *info = infoIt == functionLoweringInfo.end() ? nullptr : &infoIt -> second;
    Value *tensorResultSlot = nullptr;
    Value *scalarizedResultSlot = nullptr;
    unsigned argumentOffset = 0;
    if (info && info->returnType) {
        scalarizedResultSlot = builder->create_alloca(info->returnType);
        args.push_back(scalarizedResultSlot);
        argumentOffset = 1;
    }else if(info && info->tensorReturn){
        ArrayType *type = info ->tensorReturnType ? info -> tensorReturnType : expectedTensorType;
        tensorResultSlot = expectedTensorTarget ? expectedTensorTarget : (Value *)builder->create_alloca(type);
        tensorResultSlot->setSemFlag(SemFlag::SrcTensor);
        Type *elementType = static_cast<PointerType *>(functionType->args_[0])->contained_;
        args.push_back(tensorData(this, tensorResultSlot, elementType));
        argumentOffset = 1;
    }
    const unsigned fixedArgumentCount = functionType->args_.size();
    unsigned fixedIndex = argumentOffset;
    ArrayType *savedTensorExpected = expectedTensorType;
    Value *savedTensorTarget = expectedTensorTarget;
    expectedTensorType = nullptr;
    expectedTensorTarget = nullptr;
    for (int i = 0; i < ast.arguments.size(); i++) {
        auto &argument = ast.arguments[i];
        if (const auto *text = std::get_if<std::string>(&argument)) {
            if (fixedIndex >= fixedArgumentCount ||
                functionType->args_[fixedIndex]->tid_ != Type::PointerTyID ||
                static_cast<PointerType *>(functionType->args_[fixedIndex])->contained_ !=
                    module->int8_ty_) {
                std::cerr << "string literal is only valid for a string runtime parameter\n";
                std::exit(1);
            }
            args.push_back(lowerRuntimeString(this, *text));
            ++fixedIndex;
            continue;
        }
        VectorType *savedExpected = expectedVectorType;
        ScalarizedVectorType *savedScalarizedExpected =
            expectedScalarizedVectorType;
        // ScalarizedVectorType *scalarizedParameter = info && static_cast<unsigned>(i) < info->valueParameters.size() ? info->valueParameters[i] : nullptr;
        bool tensorParameter = info && static_cast<unsigned>(i) < info -> tensorParameters.size() && info -> tensorParameters[i];
        // expectedScalarizedVectorType = scalarizedParameter;
        Type *fixedParameterType =
            fixedIndex < fixedArgumentCount
                ? functionType->args_[fixedIndex]
                : nullptr;
        expectedVectorType = dynamic_cast<VectorType *>(fixedParameterType);
        std::get<std::unique_ptr<ExprAST>>(argument)->accept(*this);
        expectedVectorType = savedExpected;
        expectedScalarizedVectorType = savedScalarizedExpected;
        if (heapTensorStorage.find(recentVal) != heapTensorStorage.end()) 
            tensorArgumentsToRelease.push_back(recentVal);
        // if (scalarizedParameter) {
        //     auto *value = asScalarizedVector(recentVal);
        //     auto *slot = builder->create_alloca(scalarizedParameter);
        //     storeScalarizedVector(this, slot, value, scalarizedParameter);
        //     args.push_back(slot);
        //     ++fixedIndex;
        //     continue;
        // }
        Value *firstDim = tensorParameter ? tensorFirstDimension(this, recentVal) : nullptr;
        if(fixedParameterType && fixedParameterType->tid_ == Type::PointerTyID && recentVal ->type_->tid_==Type::PointerTyID && recentVal -> type_ != fixedParameterType && recentVal ->hasSemFlag(SemFlag::SrcTensor)){
            recentVal = builder->create_bitcast(recentVal, fixedParameterType);
            recentVal->setSemFlag(SemFlag::SrcTensor);
        }
        //检查函数形参与实参类型是否匹配
        if (fixedParameterType && recentVal->type_ == INT32_T &&
            fixedParameterType == FLOAT_T) {
            recentVal = builder->create_sitofp(recentVal, FLOAT_T);
        } else if (fixedParameterType && recentVal->type_ == FLOAT_T &&
                   fixedParameterType == INT32_T) {
            recentVal = builder->create_fptosi(recentVal, INT32_T);
        }
        args.push_back(recentVal);
        ++fixedIndex;
        if(tensorParameter){
            args.push_back(firstDim);
            ++fixedIndex;
        }
    }   
    expectedTensorType = savedTensorExpected;
    expectedTensorTarget = savedTensorTarget;
    // starttime/stoptime 在源码中无实参，但运行时函数需要一个行号参数
    if (ast.arguments.empty() &&
        (ast.callee == "_sysy_starttime" || ast.callee == "_sysy_stoptime")) {
        args.push_back(new ConstantInt(INT32_T, ast.line));
    }
    recentVal = builder->create_call(fun, args);
    for(auto *arg : tensorArgumentsToRelease) releaseTensor(this, arg);
    if(tensorResultSlot) recentVal = tensorResultSlot;
    else if(info && info -> returnType)
        recentVal = loadScalarizedVector(this, scalarizedResultSlot,
                                         info->returnType);
}
