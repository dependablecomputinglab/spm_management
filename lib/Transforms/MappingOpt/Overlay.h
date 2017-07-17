#pragma once
#ifndef __OVERLAY_H__
#define __OVERLAY_H__

#include "llvm/Pass.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/YAMLTraits.h"

#include "llvm/Support/CommandLine.h"

#include <cassert>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <set>
#include <stack>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "FuncType.h"

using namespace llvm;

typedef std::pair <Function *, std::vector<BasicBlock *> >  NodeType;
typedef std::vector < NodeType > PathType;
typedef std::set <PathType> PathsType;
typedef std::map < std::pair <Function *, Function *>, unsigned long > CostInfo;

class CallPathFinder {
    public:

    CallPathFinder(Pass *p, Module &m);
    PathsType getCallPaths(Function *root);
    std::unordered_set <Function *> getReferredFunctions();

    private:
    void concatenate(std::set< PathType > &dest, NodeType &src);
    std::unordered_set <Function *> referredFuncs;
    std::unordered_map <Function *, std::set< PathType > > funcCallPaths;

    Pass *pass;
    CallGraph &cg;
    Module &mod;
};

class CostCalculator {
    public:
    class Region {
        public:
        unsigned long getSize();
        bool hasFunction(Function *func) { return funcs.find(func) != funcs.end(); }
        void addFunction(Function *func) {funcs.insert(func);}
        std::set <Function *> getFunctions() {return funcs;}
        void merge(Region *r) {
            std::set <Function *> newFuncs = r->getFunctions();
            funcs.insert(newFuncs.begin(), newFuncs.end());
        }

        std::string getDescription();
        private:
        std::set<Function *> funcs;
    };


    CostCalculator(Pass *p, Module &m);
    void getCallPaths();
    unsigned long calculateCost(unsigned long spmSize);
    long getNextSpmSize();
    CostInfo analyzeCost();

    private:
    unsigned long getRegionSizeSum();
    unsigned long getMaxRegionSize();
    unsigned long findMerger(Region* &src, Region* &dest);
    unsigned long calculateMergerCost(Region *r1, Region *r2);
    void dump();

    Pass *pass;
    CallGraph &cg;
    Module &mod;
    std::set<Region *> regions;
    CallPathFinder pathFinder;
    std::unordered_set <Function *> referredFuncs;
    PathsType callPaths;
    //std::unordered_map <Function *, unsigned long> funcSize;

};

#endif
