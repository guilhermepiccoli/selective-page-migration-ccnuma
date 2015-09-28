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
#include "LoopInfoExpr.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/SmallVector.h"

/* ****************************************************************** */
/* ****************************************************************** */

static cl::opt<bool>	ClDebug("loop-info-expr-debug", cl::desc("Enable debugging for the loop information pass"),
						cl::Hidden, cl::init(false));

static RegisterPass<LoopInfoExpr> X("loop-info-expr", "Loop information");
char LoopInfoExpr::ID = 0;

#define LIE_DEBUG(X) { if (ClDebug) {X;} }

/* ****************************************************************** */
/* ****************************************************************** */


bool LoopInfoExpr::IsLoopInvariant(Loop *L, Expr Ex) {
	auto Symbols = Ex.getSymbols();
	
	for (auto& Sym : Symbols) {
		if (  !L->isLoopInvariant( Sym.getSymbolValue() )  )
			return false;
	}
	
	return true;
}


void LoopInfoExpr::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.addRequired<LoopInfo>();
	AU.addRequired<SymPyInterface>();
	AU.setPreservesAll();
}


bool LoopInfoExpr::runOnFunction(Function &F) {
	LI_ = &getAnalysis<LoopInfo>();
	SPI_ = &getAnalysis<SymPyInterface>();
	return false;
}


bool LoopInfoExpr::isInductionVariable(PHINode *Phi) {
	if (  !LI_->isLoopHeader( Phi->getParent() )  )
		return false;

	Loop *L = LI_->getLoopFor( Phi->getParent() );
	
	PHINode *Indvar;
	Expr IndvarStart, IndvarEnd, IndvarStep;
	
	return ( getLoopInfo(L, Indvar, IndvarStart, IndvarEnd, IndvarStep) ) && (Phi == Indvar);
}


Loop *LoopInfoExpr::getLoopForInductionVariable(PHINode *Phi) {
	if (  !LI_->isLoopHeader( Phi->getParent() )  )
		return nullptr;

	Loop *L = LI_->getLoopFor( Phi->getParent() );
	
	PHINode *Indvar;
	Expr IndvarStart, IndvarEnd, IndvarStep;

	return ( getLoopInfo(L, Indvar, IndvarStart, IndvarEnd, IndvarStep) && (Phi == Indvar) ) ? L : nullptr;
}


Expr LoopInfoExpr::getExprForLoop(Loop *L, Value *V) {
	
	if ( L && L->isLoopInvariant(V) )
		return Expr(V);

	else if ( !L && !isa<Instruction>(V) )
		return Expr(V);

	Instruction *I = cast<Instruction>(V);

	switch ( I->getOpcode() ) {
		case Instruction::Add:
			return getExprForLoop( L, I->getOperand(0) ) + getExprForLoop( L, I->getOperand(1) );
	
		case Instruction::Sub:
			return getExprForLoop( L, I->getOperand(0) ) - getExprForLoop( L, I->getOperand(1) );
	
		case Instruction::Mul:
			return getExprForLoop( L, I->getOperand(0) ) * getExprForLoop( L, I->getOperand(1) );
	
		case Instruction::SDiv:
		case Instruction::UDiv:
			return getExprForLoop( L, I->getOperand(0) ) / getExprForLoop( L, I->getOperand(1) );
	
		case Instruction::SExt:
		case Instruction::ZExt:
		case Instruction::Trunc:
			return getExprForLoop( L, I->getOperand(0) );
	
		default:
			return Expr(V);
	}
}


Expr LoopInfoExpr::getExpr(Value *V) {
	return getExprForLoop(nullptr, V);
}


