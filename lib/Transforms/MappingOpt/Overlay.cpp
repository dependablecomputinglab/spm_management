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

#define DEBUG_TYPE "smmcmh-overlay"

#include "Overlay.h"

static std::unordered_map <Function *, unsigned long> funcSize;

CallPathFinder::CallPathFinder(Pass *p, Module &m) : cg(p->getAnalysis<CallGraphWrapperPass>().getCallGraph()), mod(m) {
    pass = p;
}

PathsType CallPathFinder::getCallPaths(Function *root) {
    BasicBlock *entryBlock;
    BasicBlock *lpHeader;
    std::set< PathType > result;
    std::stack < std::pair < BasicBlock *, std::set< PathType > > > s;

    DEBUG(errs() << root->getName() << " starts\n");

    // Return if the result has been cached
    auto it = funcCallPaths.find(root);
    if (it != funcCallPaths.end()) {
        DEBUG(errs() << root->getName() << " ends\n");
        return it->second;
    }

    referredFuncs.insert(root);


    // Return if this function does not call any user functions
    CallGraphNode *cgn = cg[root];
    assert(cgn);

    bool hasUserFunctionCalls = false;


    for (unsigned i = 0; i < cgn->size(); ++i) {
        CallGraphNode *calledCgn = (*cgn)[i];
        Function *calledFunc = calledCgn->getFunction();
        if (!calledFunc) {
            DEBUG(errs() << "cannot get !calledFunc\n");
            continue;
        }
        if (isLibraryFunction(calledFunc)) {
            DEBUG(errs() << calledFunc->getName() << " is LibFunction" << "\n");
            continue;
        }
        //TODO Skip self-recursive calls
        if (calledFunc == root)
            continue;
        hasUserFunctionCalls = true;
        break;
    }

    DEBUG(errs() << "hasUserFunctionCalls: " << hasUserFunctionCalls << "\n");

    std::set< PathType > paths;

    if(hasUserFunctionCalls) {
        // DFS visit the basic blocks of this function
        LoopInfo &lpi = pass->getAnalysis<LoopInfoWrapperPass>(*root).getLoopInfo();
        entryBlock = &root->getEntryBlock();
        lpHeader = NULL;
        if(Loop *lp = lpi.getLoopFor(entryBlock))
            lpHeader = lp->getHeader();
        std::set< PathType > emptyPaths;
        s.push(std::make_pair(&root->getEntryBlock(),  emptyPaths));

        //std::set< PathType > paths;
        while (!s.empty()) {
            bool isTerminal = false;
            std::set< PathType > subPaths;

            // Pick up a basic block
            std::pair < BasicBlock *, std::set< PathType >> temp = s.top();
            s.pop();
            BasicBlock *v = temp.first;
            // Get the paths till this basic block
            /*
               if(paths.size() == 0)
               paths = temp.second;
               */
            DEBUG(errs() << "\t" << v->getName() << " begins\n");


            // Go through the user function calls within this basic block in program order
            for (BasicBlock::iterator ii = v->begin(), ie = v->end(); ii != ie; ++ii) {
                if (CallInst *callInst = dyn_cast<CallInst>(&*ii)) {
                    Function *callee = callInst->getCalledFunction();
                    if (!callee)
                        continue;
                    if (isLibraryFunction(callee))
                        continue;
                    // TODO: Skip self-recursive calls
                    if (callee == root)
                        continue;

                    // Found an user function call
                    LoopInfo &lpi = pass->getAnalysis<LoopInfoWrapperPass>(*root).getLoopInfo();
                    std::vector <BasicBlock *> nestedLoopHeaders;
                    //unsigned lpDepth = lpi.getLoopDepth(v);
                    lpHeader = NULL;
                    if (Loop *lp = lpi.getLoopFor(v)) {
                        while (lp) {
                            //lp->dump();
                            lpHeader = lp->getHeader();
                            nestedLoopHeaders.push_back(lpHeader);
                            lp = lp->getParentLoop();
                        }
                    }
                    //DEBUG(errs() << "\t\t" << *callInst << " loop nest: " << lpDepth << "\n");
                    DEBUG(errs() << "\t\t" << *callInst << "\n");


                    // Append the caller function to the paths before this user function call
                    NodeType node = std::make_pair (root, nestedLoopHeaders);
                    concatenate(paths, node);

                    DEBUG(errs() << "\t\t\tPaths before:\n");
                    for (auto ii = paths.begin(), ie = paths.end(); ii != ie; ++ii) {
                        DEBUG(errs() << "\t\t\t\t");
                        for (auto ji = ii->begin(), je = ii->end(); ji != je; ++ji) {
                            Function *func = ji->first;
                            //unsigned lpDepth = ji->second;
                            //DEBUG(errs() << func->getName() << " " << lpDepth << " " );
                            DEBUG(errs() << func->getName() << " ( " );
                            for (auto ki = ji->second.begin(), ke = ji->second.end(); ki != ke; ++ki)
                                DEBUG(errs() << *ki << " ");
                            DEBUG(errs() << " ) " );
                        }
                        DEBUG(errs() << "\n");
                    }
                    DEBUG(errs() << "\n");

                    // Calculate the paths caused by the function call
                    subPaths = getCallPaths(callee);

                    // Combine the current paths with the additional paths caused by the function call if there are any
                    assert (!subPaths.empty());
                    {
                        size_t oldSize = paths.size();
                        auto ii = paths.begin();

                        for (size_t i = 0; i < oldSize; ++i) {
                            auto it = ii++;
                            PathType path1 = *it;

                            for (auto ji = subPaths.begin(), je = subPaths.end(); ji != je; ++ji) {
                                PathType path2 = *ji;
                                for (auto ki = path2.begin(), ke = path2.end(); ki != ke; ++ki) {
                                    // Count the enclosing loops of the call instruction
                                    //ki->second += lpDepth;
                                    if (!nestedLoopHeaders.empty()) {
                                        //ki->second.insert(ki->second.back(), nestedLoopHeaders.begin(), nestedLoopHeaders.end());
                                        for (auto ii = nestedLoopHeaders.begin(), ie = nestedLoopHeaders.end(); ii != ie; ++ii) {
                                            ki->second.push_back( *ii);
                                        }
                                    }
                                }
                                path2.insert(path2.begin(), path1.begin(), path1.end());
                                paths.insert(path2);
                            }
                            paths.erase(it);
                        }
                    }


                    DEBUG(errs() << "\t\t\tPaths after:\n");
                    for (auto ii = paths.begin(), ie = paths.end(); ii != ie; ++ii) {
                        DEBUG(errs() << "\t\t\t\t");
                        for (auto ji = ii->begin(), je = ii->end(); ji != je; ++ji) {
                            Function *func = ji->first;
                            //unsigned lpDepth = ji->second;
                            //DEBUG(errs() << func->getName() << " " << lpDepth << " ");
                            DEBUG(errs() << func->getName() << " ");
                        }
                        DEBUG(errs() << "\n");
                    }
                    DEBUG(errs() << "\n");

                    assert(!paths.empty());
                }

                else if(dyn_cast<ReturnInst>(&*ii) || dyn_cast<UnreachableInst>(&*ii)){
                    isTerminal = true;
                    break;
                }
            }


            DEBUG(errs() << "\t" << v->getName() << " ends\n");

            if(!isTerminal)  {
                DominatorTree &dt = pass->getAnalysis<DominatorTreeWrapperPass>(*root).getDomTree();
                for (succ_iterator si = succ_begin(v), se = succ_end(v); si != se; ++si) {
                    BasicBlock * succ = *si;
                    if (dt.dominates(succ, v) || succ == v)
                        continue;
                    s.push(std::make_pair(succ, paths));
                }
            } else {
                result.insert(paths.begin(), paths.end());
            }
        }
    }

    // Append the caller function to the paths before the caller function returns
    std::vector <BasicBlock *> nestedLoopHeaders;
    NodeType node = std::make_pair(root, nestedLoopHeaders);
    concatenate(result, node);
    concatenate(paths, node);
    DEBUG(errs() << root->getName() << " ends\n");
    funcCallPaths[root] = result;
    funcCallPaths[root] = paths;
    return paths;
    return result;
}



