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
#include "RelativeMinMax.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

/* ****************************************************************** */
/* ****************************************************************** */

static cl::opt<bool>	ClDebug("rel-minmax-debug", cl::desc("Enable debugging for the relative min/max pass"),
						cl::Hidden, cl::init(false));

static RegisterPass<RelativeMinMax> X("rel-minmax", "Location-relative inference of max and mins");
char RelativeMinMax::ID = 0;

#define RMM_DEBUG(X) { if (ClDebug) {X;} }

/* ****************************************************************** */
/* ****************************************************************** */


void RelativeMinMax::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.addRequired<DominatorTree>();
	AU.addRequired<LoopInfoExpr>();
	AU.addRequired<SymPyInterface>();
	AU.setPreservesAll();
}


bool RelativeMinMax::runOnFunction(Function &F) {
	DT_  = &getAnalysis<DominatorTree>();
	LIE_ = &getAnalysis<LoopInfoExpr>();
	SPI_ = &getAnalysis<SymPyInterface>();
	return false;
}

void RelativeMinMax::addMinMax(Expr PrevMin, Expr PrevMax, Expr OtherMin, Expr OtherMax, Expr &Min, Expr &Max) {
	Min = PrevMin + OtherMin;
	Max = PrevMax + OtherMax;
}


void RelativeMinMax::mulMinMax(Expr PrevMin, Expr PrevMax, Expr OtherMin, Expr OtherMax, Expr &Min, Expr &Max) {
	if ( OtherMin == OtherMax && OtherMin.isConstant() ) {
		if ( OtherMin.isPositive() ) {
			Min = PrevMin * OtherMin;
			Max = PrevMax * OtherMax;
		}
		else {
			Min = PrevMax * OtherMax;
			Max = PrevMin * OtherMin;
		}
	}
	
	else if ( PrevMin == PrevMax && PrevMin.isConstant() ) {
		if ( PrevMin.isPositive() ) {
			Min = PrevMin * OtherMin;
			Max = PrevMax * OtherMax;
		}
		else {
			Min = PrevMax * OtherMax;
			Max = PrevMin * OtherMin;
		}
	}
	
	else {
		Min = (PrevMin * OtherMin).min(PrevMin * OtherMax)
		                          .min(OtherMin * PrevMax);
		
		Max = (PrevMax * OtherMin).max(PrevMax * OtherMax)
		                          .max(OtherMax * PrevMin);
	}
}


