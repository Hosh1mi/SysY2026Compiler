#include "../../include/backend/arm64/codegen.hpp"
#include "../../include/backend/arm64/peephole.hpp"
#include "../../include/backend/arm64/context.hpp"
#include "../../include/backend/arm64/machine.hpp"
#include "../../include/backend/arm64/scheduler.hpp"
#include "../../include/mid/ir/ir.hpp"
#include <cstring>
#include <functional>
#include <iostream>
#include <vector>
#include <thread>
#include <sstream>
#include <algorithm>
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

void Arm64CodeGen::generate() {
    // 1. 分类全局变量
    std::vector<GlobalVariable*> rodata, data, bss;
    for (auto gv : m_->global_list_) {
        if (gv->is_const_) {
            rodata.push_back(gv);
        } else if (gv->init_val_ && !dynamic_cast<ConstantZero*>(gv->init_val_)) {
            data.push_back(gv);
        } else {
            bss.push_back(gv);
        }
    }

    // 2. 外部函数声明
    for (auto f : m_->function_list_) {
        if (f->is_declaration()) {
            emitExtern(f);
        }
    }

    // 3. 收集非声明函数
    std::vector<Function*> funcs;
    for (auto f : m_->function_list_) {
        if (!f->is_declaration()) {
            funcs.push_back(f);
        }
    }

    // 4. 并行生成函数代码
    std::vector<std::string> results(funcs.size());
    if (!funcs.empty()) {
        for (auto f : funcs) {
            f->set_instr_name();
        }

        unsigned numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 1;
        if (numThreads <= 2) numThreads = 1;
        else numThreads = numThreads - 2;

        std::vector<std::vector<Function*>> partitions(numThreads);
        for (size_t i = 0; i < funcs.size(); ++i) {
            partitions[i % numThreads].push_back(funcs[i]);
        }

        std::vector<std::thread> threads;
        for (unsigned t = 0; t < numThreads; ++t) {
            threads.emplace_back([this, &partitions, &results, &funcs, t]() {
#ifdef __linux__
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                int numCores = sysconf(_SC_NPROCESSORS_ONLN);
                for (int i = 0; i < numCores; ++i) {
                    if (i != 2 && i != 3) {
                        CPU_SET(i, &cpuset);
                    }
                }
                pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
                for (auto f : partitions[t]) {
                    MachineFunction machineFunc;
                    machineFunc.name = f->name_;
                    MachineEmitter emitter(machineFunc);

                    Arm64FuncContext ctx(f, emitter, enable_regalloc_);
                    ctx.generate();
                    emitter.stream().flush();

                    auto it = std::find(funcs.begin(), funcs.end(), f);
                    size_t idx = it - funcs.begin();
                    if (!no_schedule_ && enable_regalloc_) {
                        MachineScheduler scheduler;
                        scheduler.schedule(machineFunc);
                    }
                    std::string funcAsm = printMachineFunction(machineFunc);
                    results[idx] = no_peephole_ ? funcAsm : peepholeOptimize(funcAsm);
                }
            });
        }
        for (auto &th : threads) {
            th.join();
        }
    }

    // 5. 输出 .text + 函数代码
    if (!funcs.empty()) {
        os_ << "\t.text\n";
        for (const auto &str : results) {
            os_ << str;
        }
    }

    // 6. 输出数据段：.data -> .bss -> .section .rodata
    auto emitGroup = [&](const char* sec, const std::vector<GlobalVariable*>& gvs) {
        if (gvs.empty()) return;
        os_ << sec << "\n";
        for (auto gv : gvs) {
            emitGlobal(gv);
        }
    };
    emitGroup("\t.data",            data);
    emitGroup("\t.bss",             bss);
    emitGroup("\t.section .rodata", rodata);
}

