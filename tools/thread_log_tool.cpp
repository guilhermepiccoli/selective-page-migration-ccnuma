#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <list>

//_kbhit()
#include <sys/select.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <stropts.h>

int _kbhit() {
    static const int STDIN = 0;
    static bool initialized = false;

    if (! initialized) {
        // Use termios to turn off line buffering
        termios term;
        tcgetattr(STDIN, &term);
        term.c_lflag &= ~ICANON;
        tcsetattr(STDIN, TCSANOW, &term);
        setbuf(stdin, NULL);
        initialized = true;
    }

    int bytesWaiting;
    ioctl(STDIN, FIONREAD, &bytesWaiting);
    return bytesWaiting;
}

//


int which_cpu (pid_t tid) {

//shamelessly borrowed from hwloc

//read /proc/pid/stat.
//its second field contains the command name between parentheses,
//and the command itself may contain parentheses, so read the whole line
//and find the last closing parenthesis to find the third field.

	char buf[1024] = {0};
	char name[96] = {0};
	char *tmp;
	FILE *file;
	int i;

	
	//tid = syscall(SYS_gettid);
	
	snprintf(name, sizeof(name), "/proc/%lu/stat", (unsigned long) tid);
	
	file = fopen(name, "r");
	if (!file) {
		printf("\nError in reading /proc/%lu/stat", (unsigned long) tid);
		return -1;
	}
	
	tmp = fgets(buf, sizeof(buf), file);
	fclose(file);
	
	if (!tmp) {
		printf("\nError in fgets - tmp is NULL. ABORTING!");
		exit(1);
	}

	tmp = strrchr(buf, ')');
	
	if (!tmp) {
		printf("\nError in fgets - tmp is NULL. ABORTING!");
		exit(1);
	}
	/* skip ') ' to find the actual third argument */
	tmp += 2;

	/* skip 35 fields */
	for(i=0; i<36; i++) {
		tmp = strchr(tmp, ' ');
		if (!tmp) {
			printf("\nError in fgets - tmp is NULL. ABORTING!");
			exit(1);
		}

		/* skip the ' ' itself */
		tmp++;
	}

	/* read the last cpu in the 38th field now */
	if (sscanf(tmp, "%d ", &i) != 1) {
		printf("\nError in sscanf, which is !=1. ABORTING!");
		exit(1);
	}
	return i;
} 


int main (int argc, char *argv[]) {

	int *tids, *last_cpus;
	std::list<int> **cpus;
	FILE *fp, *log;

	int i=0, processor=-1;

	if (argc != 2 ) {
		printf("\nError in input! USAGE: %s <num_threads>\n", argv[0]);
		return 1;	
	}

	int num_threads = atoi(argv[1]);

	tids = (int*)calloc( num_threads,sizeof(int) );
	last_cpus = (int*)calloc( num_threads,sizeof(int) );

	if ( (tids == NULL) || (last_cpus == NULL) ) {
		printf("\nError in allocating tids or last_cpus. ABORTING!\n");
		return -1;
	}

	cpus = (std::list<int>**)malloc( num_threads*sizeof(std::list<int>**) );
	if (cpus == NULL) {
		printf("\nError in allocating cpus. ABORTING!\n");
		return -1;
	}
	
	for (i=0; i < num_threads; ++i) {
		cpus[i] = new std::list<int>;
		if (cpus[i] == NULL) {
			printf("\nError in allocating cpus[%d]. ABORTING!\n",i);
			return -1;
		}
	}

	//busy-waiting until the file exists
	while (1)
		if ( access("thread_TIDs", F_OK) != -1 )
			break;


	//reading TIDs from file
	fp = fopen("thread_TIDs", "r");
	if (fp == NULL) {
		printf("\nError in file open (fp). ABORTING!\n");
		return -2;
	}

	for (i=0; i < num_threads; ++i) {
		fscanf(fp,"%d\n", &tids[i]);

		if (tids[i] == 0) {
			printf("\nError: tids[%d] == 0. ABORTING!\n",i);
			return -3;
		}
		last_cpus[i] = -1;
	}

	fclose(fp);
	//	

	i=0;
	while( !_kbhit() ) { //until a key is pressed

		processor = which_cpu( (pid_t)tids[i] );

		if (processor != last_cpus[i]) {
			cpus[i]->push_back(processor);
			last_cpus[i] = processor;
		}

		i++;
		i %= 64;
	}

	log = fopen("threads_LOG","w");
	if (log == NULL) {
		printf("\nError in file open (threads_LOG). ABORTING!\n");
		return -2;
	} 

	std::list<int>::iterator it, it_e;
	for (i=0; i < num_threads; ++i) {
		fprintf(log, "\n%d (%d): ", i, tids[i]);

		it_e = cpus[i]->end();
		for (it = cpus[i]->begin(); it != it_e; ++it)
			fprintf(log, "%d, ", (*it));

		fflush(log);
		cpus[i]->clear();
	}

	fclose(log);

	free(tids);
	free(last_cpus);

	for (i=0; i < num_threads; ++i)
		delete cpus[i];
	free(cpus);

	remove("thread_TIDs");
	return 0;
}
