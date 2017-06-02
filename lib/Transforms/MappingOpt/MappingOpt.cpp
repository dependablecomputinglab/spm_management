//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Support/CommandLine.h"

#include "Overlay.h"

using namespace llvm;

#define DEBUG_TYPE "smmmo"

static cl::opt<std::string> OptimizationType(
        "optimization-type", cl::Hidden,
        cl::desc("Optimization type."));

static std::unordered_map <Function *, unsigned long> funcSize;
static CostInfo costInfo;
static CallGraph *callGraph;

namespace {
    // Hello - The first implementation, without getAnalysisUsage.
    struct MappingOpt : public ModulePass {
        static char ID; // Pass identification, replacement for typeid
        MappingOpt() : ModulePass(ID) {}

        virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
            AU.setPreservesCFG();
            AU.addRequired<LoopInfoWrapperPass>();
            AU.addRequired<DominatorTreeWrapperPass>();
            AU.addRequired<CallGraphWrapperPass>();
        }


        bool readFuncSizes(Module &mod, std::string fileName) {
            std::ifstream ifs;
            ifs.open(fileName, std::ifstream::in | std::ifstream::binary);
            if(!ifs.good()) return false;

            while(ifs.good()) {
                unsigned long size;
                std::string name;
                ifs >> name >> size;
                if (name.empty()) continue;

                Function *function = mod.getFunction(name);
                funcSize[function] = size;
            }

            return true;
        }


        Function* getCaller(Function *fa, Function *fb) {
            CallGraphNode *fa_cgn = (*callGraph)[fa];
            //CallGraphNode *fb_cgn = (*callGraph)[fb];

            Function *caller = NULL; //Function *callee = NULL;
            for(auto callRecord : *fa_cgn) {
                Function *temp = callRecord.second->getFunction();
                if(temp == fb) {
                    caller = fa; //callee = fb;
                    break;
                }
            }
            if(!caller) {
                caller = fb; //callee = fa;
            }
            return caller;
        }


        unsigned int getOptimalSize(Module &mod) {
            unsigned long minSpmSize, maxSpmSize, optimalSize, threshold;
            minSpmSize = maxSpmSize = optimalSize = 0;

            for(auto const entry : funcSize) {
                //entry.first is a function *
                //Function *function = entry.first;
                unsigned long size = entry.second;
                maxSpmSize += size;
                if(size > minSpmSize) {
                    minSpmSize = size;
                }
            }

            threshold = 100 * maxSpmSize;
            unsigned long cost, nextSpmSize;
            optimalSize = nextSpmSize = maxSpmSize;
            DEBUG(errs() << "threshold: " << threshold << "\n");

            while(1) {
                CostCalculator calculator(this, mod);
                cost = calculator.calculateCost(nextSpmSize);
                if(cost >= threshold) break;
                optimalSize = nextSpmSize;
                if(optimalSize <= minSpmSize) break;
                nextSpmSize = nextSpmSize - calculator.getNextSpmSize();
            }

            CostCalculator calculator(this, mod);
            cost = calculator.calculateCost(optimalSize);
            costInfo = calculator.analyzeCost();

            return optimalSize;
        }


        bool inlineFunction(CallInst *callInst) {
            Function *callee = callInst->getCalledFunction();
            if(!callee || isLibraryFunction(callee)) {
                return false;
            }
            InlineFunctionInfo IFI;
            return InlineFunction(callInst, IFI);
        }


        bool inlineOptimize(Module &mod) {
            unsigned long cost = 0, maxConflict = 0;
            std::pair <Function *, Function *> maxConflictingFunctions;

            for(auto i = costInfo.begin(); i != costInfo.end(); i++) {
                std::pair <Function *, Function *> key = i->first;
                unsigned long conflict = i->second;
                errs() << key.first->getName() << " <-> " << key.second->getName() << "\t" << conflict << "\n";
                cost += conflict;
                if(maxConflict < conflict) {
                    maxConflict = conflict;
                    maxConflictingFunctions = i->first;
                }
            }

            Function *fa = maxConflictingFunctions.first;
            Function *fb = maxConflictingFunctions.second;
            Function *caller = getCaller(fa, fb);
            Function *callee = caller == fa ? fb : fa;
            errs() << "caller: " << caller->getName() << ", callee: " << callee->getName() << "\n";

            //Inline callee into caller
            std::vector <CallInst *> callInsts;
            for(BasicBlock &bb : *caller) {
                for(Instruction &inst : bb) {
                    if(CallInst *callInst = dyn_cast<CallInst>(&inst)) {
                        if(callInst->getCalledFunction() == callee) {
                            callInsts.push_back(callInst);
                        }
                    }
                }
            }

            errs() << "Inline function " << callee->getName() << "\n";
            for(CallInst *inst : callInsts) {
                inlineFunction(inst);
            }

            if (callee->use_empty()) {
                CallGraphNode *cgn = (*callGraph)[callee];
                cgn->removeAllCalledFunctions();
                callee->dropAllReferences();
                callee->removeFromParent();
            }

            return false;
        }


