# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>

# Author(s):
# Guilherme Piccoli


#'debug' script flag
#set -x


BENCHNAME=""

#Heuristic vars
HEURISTICDIR="<your_heuristic_path>"
HEURISTICNAME="<your_heuristic_name>"

#SPM vars
LIBDIR="<your_lib_dir>"
LIBNAME="SelectivePageMigration.so"

#C vs C++ (there are benchs written in both)
ISCPP="++"
EXTCPP="pp"

#Declare below var as empty if using system's hwloc
CUSTOMHWLOC="-L<your_hwloc_path>"

#Declare below var as empty to disable compilation with debug flag
DEBUGFLAG="-g"


argcheck() {
if [ ! -f arglib.o ]; then
clang++ $DEBUGFLAG -O3 -c arglib.cpp -o arglib.o
fi
}

buildfunc() {
clang$ISCPP $DEBUGFLAG -O3 -c $BENCHNAME.c$EXTCPP -o $BENCHNAME.o
clang$ISCPP $DEBUGFLAG -D__MIGRATE__ -O3 -c $BENCHNAME.c$EXTCPP -o ${BENCHNAME}_mig.o

clang++ -O3 -Wall -o $BENCHNAME $BENCHNAME.o arglib.o -lpthread -lm
clang++ -O3 -Wall -o ${BENCHNAME}_mig ${BENCHNAME}_mig.o arglib.o -lpthread -lm $CUSTOMHWLOC -lhwloc -lnuma

rm -f $BENCHNAME.o ${BENCHNAME}_mig.o

clang$ISCPP $DEBUGFLAG -emit-llvm -O0 -Wno-int-to-pointer-cast -c $BENCHNAME.c$EXTCPP -o $BENCHNAME.bc

opt -mem2reg -mergereturn $BENCHNAME.bc -o $BENCHNAME.m2r.bc

opt -load $LIBDIR/$LIBNAME -spm $BENCHNAME.m2r.bc -o $BENCHNAME.spm.bc
opt -load $LIBDIR/$LIBNAME -spm -spm-thread-lock $BENCHNAME.m2r.bc -o $BENCHNAME.spm_threadlock.bc

opt -O3 $BENCHNAME.spm.bc -o $BENCHNAME.final.bc
opt -O3 $BENCHNAME.spm_threadlock.bc -o $BENCHNAME.final_threadlock.bc

llc $BENCHNAME.final.bc -o $BENCHNAME.s
llc $BENCHNAME.final_threadlock.bc -o ${BENCHNAME}_threadlock.s

clang++ -O3 -o ${BENCHNAME}_spm $BENCHNAME.s arglib.o $HEURISTICDIR/$HEURISTICNAME $CUSTOMHWLOC -lhwloc -lpthread

clang++ -O3 -o ${BENCHNAME}_spm_threadlock ${BENCHNAME}_threadlock.s arglib.o $HEURISTICDIR/$HEURISTICNAME $CUSTOMHWLOC -lhwloc -lpthread

rm -f *.bc
rm -f $BENCHNAME.s ${BENCHNAME}_threadlock.s
}


#Building the benchs
argcheck

BENCHNAME=easy_add
buildfunc

BENCHNAME=easy_ch
buildfunc

BENCHNAME=easy_lu
buildfunc

BENCHNAME=easy_prod
buildfunc

ISCPP=""
EXTCPP=""

#if BucketSort show issues on building directly with clang -O3, please
#use clang -O0 and opt -O3. This might be a bug on older clang versions.
BENCHNAME=BucketSort
buildfunc

BENCHNAME=partitionStrSearch
buildfunc

rm -f arglib.o
