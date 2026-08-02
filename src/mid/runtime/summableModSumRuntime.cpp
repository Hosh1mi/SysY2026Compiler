#include "../../include/mid/runtime/summableModSumRuntime.hpp"

#include "../../include/mid/ir/basicBlock.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/module.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

class RuntimeBuilder {
public:
    RuntimeBuilder(Module *module, Function *function)
        : module(module), function(function) {}

    BasicBlock *block(const std::string &name) {
        current = new BasicBlock(module, name, function);
        return current;
    }
    void at(BasicBlock *block) { current = block; }
    ConstantInt *i64(std::int64_t value) {
        return new ConstantInt(module->int64_ty_, value);
    }
    ConstantInt *i32(std::int64_t value) {
        return new ConstantInt(module->int32_ty_, value);
    }
    BinaryInst *bin(Instruction::OpID op, Value *lhs, Value *rhs) {
        return new BinaryInst(lhs->type_, op, lhs, rhs, current);
    }
    ICmpInst *cmp(ICmpInst::ICmpOp op, Value *lhs, Value *rhs) {
        return new ICmpInst(op, lhs, rhs, current);
    }
    SelectInst *select(Value *condition, Value *yes, Value *no) {
        return new SelectInst(condition, yes, no, current);
    }
    Value *sext(Value *value) {
        return new ZextInst(Instruction::SExt, value, module->int64_ty_, current);
    }
    Value *trunc(Value *value) {
        return new ZextInst(Instruction::Trunc, value, module->int32_ty_, current);
    }
    Value *i32Value(Value *value) { return sext(trunc(value)); }
    CallInst *call(Function *callee, std::vector<Value *> arguments) {
        return new CallInst(callee, std::move(arguments), current);
    }
    AllocaInst *slot(Type *type) { return new AllocaInst(type, current); }
    LoadInst *load(Value *address) { return new LoadInst(address, current); }
    void store(Value *value, Value *address) {
        new StoreInst(value, address, current);
    }
    void jump(BasicBlock *target) { new BranchInst(target, current); }
    void branch(Value *condition, BasicBlock *yes, BasicBlock *no) {
        new BranchInst(condition, yes, no, current);
    }
    void ret(Value *value) { new ReturnInst(value, current); }

    Module *module;
    Function *function;
    BasicBlock *current = nullptr;
};

Function *makeFunction(Module *module, const std::string &name, Type *result,
                       unsigned argumentCount, Type *argumentType = nullptr) {
    if (!argumentType) argumentType = module->int64_ty_;
    return new Function(
        new FunctionType(result,
                         std::vector<Type *>(argumentCount, argumentType)),
        name, module);
}

Function *buildFloorDiv(Module *module) {
    auto *function = makeFunction(module, "__compiler.sms.floor_div",
                                  module->int64_ty_, 2);
    RuntimeBuilder b(module, function);
    b.block("label_entry");
    Value *a = function->arguments_[0];
    Value *d = function->arguments_[1];
    Value *q = b.bin(Instruction::SDiv, a, d);
    Value *r = b.bin(Instruction::SRem, a, d);
    Value *negative = b.cmp(ICmpInst::ICMP_SLT, r, b.i64(0));
    b.ret(b.bin(Instruction::Sub, q,
                b.select(negative, b.i64(1), b.i64(0))));
    return function;
}

Function *buildGCD(Module *module) {
    auto *function = makeFunction(module, "__compiler.sms.gcd",
                                  module->int64_ty_, 2);
    RuntimeBuilder b(module, function);
    auto *entry = b.block("label_entry");
    auto *loop = b.block("label_loop");
    auto *body = b.block("label_body");
    auto *exit = b.block("label_exit");

    b.at(entry);
    Value *a0 = function->arguments_[0];
    Value *negative = b.cmp(ICmpInst::ICMP_SLT, a0, b.i64(0));
    Value *absolute = b.select(negative,
                               b.bin(Instruction::Sub, b.i64(0), a0), a0);
    b.jump(loop);

    b.at(loop);
    auto *a = PhiInst::create_phi(module->int64_ty_, loop);
    auto *d = PhiInst::create_phi(module->int64_ty_, loop);
    loop->add_instruction(a);
    loop->add_instruction(d);
    a->add_phi_pair_operand(absolute, entry);
    d->add_phi_pair_operand(function->arguments_[1], entry);
    b.branch(b.cmp(ICmpInst::ICMP_NE, d, b.i64(0)), body, exit);

    b.at(body);
    Value *next = b.bin(Instruction::SRem, a, d);
    b.jump(loop);
    a->add_phi_pair_operand(d, body);
    d->add_phi_pair_operand(next, body);

    b.at(exit);
    b.ret(a);
    return function;
}

