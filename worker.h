//Rebecca Hanessian
//CS 4760
//Project 4
//header file

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
#define READY 0
#define RUNNING 1
#define BLOCKED 2
#define TERMINATED 3

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
    int remainingNano;		// how much time in ns is left before process terms
    int state;				// what is state
    int readyQ;				// is the process in the ready queue, not shown in the printed table
};

struct simClock{
	long long seconds;
	long long nanoseconds;
};

struct sharedMem{
	struct simClock ossClock;
	struct PCB table[MAXPROC];
};

struct msgbufWorker {
    long mtype;
    int status;
    int intData;
    int usedNanoTime;
    int pcbIndex;
};

struct msgbufOSS {
	long mtype;
	char message[50];
	int intData;
	int quantumNano;
};

struct circQueue {
	int processes[MAXPROC];
	int front;
	int back;
	int size;
};