void CallPathFinder::concatenate(std::set< PathType > &dest, NodeType &src) {
    size_t oldSize = dest.size();
    if (oldSize) {
        auto ii = dest.begin();
        for (size_t i = 0; i < oldSize; ++i) {
            auto it = ii++;
            PathType path = *it;
            path.push_back(src);
            dest.insert(path);
            dest.erase(it);
        }
    } else {
        PathType path;
        path.push_back(src);
        dest.insert(path);
    }
}


std::unordered_set <Function *> CallPathFinder::getReferredFunctions() {
    return referredFuncs;
}

std::unordered_map <Function *, unsigned long> CostCalculator::getFuncSize() {
    return funcSize;
}

unsigned long CostCalculator::Region::getSize() {
    if(funcs.empty()) return 0;
    unsigned long maxFuncSize = 0;
    for(std::set<Function *>::iterator i = funcs.begin(); i != funcs.end(); i++) {
        Function *func = *i;
        if(funcSize[func] > maxFuncSize) maxFuncSize = funcSize[func];
    }
    return maxFuncSize;
}


std::string CostCalculator::Region::getDescription() {
    std::string r = "[ ";
    for(std::set<Function *>::iterator ii = funcs.begin(), ie = funcs.end(); ii != ie; ++ii) {
        Function *func = *ii;
        r += func->getName();
        r += " ";
    }
    r += "]";
    return r;
}

