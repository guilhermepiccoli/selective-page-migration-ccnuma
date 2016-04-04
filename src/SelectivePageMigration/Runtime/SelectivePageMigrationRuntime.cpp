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

#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <list>

#include "hwloc.h"
#include <numaif.h>
#include <sys/syscall.h>
#include <ctime>
#include <cstring>
#include <cstdlib>

#include <pthread.h>

#ifdef __DEBUG__
#define SPMR_DEBUG(X) X
#else
#define SPMR_DEBUG(X)
#endif

#ifndef REUSE_CTE
#define REUSE_CTE 200
#endif

extern "C" {
  void __spm_init();
  void __spm_end();
  void __spm_get (void *Array, long Start, long End, long Reuse);
  void __spm_thread_lock();
  void __spm_thread_unlock();
}

const double __spm_ReuseConstant = REUSE_CTE;
const double __spm_CacheConstant = 0.2;

const long PAGE_EXP  = 12;
const long PAGE_SIZE = (1 << PAGE_EXP);

hwloc_topology_t __spm_topo;
hwloc_bitmap_t __spm_full_cpuset;
unsigned long __spm_cache_size = 0;

//thread distribution mechanism
hwloc_bitmap_t* __spm_nodes;
int __spm_num_nodes;
int __spm_current_node;

static std::mutex __spm_lock;

/*
typedef struct
	{
		int from;
		int to;
	} ft_node;

typedef std::list<ft_node*> NodeList;

typedef std::unordered_map<long, NodeList> PageMap;
PageMap page_log;

void clear_map(PageMap *map) {

	PageMap::iterator it_e = map->end();
	for (PageMap::iterator it = map->begin(); it!=it_e; ++it) {

		NodeList::iterator list_it_e = (*it).second.end();
		for (NodeList::iterator list_it = (*it).second.begin(); list_it!=list_it_e; ++list_it)
			delete (*list_it);

		(*it).second.clear();
	}
	map->clear();
}


void add_page_to_set(long page, PageMap *map, int node_from, int node_to) {
	PageMap::iterator it = map->find(page);


	if ( it == map->end() ) {
		NodeList tmp_nl;
		std::pair<long,NodeList> tmp_pair (page,tmp_nl);
		map->insert(tmp_pair);
		it = map->find(page);
	}

	ft_node *tmp_node = new ft_node;

	tmp_node->from = node_from;
	tmp_node->to = node_to;

	(*it).second.push_back(tmp_node);
}


void print_log(PageMap *map) {

	FILE *fp;
	time_t time_st;
	struct tm *now;
	char fname[100] = {0};

	const char lbreak[] = {'\n',0};

	time (&time_st);
	now = localtime(&time_st);

	strcat(strcat(fname,"page_log_"),asctime(now));
	char *lame = strstr(fname,lbreak);
	(*lame) = 0;

	fp = fopen(fname,"w");
	if (fp != NULL) {

		fprintf(fp,"------------------------------ (num_of_pages=%ld)\n",map->size());
		PageMap::iterator it_e = map->end();

		for (PageMap::iterator it = map->begin(); it!=it_e; ++it) {
			//fprintf(fp,"\n%ld",(*it).first);

			NodeList::iterator lst_it_e = (*it).second.end();
			for (NodeList::iterator lst_it = (*it).second.begin(); lst_it!=lst_it_e; ++lst_it)
				fprintf( fp,"\n%ld; %d; %d",(*it).first, (*lst_it)->from,(*lst_it)->to ); //; =if(indirect(address(row(),column()-2))=indirect(address(row(),column()-1)),0,1)

			fflush(fp);
		}

		fclose(fp);
	}

	else {
		printf("Error opening file to log page migrations\n\n");
	}
}
*/

