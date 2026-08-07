#include "../../include/mid/analysis/dominanceAnalysis.hpp"

#include "../../include/mid/ir/basicBlock.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <queue>
#include <string>

namespace {

bool verifyDominanceEnabled() {
    static bool enabled = std::getenv("DEBUG_VERIFY_DOMINANCE") != nullptr;
    return enabled;
}

std::vector<BasicBlock *> computeRPO(Function *func) {
    std::vector<BasicBlock *> postorder;
    if (!func || func->basic_blocks_.empty()) return postorder;

    std::set<BasicBlock *> visited;
    std::function<void(BasicBlock *)> dfs = [&](BasicBlock *bb) {
        if (!bb || !visited.insert(bb).second) return;
        for (auto *succ : bb->succ_bbs_) dfs(succ);
        postorder.push_back(bb);
    };
    dfs(func->basic_blocks_.front());
    std::reverse(postorder.begin(), postorder.end());
    return postorder;
}

} // namespace

void DominatorTreeAnalysis::reset() {
    function_ = nullptr;
    root_ = nullptr;
    nodes_.clear();
}

void DominatorTreeAnalysis::analyze(Function *func) {
    reset();
    function_ = func;
    auto rpo = computeRPO(func);
    if (rpo.empty()) return;

    std::map<BasicBlock *, unsigned> rpoIndex;
    std::map<BasicBlock *, BasicBlock *> idom;
    for (unsigned i = 0; i < rpo.size(); ++i) rpoIndex[rpo[i]] = i;

    BasicBlock *entry = rpo.front();
    idom[entry] = entry;
    auto intersect = [&](BasicBlock *a, BasicBlock *b) {
        while (a != b) {
            while (rpoIndex[a] > rpoIndex[b]) a = idom[a];
            while (rpoIndex[b] > rpoIndex[a]) b = idom[b];
        }
        return a;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (unsigned i = 1; i < rpo.size(); ++i) {
            BasicBlock *bb = rpo[i];
            BasicBlock *newIDom = nullptr;
            for (auto *pred : bb->pre_bbs_) {
                if (!idom.count(pred)) continue;
                newIDom = newIDom ? intersect(newIDom, pred) : pred;
            }
            if (newIDom && (!idom.count(bb) || idom[bb] != newIDom)) {
                idom[bb] = newIDom;
                changed = true;
            }
        }
    }

    for (unsigned i = 0; i < rpo.size(); ++i) {
        auto *bb = rpo[i];
        nodes_[bb] = std::unique_ptr<DominatorTreeNode>(new DominatorTreeNode(bb));
        nodes_[bb]->rpoIndex_ = i;
    }
    root_ = nodes_[entry].get();
    for (auto *bb : rpo) {
        if (bb == entry) continue;
        auto it = idom.find(bb);
        if (it == idom.end()) continue;
        auto *node = nodes_[bb].get();
        node->idom_ = nodes_[it->second].get();
        node->depth_ = node->idom_->depth_ + 1;
        node->idom_->children_.push_back(node);
    }
    assignDFSNumbers();

    if (verifyDominanceEnabled() && !verify()) {
        std::cerr << "[Dominance] invalid dominator tree for @"
                  << (func ? func->name_ : "<null>") << "\n";
        std::abort();
    }
}

void DominatorTreeAnalysis::assignDFSNumbers() {
    unsigned number = 0;
    std::function<void(DominatorTreeNode *)> dfs = [&](DominatorTreeNode *node) {
        node->dfsIn_ = number++;
        for (auto *child : node->children_) dfs(child);
        node->dfsOut_ = number++;
    };
    if (root_) dfs(root_);
}

DominatorTreeNode *DominatorTreeAnalysis::getNode(BasicBlock *bb) const {
    auto it = nodes_.find(bb);
    return it == nodes_.end() ? nullptr : it->second.get();
}

BasicBlock *DominatorTreeAnalysis::getIDom(BasicBlock *bb) const {
    auto *node = getNode(bb);
    return node && node->idom_ ? node->idom_->block_ : nullptr;
}

unsigned DominatorTreeAnalysis::getRPOIndex(BasicBlock *bb) const {
    auto *node = getNode(bb);
    return node ? node->rpoIndex_ : static_cast<unsigned>(-1);
}

const std::vector<DominatorTreeNode *> &
DominatorTreeAnalysis::getChildren(BasicBlock *bb) const {
    static const std::vector<DominatorTreeNode *> empty;
    auto *node = getNode(bb);
    return node ? node->children_ : empty;
}

bool DominatorTreeAnalysis::isReachableFromEntry(BasicBlock *bb) const {
    return getNode(bb) != nullptr;
}