Function *buildFloorSum(Module *module, Function *floorDiv) {
    auto *function = makeFunction(module, "__compiler.sms.floor_sum",
                                  module->int64_ty_, 4);
    RuntimeBuilder b(module, function);
    auto *entry = b.block("label_entry");
    auto *loop = b.block("label_loop");
    auto *reduceA = b.block("label_reduce_a");
    auto *afterA = b.block("label_after_a");
    auto *reduceB = b.block("label_reduce_b");
    auto *afterB = b.block("label_after_b");
    auto *rotate = b.block("label_rotate");
    auto *exit = b.block("label_exit");

    b.at(entry);
    Value *n0 = function->arguments_[0];
    Value *m0 = function->arguments_[1];
    Value *a0 = function->arguments_[2];
    Value *b0 = function->arguments_[3];
    Value *qa = b.call(floorDiv, {a0, m0});
    Value *qb = b.call(floorDiv, {b0, m0});
    Value *aNorm = b.bin(Instruction::Sub, a0,
                         b.bin(Instruction::Mul, qa, m0));
    Value *bNorm = b.bin(Instruction::Sub, b0,
                         b.bin(Instruction::Mul, qb, m0));
    Value *pairs = b.bin(
        Instruction::SDiv,
        b.bin(Instruction::Mul, n0,
              b.bin(Instruction::Sub, n0, b.i64(1))),
        b.i64(2));
    Value *initialAnswer = b.bin(
        Instruction::Add, b.bin(Instruction::Mul, qa, pairs),
        b.bin(Instruction::Mul, qb, n0));
    b.jump(loop);

    b.at(loop);
    auto *n = PhiInst::create_phi(module->int64_ty_, loop);
    auto *m = PhiInst::create_phi(module->int64_ty_, loop);
    auto *a = PhiInst::create_phi(module->int64_ty_, loop);
    auto *offset = PhiInst::create_phi(module->int64_ty_, loop);
    auto *answer = PhiInst::create_phi(module->int64_ty_, loop);
    for (auto *phi : {n, m, a, offset, answer}) loop->add_instruction(phi);
    n->add_phi_pair_operand(n0, entry);
    m->add_phi_pair_operand(m0, entry);
    a->add_phi_pair_operand(aNorm, entry);
    offset->add_phi_pair_operand(bNorm, entry);
    answer->add_phi_pair_operand(initialAnswer, entry);
    b.branch(b.cmp(ICmpInst::ICMP_SGE, a, m), reduceA, afterA);

    b.at(reduceA);
    Value *aQuotient = b.bin(Instruction::SDiv, a, m);
    Value *aAdd = b.bin(
        Instruction::SDiv,
        b.bin(Instruction::Mul,
              b.bin(Instruction::Mul,
                    b.bin(Instruction::Sub, n, b.i64(1)), n),
              aQuotient),
        b.i64(2));
    Value *answerAfterA = b.bin(Instruction::Add, answer, aAdd);
    Value *aAfter = b.bin(Instruction::SRem, a, m);
    b.jump(afterA);

    b.at(afterA);
    auto *aReduced = PhiInst::create_phi(module->int64_ty_, afterA);
    auto *answerA = PhiInst::create_phi(module->int64_ty_, afterA);
    afterA->add_instruction(aReduced);
    afterA->add_instruction(answerA);
    aReduced->add_phi_pair_operand(a, loop);
    aReduced->add_phi_pair_operand(aAfter, reduceA);
    answerA->add_phi_pair_operand(answer, loop);
    answerA->add_phi_pair_operand(answerAfterA, reduceA);
    b.branch(b.cmp(ICmpInst::ICMP_SGE, offset, m), reduceB, afterB);

    b.at(reduceB);
    Value *bQuotient = b.bin(Instruction::SDiv, offset, m);
    Value *answerAfterB = b.bin(
        Instruction::Add, answerA,
        b.bin(Instruction::Mul, n, bQuotient));
    Value *bAfter = b.bin(Instruction::SRem, offset, m);
    b.jump(afterB);

    b.at(afterB);
    auto *bReduced = PhiInst::create_phi(module->int64_ty_, afterB);
    auto *answerB = PhiInst::create_phi(module->int64_ty_, afterB);
    afterB->add_instruction(bReduced);
    afterB->add_instruction(answerB);
    bReduced->add_phi_pair_operand(offset, afterA);
    bReduced->add_phi_pair_operand(bAfter, reduceB);
    answerB->add_phi_pair_operand(answerA, afterA);
    answerB->add_phi_pair_operand(answerAfterB, reduceB);
    Value *y = b.bin(Instruction::Add,
                     b.bin(Instruction::Mul, aReduced, n), bReduced);
    b.branch(b.cmp(ICmpInst::ICMP_SLT, y, m), exit, rotate);

    b.at(rotate);
    Value *nextN = b.bin(Instruction::SDiv, y, m);
    Value *nextB = b.bin(Instruction::SRem, y, m);
    b.jump(loop);
    n->add_phi_pair_operand(nextN, rotate);
    m->add_phi_pair_operand(aReduced, rotate);
    a->add_phi_pair_operand(m, rotate);
    offset->add_phi_pair_operand(nextB, rotate);
    answer->add_phi_pair_operand(answerB, rotate);

    b.at(exit);
    b.ret(answerB);
    return function;
}

