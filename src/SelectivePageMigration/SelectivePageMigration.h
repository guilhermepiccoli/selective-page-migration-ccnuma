/* *********************************************************************
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * AND the GNU Lesser General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors of this code:
 *   Henrique Nazaré Santos  <hnsantos@gmx.com>
 *   Guilherme G. Piccoli    <porcusbr@gmail.com>
 *
 * Publication:
 *   Compiler support for selective page migration in NUMA
 *   architectures. PACT 2014: 369-380.
 *   <http://dx.doi.org/10.1145/2628071.2628077>
********************************************************************* */
#ifndef _SELECTIVEPAGEMIGRATION_H_
#define _SELECTIVEPAGEMIGRATION_H_

#include "PythonInterface.h"
#include "ReduceIndexation.h"
#include "RelativeExecutions.h"
#include "RelativeMinMax.h"
#include "GetWorkerFunctions.h"

#include "llvm/Pass.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"

#include <unordered_set>
#include <unordered_map>

class SelectivePageMigration : public FunctionPass {
public:
	static char ID;
	SelectivePageMigration() : FunctionPass(ID) { }

	virtual void getAnalysisUsage(AnalysisUsage &AU) const;
	virtual bool runOnFunction(Function &F);

private:
	DataLayout         *DL_;
	DominatorTree      *DT_;
	LoopInfo           *LI_;
	ReduceIndexation   *RI_;
	RelativeExecutions *RE_;
	RelativeMinMax     *RMM_;
	SymPyInterface     *SPI_;
	GetWorkerFunctions *GWF_;
	
	LLVMContext *Context_;
	Module      *Module_;
	Constant    *ReuseFn_;
	Constant    *ReuseFnDestroy_;

	bool generateCallFor(Loop *L, Instruction *I);
	bool canGenerateExprAt(Expr *Ex, BasicBlock *BB);

	struct CallInfo {
		BasicBlock *Preheader, *Final;
		Value *Array, *Min, *Max, *Reuse;

		bool operator==(const CallInfo &Other) const {
			return Preheader == Other.Preheader && Array == Other.Array;
		}
	};

	struct CallInfoHasher {
		size_t operator()(const CallInfo &CI) const {
			return (size_t)((long)CI.Preheader + (long)CI.Array);
		}
	};

	std::unordered_set<CallInfo, CallInfoHasher> Calls_;

	std::unordered_map<Value*, Value*> LoadsToInsert;
};

#endif
