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

static cl::opt<std::string> SizeThreshold(
        "size-threshold", cl::Hidden,
        cl::desc("Size threshold."));

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


        bool stopMinimize(long cost, long prevCost) {
            if(prevCost <= 0) return false;
            unsigned long threshold = atoi(SizeThreshold.c_str());
            double improved = (((double)prevCost / cost)) * 100;
            DEBUG(errs() << "threshold: " << threshold << ", improved: " << improved << 
                    ", cost: " << cost << ", prevCost: " << prevCost << "\n");
            return improved < threshold;
        }



        unsigned int getOptimalSize(Module &mod, unsigned long &optimalCost) {
            unsigned long minSpmSize, maxSpmSize, optimalSize;
            minSpmSize = maxSpmSize = optimalSize = 0;

            DEBUG(errs() << "getOptimalSize called\n");

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

            /*

            long cost, prevCost=-1, nextSpmSize;
            optimalSize = nextSpmSize = minSpmSize;

            DEBUG(errs() << "start finding SPM size starting with: " << optimalSize << "\n");
            while(1) {
                CostCalculator calculator(this, mod);
                MappingConfig mappingConfigs;
                cost = calculator.calculateCost(nextSpmSize, &mappingConfigs);
                for(auto i : mappingConfigs) {
                    errs() << i.first << "\t" << i.second << "\n";
                }
                DEBUG(errs() << "SPM size: " << nextSpmSize << ", cost: " << cost <<"\n");
                errs() << "optimalSize: " << optimalSize << ", minSpmSize: " << minSpmSize << "\n\n";
                if(stopMinimize(cost, prevCost)) break;
                optimalSize = nextSpmSize;
                if(optimalSize >= maxSpmSize) break;
                DEBUG(errs() << "getNextSpmSize called\n");
                long ret = calculator.getNextSpmSize();
                if(ret > 0) {
                    nextSpmSize = nextSpmSize + ret;
                    prevCost = cost;
                }
                else {
                    optimalSize = minSpmSize;
                    break;
                }
            }

            CostCalculator calculator(this, mod);
            cost = calculator.calculateCost(optimalSize);
            costInfo = calculator.analyzeCost();


            optimalCost = cost;
            */

            unsigned int cost;
            CostCalculator calculator(this, mod);
            MappingConfig mappingConfigs;
            cost = calculator.calculateCost(minSpmSize, &mappingConfigs);
            std::reverse(mappingConfigs.begin(), mappingConfigs.end());

            //double improved = (((double)prevCost / cost)) * 100;

            for(auto i : mappingConfigs) {
                unsigned int size = i.first;
                unsigned int cost = i.second;
                errs() << (int)(100*(double)size/minSpmSize) << ", " << (int)(100*(double)cost/minSpmSize) << "\n";
            }

            unsigned long i=0;
            double minCost = mappingConfigs[0].second;
            double minSize = mappingConfigs[0].first;

            unsigned long threshold = atoi(SizeThreshold.c_str());
            errs() << "threshold: " << threshold << "\n";

            for(i=1; i<mappingConfigs.size(); i++) {
                unsigned int prevSize, prevCost, curSize, curCost;
                prevSize = mappingConfigs[i-1].first; prevCost = mappingConfigs[i-1].second;
                curSize = mappingConfigs[i].first; curCost = mappingConfigs[i].second;
                double sizeOverhead = (curSize - prevSize) / (double)minSize;
                double performanceGain = (prevCost - curCost) / (double)minCost;
                double utility = (performanceGain / sizeOverhead) * 100;
                errs() << "sizeOverhead: " << sizeOverhead << ", performanceGain: " << performanceGain << ", utility: " << utility << "\n";
                if(utility < threshold) break;
                else {
                    errs() << "utility (" << utility << ") is greater than threshold (" << threshold << "), increase size\n";
                }
            }
            optimalSize = mappingConfigs[i-1].first;
            optimalCost = mappingConfigs[i-1].second;

            std::ofstream outFile("_opt_size", std::ios::out);
            outFile << "min\t" << mappingConfigs[0].first << "\t" << mappingConfigs[0].second << "\n";
            outFile << "max\t" << mappingConfigs[mappingConfigs.size()-1].first << "\t" << 0 << "\n";
            outFile << "opt\t" << optimalSize << "\t" << optimalCost << "\n";
            outFile.close();

            return optimalSize;
        }


        bool runOnModule(Module &mod) override {

            errs() << "opt-size pass called\n";

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
