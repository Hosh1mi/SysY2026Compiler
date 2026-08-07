#include "../../include/mid/opt/radixRecurrenceEliminate.hpp"

#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/intrinsics.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace {

struct Match {
    Function *function = nullptr;
    Argument *addend = nullptr;
    Argument *digits = nullptr;
    int modulus = 0;
};

ConstantInt *asConstant(Value *value) {
    return dynamic_cast<ConstantInt *>(value);
}

bool isConstant(Value *value, int expected) {
    auto *constant = asConstant(value);
    return constant && constant->value_ == expected;
}

bool isBinary(Instruction *inst, Instruction::OpID op,
              Value *lhs, Value *rhs, bool commutative = false) {
    if (!inst || inst->op_id_ != op || inst->num_ops_ != 2) return false;
    if (inst->get_operand(0) == lhs && inst->get_operand(1) == rhs) return true;
    return commutative && inst->get_operand(0) == rhs && inst->get_operand(1) == lhs;
}

BinaryInst *findBinaryUser(Value *value, Instruction::OpID op,
                           Value *other = nullptr, bool commutative = false) {
    BinaryInst *result = nullptr;
    for (auto &use : value->use_list_) {
        auto *inst = dynamic_cast<BinaryInst *>(use.val_);
        if (!inst || inst->op_id_ != op) continue;
        if (other && !isBinary(inst, op, value, other, commutative)) continue;
        if (result && result != inst) return nullptr;
        result = inst;
    }
    return result;
}

BinaryInst *findRemainderUser(Value *value, int modulus) {
    BinaryInst *result = nullptr;
    for (auto &use : value->use_list_) {
        auto *inst = dynamic_cast<BinaryInst *>(use.val_);
        if (!inst || inst->op_id_ != Instruction::SRem ||
            inst->get_operand(0) != value ||
            !isConstant(inst->get_operand(1), modulus))
            continue;
        if (result && result != inst) return nullptr;
        result = inst;
    }
    return result;
}

bool matchEq(Value *value, Value *lhs, int rhs) {
    auto *cmp = dynamic_cast<ICmpInst *>(value);
    if (!cmp || cmp->icmp_op_ != ICmpInst::ICMP_EQ) return false;
    return (cmp->get_operand(0) == lhs && isConstant(cmp->get_operand(1), rhs)) ||
           (cmp->get_operand(1) == lhs && isConstant(cmp->get_operand(0), rhs));
}

bool matchCondBranch(BasicBlock *block, Value *lhs, int rhs,
                     BasicBlock *trueTarget, BasicBlock *falseTarget) {
    if (!block) return false;
    auto *branch = dynamic_cast<BranchInst *>(block->get_terminator());
    return branch && branch->num_ops_ == 3 &&
           matchEq(branch->get_operand(0), lhs, rhs) &&
           branch->get_operand(1) == trueTarget &&
           branch->get_operand(2) == falseTarget;
}

bool isUnconditionalTo(BasicBlock *block, BasicBlock *target) {
    if (!block) return false;
    auto *branch = dynamic_cast<BranchInst *>(block->get_terminator());
    return branch && branch->num_ops_ == 1 && branch->get_operand(0) == target;
}

BasicBlock *incomingBlock(PhiInst *phi, Value *value) {
    BasicBlock *result = nullptr;
    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
        if (phi->get_operand(i) != value) continue;
        auto *block = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
        if (!block || result) return nullptr;
        result = block;
    }
    return result;
}

bool hasOnlyPureScalarInstructions(Function *function, CallInst *selfCall) {
    unsigned selfCalls = 0;
    for (auto *block : function->basic_blocks_) {
        for (auto *inst : block->instr_list_) {
            if (auto *call = dynamic_cast<CallInst *>(inst)) {
                auto *callee = dynamic_cast<Function *>(
                    call->get_operand(call->num_ops_ - 1));
                if (call != selfCall || callee != function) return false;
                ++selfCalls;
                continue;
            }
            switch (inst->op_id_) {
            case Instruction::Add:
            case Instruction::SDiv:
            case Instruction::SRem:
            case Instruction::ICmp:
            case Instruction::PHI:
            case Instruction::Br:
            case Instruction::Ret:
                break;
            default:
                return false;
            }
        }
    }
    return selfCalls == 1;
}

