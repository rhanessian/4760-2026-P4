//Rebecca Hanessian
//CS 4760
//Project 4: Process Scheduling
//worker file

#include "worker.h"

int main (int argc, char *argv[]){
// Check for necessary arguments	
	if (argc < 3) {
		fprintf(stderr, "Worker missing arguments.\n");
		exit(EXIT_FAILURE);
	}
	
// Initialize time and pid variables
	int durationS = atoi(argv[1]);
	int durationN = atoi(argv[2]);
	pid_t pid = getpid();
	pid_t ppid = getppid();
	
	fprintf(stderr, "\nWorker starting, PID: %d  PPID: %d\n", pid, ppid);
	fprintf(stderr, "Called with:\n\tProcess Time: %d seconds, %d nanoseconds\n\n", durationS, durationN);

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
	
// Calculate termination time	
	long long termSeconds = shm->seconds + durationS;
	long long termNano = shm->nanoseconds + durationN;
	
	if (termNano >= 1000000000) {
		termSeconds += (termNano / 1000000000);
		termNano %= 1000000000;
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
	fprintf(stderr, "SysClockS: %lld SysClockNano: %lld\nTermTimeS: %lld TermTimeNano: %lld\n", shm->seconds, shm->nanoseconds, termSeconds, termNano);
	fprintf(stderr, "--Just Starting\n\n");
	
// Main loop	
	while (active) {
// Blocking wait 
		if (msgrcv(msqid, &ossBuf, sizeof(ossBuf) - sizeof(long), pid, 0) == -1) {
			break;
		}	
		for (int i = 0; i < MAXPROC; i++) {
			if (shm->table[i].pid == getpid()) {
				(shm->table[i].msgsSent)++;
			}
		}
		
// Check simulated clock vs termination time
		if (shm->seconds > termSeconds || (shm->seconds == termSeconds && shm->nanoseconds >= termNano)) {
			active = 0;
			workerBuf.status = 0; 
		} else {
			fprintf(stderr, "WORKER PID: %d PPID: %d ", pid, ppid);
			fprintf(stderr, "SysClockS: %lld SysClockNano: %lld\nTermTimeS: %lld TermTimeNano: %lld\n", shm->seconds, shm->nanoseconds, termSeconds, termNano);	
			fprintf(stderr, "--%d messages received from OSS.\n", messageCount);

			workerBuf.status = 1;
		}
		
// Send message back to oss
		workerBuf.mtype = ppid;
		workerBuf.intData = pid;
		if (msgsnd(msqid, &workerBuf, sizeof(workerBuf) - sizeof(long), 0) == -1) {
			perror("Worker: msgsnd to OSS failed.\n");
			exit(EXIT_FAILURE);
		}
	}

	fprintf(stderr, "WORKER PID: %d PPID: %d ", pid, ppid);
	fprintf(stderr, "SysClockS: %lld SysClockNano: %lld\nTermTimeS: %lld TermTimeNano: %lld\n", shm->seconds, shm->nanoseconds, termSeconds, termNano);
	fprintf(stderr, "--Terminating after sending next message.\n%d messages received from OSS.\n", messageCount);	
	
	shmdt(shm);
	return 0;
}






