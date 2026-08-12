// This file prints the semantic AST as an indented tree for frontend
// debugging.  It deliberately uses the existing Visitor interface and never
// changes the tree or participates in IR generation.

#include "../../include/frontend/ast/astPrinter.hpp"
#include "../../include/frontend/ast/ast.hpp"

#include <cctype>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <variant>

namespace {

const char *elementName(TYPE type) {
    switch (type) {
      case TYPE_VOID: return "void";
      case TYPE_INT: return "int";
      case TYPE_FLOAT: return "float";
    }
    return "<unknown>";
}

std::string typeName(const TypeSpec &type) {
    if (type.isScalar()) return elementName(type.element);

    std::ostringstream out;
    out << "vector<" << elementName(type.element);
    if (type.isFixedVector()) {
        out << ", ";
        if (!type.laneConstant.empty())
            out << type.laneConstant;
        else
            out << type.lanes;
    }
    out << '>';
    return out.str();
}

const char *unaryName(UnaryOp op) {
    switch (op) {
      case UnaryOp::Plus: return "+";
      case UnaryOp::Minus: return "-";
      case UnaryOp::LogicalNot: return "!";
    }
    return "?";
}

const char *binaryName(BinaryOp op) {
    switch (op) {
      case BinaryOp::Add: return "+";
      case BinaryOp::Subtract: return "-";
      case BinaryOp::Multiply: return "*";
      case BinaryOp::Divide: return "/";
      case BinaryOp::Remainder: return "%";
      case BinaryOp::Less: return "<";
      case BinaryOp::LessEqual: return "<=";
      case BinaryOp::Greater: return ">";
      case BinaryOp::GreaterEqual: return ">=";
      case BinaryOp::Equal: return "==";
      case BinaryOp::NotEqual: return "!=";
      case BinaryOp::LogicalAnd: return "&&";
      case BinaryOp::LogicalOr: return "||";
    }
    return "?";
}

std::string quoteText(const std::string &text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char byte : text) {
        switch (byte) {
          case '\\': out << "\\\\"; break;
          case '"': out << "\\\""; break;
          case '\n': out << "\\n"; break;
          case '\r': out << "\\r"; break;
          case '\t': out << "\\t"; break;
          case '\0': out << "\\0"; break;
          default:
            if (std::isprint(byte)) {
                out << static_cast<char>(byte);
            } else {
                out << "\\x" << std::hex << std::setw(2)
                    << std::setfill('0') << static_cast<unsigned>(byte)
                    << std::dec;
            }
        }
    }
    out << '"';
    return out.str();
}

class ASTPrinter final : public Visitor {
public:
    explicit ASTPrinter(std::ostream &out) : out(out) {}

    void print(CompUnitAST &root) { root.accept(*this); }

    void visit(CompUnitAST &ast) override {
        line("CompUnit");
        nested([&] {
            for (auto &item : ast.items)
                std::visit([&](auto &node) { node->accept(*this); }, item);
        });
    }

    void visit(DeclAST &ast) override {
        line("Decl type=" + typeName(ast.type) +
             " const=" + (ast.isConst ? "true" : "false"));
        nested([&] {
            for (auto &object : ast.objects) object->accept(*this);
        });
    }

    void visit(ObjectDefAST &ast) override {
        line("Object name=" + quoteText(ast.name));
        nested([&] {
            printNodeList("Dimensions", ast.dimensions);
            if (ast.initializer) {
                line("Initializer");
                nested([&] { ast.initializer->accept(*this); });
            } else {
                line("Initializer <none>");
            }
        });
    }

    void visit(InitValAST &ast) override {
        if (ast.isExpression()) {
            line("InitExpr");
            nested([&] { ast.expression()->accept(*this); });
            return;
        }
        if (ast.elements().empty()) {
            line("InitList []");
            return;
        }
        line("InitList");
        nested([&] {
            for (auto &element : ast.elements()) element->accept(*this);
        });
    }

    void visit(FuncDefAST &ast) override {
        line("FuncDef name=" + quoteText(ast.name) +
             " return=" + typeName(ast.returnType));
        nested([&] {
            if (ast.parameters.empty()) {
                line("Parameters []");
            } else {
                line("Parameters");
                nested([&] {
                    for (auto &parameter : ast.parameters)
                        parameter->accept(*this);
                });
            }
            line("Body");
            nested([&] { ast.body->accept(*this); });
        });
    }

    void visit(FuncParamAST &ast) override {
        line("Param name=" + quoteText(ast.name) + " type=" +
             typeName(ast.type) + " array=" +
             (ast.isArray ? "true" : "false"));
        nested([&] { printNodeList("TrailingDimensions",
                                    ast.trailingDimensions); });
    }

