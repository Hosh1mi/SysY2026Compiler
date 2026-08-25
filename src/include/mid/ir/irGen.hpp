#pragma once
#include "ir.hpp"
#include "../../frontend/ast/ast.hpp"
#include <map>
#define ENABLE_PROFILING_HOOKS

using std::unique_ptr;
using std::vector;
// IRGen 的词法符号表。每一层 map 只保存该层声明，查找从内向外进行。
class Scope {
public:
    // 压入一个空的词法作用域；构造 GenIR 时首先建立全局层。
    void enter() {
        symbol.push_back({});
    }

    // 丢弃最内层符号；调用方必须保证至少存在一层。
    void exit() {
        symbol.pop_back();
    }

    // 仅有全局层时返回 true，用于区分全局与局部对象的 lowering。
    bool in_global() {
        return symbol.size() == 1;
    }

    // 向当前层绑定 name -> val；同层已有同名项时不覆盖并返回 false。
    bool push(std::string name, Value *val) {
        bool result;
        result = (symbol[symbol.size() - 1].insert({name, val})).second;
        return result;
    }

    // 从最内层向全局层查找 name；未声明时返回 nullptr。
    Value* find(std::string name) {
        for (auto s = symbol.rbegin(); s != symbol.rend(); s++) {
            auto iter = s->find(name);
            if (iter != s->end()) {
                return iter->second;
            }
        }
        return nullptr;
    }


private:
    // symbol[0] 是全局层，back() 是当前层。
    std::vector<std::map<std::string, Value *>> symbol;
};

// AST 到基础 IR 的有状态 Visitor。表达式访问通常把结果写入 recentVal（定义在实现
// 文件的生成上下文中）；控制流条件和左值访问分别使用分支目标与“返回地址”协议。
class GenIR: public Visitor {
public:
    // 按源码顺序访问顶层声明和函数定义。
    void visit(CompUnitAST &ast) override;
    // 设置当前声明的 IR 类型与 const 属性，再逐个生成声明对象。
    void visit(DeclAST &ast) override;
    // 生成单个全局量、局部栈槽、常量或数组及其初始化。
    void visit(ObjectDefAST &ast) override;
    // 计算一个初始化表达式；聚合递归由 globalInit/localInit 等辅助函数处理。
    void visit(InitValAST &ast) override;
    // 建立函数、形参、entry 和统一返回路径，然后访问函数体。
    void visit(FuncDefAST &ast) override;
    // 把一个源级形参降为 Argument，必要时为可赋值参数建立局部槽。
    void visit(FuncParamAST &ast) override;
    // 建立/退出词法层，并按原顺序访问块内声明和语句。
    void visit(BlockAST &ast) override;
    // 空语句不生成 IR。
    void visit(EmptyStmtAST &ast) override;
    // 先按左值协议取得目标，再计算右值并生成 store。
    void visit(AssignStmtAST &ast) override;
    // 计算表达式并丢弃结果，调用等副作用仍被保留。
    void visit(ExprStmtAST &ast) override;
    // 向当前循环退出块生成无条件分支。
    void visit(BreakStmtAST &ast) override;
    // 向当前循环条件块生成无条件分支。
    void visit(ContinueStmtAST &ast) override;
    // 写入统一返回槽并跳转返回块，或直接生成 void 返回路径。
    void visit(ReturnStmtAST &ast) override;
    // 转发到嵌套 BlockAST，由 BlockAST 自行管理作用域。
    void visit(BlockStmtAST &ast) override;
    // 建立 then/else/merge CFG，并按条件协议生成短路分支。
    void visit(IfStmtAST &ast) override;
    // 建立 cond/body/exit CFG，并设置 break/continue 目标。
    void visit(WhileStmtAST &ast) override;
    // 把整数或浮点字面量转成对应 Constant，并写入 recentVal。
    void visit(LiteralExprAST &ast) override;
    // 名称解析后按上下文返回地址或值，并降低数组下标。
    void visit(LValueAST &ast) override;
    // 检查并转换实参、生成 CallInst；特殊运行库调用会补充约定参数。
    void visit(CallExprAST &ast) override;
    // 生成一元正负号或逻辑非，常量上下文中直接折叠。
    void visit(UnaryExprAST &ast) override;
    // 生成算术、比较或短路逻辑；乘除模转交 lowerMultiplicative。
    void visit(BinaryExprAST &ast) override;
    // 对数组下标生成地址计算和 load。
    void visit(SubscriptExprAST &ast) override;
    // 构造花括号初始化值。
    void visit(AggregateExprAST &ast) override;

