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
#include "RelativeExecutions.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

/* ****************************************************************** */
/* ****************************************************************** */

static cl::opt<bool>	ClDebug("rel-exec-debug", cl::desc("Enable debugging for the relative execution pass"),
						cl::Hidden, cl::init(false));

static RegisterPass<RelativeExecutions> X("rel-exec", "Location-relative execution count inference");
char RelativeExecutions::ID = 0;

#define RE_DEBUG(X) { if (ClDebug) {X;} }

/* ****************************************************************** */
/* ****************************************************************** */


void RelativeExecutions::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.addRequired<DominatorTree>();
	AU.addRequired<LoopInfo>();
	AU.addRequired<LoopInfoExpr>();
	AU.addRequired<SymPyInterface>();
	AU.setPreservesAll();
}


bool RelativeExecutions::runOnFunction(Function &F) {
	DT_  = &getAnalysis<DominatorTree>();
	LI_  = &getAnalysis<LoopInfo>();
	LIE_ = &getAnalysis<LoopInfoExpr>();
	SPI_ = &getAnalysis<SymPyInterface>();
	return false;
}


Expr RelativeExecutions::getExecutionsRelativeTo(Loop *L, Loop *Toplevel, Loop *&Final) {
	PHINode *Indvar;
	Expr IndvarStart, IndvarEnd, IndvarStep;

	if ( !LIE_->getLoopInfo(L, Indvar, IndvarStart, IndvarEnd, IndvarStep) ) {
		RE_DEBUG(dbgs() << "RelativeExecutions: could not get loop info for loop at " << L->getHeader()->getName() << "\n");
		return Expr::InvalidExpr();
	}

	RE_DEBUG(dbgs() << "RelativeExecutions: induction variable, start, end, step: "	<< *Indvar << " => (" << IndvarStart << ", " << IndvarEnd << ", +" << IndvarStep << ")\n");

	
	PyObject *Summation = nullptr;
	{
		PyObject *IndvarObj       = SPI_->conv(Indvar);
		PyObject *IndvarStepObj   = SPI_->conv(IndvarStep);
		PyObject *IndvarStartObj  = SPI_->conv(IndvarStart);
		PyObject *IndvarEndObj    = SPI_->conv(IndvarEnd);

		//assert( IndvarObj && IndvarStepObj && IndvarStartObj && IndvarEndObj && "Conversion error" );

		if ( (IndvarObj == nullptr) || (IndvarStepObj == nullptr) || (IndvarStartObj == nullptr) || (IndvarEndObj == nullptr) ) {
			RE_DEBUG(dbgs() << "RelativeExecutions: some PyObject is null - returning InvalidExpr\n");
			return Expr::InvalidExpr();			
		}

		PyObject *Summand = SPI_->inverse(IndvarStepObj);
		Summation = SPI_->summation(Summand, IndvarObj, IndvarStartObj, IndvarEndObj);
		
		RE_DEBUG(dbgs() << "RelativeExecutions: summation for loop at " << L->getHeader()->getName() << " is: " << *Summation << "\n");
	}

	Expr Ret;
  
	while (  (Final = L) && ( L = L->getParentLoop() )  ) {
    
		if ( !LIE_->getLoopInfo(L, Indvar, IndvarStart, IndvarEnd, IndvarStep) ) {
			RE_DEBUG(dbgs() << "RelativeExecutions: could not get loop info for loop at " << L->getHeader()->getName() << "\n");
			
			Ret = SPI_->conv(Summation); 
			RE_DEBUG(dbgs() << "RelativeExecutions: partial success; returning " << Ret << "\n");
			
			return Ret;
		}

		RE_DEBUG(dbgs() << "RelativeExecutions: induction variable, start, end, step: " << *Indvar << " => (" << IndvarStart << ", " << IndvarEnd << ", +" << IndvarStep << ")\n");

		Expr SummationEx = SPI_->conv(Summation);
		Summation = SPI_->conv(SummationEx);

		PyObject *IndvarObj       = SPI_->conv(Indvar);
		PyObject *IndvarStepObj   = SPI_->conv(IndvarStep);
		PyObject *IndvarStartObj  = SPI_->conv(IndvarStart);
		PyObject *IndvarEndObj    = SPI_->conv(IndvarEnd);
		
		//assert(IndvarObj && IndvarStepObj && IndvarStartObj && IndvarEndObj && "Conversion error");

		if ( (IndvarObj == nullptr) || (IndvarStepObj == nullptr) || (IndvarStartObj == nullptr) || (IndvarEndObj == nullptr) ) {
			RE_DEBUG(dbgs() << "RelativeExecutions: some PyObject is null - returning InvalidExpr\n");
			return Expr::InvalidExpr();			
		}

		PyObject *Inverse = SPI_->inverse(IndvarStepObj);

		if (Inverse == nullptr) {
			RE_DEBUG(dbgs() << "RelativeExecutions: some PyObject is null - returning InvalidExpr\n");
			return Expr::InvalidExpr();			
		}

		PyObject *Summand = SPI_->mul(Summation, Inverse);
		
		Summation = SPI_->summation(Summand, IndvarObj, IndvarStartObj, IndvarEndObj);
		Summation = SPI_->expand(Summation);
		
		RE_DEBUG(dbgs() << "RelativeExecutions: summation for loop at " << L->getHeader()->getName() << " is: " << *Summation << "\n");

		if (L == Toplevel)
			break;
	} //while (  (Final = L) && ( L = L->getParentLoop() )  ) 


	if ( L == Toplevel || !Toplevel ) {
		Expr Ret_final = SPI_->conv(Summation);
		
		RE_DEBUG(dbgs() << "RelativeExecutions: success; returning " << Ret_final << "\n");
		
		return Ret_final;
	}

	else {
		RE_DEBUG(dbgs() << "RelativeExecutions: toplevel loop has not been reached\n");
		return Expr::InvalidExpr();
	}

}