void Arm64CodeGen::emitGlobal(GlobalVariable *gv) {
    auto pointee = static_cast<PointerType*>(gv->type_)->contained_;

    os_ << "\t.global " << gv->name_ << "\n";
    os_ << "\t.p2align 2\n";
    os_ << gv->name_ << ":\n";

    if (auto cz = dynamic_cast<ConstantZero*>(gv->init_val_)) {
        int size = 4;
        Type *ty = pointee;
        if (ty->tid_ == Type::ArrayTyID) {
            int totalElements = 1;
            Type *cur = ty;
            while (auto arrTy = dynamic_cast<ArrayType*>(cur)) {
                totalElements *= arrTy->num_elements_;
                cur = arrTy->contained_;
            }
            int elemSize = 4;
            if (cur->tid_ == Type::IntegerTyID) {
                elemSize = static_cast<IntegerType*>(cur)->num_bits_ / 8;
            } else if (cur->tid_ == Type::FloatTyID) {
                elemSize = 4;
            }
            size = totalElements * elemSize;
        }
        os_ << "\t.zero " << size << "\n";
    } else if (auto ci = dynamic_cast<ConstantInt*>(gv->init_val_)) {
        os_ << "\t.word " << ci->value_ << "\n";
    } else if (auto cf = dynamic_cast<ConstantFloat*>(gv->init_val_)) {
        float val = cf->value_;
        int bits;
        std::memcpy(&bits, &val, sizeof(bits));
        os_ << "\t.word 0x" << std::hex << bits << std::dec << "\n";
    } else if (auto ca = dynamic_cast<ConstantArray*>(gv->init_val_)) {
        std::function<bool(Constant*)> allZero = [&](Constant *elem) -> bool {
            if (auto eci = dynamic_cast<ConstantInt*>(elem))
                return eci->value_ == 0;
            if (auto ecf = dynamic_cast<ConstantFloat*>(elem))
                return ecf->value_ == 0.0f;
            if (auto eca = dynamic_cast<ConstantArray*>(elem)) {
                for (auto sub : eca->const_array)
                    if (!allZero(sub)) return false;
                return true;
            }
            return dynamic_cast<ConstantZero*>(elem) != nullptr;
        };
        bool isAllZero = true;
        for (auto elem : ca->const_array)
            if (!allZero(elem)) { isAllZero = false; break; }

        if (isAllZero) {
            int totalElements = 1;
            Type *cur = pointee;
            while (auto arrTy = dynamic_cast<ArrayType*>(cur)) {
                totalElements *= arrTy->num_elements_;
                cur = arrTy->contained_;
            }
            int elemSize = 4;
            if (cur->tid_ == Type::IntegerTyID)
                elemSize = static_cast<IntegerType*>(cur)->num_bits_ / 8;
            else if (cur->tid_ == Type::FloatTyID)
                elemSize = 4;
            os_ << "\t.zero " << (totalElements * elemSize) << "\n";
        } else {
            std::function<void(Constant*)> emitElem = [&](Constant *elem) {
                if (auto eci = dynamic_cast<ConstantInt*>(elem)) {
                    os_ << "\t.word " << eci->value_ << "\n";
                } else if (auto ecf = dynamic_cast<ConstantFloat*>(elem)) {
                    float val = ecf->value_;
                    int bits;
                    std::memcpy(&bits, &val, sizeof(bits));
                    os_ << "\t.word 0x" << std::hex << bits << std::dec << "\n";
                } else if (auto eca = dynamic_cast<ConstantArray*>(elem)) {
                    for (auto sub : eca->const_array) emitElem(sub);
                } else if (dynamic_cast<ConstantZero*>(elem)) {
                    os_ << "\t.word 0\n";
                }
            };
            for (auto elem : ca->const_array) emitElem(elem);
        }
    }
    os_ << "\n";
}

void Arm64CodeGen::emitExtern(Function *f) {
    (void)f;
}

void Arm64CodeGen::emitFunction(Function *f) {
    Arm64FuncContext ctx(f, os_, enable_regalloc_);
    ctx.generate();
}