    IRStmtBuilder *builder;       // 当前插入点的指令工厂
    Scope scope;                  // 源级名字到 IR Value 的词法映射
    std::unique_ptr<Module> module; // 生成中的模块，最终由 getModule 移出
    unsigned stringLiteralCounter = 0; // 编译器生成字符串全局量的唯一后缀

    // 创建空模块和无插入点 builder，并把 SysY 运行库声明预注册到全局作用域。
    GenIR(){
        module = std::unique_ptr<Module>(new Module());
        builder = new IRStmtBuilder(nullptr);
        auto TyVoid = module->void_ty_;
        auto TyInt32 = module->int32_ty_;
        auto TyInt32Ptr = module->get_pointer_type(module->int32_ty_);
        auto TyInt8Ptr = module->get_pointer_type(module->int8_ty_);
        auto TyFloat = module->float32_ty_;
        auto TyFloatPtr = module->get_pointer_type(module->float32_ty_);

        auto input_type = new FunctionType(TyInt32, {});
        auto get_int = new Function(input_type, "getint", module.get());

        input_type = new FunctionType(TyFloat, {});
        auto get_float = new Function(input_type, "getfloat", module.get());

        input_type = new FunctionType(TyInt32, {});
        auto get_char = new Function(input_type, "getch", module.get());

        std::vector<Type *> input_params;
        std::vector<Type *>().swap(input_params);
        input_params.push_back(TyInt32Ptr);
        input_type = new FunctionType(TyInt32, input_params);
        auto get_int_array = new Function(input_type, "getarray", module.get());

        std::vector<Type *>().swap(input_params);
        input_params.push_back(TyFloatPtr);
        input_type = new FunctionType(TyInt32, input_params);
        auto get_float_array = new Function(input_type, "getfarray", module.get());

        std::vector<Type *> output_params;
        std::vector<Type *>().swap(output_params);
        output_params.push_back(TyInt32);
        auto output_type = new FunctionType(TyVoid, output_params);
        auto put_int = new Function(output_type, "putint", module.get());

        std::vector<Type *>().swap(output_params);
        output_params.push_back(TyFloat);
        output_type = new FunctionType(TyVoid, output_params);
        auto put_float = new Function(output_type, "putfloat", module.get());

        std::vector<Type *>().swap(output_params);
        output_params.push_back(TyInt32);
        output_type = new FunctionType(TyVoid, output_params);
        auto put_char = new Function(output_type, "putch", module.get());

        std::vector<Type *>().swap(output_params);
        output_params.push_back(TyInt32);
        output_params.push_back(TyInt32Ptr);
        output_type = new FunctionType(TyVoid, output_params);
        auto put_int_array = new Function(output_type, "putarray", module.get());

        std::vector<Type *>().swap(output_params);
        output_params.push_back(TyInt32);
        output_params.push_back(TyFloatPtr);
        output_type = new FunctionType(TyVoid, output_params);
        auto put_float_array = new Function(output_type, "putfarray", module.get());

        std::vector<Type *> format_params{TyInt8Ptr};
        auto format_type = new FunctionType(TyVoid, format_params, true);
        auto put_format = new Function(format_type, "putf", module.get());

        std::vector<Type *>().swap(output_params);
        output_params.push_back(TyInt32);
        output_type = new FunctionType(TyVoid, output_params);
        auto sysy_start_time = new Function(output_type, "_sysy_starttime", module.get());

        std::vector<Type *>().swap(output_params);
        output_params.push_back(TyInt32);
        output_type = new FunctionType(TyVoid, output_params);
        auto sysy_stop_time = new Function(output_type, "_sysy_stoptime", module.get());

#ifdef ENABLE_PROFILING_HOOKS
        Function *redirect_stdin_fn = nullptr;
        Function *debug_progress_fn = nullptr;
        Function *debug_text_fn = nullptr;
        Function *profile_start_fn = nullptr;
        Function *profile_stop_fn = nullptr;
        {
            auto rs_ty = new FunctionType(TyVoid, {});
            redirect_stdin_fn = new Function(rs_ty, "redirect_stdin", module.get());

            std::vector<Type *> dp_params{TyInt32};
            auto dp_ty = new FunctionType(TyVoid, dp_params);
            debug_progress_fn = new Function(dp_ty, "debug_progress", module.get());

            std::vector<Type *> dt_params{TyInt32Ptr};
            auto dt_ty = new FunctionType(TyVoid, dt_params);
            debug_text_fn = new Function(dt_ty, "debug_text", module.get());

            std::vector<Type *> timer_params{TyInt32};
            auto timer_ty = new FunctionType(TyVoid, timer_params);
            profile_start_fn = new Function(timer_ty, "profile_start", module.get());
            profile_stop_fn = new Function(timer_ty, "profile_stop", module.get());
        }
#endif


        scope.enter();
        scope.push("getint", get_int);
        scope.push("getfloat", get_float);
        scope.push("getch", get_char);
        scope.push("getarray", get_int_array);
        scope.push("getfarray", get_float_array);
        scope.push("putint", put_int);
        scope.push("putfloat", put_float);
        scope.push("putch", put_char);
        scope.push("putarray", put_int_array);
        scope.push("putfarray", put_float_array);
        scope.push("putf", put_format);
        scope.push("_sysy_starttime", sysy_start_time);
        scope.push("_sysy_stoptime", sysy_stop_time);
#ifdef ENABLE_PROFILING_HOOKS
        scope.push("redirect_stdin", redirect_stdin_fn);
        scope.push("debug_progress", debug_progress_fn);
        scope.push("debug_text", debug_text_fn);
        scope.push("profile_start", profile_start_fn);
        scope.push("profile_stop", profile_stop_fn);
#endif
    }
    // 移出已生成 Module 的唯一所有权；每个 GenIR 实例只能有效调用一次。
    std::unique_ptr<Module> getModule() {
        return std::move(module);
    }

