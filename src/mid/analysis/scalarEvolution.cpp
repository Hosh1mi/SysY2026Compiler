// 典型示例：
//   源循环：for (int i = 0; i < 10; i += 2) use(i);
//   SCEV：  i 被表示为 {0,+,2}<loop>，精确迭代次数为 5。
// `{起始值,+,步长}<循环>` 描述值随循环迭代的变化规律。后续循环优化可以直接
// 查询这份规律，无需各自重新识别 PHI、回边更新和退出条件。

#include "../../include/mid/analysis/scalarEvolution.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

namespace {

// ScalarEvolution 是证明型分析：表达式过大时放弃推导仍然安全，下游会把
// CouldNotCompute 当成未知值并使用自己的保守逻辑。限制扁平化表达式的操作数
// 数量，可以避免很长的左深加法/乘法链反复复制、排序前缀而产生平方级耗时。
constexpr std::size_t kMaxSCEVNAryOperands = 64;

} // namespace

// 常量节点直接输出十进制数值。
std::string SCEVConstant::print() const {
    return std::to_string(value_);
}

// Unknown 表示分析保留了原 IR 值，但无法继续拆解它的变化规律。
std::string SCEVUnknown::print() const {
    if (!value_) return "unknown(<null>)";
    std::string name = value_->name_.empty() ? "<anon>" : value_->name_;
    return "unknown(" + name + ")";
}

// 按规范化后的操作数顺序输出加法表达式，主要用于调试和 IR 快照。
std::string SCEVAddExpr::print() const {
    std::ostringstream oss;
    oss << "(";
    for (size_t i = 0; i < operands_.size(); i++) {
        if (i) oss << " + ";
        oss << operands_[i]->print();
    }
    oss << ")";
    return oss.str();
}

// 按规范化后的操作数顺序输出乘法表达式。
std::string SCEVMulExpr::print() const {
    std::ostringstream oss;
    oss << "(";
    for (size_t i = 0; i < operands_.size(); i++) {
        if (i) oss << " * ";
        oss << operands_[i]->print();
    }
    oss << ")";
    return oss.str();
}

// 加法递推输出为 `{start,+,step}<header>`，同时标出它属于哪个循环。
std::string SCEVAddRecExpr::print() const {
    std::ostringstream oss;
    oss << "{" << start_->print() << ",+," << step_->print() << "}<";
    oss << (loop_ && loop_->header ? loop_->header->name_ : "?") << ">";
    return oss.str();
}

// CouldNotCompute 明确表示当前分析无法给出可用结论。
std::string SCEVCouldNotCompute::print() const {
    return "<could-not-compute>";
}

// 查询一个 IR Value 的 SCEV。每次顶层查询先清空递归访问集合；已经构造完成的
// 结果仍保留在 value_cache_ 中，可以被后续查询直接复用。
const SCEV *ScalarEvolution::getSCEV(Value *v) {
    visiting_.clear();
    return getSCEVImpl(v);
}

// 预留的作用域查询接口。当前实现尚未把内层递推在外层作用域中求值，因此
// scope 暂不参与计算，返回结果与 getSCEV(v) 相同。
const SCEV *ScalarEvolution::getSCEVAtScope(Value *v, Loop *scope) {
    (void)scope;
    return getSCEV(v);
}

// 返回循环迭代次数的 SCEV。常量次数优先返回常量节点；无法精确求常量时，
// 使用 LoopInfo 已记录的符号 tripCount；两种信息都缺失时返回无法计算。
const SCEV *ScalarEvolution::getTripCount(Loop *loop) {
    if (auto exact = getConstantTripCount(loop)) {
        Type *type = loop && loop->controlInduction.phi
                         ? loop->controlInduction.phi->type_
                         : nullptr;
        return getConstant(type, *exact);
    }
    if (!loop || !loop->hasCanonicalIV() || !loop->tripCount)
        return getCouldNotCompute(nullptr);
    return getSCEV(loop->tripCount);
}