Function *buildSumNonnegative(Module *module, Function *floorSum) {
    auto *function = makeFunction(module, "__compiler.sms.sum_nonnegative",
                                  module->int64_ty_, 4);
    RuntimeBuilder b(module, function);
    auto *entry = b.block("label_entry");
    auto *reverse = b.block("label_reverse");
    auto *merge = b.block("label_merge");

    Value *start0 = function->arguments_[0];
    Value *step0 = function->arguments_[1];
    Value *count = function->arguments_[2];
    Value *modulus = function->arguments_[3];
    b.at(entry);
    b.branch(b.cmp(ICmpInst::ICMP_SLT, step0, b.i64(0)), reverse, merge);

    b.at(reverse);
    Value *reversedStart = b.bin(
        Instruction::Add, start0,
        b.bin(Instruction::Mul, step0,
              b.bin(Instruction::Sub, count, b.i64(1))));
    Value *positiveStep = b.bin(Instruction::Sub, b.i64(0), step0);
    b.jump(merge);

    b.at(merge);
    auto *start = PhiInst::create_phi(module->int64_ty_, merge);
    auto *step = PhiInst::create_phi(module->int64_ty_, merge);
    merge->add_instruction(start);
    merge->add_instruction(step);
    start->add_phi_pair_operand(start0, entry);
    start->add_phi_pair_operand(reversedStart, reverse);
    step->add_phi_pair_operand(step0, entry);
    step->add_phi_pair_operand(positiveStep, reverse);
    Value *pairs = b.bin(
        Instruction::SDiv,
        b.bin(Instruction::Mul, count,
              b.bin(Instruction::Sub, count, b.i64(1))),
        b.i64(2));
    Value *linear = b.bin(
        Instruction::Add, b.bin(Instruction::Mul, count, start),
        b.bin(Instruction::Mul, step, pairs));
    Value *floors = b.call(floorSum, {count, modulus, step, start});
    b.ret(b.bin(Instruction::Sub, linear,
                b.bin(Instruction::Mul, modulus, floors)));
    return function;
}

Function *buildSumSigned(Module *module, Function *sumNonnegative) {
    auto *function = makeFunction(module, "__compiler.sms.sum_signed",
                                  module->int64_ty_, 4);
    RuntimeBuilder b(module, function);
    auto *entry = b.block("label_entry");
    auto *checkLastPositive = b.block("label_check_last_positive");
    auto *positive = b.block("label_positive");
    auto *checkStartNegative = b.block("label_check_start_negative");
    auto *checkLastNegative = b.block("label_check_last_negative");
    auto *negative = b.block("label_negative");
    auto *zero = b.block("label_zero");

    Value *start = function->arguments_[0];
    Value *step = function->arguments_[1];
    Value *count = function->arguments_[2];
    Value *modulus = function->arguments_[3];
    b.at(entry);
    Value *last = b.bin(
        Instruction::Add, start,
        b.bin(Instruction::Mul, step,
              b.bin(Instruction::Sub, count, b.i64(1))));
    b.branch(b.cmp(ICmpInst::ICMP_SGE, start, b.i64(0)),
             checkLastPositive, checkStartNegative);

    b.at(checkLastPositive);
    b.branch(b.cmp(ICmpInst::ICMP_SGE, last, b.i64(0)), positive,
             checkStartNegative);
    b.at(positive);
    b.ret(b.call(sumNonnegative, {start, step, count, modulus}));

    b.at(checkStartNegative);
    b.branch(b.cmp(ICmpInst::ICMP_SLE, start, b.i64(0)),
             checkLastNegative, zero);
    b.at(checkLastNegative);
    b.branch(b.cmp(ICmpInst::ICMP_SLE, last, b.i64(0)), negative, zero);
    b.at(negative);
    Value *negStart = b.bin(Instruction::Sub, b.i64(0), start);
    Value *negStep = b.bin(Instruction::Sub, b.i64(0), step);
    Value *magnitude = b.call(sumNonnegative,
                              {negStart, negStep, count, modulus});
    b.ret(b.bin(Instruction::Sub, b.i64(0), magnitude));

    b.at(zero);
    b.ret(b.i64(0));
    return function;
}

Function *buildKey(Module *module, Function *floorDiv) {
    auto *function = makeFunction(module, "__compiler.sms.key",
                                  module->int64_ty_, 2);
    RuntimeBuilder b(module, function);
    auto *entry = b.block("label_entry");
    auto *half = b.block("label_half");
    auto *wrap = b.block("label_wrap");
    Value *value = function->arguments_[0];
    Value *mode = function->arguments_[1];
    b.at(entry);
    b.branch(b.cmp(ICmpInst::ICMP_EQ, mode, b.i64(0)), half, wrap);
    b.at(half);
    b.ret(b.call(floorDiv, {value, b.i64(1LL << 31)}));
    b.at(wrap);
    b.ret(b.call(floorDiv,
                 {b.bin(Instruction::Add, value, b.i64(1LL << 31)),
                  b.i64(1LL << 32)}));
    return function;
}