bool RelativeMinMax::getMinMax(Expr Ex, Expr &Min, Expr &Max) {
	if ( Ex.isConstant() ) {
		Min = Ex;
		Max = Ex;
	}


	else if ( Ex.isSymbol() ) {
		// Bounds of induction variables have special treatment.
		if (  PHINode *Phi = dyn_cast<PHINode>( Ex.getSymbolValue() )  ) {
			if ( Loop *L = LIE_->getLoopForInductionVariable(Phi) ) {
				Expr IndvarStart, IndvarEnd, IndvarStep;
				LIE_->getLoopInfo(L, Phi, IndvarStart, IndvarEnd, IndvarStep);
				
				SmallVector<BasicBlock*,4> Eblocks;
				L->getExitingBlocks(Eblocks);

				if(!Eblocks[0]) {
					RMM_DEBUG(dbgs() << "RelativeMinMax: Problem in the Eblocks vector; we probably couldn't get the exiting blocks of the loop: "<< L << "\n" << "RelativeMinMax: So, we're aborting the min/max evaluating now...\n");
					return false;
				}
				
				TerminatorInst *TI = Eblocks[0]->getTerminator(); //simplification - we only look at the first exit
				BranchInst *BI = cast<BranchInst>(TI);
	
				ICmpInst *ICI = cast<ICmpInst>( BI->getCondition() );

				Expr MinStart, MaxStart, MinEnd, MaxEnd;
				
				if ( !getMinMax(IndvarStart, MinStart, MaxStart) || !getMinMax(IndvarEnd, MinEnd, MaxEnd) ) {
					RMM_DEBUG(dbgs() << "RelativeMinMax: Could not infer min/max for " << IndvarStart << " and/or " << IndvarEnd << "\n");
					return false;
				}

				// FIXME: we should wrap the loop in a conditional so that the following
				// min/max assumptions always hold.

				switch ( ICI->getPredicate() ) {
					case CmpInst::ICMP_SLT:
					case CmpInst::ICMP_ULT:
					case CmpInst::ICMP_SLE:
					case CmpInst::ICMP_ULE:
					case CmpInst::ICMP_EQ:  // (added later)		
						Min = MinStart;
						Max = MaxEnd;
						break;
				
					case CmpInst::ICMP_SGT:
					case CmpInst::ICMP_UGT:
					case CmpInst::ICMP_UGE:
					case CmpInst::ICMP_SGE:
						Min = MaxStart;
						Max = MinEnd;
						break;
				
					default:
						//llvm_unreachable("Invalid comparison predicate");
						RMM_DEBUG(dbgs() << "RelativeMinMax: couldn't get min/max for induction variable " << *Phi << "\nRelativeMinMax: invalid comparison predicate\n");
						return false;
				}
				
				RMM_DEBUG(dbgs() << "RelativeMinMax: min/max for induction variable " << *Phi << ": " << Min << ", " << Max << "\n");
				return true;
				
			} //if ( Loop *L = LIE_->getLoopForInductionVariable(Phi) )
		} //if (  PHINode *Phi = dyn_cast<PHINode>( Ex.getSymbolValue() )  )
		Min = Ex;
		Max = Ex;
	} //else if ( Ex.isSymbol() )


	else if ( Ex.isAdd() ) {
		for (auto SubEx : Ex) {
			Expr TmpMin, TmpMax;

			if (!getMinMax(SubEx, TmpMin, TmpMax)) {
				RMM_DEBUG(dbgs() << "RelativeMinMax: Could not infer min/max for " << SubEx << "\n");
				return false;
			}
			
			addMinMax(TmpMin, TmpMax, Min, Max, Min, Max);
		}// for
	}

	else if ( Ex.isMul() ) {
		Min = Expr::InvalidExpr();
		
		for (auto SubEx : Ex) {
			Expr TmpMin, TmpMax;
		
			if ( !getMinMax(SubEx, TmpMin, TmpMax) ) {
				RMM_DEBUG(dbgs() << "RelativeMinMax: Could not infer min/max for " << SubEx << "\n");
				return false;
			}
			
			if ( !Min.isValid() ) {
				Min = TmpMin;
				Max = TmpMax;
			}
			else {
				mulMinMax(TmpMin, TmpMax, Min, Max, Min, Max);
			}
		} //for
	}

	else if ( Ex.isPow() ) {
		if ( !Ex.getPowExp().isConstant() ) {
			RMM_DEBUG(dbgs() << "RelativeMinMax: non-constant exponent\n");
			return false;
		}
		
		Expr BaseMin, BaseMax;
		
		if ( !getMinMax(Ex.getPowBase(), BaseMin, BaseMax) ) {
			RMM_DEBUG(dbgs() << "RelativeMinMax: Could not infer min/max for " << Ex.getPowBase() << "\n");
			return false;
		}
		
		if ( Ex.getPowExp().isPositive() ) {
			Min = BaseMin ^ Ex.getPowExp();
			Max = BaseMax ^ Ex.getPowExp();
		}
		else {
			Min = BaseMax ^ Ex.getPowExp();
			Max = BaseMin ^ Ex.getPowExp();
		}
	}
  
	else if ( Ex.isMin() ) {
		Expr MinFirst, MinSecond, Bogus;
		getMinMax(Ex.at(0), MinFirst,  Bogus);
		getMinMax(Ex.at(1), MinSecond, Bogus);
		
		Min = Max = MinFirst.min(MinSecond);
	}
  
	else if ( Ex.isMax() ) {
		Expr MaxFirst, MaxSecond, Bogus;
		getMinMax(Ex.at(0), MaxFirst,  Bogus);
		getMinMax(Ex.at(1), MaxSecond, Bogus);
		
		Min = Max = MaxFirst.max(MaxSecond);
	}

	else {
		RMM_DEBUG(dbgs() << "RelativeMinMax: unhandled expression: " << Ex << "\n");
		return false;
	}
	
	return true;
}


bool RelativeMinMax::getMinMaxRelativeTo(Loop *L, Value *V, Expr &Min, Expr &Max) {
	Expr Ex = LIE_->getExprForLoop(L, V);

	return getMinMax(Ex, Min, Max);
}
