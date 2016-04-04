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

/* Created on: Feb 19, 2014 */
/* Author(s):
 * Raphael Ernani Rodrigues
 * Guilherme Piccoli
*/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <sys/time.h>
	
#ifdef __MIGRATE__
	#include <hwloc.h>
	hwloc_topology_t topology;
#endif

#ifdef __THREAD_LOG__
	#include <unistd.h>
	#include <sys/syscall.h>
	pid_t *thread_ids;
	pthread_mutex_t tid_lock;
	char work_busy_lock;
	int thread_done;
	FILE *threads_fp;
#endif

int NumThreads;

typedef struct {
	int* data;
	long size;
} IntVector;

typedef struct {

	int* data;
	long allocated_size;
	long numElements;

} Bucket;

void InitBucket(Bucket* B, long InitialSize){
	B->data = (int*)calloc(InitialSize, sizeof(int));
	B->allocated_size = InitialSize;
	B->numElements = 0;
}

void FreeBucket(Bucket* B){
	free(B->data);
	B->data = NULL;
}


void reallocate_vector(int** data, long old_size, long new_size){
	//allocate the new vector
	int* tmp = (int*)calloc(new_size, sizeof(int));
	long i;

	//copy the data to the new vector
	for(i = 0; i < old_size; i++){
		tmp[i] = (*data)[i];
	}

	//free the old vector and return the new one
	free(*data);
	*data = tmp;
}


void addElement(Bucket* B, int Element){
	B->data[B->numElements] = Element;
	B->numElements++;
	if ( B->numElements >= B->allocated_size ) {
		reallocate_vector(&(B->data), B->allocated_size, 2*B->allocated_size);
		B->allocated_size *= 2;
	}
}


IntVector generateRandomVector(long numElements){

	IntVector result;
	result.size = numElements;
	result.data = (int*)calloc(numElements, sizeof(int));

	long i;
	for (i = 0; i < numElements; i++){
		result.data[i] = rand();
	}

	return result;
}

void printVector(IntVector vector){
	long i;
	for (i = 0; i < vector.size; i++){
		printf("%d\n", vector.data[i]);
	}
}

void swap_values(int* a, int* b){
	int c = *a;
	*a = *b;
	*b = c;
}

void SelSort(int* a, long n){
	/* a[0] to a[n-1] is the array to sort */
	long i,j;
	long iMin;

	/* advance the position through the entire array */
	/*   (could do j < n-1 because single element is also min element) */
	for (j = 0; j < n-1; j++) {
	    /* find the min element in the unsorted a[j .. n-1] */

	    /* assume the min is the first element */
	    iMin = j;
	    /* test against elements after j to find the smallest */
	    for ( i = j+1; i < n; i++) {
	        /* if this element is less, then it is the new minimum */
	        if (a[i] < a[iMin]) {
	            /* found new minimum; remember its index */
	            iMin = i;
	        }
	    }

	    /* iMin is the index of the minimum element. Swap it with the current position */
	    if ( iMin != j ) {
	    	swap_values(&(a[j]), &(a[iMin]));
	    }
	}
}

void* SelSort_Bucket(void* arg){
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
				
				int end_loop = NumThreads;
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

	Bucket* vector = (Bucket*)arg;
	
#ifdef __MIGRATE__		
		hwloc_bitmap_t set = hwloc_bitmap_alloc();
		
		hwloc_get_cpubind(topology, set, HWLOC_CPUBIND_THREAD);
		hwloc_get_last_cpu_location(topology, set, HWLOC_CPUBIND_THREAD);
		
		hwloc_bitmap_singlify(set);

		hwloc_set_area_membind ( topology, (const void*)vector, sizeof(int)*(vector->allocated_size), (hwloc_const_cpuset_t)set, HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_MIGRATE );
#endif
	
	SelSort(vector->data, vector->numElements);
	pthread_exit((void*) arg);
}

void bucketSort(IntVector vector, int NumThreads){

	long i;

	Bucket buckets[NumThreads];

	//Allocate Memory
	for(i = 0; i < (long)NumThreads; i++){
		InitBucket(&(buckets[i]), (vector.size/NumThreads) + 1);
	}

	int min;
	int max;

	//Get Min and Max values
	for(i = 0; i < vector.size; i++){

		int element = vector.data[i];

		if (!i) { //first
			min = element;
			max = element;
		} else {

			if (min > element) min = element;
			if (max < element) max = element;

		}

	}

	int range = max-min;
	int bucket_lenght = range/NumThreads;

	//Distribute elements into buckets.
	for(i = 0; i < vector.size; i++){

		int element = vector.data[i];

		//Select bucket
		int BucketNumber = (element - min) / bucket_lenght;
		if(BucketNumber >= NumThreads) BucketNumber = NumThreads-1;

		addElement(&(buckets[BucketNumber]), element);
	}

#ifdef __THREAD_LOG__
	thread_done = NumThreads;
	thread_ids = (pid_t*)calloc(NumThreads,sizeof(pid_t));
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

	pthread_t thread[NumThreads];
	pthread_attr_t attr;

	/* Initialize and set thread detached attribute */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	//Sort Each Bucket Individually
	for (i = 0; i < NumThreads; i++) {

	    int rc = pthread_create(&thread[i], &attr, SelSort_Bucket, &(buckets[i]));
		if (rc) {
			printf("ERROR; return code from pthread_create() is %d\n", rc);
			exit(-1);
		 }
	}

	/* Free attribute and wait for the other threads */
	void *status;
	pthread_attr_destroy(&attr);
	for(i=0; i<NumThreads; i++) {
		int rc = pthread_join(thread[i], &status);
		if (rc) {
			printf("ERROR; return code from pthread_join() is %d\n", rc);
			exit(-1);
		}
	}

	//Concatenate the results of the buckets in the original vector
	long index = 0;

	for (i = 0; i < (long)NumThreads; i++) {
		long j;
		for(j = 0; j < buckets[i].numElements; j++){
			vector.data[index] = buckets[i].data[j];
			index++;
		}

		FreeBucket(&(buckets[i]));
	}

}

double mysecond()
{
  struct timeval tp;
  struct timezone tzp;
  gettimeofday(&tp,&tzp);
  return ( (double) tp.tv_sec + (double) tp.tv_usec * 1.e-6 );
}


int main(int argc, char** argv){
    //double tempo = mysecond();

#ifdef __MIGRATE__	
   	hwloc_topology_init(&topology);
	hwloc_topology_load(topology);
#endif	

	if (argc < 3) {

		char* programName = argv[0];
		printf("Invalid number of arguments\n\nUSAGE: %s NumElements NumThreads [seed]\n", programName);

		exit(1);
	}

	if(argc > 3){
		//we will use the same seed in order to generate always the same input
		srand(atoi(argv[3]));
	} else {
		//we will use the same seed in order to generate always the same input
		srand(0);
	}

	IntVector vector = generateRandomVector( atol(argv[1]) );

//	printVector(vector);
	NumThreads = atoi(argv[2]);
	bucketSort(vector, NumThreads);
//	printVector(vector);

	free(vector.data);

#ifdef __MIGRATE__
    hwloc_topology_destroy(topology);
#endif

    //tempo = mysecond() - tempo;
    //printf("\n\n%d %lf \n", atoi(argv[1]), tempo);

#ifdef __THREAD_LOG__
	free(thread_ids);
#endif

	return 0;
}
