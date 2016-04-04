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
 * Fernando Magno Quintao Pereira
 * Edson Borin
 * Guilherme Piccoli
*/

/*
   This program implements a dummy string search algorithm. Initially we
   populate an array v with random characters in the range 'a-z'. The size of
   v is given by ARRAY_SIZE. Then, we give
   to each thread the task of:
   1) Generate NUM_QUERIES random pattern, with size
      [MINIMUM_STRING_SIZE, MAXIMUM_STRING_SIZE]
   2) Count the number of occurrences of the pattern in v.
   3) Report the maximum number of occurrences.
 */

#include <pthread.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 


#ifdef __MIGRATE__
	#include <hwloc.h>
	hwloc_topology_t topology;
#endif

long NUM_THREADS; 

#ifdef __THREAD_LOG__
	#include <unistd.h>
	#include <sys/syscall.h>
	pid_t *thread_ids;
	pthread_mutex_t tid_lock;
	char work_busy_lock;
	int thread_done;
	FILE *threads_fp;
#endif

//============================ BEGIN INPUT SIZE ==============================//
// Size of the smallest string that any thread will search for:
#define MINIMUM_STRING_SIZE   2

// Size of the largest string that any thread will search for:
#define MAXIMUM_STRING_SIZE  16 

// Number of searches that each thread will perform:
long NUM_QUERIES; 

// Size of the string where patterns will be searched:
long ARRAY_SIZE;

//allocate in parallel or serial way

long INIT_PARALLEL;
//============================== END INPUT SIZE ==============================//

// Array that will contain the search space.
char *v;

/*
   This function initializes an array with random characters. Each thread is
   in charge of initializing one block of the array.
 */
void *initArray(void *threadIdentifier) {
   const long TID = (long)threadIdentifier; 
   const long BLOCK_SIZE = ARRAY_SIZE / NUM_THREADS;
   const long MY_START = TID * BLOCK_SIZE;
   const long MY_END = (TID + 1) * BLOCK_SIZE;
   long long int i;

   for (i = MY_START; i < MY_END; i++) {
     // randomness suppressed
     v[i] = 'a';
   }
   if (INIT_PARALLEL == 1)
		pthread_exit(NULL); 
   return NULL;
}


/*
This function generates random patterns of characteres, and searches for
them in the string.
 */
void *searchStrings(void *threadIdentifier) {


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
			
			int end_loop = NUM_THREADS;
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


	const long TID       = (long)threadIdentifier; 
	const long MY_REGION = ARRAY_SIZE / NUM_THREADS;
	const long MY_START  = TID * MY_REGION;
	const long MY_END    = (TID + 1) * MY_REGION;
   

#ifdef __MIGRATE__
		hwloc_bitmap_t set = hwloc_bitmap_alloc();
		
		hwloc_get_cpubind(topology, set, HWLOC_CPUBIND_THREAD);
		hwloc_get_last_cpu_location(topology, set, HWLOC_CPUBIND_THREAD);
		
		hwloc_bitmap_singlify(set);
		
		hwloc_set_area_membind ( topology, (const void*)(v+MY_START), MY_REGION, (hwloc_const_cpuset_t)set, HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_MIGRATE );		
		
		hwloc_bitmap_free(set);			
#endif	   
   

	long long int  i, j, k, num_occurrences, max_occur = 0;
	char most_freq[MAXIMUM_STRING_SIZE + 1];

	for (i = 0; i < MAXIMUM_STRING_SIZE + 1; i++)
		most_freq[i] = '\0';


		int str_size = MAXIMUM_STRING_SIZE;
		 
		if (str_size < MINIMUM_STRING_SIZE)
			str_size = MINIMUM_STRING_SIZE;

	for (i = 0; i < NUM_QUERIES; i++) { //for.cond2
 
		char* pattern = (char*) malloc(str_size);
		 
		for (j = 0; j < str_size; j++)
			pattern[j] = 'b'; // randomness suppressed!

		num_occurrences = 0;
		 
		for (j = MY_START; j < MY_END - str_size + 1; j++) { //for.cond19
			int success = 1;

			for (k = 0; k < str_size; k++) { //for.cond26
				
				if (v[j + k] != pattern[k]) {
					success = 0;
					break;
				}
			}

			if (success)
				num_occurrences++;
		}
		 
		 
		if (num_occurrences > max_occur) {
			strncpy(most_freq, pattern, str_size);
			most_freq[str_size] = '\0';
			max_occur = num_occurrences;
		}
	}

	//printf("TID = %d found %d occurrences of %s\n", TID, max_occur, most_freq);

	return NULL;
}


