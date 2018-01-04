#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

//unittestlab3
void part1() {
	printf(1, "--PART1 Test A--\n");
	int pid = 0;
	if ((pid = fork()) == 0) {
		char* buf = 0;
		uint size = 2000;
		if (wolfie(buf, size) < 0) {
			printf(1, "syscall call caught null pointer\n");
		}
		printf(1, "Should see null (empty buff)\n");
		printf(1, "%s\n", buf);
		exit();
	}
	wait();

	printf(1, "--PART1 Test B--\n");
	pid = 0;
	if ((pid = fork()) == 0) {
		char* buf = 0;
		printf(1, "should get page fault\n");
		buf[0] = 't';
	}
	wait();

	printf(1, "--PART1 Test C--\n");
	pid = 0;
	if ((pid = fork()) == 0) {
		int* buf = 0;

		printf(1, "should get page fault\n");
		printf(1, "%d\n", *buf);

	}
	wait();

	printf(1, "PART1 Test PASS\n");
}

int overflow(int num) {
	int buf[500];
	buf[0] = num;
	printf(1, "%d, ", buf[0]);
	overflow(++num);
	return num;
}

int overflow2(int num) {
	int buf[5000];
	buf[0] = num;
	printf(1, "%d, ", buf[0]);
	overflow2(++num);
	return num;
}

int forktest(int num) {
	char buf[2000];
	buf[0] = num + 20;

	if (num == 0) {
		printf(1, "	-| Multi page stack forking |-\n");

		int pid = fork();

		if (pid == 0) {
			printf(1, "From child: Writing wolfie to stack\n");
			int ret = wolfie(buf, 2000);
			if (ret < 0 || buf[0] == 20) {
				printf(1, "!!PART2 Test D FAIL!!syscall\n");
			}
			printf(1, "%s\n", buf);
			exit();
		} else {
			printf(1, "Child pid %d\n", pid);
		}
		wait();
		if (buf[0] == 20) {
			printf(1, "!!PART2 Test D PASS!!\n");
		}
		return buf[0];
	}

	printf(1, "%d, ", num);
	forktest(--num);
	return num;

}

int fibonacci(int n) {
	if (n == 0)
		return 0;
	else if (n == 1)
		return 1;
	else
		return (fibonacci(n - 1) + fibonacci(n - 2));
}

void part2() {
	int pid = 0;
	printf(1, "--PART2 Test A--\n");
	if ((pid = fork()) == 0) {
		printf(1, "Overflowing stack\n");
		overflow(1);
		exit();
	}
	wait();

	printf(1, "--PART2 Test B--\n");
	if ((pid = fork()) == 0) {
		printf(1, "Multi page Overflowing stack\n");
		overflow2(1);
		exit();
	}
	wait();

	printf(1, "--PART2 Test C--\n");
	if ((pid = fork()) == 0) {
		printf(1, "	-| invalid stack addr syscall |-\n");
		char* buf = (char*) 0x5000;
		uint size = 2000;
		if (wolfie(buf, size) > 0) {
			printf(1, "!!PART2 Test C FAIL!!\n");
		}
		exit();
	}
	wait();

	printf(1, "--PART2 Test D--\n");
	if ((pid = fork()) == 0) {
		forktest(10);
		exit();
	}
	wait();

	printf(1, "PART2 Test PASS\n");
}

void part3() {
	int pid = 0;
	printf(1, "--PART3 Test A--\n");
	if ((pid = fork()) == 0) {
		if (getpid() != vdso_getpid()) {
			printf(1, "!!PART3 Test A FAIL!!\n");
		}
		exit();
	}
	wait();

	printf(1, "--PART3 Test B--\n");
	if ((pid = fork()) == 0) {
		printf(1, "Overflowing stack\n");
		//overflow(1);
		int tick = vdso_getticks();
		for (int i = 0; i < 10; i++) {
			printf(1, "Tick: %d \n", vdso_getticks());
		}
		if (tick >= vdso_getticks()) {
			printf(1, "!!PART3 Test B FAIL!!\n");
		}
		exit();
	}
	wait();

	printf(1, "--PART3 Test C--\n");
	if ((pid = fork()) == 0) {
		printf(1, "10 Fibonacci series\n");
		printf(1, "0, 1, 1, 2, 3, 5, 8, 13, 21, 34\n");
		int ii = 0;
		for (int c = 1; c <= 10; c++) {
			printf(1, "%d\n", fibonacci(ii));
			ii++;
		}
		exit();
	}
	wait();

	printf(1, "--PART3 Test D--\n");

	char buf2[3000];
	char* exbuf = buf2 - 0x3000;
	if ((pid = fork()) == 0) {
		if (wolfie(exbuf, 3000) != -1) {
			printf(1, "!!PART3 Test D FAIL!!\n");
		}
		exit();
	}
	if (wolfie(exbuf, 3000) != -1) {
		printf(1, "!!PART3 Test D FAIL!!\n");
	}
	wait();
	printf(1, "PART3 Test PASS\n");
}

void readtest(void) {
	int i, fd;
	char buf[8192];
	printf(1, "read test\n");

	fd = open("big", O_CREATE | O_RDWR);
	if (fd < 0) {
		printf(1, "error: creat big failed!\n");
		exit();
	}
	for (i = 0; i < MAXFILE; i++) {
		((int*) buf)[0] = i;
		if (write(fd, buf, 512) != 512) {
			printf(1, "error: write big file failed\n", i);
			exit();
		}
	}
	close(fd);

	fd = open("big", O_RDONLY);
	if (fd < 0) {
		printf(1, "error: open big failed!\n");
		exit();
	}
	char buf2[3000];
	char* exbuf = buf2 - 0x1000;
	int pid = -1;
	if ((pid = fork()) == 0) {
		if (read(fd, exbuf, 100) != -1) {
			printf(1, "!!read FAIL!!\n");
		}
		close(fd);
		exit();
	}
	if (read(fd, exbuf, 100) != -1) {
		printf(1, "!!read FAIL!!\n");
	}
	wait();
	close(fd);

	if (unlink("big") < 0) {
		printf(1, "unlink big failed\n");
		exit();
	}
	printf(1, "read ok\n");
}

int main(int argc, char *argv[]) {
	//part1();
	//part2();
	//part3();
	readtest();
	exit();
}
//unittestlab3