bool DominatorTreeAnalysis::dominates(BasicBlock *a, BasicBlock *b) const {
    auto *an = getNode(a);
    auto *bn = getNode(b);
    return an && bn && an->dfsIn_ <= bn->dfsIn_ && an->dfsOut_ >= bn->dfsOut_;
}

bool DominatorTreeAnalysis::properlyDominates(BasicBlock *a,
                                               BasicBlock *b) const {
    return a != b && dominates(a, b);
}

bool DominatorTreeAnalysis::instructionComesBefore(const Instruction *a,
                                                    const Instruction *b) const {
    if (!a || !b || !a->parent_ || a->parent_ != b->parent_) return false;
    for (auto *inst : a->parent_->instr_list_) {
        if (inst == a) return true;
        if (inst == b) return false;
    }
    return false;
}

bool DominatorTreeAnalysis::dominates(Instruction *def,
                                       Instruction *use) const {
    if (!def || !use || !def->parent_ || !use->parent_) return false;
    if (def == use) return false;
    if (def->parent_ == use->parent_)
        return instructionComesBefore(def, use);
    return dominates(def->parent_, use->parent_);
}

bool DominatorTreeAnalysis::dominates(Value *def, const Use &use) const {
    auto *user = dynamic_cast<Instruction *>(use.val_);
    if (!def || !user || !user->parent_) return false;

    auto *defInst = dynamic_cast<Instruction *>(def);
    if (!defInst) return true;
    auto *phi = dynamic_cast<PhiInst *>(user);
    if (!phi) return dominates(defInst, user);
    if ((use.arg_no_ % 2) != 0 || use.arg_no_ + 1 >= phi->num_ops_)
        return false;
    auto *incoming = dynamic_cast<BasicBlock *>(phi->get_operand(use.arg_no_ + 1));
    if (!incoming || !defInst->parent_) return false;
    if (defInst->parent_ == incoming)
        return defInst != incoming->get_terminator();
    return dominates(defInst->parent_, incoming);
}

BasicBlock *DominatorTreeAnalysis::findNearestCommonDominator(
    BasicBlock *a, BasicBlock *b) const {
    auto *an = getNode(a);
    auto *bn = getNode(b);
    if (!an || !bn) return nullptr;
    while (an->depth_ > bn->depth_) an = an->idom_;
    while (bn->depth_ > an->depth_) bn = bn->idom_;
    while (an != bn) {
        an = an->idom_;
        bn = bn->idom_;
    }
    return an ? an->block_ : nullptr;
}

void DominatorTreeAnalysis::getDescendants(
    BasicBlock *root, std::vector<BasicBlock *> &result) const {
    auto *node = getNode(root);
    if (!node) return;
    result.push_back(root);
    for (auto *child : node->children_)
        getDescendants(child->block_, result);
}

bool DominatorTreeAnalysis::verify() const {
    if (!function_) return nodes_.empty() && !root_;
    auto rpo = computeRPO(function_);
    if (rpo.size() != nodes_.size()) return false;
    if (rpo.empty()) return root_ == nullptr;
    if (!root_ || root_->block_ != rpo.front() || root_->idom_) return false;

    std::set<BasicBlock *> all(rpo.begin(), rpo.end());
    std::map<BasicBlock *, std::set<BasicBlock *>> doms;
    for (auto *bb : rpo) doms[bb] = bb == rpo.front() ? std::set<BasicBlock *>{bb} : all;
    bool changed = true;
    while (changed) {
        changed = false;
        for (unsigned i = 1; i < rpo.size(); ++i) {
            auto *bb = rpo[i];
            std::set<BasicBlock *> next;
            bool first = true;
            for (auto *pred : bb->pre_bbs_) {
                if (!doms.count(pred)) continue;
                if (first) {
                    next = doms[pred];
                    first = false;
                } else {
                    std::set<BasicBlock *> intersection;
                    std::set_intersection(next.begin(), next.end(),
                                          doms[pred].begin(), doms[pred].end(),
                                          std::inserter(intersection, intersection.begin()));
                    next = std::move(intersection);
                }
            }
            next.insert(bb);
            if (next != doms[bb]) {
                doms[bb] = std::move(next);
                changed = true;
            }
        }
    }
    for (auto *a : rpo)
        for (auto *b : rpo)
            if (dominates(a, b) != (doms[b].count(a) != 0)) return false;

    for (auto *bb : rpo) {
        auto *node = getNode(bb);
        if (!node) return false;
        if (bb == rpo.front()) continue;

        BasicBlock *expectedIDom = nullptr;
        for (auto *candidate : doms[bb]) {
            if (candidate == bb) continue;
            bool deepest = true;
            for (auto *other : doms[bb]) {
                if (other == bb || other == candidate) continue;
                if (!doms[candidate].count(other)) {
                    deepest = false;
                    break;
                }
            }
            if (deepest) {
                expectedIDom = candidate;
                break;
            }
        }
        if (getIDom(bb) != expectedIDom) return false;
        if (!node->idom_ || node->depth_ != node->idom_->depth_ + 1)
            return false;
        if (std::find(node->idom_->children_.begin(),
                      node->idom_->children_.end(), node) ==
            node->idom_->children_.end())
            return false;
    }
    return true;
}