std::optional<long long>
ScalarEvolution::getConstantTripCount(Loop *loop) const {
    // 当前只处理单退出块、单退出边的规范循环。多出口循环需要比较多条路径的
    // 首次退出时刻，当前分析没有建立这类证明。
    if (!loop || loop->exiting.size() != 1 || loop->exits.size() != 1)
        return std::nullopt;

    // LoopInfo 已识别出的归纳变量描述包含 start、bound、step、比较谓词和
    // 比较所在位置。本函数只接受常量步长。
    const InductionDescriptor *induction = loop->getInductionDescriptor();
    if (!induction || !induction->constantStep)
        return std::nullopt;

    // header guard 对应 while/for 的先判断形态；latch guard 对应循环体执行后
    // 再判断的形态。退出块必须正好是这个 guard，公式才覆盖全部退出路径。
    BasicBlock *guard =
        induction->guardPosition == InductionGuardPosition::Header
            ? loop->header
            : loop->singleLatch();
    if (!guard || loop->exiting.front() != guard)
        return std::nullopt;

    // 精确常量次数要求起始值和边界都是整数常量。符号边界由 getTripCount
    // 的后备路径表示成普通 SCEV。
    auto *startConstant = dynamic_cast<ConstantInt *>(induction->start);
    auto *boundConstant = dynamic_cast<ConstantInt *>(induction->bound);
    if (!startConstant || !boundConstant)
        return std::nullopt;

    const long long start = startConstant->value_;
    const long long bound = boundConstant->value_;
    const long long step = *induction->constantStep;
    if (step == 0)
        return std::nullopt;

    // 正步长只支持 `<`/`<=`，负步长只支持 `>`/`>=`。方向不匹配时，
    // 简单除法公式无法证明循环会到达退出条件，直接放弃。
    long long iterations = 0;
    if (step > 0) {
        if (induction->predicate == ICmpInst::ICMP_SLT) {
            if (start < bound)
                // 向上取整：(bound-start)/step。例如 0,2,4,6,8 共 5 次。
                iterations = (bound - start + step - 1) / step;
        } else if (induction->predicate == ICmpInst::ICMP_SLE) {
            if (start <= bound)
                iterations = (bound - start) / step + 1;
        } else {
            return std::nullopt;
        }
    } else {
        const long long magnitude = -step;
        if (induction->predicate == ICmpInst::ICMP_SGT) {
            if (start > bound)
                iterations = (start - bound + magnitude - 1) / magnitude;
        } else if (induction->predicate == ICmpInst::ICMP_SGE) {
            if (start >= bound)
                iterations = (start - bound) / magnitude + 1;
        } else {
            return std::nullopt;
        }
    }

    // latch 在第一次比较前已经执行过一轮循环体，所以最少执行一次。
    if (induction->guardPosition == InductionGuardPosition::Latch)
        iterations = std::max(1LL, iterations);

    // 递推值必须在不发生有符号 i32 回绕的情况下到达第一次比较失败的位置。
    // 使用 __int128 计算 terminal，避免检查过程自身溢出。若终值超出 i32，
    // 上面的数学次数无法精确描述机器整数递推，返回未知。
    const __int128 terminal =
        static_cast<__int128>(start) +
        static_cast<__int128>(iterations) * static_cast<__int128>(step);
    if (terminal < std::numeric_limits<int>::min() ||
        terminal > std::numeric_limits<int>::max())
        return std::nullopt;

    return iterations;
}

// 计算数组各维 stride 时使用的乘法检查。调用处传入的维度和累计 stride
// 都是正数，因此只需检查 long long 正向上界。
static bool multiplyNoOverflow(long long a, long long b, long long &out) {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a > std::numeric_limits<long long>::max() / b)
        return false;
    out = a * b;
    return true;
}

