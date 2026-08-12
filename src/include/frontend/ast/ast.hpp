#pragma once

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// The AST is the stable boundary between parsing and IR generation.  It
// represents source meaning, not parser productions: precedence-only grammar
// layers and list-helper nonterminals must not appear here.

enum TYPE { TYPE_VOID, TYPE_INT, TYPE_FLOAT };

enum class VECTOR_EXTENT { SCALAR, FIXED, DYNAMIC };

// All accepted vector spellings lower to this single representation.  If the
// official grammar changes, the parser normally only needs to construct a
// different TypeSpec; AST consumers continue to see element type and extent.
struct TypeSpec {
    TYPE element = TYPE_VOID;
    VECTOR_EXTENT extent = VECTOR_EXTENT::SCALAR;
    unsigned lanes = 1;
    std::string laneConstant;

    TypeSpec() = default;
    TypeSpec(TYPE scalar) : element(scalar) {}

    static TypeSpec fixed(TYPE element, unsigned lanes) {
        TypeSpec result(element);
        result.extent = VECTOR_EXTENT::FIXED;
        result.lanes = lanes;
        return result;
    }

    static TypeSpec fixed(TYPE element, std::string laneConstant) {
        TypeSpec result(element);
        result.extent = VECTOR_EXTENT::FIXED;
        result.lanes = 0;
        result.laneConstant = std::move(laneConstant);
        return result;
    }

    static TypeSpec dynamic(TYPE element) {
        TypeSpec result(element);
        result.extent = VECTOR_EXTENT::DYNAMIC;
        result.lanes = 0;
        return result;
    }

    bool isScalar() const { return extent == VECTOR_EXTENT::SCALAR; }
    bool isFixedVector() const { return extent == VECTOR_EXTENT::FIXED; }
    bool isDynamicVector() const { return extent == VECTOR_EXTENT::DYNAMIC; }
    bool isVector() const { return !isScalar(); }

    bool operator==(TYPE rhs) const { return isScalar() && element == rhs; }
    bool operator!=(TYPE rhs) const { return !(*this == rhs); }
    bool operator==(const TypeSpec &rhs) const {
        return element == rhs.element && extent == rhs.extent &&
               lanes == rhs.lanes && laneConstant == rhs.laneConstant;
    }
    bool operator!=(const TypeSpec &rhs) const { return !(*this == rhs); }
};

enum class UnaryOp { Plus, Minus, LogicalNot };

// One operator enum is enough because the parser has already encoded
// precedence and associativity in the shape of the binary-expression tree.
enum class BinaryOp {
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,
    LogicalAnd,
    LogicalOr
};

class Visitor;
class BaseAST;
class ExprAST;
class StmtAST;
class CompUnitAST;
class DeclAST;
class ObjectDefAST;
class InitValAST;
class FuncDefAST;
class FuncParamAST;
class BlockAST;
class EmptyStmtAST;
class AssignStmtAST;
class ExprStmtAST;
class BreakStmtAST;
class ContinueStmtAST;
class ReturnStmtAST;
class BlockStmtAST;
class IfStmtAST;
class WhileStmtAST;
class LiteralExprAST;
class LValueAST;
class CallExprAST;
class UnaryExprAST;
class BinaryExprAST;
class SubscriptExprAST;
class AggregateExprAST;

class BaseAST {
public:
    virtual ~BaseAST() = default;
    virtual void accept(Visitor &visitor) = 0;
};

class ExprAST : public BaseAST {
public:
    ~ExprAST() override = default;
};

class StmtAST : public BaseAST {
public:
    ~StmtAST() override = default;
};

// An initializer is either one expression or one brace list.  The variant
// makes the distinction exhaustive and also represents an empty brace list.
class InitValAST final : public BaseAST {
public:
    using List = std::vector<std::unique_ptr<InitValAST>>;
    using Value = std::variant<std::unique_ptr<ExprAST>, List>;

    explicit InitValAST(std::unique_ptr<ExprAST> expression)
        : value(std::move(expression)) {}
    explicit InitValAST(List elements = {}) : value(std::move(elements)) {}

    bool isExpression() const {
        return std::holds_alternative<std::unique_ptr<ExprAST>>(value);
    }
    ExprAST *expression() const {
        if (const auto *slot =
                std::get_if<std::unique_ptr<ExprAST>>(&value))
            return slot->get();
        return nullptr;
    }
    List &elements() { return std::get<List>(value); }
    const List &elements() const { return std::get<List>(value); }

    Value value;
    void accept(Visitor &visitor) override;
};

