#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "x86.h"
#include "vdso.h"
#include "user.h"

char*
strcpy(char *s, char *t) {
	char *os;

	os = s;
	while ((*s++ = *t++) != 0)
		;
	return os;
}

int strcmp(const char *p, const char *q) {
	while (*p && *p == *q)
		p++, q++;
	return (uchar) *p - (uchar) *q;
}

uint strlen(char *s) {
	int n;

	for (n = 0; s[n]; n++)
		;
	return n;
}

void*
memset(void *dst, int c, uint n) {
	stosb(dst, c, n);
	return dst;
}

char*
strchr(const char *s, char c) {
	for (; *s; s++)
		if (*s == c)
			return (char*) s;
	return 0;
}

char*
gets(char *buf, int max) {
	int i, cc;
	char c;

	for (i = 0; i + 1 < max;) {
		cc = read(0, &c, 1);
		if (cc < 1)
			break;
		buf[i++] = c;
		if (c == '\n' || c == '\r')
			break;
	}
	buf[i] = '\0';
	return buf;
}

int stat(char *n, struct stat *st) {
	int fd;
	int r;

	fd = open(n, O_RDONLY);
	if (fd < 0)
		return -1;
	r = fstat(fd, st);
	close(fd);
	return r;
}

int atoi(const char *s) {
	int n;

	n = 0;
	while ('0' <= *s && *s <= '9')
		n = n * 10 + *s++ - '0';
	return n;
}

void*
memmove(void *vdst, void *vsrc, int n) {
	char *dst, *src;

	dst = vdst;
	src = vsrc;
	while (n-- > 0)
		*dst++ = *src++;
	return vdst;
}

uint vdso_getticks() {
	static vdso_getticks_t _getticks_func = 0;

	// upon the first use, get the entry from the kernel
	if (0 == _getticks_func) {
		_getticks_func = vdso_entry(VDSO_GETTICKS);
	}

	// call the function
	return _getticks_func();
}

uint vdso_getpid() {
	static vdso_getpid_t _getpid_func = 0;

	// upon the first use, get the entry from the kernel
	if (0 == _getpid_func) {
		_getpid_func = vdso_entry(VDSO_GETPID);
	}

	// call the function
	return _getpid_func();
}

void mutex_init(mutex_t *mutex) {
	mutex_t* shmmutex = (mutex_t*) shmbrk(sizeof(mutex_t));
	memset(shmmutex, 0, sizeof(mutex_t));
	shmmutex->lock = 0;
	*mutex = *shmmutex;
}

void mutex_lock(mutex_t *mutex) {
	while (__sync_lock_test_and_set(&(mutex->lock), 1)) {
		futex_wait((int*) &(mutex->lock), 1);
	}
}

int mutex_trylock(mutex_t *mutex) {

	if (__sync_lock_test_and_set(&(mutex->lock), 1)) {
		return -1;
	} else {
		return 0;
	}
}

void mutex_unlock(mutex_t *mutex) {
	__sync_lock_release(&(mutex->lock));
	futex_wake((int*) &(mutex->lock));
}

void cv_init(cond_var_t *cv) {
	cond_var_t* cvar = (cond_var_t*) shmbrk(sizeof(cond_var_t));
	memset(cvar, 0, sizeof(cond_var_t));
	cvar->cvnum = 0;
	mutex_init((mutex_t*) &(cvar->cvslock));
	*cv = *cvar;
}

void cv_wait(cond_var_t *cv, mutex_t *mutex) {
	mutex_lock((mutex_t*) &(cv->cvslock));
	mutex_unlock(mutex);
	cv->cvnum = cv->cvnum + 1;
	//printf(1,"new cv->cvnum %d \n",cv->cvnum);
	//this syscall will release cvslock after holding ptable lock.
	futex_wait2((int*) &(cv->cvnum), (int) (cv->cvnum), (void*) &(cv->cvslock));
	//no need to unlock
	mutex_lock(mutex);
}

void cv_bcast(cond_var_t *cv) {
	mutex_lock((mutex_t*) &(cv->cvslock));
	int numwake = 0;
	do {
		numwake = numwake + futex_wake((int*) &(cv->cvnum));
		//printf(1,"1");
	} while (cv->cvnum != numwake);
	cv->cvnum=cv->cvnum-numwake;
	mutex_unlock((mutex_t*) &(cv->cvslock));
}