Function *buildEndOfKeyRun(Module *module, Function *key) {
    auto *function = makeFunction(module, "__compiler.sms.end_key_run",
                                  module->int64_ty_, 5);
    RuntimeBuilder b(module, function);
    auto *entry = b.block("label_entry");
    auto *loop = b.block("label_loop");
    auto *body = b.block("label_body");
    auto *raiseLow = b.block("label_raise_low");
    auto *lowerHigh = b.block("label_lower_high");
    auto *latch = b.block("label_latch");
    auto *exit = b.block("label_exit");

    Value *start = function->arguments_[0];
    Value *step = function->arguments_[1];
    Value *count = function->arguments_[2];
    Value *first = function->arguments_[3];
    Value *mode = function->arguments_[4];
    b.at(entry);
    Value *wantedValue = b.bin(
        Instruction::Add, start, b.bin(Instruction::Mul, step, first));
    Value *wanted = b.call(key, {wantedValue, mode});
    Value *initialLow = b.bin(Instruction::Add, first, b.i64(1));
    b.jump(loop);

    b.at(loop);
    auto *low = PhiInst::create_phi(module->int64_ty_, loop);
    auto *high = PhiInst::create_phi(module->int64_ty_, loop);
    loop->add_instruction(low);
    loop->add_instruction(high);
    low->add_phi_pair_operand(initialLow, entry);
    high->add_phi_pair_operand(count, entry);
    b.branch(b.cmp(ICmpInst::ICMP_SLT, low, high), body, exit);

    b.at(body);
    Value *middle = b.bin(
        Instruction::Add, low,
        b.bin(Instruction::SDiv,
              b.bin(Instruction::Sub, high, low), b.i64(2)));
    Value *middleValue = b.bin(
        Instruction::Add, start, b.bin(Instruction::Mul, step, middle));
    Value *middleKey = b.call(key, {middleValue, mode});
    b.branch(b.cmp(ICmpInst::ICMP_EQ, middleKey, wanted),
             raiseLow, lowerHigh);

    b.at(raiseLow);
    Value *nextLow = b.bin(Instruction::Add, middle, b.i64(1));
    b.jump(latch);
    b.at(lowerHigh);
    b.jump(latch);

    b.at(latch);
    auto *mergedLow = PhiInst::create_phi(module->int64_ty_, latch);
    auto *mergedHigh = PhiInst::create_phi(module->int64_ty_, latch);
    latch->add_instruction(mergedLow);
    latch->add_instruction(mergedHigh);
    mergedLow->add_phi_pair_operand(nextLow, raiseLow);
    mergedLow->add_phi_pair_operand(low, lowerHigh);
    mergedHigh->add_phi_pair_operand(high, raiseLow);
    mergedHigh->add_phi_pair_operand(middle, lowerHigh);
    b.jump(loop);
    low->add_phi_pair_operand(mergedLow, latch);
    high->add_phi_pair_operand(mergedHigh, latch);

    b.at(exit);
    b.ret(low);
    return function;
}