void DominatorTreeAnalysis::print(std::ostream &os) const {
    for (const auto &entry : nodes_) {
        auto *node = entry.second.get();
        os << entry.first->name_ << " idom="
           << (node->idom_ ? node->idom_->block_->name_ : "<root>")
           << " depth=" << node->depth_ << "\n";
    }
}

void PostDominatorTreeAnalysis::reset() {
    function_ = nullptr;
    roots_.clear();
    reachesExit_.clear();
    postDominators_.clear();
    ipdom_.clear();
}

void PostDominatorTreeAnalysis::analyze(Function *func) {
    reset();
    function_ = func;
    if (!func || func->basic_blocks_.empty()) return;

    std::queue<BasicBlock *> worklist;
    for (auto *bb : func->basic_blocks_) {
        if (bb->succ_bbs_.empty()) {
            roots_.push_back(bb);
            reachesExit_.insert(bb);
            worklist.push(bb);
        }
    }
    while (!worklist.empty()) {
        auto *bb = worklist.front();
        worklist.pop();
        for (auto *pred : bb->pre_bbs_)
            if (reachesExit_.insert(pred).second) worklist.push(pred);
    }

    std::set<BasicBlock *> all = reachesExit_;
    for (auto *bb : reachesExit_)
        postDominators_[bb] = bb->succ_bbs_.empty()
                                  ? std::set<BasicBlock *>{bb}
                                  : all;

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto *bb : reachesExit_) {
            if (bb->succ_bbs_.empty()) continue;
            std::set<BasicBlock *> next;
            bool first = true;
            for (auto *succ : bb->succ_bbs_) {
                if (!reachesExit_.count(succ)) continue;
                if (first) {
                    next = postDominators_[succ];
                    first = false;
                } else {
                    std::set<BasicBlock *> intersection;
                    std::set_intersection(next.begin(), next.end(),
                                          postDominators_[succ].begin(),
                                          postDominators_[succ].end(),
                                          std::inserter(intersection, intersection.begin()));
                    next = std::move(intersection);
                }
            }
            if (first) continue;
            next.insert(bb);
            if (next != postDominators_[bb]) {
                postDominators_[bb] = std::move(next);
                changed = true;
            }
        }
    }

    for (auto *bb : reachesExit_) {
        BasicBlock *candidate = nullptr;
        for (auto *possible : postDominators_[bb]) {
            if (possible == bb) continue;
            bool immediate = true;
            for (auto *other : postDominators_[bb]) {
                if (other == bb || other == possible) continue;
                if (postDominators_[other].count(possible)) {
                    immediate = false;
                    break;
                }
            }
            if (immediate) {
                candidate = possible;
                break;
            }
        }
        ipdom_[bb] = candidate;
    }

    if (verifyDominanceEnabled() && !verify()) {
        std::cerr << "[Dominance] invalid post-dominator tree for @"
                  << func->name_ << "\n";
        std::abort();
    }
}

BasicBlock *PostDominatorTreeAnalysis::getIPostDominator(BasicBlock *bb) const {
    auto it = ipdom_.find(bb);
    return it == ipdom_.end() ? nullptr : it->second;
}

bool PostDominatorTreeAnalysis::canReachExit(BasicBlock *bb) const {
    return reachesExit_.count(bb) != 0;
}

bool PostDominatorTreeAnalysis::postDominates(BasicBlock *a,
                                               BasicBlock *b) const {
    auto it = postDominators_.find(b);
    return a && it != postDominators_.end() && it->second.count(a) != 0;
}

bool PostDominatorTreeAnalysis::properlyPostDominates(BasicBlock *a,
                                                       BasicBlock *b) const {
    return a != b && postDominates(a, b);
}