/*
class PageIntervals {
private:
  struct PageRegion {
    long Start, End;
    bool operator<(const PageRegion& Other) const {
		SPMR_DEBUG(std::cout << "Runtime: (" << Start << ", " << End << ") < ("
							 << Other.Start << ", " << Other.End << ") == "
							 << (Start < Other.Start ? End < Other.End : false) << "\n");
      return Start < Other.Start ? End < Other.End : false;
    }
  };

public:
  typedef std::map<PageRegion, pid_t> RegionsTy;

  RegionsTy::iterator insert(long Start, long End, pid_t Idx);
  RegionsTy::iterator begin() { return Regions_.begin(); }
  RegionsTy::iterator end()   { return Regions_.end();   }

private:
  RegionsTy::iterator join(RegionsTy::iterator L);
  RegionsTy::iterator join(RegionsTy::iterator L, RegionsTy::iterator R);

  RegionsTy Regions_;
};
*/

/*
PageIntervals::RegionsTy::iterator PageIntervals::insert(long Start, long End,
                                                         pid_t Idx) {
  SPMR_DEBUG(std::cout << "Runtime: inserting region (" << Start << ", "
                       << End << ")\n");
  PageRegion R = { Start, End };
  auto It = Regions_.insert(std::make_pair(R, Idx));
  if (It.second) {
    // Region was inserted - attempt to join it with its neighbors.
    auto RIt = join(It.first);
    SPMR_DEBUG(std::cout << "Runtime: iterator for region (" << Start << ", "
                         << End << ") is (" << RIt->first.Start << ", "
                         << RIt->first.End << ")\n");
    return RIt;
  } else {
    // Not inserted - an iterator already exists for this region.
    SPMR_DEBUG(std::cout << "Runtime: iterator already exists for region ("
                         << Start << ", " << End << ")\n");
    return end();
  }
}

*/

/*
PageIntervals::RegionsTy::iterator PageIntervals::join(RegionsTy::iterator L) {
  RegionsTy::iterator It = L; ++It;
  // Join with the greater (>) subtree.
  if (It != Regions_.end()) {
    L = join(L, It);
  }
  // Join with the lesser (<) subtree.
  if (L != Regions_.begin()) {
    RegionsTy::iterator It = L; --It;
    L = join(L, It);
  }
  return L;
}

*/

/*
PageIntervals::RegionsTy::iterator PageIntervals::join(RegionsTy::iterator L,
                                                      RegionsTy::iterator R) {
  // TODO: try to lease a single conflicting page to multiple threads - it
  // could be an array boundary.
  if (L->second != R->second) {
    SPMR_DEBUG(std::cout << "Runtime: region conflicts with other thread: ("
                         << L->first.Start << ", " << L->first.End << ") and ("
                         << R->first.Start << ", " << R->first.End << ")\n");
    return L;
  }
  //
  // L |---- ---- ---- ----|
  // R           |---- ---- ---- ----|
  //
  if (L->first.Start <= R->first.Start && L->first.End <= R->first.End) {
    long Start = L->first.Start, End = R->first.End, Idx = L->second;
    Regions_.erase(L);
    Regions_.erase(R);
    return insert(Start, End, Idx);
  }
   //
   // L           |---- ---- ---- ----|
   // R |---- ---- ---- ----|
   //
  if (R->first.Start <= L->first.Start && R->first.End <= L->first.End) {
    long Start = R->first.Start, End = L->first.End, Idx = L->second;
    Regions_.erase(R);
    Regions_.erase(L);
    return insert(Start, End, Idx);
  }
   //
   // L      |---- ----|
   // R |---- ---- ---- ----|
   //
  if (L->first.Start >= R->first.Start && L->first.End <= R->first.End) {
    Regions_.erase(L);
    return R;
  }
   //
   // L |---- ---- ---- ----|
   // R      |---- ----|
   //
  if (R->first.Start >= L->first.Start && R->first.End <= L->first.End) {
    Regions_.erase(R);
    return L;
  }
  //
  // L |---- ---- ---- ----|
  // R                         |---- ---- ---- ----|
  //
  assert(L->first.End < R->first.Start || L->first.Start > R->first.End ||
         L->second != R->second);
  return L;
}
*/

//static PageIntervals SPMPI;
//static std::mutex    SPMPILock;

//uint64_t count;

