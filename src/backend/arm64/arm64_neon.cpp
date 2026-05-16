#include "../../include/backend/arm64/arm64_context.hpp"
#include "../../include/mid/ir/ir.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#define NEON_LOG(msg) if (debugNEON) std::cerr << "[NEON] " << msg << "\n"

static int typeSize(Type *ty) {
    switch (ty->tid_) {
    case Type::IntegerTyID: return 4;
    case Type::FloatTyID:   return 4;
    case Type::PointerTyID: return 8;
    case Type::ArrayTyID:
        return static_cast<ArrayType*>(ty)->num_elements_ *
               typeSize(static_cast<ArrayType*>(ty)->contained_);
    default: return 8;
    }
}

// ── NEON emit helpers ────────────────────────────────────────────────

static void emitNEON_ld1_4s(std::ostream &os, const std::string &vreg,
                             const std::string &addr) {
    os << "\tld1 {" << vreg << ".4s}, [" << addr << "]\n";
}

static void emitNEON_ld1_lane_s(std::ostream &os, const std::string &vreg,
                                 int lane, const std::string &addr) {
    os << "\tld1 {" << vreg << ".s}[" << lane << "], [" << addr << "]\n";
}

static void emitNEON_st1_4s(std::ostream &os, const std::string &vreg,
                             const std::string &addr) {
    os << "\tst1 {" << vreg << ".4s}, [" << addr << "]\n";
}

static void emitNEON_binop_4s(std::ostream &os, const char *neonOp,
                               const std::string &vd, const std::string &vn,
                               const std::string &vm) {
    os << "\t" << neonOp << " " << vd << ".4s, " << vn
       << ".4s, " << vm << ".4s\n";
}

// ── NEON register pool ───────────────────────────────────────────────

void Arm64FuncContext::resetNEONRegs() {
    usedNEONRegs_.clear();
}

std::string Arm64FuncContext::allocNEONReg() {
    // Caller-saved NEON registers: v0-v7, v16-v31 (24 regs total)
    for (int r = 0; r <= 7; r++) {
        if (!usedNEONRegs_.count(r)) {
            usedNEONRegs_.insert(r);
            return "v" + std::to_string(r);
        }
    }
    for (int r = 16; r <= 31; r++) {
        if (!usedNEONRegs_.count(r)) {
            usedNEONRegs_.insert(r);
            return "v" + std::to_string(r);
        }
    }
    usedNEONRegs_.insert(0);
    return "v0";
}

void Arm64FuncContext::freeNEONReg(const std::string &reg) {
    if (reg.size() >= 2 && reg[0] == 'v') {
        usedNEONRegs_.erase(std::stoi(reg.substr(1)));
    }
}
// Scan a basic block for groups of 4 scalar loads / stores / binops
// that form a stride-1 vectorizable pattern.  When a complete
// load→(op)→store chain is found, emit compact NEON instructions
// and return true so the caller can skip the matched IR instructions.

