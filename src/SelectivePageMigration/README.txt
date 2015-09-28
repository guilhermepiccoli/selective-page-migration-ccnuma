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

-- Prerequisites --

1) Use an RTTI-enabled build of LLVM (build with REQUIRES_RTTI=1).

2) Download and install the latest version of SymPy from
   git://github.com/sympy/sympy.git (requires Python 2.7).
   Add the lib.*/ directory from the SymPy build to your PYTHONPATH.

3) Download and install GiNaC 1.6.2 from
   ftp://ftpthep.physik.uni-mainz.de/pub/GiNaC/ginac-1.6.2.tar.bz2 and
   apply the patch ginac.diff from the SPM repository. Add GiNaC's
   <build>/lib/ directory to your LD_LIBRARY_PATH.

4) Download and install latest version of hwloc from
   http://www.open-mpi.org/software/hwloc
   Add hwloc's <build>/lib/ directory to your LD_LIBRARY_PATH.


-- Compiling --
1) Add GiNaC's <build>/include/ directory to CXXFLAGS preceded by
   -I before running make, if necessary.

2) Run make.


-- Using --
1) Compile the input file to bytecode with "-c -emit-llvm -O0".

2) Run "opt -mem2reg -mergereturn -load SelectivePageMigration.so -spm
   in.bc -o out.bc".
   You may specify a single function to be transformed with
   "-spm-pthread-function <func_name>".

3) Generate an object file from out.bc with llc & gcc/clang.
   You may choose to optimize (-O3) with opt before running llc.

4) Compile the runtime with "g++ -O3 -std=c++0x -c
   SelectivePageMigrationRuntime.cpp -o SelectivePageMigrationRuntime.o".

5) Link the object file with SelectivePageMigrationRuntime.o and with
   hwloc using -lhwloc.
