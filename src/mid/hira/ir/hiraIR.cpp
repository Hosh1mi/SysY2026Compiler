#include "../../../include/mid/hira/ir/hiraIR.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"

#include <algorithm>
#include <cassert>

namespace hira {

void HiraNode::addOperand(HiraValue *value) {
    assert(value && "Hira operands must be explicit values");
    operands_.push_back(value);
}

void HiraNode::addResult(HiraValue *value) {
    assert(value && "Hira results must be explicit values");
    assert(!value->definingNode_ && "Hira value already has a definition");
    value->definingNode_ = this;
    results_.push_back(value);
}

HiraNode *HiraSequence::append(std::unique_ptr<HiraNode> node) {
    return insert(nodes_.size(), std::move(node));
}

HiraNode *HiraSequence::insert(std::size_t position,
                               std::unique_ptr<HiraNode> node) {
    assert(node && "cannot insert a null Hira node");
    assert(!node->parent_ && "Hira node already belongs to a sequence");
    assert(position <= nodes_.size() && "Hira insertion is out of range");
    node->parent_ = this;
    HiraNode *result = node.get();
    nodes_.insert(nodes_.begin() + static_cast<std::ptrdiff_t>(position),
                  std::move(node));
    return result;
}

std::unique_ptr<HiraNode> HiraSequence::remove(HiraNode *node) {
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
                           [node](const std::unique_ptr<HiraNode> &candidate) {
                               return candidate.get() == node;
                           });
    if (it == nodes_.end())
        return nullptr;
    (*it)->parent_ = nullptr;
    std::unique_ptr<HiraNode> result = std::move(*it);
    nodes_.erase(it);
    return result;
}

void HiraLoop::addCarriedValue(HiraValue *value) {
    assert(value && "loop-carried values must be explicit");
    carriedValues_.push_back(value);
    addOperand(value);
}

void HiraLoop::addYieldValue(HiraValue *value) {
    assert(value && "loop yield values must be explicit");
    yieldValues_.push_back(value);
}

void SourceMapping::mapValue(HiraValue *hiraValue, ::Value *llvmValue) {
    assert(hiraValue && llvmValue);
    valueToSource_[hiraValue] = llvmValue;
    sourceToValue_[llvmValue] = hiraValue;
}

void SourceMapping::mapNode(HiraNode *hiraNode,
                            Instruction *llvmInstruction) {
    assert(hiraNode && llvmInstruction);
    nodeToSource_[hiraNode] = llvmInstruction;
    sourceToNode_[llvmInstruction] = hiraNode;
}

void SourceMapping::mapLoop(HiraLoop *hiraLoop, Loop *llvmLoop) {
    assert(hiraLoop && llvmLoop);
    loopToSource_[hiraLoop] = llvmLoop;
    sourceToLoop_[llvmLoop] = hiraLoop;
}

::Value *SourceMapping::sourceValue(const HiraValue *value) const {
    auto it = valueToSource_.find(value);
    return it == valueToSource_.end() ? nullptr : it->second;
}

HiraValue *SourceMapping::hiraValue(const ::Value *value) const {
    auto it = sourceToValue_.find(value);
    return it == sourceToValue_.end() ? nullptr : it->second;
}

Instruction *SourceMapping::sourceInstruction(const HiraNode *node) const {
    auto it = nodeToSource_.find(node);
    return it == nodeToSource_.end() ? nullptr : it->second;
}

HiraNode *SourceMapping::hiraNode(const Instruction *instruction) const {
    auto it = sourceToNode_.find(instruction);
    return it == sourceToNode_.end() ? nullptr : it->second;
}

Loop *SourceMapping::sourceLoop(const HiraLoop *loop) const {
    auto it = loopToSource_.find(loop);
    return it == loopToSource_.end() ? nullptr : it->second;
}

HiraLoop *SourceMapping::hiraLoop(const Loop *loop) const {
    auto it = sourceToLoop_.find(loop);
    return it == sourceToLoop_.end() ? nullptr : it->second;
}

HiraRegion::HiraRegion(Loop *sourceLoop) : sourceLoop_(sourceLoop) {
    if (!sourceLoop)
        return;
    sourcePreheader_ = sourceLoop->preheader;
    sourceExits_ = sourceLoop->exits;
}

HiraValue *HiraRegion::createValue(Type *type) {
    values_.push_back(std::make_unique<HiraValue>(nextValueId_++, type));
    return values_.back().get();
}

void HiraRegion::addParameter(HiraValue *value) {
    assert(value && "region parameters must be explicit");
    parameters_.push_back(value);
}

void HiraRegion::addResult(HiraValue *value) {
    assert(value && "region results must be explicit");
    results_.push_back(value);
}

} // namespace hira