void migrate(long PageStart, long PageEnd) {
	SPMR_DEBUG(std::cout << "Runtime: migrate pages: " << PageStart << " to "
					   << PageEnd << "\n");
	SPMR_DEBUG(std::cout << "Runtime: hwloc call: " << (PageStart << PAGE_EXP)
					   << ", " << ((PageEnd - PageStart) << PAGE_EXP) << "\n");

	hwloc_bitmap_t set = hwloc_bitmap_alloc();

	hwloc_get_cpubind(__spm_topo, set, HWLOC_CPUBIND_THREAD);
	hwloc_get_last_cpu_location(__spm_topo, set, HWLOC_CPUBIND_THREAD);

	hwloc_bitmap_singlify(set);

/*
	hwloc_nodeset_t nodeset = hwloc_bitmap_alloc();

	hwloc_cpuset_to_nodeset(__spm_topo, set, nodeset);

	char set_str[15] = {0};
	hwloc_bitmap_snprintf(set_str,14,(hwloc_const_bitmap_t)nodeset);

	uint64_t set_num = strtoull(set_str,NULL,16);
	int nto=-1, nfrom=-1;

	for (nfrom=0; set_num != 1; ++nfrom) //log2
		set_num >>= 1;

	//nfrom %= 8;

	long remainder = (long)( (PageStart << PAGE_EXP) & (PAGE_SIZE-1) );
	long page = (long)( (PageStart << PAGE_EXP) - remainder );
	long len = (PageEnd - PageStart) + remainder;
*/
	assert(
			hwloc_set_area_membind(__spm_topo, (const void*)(PageStart << PAGE_EXP),
								  (PageEnd - PageStart) << PAGE_EXP,
								  (hwloc_const_cpuset_t)set, HWLOC_MEMBIND_BIND,
								  HWLOC_MEMBIND_MIGRATE)
	!= -1 && "Unable to migrate requested pages");
/*
SPMPILock.lock();
	count++;
SPMPILock.unlock();
*/


/*
	for (long j=0; j<len; ++j) {
		get_mempolicy(&nto, NULL, 0, (void*)(page+j), MPOL_F_NODE | MPOL_F_ADDR);
		SPMPILock.lock();
			add_page_to_set(page+j,&page_log,nfrom,nto);
		SPMPILock.unlock();
	}

	hwloc_bitmap_free(nodeset);
*/
	hwloc_bitmap_free(set);
}


void __spm_init() {
  SPMR_DEBUG(std::cout << "Runtime: initialize\n");

  hwloc_topology_init(&__spm_topo);
  hwloc_topology_load(__spm_topo);

  hwloc_obj_t obj;
  for (obj = hwloc_get_obj_by_type(__spm_topo, HWLOC_OBJ_PU, 0); obj;
       obj = obj->parent)
    if (obj->type == HWLOC_OBJ_CACHE)
      __spm_cache_size += obj->attr->cache.size;

	__spm_full_cpuset = hwloc_bitmap_alloc();
	hwloc_get_cpubind(__spm_topo, __spm_full_cpuset, HWLOC_CPUBIND_PROCESS);

////////////////////////////////////////////////////////////////////////
	__spm_num_nodes = hwloc_get_nbobjs_by_type (__spm_topo, HWLOC_OBJ_NODE);
	__spm_current_node = -1;


	__spm_nodes = (hwloc_bitmap_t*)malloc(__spm_num_nodes*sizeof(hwloc_bitmap_t));
	if (__spm_nodes == NULL) {
		printf("\nOOOOOPPPSSSSSS....\n");
		exit(99);
	}

	for (int i=0; i<__spm_num_nodes; ++i) {
		obj = hwloc_get_obj_by_type (__spm_topo, HWLOC_OBJ_NODE, i);
		__spm_nodes[i] = hwloc_bitmap_alloc();
		hwloc_bitmap_copy(__spm_nodes[i], obj->nodeset);
	}
}


