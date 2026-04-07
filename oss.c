//Rebecca Hanessian
//CS 4760
//Project 4: Process Scheduling
//oss file

#include "worker.h"

// Create global variables
int shmid;
struct sharedMem *shm = NULL;
int printNano = 500000000;
int printSec = 0;
int lastLaunchNano = -1;
int launchNumber = 0;
int ossMsgsSent = 0;
pid_t nextPID;
int totalWorkers = 0;

// Create options struct for command line arguments
typedef struct {
    int proc;
    int simul;
    float time;
    float inter;
    char logfile[50];
} options_t;

// Function to calculate child process durations
void getProcessDuration(int min1,int max1, int min2, int max2, long long *secDur, long long *nanoDur) {
    int range1 = max1 - min1 + 1;
    int range2 = max2 - min2 + 1;
    if (range1 <= 0) range1 = 1;
    if (range2 <= 0) range2 = 1;
    *secDur = (rand() % range1) + min1;
    *nanoDur = (rand() % range2) + min2;
    if ((*nanoDur) >= 1000000000) {
		(*secDur) += (*nanoDur / 1000000000);
		(*nanoDur) %= 1000000000;
	}
}

// Function to print start of OSS to console and logfile
void printOSSstart(pid_t osspid, FILE *stream, options_t options){
	fprintf(stream, "OSS starting, PID: %d\n", osspid);
	fprintf(stream, "Called with:\n-n %d\n-s %d\n-t %f\n-i %f\n-f %s\n\n", options.proc, options.simul, options.time, options.inter, options.logfile);
}

// Function to print process table to console and logfile
void printProcessTable(struct sharedMem *shm, FILE *stream) {
    fprintf(stream, "OSS PID:%d  SysClock Seconds: %lld SysClock Nanoseconds: %lld\n", getpid(), shm->seconds, shm->nanoseconds);
    fprintf(stream, "Process Table:\n");
    fprintf(stream, "%-6s %-10s %-9s %-7s %-9s %-15s %-12s %-12s %-10s %-20s\n", "Entry:", "Occupied:", "Launch #:", "PID:", "Active:", "Start Seconds:", "Start Nano:", "End Seconds:", "End Nano:", "Messages From OSS:");
    
    for (int i = 0; i < MAXPROC; i++) {
        fprintf(stream, "%-6d %-10d %-9d %-7d %-9d %-15d %-12d %-12d %-10d %-20d\n", 
            i, 
            shm->table[i].occupied,
            shm->table[i].launched, 
            shm->table[i].pid, 
            shm->table[i].active,
            shm->table[i].startS, 
            shm->table[i].startN,
            shm->table[i].termS,   
            shm->table[i].termN,
            shm->table[i].msgsSent);
    }
    fprintf(stream, "---------------------------------------------------------------------------------\n");
}

// Function to get next child to send message
pid_t getNextWorker(pid_t currentPID) {
    static int lastIdx = 0; 

    for (int i = 0; i < MAXPROC; i++) {
        if (shm->table[i].occupied && shm->table[i].pid == currentPID) {
            lastIdx = i;
            break;
        }
    }
    
    for (int i = 1; i <= MAXPROC; i++) {
        int checkIdx = (lastIdx + i) % MAXPROC;

        if (shm->table[checkIdx].occupied && shm->table[checkIdx].active) {
            lastIdx = checkIdx;            
            return shm->table[checkIdx].pid;
        }
    }
    return -1; 
}

// Function to print ending report
void printSummary(struct sharedMem *shm, FILE *stream, pid_t osspid, int totalWorkers) {
	fprintf(stream, "OSS PID: %d Terminating\n", osspid);
	fprintf(stream, "%d workers were launched and terminated\n", totalWorkers);
	fprintf(stream, "%d messages sent from OSS to workers.\n", ossMsgsSent);
	fprintf(stream, "Workers ran for a combined time of %lld seconds %lld nanoseconds\n", shm->seconds, shm->nanoseconds);
}
 
