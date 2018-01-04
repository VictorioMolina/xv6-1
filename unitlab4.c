#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

//unitlab4
void part1() {
	printf(1, "--PART1 Test-\n");

	int pid = 0;
	int* shmbreak = (int*) shmbrk(sizeof(int));
	if (shmbreak < 0) {
		printf(1, "##PART1 Test FAILED 1##\n");
		return;
	}

	if ((pid = fork()) == 0) {
		if ((pid = fork()) == 0) {
			int old_tick = vdso_getticks();
			while (old_tick + 100 > vdso_getticks()) {
				continue;
			}
			if (*shmbreak != 8) {
				printf(1, "##PART1 Test FAILED 2##\n");
			}
			exit();
		} else {
			int old_tick = vdso_getticks();
			while (old_tick + 100 > vdso_getticks()) {
				continue;
			}
			if (*shmbreak != 8) {
				printf(1, "##PART1 Test FAILED 3##\n");
			}
			wait();
			*shmbreak = 9;
			exit();
		}
	} else {
		*shmbreak = 8;
		wait();
		if (*shmbreak != 9) {
			printf(1, "##PART1 Test FAILED 4##\n");
		}
	}

	int i, fd;
	fd = open("small", O_CREATE | O_RDWR);
	if (fd >= 0) {
	} else {
		printf(1, "error: creat small failed!\n");
		exit();
	}
	for (i = 0; i < 100; i++) {
		if (write(fd, "aaaaaaaaaa", 10) != 10) {
			printf(1, "error: write aa %d new file failed\n", i);
			exit();
		}
		if (write(fd, "bbbbbbbbbb", 10) != 10) {
			printf(1, "error: write bb %d new file failed\n", i);
			exit();
		}
	}
	close(fd);
	fd = open("small", O_RDONLY);
	if (fd < 0) {
		printf(1, "error: open big failed!\n");
		exit();
	}

	if (read(fd, shmbreak, sizeof(int) + 1) != -1) {
		printf(1, "##PART1 Test FAILED 5##\n");
	}
	close(fd);

	fd = open("small", O_RDONLY);
	if (fd < 0) {
		printf(1, "error: open big failed!\n");
		exit();
	}

	if (read(fd, shmbreak + sizeof(int), 1) != -1) {
		printf(1, "##PART1 Test FAILED 6##\n");
	}

	int* newbk = (int*) shmbrk(-1);
	if (newbk < 0) {
		printf(1, "##PART1 Test FAILED 7##\n");
		return;
	}

	if (read(fd, shmbreak, 1) != -1) {
		printf(1, "##PART1 Test FAILED 8##\n");
	}

	close(fd);
}

void part2() {
	printf(1, "--PART2 Test-\n");
	int pid;

	int *futex = (int *) shmbrk(4096);
	int *num = futex + 1000;
	*num = 0;
	*futex = 0;

	if ((pid = fork()) == 0) {
		for (int n = 0; n < 10; n++) {
			pid = fork();
			if (pid == 0) {
				futex_wait(futex, 0);
				__sync_fetch_and_add(num, 1);
				exit();
			}
		}
		for (int n = 0; n < 10; n++) {
			wait();
		}
		exit();
	} else {
		//parent
		int old_tick = vdso_getticks();
		while (old_tick + 200 > vdso_getticks()) {
			continue;
		}
		int count = 0;
		old_tick = vdso_getticks();
		while (1) {
			count = count + futex_wake(futex);
			if (vdso_getticks() > old_tick + 200) {
				break;
			}
		}
		if (*num != count) {
			printf(1, "##PART2 Test FAILED 1##\n");
		}

		wait();
	}
}
typedef struct {
	volatile int total;
	volatile mutex_t mutex;
	volatile cond_var_t cv;
} share_mem_t;

void part3() {
	printf(1, "--PART3 Test-\n");
	int pid;
	share_mem_t* shmem = (share_mem_t*) shmbrk(sizeof(share_mem_t));
	memset(shmem, 0, sizeof(share_mem_t));
	mutex_init((mutex_t*) &(shmem->mutex));
	cv_init((cond_var_t*) &(shmem->cv));

	for (int i = 0; i < 20; i++) {
		pid = fork();
		if (pid == 0) {
			for (int j = 0; j < 100; j++) {
				mutex_lock((mutex_t*) &(shmem->mutex));
				shmem->total = shmem->total + j;
//				cv_wait((cond_var_t*) &(shmem->cv),
//												(mutex_t*) &(shmem->mutex));
				mutex_unlock((mutex_t*) &(shmem->mutex));
			}
			exit();
		}
	}

//	while(1){
//		mutex_lock((mutex_t*) &(shmem->mutex));
//		if(shmem->total<4950 * 50){
//			cv_bcast((cond_var_t*) &(shmem->cv));
//		}
//		else{
//			break;
//		}
//		mutex_unlock((mutex_t*) &(shmem->mutex));
//		sleep(1);
//	}
	for (int i = 0; i < 20; i++) {
		wait();
	}
	if (shmem->total != 4950 * 20) {
		printf(1, "##PART3 Test FAILED 1##\n");
		printf(1, "##total: %d ##\n", shmem->total);
	}

	for (int i = 0; i < 5; i++) {
		pid = fork();
		if (pid == 0) {
			mutex_lock((mutex_t*) &(shmem->mutex));
			cv_wait((cond_var_t*) &(shmem->cv), (mutex_t*) &(shmem->mutex));
			shmem->total = shmem->total + 1;
			mutex_unlock((mutex_t*) &(shmem->mutex));
			exit();
		}
	}

	sleep(1000);
	cv_bcast((cond_var_t*) &(shmem->cv));
	for (int i = 0; i < 5; i++) {
		wait();
	}
	if (shmem->total != 4950 * 20 + 5) {
		printf(1, "##PART3 Test FAILED 2##\n");
		printf(1, "##total: %d ##\n", shmem->total);
	}
}

int main(int argc, char *argv[]) {
	part1();
	part2();
	part3();
	exit();
}
//unitlab4
