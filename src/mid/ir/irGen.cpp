// IRGen 把前端 AST 降为未优化的中端 IR。标量局部变量先放入 entry alloca，控制语句显式
// 建立基本块和分支，非 void 函数经统一 retval/return block 返回；tensor 使用数组存储和
// 隐藏返回指针。后续 Mem2Reg、CFG 化简和向量优化均依赖这里生成的 use-def 关系。
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

ArrayType* expectedTensorType = nullptr; // 上下文给出的静态结果形状
Value* expectedTensorTarget = nullptr;   // 可供表达式直接写入的目标存储
bool currentTensorReturn = false;        // 当前函数是否采用 tensor 隐式返回参数
Value* tensorReturnPointer = nullptr;    // 调用者传入的 tensor 返回缓冲区
Type* curType;                          // 当前 decl 类型
bool curSourceTensor = false;
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

struct FunctionLoweringInfo {
    bool tensorReturn = false;                  // 是否通过隐藏指针返回 tensor
    ArrayType* tensorReturnType = nullptr;      // 已推导出的静态 tensor 返回形状
    vector<bool> tensorParameters;              // 与源级形参一一对应
};

// 保存函数 ABI 扩展信息，以及动态 tensor 的形状和堆存储所有权。
static std::unordered_map<Function *, FunctionLoweringInfo>
    functionLoweringInfo;
static std::unordered_set<std::string> pendingTensorParameters;
static std::unordered_map<Value*, Value*> tensorFirstDimensions; // Value -> 运行时首维
static std::unordered_map<Value*, Value*> heapTensorStorage;     // tensor/view -> malloc 原始指针
static Function* tensorMalloc = nullptr;
static Function* tensorFree = nullptr;
static BasicBlock* createNamedBB(Module* m, Function* func, const std::string &base);
static Type *scalarType(Module *module, TYPE element);


// 功能：从 tensor 指针的 pointee 中取得当前仍然静态可见的数组尾部类型。
static ArrayType* tensorType(Value *val){
    return dynamic_cast<ArrayType *>(static_cast<PointerType *>(val->type_)->contained_);
}

// 功能：仅为所有维度都静态的 tensor 返回完整数组类型；动态首维返回 nullptr。
static ArrayType* staticTensorType(Value *val){
    if(tensorFirstDimensions.find(val) != tensorFirstDimensions.end()){
        return nullptr;
    }
    return tensorType(val);
}

// 功能：剥离全部数组维度，得到 tensor 的 i32 或 float 标量元素类型。
static Type* tensorElementType(Type *type){
    while(type->tid_==Type::ArrayTyID){
        type = static_cast<ArrayType *>(type)->contained_;
    }
    return type;
}

// 功能：计算静态 tensor 形状中各维长度的乘积。
static unsigned tensorElementCount(ArrayType *type){
    unsigned count = 1;
    Type *current = type;
    while(current->tid_==Type::ArrayTyID){
        auto *array = static_cast<ArrayType *>(current);
        count *= array->num_elements_;
        current = array->contained_;
    }
    return count;
}

// 功能：生成 tensor 的总元素数；动态首维与静态尾部长度在 IR 中相乘。
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

// 功能：优先返回记录的运行时首维，否则使用静态数组的最外层长度。
static Value* tensorFirstDimension(GenIR *gen, Value *value){
    auto found = tensorFirstDimensions.find(value);
    if(found != tensorFirstDimensions.end())
        return found->second;
    return new ConstantInt(gen->module->int32_ty_, tensorType(value) -> num_elements_);
}

// 功能：按 tensor 元素类型构造零值，供点积归约和一元负号 lowering 使用。
static Constant *zeroScalar(Module *module, Type *type) {
    if (type == module->float32_ty_)
        return new ConstantFloat(module->float32_ty_, 0.0f);
    return new ConstantInt(module->int32_ty_, 0);
}

// 功能：把 tensor 存储统一视作一维标量指针，供线性下标访问。
static Value* tensorData(GenIR *gen, Value *value, Type *elementType){
    Type* pointerType = gen->module->get_pointer_type(elementType);
    if(value->type_ == pointerType) 
        return value;
    return gen->builder->create_bitcast(value, pointerType);
}