bool LoopInfoExpr::getLoopInfo(Loop *L, PHINode *&Indvar, Expr &IndvarStart, Expr &IndvarEnd, Expr &IndvarStep) {


	SmallVector<BasicBlock*,4> Eblocks;
	L->getExitingBlocks(Eblocks); //trick that allow us to analyze loops with more than 1 exiting blocks...

	if(!Eblocks[0]) {
		LIE_DEBUG(dbgs() << "LoopInfoExpr: Problem in the Eblocks vector; we probably couldn't get the exiting blocks of the loop: "<< *L << "\n" << "LoopInfoExpr: So, we're aborting the getLoopInfo() now...\n");
		return false;
	}
	
	TerminatorInst *TI = Eblocks[0]->getTerminator(); //..but still a simplification, since we only analyze the first exiting block
	
	BranchInst *BI = dyn_cast<BranchInst>(TI);
	if (!BI)
		return false;

	ICmpInst *ICI = dyn_cast<ICmpInst>( BI->getCondition() );
	if (!ICI)
		return false;

	switch ( ICI->getPredicate() ) {
		case CmpInst::ICMP_SLT: // signed <
		case CmpInst::ICMP_SGT: // signed >
		case CmpInst::ICMP_ULT: // unsigd <
		case CmpInst::ICMP_UGT: // unsigd >
		case CmpInst::ICMP_SLE: // signed <=
		case CmpInst::ICMP_SGE: // signed >=
		case CmpInst::ICMP_ULE: // unsigd <=
		case CmpInst::ICMP_UGE: // unsigd >=
		case CmpInst::ICMP_EQ:  //  ==	(added later)		
			break;
		
		default:
			LIE_DEBUG(dbgs() << "LoopInfoExpr: invalid loop comparison predicate\n");
			return false;
	}

	//Get the toplevel loop and use it to generate all lasting expressions.
	Loop *Toplevel = L;
	while ( Toplevel->getParentLoop() )
		Toplevel = Toplevel->getParentLoop();

	/* Get the expression that gives the LHS. If all the atoms are
	 * loop-invariant except for a single phi-node, RHS is
	 * loop-invariant, and the found phi-node varies with a pattern
	 * i = i + <loop invariant expression>, then we've found the
	 * induction variable.
	 */ 

	Expr LHS = getExprForLoop(Toplevel, ICI->getOperand(0));
	Expr RHS = getExprForLoop(Toplevel, ICI->getOperand(1));

	LIE_DEBUG(dbgs() << "LoopInfoExpr: toplevel LHS & RHS: " << LHS << ", " << RHS << "\n");
	
	if ( !LHS.isValid() || !RHS.isValid() )
		return false;

	Expr Var, Invar;
	if ( !IsLoopInvariant(L, LHS) && IsLoopInvariant(L, RHS) ) {
		Var		=	LHS;
		Invar	=	RHS;
	}

	else if ( IsLoopInvariant(L, LHS) && !IsLoopInvariant(L, RHS) ) {
		Var		=	RHS;
		Invar	=	LHS;
	}


	else {
		
		LIE_DEBUG(dbgs() << "LoopInfoExpr: LHS & RHS have apparently incorrect loop-variance...\n" << "LoopInfoExpr: but.....we are trying to not give up yet, by evaluating if it's a load instruction with global operand\n");

		Value *LHS_Val=nullptr, *RHS_Val=nullptr;
		
		for ( auto &Sym : LHS.getSymbols() ) {
			Value *Vld = Sym.getSymbolValue();
			
			if ( Vld != nullptr && isa<LoadInst>(Vld) ) {
				LHS_Val = Vld;
				
				Var		=	RHS;
				Invar	=	LHS; //global value is considered the invariable one by us - if it's not global, we catch it later, in the SelectivePageMigration.cpp, inside generateCallFor().
				
				break;
			}
		}
		
		if (LHS_Val == nullptr) { //let's try RHS!
			for ( auto &Sym : RHS.getSymbols() ) {
				Value *Vld = Sym.getSymbolValue();
				
				if ( Vld != nullptr && isa<LoadInst>(Vld) ) {
					RHS_Val = Vld;
					
					Var		=	LHS;
					Invar	=	RHS;
					
					break;
				}
			}
				
		}
		 
		if (LHS_Val == nullptr && RHS_Val == nullptr) {
			LIE_DEBUG(dbgs() << "LoopInfoExpr: Unfortunately...aborting this pass here. Our approach have failed...it was impossible to determine which is the invariant-to-loop value: RHS (" << RHS << ") or LHS (" << LHS << ")\n\n");
			return false;
		}
	
	}

	if ( PHINode *Phi = getSingleLoopVariantPhi(L, Var) ) {

		BasicBlock *LPreheader = L->getLoopPreheader();
		BasicBlock *LLatch = L->getLoopLatch();
		
		if (!LPreheader || !LLatch) {
			LIE_DEBUG(dbgs() << "LoopInfoExpr: invalid loop preheader or latch...aborting this step.\n");
			return false;
		}
		
		Value *PreheaderIncoming	=	Phi->getIncomingValueForBlock( LPreheader );
		Value *LatchIncoming		=	Phi->getIncomingValueForBlock( LLatch );

		if ( !L->isLoopInvariant(PreheaderIncoming) || L->isLoopInvariant(LatchIncoming) ) {
			LIE_DEBUG(dbgs() << "LoopInfoExpr: incoming value have incorrect loop-variance...aborting this step.\n");
			return false;
		}

		Indvar = Phi;
		IndvarStart = getExprForLoop( Toplevel, PreheaderIncoming );

		switch ( ICI->getPredicate() ) {
			case CmpInst::ICMP_SLT:
			case CmpInst::ICMP_ULT:
				IndvarEnd = Invar - 1;
				break;
			
			case CmpInst::ICMP_SGT:
			case CmpInst::ICMP_UGT:
				IndvarEnd = Invar + 1;
				break;
			
			case CmpInst::ICMP_SLE:
			case CmpInst::ICMP_ULE:
			case CmpInst::ICMP_UGE:
			case CmpInst::ICMP_SGE:
			case CmpInst::ICMP_EQ: //added later
				IndvarEnd = Invar;
				break;
			
			default:
				LIE_DEBUG(dbgs() << "LoopInfoExpr: invalid loop comparison predicate\n");
				return false;
		}
	
		ExprMap Repls;
		Expr PhiEx(Phi), LatchIncomingEx = getExprForLoop(Toplevel, LatchIncoming), Wild = Expr::WildExpr();
		
		if ( !LatchIncomingEx.match(PhiEx + Wild, Repls) || Repls.size() != 1 || Repls[Wild].has(PhiEx) ) {
			LIE_DEBUG(dbgs() << "LoopInfoExpr: could not determine accurate step\n");
			return false;
		}

		IndvarStep = Repls[Wild];

		LIE_DEBUG(dbgs() << "LoopInfoExpr: induction variable, start, end, step: " << *Indvar << " => (" << IndvarStart << ", " << IndvarEnd << ", +" << IndvarStep << ")\n");
		
		return true;
	
	} //if ( PHINode *Phi = getSingleLoopVariantPhi(L, Var) )

	return false;
}


PHINode *LoopInfoExpr::getSingleLoopVariantPhi(Loop *L, Expr Ex) {
	auto Symbols = Ex.getSymbols();
	PHINode *Phi = nullptr;
	
	for (auto& Sym : Symbols) {
		Value *V = Sym.getSymbolValue();
		if ( !L->isLoopInvariant(V) ) {
			if ( !isa<PHINode>(V) || Phi )
				return nullptr;
			
			Phi = cast<PHINode>(V);
		}
	}
	
	return Phi;
}
