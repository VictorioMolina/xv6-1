#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

#define BUFFERSIZE 129

typedef struct {
	//extra char for full case
	volatile char buff[BUFFERSIZE];
	volatile int head;
	//tail is next free
	volatile int tail;
	volatile int total;
	volatile int pdone;
	volatile mutex_t mutex;
	volatile cond_var_t cv_c;
	volatile cond_var_t cv_p;

} share_mem_t;

int put(share_mem_t *shmem, volatile char* chara) {
	//buffer full
	if ((shmem->tail + 1) % BUFFERSIZE == shmem->head) {
		return -1;
	}
	(shmem->buff)[shmem->tail] = chara[0];
	shmem->tail = (shmem->tail + 1) % BUFFERSIZE;
	return 1;
}

int get(share_mem_t *shmem, volatile char* chara) {
	//empty
	if (shmem->tail == shmem->head) {
		return -1;
	}
	chara[0] = (shmem->buff)[shmem->head];
	shmem->head = (shmem->head + 1) % BUFFERSIZE;
	return 1;
}

int isempty(share_mem_t *shmem) {
	//empty
	if (shmem->tail == shmem->head) {
		return 1;
	}
	else{
		return -1;
	}
}
//checksum
int main(int argc, char *argv[]) {

	int fd = open("README", O_RDONLY);
	if (fd < 0) {
		printf(1, "error: open README failed!\n");
		exit();
	}
	char buff[4096];
	int i;
	int sthsum=0;
	while ((i = read(fd, buff, 4096)) != 0) {
		for (int j = 0; j < i; j++) {
			sthsum = sthsum + (int)buff[j];
		}
	}
	printf(1, "single theard sum is %d \n", sthsum);
	close(fd);

	share_mem_t* shmem = (share_mem_t*) shmbrk(sizeof(share_mem_t));
	memset(shmem, 0, sizeof(share_mem_t));
	cv_init((cond_var_t*) &(shmem->cv_p));
	cv_init((cond_var_t*) &(shmem->cv_c));
	mutex_init((mutex_t*) &(shmem->mutex));

	fd = open("README", O_RDONLY);
	if (fd < 0) {
		printf(1, "error: open README failed!\n");
		exit();
	}

	int pid;

#define NUMC 4
#define NUMP 4
#define DEBUGPRINT2 0
#define DEBUG2(...) (DEBUGPRINT2==1?printf(__VA_ARGS__):(void)0)

//4 producer
	for (i = 0; i < NUMP; ++i) {
		pid = fork();
		if (pid == 0) {
			while (1) {
				mutex_lock((mutex_t*) &(shmem->mutex));
				DEBUG2(1, "A");
				volatile char chara[1];
				int res = read(fd,(char*) chara, 1);
				//nore more to read
				if (res == 0) {
					DEBUG2(1, "B");
					shmem->pdone = shmem->pdone + 1;
					cv_bcast((cond_var_t*) &(shmem->cv_c));
					mutex_unlock((mutex_t*) &(shmem->mutex));
					exit();
				} else if (res == 1) {
					DEBUG2(1, "C");
					//try to put it in
					int resp;
					do {
						resp = put(shmem, chara);
						if (resp == 1) {
							DEBUG2(1, "D");
						} else {
							DEBUG2(1, "E");
						//keep chara and go to sleep
						cv_bcast((cond_var_t*) &(shmem->cv_c));
						cv_wait((cond_var_t*) &(shmem->cv_p),
								(mutex_t*) &(shmem->mutex));
						}
					} while (resp == -1);
					mutex_unlock((mutex_t*) &(shmem->mutex));
				} else {
					printf(1, "read failed");
				}
			}
		}
	}

//4 consumer
	for (i = 0; i < NUMC; ++i) {
		pid = fork();
		if (pid == 0) {
			int csum = 0;
			while (1) {
				mutex_lock((mutex_t*) &(shmem->mutex));
				DEBUG2(1, "a");
				if (shmem->pdone < NUMP || isempty(shmem)==-1) {
					DEBUG2(1, "b");
					volatile char buf[1];
					int res = get(shmem, buf);
					if (res == -1) {
						DEBUG2(1, "c");
						cv_bcast((cond_var_t*) &(shmem->cv_p));
						cv_wait((cond_var_t*) &(shmem->cv_c),
								(mutex_t*) &(shmem->mutex));
						mutex_unlock((mutex_t*) &(shmem->mutex));
					} else {
						DEBUG2(1, "d");
						csum = csum + (int)buf[0];
						mutex_unlock((mutex_t*) &(shmem->mutex));
					}
				} else {
					DEBUG2(1, "c");
					shmem->total = shmem->total + csum;
					mutex_unlock((mutex_t*) &(shmem->mutex));
					exit();
				}
			}

		}
	}

	for (i = 0; i < NUMP + NUMC; ++i) {
		wait();
	}

	close(fd);
	printf(1, "4:4 sum is %d\n", (int) shmem->total);

	exit();
}
//checksum