Function *buildSumMonotone(Module *module, Function *gcd,
                           Function *endKey, Function *sumSigned) {
    auto *function = makeFunction(module, "__compiler.sms.sum_monotone",
                                  module->int64_ty_, 9);
    RuntimeBuilder b(module, function);
    auto *entry = b.block("label_entry");
    auto *outerCond = b.block("label_outer_cond");
    auto *outerBody = b.block("label_outer_body");
    auto *phaseCond = b.block("label_phase_cond");
    auto *phaseBody = b.block("label_phase_body");
    auto *bCond = b.block("label_b_cond");
    auto *bBody = b.block("label_b_body");
    auto *zCond = b.block("label_z_cond");
    auto *zBody = b.block("label_z_body");
    auto *signCond = b.block("label_sign_cond");
    auto *signBody = b.block("label_sign_body");
    auto *searchCond = b.block("label_search_cond");
    auto *searchBody = b.block("label_search_body");
    auto *searchRaise = b.block("label_search_raise");
    auto *searchLower = b.block("label_search_lower");
    auto *searchLatch = b.block("label_search_latch");
    auto *searchExit = b.block("label_search_exit");
    auto *signExit = b.block("label_sign_exit");
    auto *zExit = b.block("label_z_exit");
    auto *bExit = b.block("label_b_exit");
    auto *phaseExit = b.block("label_phase_exit");
    auto *outerExit = b.block("label_outer_exit");

    Value *u0 = function->arguments_[0];
    Value *du = function->arguments_[1];
    Value *count = function->arguments_[2];
    Value *linearMultiplier = function->arguments_[3];
    Value *multiplier = function->arguments_[4];
    Value *divisor = function->arguments_[5];
    Value *quotientMultiplier = function->arguments_[6];
    Value *rawConstant = function->arguments_[7];
    Value *innerModulus = function->arguments_[8];

    b.at(entry);
    Value *answerSlot = b.slot(module->int64_ty_);
    Value *outerSlot = b.slot(module->int64_ty_);
    Value *phaseSlot = b.slot(module->int64_ty_);
    Value *bRunSlot = b.slot(module->int64_ty_);
    Value *zRunSlot = b.slot(module->int64_ty_);
    Value *signRunSlot = b.slot(module->int64_ty_);
    b.store(b.i64(0), answerSlot);
    b.store(b.i64(0), outerSlot);
    b.jump(outerCond);

    b.at(outerCond);
    Value *outer = b.load(outerSlot);
    b.branch(b.cmp(ICmpInst::ICMP_SLT, outer, count), outerBody, outerExit);

    b.at(outerBody);
    Value *multipliedStart = b.bin(Instruction::Mul, multiplier, u0);
    Value *multipliedStep = b.bin(Instruction::Mul, multiplier, du);
    Value *outerEnd = b.call(endKey,
                             {multipliedStart, multipliedStep, count,
                              outer, b.i64(0)});
    Value *segmentCount = b.bin(Instruction::Sub, outerEnd, outer);
    Value *segmentU = b.bin(
        Instruction::Add, u0, b.bin(Instruction::Mul, du, outer));
    Value *dy = multipliedStep;
    Value *gcdValue = b.call(gcd, {dy, divisor});
    Value *period = b.bin(Instruction::SDiv, divisor, gcdValue);
    Value *periodSmaller = b.cmp(ICmpInst::ICMP_SLT, period, segmentCount);
    Value *phases = b.select(periodSmaller, period, segmentCount);
    b.store(b.i64(0), phaseSlot);
    b.jump(phaseCond);

    b.at(phaseCond);
    Value *phase = b.load(phaseSlot);
    b.branch(b.cmp(ICmpInst::ICMP_SLT, phase, phases), phaseBody, phaseExit);

    b.at(phaseBody);
    Value *phaseCount = b.bin(
        Instruction::Add,
        b.bin(Instruction::SDiv,
              b.bin(Instruction::Sub,
                    b.bin(Instruction::Sub, segmentCount, b.i64(1)), phase),
              period),
        b.i64(1));
    Value *phaseU = b.bin(
        Instruction::Add, segmentU, b.bin(Instruction::Mul, du, phase));
    Value *phaseY = b.i32Value(b.bin(Instruction::Mul, multiplier, phaseU));
    Value *quotient = b.bin(Instruction::SDiv, phaseY, divisor);
    Value *nextPhaseY = b.bin(
        Instruction::Add, phaseY,
        b.bin(Instruction::Mul, dy, period));
    Value *quotientStep = b.bin(
        Instruction::Sub, b.bin(Instruction::SDiv, nextPhaseY, divisor),
        quotient);
    b.store(b.i64(0), bRunSlot);
    b.jump(bCond);

    b.at(bCond);
    Value *bRun = b.load(bRunSlot);
    b.branch(b.cmp(ICmpInst::ICMP_SLT, bRun, phaseCount), bBody, bExit);

    b.at(bBody);
    Value *scaledQuotient = b.bin(Instruction::Mul,
                                  quotientMultiplier, quotient);
    Value *scaledQuotientStep = b.bin(Instruction::Mul,
                                      quotientMultiplier, quotientStep);
    Value *bEnd = b.call(endKey,
        {scaledQuotient, scaledQuotientStep, phaseCount, bRun, b.i64(1)});
    Value *bCount = b.bin(Instruction::Sub, bEnd, bRun);
    Value *runU = b.bin(
        Instruction::Add, phaseU,
        b.bin(Instruction::Mul,
              b.bin(Instruction::Mul, du, period), bRun));
    Value *runQ = b.bin(
        Instruction::Add, quotient,
        b.bin(Instruction::Mul, quotientStep, bRun));
    Value *wrappedB = b.i32Value(
        b.bin(Instruction::Mul, quotientMultiplier, runQ));
    Value *rawZ = b.bin(
        Instruction::Add,
        b.bin(Instruction::Add,
              b.bin(Instruction::Mul, linearMultiplier, runU), wrappedB),
        rawConstant);
    Value *dz = b.bin(
        Instruction::Add,
        b.bin(Instruction::Mul, linearMultiplier,
              b.bin(Instruction::Mul, du, period)),
        b.bin(Instruction::Mul, quotientMultiplier, quotientStep));
    b.store(b.i64(0), zRunSlot);
    b.jump(zCond);

    b.at(zCond);
    Value *zRun = b.load(zRunSlot);
    b.branch(b.cmp(ICmpInst::ICMP_SLT, zRun, bCount), zBody, zExit);

    b.at(zBody);
    Value *zEnd = b.call(endKey, {rawZ, dz, bCount, zRun, b.i64(1)});
    Value *zCount = b.bin(Instruction::Sub, zEnd, zRun);
    Value *z0 = b.i32Value(b.bin(
        Instruction::Add, rawZ, b.bin(Instruction::Mul, dz, zRun)));
    b.store(b.i64(0), signRunSlot);
    b.jump(signCond);

    b.at(signCond);
    Value *signRun = b.load(signRunSlot);
    b.branch(b.cmp(ICmpInst::ICMP_SLT, signRun, zCount),
             signBody, signExit);

    b.at(signBody);
    Value *signValue = b.bin(
        Instruction::Add, z0, b.bin(Instruction::Mul, dz, signRun));
    Value *sign = b.cmp(ICmpInst::ICMP_SLT, signValue, b.i64(0));
    Value *initialLow = b.bin(Instruction::Add, signRun, b.i64(1));
    b.jump(searchCond);

    b.at(searchCond);
    auto *low = PhiInst::create_phi(module->int64_ty_, searchCond);
    auto *high = PhiInst::create_phi(module->int64_ty_, searchCond);
    searchCond->add_instruction(low);
    searchCond->add_instruction(high);
    low->add_phi_pair_operand(initialLow, signBody);
    high->add_phi_pair_operand(zCount, signBody);
    b.branch(b.cmp(ICmpInst::ICMP_SLT, low, high), searchBody, searchExit);

    b.at(searchBody);
    Value *middle = b.bin(
        Instruction::Add, low,
        b.bin(Instruction::SDiv,
              b.bin(Instruction::Sub, high, low), b.i64(2)));
    Value *middleValue = b.bin(
        Instruction::Add, z0, b.bin(Instruction::Mul, dz, middle));
    Value *middleSign = b.cmp(ICmpInst::ICMP_SLT, middleValue, b.i64(0));
    b.branch(b.cmp(ICmpInst::ICMP_EQ, middleSign, sign),
             searchRaise, searchLower);

    b.at(searchRaise);
    Value *raised = b.bin(Instruction::Add, middle, b.i64(1));
    b.jump(searchLatch);
    b.at(searchLower);
    b.jump(searchLatch);
    b.at(searchLatch);
    auto *nextLow = PhiInst::create_phi(module->int64_ty_, searchLatch);
    auto *nextHigh = PhiInst::create_phi(module->int64_ty_, searchLatch);
    searchLatch->add_instruction(nextLow);
    searchLatch->add_instruction(nextHigh);
    nextLow->add_phi_pair_operand(raised, searchRaise);
    nextLow->add_phi_pair_operand(low, searchLower);
    nextHigh->add_phi_pair_operand(high, searchRaise);
    nextHigh->add_phi_pair_operand(middle, searchLower);
    b.jump(searchCond);
    low->add_phi_pair_operand(nextLow, searchLatch);
    high->add_phi_pair_operand(nextHigh, searchLatch);

    b.at(searchExit);
    Value *runStart = b.bin(
        Instruction::Add, z0, b.bin(Instruction::Mul, dz, signRun));
    Value *runCount = b.bin(Instruction::Sub, low, signRun);
    Value *runSum = b.call(sumSigned,
                           {runStart, dz, runCount, innerModulus});
    Value *answer = b.load(answerSlot);
    b.store(b.bin(Instruction::Add, answer, runSum), answerSlot);
    b.store(low, signRunSlot);
    b.jump(signCond);

    b.at(signExit);
    b.store(zEnd, zRunSlot);
    b.jump(zCond);
    b.at(zExit);
    b.store(bEnd, bRunSlot);
    b.jump(bCond);
    b.at(bExit);
    b.store(b.bin(Instruction::Add, phase, b.i64(1)), phaseSlot);
    b.jump(phaseCond);
    b.at(phaseExit);
    b.store(outerEnd, outerSlot);
    b.jump(outerCond);

    b.at(outerExit);
    b.ret(b.load(answerSlot));
    return function;
}

