//Rebecca Hanessian
//CS 4760
//Project 4: Process Scheduling
//oss file

#include "worker.h"

// Create options struct for command line arguments
typedef struct {
    int proc;
    int simul;
    float time;
    float inter;
    char logfile[50];
} options_t;

// Create an array of state names used for printing table
const char* stateNames[] = {
    "READY",
    "RUNNING",
    "BLOCKED",
    "TERMINATED",
    "EMPTY"
};

// Create global variables
int totalWorkers = 0;
int launchNumber = 0;
int activeWorkers = 0;
int ossMsgsSent = 0;
int shmid = -1;
struct sharedMem *shm = NULL;
int printNano = 500000000;
int printSec = 0;
int timeQuantum = 25000000;
int waitTime = 10000000;
struct circQueue blockQ;
struct circQueue readyQ;
options_t options;
FILE *fptr = NULL;
long long timeMaxNano = 0;
long long interNano = 0;
long long cpuNano = 0;
long long totalSimNano = 0;
int totalWorkersToLaunch = 0;
int maxSimul = 0;
struct msgbufWorker msgWorker;
struct msgbufOSS msgOSS;
int msqid = -1;
struct simClock nextLaunch;
struct simClock eventTime;
int linesNum = 0;
const int MAXLINES = 10000;
char printMessage[100] = {0};
int logLimitAlert = 0;

// Function to limit logfile to 10000 lines
void logLimit(const char *output) {
	if (fptr == NULL) {
		return;
	}
	if (linesNum >= MAXLINES) {
		if (!logLimitAlert) {
			fprintf(stdout, "Maximum logfile output reached. Nothing more will be logged.\n");
			logLimitAlert = 1;
		}
		return;
	}

    int lines = 0;
    for (int i = 0; output[i]; i++) {
        if (output[i] == '\n') lines++;
    }
    if (output[strlen(output)-1] != '\n') lines++;

    if (linesNum + lines <= MAXLINES) {
        fprintf(fptr, "%s", output);
        fflush(fptr);
        linesNum += lines; 
    } else {
    	if (!logLimitAlert) {
    		fprintf(stdout, "Maximum logfile output reached. Nothing more will be logged.\n");
    		logLimitAlert = 1;
    	}
    	linesNum = MAXLINES;
    }
}

// Function to direct output to correct stream utilizing logfile limit
void writeOutput(FILE *stream, const char *message) {
    if (stream == fptr) {
        logLimit(message);
    } else {
        fprintf(stream, "%s", message);
    }
}

// Function to print ready queue
void printQueue(FILE *stream, const char *qName, int items[], int front, int count) {
    char message[500];
    
    snprintf(message, sizeof(message), "%s [ ", qName);
    writeOutput(stream, message);
    
    if (count == 0) {
    	writeOutput(stream, " ]\n");
    	return;
    }
    	
    int ind = front;
    for (int i = 0; i < count; i++) {
    	int tableIndex = items[ind];
    	
    	if (tableIndex >= 0 && tableIndex < MAXPROC) {
    		snprintf(message, sizeof(message), "P%d", shm->table[tableIndex].pid);
    	} else {
    		snprintf(message, sizeof(message), "Empty");
    	}
    	writeOutput(stream, message);
        
        if (i < count - 1) {
            writeOutput(stream, " ");
        }
        ind = (ind + 1) % MAXPROC;
    }
    writeOutput(stream, " ]\n");
}

// Function to add two clock times together
struct simClock addClocks(struct simClock a, struct simClock b) {
	struct simClock result;
	result.seconds = a.seconds + b.seconds;
	result.nanoseconds = a.nanoseconds + b.nanoseconds;
	if (result.nanoseconds >= 1000000000) {
		result.seconds += result.nanoseconds / 1000000000;
		result.nanoseconds = result.nanoseconds % 1000000000;
	}
	return result;
}

