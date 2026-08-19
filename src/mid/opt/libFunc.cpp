#include "../../include/mid/opt/libFunc.hpp"

// 获取或创建 memset/memcpy/memmove 声明。若同名符号已有函数体，则拒绝复用，
// 避免把用户定义函数误当成具有标准库语义的内建函数。
Function *getOrInsertLibFunc(Module *module, LibFunc kind) {
    const char *name = "memcpy";
    if (kind == LibFunc::Memset)
        name = "memset";
    else if (kind == LibFunc::Memmove)
        name = "memmove";

    for (auto *func : module->function_list_) {
        if (func->name_ == name) {
            if (func->is_declaration())
                return func;
            return nullptr;
        }
    }

    auto *ptrTy = module->get_pointer_type(module->int32_ty_);
    std::vector<Type *> args;
    if (kind == LibFunc::Memset)
        args = {ptrTy, module->int32_ty_, module->int32_ty_};
    else
        args = {ptrTy, ptrTy, module->int32_ty_};

    auto *fty = new FunctionType(module->void_ty_, args);
    return new Function(fty, name, module);
}