bool PostDominatorTreeAnalysis::verify() const {
    if (!function_) return reachesExit_.empty();

    std::set<BasicBlock *> expectedReachable;
    std::vector<BasicBlock *> expectedRoots;
    std::queue<BasicBlock *> worklist;
    for (auto *bb : function_->basic_blocks_) {
        if (!bb->succ_bbs_.empty()) continue;
        expectedRoots.push_back(bb);
        expectedReachable.insert(bb);
        worklist.push(bb);
    }
    while (!worklist.empty()) {
        auto *bb = worklist.front();
        worklist.pop();
        for (auto *pred : bb->pre_bbs_)
            if (expectedReachable.insert(pred).second) worklist.push(pred);
    }
    if (expectedReachable != reachesExit_ || expectedRoots != roots_)
        return false;

    std::map<BasicBlock *, std::set<BasicBlock *>> expected;
    for (auto *bb : expectedReachable)
        expected[bb] = bb->succ_bbs_.empty()
                           ? std::set<BasicBlock *>{bb}
                           : expectedReachable;
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto *bb : expectedReachable) {
            if (bb->succ_bbs_.empty()) continue;
            std::set<BasicBlock *> next;
            bool first = true;
            for (auto *succ : bb->succ_bbs_) {
                if (!expectedReachable.count(succ)) continue;
                if (first) {
                    next = expected[succ];
                    first = false;
                } else {
                    std::set<BasicBlock *> intersection;
                    std::set_intersection(
                        next.begin(), next.end(), expected[succ].begin(),
                        expected[succ].end(),
                        std::inserter(intersection, intersection.begin()));
                    next = std::move(intersection);
                }
            }
            if (first) continue;
            next.insert(bb);
            if (next != expected[bb]) {
                expected[bb] = std::move(next);
                changed = true;
            }
        }
    }
    if (expected != postDominators_) return false;

    for (auto *bb : reachesExit_) {
        auto *parent = getIPostDominator(bb);
        BasicBlock *expectedParent = nullptr;
        for (auto *candidate : expected[bb]) {
            if (candidate == bb) continue;
            bool immediate = true;
            for (auto *other : expected[bb]) {
                if (other == bb || other == candidate) continue;
                if (expected[other].count(candidate)) {
                    immediate = false;
                    break;
                }
            }
            if (immediate) {
                expectedParent = candidate;
                break;
            }
        }
        if (parent != expectedParent) return false;
    }
    return true;
}

void PostDominatorTreeAnalysis::print(std::ostream &os) const {
    for (auto *bb : reachesExit_) {
        auto *parent = getIPostDominator(bb);
        os << bb->name_ << " ipdom="
           << (parent ? parent->name_ : "<virtual-root>") << "\n";
    }
}

void DominanceFrontierAnalysis::reset() {
    function_ = nullptr;
    frontiers_.clear();
}

void DominanceFrontierAnalysis::analyze(Function *func,
                                         const DominatorTreeAnalysis &DT) {
    reset();
    function_ = func;
    if (!func) return;
    for (auto *bb : func->basic_blocks_) {
        if (!DT.isReachableFromEntry(bb) || bb->pre_bbs_.size() < 2) continue;
        BasicBlock *idom = DT.getIDom(bb);
        for (auto *pred : bb->pre_bbs_) {
            if (!DT.isReachableFromEntry(pred)) continue;
            BasicBlock *runner = pred;
            while (runner && runner != idom) {
                frontiers_[runner].insert(bb);
                runner = DT.getIDom(runner);
            }
        }
    }
    if (verifyDominanceEnabled() && !verify(DT)) {
        std::cerr << "[Dominance] invalid dominance frontier for @"
                  << func->name_ << "\n";
        std::abort();
    }
}

const std::set<BasicBlock *> &
DominanceFrontierAnalysis::getFrontier(BasicBlock *bb) const {
    static const std::set<BasicBlock *> empty;
    auto it = frontiers_.find(bb);
    return it == frontiers_.end() ? empty : it->second;
}

bool DominanceFrontierAnalysis::verify(const DominatorTreeAnalysis &DT) const {
    if (function_ != DT.function()) return false;
    for (const auto &entry : frontiers_) {
        for (auto *frontier : entry.second) {
            bool hasDominatedPred = false;
            for (auto *pred : frontier->pre_bbs_)
                hasDominatedPred |= DT.dominates(entry.first, pred);
            if (!hasDominatedPred || DT.properlyDominates(entry.first, frontier))
                return false;
        }
    }
    return true;
}

void DominanceFrontierAnalysis::print(std::ostream &os) const {
    for (const auto &entry : frontiers_) {
        os << entry.first->name_ << ":";
        for (auto *bb : entry.second) os << " " << bb->name_;
        os << "\n";
    }
}