    // 把 recentVal 转成当前声明 curType；常量直接重建，动态值插入转换指令。
    void checkInitType() const;

    // 局部数组初始化时，从 up 层当前位置 cnt 计算下一个子聚合边界。
    static int getNextDim(vector<int> &dimensionsCnt, int up, int cnt);

    // 递归展开局部数组初始化，把 list 中的表达式以 GEP/store 写入 ptr。
    void localInit(Value *ptr, vector<unique_ptr<InitValAST>> &list, vector<int> &dimensionsCnt, int up);

    // 全局聚合组装时，根据各层已收集元素数找到下一次需要归并的层级。
    static int getNextDim(vector<int> &elementsCnts, int up);

    // 把全局初始化树递归组装为与 arrayTys[up] 匹配的 ConstantArray。
    ConstantArray *globalInit(vector<int> &dimensions, vector<ArrayType *> &arrayTys, int up, vector<unique_ptr<InitValAST>> &list);

    // 把已完成的低层 elements 向上合并 dimAdd 层，并同步各层元素计数。
    static void
    mergeElements(vector<int> &dimensions, vector<ArrayType *> &arrayTys, int up, int dimAdd,
                  vector<Constant *> &elements,
                  vector<int> &elementsCnts);

    // 用零值补齐未满的聚合，并完成从叶元素到 arrayTys[up] 的最终归并。
    void finalMerge(vector<int> &dimensions, vector<ArrayType *> &arrayTys, int up, vector<Constant *> &elements,
                    vector<int> &elementsCnts) const;

    // 常量计算用类型统一：提取两侧数值，返回结果是否应保持整数类型。
    bool checkCalType(Value **val, int *intVal, float *floatVal);

    // 动态计算用类型统一：必要时修改 val[0]/val[1] 并插入 int/float 转换。
    void checkCalType(Value **val);

    // 生成乘、除、模；拆出此函数只为缩短 BinaryExpr lowering。
    void lowerMultiplicative(BinaryExprAST &ast);
};