CostCalculator::CostCalculator(Pass *p, Module &m) : cg(p->getAnalysis<CallGraphWrapperPass>().getCallGraph()), mod(m), pathFinder(p, m) {
    pass = p;
}


void CostCalculator::getCallPaths() {
    Function *funcMain = mod.getFunction("main");
    assert(funcMain);
    callPaths = pathFinder.getCallPaths(funcMain);
    referredFuncs = pathFinder.getReferredFunctions();
    assert(!referredFuncs.empty());
}

unsigned long CostCalculator::calculateCost(unsigned long spmSize, MappingConfig *configs) {

    std::ifstream ifs;
    std::ofstream ofs;
    Region *src, *dest;

    funcSize.clear();

    getCallPaths();

    DEBUG(errs() << "\n\nCall Paths:\n");
    for (auto ii = callPaths.begin(), ie = callPaths.end(); ii != ie; ++ii) {
        for (auto ji = ii->begin(), je = ii->end(); ji != je; ++ji) {
            Function *func = ji->first;
            DEBUG(errs() << func->getName() << " " );
            /*
               DEBUG(errs() << func->getName() << " ( " );
               for (auto ki = ji->second.begin(), ke = ji->second.end(); ki != ke; ++ki)
               DEBUG(errs() << *ki << " ");
               DEBUG(errs() << " ) " );
               */

        }
        DEBUG(errs() << "\n");
    }

    DEBUG(errs() << "\n\n");


    ifs.open ("_func_size", std::ifstream::in | std::ifstream::binary);
    while (ifs.good()) {
        unsigned long size;
        std::string name;
        ifs >> name >> size;
        if (name.empty())
            continue;
        Function *func = mod.getFunction(name);
        assert(func);
        if (referredFuncs.find(func) == referredFuncs.end()) {
            DEBUG(errs() << "skip function " << func->getName() << "\n");
            continue;
        }
        funcSize[func]  = size;
        referredFuncs.insert(func);
        //errs() << func->getName() << " " << size << "\n";
    }

    // Initially place each function in a seperate region
    for(std::unordered_set<Function *>::iterator ii = referredFuncs.begin(), ie = referredFuncs.end(); ii != ie; ++ii) {
        Function *func = *ii;
        Region *region = new Region();
        region->addFunction(func);
        regions.insert(region);
        DEBUG(errs() << "making a region for: " << func->getName() << "\n");
    }

    // Try to merge regions until the overall size of regions can fit in the SPM
    unsigned long maxFuncSize = getMaxRegionSize();
    if (maxFuncSize > spmSize ) {
        errs() << "SPM size is not large enough. The maxium function size = " << maxFuncSize << ", SPM size = " << spmSize << "\n" ;
        exit (-1);
    }

    unsigned long cost;
    if(getRegionSizeSum() <= spmSize) cost = 0;
    if(configs) {
        configs->push_back(std::pair<unsigned int, unsigned int>(getRegionSizeSum(), cost));
    }
    while(getRegionSizeSum() > spmSize) {
        DEBUG(errs() << "\nSum of region size: " << getRegionSizeSum() << ", spm size: " << spmSize << "\n\n");
        cost = findMerger(src, dest);
        DEBUG(errs() << "Merge " << src->getDescription() << " and " << dest->getDescription() << "\n\n");
        dest->merge(src);
        regions.erase(src);
        if(configs) {
            configs->push_back(std::pair<unsigned int, unsigned int>(getRegionSizeSum(), cost));
        }
    }
    DEBUG(errs() << "Calculation finished" << "\n");
    DEBUG(errs() << "Sum of region size: " << getRegionSizeSum() << ", spm size: " << spmSize << "\n");
    DEBUG(errs() << "final cost: " << cost << "\n");
    DEBUG(errs() << "Final regions: ");
    for(std::set<Region *>::iterator i = regions.begin(); i != regions.end(); i++) {
        Region *region = *i;
        DEBUG(errs() << region->getDescription() << " ");
    }
    DEBUG(dump());
    errs() << "\n";
    return cost;
}


