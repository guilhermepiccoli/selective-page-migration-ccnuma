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
#ifndef _LOOPINFOEXPR_H_
#define _LOOPINFOEXPR_H_

#include "Expr.h"
#include "PythonInterface.h"

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Instructions.h"


class LoopInfoExpr : public FunctionPass {
public:
	static char ID;
	LoopInfoExpr() : FunctionPass(ID) { }

	static bool IsLoopInvariant(Loop *L, Expr Ex);

	virtual void getAnalysisUsage(AnalysisUsage &AU) const;
	virtual bool runOnFunction(Function &F);

	bool isInductionVariable(PHINode *Phi);
	Loop *getLoopForInductionVariable(PHINode *Phi);

	// Builds an expression for V, stopping at loop-invariant atoms.
	Expr getExprForLoop(Loop *L, Value *V);
	
	// Builds an expression for V, stopping at any value that is not an instruction.
	Expr getExpr(Value *V);

	// Returns the induction variable for the given loop & its start, end, & step.
	bool getLoopInfo(Loop *L, PHINode *&Indvar, Expr &IndvarStart, Expr &IndvarEnd, Expr &IndvarStep);

private:
	PHINode *getSingleLoopVariantPhi(Loop *L, Expr Ex);
	
	LoopInfo *LI_;
	SymPyInterface *SPI_;
};

#endif