// A declaration owns one or more object definitions that share a type and a
// const qualifier, matching the semantic unit consumed by IR generation.
class ObjectDefAST final : public BaseAST {
public:
    ObjectDefAST(std::string name,
                 std::vector<std::unique_ptr<ExprAST>> dimensions = {},
                 std::unique_ptr<InitValAST> initializer = nullptr)
        : name(std::move(name)), dimensions(std::move(dimensions)),
          initializer(std::move(initializer)) {}

    std::string name;
    std::vector<std::unique_ptr<ExprAST>> dimensions;
    std::unique_ptr<InitValAST> initializer;
    void accept(Visitor &visitor) override;
};

class DeclAST final : public BaseAST {
public:
    DeclAST(TypeSpec type, bool isConst,
            std::vector<std::unique_ptr<ObjectDefAST>> objects)
        : type(std::move(type)), isConst(isConst),
          objects(std::move(objects)) {}

    TypeSpec type;
    bool isConst = false;
    std::vector<std::unique_ptr<ObjectDefAST>> objects;
    void accept(Visitor &visitor) override;
};

class FuncParamAST final : public BaseAST {
public:
    FuncParamAST(TypeSpec type, std::string name, bool isArray = false,
                 std::vector<std::unique_ptr<ExprAST>> dimensions = {})
        : type(std::move(type)), name(std::move(name)), isArray(isArray),
          trailingDimensions(std::move(dimensions)) {}

    TypeSpec type;
    std::string name;
    // true denotes the omitted first dimension in `T name[][N]`.
    bool isArray = false;
    std::vector<std::unique_ptr<ExprAST>> trailingDimensions;
    void accept(Visitor &visitor) override;
};

class FuncDefAST final : public BaseAST {
public:
    FuncDefAST(TypeSpec returnType, std::string name,
               std::vector<std::unique_ptr<FuncParamAST>> parameters,
               std::unique_ptr<BlockAST> body)
        : returnType(std::move(returnType)), name(std::move(name)),
          parameters(std::move(parameters)), body(std::move(body)) {}

    TypeSpec returnType;
    std::string name;
    std::vector<std::unique_ptr<FuncParamAST>> parameters;
    std::unique_ptr<BlockAST> body;
    void accept(Visitor &visitor) override;
};

// The two file-scope alternatives are stored directly, preserving source
// order without making local declarations inherit a misleading top-level base.
using TopLevelItemAST =
    std::variant<std::unique_ptr<DeclAST>, std::unique_ptr<FuncDefAST>>;

class CompUnitAST final : public BaseAST {
public:
    std::vector<TopLevelItemAST> items;
    void accept(Visitor &visitor) override;
};

// A block item is intentionally a value variant: a declaration and a
// statement are mutually exclusive, with no nullable wrapper node.
using BlockItemAST =
    std::variant<std::unique_ptr<DeclAST>, std::unique_ptr<StmtAST>>;

class BlockAST final : public BaseAST {
public:
    std::vector<BlockItemAST> items;
    void accept(Visitor &visitor) override;
};

class EmptyStmtAST final : public StmtAST {
public:
    void accept(Visitor &visitor) override;
};

class AssignStmtAST final : public StmtAST {
public:
    AssignStmtAST(std::unique_ptr<LValueAST> target,
                  std::unique_ptr<ExprAST> value)
        : target(std::move(target)), value(std::move(value)) {}

    std::unique_ptr<LValueAST> target;
    std::unique_ptr<ExprAST> value;
    void accept(Visitor &visitor) override;
};

class ExprStmtAST final : public StmtAST {
public:
    explicit ExprStmtAST(std::unique_ptr<ExprAST> expression)
        : expression(std::move(expression)) {}

    std::unique_ptr<ExprAST> expression;
    void accept(Visitor &visitor) override;
};

class BreakStmtAST final : public StmtAST {
public:
    void accept(Visitor &visitor) override;
};

class ContinueStmtAST final : public StmtAST {
public:
    void accept(Visitor &visitor) override;
};

class ReturnStmtAST final : public StmtAST {
public:
    explicit ReturnStmtAST(std::unique_ptr<ExprAST> value = nullptr)
        : value(std::move(value)) {}

    std::unique_ptr<ExprAST> value;
    void accept(Visitor &visitor) override;
};

class BlockStmtAST final : public StmtAST {
public:
    explicit BlockStmtAST(std::unique_ptr<BlockAST> block)
        : block(std::move(block)) {}

    std::unique_ptr<BlockAST> block;
    void accept(Visitor &visitor) override;
};

class IfStmtAST final : public StmtAST {
public:
    IfStmtAST(std::unique_ptr<ExprAST> condition,
              std::unique_ptr<StmtAST> thenBranch,
              std::unique_ptr<StmtAST> elseBranch = nullptr)
        : condition(std::move(condition)), thenBranch(std::move(thenBranch)),
          elseBranch(std::move(elseBranch)) {}

