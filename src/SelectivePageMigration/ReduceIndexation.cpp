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
#include "ReduceIndexation.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

/* ************************************************************************** */
/* ************************************************************************** */

static cl::opt<bool>
  ClDebug( "reduce-indexation-debug",
          cl::desc("Enable debugging for the GEP reduction pass"),
          cl::Hidden, cl::init(false) );

static RegisterPass<ReduceIndexation>
  X("reduce-indexation", "Reduces GEPs to a base pointer and symbolic offset");
char ReduceIndexation::ID = 0;

#define RA_DEBUG(X) { if (ClDebug) { X; } }

/* ************************************************************************** */
/* ************************************************************************** */

void ReduceIndexation::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<DataLayout>();
  AU.addRequired<LoopInfoExpr>();
  AU.setPreservesAll();
}

bool ReduceIndexation::runOnFunction(Function &F) {
  DL_  = &getAnalysis<DataLayout>();
  LIE_ = &getAnalysis<LoopInfoExpr>();
  return false;
}

bool ReduceIndexation::reduceStore(StoreInst *SI, Value *&Array,
                                   Expr &Offset) const {
  bool Ret = reduceMemoryOp(SI->getPointerOperand(), Array, Offset);
  RA_DEBUG(
    if (Ret)
      dbgs() << "ReduceIndexation: reduced " << *SI << " to " << *Array
             << "+" << Offset << "\n";
    else
      dbgs() << "ReduceIndexation: could not reduce store " << *SI << "\n";
  );
  return Ret;
}

bool ReduceIndexation::reduceLoad(LoadInst *LI, Value *&Array,
                                  Expr &Offset) const {
  bool Ret = reduceMemoryOp(LI->getPointerOperand(), Array, Offset);
  RA_DEBUG(
    if (Ret)
      dbgs() << "ReduceIndexation: reduced " << *LI << " to " << *Array
                  << "+" << Offset << "\n";
    else
      dbgs() << "ReduceIndexation: could not reduce load " << *LI << "\n";
  );
  return Ret;
}

bool ReduceIndexation::reduceGetElementPtr(GetElementPtrInst *GEP, Value *&Array,
                         Expr &Offset) const {
  bool Ret = reduceMemoryOp(GEP, Array, Offset);
  RA_DEBUG(
    if (Ret)
      dbgs() << "ReduceIndexation: reduced " << *GEP << " to " << *Array
                  << "+" << Offset << "\n";
    else
      dbgs() << "ReduceIndexation: could not reduce GEP " << *GEP << "\n";
  );
  return Ret;
}

bool ReduceIndexation::reduceMemoryOp(Value *Ptr, Value *&Array,
                                      Expr& Subscript) const {
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
    if (reduceMemoryOp(GEP->getPointerOperand(), Array, Subscript)) {
      Type *Ty = GEP->getPointerOperand()->getType();

      for (unsigned Idx = 1; Idx < GEP->getNumOperands(); ++Idx) {
        if (StructType *ST = dyn_cast<StructType>(Ty)) {
          uint64_t Val =
            cast<ConstantInt>(GEP->getOperand(Idx))->getValue().getZExtValue();

          for (uint64_t SIdx = 0; SIdx < Val; ++SIdx)
            Subscript = Subscript + DL_->getTypeAllocSize(ST->getElementType(SIdx));

          Ty = ST->getElementType(Val);

          continue;
        }

        if (PointerType *PT = dyn_cast<PointerType>(Ty))
          Ty = PT->getElementType();
        else if (ArrayType *AT = dyn_cast<ArrayType>(Ty))
          Ty = AT->getElementType();

        // Multiply the current subscript by the size of the type being indexed.
        unsigned AllocSize = DL_->getTypeAllocSize(Ty);

        Subscript = Subscript + LIE_->getExpr(GEP->getOperand(Idx)) * AllocSize;
        if (!Subscript.isValid())
          return false;
      }
      return true;
    }
  }

  Array = Ptr;
  return true;
}