// 将多维 GEP 转换成“基址 + 线性元素偏移”。例如 int a[10][20] 的
// gep a, 0, i, j 会得到 shape={10,20}、elementOffset=i*20+j。
// 偏移以最内层元素为单位，调用方可结合 elementType 计算字节距离。
SCEVGEPInfo ScalarEvolution::getLinearizedGEP(GetElementPtrInst *gep) {
    SCEVGEPInfo info;
    if (!gep || gep->num_ops() < 2)
        return info;

    // 操作数 0 必须是指针，后续所有索引必须是 i32。
    Value *base = gep->get_operand(0);
    auto *ptrTy = dynamic_cast<PointerType *>(base->type_);
    if (!ptrTy)
        return info;

    for (unsigned i = 1; i < gep->num_ops(); i++) {
        auto *idxTy = dynamic_cast<IntegerType *>(gep->get_operand(i)->type_);
        if (!idxTy || idxTy->num_bits_ != 32)
            return info;
    }

    // 沿指针所指类型逐层剥离数组，记录每一维长度，并留下最终元素类型。
    std::vector<long long> shape;
    Type *elementTy = ptrTy->contained_;
    while (auto *arrTy = dynamic_cast<ArrayType *>(elementTy)) {
        shape.push_back(arrTy->num_elements_);
        elementTy = arrTy->contained_;
    }

    Type *offsetTy = gep->get_operand(1)->type_;
    std::vector<const SCEV *> terms;

    if (shape.empty()) {
        // 普通元素指针只允许一个索引，线性偏移就是该索引本身。
        if (gep->num_ops() != 2)
            return info;
        terms.push_back(getSCEV(gep->get_operand(1)));
    } else {
        // 数组对象的第一个 GEP 索引用于选择指针指向的对象。当前线性化仅接受
        // 常见的前导 0，随后索引才依次对应数组维度。
        auto *leading = dynamic_cast<ConstantInt *>(gep->get_operand(1));
        if (!leading || leading->value_ != 0)
            return info;

        unsigned dataIndexCount = gep->num_ops() - 2;
        if (dataIndexCount > shape.size())
            return info;

        for (unsigned idxNo = 0; idxNo < dataIndexCount; idxNo++) {
            // 行主序下，第 idxNo 维的 stride 等于其后所有维度长度的乘积。
            long long stride = 1;
            for (size_t dim = idxNo + 1; dim < shape.size(); dim++) {
                if (!multiplyNoOverflow(stride, shape[dim], stride))
                    return info;
            }

            // 每个下标也转成 SCEV，因此 i、i+1、归纳变量等形式都能保留下来。
            const SCEV *idxS = getSCEV(gep->get_operand(idxNo + 2));
            if (!idxS || idxS->kind() == SCEVKind::CouldNotCompute)
                return info;

            if (stride == 1) {
                terms.push_back(idxS);
            } else {
                terms.push_back(getMulExpr({getConstant(offsetTy, stride), idxS}, offsetTy));
            }
        }
    }

    // 将所有“下标 * stride”项相加。getAddExpr 会继续展平、合并常量并排序。
    const SCEV *offset = nullptr;
    if (terms.empty()) {
        offset = getConstant(offsetTy, 0);
    } else {
        offset = getAddExpr(terms, offsetTy);
    }

    if (!offset || offset->kind() == SCEVKind::CouldNotCompute)
        return info;

    // 所有检查完成后才设置 valid，失败路径返回的其它字段不应被调用方使用。
    info.valid = true;
    info.basePtr = base;
    info.elementType = elementTy;
    info.shape = std::move(shape);
    info.elementOffset = offset;
    return info;
}

// 判断 SCEV 在指定循环的所有迭代中是否保持不变。
// 判断依据来自表达式结构，不依赖 Value 名称：
//   常量恒定；循环外定义的 Unknown 恒定；加法/乘法要求每个操作数恒定；
//   属于当前循环或其子循环的 AddRec 会随迭代变化。
bool ScalarEvolution::isLoopInvariant(const SCEV *s, Loop *loop) const {
    if (!s || !loop) return false;

    switch (s->kind()) {
    case SCEVKind::Constant:
        return true;
    case SCEVKind::CouldNotCompute:
        return false;
    case SCEVKind::Unknown: {
        // 参数、全局量、常量类 Value 没有循环内定义指令；指令值则查询其父块
        // 是否属于 loop。
        auto *unknown = static_cast<const SCEVUnknown*>(s);
        auto *inst = dynamic_cast<Instruction*>(unknown->value());
        return !inst || !loop->isInLoop(inst);
    }
    case SCEVKind::AddExpr:
    case SCEVKind::MulExpr: {
        auto *nary = static_cast<const SCEVNAryExpr*>(s);
        for (auto *op : nary->operands()) {
            if (!isLoopInvariant(op, loop)) return false;
        }
        return true;
    }
    case SCEVKind::AddRecExpr: {
        // 外层循环的递推在内层循环的一次完整执行期间可以视为不变量；当前
        // 循环及其子循环的递推仍然会变化。
        auto *addrec = static_cast<const SCEVAddRecExpr*>(s);
        if (isSameOrDescendantLoop(addrec->loop(), loop)) return false;
        return isLoopInvariant(addrec->start(), loop) &&
               isLoopInvariant(addrec->step(), loop);
    }
    }
    return false;
}