unsigned long CostCalculator::findMerger(Region* &src, Region* &dest) {
    unsigned long minCost = ~0;
    //for all possible region combinations
    DEBUG(errs() << "finding merge target among " << regions.size() << " regions\n");
    if(regions.size() <= 1) {
        return (*regions.begin())->getSize();
    }
    for(std::set<Region *>::iterator ii = regions.begin(), ie = regions.end(); ii != ie; ++ii) {
        std::set<Region *>::iterator in = ii;
        ++in;
        if(in == ie) break;
        Region *r1 = *ii;
        while(in != ie) {
            Region *r2 = *in;
            unsigned long cost = calculateMergerCost(r1, r2);
            if(cost < minCost) {
                src = r1;
                dest = r2;
                minCost = cost;
            }
            ++in;
        }
    }
    assert(src && dest);
    return minCost;
}


long CostCalculator::getNextSpmSize() {
    CostInfo costInfo = analyzeCost();

    if(costInfo.size() < 1) {
        return -1;
    }

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
    errs() << "\n";

    unsigned long srcSize, destSize;
    Function *f1 = maxConflictingFunctions.first;
    Function *f2 = maxConflictingFunctions.second;
    errs() << "f1: " << f1->getName() << ", f2: " << f2->getName() << "\n";
    srcSize = funcSize[f1];
    destSize = funcSize[f2];
    Function *ret_func = srcSize > destSize ? f1 : f2;
    unsigned long ret = funcSize[ret_func];
    errs() << "getNextSpmSize returns " << ret << ", which is the size of " << ret_func->getName() << "\n";
    return ret;
}


