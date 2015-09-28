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
#ifndef _RELATIVEMINMAX_H_
#define _RELATIVEMINMAX_H_

#include "LoopInfoExpr.h"
#include "PythonInterface.h"

#include "llvm/Pass.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"

#include <python2.7/Python.h>

#include <vector>


class RelativeMinMax : public FunctionPass {
public:
	static char ID;
	RelativeMinMax() : FunctionPass(ID) { }

	virtual void getAnalysisUsage(AnalysisUsage &AU) const;
	virtual bool runOnFunction(Function &F);

	bool getMinMaxRelativeTo(Loop *L, Value *V, Expr &Min, Expr &Max);
	bool getMinMax(Expr Ex, Expr &Min, Expr &Max);

private:
	void addMinMax(Expr PrevMin, Expr PrevMax, Expr OtherMin, Expr OtherMax, Expr &Min, Expr &Max);
	void mulMinMax(Expr PrevMin, Expr PrevMax, Expr OtherMin, Expr OtherMax, Expr &Min, Expr &Max);
	
	LoopInfo *LI_;
	DominatorTree *DT_;
	SymPyInterface *SPI_;
	LoopInfoExpr *LIE_;
};

#endif