    std::unique_ptr<ExprAST> condition;
    std::unique_ptr<StmtAST> thenBranch;
    std::unique_ptr<StmtAST> elseBranch;
    void accept(Visitor &visitor) override;
};

class WhileStmtAST final : public StmtAST {
public:
    WhileStmtAST(std::unique_ptr<ExprAST> condition,
                 std::unique_ptr<StmtAST> body)
        : condition(std::move(condition)), body(std::move(body)) {}

    std::unique_ptr<ExprAST> condition;
    std::unique_ptr<StmtAST> body;
    void accept(Visitor &visitor) override;
};

class LiteralExprAST final : public ExprAST {
public:
    explicit LiteralExprAST(int value) : value(value) {}
    explicit LiteralExprAST(float value) : value(value) {}

    std::variant<int, float> value;
    void accept(Visitor &visitor) override;
};

// LValue is a real expression node because the same source form can be read
// as a value or requested as an address by an assignment.
class LValueAST final : public ExprAST {
public:
    explicit LValueAST(std::string name) : name(std::move(name)) {}

    std::string name;
    std::vector<std::unique_ptr<ExprAST>> indices;
    void accept(Visitor &visitor) override;
};

// Runtime strings are a deliberate call-argument alternative, never a fake
// SysY expression.  That keeps ordinary expression visitors type-safe.
using CallArgumentAST = std::variant<std::unique_ptr<ExprAST>, std::string>;

class CallExprAST final : public ExprAST {
public:
    explicit CallExprAST(std::string callee, int line = 0)
        : callee(std::move(callee)), line(line) {}

    std::string callee;
    std::vector<CallArgumentAST> arguments;
    int line = 0;
    void accept(Visitor &visitor) override;
};

class UnaryExprAST final : public ExprAST {
public:
    UnaryExprAST(UnaryOp op, std::unique_ptr<ExprAST> operand)
        : op(op), operand(std::move(operand)) {}

    UnaryOp op;
    std::unique_ptr<ExprAST> operand;
    void accept(Visitor &visitor) override;
};

class BinaryExprAST final : public ExprAST {
public:
    BinaryExprAST(BinaryOp op, std::unique_ptr<ExprAST> left,
                  std::unique_ptr<ExprAST> right)
        : op(op), left(std::move(left)), right(std::move(right)) {}

    BinaryOp op;
    std::unique_ptr<ExprAST> left;
    std::unique_ptr<ExprAST> right;
    void accept(Visitor &visitor) override;
};

// This node is only needed for indexing a computed vector value.  Indexing an
// identifier remains inside LValueAST so array addressing stays explicit.
class SubscriptExprAST final : public ExprAST {
public:
    SubscriptExprAST(std::unique_ptr<ExprAST> base,
                     std::unique_ptr<ExprAST> index)
        : base(std::move(base)), index(std::move(index)) {}

    std::unique_ptr<ExprAST> base;
    std::unique_ptr<ExprAST> index;
    void accept(Visitor &visitor) override;
};

class AggregateExprAST final : public ExprAST {
public:
    explicit AggregateExprAST(std::unique_ptr<InitValAST> initializer)
        : initializer(std::move(initializer)) {}

    std::unique_ptr<InitValAST> initializer;
    void accept(Visitor &visitor) override;
};

class Visitor {
public:
    virtual ~Visitor() = default;
    virtual void visit(CompUnitAST &ast) = 0;
    virtual void visit(DeclAST &ast) = 0;
    virtual void visit(ObjectDefAST &ast) = 0;
    virtual void visit(InitValAST &ast) = 0;
    virtual void visit(FuncDefAST &ast) = 0;
    virtual void visit(FuncParamAST &ast) = 0;
    virtual void visit(BlockAST &ast) = 0;
    virtual void visit(EmptyStmtAST &ast) = 0;
    virtual void visit(AssignStmtAST &ast) = 0;
    virtual void visit(ExprStmtAST &ast) = 0;
    virtual void visit(BreakStmtAST &ast) = 0;
    virtual void visit(ContinueStmtAST &ast) = 0;
    virtual void visit(ReturnStmtAST &ast) = 0;
    virtual void visit(BlockStmtAST &ast) = 0;
    virtual void visit(IfStmtAST &ast) = 0;
    virtual void visit(WhileStmtAST &ast) = 0;
    virtual void visit(LiteralExprAST &ast) = 0;
    virtual void visit(LValueAST &ast) = 0;
    virtual void visit(CallExprAST &ast) = 0;
    virtual void visit(UnaryExprAST &ast) = 0;
    virtual void visit(BinaryExprAST &ast) = 0;
    virtual void visit(SubscriptExprAST &ast) = 0;
    virtual void visit(AggregateExprAST &ast) = 0;
};
