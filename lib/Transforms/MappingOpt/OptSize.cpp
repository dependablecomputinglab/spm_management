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


static std::unordered_map <Function *, unsigned long> funcSize;
static CostInfo costInfo;
static CallGraph *callGraph;

namespace {
    // Hello - The first implementation, without getAnalysisUsage.
    struct OptSize : public ModulePass {
        static char ID; // Pass identification, replacement for typeid
        OptSize() : ModulePass(ID) {}

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

            Function *caller = NULL;
            //Function *callee = NULL;
            for(auto callRecord : *fa_cgn) {
                Function *temp = callRecord.second->getFunction();
                if(temp == fb) {
                    caller = fa;
                    //callee = fb;
                    break;
                }
            }
            if(!caller) {
                caller = fb;
                //callee = fa;
            }
            return caller;
        }


        unsigned int getOptimalSize(Module &mod, unsigned long &optimalCost) {
            unsigned long minSpmSize, maxSpmSize, optimalSize, threshold;
            minSpmSize = maxSpmSize = optimalSize = 0;

            //use only referenced functions
            {
                CostCalculator calculator(this, mod);
                calculator.calculateCost(100000);
                funcSize = calculator.getFuncSize();
            }

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
                DEBUG(errs() << "SPM size: " << nextSpmSize << ", cost: " << cost <<"\n\n");
                if(cost >= threshold) break;
                optimalSize = nextSpmSize;
                errs() << "optimalSize: " << optimalSize << ", minSpmSize: " << minSpmSize << "\n";
                if(optimalSize <= minSpmSize) break;
                long ret = calculator.getNextSpmSize();
                if(ret > 0)
                    nextSpmSize = nextSpmSize - calculator.getNextSpmSize();
                else {
                    optimalSize = minSpmSize;
                    break;
                }
            }

            CostCalculator calculator(this, mod);
            cost = calculator.calculateCost(optimalSize);
            costInfo = calculator.analyzeCost();


            optimalCost = cost;

            std::ofstream outFile("_opt_size", std::ios::out);
            outFile << "min\t" << minSpmSize << "\t" << -1 << "\n";
            outFile << "max\t" << maxSpmSize << "\t" << -1 << "\n";
            outFile << "opt\t" << optimalSize << "\t" << optimalCost << "\n";
            outFile.close();

            return optimalSize;
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

            //get optimal SPM size for the current code
            unsigned long optimalCost;
            unsigned int optimalSize = getOptimalSize(mod, optimalCost);
            DEBUG(errs() << "optimal size: " << optimalSize << ", cost: " << optimalCost << "\n");

            return false;
        }
    };
}

char OptSize::ID = 0;
static RegisterPass<OptSize> X("smmmo-opt-size", "Mapping Opt Pass");
