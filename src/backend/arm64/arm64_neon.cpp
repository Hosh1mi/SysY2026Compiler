#include "../include/backend/arm64/arm64_context.hpp"
#include "../include/mid/ir/ir.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <vector>

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
    for (int r = 0; r <= 7; r++) {
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
        os_.rdbuf(oldBuf);
        return false;
    }

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

    if (loadVecs.empty() || storeVecs.empty()) {
        os_.rdbuf(oldBuf);
        return false;
    }

    os_.rdbuf(oldBuf);
    return false;
}
