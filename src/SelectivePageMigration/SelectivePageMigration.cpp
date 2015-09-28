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
#include "SelectivePageMigration.h"

#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#include <vector>
#include <set>
#include <string>

using namespace llvm;

/* ***************************************************************** */
/* ***************************************************************** */

static cl::opt<bool>	ClDebug( "spm-debug", cl::desc("Enable debugging for the SPM transformation"),
						cl::Hidden, cl::init(false) );


static cl::opt<bool>	ClThreadLock( "spm-thread-lock", cl::desc("Enable thread-lock mechanism for the SPM transformation"),
						cl::Hidden, cl::init(false) );


static cl::opt<std::string>	ClFunc( "spm-pthread-function", cl::desc("Only analyze/transform the given function"),
							cl::Hidden, cl::init("") );


static RegisterPass<SelectivePageMigration> X( "spm", "ccNUMA selective page migration transformation");
char SelectivePageMigration::ID = 0;

#define SPM_DEBUG(X) { if (ClDebug) {X;} }

/* ***************************************************************** */
/* ***************************************************************** */


void SelectivePageMigration::getAnalysisUsage(AnalysisUsage &AU) const {

	AU.addRequired<DataLayout>();  
	AU.addRequired<DominatorTree>();
	AU.addRequired<LoopInfo>();
	
	AU.addRequired<ReduceIndexation>();
	AU.addRequired<RelativeExecutions>();
	AU.addRequired<RelativeMinMax>();
	AU.addRequired<SymPyInterface>();
	AU.addRequired<GetWorkerFunctions>();

	AU.setPreservesCFG(); //it was originally setPreservesAll()
}