CostInfo CostCalculator::analyzeCost() {
    unsigned long maxCost = 0;
    unsigned long cost = 0;

    CostInfo costInfo;

    for (auto ii = callPaths.begin(), ie = callPaths.end(); ii != ie; ++ii) {
        PathType path;
        PathType tempPath = *ii;

        cost = 0;

        DEBUG(errs() << "Path:\t");

        for (size_t i = 0; i < tempPath.size(); ++i) {
            NodeType node = tempPath[i];
            Function *func = node.first;
            DEBUG(errs() << func->getName() << " ( " );
            for (auto ki = node.second.begin(), ke = node.second.end(); ki != ke; ++ki)
                DEBUG(errs() << *ki << " ");
            DEBUG(errs() << " ) " );

        }
        DEBUG(errs() << "\n\n");

        path = *ii;

        if (path.size() < 2)
            continue;

        // Step 4: Remove redundant adjacent functions calls

        bool converge = false;
        while (!converge) {
            //converge = true;
            // Remove reduant adjancent function calls in the same for loop or if both are not in any loop
            tempPath = path;
            path.clear();
            for (size_t i = 0; i < tempPath.size() - 1; ++i) {
                NodeType node = tempPath[i];
                NodeType nextNode = tempPath[i+1];
                // If the current function call if the next call is to the same function and  in the same (innermost) loop
                if (node.first == nextNode.first) {
                    BasicBlock *lpHeader1 = node.second.empty() ? NULL: node.second[0];
                    BasicBlock *lpHeader2 = nextNode.second.empty() ? NULL: nextNode.second[0];
                    if(lpHeader1 == lpHeader2) {
                        //converge = false;
                        continue;
                    }
                }
                path.push_back(node);
            }
            path.push_back(tempPath.back());


            if (path.size() < 2)
                break;
            // Remove loops with only one function call
            //tempPath = path;
            //path.clear();
            size_t size = path.size();
            for (size_t i = 0; i < size; ++i) {
                NodeType currentNode = path[i];
                size_t size1 = currentNode.second.size();
                // Skip the current function call if it is not in any loops
                if (!size1)
                    continue;
                std::vector <BasicBlock *> vec;
                // Go through the enclosing loops of the current node and remove loops that only contain the current node
                for (size_t j = 0; j < size1; ++j) {
                    BasicBlock *lpHeader1 = currentNode.second[j];
                    bool onlyCall = true;

                    // Keep the current node if it is in the same loop with the next node
                    if (i != size-1) {
                        NodeType nextNode = path[i+1];
                        size_t size2 = nextNode.second.size();
                        for (size_t k = 0; k < size2; ++k) {
                            BasicBlock *lpHeader2 = nextNode.second[k];
                            if (lpHeader2 ==  lpHeader1) {
                                vec.push_back(lpHeader1);
                                onlyCall = false;
                                break;
                            }
                        }
                    }
                    if (!onlyCall)
                        continue;
                    // Keep the current node if it is in the same loop with the previous node
                    if (i != 0) {
                        NodeType prevNode = path[i-1];
                        size_t size2 = prevNode.second.size();
                        for (size_t k = 0; k < size2; ++k) {
                            BasicBlock *lpHeader2 = prevNode.second[k];
                            if (lpHeader2 ==  lpHeader1) {
                                vec.push_back(lpHeader1);
                                onlyCall = false;
                                break;
                            }
                        }

                    }
                    if (!onlyCall)
                        continue;
                    //converge = false;
                }
                currentNode.second = vec;
            }

            //if (path.size() < 2)
            //break;

            if (tempPath == path)
                converge = true;

        }


        DEBUG(errs() << "\nAfter step 4:");
        DEBUG(errs() << "\t");
        for (size_t i = 0; i < path.size(); ++i) {
            NodeType node = path[i];
            Function *func = node.first;
            DEBUG(errs() << func->getName() << " (");
            for (auto ki = node.second.begin(), ke = node.second.end(); ki != ke; ++ki)
                DEBUG(errs() << *ki << " ");
            DEBUG(errs() << " ) " );
        }
        DEBUG(errs() << "\n\n");

        if (path.size() < 2)
            continue;



        //Step 5: Calculate the cost

        cost = 0;
        Function *prev, *current;
        prev = current = NULL;
        for (size_t i = 0; i < path.size(); ++i) {
            NodeType node = path[i];
            unsigned depth = 0;
            Function *func = node.first;
            size_t num = node.second.size();

            prev = current;
            current = func;

            prev = func;
            if(i+1 < path.size()) {
                current = path[i+1].first;
            } else {
                current = NULL;
            }

            if (num) {
                Function *last = NULL, *current = NULL;
                for (size_t i = 0; i < num; ++i) {
                    BasicBlock *lpHeader = node.second[i];
                    current = lpHeader->getParent();
                    DEBUG(errs() << current->getName() << " depth: " << depth << " ");
                    if (current == last) {
                        DEBUG(errs() << "\n");
                        continue;
                    }
                    LoopInfo &lpi = pass->getAnalysis<LoopInfoWrapperPass>(*current).getLoopInfo();
                    unsigned lpDepth = lpi.getLoopDepth(lpHeader);
                    DEBUG(errs() << lpDepth << "\n");
                    depth += lpDepth;
                    last = current;

                }
            }
            DEBUG(errs() << func->getName() << " ( " << depth << " )\n");
            unsigned long numExec = (unsigned long)pow(10, (double)depth);

            unsigned long currentCost = funcSize[func] * numExec;

            /*
               if(prev && current) {
               errs() << prev->getName() << "->" << current->getName();
               errs() << " depth: " << depth << " currentCost: " << currentCost << "\n";
               }
               */

            cost += currentCost;

            if(prev && current) {
                std::pair <Function *, Function *> key;
                if(prev < current) key = std::make_pair(prev, current);
                else key = std::make_pair(current, prev);
                //key = std::make_pair(prev, current);
                if(costInfo.find(key) == costInfo.end()) {
                    costInfo[key] = currentCost;
                } else {
                    costInfo[key] += currentCost;
                }
            }
        }

        /*
           for(auto i = costInfo.begin(); i != costInfo.end(); i++) {
           std::pair <Function *, Function *> key = i->first;
           unsigned long conflict = i->second;
           errs() << key.first->getName() << " <-> " << key.second->getName() << "\t" << conflict << "\n";
           }
           */

        DEBUG(errs() << "\n");
        DEBUG(errs() << "\nAfter step 5: cost = " << cost << "\n");
        if (cost > maxCost) maxCost = cost;
    }
    DEBUG(errs() << "\tFinal cost = " << maxCost << "\n");
    return costInfo;
}