// 功能：为动态形状 tensor 发射 malloc，并登记首维和所有权供后续释放。
static Value* createDynamicTensor(GenIR *gen, Value *model, Value *count, Type *pointerType = nullptr){
    if(!tensorMalloc){
        Type* bytePointer = gen->module->get_pointer_type(gen->module->int8_ty_);//?
        tensorMalloc = new Function(new FunctionType(bytePointer, {gen->module->int32_ty_}), "malloc", gen->module.get());
        tensorFree = new Function(new FunctionType(gen->module->void_ty_, {bytePointer}), "free", gen->module.get());
    }
    // 当前 tensor 元素仅支持 i32/float，二者均占 4 字节。
    Value *bytes = gen->builder->create_imul(count, new ConstantInt(gen->module->int32_ty_, 4));
    Value *storage = gen->builder->create_call(tensorMalloc, {bytes});
    Value *result = gen->builder->create_bitcast(storage, pointerType ? pointerType : model->type_);
    result -> setSemFlag(SemFlag::SrcTensor);
    tensorFirstDimensions[result] = tensorFirstDimension(gen, model);
    heapTensorStorage[result] = storage;
    return result;
}

// 功能：仅释放本 lowering 创建且仍由该 Value 持有的动态 tensor。
static void releaseTensor(GenIR *gen, Value *value){
    auto found = heapTensorStorage.find(value);
    if(found == heapTensorStorage.end()) return;
    gen->builder->create_call(tensorFree, {found->second});
    heapTensorStorage.erase(found), tensorFirstDimensions.erase(value);
}

// 功能：tensor 下标产生子视图时，把堆存储所有权从旧 Value 转交给新 Value。
static void moveTensorStorage(Value* from, Value* to){
    auto found = heapTensorStorage.find(from);
    if(found == heapTensorStorage.end()) return;
    heapTensorStorage[to] = found->second;
    heapTensorStorage.erase(found);
}