bool SelectivePageMigration::runOnFunction(Function &F) {

	DL_  = &getAnalysis<DataLayout>();
	DT_  = &getAnalysis<DominatorTree>();
	LI_  = &getAnalysis<LoopInfo>();
		
	RI_  = &getAnalysis<ReduceIndexation>();
	RE_  = &getAnalysis<RelativeExecutions>();
	RMM_ = &getAnalysis<RelativeMinMax>();
	GWF_ = &getAnalysis<GetWorkerFunctions>();

	Module_  = F.getParent();
	Context_ = &Module_->getContext();

	Type		*VoidTy		= Type::getVoidTy(*Context_);
	IntegerType	*IntTy		= IntegerType::getInt64Ty(*Context_);
	PointerType	*IntPtrTy	= PointerType::getUnqual(IntTy);
	PointerType	*VoidPtrTy	= PointerType::getInt8PtrTy(*Context_);

	if (F.getName() == "main") {
		SPM_DEBUG(dbgs() << "SelectivePageMigration: inserting hwloc calls into main function\n");
		
		FunctionType *FnType = FunctionType::get(VoidTy, ArrayRef<Type*>(), false);
		IRBuilder<> IRB(  &( *F.getEntryBlock().begin() )  );

		Constant *Init = Module_->getOrInsertFunction("__spm_init", FnType);
		IRB.CreateCall(Init);

		Constant *End = Module_->getOrInsertFunction("__spm_end", FnType);
		for (auto &BB : F) {
		
			TerminatorInst *TI = BB.getTerminator();
			if ( isa<ReturnInst>(TI) ) {
				IRB.SetInsertPoint(TI);
				IRB.CreateCall(End);
			}
		
		} //for
	} //if (F.getName() == "main")
	
	CallInst *TLockInst = nullptr;
	if (ClThreadLock == true) { //thread lock mechanism
		std::set<string>::iterator worker_func = GWF_->workers.find( F.getName().str() );

		if ( worker_func != GWF_->workers.end() ) {
			FunctionType *FType = FunctionType::get(VoidTy, ArrayRef<Type*>(), false);
			IRBuilder<> IRBld_lock(  &( *F.getEntryBlock().begin() )  );
			//IRBuilder<> IRBld_lock(  &( *F.getEntryBlock().getTerminator() )  ); //insert at end

			Constant *TLock = Module_->getOrInsertFunction("__spm_thread_lock", FType);
			TLockInst = IRBld_lock.CreateCall(TLock);
			
			IRBuilder<> IRBld_unlock(  &( *F.back().getTerminator() )  );
			TLock = Module_->getOrInsertFunction("__spm_thread_unlock", FType);
			TLockInst = IRBld_unlock.CreateCall(TLock);
		}
	
	} //if (ClThreadLock == true)

	if ( !ClFunc.empty() && F.getName() != ClFunc ) {
		SPM_DEBUG(dbgs() << "SelectivePageMigration: skipping function " << F.getName() << "\n");
		return false;
	}

	//'start' the function pass, after the special cases

	SPM_DEBUG(dbgs() << "\n\n\nSelectivePageMigration: ***** processing function " << F.getName() << " *****\n\n\n");

	if (TLockInst != nullptr)
		SPM_DEBUG(dbgs() << "\nSelectivePageMigration: Thread lock call inserted: "<< *TLockInst << "\n");

	Calls_.clear();
	LoadsToInsert.clear();

	std::vector<Type*> ReuseFnFormals = { VoidPtrTy, IntTy, IntTy, IntTy };
	FunctionType *ReuseFnType = FunctionType::get(VoidTy, ReuseFnFormals, false);
	
	ReuseFn_ = F.getParent()->getOrInsertFunction("__spm_get", ReuseFnType);

	std::set<BasicBlock*> Processed;
	auto Entry = DT_->getRootNode();
  
  
	for (auto ET = po_begin(Entry), EE = po_end(Entry); ET != EE; ++ET) { //this for analyzes loops that we want to optimize
		BasicBlock *Header = (*ET)->getBlock();

		if ( LI_->isLoopHeader(Header) ) {
			SPM_DEBUG(dbgs() << "SelectivePageMigration: processing loop at " << Header->getName() << "\n");
			
			Loop *L = LI_->getLoopFor(Header);

			if ( L->getNumBackEdges() != 1 || std::distance( pred_begin(Header), pred_end(Header) ) != 2 ) {
				SPM_DEBUG(dbgs() << "SelectivePageMigration: loop has multiple backedges or multiple incoming outer blocks\n");
				
				continue;
			}

			SPM_DEBUG(dbgs() << "SelectivePageMigration: processing loop at " << Header->getName() << "\n");

			for (auto BB = L->block_begin(), BE = L->block_end(); BB != BE; ++BB) { //this for analyzes the instructions of the Basic Block, one at a time
				if ( !Processed.count(*BB) ) {
					Processed.insert(*BB);
					for (auto &I : *(*BB))
						generateCallFor(L, &I); //this call is really important - there is where the 'magic' is done
				}
			} //for
		
		} //if ( LI_->isLoopHeader(Header) )
	} //for (auto ET = po_begin(Entry), EE = po_end(Entry); ET != EE; ++ET)

	
	bool ret_val = ( Calls_.empty() ) ? false : true; //if there are calls to be inserted, the program is modified, so it must return true
	
	for (auto &CI : Calls_) { //this for creates all the calls to the __spm_get function

		IRBuilder<> IRB( CI.Preheader->getTerminator() );
		Value *VoidArray;
		
		auto LExist = LoadsToInsert.find(CI.Array);
		
		if ( LExist != LoadsToInsert.end() ) {
			
			if ( isa<LoadInst>(LExist->second) ) {
				LoadInst *Ltmp = dyn_cast<LoadInst>(CI.Array);

				LoadInst *Lins = new LoadInst( Ltmp->getOperand(0), "", Ltmp->isVolatile(), Ltmp->getAlignment() );

				Lins = IRB.Insert(Lins);
				VoidArray = IRB.CreateBitCast(dyn_cast<Value>(Lins), VoidPtrTy);
			}
			
			else {
				StoreInst *Stmp = dyn_cast<StoreInst>(CI.Array);

				LoadInst *Lins = new LoadInst( Stmp->getOperand(1), "", Stmp->isVolatile(), Stmp->getAlignment() );

				Lins = IRB.Insert(Lins);
				VoidArray = IRB.CreateBitCast(dyn_cast<Value>(Lins), VoidPtrTy);
			}
			
			//LoadsToInsert.erase(CI.Array);
		} //if ( LExist != LoadsToInsert.end() )
		
		else
			VoidArray = IRB.CreateBitCast(CI.Array, VoidPtrTy);
		
		std::vector<Value*> Args = { VoidArray, CI.Min, CI.Max, CI.Reuse };
		CallInst *CR = IRB.CreateCall(ReuseFn_, Args);

		SPM_DEBUG(dbgs() << "\nSelectivePageMigration: call instruction: " << *CR << "\n\n");
	} //for (auto &CI : Calls_)


	return ret_val;
}