// 在表达式树中寻找属于指定循环的第一个 AddRec。调用方可借此从
// `base + scale*{start,+,step}` 一类组合表达式中取出归纳部分。
const SCEVAddRecExpr *ScalarEvolution::getAddRecForLoop(const SCEV *s, Loop *loop) const {
    if (!s || !loop) return nullptr;
    if (auto *addrec = dynamic_cast<const SCEVAddRecExpr*>(s)) {
        return addrec->loop() == loop ? addrec : nullptr;
    }
    if (auto *nary = dynamic_cast<const SCEVNAryExpr*>(s)) {
        for (auto *op : nary->operands()) {
            if (auto *found = getAddRecForLoop(op, loop)) return found;
        }
    }
    return nullptr;
}

// 清空所有节点、唯一化表和值缓存。SCEV 指针由 nodes_ 持有，调用 clear 后
// 先前返回的 SCEV 指针全部失效。
void ScalarEvolution::clear() {
    nodes_.clear();
    scevOrder_.clear();
    unique_.clear();
    value_cache_.clear();
    visiting_.clear();
}

// 将一个 IR Value 递归翻译成 SCEV。当前直接理解整数常量、整数 add/sub/mul
// 和规范归纳 PHI；其它合法 IR 值保留为 Unknown，非法或递归无法收敛时返回
// CouldNotCompute。
const SCEV *ScalarEvolution::getSCEVImpl(Value *v) {
    if (!v) return getCouldNotCompute(nullptr);

    // 同一个 Value 的分析结果固定，优先复用缓存。
    auto cached = value_cache_.find(v);
    if (cached != value_cache_.end()) return cached->second;

    // PHI 与回边更新会在 use-def 图中形成环。visiting_ 检测当前递归栈上的
    // 重入，阻止普通递归翻译无限循环；规范归纳 PHI 由 tryCreateAddRec 处理。
    if (!visiting_.insert(v).second)
        return getCouldNotCompute(v->type_);

    const SCEV *result = nullptr;

    if (auto *ci = dynamic_cast<ConstantInt*>(v)) {
        result = getConstant(ci->type_, ci->value_);
    } else if (auto *phi = dynamic_cast<PhiInst*>(v)) {
        // 只有满足 `{start,+,step}` 形态的循环头 PHI 才建立 AddRec；普通 PHI
        // 仍作为一个不可拆解的 Unknown 保存。
        result = tryCreateAddRec(phi);
        if (!result) result = getUnknown(v);
    } else if (auto *bin = dynamic_cast<BinaryInst*>(v)) {
        if (bin->type_->tid_ != Type::IntegerTyID) {
            result = getUnknown(v);
        } else if (bin->op_id_ == Instruction::Add) {
            result = getAddExpr({getSCEVImpl(bin->get_operand(0)),
                                 getSCEVImpl(bin->get_operand(1))}, bin->type_);
        } else if (bin->op_id_ == Instruction::Sub) {
            // a-b 统一表示为 a+(-1*b)，让加法规范化逻辑可以复用。
            const SCEV *rhs = getNegative(getSCEVImpl(bin->get_operand(1)), bin->type_);
            result = getAddExpr({getSCEVImpl(bin->get_operand(0)), rhs}, bin->type_);
        } else if (bin->op_id_ == Instruction::Mul) {
            result = getMulExpr({getSCEVImpl(bin->get_operand(0)),
                                 getSCEVImpl(bin->get_operand(1))}, bin->type_);
        } else {
            result = getUnknown(v);
        }
    } else if (dynamic_cast<Constant*>(v) ||
               dynamic_cast<Argument*>(v) ||
               dynamic_cast<GlobalVariable*>(v) ||
               dynamic_cast<Instruction*>(v)) {
        result = getUnknown(v);
    } else {
        result = getCouldNotCompute(v->type_);
    }

    // 结果构造完成后退出当前递归栈，并写入永久缓存。
    visiting_.erase(v);
    value_cache_[v] = result;
    return result;
}

