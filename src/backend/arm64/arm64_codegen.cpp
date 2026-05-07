#include "../../include/backend/arm64/arm64_codegen.hpp"
#include "../../include/backend/arm64/arm64_context.hpp"
#include "../../include/mid/ir/ir.hpp"
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
    os_ << "\t.text\n\n";

    // 1. 全局变量（单线程，顺序输出）
    for (auto gv : m_->global_list_) {
        emitGlobal(gv);
    }

    // 2. 外部函数声明（单线程）
    for (auto f : m_->function_list_) {
        if (f->is_declaration()) {
            emitExtern(f);
        }
    }

    os_ << "\n\t.text\n\n";

    // 3. 收集需要生成代码的函数（非声明）
    std::vector<Function*> funcs;
    for (auto f : m_->function_list_) {
        if (!f->is_declaration()) {
            funcs.push_back(f);
        }
    }
    if (funcs.empty()) return;

    // 4. 提前为所有指令命名（避免多线程静态计数器竞争）
    for (auto f : funcs) {
        f->set_instr_name();
    }

    // 5. 计算可用线程数（排除隔离核心2、3）
    unsigned numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 1;
    // 4核CPU，扣除核心2、3后最多可用2个线程
    if (numThreads <= 2) numThreads = 1;
    else numThreads = numThreads - 2;  // 对于4核=2线程

    // 6. 将函数均匀分配给各线程（轮转分配）
    std::vector<std::vector<Function*>> partitions(numThreads);
    for (size_t i = 0; i < funcs.size(); ++i) {
        partitions[i % numThreads].push_back(funcs[i]);
    }

    // 7. 存储每个函数生成结果的字符串（按原始顺序）
    std::vector<std::string> results(funcs.size());
    std::vector<std::thread> threads;

    for (unsigned t = 0; t < numThreads; ++t) {
        threads.emplace_back([this, &partitions, &results, &funcs, t, numThreads]() {
            // 设置CPU亲和性：只允许编译器线程运行在非隔离核心（核心0,1）
#ifdef __linux__
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            int numCores = sysconf(_SC_NPROCESSORS_ONLN);
            for (int i = 0; i < numCores; ++i) {
                if (i != 2 && i != 3) {   // 排除隔离核心
                    CPU_SET(i, &cpuset);
                }
            }
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif

            // 为该线程分配的每个函数生成汇编
            for (auto f : partitions[t]) {
                std::ostringstream local_os;
                Arm64FuncContext ctx(f, local_os);
                ctx.generate();

                // 找到该函数在原始funcs中的位置，存入results对应索引
                auto it = std::find(funcs.begin(), funcs.end(), f);
                size_t idx = it - funcs.begin();
                results[idx] = local_os.str();
            }
        });
    }

    // 8. 等待所有线程完成
    for (auto &th : threads) {
        th.join();
    }

    // 9. 按原始函数顺序输出结果
    for (const auto &str : results) {
        os_ << str;
    }
}

void Arm64CodeGen::emitGlobal(GlobalVariable *gv) {
    auto pointee = static_cast<PointerType*>(gv->type_)->contained_;

    if (gv->is_const_) {
        os_ << "\t.section .rodata\n";
    } else if (gv->init_val_ && !dynamic_cast<ConstantZero*>(gv->init_val_)) {
        os_ << "\t.data\n";
    } else {
        os_ << "\t.bss\n";
    }

    os_ << "\t.global " << gv->name_ << "\n";
    os_ << "\t.p2align 2\n";
    os_ << gv->name_ << ":\n";

    if (auto cz = dynamic_cast<ConstantZero*>(gv->init_val_)) {
        int size = 4; // default i32/float
        Type *ty = pointee;
        if (ty->tid_ == Type::ArrayTyID) {
            // Total elements
            int totalElements = 1;
            Type *cur = ty;
            while (auto arrTy = dynamic_cast<ArrayType*>(cur)) {
                totalElements *= arrTy->num_elements_;
                cur = arrTy->contained_;
            }
            // Element size
            int elemSize = 4; 
            if (cur->tid_ == Type::IntegerTyID) {
                elemSize = static_cast<IntegerType*>(cur)->num_bits_ / 8;
            } else if (cur->tid_ == Type::FloatTyID) {
                elemSize = 4; // float set to 4 bytes
            }
            size = totalElements * elemSize;
        }
        os_ << "\t.zero " << size << "\n";
    } else if (auto ci = dynamic_cast<ConstantInt*>(gv->init_val_)) {
        os_ << "\t.word " << ci->value_ << "\n";
    } else if (auto cf = dynamic_cast<ConstantFloat*>(gv->init_val_)) {
        int bits;
        float val = cf->value_;
        os_ << "\t.word 0x" << std::hex << *reinterpret_cast<int*>(&val) << std::dec << "\n";
    } else if (auto ca = dynamic_cast<ConstantArray*>(gv->init_val_)) {
        for (auto elem : ca->const_array) {
            if (auto eci = dynamic_cast<ConstantInt*>(elem)) {
                os_ << "\t.word " << eci->value_ << "\n";
            } else if (auto ecf = dynamic_cast<ConstantFloat*>(elem)) {
                float val = ecf->value_;
                os_ << "\t.word 0x" << std::hex << *reinterpret_cast<int*>(&val) << std::dec << "\n";
            } else if (auto eca = dynamic_cast<ConstantArray*>(elem)) {
                for (auto sub : eca->const_array) {
                    if (auto sci = dynamic_cast<ConstantInt*>(sub)) {
                        os_ << "\t.word " << sci->value_ << "\n";
                    } else if (auto scf = dynamic_cast<ConstantFloat*>(sub)) {
                        float val = scf->value_;
                        os_ << "\t.word 0x" << std::hex << *reinterpret_cast<int*>(&val) << std::dec << "\n";
                    }
                }
            } else if (dynamic_cast<ConstantZero*>(elem)) {
                os_ << "\t.word 0\n";
            }
        }
    }
    os_ << "\n";
}

void Arm64CodeGen::emitExtern(Function *f) {
    // __aeabi_* 函数会作为普通外部函数处理
    (void)f;
}

void Arm64CodeGen::emitFunction(Function *f) {
    Arm64FuncContext ctx(f, os_);
    ctx.generate();
}
