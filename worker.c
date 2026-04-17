//Rebecca Hanessian
//CS 4760
//Project 4: Process Scheduling
//worker file

#include "worker.h"

int main (int argc, char *argv[]){
// Check for necessary arguments	
	if (argc < 4) {
		fprintf(stderr, "Worker: missing arguments.\n");
		exit(EXIT_FAILURE);
	}
	
// Initialize time and pid variables
	int durationS = atoi(argv[1]);
	int durationN = atoi(argv[2]);
	int processIndex = atoi(argv[3]);
	pid_t pid = getpid();
	pid_t ppid = getppid();
	
	fprintf(stderr, "\nWorker starting, PID: %d  PPID: %d\n", pid, ppid);
	fprintf(stderr, "Called with:\n\tProcess Time: %d seconds, %d nanoseconds\n\tIndex: %d\n\n", durationS, durationN, processIndex);

// Attach to shared memory simulated clock
	key_t ossKey = ftok("oss.c", 'c');
	int shmid = shmget(ossKey, sizeof(struct sharedMem), 0);
	if (shmid == -1) {
		perror("Worker: shmget failed.\n");
		exit(EXIT_FAILURE);
	}
	
	struct sharedMem *shm = (struct sharedMem *)shmat(shmid, NULL, 0);
	if (shm == (void *)-1) {
		perror("Worker: shmat failed.\n");
		exit(EXIT_FAILURE);
	}
	
// Attach to message queue	
	int messageCount = 0;
	struct msgbufWorker workerBuf;
	struct msgbufOSS ossBuf;
	workerBuf.mtype = 1;
	int msqid = 0;
	key_t msgkey;
	
	if ((msgkey = ftok("msgQueue.txt", 1)) == -1) {
		perror("ftok");
		exit(EXIT_FAILURE);
	}
	
	if ((msqid = msgget(msgkey, PERMS)) == -1) {
		perror("msgget in worker");
		exit(EXIT_FAILURE);
	}
	fprintf(stderr, "Worker %d is accessing the queue.\n", pid);
	int active = 1;
	
	fprintf(stderr, "WORKER PID: %d PPID: %d ", pid, ppid);
//	fprintf(stderr, "SysClockS: %lld SysClockNano: %lld\nTermTimeS: %lld TermTimeNano: %lld\n", shm->ossClock.seconds, shm->ossClock.nanoseconds, termSeconds, termNano);
	fprintf(stderr, "--Just Starting\n\n");
	
// Main loop	
	while (active) {
// Receive message from oss, increment count 
		if (msgrcv(msqid, &ossBuf, sizeof(ossBuf) - sizeof(long), pid, 0) == -1) {
			break;
		}	
		for (int i = 0; i < MAXPROC; i++) {
			if (shm->table[i].pid == getpid()) {
				(shm->table[i].msgsSent)++;
			}
		}
		
// Check remaining time and termination
		int remainingTime = shm->table[processIndex].remainingTime;
		long long value = remainingTime - ossBuf.quantumNano;
		int elapsedTime = 0;
		if (value =< 0) {
			elapsedTime = -remainingTime;
		}
	
// Decide whether or not to be blocked
		int blockedChance = rand() % 100;
		int toBlock = 0;
		long long nanoTime = (durationS * 1000000000LL) + durationN;
		long long blockedTime = 0;
		if (blockedChance < 20) {
			toBlock = 1;
		}

// Choose time to run before interruption
		if ((value > 0 ) && toBlock) {
			blockedStartTime = rand() % (ossBuf.quantumNano-1);
			elapsedTime = blockedStartTime;
		} else if ((value > 0) && !toBlock) {
			elapsedTime = ossBuf.quantumNano;
		}
		
// Send message back to oss
		workerBuf.usedNanoTime = elapsedTime;
		workerBuf.mtype = ppid;
		workerBuf.intData = pid;
		if (msgsnd(msqid, &workerBuf, sizeof(workerBuf) - sizeof(long), 0) == -1) {
			perror("Worker: msgsnd to OSS failed.\n");
			exit(EXIT_FAILURE);
		}
	}
/*
	fprintf(stderr, "WORKER PID: %d PPID: %d ", pid, ppid);
	fprintf(stderr, "SysClockS: %lld SysClockNano: %lld\nTermTimeS: %lld TermTimeNano: %lld\n", shm->seconds, shm->nanoseconds, termSeconds, termNano);
	fprintf(stderr, "--Terminating after sending next message.\n%d messages received from OSS.\n", messageCount);	
*/	
	shmdt(shm);
	return 0;
}