    void visit(BlockAST &ast) override {
        if (ast.items.empty()) {
            line("Block []");
            return;
        }
        line("Block");
        nested([&] {
            for (auto &item : ast.items)
                std::visit([&](auto &node) { node->accept(*this); }, item);
        });
    }

    void visit(EmptyStmtAST &) override { line("EmptyStmt"); }

    void visit(AssignStmtAST &ast) override {
        line("AssignStmt");
        nested([&] {
            line("Target");
            nested([&] { ast.target->accept(*this); });
            line("Value");
            nested([&] { ast.value->accept(*this); });
        });
    }

    void visit(ExprStmtAST &ast) override {
        line("ExprStmt");
        nested([&] { ast.expression->accept(*this); });
    }

    void visit(BreakStmtAST &) override { line("BreakStmt"); }
    void visit(ContinueStmtAST &) override { line("ContinueStmt"); }

    void visit(ReturnStmtAST &ast) override {
        if (!ast.value) {
            line("ReturnStmt value=<none>");
            return;
        }
        line("ReturnStmt");
        nested([&] { ast.value->accept(*this); });
    }

    void visit(BlockStmtAST &ast) override {
        line("BlockStmt");
        nested([&] { ast.block->accept(*this); });
    }

    void visit(IfStmtAST &ast) override {
        line("IfStmt");
        nested([&] {
            line("Condition");
            nested([&] { ast.condition->accept(*this); });
            line("Then");
            nested([&] { ast.thenBranch->accept(*this); });
            if (ast.elseBranch) {
                line("Else");
                nested([&] { ast.elseBranch->accept(*this); });
            } else {
                line("Else <none>");
            }
        });
    }

    void visit(WhileStmtAST &ast) override {
        line("WhileStmt");
        nested([&] {
            line("Condition");
            nested([&] { ast.condition->accept(*this); });
            line("Body");
            nested([&] { ast.body->accept(*this); });
        });
    }

    void visit(LiteralExprAST &ast) override {
        if (const auto *integer = std::get_if<int>(&ast.value)) {
            line("IntLiteral value=" + std::to_string(*integer));
            return;
        }
        std::ostringstream value;
        value << std::setprecision(std::numeric_limits<float>::max_digits10)
              << std::get<float>(ast.value);
        line("FloatLiteral value=" + value.str());
    }

    void visit(LValueAST &ast) override {
        line("LValue name=" + quoteText(ast.name));
        nested([&] { printNodeList("Indices", ast.indices); });
    }

    void visit(CallExprAST &ast) override {
        line("CallExpr callee=" + quoteText(ast.callee) +
             " line=" + std::to_string(ast.line));
        if (ast.arguments.empty()) {
            nested([&] { line("Arguments []"); });
            return;
        }
        nested([&] {
            line("Arguments");
            nested([&] {
                for (auto &argument : ast.arguments) {
                    if (auto *expression =
                            std::get_if<std::unique_ptr<ExprAST>>(&argument)) {
                        (*expression)->accept(*this);
                    } else {
                        line("RuntimeString value=" +
                             quoteText(std::get<std::string>(argument)));
                    }
                }
            });
        });
    }

    void visit(UnaryExprAST &ast) override {
        line(std::string("UnaryExpr op=") + unaryName(ast.op));
        nested([&] { ast.operand->accept(*this); });
    }

    void visit(BinaryExprAST &ast) override {
        line(std::string("BinaryExpr op=") + binaryName(ast.op));
        nested([&] {
            line("Left");
            nested([&] { ast.left->accept(*this); });
            line("Right");
            nested([&] { ast.right->accept(*this); });
        });
    }

    void visit(SubscriptExprAST &ast) override {
        line("SubscriptExpr");
        nested([&] {
            line("Base");
            nested([&] { ast.base->accept(*this); });
            line("Index");
            nested([&] { ast.index->accept(*this); });
        });
    }

    void visit(AggregateExprAST &ast) override {
        line("AggregateExpr");
        nested([&] { ast.initializer->accept(*this); });
    }

private:
    template <typename Function>
    void nested(Function function) {
        ++depth;
        function();
        --depth;
    }

    void line(const std::string &text) {
        for (unsigned i = 0; i < depth; ++i) out << "  ";
        out << text << '\n';
    }

    template <typename Node>
    void printNodeList(const char *name,
                       std::vector<std::unique_ptr<Node>> &nodes) {
        if (nodes.empty()) {
            line(std::string(name) + " []");
            return;
        }
        line(name);
        nested([&] {
            for (auto &node : nodes) node->accept(*this);
        });
    }

    std::ostream &out;
    unsigned depth = 0;
};

} // namespace

void dumpAST(CompUnitAST &root, std::ostream &out) {
    ASTPrinter printer(out);
    printer.print(root);
}