unsigned long CostCalculator::calculateMergerCost(Region *r1, Region *r2) {
    unsigned long maxCost = 0;
    unsigned long cost = 0;

    DEBUG(errs() << "calculate cost for merging " << r1->getDescription() << " and " << r2->getDescription() << "\n");

    for (auto ii = callPaths.begin(), ie = callPaths.end(); ii != ie; ++ii) {
        PathType path;
        PathType tempPath = *ii;

        cost = 0;

        DEBUG(errs() << "Path:\t");

        for (size_t i = 0; i < tempPath.size(); ++i) {
            NodeType node = tempPath[i];
            Function *func = node.first;
            DEBUG(errs() << func->getName() << " ( " );
            for (auto ki = node.second.begin(), ke = node.second.end(); ki != ke; ++ki)
                DEBUG(errs() << *ki << " ");
            DEBUG(errs() << " ) " );

        }
        DEBUG(errs() << "\n\n");


        // Step 2: Remove irrevelant nodes

        for (size_t i = 0; i < tempPath.size(); ++i) {
            NodeType p = tempPath[i];
            Function *func = p.first;
            if ( r1->hasFunction(func) || r2->hasFunction(func) )
                path.push_back(p);
        }
        //assert (path.size() >= 2);

        DEBUG(errs() << "\nAfter step 2:");
        DEBUG(errs() << "\t");
        for (size_t i = 0; i < path.size(); ++i) {
            NodeType node = path[i];
            Function *func = node.first;
            DEBUG(errs() << func->getName() << " (");
            for (auto ki = node.second.begin(), ke = node.second.end(); ki != ke; ++ki)
                DEBUG(errs() << *ki << " ");
            DEBUG(errs() << " ) " );
        }
        DEBUG(errs() << "\n\n");

        if (path.size() < 2)
            continue;

        // Step 3 has been taken care when the paths are constructed

        // Step 4: Remove redundant adjacent functions calls

        bool converge = false;
        while (!converge) {
            //converge = true;
            // Remove reduant adjancent function calls in the same for loop or if both are not in any loop
            tempPath = path;
            path.clear();
            for (size_t i = 0; i < tempPath.size() - 1; ++i) {
                NodeType node = tempPath[i];
                NodeType nextNode = tempPath[i+1];
                // If the current function call if the next call is to the same function and  in the same (innermost) loop
                if (node.first == nextNode.first) {
                    BasicBlock *lpHeader1 = node.second.empty() ? NULL: node.second[0];
                    BasicBlock *lpHeader2 = nextNode.second.empty() ? NULL: nextNode.second[0];
                    if(lpHeader1 == lpHeader2) {
                        //converge = false;
                        continue;
                    }
                }
                path.push_back(node);
            }
            path.push_back(tempPath.back());


            if (path.size() < 2)
                break;
            // Remove loops with only one function call
            //tempPath = path;
            //path.clear();
            size_t size = path.size();
            for (size_t i = 0; i < size; ++i) {
                NodeType currentNode = path[i];
                size_t size1 = currentNode.second.size();
                // Skip the current function call if it is not in any loops
                if (!size1)
                    continue;
                std::vector <BasicBlock *> vec;
                // Go through the enclosing loops of the current node and remove loops that only contain the current node
                for (size_t j = 0; j < size1; ++j) {
                    BasicBlock *lpHeader1 = currentNode.second[j];
                    bool onlyCall = true;

                    // Keep the current node if it is in the same loop with the next node
                    if (i != size-1) {
                        NodeType nextNode = path[i+1];
                        size_t size2 = nextNode.second.size();
                        for (size_t k = 0; k < size2; ++k) {
                            BasicBlock *lpHeader2 = nextNode.second[k];
                            if (lpHeader2 ==  lpHeader1) {
                                vec.push_back(lpHeader1);
                                onlyCall = false;
                                break;
                            }
                        }
                    }
                    if (!onlyCall)
                        continue;
                    // Keep the current node if it is in the same loop with the previous node
                    if (i != 0) {
                        NodeType prevNode = path[i-1];
                        size_t size2 = prevNode.second.size();
                        for (size_t k = 0; k < size2; ++k) {
                            BasicBlock *lpHeader2 = prevNode.second[k];
                            if (lpHeader2 ==  lpHeader1) {
                                vec.push_back(lpHeader1);
                                onlyCall = false;
                                break;
                            }
                        }

                    }
                    if (!onlyCall)
                        continue;
                    //converge = false;
                }
                currentNode.second = vec;
            }

            //if (path.size() < 2)
            //break;

            if (tempPath == path)
                converge = true;

        }


        DEBUG(errs() << "\nAfter step 4:");
        DEBUG(errs() << "\t");
        for (size_t i = 0; i < path.size(); ++i) {
            NodeType node = path[i];
            Function *func = node.first;
            DEBUG(errs() << func->getName() << " (");
            for (auto ki = node.second.begin(), ke = node.second.end(); ki != ke; ++ki)
                DEBUG(errs() << *ki << " ");
            DEBUG(errs() << " ) " );
        }
        DEBUG(errs() << "\n\n");

        if (path.size() < 2)
            continue;



        //Step 5: Calculate the cost

        cost = 0;
        for (size_t i = 0; i < path.size(); ++i) {
            NodeType node = path[i];
            unsigned depth = 0;
            Function *func = node.first;
            size_t num = node.second.size();

            if (num) {
                Function *last = NULL, *current = NULL;
                for (size_t i = 0; i < num; ++i) {
                    BasicBlock *lpHeader = node.second[i];
                    current = lpHeader->getParent();
                    DEBUG(errs() << current->getName() << " depth: " << depth << " ");
                    if (current == last) {
                        DEBUG(errs() << "\n");
                        continue;
                    }
                    LoopInfo &lpi = pass->getAnalysis<LoopInfoWrapperPass>(*current).getLoopInfo();
                    unsigned lpDepth = lpi.getLoopDepth(lpHeader);
                    DEBUG(errs() << lpDepth << "\n");
                    depth += lpDepth;
                    last = current;

                }
            }
            DEBUG(errs() << func->getName() << " ( " << depth << " )\n");
            unsigned long numExec = (unsigned long)pow(10, (double)depth);
            cost += funcSize[func] * numExec;
        }
        DEBUG(errs() << "\n");
        DEBUG(errs() << "\nAfter step 5: cost = " << cost << "\n");
        if (cost > maxCost) maxCost = cost;
    }
    DEBUG(errs() << "\tFinal cost = " << maxCost << "\n");
    return maxCost;
}