// 尝试识别一阶加法递推：
//   %iv = phi [%start, %preheader], [%next, %latch]
//   %next = add %iv, %step
// 识别成功后得到 `{start,+,step}<loop>`。当前只处理恰好一条循环外入边和
// 一条循环内回边的整数 PHI，并要求 step 对该循环不变。
const SCEV *ScalarEvolution::tryCreateAddRec(PhiInst *phi) {
    if (!phi || phi->type_->tid_ != Type::IntegerTyID) return nullptr;

    // 归纳 PHI 必须位于它所属循环的 header。
    Loop *loop = LI_->getLoopFor(phi->parent_);
    if (!loop || loop->header != phi->parent_) return nullptr;

    Value *outsideVal = nullptr;
    Value *insideVal = nullptr;

    // PHI 操作数按 [value, predecessor] 成对排列。循环外值是 start，循环内
    // 值是每轮回边传回的 update。
    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
        auto *pred = dynamic_cast<BasicBlock*>(phi->get_operand(i + 1));
        if (!pred) return nullptr;

        if (loop->blocks.count(pred)) {
            if (insideVal) return nullptr;
            insideVal = phi->get_operand(i);
        } else {
            if (outsideVal) return nullptr;
            outsideVal = phi->get_operand(i);
        }
    }
    if (!outsideVal || !insideVal) return nullptr;

    // 回边值必须直接由整数 add/sub 产生。
    auto *update = dynamic_cast<BinaryInst*>(insideVal);
    if (!update || update->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *stepVal = nullptr;
    bool negateStep = false;

    if (update->op_id_ == Instruction::Add) {
        // 加法满足交换律，phi 可以位于任一操作数。
        if (update->get_operand(0) == phi) {
            stepVal = update->get_operand(1);
        } else if (update->get_operand(1) == phi) {
            stepVal = update->get_operand(0);
        }
    } else if (update->op_id_ == Instruction::Sub) {
        // 只接受 phi-step。step-phi 不构成固定步长加法递推。
        if (update->get_operand(0) == phi) {
            stepVal = update->get_operand(1);
            negateStep = true;
        }
    }
    if (!stepVal) return nullptr;

    // 将减法步长统一取负，再验证 step 不随当前循环变化。
    const SCEV *step = getSCEVImpl(stepVal);
    if (negateStep) step = getNegative(step, phi->type_);
    if (!isLoopInvariant(step, loop)) return nullptr;

    // start 可以是常量、函数参数或外层循环表达式，只要能形成有效 SCEV。
    const SCEV *start = getSCEVImpl(outsideVal);
    if (start->kind() == SCEVKind::CouldNotCompute ||
        step->kind() == SCEVKind::CouldNotCompute)
        return nullptr;

    return getAddRecExpr(start, step, loop, phi, phi->type_);
}

// 取得唯一的常量节点。key 同时包含类型和数值，避免不同整数类型共享节点。
// unique_ 命中时返回已有指针；新节点统一由 nodes_ 管理生命周期。
const SCEV *ScalarEvolution::getConstant(Type *type, long long value) {
    std::string key = "C:" + keyForPointer(type) + ":" + std::to_string(value);
    auto it = unique_.find(key);
    if (it != unique_.end()) return it->second;

    nodes_.push_back(std::make_unique<SCEVConstant>(type, value));
    const SCEV *s = nodes_.back().get();
    scevOrder_[s] = scevOrder_.size();
    unique_[key] = s;
    return s;
}

// 为无法继续分析的 IR Value 建立唯一 Unknown。相同 Value 始终对应同一节点，
// 因而指针身份可以参与更大表达式的唯一化。
const SCEV *ScalarEvolution::getUnknown(Value *value) {
    std::string key = "U:" + keyForPointer(value);
    auto it = unique_.find(key);
    if (it != unique_.end()) return it->second;

    nodes_.push_back(std::make_unique<SCEVUnknown>(value));
    const SCEV *s = nodes_.back().get();
    scevOrder_[s] = scevOrder_.size();
    unique_[key] = s;
    return s;
}

// 按类型复用 CouldNotCompute 节点。该节点会沿加法、乘法构造向上传播，确保
// 下游不会把缺少证明的表达式当成可计算结果。
const SCEV *ScalarEvolution::getCouldNotCompute(Type *type) {
    std::string key = "X:" + keyForPointer(type);
    auto it = unique_.find(key);
    if (it != unique_.end()) return it->second;

    nodes_.push_back(std::make_unique<SCEVCouldNotCompute>(type));
    const SCEV *s = nodes_.back().get();
    scevOrder_[s] = scevOrder_.size();
    unique_[key] = s;
    return s;
}

