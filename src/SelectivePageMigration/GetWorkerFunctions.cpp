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
#include "GetWorkerFunctions.h"

static RegisterPass<GetWorkerFunctions> Z("GWF", "GetWorkerFunctions", false, true);
char GetWorkerFunctions::ID = 0;

bool GetWorkerFunctions::runOnModule(Module &M) {
	Module::iterator B,E;

	for (B = M.begin(),E = M.end(); B != E; ++B) {
		for (Function::iterator B_f = (*B).begin(), E_f = (*B).end(); B_f != E_f; ++B_f) {
			for (BasicBlock::iterator B_b = (*B_f).begin(), E_b = (*B_f).end(); B_b != E_b; ++B_b) {
				if ( CallInst* callInst = dyn_cast<CallInst>(&*B_b) ) {

					if (callInst->getCalledFunction() != nullptr)
						if ( callInst->getCalledFunction()->getName() == "pthread_create") {
							Value *val = (*callInst).getArgOperand(2); 
							GetWorkerFunctions::workers.insert(  ( (*val).getName() ).str()  );
						}
				}
			}
		}
	}
	return false;
}