bool SelectivePageMigration::generateCallFor(Loop *L, Instruction *I) {
	if (!isa<LoadInst>(I) && !isa<StoreInst>(I))
		return false;

	Value *Array;
	unsigned Size;
	Expr Subscript;

	if (isa<LoadInst>(I)) {
		if ( !RI_->reduceLoad(cast<LoadInst>(I), Array, Subscript) ) {
			SPM_DEBUG(dbgs() << "SelectivePageMigration: could not reduce load " << *I << "\n");
			SPM_DEBUG(dbgs() << "The instruction: " << *I << " won't be optimized\n");
			return false;
		}
		
		Size = DL_->getTypeAllocSize(I->getType());

		SPM_DEBUG(dbgs() << "SelectivePageMigration: reduced load " << *I << " to: " << *Array  << " + " << Subscript << "\n");
}

	else {
		if ( !RI_->reduceStore(cast<StoreInst>(I), Array, Subscript) ) {
			SPM_DEBUG(dbgs() << "SelectivePageMigration: could not reduce store " << *I << "\n");
			SPM_DEBUG(dbgs() << "The instruction: " << *I << " won't be optimized\n");
			return false;
		}
		
		Size = DL_->getTypeAllocSize(I->getOperand(0)->getType());
		
		SPM_DEBUG(dbgs() << "SelectivePageMigration: reduced store " << *I << " to: " << *Array  << " + " << Subscript << "\n");
	}

	Loop *Final;
	Expr ReuseEx = RE_->getExecutionsRelativeTo(L, nullptr, Final);

	if ( !ReuseEx.isValid() ) {
		SPM_DEBUG(dbgs() << "SelectivePageMigration: could not calculate reuse for loop " << L->getHeader()->getName() << "\n");
		SPM_DEBUG(dbgs() << "The instruction: " << *Array << " won't be optimized\n");
		return false;
	}
	
	SPM_DEBUG(dbgs() << "SelectivePageMigration: reuse of "	<< L->getHeader()->getName() << " relative to "	<< Final->getHeader()->getName() << ": " << ReuseEx << "\n");

	BasicBlock *Preheader = Final->getLoopPreheader();
	BasicBlock *Exit      = Final->getExitBlock();
  
	if ( Instruction *AI = dyn_cast<Instruction>(Array) ) {
		if ( !DT_->dominates(AI->getParent(), Preheader) && AI->getParent() != Preheader ) {

			SPM_DEBUG(dbgs() << "SelectivePageMigration: array does not dominate loop preheader; we'll try to insert a load to avoid the dominance problem...\n" );
			
			if ( isa<LoadInst>(Array) ) {
				LoadInst *L = dyn_cast<LoadInst>(Array);
				
				if (L != nullptr) {
					Value *Vtmp = dyn_cast<Value>( L->getPointerOperand() );
					if ( Vtmp == nullptr || !isa<GlobalVariable>(Vtmp) ) {
						SPM_DEBUG(dbgs() << "SelectivePageMigration: array does not dominate loop preheader and...we couldn't solve.\n" << "The instruction: " << *Array << " won't be optimized\n");
						return false;						
					}
					LoadsToInsert[Array] = Array;
				}
				
				else {
					SPM_DEBUG(dbgs() << "SelectivePageMigration: array does not dominate loop preheader and...we couldn't solve.\n" << "The instruction: " << *Array << " won't be optimized\n");
					return false;											
				}
			}
			
			else {
				StoreInst *S = dyn_cast<StoreInst>(Array);
				
				if (S != nullptr) {
					Value *Vtmp = dyn_cast<Value>( S->getPointerOperand() );
					if ( Vtmp == nullptr || !isa<GlobalVariable>(Vtmp) ) {
						SPM_DEBUG(dbgs() << "SelectivePageMigration: array does not dominate loop preheader and...we couldn't solve.\n" << "The instruction: " << *Array << " won't be optimized\n");
						return false;						
					}
					LoadsToInsert[Array] = Array;
				}
				
				else {
					SPM_DEBUG(dbgs() << "SelectivePageMigration: array does not dominate loop preheader and...we couldn't solve.\n" << "The instruction: " << *Array << " won't be optimized\n");
					return false;											
				}
			}
		
		} //if ( !DT_->dominates(AI->getParent(), Preheader) && AI->getParent() != Preheader )
			
	} // if ( Instruction *AI = dyn_cast<Instruction>(Array) )

	Expr MinEx, MaxEx;

	if (!RMM_->getMinMax(Subscript, MinEx, MaxEx)) {
		SPM_DEBUG(dbgs() << "SelectivePageMigration: could calculate min/max for subscript " << Subscript << "\n");
		SPM_DEBUG(dbgs() << "The instruction: " << *Array << " won't be optimized\n");
		return false;
	}
	
	SPM_DEBUG(dbgs() << "SelectivePageMigration: min/max for subscript " << Subscript << ": " << MinEx << ", " << MaxEx << "\n");

	if ( !canGenerateExprAt(&ReuseEx, Preheader) || !canGenerateExprAt(&MinEx, Preheader) || !canGenerateExprAt(&MaxEx, Preheader) ) {
		SPM_DEBUG(dbgs() << "SelectivePageMigration: symbol does not dominate loop preheader\n");
		SPM_DEBUG(dbgs() << "The instruction: " << *Array << " won't be optimized\n");
		return false;
	}
  
	if (MinEx == MaxEx) { //avoid the degenerate case of migrating a single page
		SPM_DEBUG(dbgs() << "\nSelectivePageMigration: Min and Max are the same value: " << MinEx << " (Min); " << MaxEx << "(Max).\n");
		SPM_DEBUG(dbgs() << "SelectivePageMigration: This case will lead to a migration of a single page, which doesn't help us at all - so, we're quitting in this point.\n" << "The instruction: " << *Array << " won't be optimized\n\n");
		return false;
	}
	
	IRBuilder<> IRB( Preheader->getTerminator() );
  
	Value *Reuse = (ReuseEx * Size).getExprValue( 64, IRB, Module_ );
	Value *Min   = MinEx.getExprValue(64, IRB, Module_);
	Value *Max   = MaxEx.getExprValue(64, IRB, Module_);

	SPM_DEBUG(dbgs() << "SelectivePageMigration: values for reuse, min, max:\n*** Reuse: "<< *Reuse << "\n*** Min: " << *Min << "\n*** Max: " << *Max << "\n\n");


	CallInfo CI = { Preheader, Exit, Array, Min, Max, Reuse };
	auto Call = Calls_.insert(CI);
	
	if (!Call.second) {
		IRBuilder<> IRB( Preheader->getTerminator() );
		CallInfo SCI = *Call.first;

		Value *CmpMin = IRB.CreateICmp(CmpInst::ICMP_SLT, SCI.Min, CI.Min);
		SCI.Min = IRB.CreateSelect(CmpMin, SCI.Min, CI.Min);

		Value *CmpMax = IRB.CreateICmp(CmpInst::ICMP_SGT, SCI.Max, CI.Max);
		SCI.Max = IRB.CreateSelect(CmpMax, SCI.Max, CI.Max);

		SCI.Reuse = IRB.CreateAdd(SCI.Reuse, CI.Reuse);

		Calls_.erase(SCI);
		Calls_.insert(SCI);
	} // if (!Call.second)

	return true;
}


