#include "../../include/mid/opt/libFunc.hpp"

Function *getOrInsertLibFunc(Module *module, LibFunc kind) {
    const char *name = "_sysy_memcpy";
    if (kind == LibFunc::Memset)
        name = "_sysy_memset";
    else if (kind == LibFunc::Memmove)
        name = "_sysy_memmove";

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