bool Arm64FuncContext::tryEmitNEON(BasicBlock *bb) {
    static bool debugNEON = std::getenv("NEON_DEBUG") != nullptr;

    NEON_LOG("=== block: " + func_->name_ + "::" + bb->name_ + " ===");

    // Redirect output to a temp buffer so NEON instructions are not
    // emitted at the top of the block (before e.g. __aeabi_memclr4).
    auto oldBuf = os_.rdbuf();
    std::ostringstream tmpStream;
    os_.rdbuf(tmpStream.rdbuf());

    // Collect non-phi, non-terminator instructions
    std::vector<Instruction*> insts;
    for (auto inst : bb->instr_list_) {
        if (inst->is_phi()) continue;
        if (inst->isTerminator()) break;
        insts.push_back(inst);
    }

    // ── Loop body detection (for single-iteration loops) ──────────
    // If this block is the body of a simple loop (header→body→back-edge),
    // we can virtually unroll it into 4-lane NEON without mid-end changes.
    bool isLoopBody = false;
    PhiInst *loopIV = nullptr;
    Value *loopBound = nullptr;
    Instruction *loopIncr = nullptr;   // the IV+1 instruction in the body
    BasicBlock *loopHeader = nullptr;
    BasicBlock *loopExit = nullptr;
    int loopStride = 1;
    {
        auto *term = bb->get_terminator();
        if (term && term->is_br() && term->num_ops_ == 1) {
            auto *hdr = static_cast<BasicBlock*>(term->get_operand(0));
            // Candidate header: must have a conditional branch splitting
            // to bb (body) and some exit
            auto *hdrTerm = hdr->get_terminator();
            if (hdrTerm && hdrTerm->is_br() && hdrTerm->num_ops_ == 3) {
                auto *hdrTrueBB  = static_cast<BasicBlock*>(hdrTerm->get_operand(1));
                auto *hdrFalseBB = static_cast<BasicBlock*>(hdrTerm->get_operand(2));
                BasicBlock *bodyBB = nullptr, *exitBB = nullptr;
                if (hdrTrueBB == bb && hdrFalseBB != bb) { bodyBB = bb; exitBB = hdrFalseBB; }
                else if (hdrFalseBB == bb && hdrTrueBB != bb) { bodyBB = bb; exitBB = hdrTrueBB; }
                if (bodyBB && exitBB) {
                    // Find IV phi in header
                    for (auto inst : hdr->instr_list_) {
                        if (!inst->is_phi()) break;
                        auto *phi = static_cast<PhiInst*>(inst);
                        if (phi->type_->tid_ != Type::IntegerTyID) continue;
                        // Check for IV pattern: one incoming from outside, one from latch
                        Value *initVal = nullptr, *latchVal = nullptr;
                        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                            auto *pred = static_cast<BasicBlock*>(phi->get_operand(i + 1));
                            if (pred == bb) latchVal = phi->get_operand(i);
                            else initVal = phi->get_operand(i);
                        }
                        if (!initVal || !latchVal) continue;
                        // Latch value must be add(phi, constant)
                        auto *upd = dynamic_cast<BinaryInst*>(latchVal);
                        if (!upd || !upd->is_add()) continue;
                        Value *op0 = upd->get_operand(0), *op1 = upd->get_operand(1);
                        int stride = 0;
                        if (op0 == phi && dynamic_cast<ConstantInt*>(op1))
                            stride = static_cast<ConstantInt*>(op1)->value_;
                        else if (op1 == phi && dynamic_cast<ConstantInt*>(op0))
                            stride = static_cast<ConstantInt*>(op0)->value_;
                        if (stride < 1 || stride > 4) continue;
                        // Find the icmp in header that branches on the IV
                        auto *cond = dynamic_cast<ICmpInst*>(hdrTerm->get_operand(0));
                        if (!cond) continue;
                        Value *bound = nullptr;
                        if (cond->get_operand(0) == phi) bound = cond->get_operand(1);
                        else if (cond->get_operand(1) == phi) bound = cond->get_operand(0);
                        else continue;
                        // Found!
                        loopIV = phi; loopBound = bound; loopIncr = upd;
                        loopHeader = hdr; loopExit = exitBB;
                        loopStride = stride;
                        isLoopBody = true;
                        break;
                    }
                }
            }
        }
    }

    if (insts.size() < 4 && !isLoopBody) {
        NEON_LOG("  reject: too few insts (" << insts.size() << ") and not loop body");
        os_.rdbuf(oldBuf);
        return false;
    }

    if (isLoopBody)
        NEON_LOG("  loop body: IV=" << (loopIV ? loopIV->name_ : "?")
                 << " stride=" << loopStride);

    // ── Group loads / stores by base pointer ──────────────────────
    // key: base pointer (first GEP operand after stripping), value: list of (inst, elementOffset)
    std::map<Value*, std::vector<std::pair<Instruction*, int>>> loadGroups;
    std::map<Value*, std::vector<std::pair<Instruction*, int>>> storeGroups;

    // Also track: load → vector of 4 loads that form a NEON group
    // and the gep index operands that give the element offset
    struct NEONGroup {
        std::vector<LoadInst*> loads;
        std::vector<StoreInst*> stores;
        std::vector<BinaryInst*> binops;
        Value *base;         // common base pointer
        Value *ivBase;       // the IV or IV+k value that the element offset is relative to
        int offset0;         // element offset of first access
    };

    // Helper: extract element offset from a GEP's last index.
    // Returns true if the index is "IV + const" or the IV itself.
    // Recursively walks chained adds (e.g. add(add(IV, 1), 1) → offset 2)
    // produced by unrolled loop bodies.
    auto extractOffset = [](Value *idx, int &off) -> bool {
        off = 0;
        while (true) {
            if (auto *ci = dynamic_cast<ConstantInt*>(idx)) {
                off += ci->value_; return true;
            }
            if (auto *bin = dynamic_cast<BinaryInst*>(idx)) {
                if (!bin->is_add()) return false;
                Value *a = bin->get_operand(0), *b = bin->get_operand(1);
                auto *ca = dynamic_cast<ConstantInt*>(a);
                auto *cb = dynamic_cast<ConstantInt*>(b);
                if (ca && cb) { off += ca->value_ + cb->value_; return true; }
                if (ca) { off += ca->value_; idx = b; continue; }
                if (cb) { off += cb->value_; idx = a; continue; }
                // Neither operand is a constant — bare IV relative
                return true;
            }
            // Bare IV phi or other non-constant instruction — done
            return true;
        }
    };

    for (auto *inst : insts) {
        if (inst->is_load()) {
            auto *ld = static_cast<LoadInst*>(inst);
            Value *ptr = ld->get_operand(0);
            auto *gep = dynamic_cast<GetElementPtrInst*>(ptr);
            if (!gep || gep->num_ops_ < 2) continue;
            Value *base = gep->get_operand(0);
            Value *lastIdx = gep->get_operand(gep->num_ops_ - 1);
            int elemOff = 0;
            if (!extractOffset(lastIdx, elemOff)) continue;
            loadGroups[base].push_back({inst, elemOff});
        } else if (inst->is_store()) {
            auto *st = static_cast<StoreInst*>(inst);
            Value *ptr = st->get_operand(1);
            auto *gep = dynamic_cast<GetElementPtrInst*>(ptr);
            if (!gep || gep->num_ops_ < 2) continue;
            Value *base = gep->get_operand(0);
            Value *lastIdx = gep->get_operand(gep->num_ops_ - 1);
            int elemOff = 0;
            if (!extractOffset(lastIdx, elemOff)) continue;
            storeGroups[base].push_back({inst, elemOff});
        }
    }

    // ── Find groups of 4 loads with consecutive element offsets ───
    struct LoadVec { Value *base; int off0; std::vector<LoadInst*> loads; };
    std::vector<LoadVec> loadVecs;

    for (auto &[base, entries] : loadGroups) {
        // Sort by element offset
        std::sort(entries.begin(), entries.end(),
            [](auto &a, auto &b) { return a.second < b.second; });
        // Look for runs of 4 consecutive offsets
        for (size_t i = 0; i + 3 < entries.size(); ) {
            int off0 = entries[i].second;
            if (entries[i+1].second == off0 + 1 &&
                entries[i+2].second == off0 + 2 &&
                entries[i+3].second == off0 + 3) {
                LoadVec lv;
                lv.base = base;
                lv.off0 = off0;
                for (int j = 0; j < 4; j++)
                    lv.loads.push_back(static_cast<LoadInst*>(entries[i+j].first));
                loadVecs.push_back(lv);
                i += 4;
            } else {
                i++;
            }
        }
    }

    // ── Find groups of 4 stores with consecutive element offsets ──
    struct StoreVec { Value *base; int off0; std::vector<StoreInst*> stores; };
    std::vector<StoreVec> storeVecs;

    for (auto &[base, entries] : storeGroups) {
        std::sort(entries.begin(), entries.end(),
            [](auto &a, auto &b) { return a.second < b.second; });
        for (size_t i = 0; i + 3 < entries.size(); ) {
            int off0 = entries[i].second;
            if (entries[i+1].second == off0 + 1 &&
                entries[i+2].second == off0 + 2 &&
                entries[i+3].second == off0 + 3) {
                StoreVec sv;
                sv.base = base;
                sv.off0 = off0;
                for (int j = 0; j < 4; j++)
                    sv.stores.push_back(static_cast<StoreInst*>(entries[i+j].first));
                storeVecs.push_back(sv);
                i += 4;
            } else {
                i++;
            }
        }
    }

    NEON_LOG("  loadGroups=" << loadGroups.size()
             << " loadVecs=" << loadVecs.size()
             << " storeGroups=" << storeGroups.size()
             << " storeVecs=" << storeVecs.size());
    if (debugNEON) {
        for (auto &[base, entries] : loadGroups) {
            std::cerr << "  load base=" << (!base->name_.empty() ? base->name_ : "?")
                      << " offsets:";
            for (auto &e : entries) std::cerr << " " << e.second;
            std::cerr << "\n";
        }
        for (auto &[base, entries] : storeGroups) {
            std::cerr << "  store base=" << (!base->name_.empty() ? base->name_ : "?")
                      << " offsets:";
            for (auto &e : entries) std::cerr << " " << e.second;
            std::cerr << "\n";
        }
    }

    // Helper to emit address of a GEP into a fresh address register
    std::function<std::string(Value*)> emitGEPAddr;
    emitGEPAddr = [&](Value *gepVal) -> std::string {
        auto *gep = static_cast<GetElementPtrInst*>(gepVal);
        std::string baseReg;
        Value *base = gep->get_operand(0);
        if (auto *gv = dynamic_cast<GlobalVariable*>(base)) {
            baseReg = allocAddrReg();
            os_ << "\tadrp " << baseReg << ", " << gv->name_ << "\n";
            os_ << "\tadd " << baseReg << ", " << baseReg
                << ", :lo12:" << gv->name_ << "\n";
        } else if (auto *alloca = dynamic_cast<AllocaInst*>(base)) {
            // Alloca: emit stack address computation even if a register
            // is pre-assigned — NEON lowering runs before normal
            // instruction emission, so no one else will emit this.
            if (hasAssignedReg(base))
                baseReg = assignedReg(base, true);
            else
                baseReg = allocAddrReg();
            int off = getSlot(base);
            if (off < 0) {
                int absOff = -off;
                if (absOff <= 4095)
                    os_ << "\tsub " << baseReg << ", x29, #" << absOff << "\n";
                else {
                    os_ << "\tmovz x17, #" << (absOff & 0xFFFF) << "\n";
                    os_ << "\tmovk x17, #" << ((absOff >> 16) & 0xFFFF) << ", lsl #16\n";
                    os_ << "\tsub " << baseReg << ", x29, x17\n";
                }
            } else {
                if (off <= 4095)
                    os_ << "\tadd " << baseReg << ", x29, #" << off << "\n";
                else {
                    os_ << "\tmovz x17, #" << (off & 0xFFFF) << "\n";
                    os_ << "\tmovk x17, #" << ((off >> 16) & 0xFFFF) << ", lsl #16\n";
                    os_ << "\tadd " << baseReg << ", x29, x17\n";
                }
            }
        } else if (dynamic_cast<GetElementPtrInst*>(base)) {
            baseReg = emitGEPAddr(base);
        } else {
            baseReg = loadAddr(base);
            // loadAddr may return a pre-assigned callee-saved register
            // (e.g. a function parameter). Copy it before adding offsets
            // so the original register is not corrupted.
            if (gep->num_ops_ > 1) {
                std::string fresh = allocAddrReg();
                os_ << "\tmov " << fresh << ", " << baseReg << "\n";
                baseReg = fresh;
            }
        }
        Type *curTy = static_cast<PointerType*>(base->type_)->contained_;
        for (unsigned i = 1; i < gep->num_ops_; i++) {
            int elemSize = typeSize(curTy);
            // Advance curTy for next iteration
            if (curTy->tid_ == Type::ArrayTyID)
                curTy = static_cast<ArrayType*>(curTy)->contained_;
            else if (curTy->tid_ == Type::PointerTyID)
                curTy = static_cast<PointerType*>(curTy)->contained_;

            Value *idx = gep->get_operand(i);
            if (auto *ci = dynamic_cast<ConstantInt*>(idx)) {
                int offset = ci->value_ * elemSize;
                if (offset == 0) continue;
                if (offset > 0 && offset <= 4095)
                    os_ << "\tadd " << baseReg << ", " << baseReg << ", #" << offset << "\n";
                else if (offset < 0 && -offset <= 4095)
                    os_ << "\tsub " << baseReg << ", " << baseReg << ", #" << -offset << "\n";
                else {
                    os_ << "\tmovz x17, #" << (abs(offset) & 0xFFFF) << "\n";
                    os_ << "\tmovk x17, #" << ((abs(offset) >> 16) & 0xFFFF) << ", lsl #16\n";
                    os_ << (offset > 0 ? "\tadd " : "\tsub ")
                        << baseReg << ", " << baseReg << ", x17\n";
                }
                continue;
            }
            std::string idxReg;
            if (hasAssignedReg(idx)) {
                idxReg = assignedReg(idx);
            } else {
                idxReg = loadInt(idx);
            }
            std::string scaled = allocAddrReg();
            os_ << "\tsxtw " << scaled << ", " << idxReg << "\n";
            if (elemSize > 1) {
                auto isPowerOfTwo = [](int n) { return n > 0 && (n & (n - 1)) == 0; };
                if (isPowerOfTwo(elemSize)) {
                    int shift = 0;
                    while ((1 << shift) < elemSize) shift++;
                    os_ << "\tadd " << baseReg << ", " << baseReg
                        << ", " << scaled << ", lsl #" << shift << "\n";
                } else {
                    std::string elemReg = allocAddrReg();
                    uint32_t val = static_cast<uint32_t>(elemSize);
                    os_ << "\tmovz " << elemReg << ", #" << (val & 0xFFFF) << "\n";
                    if (val & 0xFFFF0000)
                        os_ << "\tmovk " << elemReg << ", #" << ((val >> 16) & 0xFFFF) << ", lsl #16\n";
                    os_ << "\tmul " << scaled << ", " << scaled << ", " << elemReg << "\n";
                    os_ << "\tadd " << baseReg << ", " << baseReg << ", " << scaled << "\n";
                    freeAddrReg(elemReg);
                }
            } else {
                os_ << "\tadd " << baseReg << ", " << baseReg << ", " << scaled << "\n";
            }
            freeAddrReg(scaled);
        }
        return baseReg;
    };

    // ── Loop body NEON virtual unrolling ──────────────────────────
    {
        int storeCount = 0;
        for (auto *i : insts) if (i->is_store()) storeCount++;
        bool tryLoopBody = isLoopBody && storeCount == 1 && insts.size() <= 8;
        if (!isLoopBody && debugNEON)
            NEON_LOG("  reject loop-body: not loop body");
        else if (isLoopBody && storeCount != 1 && debugNEON)
            NEON_LOG("  reject loop-body: storeCount=" << storeCount << " (need 1)");
        else if (isLoopBody && insts.size() > 8 && debugNEON)
            NEON_LOG("  reject loop-body: insts.size()=" << insts.size() << " (max 8)");
        if (tryLoopBody) {
            for (auto *inst : insts) {
                if (!inst->is_store()) continue;
                auto *si = static_cast<StoreInst*>(inst);
                auto *stGep = dynamic_cast<GetElementPtrInst*>(si->get_operand(1));
                if (!stGep || stGep->num_ops_ < 2) continue;
                auto *ld = dynamic_cast<LoadInst*>(si->get_operand(0));
                if (!ld) continue;
                auto *ldGep = dynamic_cast<GetElementPtrInst*>(ld->get_operand(0));
                if (!ldGep || ldGep->num_ops_ < 2) continue;

                Value *ldLast = ldGep->get_operand(ldGep->num_ops_-1);
                bool ldConsecutive = (ldLast == loopIV);
                if (!ldConsecutive) {
                    auto *ldBin = dynamic_cast<BinaryInst*>(ldLast);
                    if (ldBin && ldBin->is_add()) {
                        Value *a = ldBin->get_operand(0), *b = ldBin->get_operand(1);
                        ldConsecutive = (a == loopIV || b == loopIV);
                    }
                }

                int ivIdxPos = -1, stride = 0;
                if (!ldConsecutive) {
                    for (unsigned k = 1; k < ldGep->num_ops_; k++) {
                        Value *idx = ldGep->get_operand(k);
                        if (idx == loopIV) { ivIdxPos = (int)k; break; }
                        auto *ib = dynamic_cast<BinaryInst*>(idx);
                        if (ib && ib->is_add()) {
                            Value *a = ib->get_operand(0), *b = ib->get_operand(1);
                            if (a == loopIV || b == loopIV) { ivIdxPos = (int)k; break; }
                        }
                    }
                    if (ivIdxPos < 0) continue;
                    Type *ty = static_cast<PointerType*>(ldGep->get_operand(0)->type_)->contained_;
                    for (unsigned k = 1; k <= (unsigned)ivIdxPos; k++) {
                        stride = typeSize(ty);
                        if (k < (unsigned)ivIdxPos)
                            if (auto *arr = dynamic_cast<ArrayType*>(ty)) ty = arr->contained_;
                    }
                }

                // Remainder guard
                std::string rlbl = ".L_" + func_->name_ + "_r" +
                    std::to_string(edgeCounter_++);
                {
                    std::string ivReg = hasAssignedReg(loopIV) ? assignedReg(loopIV)
                        : loadInt(loopIV);
                    std::string bndReg;
                    if (hasAssignedReg(loopBound)) bndReg = assignedReg(loopBound);
                    else if (auto *ci = dynamic_cast<ConstantInt*>(loopBound))
                        { bndReg = allocIntReg(); emitIntConst(ci->value_, bndReg); }
                    else bndReg = loadInt(loopBound);
                    std::string rr = allocIntReg();
                    os_ << "\tsub " << rr << ", " << bndReg << ", " << ivReg << "\n";
                    os_ << "\tcmp " << rr << ", #4\n";
                    os_ << "\tblt " << rlbl << "\n";
                }

                resetNEONRegs();
                std::string v0 = allocNEONReg();

                if (ldConsecutive) {
                    std::string ldAddr = emitGEPAddr(ldGep);
                    emitNEON_ld1_4s(os_, v0, ldAddr);
                    freeAddrReg(ldAddr);
                } else {
                    std::string ba = emitGEPAddr(ldGep);
                    emitNEON_ld1_lane_s(os_, v0, 0, ba);
                    if (stride > 0) {
                        std::string sr = allocAddrReg();
                        os_ << "\tmovz " << sr << ", #" << (stride & 0xFFFF) << "\n";
                        if (stride > 0xFFFF)
                            os_ << "\tmovk " << sr << ", #" << ((stride>>16)&0xFFFF) << ", lsl #16\n";
                        for (int lane = 1; lane < 4; lane++) {
                            std::string lo = allocAddrReg();
                            if (lane == 1) os_ << "\tmov " << lo << ", " << sr << "\n";
                            else {
                                std::string prev = allocAddrReg();
                                os_ << "\tmovz " << prev << ", #" << lane << "\n";
                                os_ << "\tmul " << lo << ", " << sr << ", " << prev << "\n";
                                freeAddrReg(prev);
                            }
                            std::string la = allocAddrReg();
                            os_ << "\tadd " << la << ", " << ba << ", " << lo << "\n";
                            emitNEON_ld1_lane_s(os_, v0, lane, la);
                            freeAddrReg(la); freeAddrReg(lo);
                        }
                        freeAddrReg(sr);
                    }
                    freeAddrReg(ba);
                }
                {
                    std::string stAddr = emitGEPAddr(stGep);
                    emitNEON_st1_4s(os_, v0, stAddr);
                    freeAddrReg(stAddr);
                }
                os_ << rlbl << ":\n";

                neonEmitted_.insert(si);
                neonEmitted_.insert(ld);
                if (loopIncr) {
                    neonEmitted_.insert(loopIncr);
                    std::string ivDst = hasAssignedReg(loopIV) ? assignedReg(loopIV)
                        : loadInt(loopIV);
                    os_ << "\tadd " << ivDst << ", " << ivDst << ", #4\n";
                    // If loopIncr result is spilled, update the spill slot
                    // so the phi copy at the end of this block loads the
                    // correct value instead of stale pre-NEON data.
                    if (!hasAssignedReg(loopIncr)) {
                        int slot = getSlot(loopIncr);
                        if (slot >= -256 && slot <= 255) {
                            os_ << "\tstr " << ivDst << ", [x29, #" << slot << "]\n";
                        } else {
                            int pos = -slot;
                            if (pos <= 4095)
                                os_ << "\tsub x17, x29, #" << pos << "\n";
                            else {
                                os_ << "\tmovz x17, #" << (pos & 0xFFFF) << "\n";
                                os_ << "\tmovk x17, #" << ((pos >> 16) & 0xFFFF) << ", lsl #16\n";
                                os_ << "\tsub x17, x29, x17\n";
                            }
                            os_ << "\tstr " << ivDst << ", [x17]\n";
                        }
                    }
                }

                deferredNEONCode_ = tmpStream.str();
                os_.rdbuf(oldBuf);
                return true;
            }
        }
        if (isLoopBody) { os_.rdbuf(oldBuf); return false; }
    }

    // Constant store pattern: 4 stores of the same constant.
    // e.g. for(i) A[i]=-1; A[i+1]=-1; A[i+2]=-1; A[i+3]=-1;
    // Does not require any loads.
    for (auto &sv : storeVecs) {
        if (sv.stores.size() != 4) continue;
        Value *val0 = sv.stores[0]->get_operand(0);
        auto *ci0 = dynamic_cast<ConstantInt*>(val0);
        if (!ci0) continue;
        bool sameConst = true;
        for (int i = 1; i < 4; i++) {
            auto *ci = dynamic_cast<ConstantInt*>(sv.stores[i]->get_operand(0));
            if (!ci || ci->value_ != ci0->value_) { sameConst = false; break; }
        }
        if (!sameConst) continue;

        NEON_LOG("  pattern: const-fill val=" << ci0->value_ << " off0=" << sv.off0);

        resetNEONRegs();
        std::set<Instruction*> matched;

        std::string wtmp = allocIntReg();
        emitIntConst(ci0->value_, wtmp);
        std::string v0 = allocNEONReg();
        os_ << "\tdup " << v0 << ".4s, " << wtmp << "\n";
        freeIntReg(wtmp);

        std::string addr = emitGEPAddr(sv.stores[0]->get_operand(1));
        emitNEON_st1_4s(os_, v0, addr);
        freeAddrReg(addr);

        // Mark only the stores, not the GEPs — the GEPs must still be
        // emitted normally so their assigned registers get set.
        for (int i = 0; i < 4; i++)
            matched.insert(sv.stores[i]);
        for (auto *m : matched) neonEmitted_.insert(m);
        resetNEONRegs();
        deferredNEONCode_ = tmpStream.str();
        os_.rdbuf(oldBuf);
        return true;
    }

    // ── Arbitrary scalar fill ──────────────────────────────────────
    // Extends constant fill: 4 stores of the same arbitrary Value*,
    // not necessarily a constant.  Load into GP reg, dup, st1.
    for (auto &sv : storeVecs) {
        if (sv.stores.size() < 4) continue;
        Value *v0 = sv.stores[0]->get_operand(0);
        // Skip constants — already handled above
        if (dynamic_cast<ConstantInt*>(v0) || dynamic_cast<ConstantFloat*>(v0))
            continue;
        bool sameVal = true;
        for (int i = 1; i < 4; i++) {
            if (sv.stores[i]->get_operand(0) != v0) { sameVal = false; break; }
        }
        if (!sameVal) continue;

        NEON_LOG("  pattern: scalar-fill off0=" << sv.off0);

        resetNEONRegs();
        bool isFloat = v0->type_->tid_ == Type::FloatTyID;
        std::string srcReg;
        if (hasAssignedReg(v0)) {
            srcReg = assignedReg(v0);
        } else if (isFloat) {
            srcReg = loadFloat(v0);
        } else {
            srcReg = loadInt(v0);
        }

        std::string vreg = allocNEONReg();
        if (isFloat) {
            // float: dup v.4s, vN.s[0] — use the SIMD reg directly
            // assignedReg for float returns "sN", need "vN" for dup lane
            std::string vSrc = srcReg;
            if (!vSrc.empty() && vSrc[0] == 's')
                vSrc = "v" + vSrc.substr(1);
            os_ << "\tdup " << vreg << ".4s, " << vSrc << ".s[0]\n";
        } else {
            os_ << "\tdup " << vreg << ".4s, " << srcReg << "\n";
        }

        std::string addr = emitGEPAddr(sv.stores[0]->get_operand(1));
        emitNEON_st1_4s(os_, vreg, addr);
        freeAddrReg(addr);

        for (int i = 0; i < 4; i++)
            neonEmitted_.insert(sv.stores[i]);
        resetNEONRegs();
        deferredNEONCode_ = tmpStream.str();
        os_.rdbuf(oldBuf);
        return true;
    }

    if (loadVecs.empty() || storeVecs.empty()) {
        if (debugNEON) {
            if (loadVecs.empty())  NEON_LOG("  reject: no loadVecs");
            if (storeVecs.empty()) NEON_LOG("  reject: no storeVecs");
        }
        os_.rdbuf(oldBuf);
        return false;
    }

    // ── load-load-binop-store pattern ──────────────────────────────
    // Matches: x0=load A[i+0]; y0=load B[i+0]; z0=add x0,y0; store z0,C[i+0]
    //          ... (×4 consecutive)
    // Emits:  ld1 {vA.4s}, [A]; ld1 {vB.4s}, [B]; op vC.4s, vA.4s, vB.4s; st1 {vC.4s}, [C]
    for (auto &sv : storeVecs) {
        if (sv.stores.size() < 4) continue;

        // Each stored value must be a BinaryInst of the same opcode
        BinaryInst *bins[4] = {};
        Instruction::OpID op;
        bool valid = true;
        for (int i = 0; i < 4; i++) {
            bins[i] = dynamic_cast<BinaryInst*>(sv.stores[i]->get_operand(0));
            if (!bins[i]) { valid = false; break; }
            if (i == 0) op = bins[i]->op_id_;
            else if (bins[i]->op_id_ != op) { valid = false; break; }
        }
        if (!valid) {
            if (debugNEON) NEON_LOG("  reject binop-store: not all binops or mixed ops, off0=" << sv.off0);
            continue;
        }

        const char *neonOp = nullptr;
        bool commutative = false;
        switch (op) {
        case Instruction::Add:  neonOp = "add";  commutative = true; break;
        case Instruction::Mul:  neonOp = "mul";  commutative = true; break;
        case Instruction::Sub:  neonOp = "sub";  break;
        case Instruction::FAdd: neonOp = "fadd"; commutative = true; break;
        case Instruction::FMul: neonOp = "fmul"; commutative = true; break;
        case Instruction::FSub: neonOp = "fsub"; break;
        default:
            if (debugNEON) NEON_LOG("  reject binop-store: unsupported op off0=" << sv.off0);
            continue;
        }

        // For each binop, extract the two LoadInsts and group by GEP base.
        // Determine (baseA, posA) and (baseB, posB) from first binop, then verify.
        Value *baseA = nullptr, *baseB = nullptr;
        int posA = -1, posB = -1;  // which operand position (0 or 1)
        struct LoadInfo { LoadInst *ld; GetElementPtrInst *gep; int offset; };
        LoadInfo liA[4] = {}, liB[4] = {};

        for (int i = 0; i < 4; i++) {
            auto *l0 = dynamic_cast<LoadInst*>(bins[i]->get_operand(0));
            auto *l1 = dynamic_cast<LoadInst*>(bins[i]->get_operand(1));
            if (!l0 || !l1) { valid = false; break; }
            auto *g0 = dynamic_cast<GetElementPtrInst*>(l0->get_operand(0));
            auto *g1 = dynamic_cast<GetElementPtrInst*>(l1->get_operand(0));
            if (!g0 || !g1) { valid = false; break; }
            Value *b0 = g0->get_operand(0);
            Value *b1 = g1->get_operand(0);

            // Handle the case where both operands come from the same base
            // (e.g. A[i] = A[i] * A[i]).  This is a splat, not a two-load binop.
            if (i == 0 && b0 == b1) { valid = false; break; }

            if (i == 0) {
                baseA = b0; posA = 0;
                baseB = b1; posB = 1;
            }

            // Determine which operand goes to which base
            LoadInst *ldA = nullptr, *ldB = nullptr;
            GetElementPtrInst *gepA = nullptr, *gepB = nullptr;
            if (b0 == baseA && b1 == baseB) {
                ldA = l0; gepA = g0; ldB = l1; gepB = g1;
            } else if (b0 == baseB && b1 == baseA) {
                if (!commutative) { valid = false; break; }
                ldA = l1; gepA = g1; ldB = l0; gepB = g0;
            } else {
                valid = false; break;
            }

            Value *idxA = gepA->get_operand(gepA->num_ops_ - 1);
            Value *idxB = gepB->get_operand(gepB->num_ops_ - 1);
            int offA, offB;
            if (!extractOffset(idxA, offA) || !extractOffset(idxB, offB))
                { valid = false; break; }
            liA[i] = {ldA, gepA, offA};
            liB[i] = {ldB, gepB, offB};
        }

        if (!valid || !baseA || !baseB) continue;

        // Verify consecutive offsets for both load groups
        int off0 = sv.off0;
        for (int i = 0; i < 4; i++) {
            if (liA[i].offset != off0 + i) { valid = false; break; }
            if (liB[i].offset != off0 + i) { valid = false; break; }
        }
        if (!valid) {
            if (debugNEON) NEON_LOG("  reject binop-store: non-consecutive load offsets, off0=" << sv.off0);
            continue;
        }

        NEON_LOG("  pattern: load-load-binop-store op=" << neonOp
                 << " off0=" << off0
                 << " baseA=" << (!baseA->name_.empty() ? baseA->name_ : "?")
                 << " baseB=" << (!baseB->name_.empty() ? baseB->name_ : "?"));

        resetNEONRegs();
        std::string vA = allocNEONReg();
        std::string vB = allocNEONReg();
        std::string vC = allocNEONReg();

        std::string addrA = emitGEPAddr(liA[0].gep);
        std::string addrB = emitGEPAddr(liB[0].gep);
        emitNEON_ld1_4s(os_, vA, addrA);
        emitNEON_ld1_4s(os_, vB, addrB);
        emitNEON_binop_4s(os_, neonOp, vC, vA, vB);
        freeAddrReg(addrA);
        freeAddrReg(addrB);

        std::string addrC = emitGEPAddr(
            static_cast<GetElementPtrInst*>(sv.stores[0]->get_operand(1)));
        emitNEON_st1_4s(os_, vC, addrC);
        freeAddrReg(addrC);

        for (int i = 0; i < 4; i++) {
            neonEmitted_.insert(liA[i].ld);
            neonEmitted_.insert(liB[i].ld);
            neonEmitted_.insert(bins[i]);
            neonEmitted_.insert(sv.stores[i]);
        }
        resetNEONRegs();
        deferredNEONCode_ = tmpStream.str();
        os_.rdbuf(oldBuf);
        return true;
    }

    // ── addv reduction pattern ─────────────────────────────────────
    // Matches: s1=add sum,x0; s2=add s1,x1; s3=add s2,x2; s4=add s3,x3;
    //          where x0..x3 are 4 consecutive loads from the same base.
    // Emits:  ld1 {v0.4s}, [addr]; addv/faddv s1, v0.4s; fmov+add
    for (auto &lv : loadVecs) {
        if (lv.loads.size() < 4) continue;

        // Find the add that consumes each load.  Each load must have
        // exactly one user, and that user must be an add/fadd.
        BinaryInst *adds[4] = {};
        Value *accum = nullptr;
        bool valid = true;
        bool isFloat = false;

        for (int i = 0; i < 4; i++) {
            LoadInst *ld = lv.loads[i];
            if (ld->use_list_.size() != 1) { valid = false; break; }
            auto *bin = dynamic_cast<BinaryInst*>(ld->use_list_.begin()->val_);
            if (!bin || (!bin->is_add() && !bin->is_fadd())) { valid = false; break; }
            adds[i] = bin;
            if (i == 0) isFloat = bin->is_fadd();
            else if (isFloat != (bool)bin->is_fadd()) { valid = false; break; }
        }
        if (!valid) continue;

        // Determine the accumulator: the operand of adds[0] that is NOT lv.loads[0]
        if (adds[0]->get_operand(0) == lv.loads[0])
            accum = adds[0]->get_operand(1);
        else if (adds[0]->get_operand(1) == lv.loads[0])
            accum = adds[0]->get_operand(0);
        else continue;

        // Accumulator must not be one of the 4 loads
        bool accumIsLoad = false;
        for (int i = 0; i < 4; i++)
            if (accum == lv.loads[i]) { accumIsLoad = true; break; }
        if (accumIsLoad) continue;

        // Verify the chain: adds[i] consumes adds[i-1] + lv.loads[i]
        for (int i = 1; i < 4; i++) {
            Value *op0 = adds[i]->get_operand(0);
            Value *op1 = adds[i]->get_operand(1);
            if (!((op0 == adds[i-1] && op1 == lv.loads[i]) ||
                  (op1 == adds[i-1] && op0 == lv.loads[i]))) {
                valid = false; break;
            }
        }
        if (!valid) {
            if (debugNEON) NEON_LOG("  reject reduction: adds not chained, off0=" << lv.off0);
            continue;
        }

        NEON_LOG("  pattern: reduction " << (isFloat ? "faddv" : "addv")
                 << " off0=" << lv.off0);

        resetNEONRegs();
        std::string vLd = allocNEONReg();
        std::string addr = emitGEPAddr(
            static_cast<GetElementPtrInst*>(lv.loads[0]->get_operand(0)));
        emitNEON_ld1_4s(os_, vLd, addr);
        freeAddrReg(addr);

        // Destination register for the final result (adds[3])
        std::string dstReg;
        if (hasAssignedReg(adds[3]))
            dstReg = assignedReg(adds[3]);
        else if (isFloat)
            dstReg = allocFloatReg();
        else
            dstReg = allocIntReg();

        if (isFloat) {
            // faddv sLd, vLd.4s  →  fadd sDst, sAccum, sLd
            std::string sLd = "s" + vLd.substr(1);
            os_ << "\tfaddv " << sLd << ", " << vLd << ".4s\n";
            std::string accReg;
            if (hasAssignedReg(accum))
                accReg = assignedReg(accum);
            else
                accReg = loadFloat(accum);
            os_ << "\tfadd " << dstReg << ", " << accReg << ", " << sLd << "\n";
        } else {
            // addv sLd, vLd.4s  →  fmov wTmp, sLd  →  add wDst, wAccum, wTmp
            std::string sLd = "s" + vLd.substr(1);
            os_ << "\taddv " << sLd << ", " << vLd << ".4s\n";
            std::string wTmp = allocIntReg();
            os_ << "\tfmov " << wTmp << ", " << sLd << "\n";
            std::string accReg;
            if (hasAssignedReg(accum))
                accReg = assignedReg(accum);
            else
                accReg = loadInt(accum);
            os_ << "\tadd " << dstReg << ", " << accReg << ", " << wTmp << "\n";
            freeIntReg(wTmp);
        }

        for (int i = 0; i < 4; i++) {
            neonEmitted_.insert(lv.loads[i]);
            neonEmitted_.insert(adds[i]);
        }
        resetNEONRegs();
        deferredNEONCode_ = tmpStream.str();
        os_.rdbuf(oldBuf);
        return true;
    }

    if (debugNEON) NEON_LOG("  no pattern matched");
    os_.rdbuf(oldBuf);
    return false;
}