/*
   Print the contents of the array.
 */
void printArray(char* array) {

  int t = 0;
  printf("Contents of the array:");
  for (t = 0; t < ARRAY_SIZE; t++) {
    if (t % 40 == 0) {
      printf("\n");
    }
    printf("%2c", array[t]);
  }
  printf("\n");
}


int main (int argc, char *argv[]) { 

  if (argc != 5) {
    fprintf(stderr, "Syntax: %s num_threads array_size num_queries initparallel\n", argv[0]);
    fprintf(stderr, "- num_points >> num_K\n");
    return -1;
  } else {

#ifdef __MIGRATE__
	hwloc_topology_init(&topology);
	hwloc_topology_load(topology);
#endif

    pthread_t* init_threads;
    pthread_t* search_threads;
    int t; 

    // Read input arguments
    NUM_THREADS = atol(argv[1]);
    ARRAY_SIZE  = atol(argv[2]);
    NUM_QUERIES = atol(argv[3]);
	INIT_PARALLEL = atol(argv[4]);

    // Initialize array data-structures
    
    v = (char*) malloc(ARRAY_SIZE);

    if (v == NULL) {
		fprintf(stderr, "\nError allocating v! Aborting...\n");
		return -1;
	}
 
    init_threads = (pthread_t*) malloc(NUM_THREADS * sizeof(pthread_t));
    search_threads = (pthread_t*) malloc(NUM_THREADS * sizeof(pthread_t));


    //========================= ARRAY INITIALIZATION ==========================//
    
	if (INIT_PARALLEL == 1) {
		// Initialize the array of initArray threads:
		for(t = 0; t < NUM_THREADS; t++){ 
		  int rc = pthread_create(&init_threads[t], NULL, initArray, (void *)t); 
		  if (rc) { 
			printf("ERROR: pthread_create(initArray) is %d\n", rc); 
			return -1; 
		  } 
		} 

		// Wait for the other threads.

		for(t = 0; t < NUM_THREADS; t++) { 
		  void* status;
		  int rc = pthread_join(init_threads[t], &status);
		  if (rc) { 
			printf("ERROR: return code from pthread_join() is %d\n", rc); 
			return -1; 
		  } 
		}
	}
	else {
		for(t=0; t < NUM_THREADS; t++) 
			initArray( (void *)t );
	}

    // printArray();

#ifdef __THREAD_LOG__
	thread_done = NUM_THREADS;
	thread_ids = (pid_t*)calloc(NUM_THREADS,sizeof(pid_t));
	if (thread_ids == NULL) {
		printf("\nError allocating thread_ids!");
		exit(1);
	}
	
	if (pthread_mutex_init(&tid_lock, NULL) != 0) {
      printf("\n mutex init failed\n");
      exit(1);
    }
	work_busy_lock = 1;
#endif



    //============================= ARRAY SEARCH ==============================//
    
    // Initialize the array of searchString threads:

    for(t = 0; t < NUM_THREADS; t++){ 
      int rc = pthread_create(&search_threads[t], NULL, searchStrings,
          (void *)t); 
      if (rc) { 
        printf("ERROR: pthread_create(searchStrings) is %d\n", rc); 
        return -1; 
      } 
    } 

   // Wait for the other threads.
   for(t = 0; t < NUM_THREADS; t++) {
     void* status;
     int rc = pthread_join(search_threads[t], &status);
     if (rc) {
       printf("ERROR: return code from pthread_join() is %d\n", rc);
       return -1;
     }
   }

#ifdef __MIGRATE__  
	hwloc_topology_destroy(topology);
#endif 	

  }
	free(v);
	//free(init_threads);
    //free(search_threads);

#ifdef __THREAD_LOG__
	free(thread_ids);
#endif

	return 0;
}
