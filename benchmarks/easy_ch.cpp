/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

/* Author(s):
 * Edson Borin
 * Guilherme Piccoli
*/

#include <iostream>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <string>
#include <sys/time.h>
#include <errno.h>
#include <cmath>

#include "arglib.h"

#include <unistd.h>
#include <sys/syscall.h>

#ifdef __MIGRATE__
	#include <hwloc.h>
	hwloc_topology_t topology;
#endif

#ifdef __THREAD_LOG__
	#include <sys/syscall.h>
	pid_t *thread_ids;
	pthread_mutex_t tid_lock;
	char work_busy_lock;
	int thread_done;
	FILE *threads_fp;
#endif

using namespace std;

vector<double*> matricesA;
vector<double*> matricesB;

pthread_mutex_t lock;
int next_matrix = 0;

clarg::argInt nmats("-nm", "Number of matrix multiplications", 1);
clarg::argInt matsz("-msz", "Matrix size = N x N", 1000);
clarg::argInt nthreads("-nt", "Number of threads", 1);


double* new_matrix(double* src, unsigned N) {
  
  double *m = (double*) malloc( N*N*sizeof(double) );
  if (m == NULL) {
	printf("\n\nError: new_matrix()\n\n");
	exit( -999 );
  }

  if (src != NULL) { memcpy( m, src, N*N*sizeof(double) ); }

  else { memset( m, 0, N*N*sizeof(double) ); }
	
  return m;
}


void init_matrices(void) {
	
  for (unsigned i=0; i<nmats.get_value(); i++) {
    matricesA.push_back( new_matrix(0, matsz.get_value()) );
    matricesB.push_back( new_matrix(0, matsz.get_value()) );
  }
}


void free_matrices() {
  
  for (unsigned i=0; i<nmats.get_value(); i++) {
    free(matricesA[i]);
    free(matricesB[i]);
  }
}


double mysecond()
{
  struct timeval tp;
  struct timezone tzp;
  gettimeofday(&tp,&tzp);
  return ( (double) tp.tv_sec + (double) tp.tv_usec * 1.e-6 );
}


int get_matrix(void) {
  int id; 

  if (next_matrix >= nmats.get_value())
    return -1;

	pthread_mutex_lock(&lock);
		id = next_matrix++;
	pthread_mutex_unlock(&lock);

  return id;
}


void* worker_thread(void* m) {

#ifdef __THREAD_LOG__
		pthread_mutex_lock(&tid_lock); 
		//{
			thread_done--;
			thread_ids[thread_done] = syscall(SYS_gettid);
			
			if (thread_done == 0) {
				threads_fp = fopen("thread_TIDs.tmp","w");
				if (threads_fp == NULL) {
					printf("\nError in threads_fp...ABORTING!");
					exit(1);
				}
				
				int end_loop = nthreads.get_value();
				//fprintf(threads_fp,"%d\n",end_loop);
				
				for (int tmp_loop=0; tmp_loop < end_loop; ++tmp_loop)
					fprintf(threads_fp,"%d\n", (int)thread_ids[tmp_loop]);
								
				fclose(threads_fp);
				rename("thread_TIDs.tmp", "thread_TIDs");
				work_busy_lock = 0;
			}
		//}
		pthread_mutex_unlock(&tid_lock);

		while (work_busy_lock != 0) {}
#endif

	int matrix_id;

    while ( (matrix_id = get_matrix()) >= 0) {
      
		double* A = matricesA[matrix_id];
		double* B = matricesB[matrix_id];
				
		unsigned N = matsz.get_value();

#ifdef __MIGRATE__		
		hwloc_bitmap_t set = hwloc_bitmap_alloc();
		
		hwloc_get_cpubind(topology, set, HWLOC_CPUBIND_THREAD);
		hwloc_get_last_cpu_location(topology, set, HWLOC_CPUBIND_THREAD);
		
		hwloc_bitmap_singlify(set);

		hwloc_set_area_membind ( topology, (const void*)A, N*N*sizeof(double), (hwloc_const_cpuset_t)set, HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_MIGRATE );
		hwloc_set_area_membind ( topology, (const void*)B, N*N*sizeof(double), (hwloc_const_cpuset_t)set, HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_MIGRATE );

#endif	
		int i,j,k;	
		  
//cholesky
		for (i=0; i<N*N; i++)
			A[i] = 1.0;	

		double s;
		
		for (i=0; i<N; i++) {
			for (j=0; j<(i+1); j++) {
				s=0;

				for (k=0; k<j; k++) 
					s += B[i * N + k] * B[j * N + k];
				
				if (i == j) {
					B[i * N + j] = sqrt( A[i * N + i] - s );
				}
				
				else {
					B[i * N + j] = ( 1.0 / B[j * N + j] * (A[i * N + j] - s) );
				}
			}
		}//for (i=0; i<N; i++)

	}

    return NULL;
}


int main(int argc, char *argv[]) {

#ifdef __MIGRATE__	
   	hwloc_topology_init(&topology);
	hwloc_topology_load(topology);
#endif	

	if (argc <= 1)
		return -10;

    if (clarg::parse_arguments(argc, argv)) {
        cerr << "Error when parsing the arguments!" << endl;
        return 1;
    }
        
    if (nmats.get_value() < 1) {
        cerr << "Error, nm must be >= 1" << endl;
        return 1;
    }

    if (pthread_mutex_init(&lock, NULL) != 0)
    {
      printf("\n mutex init failed\n");
      return 1;
    }

    init_matrices();

    int n_threads = nthreads.get_value();
    vector<pthread_t> threads(n_threads);

#ifdef __THREAD_LOG__
	thread_done = n_threads;
	thread_ids = (pid_t*)calloc(n_threads,sizeof(pid_t));
	if (thread_ids == NULL) {
		printf("\nError allocating thread_ids!");
		return -1;
	}
	
	if (pthread_mutex_init(&tid_lock, NULL) != 0) {
      printf("\n mutex init failed\n");
      return 1;
    }
	work_busy_lock = 1;
#endif

    double tempo = mysecond();

//pthread    
    for(int t=0; t<n_threads; t++)
      pthread_create(&threads[t], NULL, worker_thread, (void*) t);

    for(int t=0; t<n_threads; t++)
      pthread_join(threads[t], NULL);
//
    tempo = mysecond() - tempo;

    //printf("\n\n%d %f \n", matsz.get_value(), tempo);
	free_matrices();

#ifdef __MIGRATE__
	hwloc_topology_destroy(topology);
#endif

#ifdef __THREAD_LOG__
	free(thread_ids);
#endif

	return 0;
}