bool SelectivePageMigration::canGenerateExprAt(Expr *Ex, BasicBlock *BB) {
	for ( auto &Sym : Ex->getSymbols() ) {
		if ( Instruction *I = dyn_cast<Instruction>(Sym.getSymbolValue()) ) {

			if (!DT_->dominates(I->getParent(), BB) && I->getParent() != BB) {
				
				if ( LoadInst *L = dyn_cast<LoadInst>(I) ) { //in this case, we will try to add a load before the call point to avoid dominance problems

					Value *Vtmp = dyn_cast<Value>( L->getPointerOperand() );
					if ( Vtmp != nullptr && isa<GlobalVariable>(Vtmp) ) {
						Value *R = dyn_cast<Value>(I);
						LoadInst *L_ins= new LoadInst( L->getOperand(0), "", L->isVolatile(), L->getAlignment() );
					
						IRBuilder<> IRB( BB->getTerminator() );
						L_ins = IRB.Insert(L_ins);
					
						Value *V = dyn_cast<Value>(L_ins);
						Expr E(V, R); //constructor used to replace the Value of an Expr to another Value

						return canGenerateExprAt(Ex, BB); //could this generate an infinite loop? *CAUTION*
					}
					
					else {
						SPM_DEBUG(dbgs() << "\nSelectivePageMigration: symbol "<< *L <<" does not dominate block " << BB->getName() << "\n\n");
						return false;
					}
				}
					
				else {
					SPM_DEBUG(dbgs() << "\nSelectivePageMigration: symbol "<< *I <<" does not dominate block " << BB->getName() << "\n\n");
					return false;
				}
			}

		} //if ( Instruction *I = dyn_cast<Instruction>(Sym.getSymbolValue()) )
	} //for ( auto &Sym : Ex->getSymbols() )

	return true;
}