// Function to initialize queue
void initQueue(struct circQueue *q) {
	q->front = -1;
    q->back = -1;
    q->count = 0;
    for (int i = 0; i < MAXPROC; i++) {
        q->processes[i] = -1;
    }
}

// Check if queue is full or empty
int isEmpty(struct circQueue *q) {
    return q->count == 0;
}

// Function to add to queue
void enqueue(struct circQueue *q, int i) {
    if ((q->back + 1) % MAXPROC == q->front) {
        return;
    }
    if (q->front == -1) {
        q->front = 0;
        q->back = 0;
    } else {
        q->back = (q->back + 1) % MAXPROC;
    }
    q->processes[q->back] = i;
    q->count++;
}

// Function to get next process to run from queue
int dequeue(struct circQueue *q) {
    if (q->front == -1) return -1;
    int value = q->processes[q->front];
    q->processes[q->front] = -1;
    if (q->front == q->back) {
        q->front = -1;
        q->back = -1;
    } else {
        q->front = (q->front + 1) % MAXPROC;
    }
    q->count--;
    return value;
}

// Function to remove process from blocked queue
void removeBlock(struct circQueue *blockQ, int ind){
	struct circQueue intQ;
	initQueue(&intQ);
	while (!isEmpty(blockQ)) {
		int proc = dequeue(blockQ);
		if (proc != ind) {
			enqueue(&intQ, proc);
		}
	}
	*blockQ = intQ;
}

// Function to set process to ready
void setReady(int i) {
	shm->table[i].state = READY;
	if (!shm->table[i].ready) {
		shm->table[i].ready = 1;
		enqueue(&readyQ, i);
	}
}

// Function to set process to running
void setRunning(int i) {
	shm->table[i].state = RUNNING;
	shm->table[i].ready = 0;
}

// Function to set process to blocked
void setBlocked(int i) {
	shm->table[i].state = BLOCKED;
	eventTime.nanoseconds = waitTime;
	eventTime.seconds = 0;
	eventTime = addClocks(shm->ossClock, eventTime);

	shm->table[i].eventWaitSec = eventTime.seconds;
	shm->table[i].eventWaitNano = eventTime.nanoseconds;
	enqueue(&blockQ, i);
}

// Function to set process to terminated and update table
void setTerminated(int i) {
	shm->table[i].state = TERMINATED;
	shm->table[i].active = false;
	activeWorkers--;
}