        bool outlineOptimizePerf(Module &mod) {
            unsigned long cost = 0, maxConflict = 0;
            std::pair <Function *, Function *> maxConflictingFunctions;

            for(auto i = costInfo.begin(); i != costInfo.end(); i++) {
                std::pair <Function *, Function *> key = i->first;
                unsigned long conflict = i->second;
                errs() << key.first->getName() << " <-> " << key.second->getName() << "\t" << conflict << "\n";
                cost += conflict;
                if(maxConflict < conflict) {
                    maxConflict = conflict;
                    maxConflictingFunctions = i->first;
                }
            }

            Function *fa = maxConflictingFunctions.first;
            Function *fb = maxConflictingFunctions.second;
            Function *caller = getCaller(fa, fb);
            Function *callee = caller == fa ? fb : fa;
            errs() << "caller: " << caller->getName() << ", callee: " << callee->getName() << "\n";

            //Inline callee into caller
            std::vector <CallInst *> callInsts;
            for(BasicBlock &bb : *caller) {
                for(Instruction &inst : bb) {
                    if(CallInst *callInst = dyn_cast<CallInst>(&inst)) {
                        if(callInst->getCalledFunction() == callee) {
                            callInsts.push_back(callInst);
                        }
                    }
                }
            }

            for(CallInst *inst : callInsts) {
                BasicBlock *b = inst->getParent();
                Function *f = b->getParent();

                DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>(*f).getDomTree();
                LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(*f).getLoopInfo();
                LI.verify(DT);
                Loop *loop = LI.getLoopFor(b);

                if(!loop) continue;
                errs() << loop;
                loop->dump();
                errs() << inst;
                inst->dump();

                auto BBs = loop->getBlocks();
                for(auto &BB : BBs) {
                    for (BasicBlock::const_iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
                        //if (isa<AllocaInst>(I) || isa<InvokeInst>(I))
                            I->dump();
                    }
                }


                /*
                DominatorTree *DT = new DominatorTree(
                        getAnalysis<DominatorTreeWrapperPass>(*f).getDomTree());
                */

                CodeExtractor code_extractor(DT, *loop);
                Function *new_func = code_extractor.extractCodeRegion();
                if(!new_func) {
                    errs() << "failed to extract the loop\n";
                } else {
                    errs() << "loop " << *loop << " is extracted into new function " <<
                        new_func->getName() << "\n";
                }
            }
            return false;
        }


        bool outlineOptimizeSize(Module &mod) {
            return false;
        }


        bool runOnModule(Module &mod) override {

            //read function sizes
            std::string fileName = "_func_size";
            if(!readFuncSizes(mod, fileName)) {
                errs() << "Error reading " << fileName << "\n";
                return false;
            }

            //make callgraph
            callGraph = &getAnalysis<CallGraphWrapperPass>().getCallGraph();

            //build costInfo
            unsigned long minSpmSize = 0;
            for(auto const entry : funcSize) {
                //entry.first is a function *
                //Function *function = entry.first;
                unsigned long size = entry.second;
                if(size > minSpmSize) {
                    minSpmSize = size;
                }
            }
            CostCalculator calculator(this, mod);
            errs() << "calculate cost for minSpmSize=" << minSpmSize << "\n";
            calculator.calculateCost(minSpmSize);
            costInfo = calculator.analyzeCost();

            if (OptimizationType == "inline") {
                inlineOptimize(mod);
            } else if (OptimizationType == "size") {
                outlineOptimizeSize(mod);
            } else if (OptimizationType == "perf") {
                outlineOptimizePerf(mod);
            }

            /*
            GCCFG gccfg(this);
            gccfg.analyze();
            gccfg.print();
            analysisResult = gccfg.getAnalysisResult();
            */

            /*
            CostCalculator calculator(this, mod);
            calculator.calculateCost(119);

            for(Function &f : mod) {
                if(f.getName() != "main") continue;
                LoopInfo &LI = getAnalysis<LoopInfo>(f);
                DominatorTree *DT = new DominatorTree(
                        getAnalysis<DominatorTreeWrapperPass>(f).getDomTree());
                for(Loop* loop : LI) {
                    loop->dump();
                    CodeExtractor code_extractor(*DT, *loop);
                    Function *new_func = code_extractor.extractCodeRegion();
                }

                for(BasicBlock &bb : f) {
                    errs() << bb.size() << "\n";
                    //bb.dump();
                    for(Instruction &inst : bb) {
                        //
                    }
                }
            }
            */

            return false;
        }
    };
}

char MappingOpt::ID = 0;
static RegisterPass<MappingOpt> X("smmmo", "Mapping Opt Pass");