// 构造规范化加法表达式。处理顺序如下：
//   1. 递归展开嵌套 AddExpr，把加法树变成一个操作数列表；
//   2. 合并所有常量，删除多余的 0；
//   3. 按节点种类和创建顺序排序，消除交换律带来的排列差异；
//   4. 用规范 key 唯一化节点。
// 因此 a+(b+1)、(1+a)+b 等结构会得到同一种 SCEV 形态。
const SCEV *ScalarEvolution::getAddExpr(std::vector<const SCEV*> operands, Type *type) {
    std::vector<const SCEV*> flat;
    long long constant = 0;

    // 递归 lambda 负责展平表达式并累计常量。超过操作数上限时返回 false，
    // 调用方会生成 CouldNotCompute，避免分析时间失控。
    auto collect = [&](auto &&self, const SCEV *op) -> bool {
        if (!op) return false;
        if (!type) type = op->type();
        if (op->kind() == SCEVKind::CouldNotCompute) return false;
        if (auto *c = dynamic_cast<const SCEVConstant*>(op)) {
            constant += c->value();
        } else if (auto *add = dynamic_cast<const SCEVAddExpr*>(op)) {
            for (auto *nested : add->operands()) {
                if (!self(self, nested)) return false;
            }
        } else {
            if (flat.size() >= kMaxSCEVNAryOperands)
                return false;
            flat.push_back(op);
        }
        return true;
    };

    for (auto *op : operands) {
        if (!collect(collect, op)) return getCouldNotCompute(type);
    }

    // 全部操作数都是常量时，flat 为空，此时仍需放入合并后的常量节点。
    if (constant != 0 || flat.empty())
        flat.push_back(getConstant(type, constant));

    // scevOrder_ 是节点创建时分配的稳定序号。先按 kind、再按序号排序，
    // 使交换后的同组操作数产生完全相同的 key。
    std::sort(flat.begin(), flat.end(),
              [this](const SCEV *a, const SCEV *b) {
                  if (a->kind() != b->kind())
                      return a->kind() < b->kind();
                  return scevOrder_.at(a) < scevOrder_.at(b);
              });

    // 非单项表达式中的加法单位元 0 可以删除。
    std::vector<const SCEV*> cleaned;
    cleaned.reserve(flat.size());
    for (auto *op : flat) {
        auto *c = dynamic_cast<const SCEVConstant*>(op);
        if (c && c->value() == 0 && flat.size() > 1) continue;
        cleaned.push_back(op);
    }

    if (cleaned.empty()) return getConstant(type, 0);
    if (cleaned.size() == 1) return cleaned.front();

    // key 包含结果类型和每个规范操作数的节点编号。
    std::string key = "A:" + keyForPointer(type);
    for (auto *op : cleaned) key += ":" + keyForSCEV(op);
    auto it = unique_.find(key);
    if (it != unique_.end()) return it->second;

    nodes_.push_back(std::make_unique<SCEVAddExpr>(type, cleaned));
    const SCEV *s = nodes_.back().get();
    scevOrder_[s] = scevOrder_.size();
    unique_[key] = s;
    return s;
}

// 构造规范化乘法表达式，流程与 getAddExpr 对称：展平嵌套乘法、合并常量、
// 删除单位元 1、稳定排序并唯一化。任一因子为 0 时直接返回常量 0。
const SCEV *ScalarEvolution::getMulExpr(std::vector<const SCEV*> operands, Type *type) {
    std::vector<const SCEV*> flat;
    long long constant = 1;

    // 收集非乘法叶子，同时把所有常量因子乘到 constant 中。
    auto collect = [&](auto &&self, const SCEV *op) -> bool {
        if (!op) return false;
        if (!type) type = op->type();
        if (op->kind() == SCEVKind::CouldNotCompute) return false;
        if (auto *c = dynamic_cast<const SCEVConstant*>(op)) {
            constant *= c->value();
        } else if (auto *mul = dynamic_cast<const SCEVMulExpr*>(op)) {
            for (auto *nested : mul->operands()) {
                if (!self(self, nested)) return false;
            }
        } else {
            if (flat.size() >= kMaxSCEVNAryOperands)
                return false;
            flat.push_back(op);
        }
        return true;
    };

    for (auto *op : operands) {
        if (!collect(collect, op)) return getCouldNotCompute(type);
    }

    // 零因子吸收整个乘法表达式；无需保留其它操作数。
    if (constant == 0) return getConstant(type, 0);
    if (constant != 1 || flat.empty())
        flat.push_back(getConstant(type, constant));

    std::sort(flat.begin(), flat.end(),
              [this](const SCEV *a, const SCEV *b) {
                  if (a->kind() != b->kind())
                      return a->kind() < b->kind();
                  return scevOrder_.at(a) < scevOrder_.at(b);
              });

    // 非单项表达式中的乘法单位元 1 可以删除。
    std::vector<const SCEV*> cleaned;
    cleaned.reserve(flat.size());
    for (auto *op : flat) {
        auto *c = dynamic_cast<const SCEVConstant*>(op);
        if (c && c->value() == 1 && flat.size() > 1) continue;
        cleaned.push_back(op);
    }

    if (cleaned.empty()) return getConstant(type, 1);
    if (cleaned.size() == 1) return cleaned.front();

    std::string key = "M:" + keyForPointer(type);
    for (auto *op : cleaned) key += ":" + keyForSCEV(op);
    auto it = unique_.find(key);
    if (it != unique_.end()) return it->second;

    nodes_.push_back(std::make_unique<SCEVMulExpr>(type, cleaned));
    const SCEV *s = nodes_.back().get();
    scevOrder_[s] = scevOrder_.size();
    unique_[key] = s;
    return s;
}

