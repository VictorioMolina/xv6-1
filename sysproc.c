#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"

int sys_fork(void) {
	return fork();
}

int sys_exit(void) {
	exit();
	return 0;  // not reached
}

int sys_wait(void) {
	return wait();
}

int sys_kill(void) {
	int pid;

	if (argint(0, &pid) < 0)
		return -1;
	return kill(pid);
}

int sys_getpid(void) {
	return myproc()->pid;
}

int sys_sbrk(void) {
	int addr;
	int n;

	if (argint(0, &n) < 0)
		return -1;
	addr = myproc()->sz;
	if (growproc(n) < 0)
		return -1;
	return addr;
}

int sys_sleep(void) {
	int n;
	uint ticks0;

	if (argint(0, &n) < 0)
		return -1;
	acquire(&tickslock);
	ticks0 = ticks;
	while (ticks - ticks0 < n) {
		if (myproc()->killed) {
			release(&tickslock);
			return -1;
		}
		sleep(&ticks, &tickslock);
	}
	release(&tickslock);
	return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int sys_uptime(void) {
	uint xticks;

	acquire(&tickslock);
	xticks = ticks;
	release(&tickslock);
	return xticks;
}

int sys_shmbrk(void) {
	// LAB 4: Your Code Here
	int n;

	if (argint(0, &n) < 0)
		return -1;

	uint oldshmbreak = myproc()->shmbreak;
	if (n == 0) {
		return myproc()->shmbreak;
	} else if (n < 0) {
		//free all shm page
		deallocuvm(myproc()->pgdir, myproc()->shmtop, myproc()->stacktop,
				myproc());
		myproc()->shmbreak = myproc()->stacktop;
		myproc()->shmnextpg = myproc()->stacktop;
		return oldshmbreak;
	} else {
		if (myproc()->shmbreak + n >= myproc()->shmtop) {
			//overflow shm space
			return -1;
		}
		//2 cases:  need alloc a page or not
		int newbk = myproc()->shmbreak + n;
		int newnextpg = PGROUNDUP(newbk);

		if (newnextpg > myproc()->shmnextpg) {
			//need new pages
			int allocret = allocuvm(myproc()->pgdir, myproc()->shmnextpg,
					newnextpg, (struct proc *) 0);
			if (allocret == 0)
				panic("panic sys_shmbrk allocuvm failed");

			myproc()->shmnextpg = newnextpg;
			myproc()->shmbreak = newbk;
			return oldshmbreak;

		} else if (newnextpg == myproc()->shmnextpg) {
			//will not need to alloc more pg
			myproc()->shmbreak = newbk;
			return oldshmbreak;
		} else {
			panic("sys_shmbrk");
			return -1;
		}

	}
}

int sys_futex_wait(void) {
	// LAB 4: Your Code Here
	int * loc;
	int val;

	if (argint(1, &val) < 0)
		return -1;
	if (argptr(0, (void*) &loc, sizeof(*loc)) < 0)
		return -1;

	acquire(&ptable.lock);
	if (*loc == val) {
		sleep(loc, &ptable.lock);
		release(&ptable.lock);
		return 0;
	} else {
		release(&ptable.lock);
		return -1;
	}

}

typedef struct {
	volatile int lock;
} mutex_t;

int sys_futex_wait2(void) {
	// LAB 4: Your Code Here
	int * loc;
	int val;
	mutex_t *lk;

	if (argint(1, &val) < 0)
		return -1;
	if (argptr(0, (void*) &loc, sizeof(*loc)) < 0)
		return -1;
	if (argptr(2, (void*) &lk, sizeof(*lk)) < 0)
		return -1;

	acquire(&ptable.lock);
	lk->lock=0;
	wakeupfutex2((void*)&(lk->lock));
	if (*loc == val) {
		sleep(loc, &ptable.lock);
		release(&ptable.lock);
		return 0;
	} else {
		release(&ptable.lock);
		return -1;
	}

}

int sys_futex_wake(void) {
	// LAB 4: Your Code Here
	int * loc;
	if (argptr(0, (void*) &loc, sizeof(*loc)) < 0)
		return -1;

	int i = wakeupfutex(loc);

	return i;
}

// lab1 sys call
void*
memmove_copy(void *dst, const void *src, uint n) {
	const char *s;
	char *d;

	s = src;
	d = dst;
	if (s < d && s + n > d) {
		s += n;
		d += n;
		while (n-- > 0)
			*--d = *--s;
	} else
		while (n-- > 0)
			*d++ = *s++;

	return dst;
}
// lab1 sys call
int sys_wolfie(void) {
	char *buf;
	int bufsize;

	if (argint(1, &bufsize) < 0 || argptr(0, &buf, bufsize) < 0)
		return -1;

	char wolfie[] =
			"1111111111111110011100000111111100011111\n\
1111111111111100011111000011110000011111\n\
1110001111111000011111100001100111011111\n\
1110000001110000000111100000000111011111\n\
1110110000000000000000000000000111011111\n\
1110101000000000000000000000000111111111\n\
1110111100000000000000000000000111011111\n\
1110111100000000000000000000000010011111\n\
1111011100000000000000000000000000011111\n\
1111011100000000000000000000000000111111\n\
1111001100110010000000000000000100111111\n\
1111100001110001010000000000000100111111\n\
1111110111101000000000000000110000001111\n\
1111110011110000000000000000000000000111\n\
1111100001110000000000000000000000000011\n\
1111000000000000000000000000000000000011\n\
1100001000000000000000000000000000000001\n\
1000111000000000000000000000000000000001\n\
1111100000000000000000000000000000000000\n\
1111100001000000000000000000000000000001\n\
1111111111000000110000100000000000000001\n\
1111111100010001111111111000000000000011\n\
1111100100111011111111101000000000000111\n\
1111100000111111111111100000000000001111\n\
1111000000111111111111110000000000011111\n\
1111100000111111111111100000000001111111\n\
1111111011011111111111110000001111111111\n\
1111111111011111111111110100000111111111\n\
1111111100011111111000111100000011111111\n\
1111111000011001100000001000000000111111\n\
1111110000001000100000000000000000011111\n\
1111000000001000000000000000000000001111\n\
1110000000001000000000000000000000000111\n\
1000000000001010000000000000000000000011\n\
0000000000001111000000000100000000000001\n\
0000000000001111110000001000000000000000\n\
0000000000000111111111111110000000000000\n\
1000000000000011111111111100000000000000\n\
0000000000000011111111001000000000000000\n\
0000000011001111111111000000000000000000\n";

//1640 bytes + null = 1641
	if (bufsize < 1641) {
		return -1;
	}
	memmove_copy(buf, wolfie, 1640);
	buf[1640] = '\0';

	return 1641;

}

int sys_hi(void) {
	return 2;
}

