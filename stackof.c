#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

int data[10000];

//stackof

int func(int val) {
	int index;
	int res;
	char s[2048];
	volatile char *space = (volatile char *) &s;

	if (val == 0) {
		res = 0;
	} else {
		res = func(val - 1) + 1;
		for (index = 0; index < sizeof(space); index++) {
			space[index] = res;
		}
	}

	data[val] = res;
	return res;
}

void cause_of() {

}

int main(int argc, char *argv[]) {
	int pid;
	int fd = open("README", O_RDONLY);

	// single guard page - should run with no problem.
	pid = fork();
	if (0 == pid) {
		printf(1,
				"SINGLE-GUARD do not expect crash. Should see the result line...\n");
		int range = 2;
		func(range - 1);
		printf(1, "2.1 SINGLE-GUARD passed. Result is %d\n", data[range - 1]);
		exit();
	}
	wait();

	// multiple guard pages - should run with no problem.
	pid = fork();
	if (0 == pid) {
		printf(1,
				"MULTI-GUARD Do not expect crash. Should see the result line...\n");
		int range = 100;
		func(range - 1);
		printf(1, "2.1 MULTI-GUARD passed. Result is %d\n", data[range - 1]);
		exit();
	}
	wait();

	// Invalid addr in stack VMA - should get killed
	pid = fork();
	if (0 == pid) {
		printf(1, "INVAL-ADDR...\n");
		int range = 100;
		volatile int *addr = &range;
		addr += 10000;
		printf(1, "2.2 passed if no err, inval addr value is %d\n", *addr);
		printf(1, "2.2 failed, INVAL-ADDR should not get here.\n");
		exit();
	}
	wait();

	// Stack MAX-OUT - should get killed
	pid = fork();
	if (0 == pid) {
		printf(1, "2.3 passed if no err, MAX-OUT...\n");
		int range = 1000000;
		func(range - 1);
		printf(1, "2.3 failed, MAX-OUT should not get here.\n");
		exit();
	}
	wait();

	// Invalid addr passed to read - should not crash
	pid = fork();
	if (0 == pid) {
		printf(1, "INVAL-read()...\n");
		int range = 100;
		volatile int *addr = &range;
		addr += 10000;
		if (read(fd, (void *) addr, 10) >= 0) {
			printf(1, "2.4 Failed, INVAL-read() should not get here\n");
			exit();
		}
		printf(1, "2.4 passed, INVAL-read passed.\n");
		exit();
	}
	wait();

	// OF-FORK-OF
	pid = fork();
	if (0 == pid) {
		printf(1, "2.5 OF-FORK-OF. Should see two 'passed' messages...\n");
		// cause stack overflow
		func(3);
		// fork
		if (0 == fork()) {
			// cause more overflows
			func(100);
			printf(1, "2.5 OF-FORK-OF passed. pid=%d\n", getpid());
			exit();
		} else {
			// cause more overflows
			func(100);
			printf(1, "2.5 OF-FORK-OF passed. pid=%d\n", getpid());
			wait();
			exit();
		}
	}
	wait();

	// OF-FORK-READ
	pid = fork();
	if (0 == pid) {
		printf(1, "2.6 OF-FORK-READ. Should see two 'passed' messages...\n");
		// cause stack overflow
		func(3);
		// fork
		if (0 == fork()) {
			int range = 100;
			volatile int *addr = &range;
			addr += 10000;
			if (read(fd, (void *) addr, 10) >= 0) {
				printf(1, "2.6 failed, OF-FORK-READ should not get here.\n");
				exit();
			}
			printf(1, "2.6 OF-FORK-READ passed. pid=%d\n", getpid());
			exit();
		} else {
			// cause more overflows
			int range = 100;
			volatile int *addr = &range;
			addr += 10000;
			if (read(fd, (void *) addr, 10) >= 0) {
				printf(1, "2.6 failed, OF-FORK-READ should not get here.\n");
				exit();
			}
			printf(1, "2.6 OF-FORK-READ passed. pid=%d\n", getpid());
			wait();
			exit();
		}
	}
	wait();

	exit();
} //stackof
