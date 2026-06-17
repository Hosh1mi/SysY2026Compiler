#pragma once
#include "ir.hpp"
#include "../../frontend/ast/ast.hpp"
#include <map>

class Scope {
public:
    // enter a new scope
    void enter() {
        symbol.push_back({});
    }

    // exit a scope
    void exit() {
        symbol.pop_back();
    }

    bool in_global() {
        return symbol.size() == 1;
    }

    // push a name to scope
    // return true if successful
    // return false if this name already exists
    // but func name could be same with variable name
    bool push(std::string name, Value *val) {
        bool result;
        result = (symbol[symbol.size() - 1].insert({name, val})).second;
        return result;
    }

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
    std::vector<std::map<std::string, Value *>> symbol;
};

class GenIR: public Visitor {
public:
    void visit(CompUnitAST &ast) override;
    void visit(DeclDefAST &ast) override;
    void visit(DeclAST &ast) override;
    void visit(DefAST &ast) override;
    void visit(InitValAST &ast) override;
    void visit(FuncDefAST &ast) override;
    void visit(FuncFParamAST &ast) override;
    void visit(BlockAST &ast) override;
    void visit(BlockItemAST &ast) override;
    void visit(StmtAST &ast) override;
    void visit(ReturnStmtAST &ast) override;
    void visit(SelectStmtAST &ast) override;
    void visit(IterationStmtAST &ast) override;
    void visit(AddExpAST &ast) override;
    void visit(LValAST &ast) override;
    void visit(MulExpAST &ast) override;
    void visit(UnaryExpAST &ast) override;
    void visit(PrimaryExpAST &ast) override;
    void visit(CallAST &ast) override;
    void visit(NumberAST &ast) override;
    void visit(RelExpAST &ast) override;
    void visit(EqExpAST &ast) override;
    void visit(LAndExpAST &ast) override;
    void visit(LOrExpAST &ast) override;

    IRStmtBuilder *builder;
    Scope scope;
    std::unique_ptr<Module> module;

    GenIR(){
        module = std::unique_ptr<Module>(new Module());
        builder = new IRStmtBuilder(nullptr, module.get());
        auto TyVoid = module->void_ty_;
        auto TyInt32 = module->int32_ty_;
        auto TyInt32Ptr = module->get_pointer_type(module->int32_ty_);
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

        std::vector<Type *>().swap(output_params);
        // output_params.clear();
        output_params.push_back(TyInt32);
        output_type = new FunctionType(TyVoid, output_params);
        auto sysy_start_time = new Function(output_type, "_sysy_starttime", module.get());

        std::vector<Type *>().swap(output_params);
        output_params.push_back(TyInt32);
        output_type = new FunctionType(TyVoid, output_params);
        auto sysy_stop_time = new Function(output_type, "_sysy_stoptime", module.get());

#ifdef ENABLE_PROFILING_HOOKS
        // Opt-in profiling helpers (lib/sylib.c).  Exposed only when the
        // compiler is built with -DENABLE_PROFILING_HOOKS, because both are
        // unsafe on the A53 target / production .sy programs:
        //   - redirect_stdin: freopens "/tmp/mmc-1.in" onto stdin.  On the
        //     A53 board the file doesn't exist; freopen fails and leaves
        //     stdin == NULL, which breaks every getint/getarray call
        //     (input is normally read from a memory page at 0xc0000000).
        //     See MEMORY.md feedback_redirect_stdin_a53.
        //   - debug_progress: fprintf(stderr, "[profile] seg%d\n", seg).
        //     Useless on A53 (no console) and the extra UART traffic skews
        //     timing measurements.
        // Stock contest .sy programs must not call these; they are reserved
        // for in-tree profiling scratch files only.
        Function *redirect_stdin_fn = nullptr;
        Function *debug_progress_fn = nullptr;
        {
            auto rs_ty = new FunctionType(TyVoid, {});
            redirect_stdin_fn = new Function(rs_ty, "redirect_stdin", module.get());

            std::vector<Type *> dp_params{TyInt32};
            auto dp_ty = new FunctionType(TyVoid, dp_params);
            debug_progress_fn = new Function(dp_ty, "debug_progress", module.get());
        }
#endif

        // output_params.clear();
        // output_params.push_back(TyInt32);
        // output_type = new FunctionType(TyInt32, output_params);
        // auto my_malloc = new Function(output_type, "malloc", module.get());


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
        scope.push("_sysy_starttime", sysy_start_time);
        scope.push("_sysy_stoptime", sysy_stop_time);
#ifdef ENABLE_PROFILING_HOOKS
        scope.push("redirect_stdin", redirect_stdin_fn);
        scope.push("debug_progress", debug_progress_fn);
#endif
    }
    std::unique_ptr<Module> getModule() {
        return std::move(module);
    }

    void checkInitType() const;

    static int getNextDim(vector<int> &dimensionsCnt, int up, int cnt);

    void localInit(Value *ptr, vector<unique_ptr<InitValAST>> &list, vector<int> &dimensionsCnt, int up);

    static int getNextDim(vector<int> &elementsCnts, int up);

    ConstantArray *globalInit(vector<int> &dimensions, vector<ArrayType *> &arrayTys, int up, vector<unique_ptr<InitValAST>> &list);

    static void
    mergeElements(vector<int> &dimensions, vector<ArrayType *> &arrayTys, int up, int dimAdd,
                  vector<Constant *> &elements,
                  vector<int> &elementsCnts);

    void finalMerge(vector<int> &dimensions, vector<ArrayType *> &arrayTys, int up, vector<Constant *> &elements,
                    vector<int> &elementsCnts) const;

    bool checkCalType(Value **val, int *intVal, float *floatVal);

    void checkCalType(Value **val);
};