unsigned long CostCalculator::getRegionSizeSum() {
    unsigned long size = 0;
    for(std::set<Region *>::iterator ii = regions.begin(), ie = regions.end(); ii != ie; ++ii) {
        Region *region = *ii;
        size += region->getSize();
    }
    return size;
}

unsigned long CostCalculator::getMaxRegionSize() {
    unsigned long maxSize = 0;
    for(std::set<Region *>::iterator ii = regions.begin(), ie = regions.end(); ii != ie; ++ii) {
        Region *region = *ii;
        unsigned long size =  region->getSize();
        if (size > maxSize) maxSize = size;
    }
    return maxSize;
}

void CostCalculator::dump() {
    unsigned long regionId = 0;
    std::ofstream ofs;
    ofs.open ("_mapping", std::ofstream::out | std::ofstream::trunc | std::ofstream::binary);
    ofs << regions.size() << "\n";
    for(std::set<Region *>::iterator ii = regions.begin(), ie = regions.end(); ii != ie; ++ii) {
        Region *region = *ii;
        std::set<Function *> funcs = region->getFunctions();
        for (std::set<Function*>::iterator ji = funcs.begin(), je = funcs.end(); ji != je; ++ji) {
            Function *func = *ji;
            ofs << func->getName().str() << " " << regionId << "\n";
        }
        ++regionId;
    }
}
