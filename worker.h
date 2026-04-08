#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <time.h>



#define MAXPROC 18
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define MAXTEXT 200
#define PERMS 0644

struct PCB{
	bool occupied;			// is the slot being used
	int launched;			// number launched (if 5th then 5)   
    pid_t pid;  			// pid of child
    bool active;			// is child still running     
    int startS;				// time created seconds
    int startN;				// time created nanoseconds
    int serviceTimeSec; 	// total seconds it has been scheduled
    int serviceTimeNano; 	// total nanoseconds it has been scheduled
    int eventWaitSec;		// when does its event happen
    int eventWaitNano;		// when does its event happen
    bool blocked;			// is this process waiting on an event
    int msgsSent; 			// total number of messages received from OSS
};

struct sharedMem{
	long long seconds;
	long long nanoseconds;
	struct PCB table[MAXPROC];
};

struct msgbufWorker {
    long mtype;
    int status;
    int intData;
};

struct msgbufOSS {
	long mtype;
	char message[50];
	int intData;
};