// Function to catch termination signals from ctrl-C and sigalrm
void cleanTerm (int signal) {
	if (signal == SIGALRM) {
		fprintf(stderr, "\n60 seconds passed. Terminating.\n");
	} else if (signal == SIGINT) {
		fprintf(stderr, "\nCtrl-C entered. Terminating.\n");
	}
	
	for (int i = 0; i < MAXPROC; i++) {
		if (shm->table[i].occupied == 1 && shm->table[i].pid > 0) {
			kill(shm->table[i].pid, SIGTERM);
		}
	}
	
	while(wait(NULL) > 0);
	
	shmdt(shm);
	shmctl(shmid, IPC_RMID, NULL);

    exit(EXIT_SUCCESS);
}

// Function to find empty slot in process table
int getEmpty() {
	for (int i = 0; i < MAXPROC; i++) {
		if (shm->table[i].occupied == false) {
			return i;
		} else if (shm->table[i].active == false) {
			shm->table[i].occupied = false;
			shm->table[i].launched = 0;
			shm->table[i].pid = 0;
           	shm->table[i].startS = 0;
           	shm->table[i].startN = 0;
           	shm->table[i].termS = 0;
           	shm->table[i].termN = 0;
           	shm->table[i].msgsSent = 0;
           	return i;
		}
	}
	return -1;
}

// Function to print -h usage message
void print_usage (const char* argmt){
	fprintf(stderr, "Usage: %s [-h] [-n proc] [-s simul] [-t timeLimitForChildren] [-i intervalToLaunchInSeconds] [-f logfile]\n", argmt);
	fprintf(stderr, "	proc is the number of user processes to launch\n");
	fprintf(stderr, "	simul is the number of processes that can run simultaneously\n");
	fprintf(stderr, "	timeLimitForChildren is the maximum simulated time that should pass before child process terminates.\n");
	fprintf(stderr, "	intervalToLaunchInSeconds is the minimum interval between launching child processes.\n");
	fprintf(stderr, "	logfile is the filename to write OSS output\n");
	fprintf(stderr, "Default proc is 10, default simul is 3, default timeLimitForChildren is 5.7,\n default intervalToLaunchInSeconds is 0.2, default logfile is 'logfile.txt'.\n");
}