// 功能：发射一个线性循环，将 source 的全部 tensor 元素复制到 target。
static void copyTensor(GenIR *gen, Value* target, Value* source, ArrayType* type){
    Type *elementType = type ? tensorElementType(type) : tensorElementType(static_cast<PointerType *>(source->type_)->contained_);
    Value *targetData = tensorData(gen, target, elementType);
    Value *sourceData = tensorData(gen, source, elementType);
    Value *count = type ? (Value *) new ConstantInt(gen->module->int32_ty_, tensorElementCount(type)) : tensorElementCount(gen, source);
    // 意图：统一展平静态和动态形状，避免为每个维度分别构造嵌套循环。
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

// 功能：根据元素类型为一次 tensor 标量运算选择整数或浮点 IR 指令。
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

// 功能：将 tensor-tensor 或 tensor-scalar 二元运算降低为逐元素线性循环。
static Value* lowerTensorBinary(GenIR *gen, Value *lhs, Value *rhs, BinaryOp op){
    bool lhsTensor = lhs->hasSemFlag(SemFlag::SrcTensor);
    bool rhsTensor = rhs->hasSemFlag(SemFlag::SrcTensor);
    Value *model = lhsTensor ? lhs : rhs;
    ArrayType *type = expectedTensorType;
    if(!type && lhsTensor) type = staticTensorType(lhs);
    if(!type && rhsTensor) type = staticTensorType(rhs);
    Type *elementType = type ? tensorElementType(type) : tensorElementType(static_cast<PointerType *>(model->type_)->contained_);
    Value *count = type ? (Value *)new ConstantInt(gen->module->int32_ty_, tensorElementCount(type)) : tensorElementCount(gen, model);
    // 意图：静态形状使用栈上数组；动态首维使用带所有权记录的堆存储。
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
    // 意图：标量操作数在每次迭代复用，tensor 操作数按同一线性下标加载。
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

// 功能：只检查 AST 是否含完整 tensor 值，不发射 IR；下标后的标量不算 tensor 值。
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

// 功能：把二维 tensor 矩阵乘法 MxK @ KxN 降低为三重循环。
static Value *lowerTensorMatMul(GenIR *gen, Value *lhs, Value *rhs){
    ArrayType *leftType = tensorType(lhs);
    ArrayType *rightType = tensorType(rhs);
    ArrayType *leftRowType = dynamic_cast<ArrayType *>(leftType->contained_);
    ArrayType *rightRowType = dynamic_cast<ArrayType *>(rightType->contained_);
    // 意图：优先采用赋值/返回上下文给出的结果形状，否则从两侧操作数推导。
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
    // 意图：行数静态可知时保留完整数组类型，否则分配动态结果并记录运行时首维。
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
    // 意图：显式构造 row/column/common 三层 CFG，common 层计算一个输出元素的点积。
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
    // 意图：两侧均按行主序展平，分别访问 lhs[row][k] 与 rhs[k][column]。
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

    // 操作数若是本表达式产生的动态临时量，其最后一次使用在此结束。
    releaseTensor(gen, lhs);
    releaseTensor(gen, rhs);
    return result;
}

// 功能：检测表达式内的调用，避免跨调用把外层目标缓冲区错误地下传给子表达式。
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

// 用轻量表达式树暂存已求值叶子，使整段逐元素表达式只生成一个遍历循环。
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

// 功能：判断运算符能否在 tensor 的每个元素上独立执行。
static bool isTensorElementwise(BinaryOp op){
    return op == BinaryOp::Add || op == BinaryOp::Subtract ||
           op == BinaryOp::Multiply || op == BinaryOp::Divide ||
           op == BinaryOp::Remainder;
}

// 功能：从融合表达式中选择一个 tensor 叶子，作为动态形状和布局的模型。
static Value *tensorExpressionModel(TensorExpression *expression){
    if(expression -> kind == TensorExpression::Leaf)
        return expression->tensor ? expression -> value : nullptr;

    Value *model = tensorExpressionModel(expression -> left.get());
    if(!model && expression -> right)
        model = tensorExpressionModel(expression -> right.get());
    return model;
}

// 功能：递归收集可融合的逐元素运算；不可融合的子表达式立即求值并成为叶子。
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
    // 意图：叶子求值不能直接占用外层结果缓冲区，否则会覆盖尚未消费的操作数。
    Value *savedTarget = expectedTensorTarget;
    expectedTensorTarget = nullptr;
    expression->accept(*gen);
    expectedTensorTarget = savedTarget;
    return std::unique_ptr<TensorExpression>(new TensorExpression(recentVal, recentVal->hasSemFlag(SemFlag::SrcTensor)));
}

// 功能：为给定线性下标递归发射融合表达式的一次标量计算。
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

// 功能：融合循环完成后，释放树中由子表达式产生的动态 tensor 临时量。
static void releaseTensorExpression(GenIR *gen, TensorExpression *expression){
    if(expression -> kind == TensorExpression::Leaf){
        if(expression -> tensor) releaseTensor(gen, expression->value);
        return;
    }
    releaseTensorExpression(gen, expression->left.get());
    if(expression->right) releaseTensorExpression(gen, expression->right.get());
}

// 功能：把整棵逐元素 tensor 表达式降低为单个线性循环并返回结果存储。
static Value *lowerFusedTensorExpression(GenIR *gen, ExprAST *expression){
    std::unique_ptr<TensorExpression> tree = buildTensorExpression(gen, expression);
    Value *model = tensorExpressionModel(tree.get());
    ArrayType *type = expectedTensorType;
    if(!type) type = staticTensorType(model);
    Type *elementType = type ? tensorElementType(type) : tensorElementType(static_cast<PointerType *> (model -> type_) -> contained_);
    Value * count = type ? (Value *) new ConstantInt(gen->module->int32_ty_, tensorElementCount(type)):tensorElementCount(gen, model);
    // 意图：赋值或返回上下文可提供目标缓冲区，从而省去中间结果和最终复制。
    Value *result = expectedTensorTarget;
    if(!result && type) {
        result = gen->builder->create_alloca(type);
        result->setSemFlag(SemFlag::SrcTensor);
    }
    else if(!result){
        result = createDynamicTensor(gen, model, count);
    }
    Value *resultData = tensorData(gen, result, elementType);
    // 意图：每个元素只遍历一次，在循环体内递归计算完整标量表达式。
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

static Type *scalarType(Module *module, TYPE element) {
    return element == TYPE_INT ? module->int32_ty_ : module->float32_ty_;
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
    curSourceTensor = ast.tensor;
    curType = scalarType(module.get(), ast.type);
    for (auto &object : ast.objects) {
        object->accept(*this);
    }
}

void GenIR::visit(ObjectDefAST &ast) {
    string varName = ast.name;
    //全局变量或常量
    if (scope.in_global()) {
        if (ast.dimensions.empty()) {   //不是数组，即全局量
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
                // tensor 与普通数组共享 IR 类型，用语义位保留源级身份供表达式 lowering 识别。
                if (curSourceTensor) var -> setSemFlag(SemFlag::SrcTensor);
                scope.push(varName, var);
            } else {
                useConst = true; //全局数组量的初始值必为常量
                auto init = globalInit(dimensions, arrayTys, 0,
                                       ast.initializer->elements());
                useConst = false;
                auto var = new GlobalVariable(varName, module.get(), arrayTys[0], isConst, init);
                if (isConst) var->setSemFlag(SemFlag::ImmutableObject);
                // 带初始化的全局 tensor 同样沿用嵌套 ArrayType 表示。
                if (curSourceTensor) var -> setSemFlag(SemFlag::SrcTensor);
                scope.push(varName, var);
            }
        }
        return;
    }


    //局部变量或常量
    if (ast.dimensions.empty()) {   //不是数组，即普通局部量
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
        // 标记局部存储，使后续赋值、下标和调用走 tensor 专用路径。
        if (curSourceTensor) arrayAlloc->setSemFlag(SemFlag::SrcTensor);
        scope.push(varName, arrayAlloc);
        // tensor 表达式初始化优先直接写入声明的数组；未直写时再复制并回收临时量。
        if (curSourceTensor && ast.initializer != nullptr && ast.initializer->isExpression()){
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
        // 从最外层开始解析大括号对齐；递归时 getNextDim 再进入下一层。
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
    // 当前层 up 已被占用，只在更深层中寻找与已初始化元素数对齐的维度。
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
    pendingTensorParameters.clear();
    // 功能：记录当前函数的 tensor ABI 状态，函数结束时统一清空，避免污染下一函数。
    currentTensorReturn = ast.tensorReturn;
    tensorReturnPointer = nullptr;
    Type *retType;
    if(currentTensorReturn){
        // tensor 按值返回降低为 void，并把调用者提供的元素缓冲区放在第一个隐藏形参。
        retType = VOID_T;
        params.push_back(module->get_pointer_type(scalarType(module.get(), ast.returnType)));
        paramNames.push_back("$tensor.return");
    } else if (ast.returnType == TYPE_VOID) {
        retType = VOID_T;
    } else {
        retType = scalarType(module.get(), ast.returnType);
    }
    for (auto &parameter : ast.parameters)
        parameter->accept(*this);

    auto funTy = new FunctionType(retType, params);
    auto func = new Function(funTy, ast.name, module.get());
    currentFunction = func;
    // 保存源级签名中 IR FunctionType 无法表达的 tensor 返回值和 tensor 形参信息。
    FunctionLoweringInfo info;
    info.tensorReturn = currentTensorReturn;
    for(auto &p: ast.parameters)
        info.tensorParameters.push_back(p->tensor);
    functionLoweringInfo[func] = std::move(info);
    scope.push(ast.name, func);
    scope.enter();

    std::vector<Value *> args;
    for (auto arg = func->arguments_.begin(); arg != func->arguments_.end(); ++arg)
        args.push_back(*arg);

    auto *entry = createNamedBB(module.get(), func, "entry");
    builder->BB_ = entry;

    // 普通形参保存在局部槽中；tensor 形参保留其地址和动态维度信息。
    for (int i = 0; i < (int)paramNames.size(); i++) {
        if(currentTensorReturn && i == 0){
            // 隐藏返回指针由 return lowering 直接使用，不为它创建普通局部槽。
            tensorReturnPointer = args[i];
            continue;
        }
        auto *alloc = builder->create_alloca(params[i]);
        alloc->name_ = paramNames[i];
        if(pendingTensorParameters.find(paramNames[i])!=pendingTensorParameters.end()){
            // tensor 参数的指针仍存入局部槽，额外隐藏参数携带无法编码在类型中的首维。
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
    currentTensorReturn = false;
    tensorReturnPointer = nullptr;
}

void GenIR::visit(FuncParamAST &ast) {
    //获取参数类型
    Type *paramType;
    paramType = scalarType(module.get(), ast.type);
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
    if(ast.tensor){
        // 每个 tensor 形参后紧跟一个 i32 隐藏形参，传递其运行时第一维长度。
        pendingTensorParameters.insert(ast.name);
        params.push_back(INT32_T);
        paramNames.push_back("$" + ast.name + ".dim0");
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
    requireLVal = true;  
    ast.target->accept(*this);
    auto var = recentVal;
    if(var->hasSemFlag(SemFlag::SrcTensor)){
        // tensor 赋值下传目标形状和存储，允许无嵌套调用的表达式直接写入左值。
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
        // 不能直写目标时保留按值语义：复制结果，再释放可能的动态临时量。
        if(recentVal != var){
            copyTensor(this, var, recentVal, type);
            releaseTensor(this, recentVal);
        }
        return;
    }

    ast.value->accept(*this);
    auto expval = recentVal;
    Type* varElemType = static_cast<PointerType*>(var->type_)->contained_;
    if (varElemType == FLOAT_T && expval->type_ == INT32_T) {
        expval = builder->create_sitofp(expval, FLOAT_T);
    } else if (varElemType == INT32_T && expval->type_ == FLOAT_T) {
        expval = builder->create_fptosi(expval, INT32_T);
    }
    builder->create_store(expval, var);
}

void GenIR::visit(ExprStmtAST &ast) {
    ast.expression->accept(*this);
    // 独立表达式无人接收其结果，及时释放动态 tensor 临时量。
    releaseTensor(this, recentVal);
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
        // tensor 返回值写入调用者的隐藏缓冲区；顶层调用可直接复用该缓冲区。
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
            // 记录可推导的静态返回形状，供后续调用点选择栈上结果槽。
            if(type){
                functionLoweringInfo[currentFunction].tensorReturnType = type;
            }
            // 非直写表达式在返回前复制到 ABI 缓冲区，并结束临时量生命周期。
            if(recentVal != tensorReturnPointer){
                copyTensor(this, tensorReturnPointer, recentVal, type);
            }
            releaseTensor(this, recentVal);
        }
        recentVal = builder->create_br(retBB);
        has_br = true;
        return;
    }
    if (ast.value != nullptr) {
        ast.value->accept(*this);
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
}

void GenIR::visit(BinaryExprAST &ast) {
    // 无调用的逐元素 tensor 表达式整体融合，避免每个二元节点各生成一次遍历和临时量。
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
    ast.right->accept(*this);
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

    //若都是常量
    if (useConst) {
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
    if (val[0]->type_->tid_ == Type::IntegerTyID) {
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
        // 矩阵乘的目标形状只约束最终结果，不能传给左右操作数的求值过程。
        expectedTensorType = nullptr;
        expectedTensorTarget = nullptr;
    }
    ast.left->accept(*this);
    val[0] = recentVal;
    ast.right->accept(*this);
    if(ast.op == BinaryOp::Matmul){
        expectedTensorType = savedTensorExpected;
        expectedTensorTarget = savedTensorTarget;
    }
    val[1] = recentVal;
    if(ast.op == BinaryOp::Matmul){
        // 两个操作数就绪后恢复外层上下文，由专用 lowering 推导 M、K、N 并写结果。
        recentVal = lowerTensorMatMul(this, val[0], val[1]);
        return;
    }
    if(val[0]->hasSemFlag(SemFlag::SrcTensor) || val[1]->hasSemFlag(SemFlag::SrcTensor)){
        recentVal = lowerTensorBinary(this,val[0],val[1],ast.op);
        return;
    }

    //若都是常量
    if (useConst) {
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
    if (val[0]->type_->tid_ == Type::IntegerTyID) {
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
    // tensor 正负号可并入逐元素融合循环；含调用时退回顺序求值以保留副作用次序。
    if((ast.op == UnaryOp::Plus || ast.op == UnaryOp::Minus) && !containsCall(&ast) && containsTensorValue(this, &ast)){
        recentVal = lowerFusedTensorExpression(this, &ast);
        return;
    }
    ast.operand->accept(*this);
    if(recentVal->hasSemFlag(SemFlag::SrcTensor)&&(ast.op == UnaryOp::Plus || ast.op == UnaryOp::Minus)){
        // 非融合路径把 -tensor 视为 0 - tensor；一元加直接保留原值。
        if(ast.op == UnaryOp::Minus){
            ArrayType* type = tensorType(recentVal);
            Value* zero = zeroScalar(module.get(),tensorElementType(type));
            recentVal = lowerTensorBinary(this, zero, recentVal, BinaryOp::Subtract);
        }
        return;
    }
    if (useConst) {
        if (ast.op == UnaryOp::Minus) {
            if (auto *integer = dynamic_cast<ConstantInt *>(recentVal)) {
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
    if (recentVal->type_->tid_ == Type::IntegerTyID) {
        if (ast.op == UnaryOp::Minus)
            recentVal = builder->create_isub(
                CONST_INT(0),
                recentVal);
        else if (ast.op == UnaryOp::LogicalNot)
            recentVal = builder->create_zext(
                builder->create_icmp_eq(recentVal, CONST_INT(0)), INT32_T);
    } else {
        if (ast.op == UnaryOp::Minus)
            recentVal = builder->create_fsub(
                CONST_FLOAT(0),
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
        // 动态首维 tensor 本身已是首元素指针，下标时无需静态数组 GEP 的前导零。
        ArrayType* type = tensorType(base);
        ast.index->accept(*this);
        auto dynamicDimension = tensorFirstDimensions.find(base);
        if(dynamicDimension != tensorFirstDimensions.end()){
            Value* element = builder->create_gep(base,{recentVal});
            if(type){
                // 去掉运行时首维后仍有静态尾部，结果是继续携带 tensor 语义的子视图。
                element->setSemFlag(SemFlag::SrcTensor);
                moveTensorStorage(base,element);
                tensorFirstDimensions.erase(dynamicDimension);
                recentVal = element;
            }
            else{
                // 无尾部数组说明已落到标量；加载后结束动态临时 tensor 的生命周期。
                recentVal = builder->create_load(element);
                releaseTensor(this, base);
            }
            return;
        }
        // 静态数组下标采用 {0, index}；若仍有维度则返回子 tensor，否则加载标量。
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
    ast.index->accept(*this);
    recentVal = builder->create_gep(base, {CONST_INT(0), recentVal});
    if (!requireLVal)
        recentVal = builder->create_load(recentVal);
}

void GenIR::visit(AggregateExprAST &ast) {
    ast.initializer->accept(*this);
}

void GenIR::visit(LValueAST &ast) {
    bool isTrueLVal = requireLVal; //是否真是作为左值
    requireLVal = false;
    auto var = scope.find(ast.name);
    if (auto *constant = dynamic_cast<ConstantArray *>(var)) {
    }
    //全局作用域内，一定使用常量，全局作用域下访问左值时useConst已置为true
    if (scope.in_global()) {
        //不是数组，直接返回该常量
        if (ast.indices.empty()) {
            recentVal = var;
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
            }
        }
        return;
    }

    //局部作用域
    if (var->type_->tid_ == Type::IntegerTyID || var->type_->tid_ == Type::FloatTyID) { //说明为局部常量
        recentVal = var;
        return;
    }
    // 不是常量那么var一定是指针类型
    Type* varType = static_cast<PointerType*>(var->type_)->contained_; //所指的类型
    if(var->hasSemFlag(SemFlag::SrcTensor) && ast.indices.empty()){
        // 完整 tensor 作为右值时保留地址；参数槽需先 load，并把运行时首维元数据传给该地址。
        recentVal = varType->tid_ == Type::PointerTyID ? (Value *)builder->create_load(var) : var;
        recentVal->setSemFlag(SemFlag::SrcTensor);
        auto dimension = tensorFirstDimensions.find(var);
        if(dimension != tensorFirstDimensions.end()){
            tensorFirstDimensions[recentVal] = dimension->second;
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
        while (tail->tid_ == Type::ArrayTyID) {
            ++arrayDepth;
            tail = static_cast<ArrayType *>(tail)->contained_;
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
    vector<Value *> args;
    // 动态 tensor 实参必须活到 call 之后，再统一释放其临时存储。
    vector<Value *> tensorArgumentsToRelease;
    auto *functionType = static_cast<FunctionType *>(fun->type_);
    auto infoIt = functionLoweringInfo.find(fun);
    FunctionLoweringInfo *info = infoIt == functionLoweringInfo.end() ? nullptr : &infoIt -> second;
    Value *tensorResultSlot = nullptr;
    unsigned argumentOffset = 0;
    if(info && info->tensorReturn){
        // tensor 返回调用采用 sret 风格：选择上下文目标或新结果槽，并作为第一个实参传入。
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
    // 外层结果提示不应影响实参表达式；各实参按形参类型独立求值。
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
        bool tensorParameter = info && static_cast<unsigned>(i) < info -> tensorParameters.size() && info -> tensorParameters[i];
        Type *fixedParameterType =
            fixedIndex < fixedArgumentCount
                ? functionType->args_[fixedIndex]
                : nullptr;
        std::get<std::unique_ptr<ExprAST>>(argument)->accept(*this);
        if (heapTensorStorage.find(recentVal) != heapTensorStorage.end()) 
            tensorArgumentsToRelease.push_back(recentVal);
        // tensor 形参在数据指针之后追加运行时首维；必要时先统一指针类型。
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
    // call 已消费所有实参，释放临时 tensor，并把隐藏返回槽作为调用表达式的值。
    for(auto *arg : tensorArgumentsToRelease) releaseTensor(this, arg);
    if(tensorResultSlot) recentVal = tensorResultSlot;
}