// 构造并唯一化 AddRec。key 包含类型、所属循环、来源 PHI、start 和 step，
// 防止不同循环中形状相同的递推被当成同一个演化过程。
const SCEV *ScalarEvolution::getAddRecExpr(const SCEV *start, const SCEV *step,
                                           Loop *loop, PhiInst *phi, Type *type) {
    if (!start || !step || !loop) return getCouldNotCompute(type);

    std::string key = "R:" + keyForPointer(type) + ":" + keyForPointer(loop) +
                      ":" + keyForPointer(phi) +
                      ":" + keyForSCEV(start) + ":" + keyForSCEV(step);
    auto it = unique_.find(key);
    if (it != unique_.end()) return it->second;

    nodes_.push_back(std::make_unique<SCEVAddRecExpr>(type, start, step, loop, phi));
    const SCEV *s = nodes_.back().get();
    scevOrder_[s] = scevOrder_.size();
    unique_[key] = s;
    return s;
}

// 统一用 `-1 * s` 表示取负，使乘法的常量合并和唯一化继续生效。
const SCEV *ScalarEvolution::getNegative(const SCEV *s, Type *type) {
    if (!s) return getCouldNotCompute(type);
    if (!type) type = s->type();
    return getMulExpr({getConstant(type, -1), s}, type);
}

// 沿 parent 链判断 candidate 是否等于 ancestor，或嵌套在 ancestor 内。
bool ScalarEvolution::isSameOrDescendantLoop(Loop *candidate, Loop *ancestor) const {
    for (Loop *cur = candidate; cur; cur = cur->parent) {
        if (cur == ancestor) return true;
    }
    return false;
}

// 检查表达式中是否含有属于 loop 或其子循环的 AddRec。该辅助函数用于判断
// 某个复合表达式是否包含会随目标循环层次变化的递推分量。
bool ScalarEvolution::containsVaryingAddRec(const SCEV *s, Loop *loop) const {
    if (!s || !loop) return false;
    if (auto *addrec = dynamic_cast<const SCEVAddRecExpr*>(s))
        return isSameOrDescendantLoop(addrec->loop(), loop);
    if (auto *nary = dynamic_cast<const SCEVNAryExpr*>(s)) {
        for (auto *op : nary->operands()) {
            if (containsVaryingAddRec(op, loop)) return true;
        }
    }
    return false;
}

// 把对象地址编码进唯一化 key。SCEV 分析期间相关 IR、类型和 Loop 对象地址
// 保持稳定，因此地址可作为对象身份使用。
std::string ScalarEvolution::keyForPointer(const void *ptr) {
    std::ostringstream oss;
    oss << ptr;
    return oss.str();
}

// 用“节点种类 + 创建序号”表示一个 SCEV 子节点。序号比完整递归文本更短，
// 构造大型表达式 key 时也无需反复打印整棵子树。
std::string ScalarEvolution::keyForSCEV(const SCEV *s) const {
    if (!s) return "<null>";
    auto it = scevOrder_.find(s);
    if (it == scevOrder_.end()) return "<unknown-scev>";
    return std::to_string(static_cast<int>(s->kind())) + "#" +
           std::to_string(it->second);
}