// Function to generate overhead time quantum
int getTQ(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

// Function to increment clock
void incClock(long long nanoInc) {
	shm->ossClock.nanoseconds += nanoInc;
	totalSimNano += nanoInc;
	
	if (shm->ossClock.nanoseconds >= 1000000000) {
		(shm->ossClock.seconds)++;
		shm->ossClock.nanoseconds -= 1000000000;
	}
}

// Function to update the remaining run time for a process
void updateTime(int i, long long nanoUsed) {
	long long actualTimeUsed = llabs(nanoUsed);
	shm->table[i].remainingNano -= actualTimeUsed;
	shm->table[i].serviceTimeNano += actualTimeUsed;
	if (shm->table[i].remainingNano < 0) {
		shm->table[i].remainingNano = 0;
	}
}

// Function to send message to worker
void ossSendMsg(int i, int tq) {
	char message[100];
	snprintf(message, sizeof(message),
		"OSS: Sending message to worker PID %d at time %lld:%lld\n", 
		shm->table[i].pid, shm->ossClock.seconds, shm->ossClock.nanoseconds);
	writeOutput(stdout, message);
	writeOutput(fptr, message);

	msgOSS.mtype = i + 1;
	msgOSS.intData = shm->table[i].pid;
	msgOSS.quantumNano = tq;
	sprintf(msgOSS.message, "Message sent to PID %d from OSS\n", msgOSS.intData);
	if (msgsnd(msqid, &msgOSS, sizeof(msgOSS) - sizeof(long), 0) != 0) {
		fprintf(stderr, "OSS: msgsnd to worker %ld failed\n", msgOSS.mtype);
	} else {
		ossMsgsSent++;
	}
}

// Function to handle response message from worker
void handle(struct msgbufWorker msg) {
	int i = msg.pcbIndex;
	if (i < 0 || i >= MAXPROC) {
        fprintf(stderr, "OSS: Received invalid index %d\n", i);
        return;
    }
	long long timeUsed = msg.usedNanoTime;
	long long actualTime = llabs(timeUsed);
	
	cpuNano += actualTime;
	updateTime(i, actualTime);
	incClock(actualTime);
	
	char message[300];

	if (timeUsed < 0) {
    	setTerminated(i);
    	waitpid(shm->table[i].pid, NULL, 0);
	} else if (timeUsed == timeQuantum) {
		snprintf(message, sizeof(message),
			"OSS: Putting process with PID %d into ready queue\n", 
			shm->table[i].pid);
		writeOutput(stdout, message);
		writeOutput(fptr, message);
		printQueue(stdout, "OSS: Ready queue", readyQ.processes, readyQ.front, readyQ.count);
		printQueue(fptr, "OSS: Ready queue", readyQ.processes, readyQ.front, readyQ.count);
		setReady(i);
	} else {
		snprintf(message, sizeof(message),
			"OSS: PID %d did not use its entire time quantum\n"
			"OSS: Putting process with PID %d into blocked queue\n", 
			shm->table[i].pid,
			shm->table[i].pid);
		writeOutput(stdout, message);
		writeOutput(fptr, message);
		printQueue(stdout, "OSS: Ready queue", readyQ.processes, readyQ.front, readyQ.count);
		printQueue(fptr, "OSS: Ready queue", readyQ.processes, readyQ.front, readyQ.count);
		setBlocked(i);
	}
}

// Function to select the next process to run from dequeue and logic
int getNextRun() {
	return dequeue(&readyQ);
}

// Function to assign a total run time to a process
long long assignTime(int i, long long maxNs) {
	long long t = (rand() % maxNs) + 1;
	shm->table[i].remainingNano = t;
	return t;
}

// Function to check if process block time is up
int timeComplete(struct simClock a, struct simClock b) {
	if (a.seconds > b.seconds) return 1;
    if (a.seconds < b.seconds) return 0;
    return a.nanoseconds >= b.nanoseconds;	
}

// Function to clear a PCB slot
void clearPCBslot(int i) {
	shm->table[i].occupied = false;
	shm->table[i].launched = 0;
	shm->table[i].pid = 0;
    shm->table[i].startS = 0;
    shm->table[i].startN = 0;
	shm->table[i].serviceTimeSec = 0;
    shm->table[i].serviceTimeNano = 0;
    shm->table[i].eventWaitSec = 0;
    shm->table[i].eventWaitNano = 0;
    shm->table[i].remainingNano = 0;
    shm->table[i].ready = 0;
    shm->table[i].state = 4;
    shm->table[i].active = false;
}

// Function to find empty slot in process table
int getEmpty() {
	for (int i = 0; i < MAXPROC; i++) {
		if (shm->table[i].occupied == false) {
			return i;
		} 
	}
	for (int j = 0; j < MAXPROC; j++) {
		if (shm->table[j].active == false) {
			clearPCBslot(j);
			return j;
		}
	}
	return -1;
}

// Function to get next launch time
struct simClock getNextLaunchTime() {
	struct simClock intervalClock = {0, interNano};
	return addClocks(shm->ossClock, intervalClock); 
}

// Function to fork and exec a worker
void forkWorker(int i, long long secDuration, long long nanoDuration) {
	pid_t pid = fork();
	if (pid == 0) {
		char sec[256], nano[256], ind[10];
		snprintf(sec, sizeof(sec), "%lld", secDuration);
		snprintf(nano, sizeof(nano), "%lld", nanoDuration);
		snprintf(ind, sizeof(ind), "%d", i);
        char *newargv[] = {"./worker", sec, nano, ind, NULL};
        execvp(newargv[0],newargv);
        perror("Execvp error\n");
        exit(EXIT_FAILURE);
	} else if (pid > 0) {
		totalWorkers++;
		activeWorkers++;
						
		launchNumber = totalWorkers;
		nextLaunch = getNextLaunchTime();
						
		shm->table[i].launched = totalWorkers;
		shm->table[i].occupied = true;
		shm->table[i].pid = pid;
		shm->table[i].active = true;
    	shm->table[i].startS = shm->ossClock.seconds;
    	shm->table[i].startN = shm->ossClock.nanoseconds;
    	shm->table[i].serviceTimeSec = 0;
    	shm->table[i].serviceTimeNano = 0;
    	setReady(i);
	} else {
		perror("OSS: Fork error\n");
		exit(EXIT_FAILURE);
	}
}

// Function to determine if oss should launch a new process
int chooseLaunch() {
	if (totalWorkers >= totalWorkersToLaunch) return 0;
	if (activeWorkers >= maxSimul) return 0;
	if (!timeComplete(shm->ossClock, nextLaunch)) return 0;
	return 1;
}

// Function to check the blocked queue for processes to move to ready
void checkBlockQ() {
	int count = blockQ.count;
	char message[100];

    for (int k = 0; k < count; k++) {
        int j = dequeue(&blockQ);
        if (j == -1) break;

        struct simClock procEvent;
        procEvent.seconds = shm->table[j].eventWaitSec;
        procEvent.nanoseconds = shm->table[j].eventWaitNano;

        if (timeComplete(shm->ossClock, procEvent)) {
        	snprintf(message, sizeof(message),
        		"OSS: Unblocking process with PID %d at time %lld:%lld\n",
                    shm->table[j].pid,
                    shm->ossClock.seconds,
                    shm->ossClock.nanoseconds);
            writeOutput(stdout, message);
            writeOutput(fptr, message);

            setReady(j);
        } else {
            enqueue(&blockQ, j);
        }
    }
}

// Function to clean up at the end of program
void cleanup() {
	if (msqid != -1 && msgctl(msqid, IPC_RMID, NULL) == -1) {
		perror("OSS: msgctl to get rid of queue in parent failed\n");
	}
	if (fptr != NULL) {
		fclose(fptr);
	}
	for (int i = 0; shm != NULL && i < MAXPROC; i++) {
		if (shm->table[i].occupied == 1 && shm->table[i].pid > 0) {
			kill(shm->table[i].pid, SIGTERM);
		}
	}
	while(wait(NULL) > 0);
	if (shm != NULL && shm != (void *) -1) {
		shmdt(shm);
	}
	if (shm > 0) {
		shmctl(shmid, IPC_RMID, NULL);
	}
}

// Function to catch termination signals from ctrl-C and sigalrm
void cleanTerm (int signal) {
	if (signal == SIGALRM) {
		fprintf(stderr, "\n3 seconds passed. Terminating.\n");
	} else if (signal == SIGINT) {
		fprintf(stderr, "\nCtrl-C entered. Terminating.\n");
	} else if (signal == SIGSEGV) {
		fprintf(stderr, "\nSeg fault occurred. Terminating.\n");
	}
    exit(EXIT_FAILURE);
}

// Function to print -h usage message
void printUsage (const char* argmt){
	fprintf(stderr, "Usage: %s [-h] [-n proc] [-s simul] [-t timeLimitForChildren] [-i intervalToLaunchInSeconds] [-f logfile]\n", argmt);
	fprintf(stderr, "	proc is the number of user processes to xz\n");
	fprintf(stderr, "	simul is the number of processes that can run simultaneously\n");
	fprintf(stderr, "	timeLimitForChildren is the maximum simulated time that should pass before child process terminates (in seconds).\n");
	fprintf(stderr, "	intervalToLaunchInSeconds is the minimum interval between launching child processes.\n");
	fprintf(stderr, "	logfile is the filename to write OSS output\n");
	fprintf(stderr, "Default proc is 15, default simul is 7, default timeLimitForChildren is 5.7,\n default intervalToLaunchInSeconds is 0.2, default logfile is 'logfile.txt'.\n");
}

// Function to print start of OSS to console and logfile
void printOSSstart(pid_t osspid, FILE *stream, options_t options){
	char message[500];
	snprintf(message, sizeof(message),
		"OSS starting, PID: %d\n"
		"Called with:\n"
		"-n %d\n-s %d\n-t %f\n-i %f\n-f %s\n\n", 
		osspid, options.proc, options.simul, options.time, options.inter, options.logfile);
	writeOutput(stream, message);
}

// Function to print process table to console and logfile
void printProcessTable(FILE *stream) {
	char message[500];
	char pcb[500];
	
	snprintf(message, sizeof(message),
		"OSS PID:%d  SysClock Seconds: %lld SysClock Nanoseconds: %lld\n"
		"Process Table:\n"
		"%-6s %-10s %-9s %-7s %-9s %-10s %-13s %-13s %-15s %-17s %-18s %-15s %-9s\n", 
		getpid(), 
		shm->ossClock.seconds, 
		shm->ossClock.nanoseconds,
		"Entry:", 
		"Occupied:", 
		"Launch #:", 
		"PID:", 
		"Active:", 
		"Start Sec:", 
		"Start Nano:", 
		"Service Sec:", 
		"Service Nano:", 
		"Event Wait Sec:", 
		"Event Wait Nano:", 
		"Remaining Nano:", 
		"State:"
	);
	writeOutput(stream, message);

    for (int i = 0; i < MAXPROC; i++) {
        snprintf(pcb, sizeof(message), 
        	"%-6d %-10d %-9d %-7d %-9d %-10d %-13lld %-13d %-15lld %-17d %-18lld %-15lld %-9s\n", 
            i + 1, 
            shm->table[i].occupied,
            shm->table[i].launched, 
            shm->table[i].pid, 
            shm->table[i].active,
            shm->table[i].startS, 
            shm->table[i].startN,
            shm->table[i].serviceTimeSec,   
            shm->table[i].serviceTimeNano,
            shm->table[i].eventWaitSec,
           	shm->table[i].eventWaitNano,
            shm->table[i].remainingNano,
            stateNames[shm->table[i].state]
        );
        writeOutput(stream, pcb);
    }
    writeOutput(stream, "----------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
	printQueue(stream, "OSS: Blocked queue", blockQ.processes, blockQ.front, blockQ.count);
}

// Function to print final report
void printFinalStats(FILE *stream) {
	double cpuUtilization = 0.0;
	char message[500];
	if (totalSimNano > 0) {
		cpuUtilization = ((double) cpuNano / (double) totalSimNano) * 100.0;
	}
	long long totalSimSec = totalSimNano / 1000000000;
	long long totalSimNanoTemp = totalSimNano % 1000000000;
	
	long long cpuSec = cpuNano / 1000000000;
	long long cpuNanoTemp = cpuNano % 1000000000;
	
	snprintf(message, sizeof(message),
		"\nFinal Report:\n"
		"Total simulated time: %lld s %lld ns\n"
		"Total CPU busy time: %lld s %lld ns\n"
		"Average CPU utilization: %.2f%%\n",
		totalSimSec, totalSimNanoTemp, cpuSec, cpuNanoTemp, cpuUtilization);
	writeOutput(stream, message);
}

int main (int argc, char *argv[]){
// Cleanup upon termination
	atexit(cleanup);
	
// Signal variables
	signal(SIGINT, cleanTerm);
	signal(SIGALRM, cleanTerm);
	signal(SIGSEGV, cleanTerm);

// Default options if none are provided by user
    options.proc = 15;
    options.simul = 7;
    options.time = 5.7;
    options.inter = 0.2;
	
	const char *fileName = "logfile.txt";
    strcpy(options.logfile, fileName);
	
	char opt;
	opterr = 0;
	
	// Getopt loop to get options values
	while ((opt = getopt (argc, argv, "hn:s:t:i:f:")) != -1)
		switch (opt) {
            case 'h':
                printUsage (argv[0]);
                return (EXIT_SUCCESS);
			case 'n':
				options.proc = atoi(optarg);
				break;
			case 's':
				options.simul = atoi(optarg);
				break;
			case 't':
				options.time = atof(optarg);
				break;
			case 'i':
				options.inter = atof(optarg);
				break;
			case 'f':
				strcpy(options.logfile, optarg);
				break;
			default:
				printf ("Invalid option %c\n", opt);
				printUsage (argv[0]);
				return (EXIT_FAILURE);		
		}
    
// Get oss pid and set option variables
    pid_t osspid = getpid();
    totalWorkersToLaunch = options.proc;
    maxSimul = options.simul;
    
// Seed random number
	srand((unsigned int)time(NULL));

// Begin timer to max oss duration
	alarm(3);
	
// Opening logfile
	fptr = fopen(options.logfile, "w");
    if (fptr == NULL) {
        perror("Error opening logfile.\n");
        return EXIT_FAILURE;
    }
	
// Print oss starting to console and logfile
	printOSSstart(osspid, fptr, options);
	printOSSstart(osspid, stdout, options);
	
// Create shared memory
	key_t ossKey = ftok("oss.c", 'c');
	if (ossKey == -1) {
		perror("OSS: ftok for shared memory failed\n");
		exit(EXIT_FAILURE);
	}
	shmid = shmget(ossKey, sizeof(struct sharedMem), 0644 | IPC_CREAT | IPC_EXCL);
	if (shmid == -1 && errno == EEXIST) {
		int oldShmid = shmget(ossKey, sizeof(struct sharedMem), 0644);
		if (oldShmid != -1) {	
			shmctl(oldShmid, IPC_RMID, NULL);
		}
		shmid = shmget(ossKey, sizeof(struct sharedMem), 0644 | IPC_CREAT | IPC_EXCL);
	}
	if (shmid == -1) {
		perror("ODD: shmget failed\n");
		exit(EXIT_FAILURE);
	}
	shm = shmat(shmid, NULL, 0);
	if (shm == (void*) -1) {
		perror("OSS: shmat failed");
		exit(EXIT_FAILURE);
	}
	memset(shm, 0, sizeof(struct sharedMem));
	
	fprintf(stdout, "OSS: Shared memory set up\n");
	logLimit("OSS: Shared memory set up\n");
		
// Create message queue
	FILE *queueFile = fopen("msgQueue.txt", "a");
	if (queueFile == NULL) {
		perror("OSS: failed to create msgQueue.txt");
		exit(EXIT_FAILURE);
	}
	fclose(queueFile);
	key_t msgkey;
	
	if ((msgkey = ftok("msgQueue.txt", 1)) == -1) {
		perror("OSS: ftok in OSS failed\n");
		exit(EXIT_FAILURE);
	}
	
	msqid = msgget(msgkey, PERMS | IPC_CREAT | IPC_EXCL); 
	if (msqid == -1 && errno == EEXIST) {
		int oldMsqid = msgget(msgkey, PERMS);
		if (oldMsqid != -1) {
			msgctl(oldMsqid, IPC_RMID, NULL);
		}
		msqid = msgget(msgkey, PERMS | IPC_CREAT | IPC_EXCL);
	}
	if (msqid == -1) {
		perror("OSS: msgget in OSS failed\n");
		exit(EXIT_FAILURE);
	}
	
	fprintf(stdout, "OSS: Message queue set up\n");
	logLimit("OSS: Message queue set up\n");
	
// Initialize occupied and pid columns in process table to 0
	for (int i = 0; i < MAXPROC; i++) {
    	shm->table[i].occupied = 0;
    	shm->table[i].pid = 0;
    	shm->table[i].state = EMPTY;
	}
	
// Initialize system clock to 0s 0ns
	shm->ossClock.seconds = 0;
	shm->ossClock.nanoseconds = 0;
	
	timeMaxNano = options.time * 1000000000LL;
    interNano = (long long)(options.inter * 1e9);
    
// Initialize ready and blocked queues
	initQueue(&readyQ);
	initQueue(&blockQ);

// Create simClocks to track when to print table/blocked q	
	struct simClock printTableClock = {0, 0};
	struct simClock printTableInc = {0, 500000000};
	
// Create loop to launch processes and send/receive messages
	while (totalWorkers < totalWorkersToLaunch || activeWorkers > 0) {
	
// Print process table and blocked queue
		if (timeComplete(shm->ossClock, printTableClock)) {
			printProcessTable(fptr);
			printProcessTable(stdout);
			
			printTableClock = addClocks(printTableClock, printTableInc);
		}
		
// Determine if a worker should be launched/launch new worker
		if (chooseLaunch()) {
			int emptySlot = getEmpty();
			if (emptySlot != -1) {
				long long burstTime = assignTime(emptySlot, timeMaxNano);
				long long secDuration = burstTime / 1000000000LL;
    			long long nanoDuration = burstTime % 1000000000LL;
				shm->table[emptySlot].occupied = true;
				forkWorker(emptySlot, secDuration, nanoDuration);
				snprintf(printMessage, sizeof(printMessage), 
					"OSS: Generating process with PID %d and putting it in the ready queue at %lld:%lld\n", 
					shm->table[emptySlot].pid, shm->ossClock.seconds, shm->ossClock.nanoseconds);
				writeOutput(stdout, printMessage);
				writeOutput(fptr, printMessage);
				printQueue(stdout, "OSS: Ready queue", readyQ.processes, readyQ.front, readyQ.count);
				printQueue(fptr, "OSS: Ready queue", readyQ.processes, readyQ.front, readyQ.count);
				int inc = getTQ(5000, 15000);
				incClock(inc);
				snprintf(printMessage, sizeof(printMessage),
					"OSS: Total time spent in dispatch was %d nanoseconds\n", inc);
				writeOutput(stdout, printMessage);
				writeOutput(fptr, printMessage);
			}
		}

// Check if any blocked workers should change to ready
		checkBlockQ();
		
// Schedule a ready process
		int procInd = getNextRun();
		if (procInd != -1) {
			setRunning(procInd);
			snprintf(printMessage, sizeof(printMessage),
				"OSS: Dispatching process with PID %d from ready queue at time %lld:%lld\n", 
				shm->table[procInd].pid, shm->ossClock.seconds, shm->ossClock.nanoseconds);
			writeOutput(stdout, printMessage);
			writeOutput(fptr, printMessage);
		}
		
// Send message to worker/schedule ready process
		if (procInd != -1) {
			setRunning(procInd);
			int inc = getTQ(500, 1500);
			incClock(inc);
			snprintf(printMessage, sizeof(printMessage),
				"OSS: Total time spent in dispatch was %d nanoseconds\n", inc);
			writeOutput(stdout, printMessage);
			writeOutput(fptr, printMessage);
			ossSendMsg(procInd, timeQuantum);
// Receive message from worker
			if (msgrcv(msqid, &msgWorker, sizeof(msgWorker) - sizeof(long), 555, 0) != -1) {
				snprintf(printMessage, sizeof(printMessage),
					"OSS: Receiving that worker PID %d ran for %lld nanoseconds\n", 
					msgWorker.intData, msgWorker.usedNanoTime);
				writeOutput(stdout, printMessage);
				writeOutput(fptr, printMessage);		
				handle(msgWorker);
			} else {
				fprintf(stderr, "OSS: msgrcv from worker %d failed\n", shm->table[procInd].pid);
			}
		} else {
			incClock(10000000);
		}
	}
// Print final report stats/PCB table
	printProcessTable(fptr);
	printProcessTable(stdout);
	
	printFinalStats(fptr);
	printFinalStats(stdout);

	return EXIT_SUCCESS;
}