void __spm_end() {
	SPMR_DEBUG(std::cout << "Runtime: end\n");
	//printf("\n\ncount=%lu\n",count);

	hwloc_bitmap_free(__spm_full_cpuset);

	for (int i=0; i<__spm_num_nodes; ++i)
		hwloc_bitmap_free(__spm_nodes[i]);
	free(__spm_nodes);

	hwloc_topology_destroy(__spm_topo);
/*
	print_log(&page_log);
	clear_map(&page_log);
*/
}


void __spm_thread_lock() {
/*
	char last_node[100];
*/

	hwloc_bitmap_t _cpuset_ = hwloc_bitmap_alloc();
/*
	hwloc_get_cpubind(__spm_topo, _cpuset_, HWLOC_CPUBIND_THREAD);
	hwloc_get_last_cpu_location(__spm_topo, _cpuset_, HWLOC_CPUBIND_THREAD);

	hwloc_bitmap_singlify(_cpuset_);

	hwloc_nodeset_t _nodeset_ = hwloc_bitmap_alloc();

	hwloc_cpuset_to_nodeset(__spm_topo, _cpuset_, _nodeset_); //from the cpuset with a single CPU, we get the node

	hwloc_cpuset_from_nodeset(__spm_topo, _cpuset_, _nodeset_);//from the node, we get all CPUs on that node
*/

	int k;
	__spm_lock.lock();
		__spm_current_node = (__spm_current_node + 1)%__spm_num_nodes;
		k = __spm_current_node;
	__spm_lock.unlock();

	hwloc_cpuset_from_nodeset(__spm_topo, _cpuset_, __spm_nodes[k]); //from one node, we get all CPUs on that

	hwloc_set_thread_cpubind(__spm_topo, (hwloc_thread_t)pthread_self(), (hwloc_const_cpuset_t)_cpuset_, HWLOC_CPUBIND_THREAD);

/*
	hwloc_bitmap_snprintf(last_node, 100, /*_nodeset_*/__spm_nodes[k]);
	//printf("\n[tid=%08x] node=%s\n",pthread_self(),last_node);
*/

	hwloc_bitmap_free(_cpuset_);
	//hwloc_bitmap_free(_nodeset_);
}


void __spm_thread_unlock() {

	hwloc_set_thread_cpubind(__spm_topo, (hwloc_thread_t)pthread_self(), (hwloc_const_cpuset_t)__spm_full_cpuset, HWLOC_CPUBIND_THREAD);
}


void __spm_get(void *Ary, long Start, long End, long Reuse) {

	SPMR_DEBUG(std::cout << "Runtime: get page for: " << (long unsigned)Ary
		<< ", " << Start << ", " << End << ", "
		<< Reuse << "\n");

	long PageStart = ((long)Ary + Start)/PAGE_SIZE;
	long PageEnd   = ((long)Ary + End)/PAGE_SIZE;


	//printf("\n\nReuse=%ld, Start=%ld, End=%ld",Reuse,PageStart,PageEnd);

	if ( (double)(End-Start) > __spm_CacheConstant*__spm_cache_size && (double)Reuse/(End-Start > 0 ? End-Start : 100000) > __spm_ReuseConstant ) { //heuristic

		//printf("\n\nExpr=%lf",(double)Reuse/(double)( (PageEnd - PageStart) * PAGE_SIZE ));
		//printf("\n\nExpr=%lu",(End-Start));
		//printf("\nMIGROU\n");
/*
		SPMR_DEBUG(std::cout << "Runtime: get pages: " << PageStart << ", "
			<< PageEnd << "\n");

		pid_t Idx = syscall(SYS_gettid);

		SPMPILock.lock();
		auto It = SPMPI.insert(PageStart, PageEnd, Idx);

		if (It == SPMPI.end()) {
			SPMPILock.unlock();
			return;
		}
		SPMPILock.unlock();

		SPMR_DEBUG(std::cout << "Runtime: thread #" << Idx
			<< " now holds pages (possibly amongst others): "
			<< It->first.Start << " to " << It->first.End << "\n");
*/

//printf("\nstart: %ld,  end: %ld",PageStart,PageEnd); //test which pages are being migrated
//printf("\nTo be migrated: %p\n",Ary);
		return (void) migrate(PageStart, PageEnd);

	}//heuristic

}