bool matchFunction(Function *function, Match &match) {
    auto isI32 = [](Type *type) {
        auto *integer = dynamic_cast<IntegerType *>(type);
        return integer && integer->num_bits_ == 32;
    };
    if (!function || function->is_declaration() ||
        function->arguments_.size() != 2 ||
        !isI32(function->get_return_type()))
        return false;

    auto *arg0 = dynamic_cast<Argument *>(function->arguments_[0]);
    auto *arg1 = dynamic_cast<Argument *>(function->arguments_[1]);
    if (!arg0 || !arg1 || !isI32(arg0->type_) || !isI32(arg1->type_))
        return false;

    CallInst *selfCall = nullptr;
    ReturnInst *ret = nullptr;
    for (auto *block : function->basic_blocks_) {
        for (auto *inst : block->instr_list_) {
            if (auto *call = dynamic_cast<CallInst *>(inst)) {
                auto *callee = dynamic_cast<Function *>(
                    call->get_operand(call->num_ops_ - 1));
                if (callee == function) {
                    if (selfCall) return false;
                    selfCall = call;
                }
            }
            if (auto *candidate = dynamic_cast<ReturnInst *>(inst)) {
                if (ret) return false;
                ret = candidate;
            }
        }
    }
    if (!selfCall || !ret || selfCall->num_ops_ != 3)
        return false;

    Argument *addend = nullptr;
    Argument *digits = nullptr;
    BinaryInst *half = nullptr;
    const std::array<Argument *, 2> args{arg0, arg1};
    for (unsigned digitIndex = 0; digitIndex < 2; ++digitIndex) {
        unsigned addendIndex = 1 - digitIndex;
        auto *candidate = dynamic_cast<BinaryInst *>(
            selfCall->get_operand(digitIndex));
        if (!candidate || candidate->op_id_ != Instruction::SDiv ||
            candidate->get_operand(0) != args[digitIndex] ||
            !isConstant(candidate->get_operand(1), 2) ||
            selfCall->get_operand(addendIndex) != args[addendIndex])
            continue;
        if (half) return false;
        half = candidate;
        digits = args[digitIndex];
        addend = args[addendIndex];
    }
    if (!half || !addend || !digits)
        return false;

    auto *doubled = findBinaryUser(selfCall, Instruction::Add,
                                   selfCall, true);
    if (!doubled) return false;

    BinaryInst *evenRem = nullptr;
    int modulus = 0;
    for (auto &use : doubled->use_list_) {
        auto *candidate = dynamic_cast<BinaryInst *>(use.val_);
        if (!candidate || candidate->op_id_ != Instruction::SRem ||
            candidate->get_operand(0) != doubled)
            continue;
        auto *constant = asConstant(candidate->get_operand(1));
        if (!constant || constant->value_ <= 1 || evenRem) return false;
        evenRem = candidate;
        modulus = constant->value_;
    }
    if (!evenRem) return false;

    auto *oddAdd = findBinaryUser(evenRem, Instruction::Add, addend, true);
    auto *oddRem = oddAdd ? findRemainderUser(oddAdd, modulus) : nullptr;
    if (!oddRem) return false;

    BinaryInst *baseRem = nullptr;
    BinaryInst *parityRem = nullptr;
    for (auto *block : function->basic_blocks_) {
        for (auto *inst : block->instr_list_) {
            auto *binary = dynamic_cast<BinaryInst *>(inst);
            if (!binary || binary->op_id_ != Instruction::SRem) continue;
            if (binary->get_operand(0) == addend &&
                isConstant(binary->get_operand(1), modulus))
                baseRem = binary;
            if (binary->get_operand(0) == digits &&
                isConstant(binary->get_operand(1), 2))
                parityRem = binary;
        }
    }
    if (!baseRem || !parityRem || ret->num_ops_ != 1) return false;

    auto *resultPhi = dynamic_cast<PhiInst *>(ret->get_operand(0));
    if (!resultPhi || resultPhi->num_ops_ != 8) return false;

    Value *zeroValue = nullptr;
    for (unsigned i = 0; i < resultPhi->num_ops_; i += 2) {
        if (isConstant(resultPhi->get_operand(i), 0)) {
            if (zeroValue) return false;
            zeroValue = resultPhi->get_operand(i);
        }
    }
    if (!zeroValue) return false;

    BasicBlock *retBlock = ret->parent_;
    BasicBlock *zeroBlock = incomingBlock(resultPhi, zeroValue);
    BasicBlock *baseBlock = incomingBlock(resultPhi, baseRem);
    BasicBlock *oddBlock = incomingBlock(resultPhi, oddRem);
    BasicBlock *evenBlock = incomingBlock(resultPhi, evenRem);
    if (!zeroBlock || !baseBlock || !oddBlock || !evenBlock ||
        !isUnconditionalTo(zeroBlock, retBlock) ||
        !isUnconditionalTo(baseBlock, retBlock) ||
        !isUnconditionalTo(oddBlock, retBlock) ||
        !isUnconditionalTo(evenBlock, retBlock))
        return false;

    BasicBlock *entry = function->basic_blocks_.front();
    auto *entryBranch = dynamic_cast<BranchInst *>(entry->get_terminator());
    if (!entryBranch || entryBranch->num_ops_ != 3 ||
        !matchEq(entryBranch->get_operand(0), digits, 0) ||
        entryBranch->get_operand(1) != zeroBlock)
        return false;

    auto *oneCheck = dynamic_cast<BasicBlock *>(entryBranch->get_operand(2));
    auto *oneBranch = oneCheck
        ? dynamic_cast<BranchInst *>(oneCheck->get_terminator()) : nullptr;
    if (!oneBranch || oneBranch->num_ops_ != 3 ||
        !matchEq(oneBranch->get_operand(0), digits, 1) ||
        oneBranch->get_operand(1) != baseBlock)
        return false;

    auto *recurBlock = dynamic_cast<BasicBlock *>(oneBranch->get_operand(2));
    if (!recurBlock || selfCall->parent_ != recurBlock ||
        !matchCondBranch(recurBlock, parityRem, 1, oddBlock, evenBlock) ||
        !hasOnlyPureScalarInstructions(function, selfCall))
        return false;

    match = {function, addend, digits, modulus};
    return true;
}