void buildEntry(Module *module, Function *function, Function *sumMonotone) {
    RuntimeBuilder b(module, function);
    auto *entry = b.block("label_entry");
    auto *closedCond = b.block("label_closed_cond");
    auto *closedBody = b.block("label_closed_body");
    auto *splitCond = b.block("label_split_cond");
    auto *splitBody = b.block("label_split_body");
    auto *splitRaise = b.block("label_split_raise");
    auto *splitLower = b.block("label_split_lower");
    auto *splitLatch = b.block("label_split_latch");
    auto *splitExit = b.block("label_split_exit");
    auto *sumLeft = b.block("label_sum_left");
    auto *sumRight = b.block("label_sum_right");
    auto *sumMerge = b.block("label_sum_merge");
    auto *closedExit = b.block("label_closed_exit");
    auto *tailCond = b.block("label_tail_cond");
    auto *tailBody = b.block("label_tail_body");
    auto *tailExit = b.block("label_tail_exit");

    b.at(entry);
    std::vector<Value *> a;
    for (auto *argument : function->arguments_) a.push_back(argument);
    Value *s = b.sext(a[0]);
    Value *t = b.sext(a[1]);
    Value *d = b.sext(a[2]);
    Value *selectionEnabled = a[3];
    Value *lhsMultiplier64 = b.sext(a[4]);
    Value *lhsConstant64 = b.sext(a[5]);
    Value *rhsMultiplier64 = b.sext(a[6]);
    Value *rhsConstant64 = b.sext(a[7]);
    Value *trueUsesRight = a[8];
    Value *linearMultiplier64 = b.sext(a[9]);
    Value *multiplier64 = b.sext(a[10]);
    Value *divisor64 = b.sext(a[11]);
    Value *quotientMultiplier64 = b.sext(a[12]);
    Value *rawConstant64 = b.sext(a[13]);
    Value *innerModulus64 = b.sext(a[14]);
    Value *additive64 = b.sext(a[15]);
    Value *outerModulus64 = b.sext(a[16]);
    Value *initial64 = b.sext(a[17]);
    Value *selectionActive =
        b.cmp(ICmpInst::ICMP_NE, selectionEnabled, b.i32(0));
    Value *selectRight =
        b.cmp(ICmpInst::ICMP_NE, trueUsesRight, b.i32(0));
    Value *trueMultiplier = b.select(
        selectRight, rhsMultiplier64, lhsMultiplier64);
    Value *trueConstant = b.select(
        selectRight, rhsConstant64, lhsConstant64);
    Value *falseMultiplier = b.select(
        selectRight, lhsMultiplier64, rhsMultiplier64);
    Value *falseConstant = b.select(
        selectRight, lhsConstant64, rhsConstant64);
    Value *count = b.bin(
        Instruction::SDiv,
        b.bin(Instruction::Sub,
              b.bin(Instruction::Add,
                    b.bin(Instruction::Sub, t, s), d), b.i64(1)),
        d);
    Value *large = b.cmp(ICmpInst::ICMP_SGT, count, b.i64(4096));
    Value *closedCount = b.select(
        large, b.bin(Instruction::Sub, count, b.i64(4096)), b.i64(0));
    Value *answerSlot = b.slot(module->int64_ty_);
    Value *firstSlot = b.slot(module->int64_ty_);
    b.store(b.i64(0), answerSlot);
    b.store(b.i64(0), firstSlot);
    b.jump(closedCond);

    b.at(closedCond);
    Value *first = b.load(firstSlot);
    b.branch(b.cmp(ICmpInst::ICMP_SLT, first, closedCount),
             closedBody, closedExit);

    b.at(closedBody);
    Value *x = b.bin(Instruction::Add, s,
                     b.bin(Instruction::Mul, d, first));
    Value *lhs = b.bin(
        Instruction::Add, b.bin(Instruction::Mul, lhsMultiplier64, x),
        lhsConstant64);
    Value *rhs = b.bin(
        Instruction::Add, b.bin(Instruction::Mul, rhsMultiplier64, x),
        rhsConstant64);
    Value *left = b.bin(
        Instruction::And,
        selectionActive, b.cmp(ICmpInst::ICMP_SLT, lhs, rhs));
    Value *initialLow = b.bin(Instruction::Add, first, b.i64(1));
    b.jump(splitCond);

    b.at(splitCond);
    auto *low = PhiInst::create_phi(module->int64_ty_, splitCond);
    auto *high = PhiInst::create_phi(module->int64_ty_, splitCond);
    splitCond->add_instruction(low);
    splitCond->add_instruction(high);
    low->add_phi_pair_operand(initialLow, closedBody);
    high->add_phi_pair_operand(closedCount, closedBody);
    b.branch(b.cmp(ICmpInst::ICMP_SLT, low, high), splitBody, splitExit);

    b.at(splitBody);
    Value *middle = b.bin(
        Instruction::Add, low,
        b.bin(Instruction::SDiv,
              b.bin(Instruction::Sub, high, low), b.i64(2)));
    Value *middleX = b.bin(
        Instruction::Add, s, b.bin(Instruction::Mul, d, middle));
    Value *middleLhs = b.bin(
        Instruction::Add,
        b.bin(Instruction::Mul, lhsMultiplier64, middleX), lhsConstant64);
    Value *middleRhs = b.bin(
        Instruction::Add,
        b.bin(Instruction::Mul, rhsMultiplier64, middleX), rhsConstant64);
    Value *middleLeft = b.bin(
        Instruction::And,
        selectionActive,
        b.cmp(ICmpInst::ICMP_SLT, middleLhs, middleRhs));
    b.branch(b.cmp(ICmpInst::ICMP_EQ, middleLeft, left),
             splitRaise, splitLower);
    b.at(splitRaise);
    Value *raised = b.bin(Instruction::Add, middle, b.i64(1));
    b.jump(splitLatch);
    b.at(splitLower);
    b.jump(splitLatch);
    b.at(splitLatch);
    auto *nextLow = PhiInst::create_phi(module->int64_ty_, splitLatch);
    auto *nextHigh = PhiInst::create_phi(module->int64_ty_, splitLatch);
    splitLatch->add_instruction(nextLow);
    splitLatch->add_instruction(nextHigh);
    nextLow->add_phi_pair_operand(raised, splitRaise);
    nextLow->add_phi_pair_operand(low, splitLower);
    nextHigh->add_phi_pair_operand(high, splitRaise);
    nextHigh->add_phi_pair_operand(middle, splitLower);
    b.jump(splitCond);
    low->add_phi_pair_operand(nextLow, splitLatch);
    high->add_phi_pair_operand(nextHigh, splitLatch);

    b.at(splitExit);
    b.branch(left, sumLeft, sumRight);
    b.at(sumLeft);
    Value *trueU = b.bin(
        Instruction::Add, b.bin(Instruction::Mul, trueMultiplier, x),
        trueConstant);
    Value *leftSum = b.call(
        sumMonotone,
        {trueU, b.bin(Instruction::Mul, trueMultiplier, d),
         b.bin(Instruction::Sub, low, first), linearMultiplier64,
         multiplier64, divisor64, quotientMultiplier64, rawConstant64,
         innerModulus64});
    b.jump(sumMerge);
    b.at(sumRight);
    Value *falseU = b.bin(
        Instruction::Add, b.bin(Instruction::Mul, falseMultiplier, x),
        falseConstant);
    Value *rightSum = b.call(
        sumMonotone,
        {falseU, b.bin(Instruction::Mul, falseMultiplier, d),
         b.bin(Instruction::Sub, low, first), linearMultiplier64, multiplier64,
         divisor64, quotientMultiplier64, rawConstant64, innerModulus64});
    b.jump(sumMerge);
    b.at(sumMerge);
    auto *segmentSum = PhiInst::create_phi(module->int64_ty_, sumMerge);
    sumMerge->add_instruction(segmentSum);
    segmentSum->add_phi_pair_operand(leftSum, sumLeft);
    segmentSum->add_phi_pair_operand(rightSum, sumRight);
    Value *oldAnswer = b.load(answerSlot);
    b.store(b.bin(Instruction::Add, oldAnswer, segmentSum), answerSlot);
    b.store(low, firstSlot);
    b.jump(closedCond);

    b.at(closedExit);
    Value *closedAnswer = b.bin(
        Instruction::Add,
        b.bin(Instruction::Add, b.load(answerSlot),
              b.bin(Instruction::Mul, additive64, closedCount)),
        initial64);
    Value *rawCanonical = b.bin(Instruction::SRem, closedAnswer,
                                outerModulus64);
    Value *canonical = b.select(
        b.cmp(ICmpInst::ICMP_SLT, rawCanonical, b.i64(0)),
        b.bin(Instruction::Add, rawCanonical, outerModulus64), rawCanonical);
    Value *candidateA = b.trunc(canonical);
    Value *candidateB = b.select(
        b.cmp(ICmpInst::ICMP_EQ, canonical, b.i64(0)), b.i32(0),
        b.trunc(b.bin(Instruction::Sub, canonical, outerModulus64)));
    b.jump(tailCond);

    b.at(tailCond);
    auto *index = PhiInst::create_phi(module->int64_ty_, tailCond);
    auto *stateA = PhiInst::create_phi(module->int32_ty_, tailCond);
    auto *stateB = PhiInst::create_phi(module->int32_ty_, tailCond);
    tailCond->add_instruction(index);
    tailCond->add_instruction(stateA);
    tailCond->add_instruction(stateB);
    index->add_phi_pair_operand(closedCount, closedExit);
    stateA->add_phi_pair_operand(candidateA, closedExit);
    stateB->add_phi_pair_operand(candidateB, closedExit);
    b.branch(b.cmp(ICmpInst::ICMP_SLT, index, count), tailBody, tailExit);

    b.at(tailBody);
    Value *tailX = b.trunc(b.bin(
        Instruction::Add, s, b.bin(Instruction::Mul, d, index)));
    Value *tailLhs = b.bin(
        Instruction::Add, b.bin(Instruction::Mul, a[4], tailX), a[5]);
    Value *tailRhs = b.bin(
        Instruction::Add, b.bin(Instruction::Mul, a[6], tailX), a[7]);
    Value *useReflection = b.bin(
        Instruction::And,
        selectionActive, b.cmp(ICmpInst::ICMP_SLT, tailLhs, tailRhs));
    Value *tailTrue = b.select(selectRight, tailRhs, tailLhs);
    Value *tailFalse = b.select(selectRight, tailLhs, tailRhs);
    Value *tailU = b.select(useReflection, tailTrue, tailFalse);
    Value *linear = b.bin(Instruction::Mul, tailU, a[9]);
    Value *product = b.bin(Instruction::Mul, tailU, a[10]);
    Value *quotient = b.bin(Instruction::SDiv, product, a[11]);
    Value *scaled = b.bin(Instruction::Mul, quotient, a[12]);
    Value *z = b.bin(
        Instruction::Add,
        b.bin(Instruction::Add, linear, scaled), a[13]);
    Value *contribution = b.bin(Instruction::SRem, z, a[14]);
    auto advance = [&](Value *state) {
        Value *sum = b.bin(Instruction::Add,
                           b.bin(Instruction::Add, state, contribution), a[15]);
        return static_cast<Value *>(b.bin(Instruction::SRem, sum, a[16]));
    };
    Value *nextA = advance(stateA);
    Value *nextB = advance(stateB);
    Value *nextIndex64 = b.bin(Instruction::Add, index, b.i64(1));
    b.jump(tailCond);
    index->add_phi_pair_operand(nextIndex64, tailBody);
    stateA->add_phi_pair_operand(nextA, tailBody);
    stateB->add_phi_pair_operand(nextB, tailBody);

    b.at(tailExit);
    Value *agreed = b.cmp(ICmpInst::ICMP_EQ, stateA, stateB);
    b.ret(b.select(agreed, stateA, b.i32(INT32_MIN)));
}

} // namespace

void materializeSummableModSumRuntime(Module *module) {
    Function *entry = nullptr;
    for (auto *function : module->function_list_)
        if (function->name_ == "__compiler.summable_mod_sum") {
            entry = function;
            break;
        }
    if (!entry || !entry->is_declaration()) return;

    Function *floorDiv = buildFloorDiv(module);
    Function *gcd = buildGCD(module);
    Function *floorSum = buildFloorSum(module, floorDiv);
    Function *sumNonnegative = buildSumNonnegative(module, floorSum);
    Function *sumSigned = buildSumSigned(module, sumNonnegative);
    Function *key = buildKey(module, floorDiv);
    Function *endKey = buildEndOfKeyRun(module, key);
    Function *sumMonotone = buildSumMonotone(module, gcd, endKey, sumSigned);
    buildEntry(module, entry, sumMonotone);
}
