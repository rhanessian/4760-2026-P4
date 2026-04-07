CC = gcc

all: oss worker

oss: oss.c
	gcc -std=gnu11 -o oss oss.c

worker: worker.c
	gcc -std=gnu11 -o worker worker.c

clean:
	$(RM) worker oss *.txt