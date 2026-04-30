#include "../../include/backend/arm_builder.hpp"
#include <cstring>

namespace {
std::string reg32(int idx) { return "w" + std::to_string(idx); }
std::string freg(int idx) { return "s" + std::to_string(idx); }
}

bool ArmBuilder::isFloatTy(Type *ty) { return ty && ty->tid_ == Type::FloatTyID; }
bool ArmBuilder::isIntTy(Type *ty) { return ty && ty->tid_ == Type::IntegerTyID; }
bool ArmBuilder::isPointerLike(Type *ty) { return ty && ty->tid_ == Type::PointerTyID; }
bool ArmBuilder::isImm12(int64_t v) { return v >= 0 && v < 4096; }

std::string ArmBuilder::globalName(Value *v) { return "_" + v->name_; }
std::string ArmBuilder::escapeLabel(const std::string &name) { return name.empty() ? ".Lunnamed" : ".L" + name; }

uint32_t ArmBuilder::floatBits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

std::string ArmBuilder::floatLiteral(float f) {
    std::ostringstream oss;
    oss.setf(std::ios::hex, std::ios::basefield);
    oss << floatBits(f);
    return oss.str();
}

size_t ArmBuilder::typeSize(Type *ty) {
    if (!ty) return 4;
    switch (ty->tid_) {
        case Type::VoidTyID: return 0;
        case Type::IntegerTyID: return static_cast<IntegerType *>(ty)->num_bits_ == 1 ? 1 : 4;
        case Type::FloatTyID: return 4;
        case Type::PointerTyID: return 8;
        case Type::ArrayTyID:
            return static_cast<ArrayType *>(ty)->num_elements_ * typeSize(static_cast<ArrayType *>(ty)->contained_);
        default: return 4;
    }
}

int ArmBuilder::alignUp(int x, int align) const { return (x + align - 1) / align * align; }

int ArmBuilder::allocateSlot(ArmFuncContext &ctx, Value *v, size_t size, size_t align) {
    if (!v) return 0;
    auto it = ctx.slots.find(v);
    if (it != ctx.slots.end()) return it->second.offset;
    ctx.frame_size = alignUp(ctx.frame_size, static_cast<int>(align));
    ctx.slots[v] = ArmFuncContext::StackSlot{ctx.frame_size, size, false};
    ctx.frame_size += static_cast<int>(alignUp(static_cast<int>(std::max<size_t>(size, 4)), static_cast<int>(align)));
    return ctx.slots[v].offset;
}