void discardBody(Function *function) {
    std::vector<BasicBlock *> blocks = function->basic_blocks_;
    for (auto blockIt = blocks.rbegin(); blockIt != blocks.rend(); ++blockIt) {
        auto *block = *blockIt;
        std::vector<Instruction *> instructions(block->instr_list_.begin(),
                                                block->instr_list_.end());
        for (auto instIt = instructions.rbegin(); instIt != instructions.rend();
             ++instIt)
            block->delete_instr(*instIt);
        block->pre_bbs_.clear();
        block->succ_bbs_.clear();
    }
    function->basic_blocks_.clear();
}

BinaryInst *binary(Module *module, BasicBlock *block, Instruction::OpID op,
                   Value *lhs, Value *rhs) {
    return new BinaryInst(module->int32_ty_, op, lhs, rhs, block);
}

void rewrite(const Match &match, Module *module) {
    Function *function = match.function;
    Argument *addend = match.addend;
    Argument *digits = match.digits;
    Function *mulMod = getOrInsertMulModIntrinsic(module);
    if (!mulMod)
        return;

    discardBody(function);

    auto *entry = new BasicBlock(module, "label_mulmod_entry", function);
    auto *zero = new BasicBlock(module, "label_mulmod_zero", function);
    auto *fallbackInit =
        new BasicBlock(module, "label_mulmod_fallback_init", function);

    auto constant = [&](int value) -> ConstantInt * {
        return new ConstantInt(module->int32_ty_, value);
    };

    auto buildFallback = [&] {
        auto *loop =
            new BasicBlock(module, "label_mulmod_fallback_loop", function);
        auto *body =
            new BasicBlock(module, "label_mulmod_fallback_body", function);
        auto *odd =
            new BasicBlock(module, "label_mulmod_fallback_odd", function);
        auto *latch =
            new BasicBlock(module, "label_mulmod_fallback_latch", function);
        auto *exit =
            new BasicBlock(module, "label_mulmod_fallback_exit", function);

        auto *initial = binary(module, fallbackInit, Instruction::SRem,
                               addend, constant(match.modulus));
        auto *leadingZeros = new UnaryInst(module->int32_ty_, Instruction::Clz,
                                           digits, fallbackInit);
        auto *topShift = binary(module, fallbackInit, Instruction::Sub,
                                constant(31), leadingZeros);
        auto *topMask = binary(module, fallbackInit, Instruction::Shl,
                               constant(1), topShift);
        auto *initialMask = binary(module, fallbackInit, Instruction::LShr,
                                   topMask, constant(1));
        new BranchInst(loop, fallbackInit);

        auto *current = PhiInst::create_phi(module->int32_ty_, loop);
        auto *mask = PhiInst::create_phi(module->int32_ty_, loop);
        loop->add_instruction(current);
        loop->add_instruction(mask);
        current->add_phi_pair_operand(initial, fallbackInit);
        mask->add_phi_pair_operand(initialMask, fallbackInit);
        auto *hasMore = new ICmpInst(ICmpInst::ICMP_NE, mask,
                                     constant(0), loop);
        new BranchInst(hasMore, body, exit, loop);

        auto *doubled = binary(module, body, Instruction::Add,
                               current, current);
        auto *evenResult = binary(module, body, Instruction::SRem,
                                  doubled, constant(match.modulus));
        auto *selectedBit = binary(module, body, Instruction::And,
                                   digits, mask);
        auto *isOdd = new ICmpInst(ICmpInst::ICMP_NE, selectedBit,
                                   constant(0), body);
        new BranchInst(isOdd, odd, latch, body);

        auto *withAddend = binary(module, odd, Instruction::Add,
                                  evenResult, addend);
        auto *oddResult = binary(module, odd, Instruction::SRem,
                                 withAddend, constant(match.modulus));
        new BranchInst(latch, odd);

        auto *next = PhiInst::create_phi(module->int32_ty_, latch);
        latch->add_instruction(next);
        next->add_phi_pair_operand(evenResult, body);
        next->add_phi_pair_operand(oddResult, odd);
        auto *nextMask = binary(module, latch, Instruction::LShr,
                                mask, constant(1));
        new BranchInst(loop, latch);
        current->add_phi_pair_operand(next, latch);
        mask->add_phi_pair_operand(nextMask, latch);

        new ReturnInst(current, exit);
    };

    auto *positive = new ICmpInst(ICmpInst::ICMP_SGT, digits,
                                  constant(0), entry);

    if (match.modulus <= (1 << 30)) {
        // For |addend| < M, every remainder has the addend's sign and
        // magnitude below M.  Doubling it or adding the addend therefore
        // stays strictly between -2M and 2M, which fits i32 at this bound.
        auto *rangeCheck =
            new BasicBlock(module, "label_mulmod_range_check", function);
        auto *fast = new BasicBlock(module, "label_mulmod_fast", function);

        new BranchInst(positive, rangeCheck, zero, entry);
        // Translate [-M + 1, M - 1] to [0, 2M - 2].  Since M <= 2^30,
        // the unsigned comparison also rejects values that wrap below zero.
        auto *biased = binary(module, rangeCheck, Instruction::Add, addend,
                              constant(match.modulus - 1));
        auto *inRange = new ICmpInst(ICmpInst::ICMP_ULT, biased,
                                     constant(2 * match.modulus - 1),
                                     rangeCheck);
        new BranchInst(inRange, fast, fallbackInit, rangeCheck);

        auto *result = new CallInst(
            mulMod, {addend, digits, constant(match.modulus)}, fast);
        new ReturnInst(result, fast);
    } else {
        new BranchInst(positive, fallbackInit, zero, entry);
    }

    new ReturnInst(constant(0), zero);
    buildFallback();
}

} // namespace

void RadixRecurrenceEliminate::execute(Module *module) {
    std::vector<Match> matches;
    for (auto *function : module->function_list_) {
        Match match;
        if (matchFunction(function, match)) matches.push_back(match);
    }
    for (const Match &match : matches) rewrite(match, module);
}