int main (int argc, char *argv[]){
// Signal variables
	signal(SIGINT, cleanTerm);
	signal(SIGALRM, cleanTerm);

// PID and options variables and struct creation
	pid_t lastPID = 0;
	pid_t pid = 0;
    char opt;
    options_t options;

// Default options if none are provided by user
    options.proc = 10;
    options.simul = 3;
    options.time = 5.7;
    options.inter = 0.2;
    
    const char* fileName = "logfile.txt";
    strcpy(options.logfile, fileName);
	
	opterr = 0;

// Getopt loop to get options values
	while ((opt = getopt (argc, argv, "hn:s:t:i:f:")) != -1)
		switch (opt) {
            case 'h':
                print_usage (argv[0]);
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
				print_usage (argv[0]);
				return (EXIT_FAILURE);		
		}

// Creation of logfile
	char *filename = options.logfile;
	FILE *fptr = fopen(filename, "w");
    if (fptr == NULL) {
        perror("Error opening logfile.\n");
        return EXIT_FAILURE;
    }
    
// Get oss pid
    pid_t osspid = getpid();
    
// Get random number between 1 and -t value to assign to child process duration
    srand((unsigned int)time(NULL));
	int timeSec = (int)options.time;
	int timeNano = (int)((options.time - timeSec) * 1e9 + 0.5);
	int minSec = 1, minNano = 0;
	long long secDuration = 1, nanoDuration = 0;

// Begin timer to max oss duration
	alarm(60);
	
// Print oss starting to console and logfile
	printOSSstart(osspid, fptr, options);
	printOSSstart(osspid, stdout, options);
	
// Create shared memory
	key_t ossKey = ftok("oss.c", 'c');
	shmid = shmget(ossKey, sizeof(struct sharedMem), 0644 | IPC_CREAT);
	shm = shmat(shmid, 0, 0);
	memset(shm, 0, sizeof(struct sharedMem));
		
// Create message queue
	struct msgbufWorker msgWorker;
	struct msgbufOSS msgOSS;
	int msqid;
	system("touch msgQueue.txt");
	key_t msgkey;
	
	if ((msgkey = ftok("msgQueue.txt", 1)) == -1) {
		perror("ftok");
		exit(EXIT_FAILURE);
	}
	
	if ((msqid = msgget(msgkey, PERMS | IPC_CREAT)) == -1) {
		perror("msgget in OSS");
		exit(EXIT_FAILURE);
	}
	
	fprintf(stdout, "OSS: Message queue set up.\n");
	fprintf(fptr, "OSS: Message queue set up.\n");
	
// Initialize occupied and pid columns in process table to 0
	for (int i = 0; i < MAXPROC; i++) {
    	shm->table[i].occupied = 0;
    	shm->table[i].pid = 0;
	}
	
	memset(&(shm->table), 0, sizeof(struct PCB));
	
// Initialize active workers and the ns since last launch
	int activeWorkers = 0;
	
	long long lastLaunchNano = -1;

// Initialize system clock to 0s 0ns
	shm->seconds = 0;
	shm->nanoseconds = 0;
	
	int nanosecInc = 250000000;
	
// Create loop to launch processes and send/receive messages
	while (totalWorkers < options.proc || activeWorkers > 0) {

// Increment system clock
		if (activeWorkers > 0) {
			nanosecInc = 250000000 / activeWorkers;
		} 
		shm->nanoseconds += nanosecInc;
		if (shm->nanoseconds >= 1000000000) {
			(shm->seconds)++;
			shm->nanoseconds -= 1000000000;
		}
		
// Print process table
		if (shm->seconds > printSec || (shm->seconds == printSec && shm->nanoseconds >= printNano)) {
			printProcessTable(shm, fptr);
			printProcessTable(shm, stdout);
			
			printNano += 500000000;
			if (printNano >= 1000000000) {
				printSec++;
				printNano -= 1000000000;
			}
		}

// Calculate next worker to message	
		nextPID = 0;
		if (activeWorkers > 0) {
			if (lastPID == 0) {
				for (int i = 0; i < MAXPROC; i++) {
					if (shm->table[i].occupied && shm->table[i].pid > 0 && shm->table[i].active) {
						nextPID = shm->table[i].pid;
						break;
					}
				}
			} else {
				nextPID = getNextWorker(lastPID);
			}
		
// Send message to worker
			if (nextPID > 0) {
				fprintf(stdout, "OSS: Sending message to worker PID %d at time %lld: %lld.\n", nextPID, shm->seconds, shm->nanoseconds);
				fprintf(fptr, "OSS: Sending message to worker PID %d at time %lld: %lld.\n", nextPID, shm->seconds, shm->nanoseconds);
				msgOSS.mtype = nextPID;
				msgOSS.intData = nextPID;
				sprintf(msgOSS.message, "Message sent to PID %d from OSS.\n", msgOSS.intData);
				if (msgsnd(msqid, &msgOSS, sizeof(msgOSS) - sizeof(long), 0) != 0) {
					fprintf(stderr, "OSS: msgsnd to worker %d failed\n", nextPID);
				} else {
					ossMsgsSent++;
// Receive message from worker
					if (msgrcv(msqid, &msgWorker, sizeof(msgWorker) - sizeof(long), osspid, 0) != -1) {
						fprintf(stdout, "OSS: Receiving message from worker PID %d at time %lld: %lld.\n", nextPID, shm->seconds, shm->nanoseconds);		
						fprintf(fptr, "OSS: Receiving message from worker PID %d at time %lld: %lld.\n", nextPID, shm->seconds, shm->nanoseconds);		
						
						if (msgWorker.status == 0) {
							fprintf(stdout, "OSS: Worker PID %d is planning to terminate.\n", nextPID);
							fprintf(fptr, "OSS: Worker PID %d is planning to terminate.\n", nextPID);
				
							for (int i = 0; i < MAXPROC; i++) {
								if (shm->table[i].pid == nextPID){
									(shm->table[i].msgsSent)++;
									if (msgWorker.intData == -1 || true) {
                            			fprintf(stdout, "OSS: Child PID %d is terminating at %lld: %lld\n", nextPID, shm->seconds, shm->nanoseconds);
                            
                            			shm->table[i].active = false;
                            			activeWorkers--;
                            			waitpid(nextPID, NULL, 0); 
                            			break; 
                        			}
								}
							}
							lastPID = 0;	
						} else if (msgWorker.status == 1) {
							lastPID = nextPID;
						} else {
							fprintf(stderr, "OSS: msgrcv from worker %d failed.\n", nextPID);
						}
					}
				}
			}		
		}
		

		
// Launch new worker
		long long termSec = 0, termNano = 0;
		if (totalWorkers < options.proc && activeWorkers < options.simul) {
			long long sysInNano = (long long)shm->seconds * 1000000000LL + shm->nanoseconds;
        	long long interNano = (long long)(options.inter * 1e9);
			
			if (lastLaunchNano == -1 || (sysInNano >= lastLaunchNano + interNano)) {
				int emptySlot = getEmpty();
				if (emptySlot != -1) {
					getProcessDuration(minSec, timeSec, minNano, timeNano, &secDuration, &nanoDuration);
					
					long long totalDuration = ((long long)secDuration * 1000000000LL + nanoDuration) * (activeWorkers + 1);
        			long long finalTimeNano = sysInNano + totalDuration;
					
					termSec = finalTimeNano / 1000000000LL;
					termNano = finalTimeNano % 1000000000LL;

					shm->table[emptySlot].occupied = true;
					pid = fork();
					if (pid == 0) {
						char sec[256], nano[256];
						snprintf(sec, sizeof(sec), "%lld", secDuration);
						snprintf(nano, sizeof(nano), "%lld", nanoDuration);
           				char *newargv[] = {"./worker", sec, nano, NULL};
            			execvp(newargv[0],newargv);
            			perror("Execvp error\n");
            			exit(EXIT_FAILURE);
					} else if (pid > 0) {
						totalWorkers++;
						activeWorkers++;
						
						lastLaunchNano = sysInNano;
						launchNumber = totalWorkers;
						shm->table[emptySlot].launched = totalWorkers;
						shm->table[emptySlot].occupied = true;
						shm->table[emptySlot].pid = pid;
						shm->table[emptySlot].active = true;
    					shm->table[emptySlot].startS = shm->seconds;
    					shm->table[emptySlot].startN = shm->nanoseconds;
    					shm->table[emptySlot].termS = termSec;   
    					shm->table[emptySlot].termN = termNano;
					} else {
						perror("Fork error\n");
						exit(EXIT_FAILURE);
					}
				}
			}  
		}
	}
	
// Print final process table to console and logfile	
	printProcessTable(shm, fptr);
	printProcessTable(shm, stdout);
	
// Print ending report to console and logfile
	printSummary(shm, fptr, osspid, totalWorkers);
	printSummary(shm, stdout, osspid, totalWorkers);
	
// Get rid of message queue
	if (msgctl(msqid, IPC_RMID, NULL) == -1) {
		perror("msgctl to get rid of queue in parent failed.\n");
		exit(EXIT_FAILURE);
	}

// Close file and shared memory
	fclose(fptr);
	shmdt(shm);
	shmctl(shmid, IPC_RMID, NULL);

	return EXIT_SUCCESS;
}