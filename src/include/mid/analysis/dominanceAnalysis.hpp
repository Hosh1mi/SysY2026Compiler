#pragma once

#include <iosfwd>
#include <map>
#include <memory>
#include <set>
#include <vector>

class BasicBlock;
class Function;
class Instruction;
class Value;
struct Use;

class DominatorTreeNode {
public:
    BasicBlock *block() const { return block_; }
    DominatorTreeNode *idom() const { return idom_; }
    const std::vector<DominatorTreeNode *> &children() const { return children_; }
    unsigned depth() const { return depth_; }
    unsigned rpoIndex() const { return rpoIndex_; }

private:
    friend class DominatorTreeAnalysis;
    explicit DominatorTreeNode(BasicBlock *block) : block_(block) {}

    BasicBlock *block_ = nullptr;
    DominatorTreeNode *idom_ = nullptr;
    std::vector<DominatorTreeNode *> children_;
    unsigned depth_ = 0;
    unsigned rpoIndex_ = 0;
    unsigned dfsIn_ = 0;
    unsigned dfsOut_ = 0;
};

class DominatorTreeAnalysis {
public:
    void analyze(Function *func);
    void reset();

    Function *function() const { return function_; }
    DominatorTreeNode *getRootNode() const { return root_; }
    DominatorTreeNode *getNode(BasicBlock *bb) const;
    BasicBlock *getIDom(BasicBlock *bb) const;
    unsigned getRPOIndex(BasicBlock *bb) const;
    const std::vector<DominatorTreeNode *> &getChildren(BasicBlock *bb) const;

    bool isReachableFromEntry(BasicBlock *bb) const;
    bool dominates(BasicBlock *a, BasicBlock *b) const;
    bool properlyDominates(BasicBlock *a, BasicBlock *b) const;
    bool dominates(Instruction *def, Instruction *use) const;
    bool dominates(Value *def, const Use &use) const;

    BasicBlock *findNearestCommonDominator(BasicBlock *a,
                                            BasicBlock *b) const;
    void getDescendants(BasicBlock *root,
                        std::vector<BasicBlock *> &result) const;

    bool verify() const;
    void print(std::ostream &os) const;

private:
    void assignDFSNumbers();
    bool instructionComesBefore(const Instruction *a,
                                const Instruction *b) const;

    Function *function_ = nullptr;
    DominatorTreeNode *root_ = nullptr;
    std::map<BasicBlock *, std::unique_ptr<DominatorTreeNode>> nodes_;
};

class PostDominatorTreeAnalysis {
public:
    void analyze(Function *func);
    void reset();

    Function *function() const { return function_; }
    BasicBlock *getIPostDominator(BasicBlock *bb) const;
    bool canReachExit(BasicBlock *bb) const;
    bool postDominates(BasicBlock *a, BasicBlock *b) const;
    bool properlyPostDominates(BasicBlock *a, BasicBlock *b) const;
    const std::vector<BasicBlock *> &roots() const { return roots_; }

    bool verify() const;
    void print(std::ostream &os) const;

private:
    Function *function_ = nullptr;
    std::vector<BasicBlock *> roots_;
    std::set<BasicBlock *> reachesExit_;
    std::map<BasicBlock *, std::set<BasicBlock *>> postDominators_;
    std::map<BasicBlock *, BasicBlock *> ipdom_;
};

class DominanceFrontierAnalysis {
public:
    void analyze(Function *func, const DominatorTreeAnalysis &DT);
    void reset();

    const std::set<BasicBlock *> &getFrontier(BasicBlock *bb) const;
    bool verify(const DominatorTreeAnalysis &DT) const;
    void print(std::ostream &os) const;

private:
    Function *function_ = nullptr;
    std::map<BasicBlock *, std::set<BasicBlock *>> frontiers_;
};
