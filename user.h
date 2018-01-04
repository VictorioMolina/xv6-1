struct stat;
struct rtcdate;

typedef struct {
	volatile int lock;
} mutex_t;

typedef struct {
	volatile int cvnum;
	volatile mutex_t cvslock;
} cond_var_t;

// system calls
int fork(void);
int exit(void) __attribute__((noreturn));
int wait(void);
int pipe(int*);
int write(int, void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(char*, int);
int mknod(char*, short, short);
int unlink(char*);
int fstat(int fd, struct stat*);
int link(char*, char*);
int mkdir(char*);
int chdir(char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
void *vdso_entry(int);
char* shmbrk(int);
int futex_wait(int *loc, int val);
int futex_wake(int *loc);
int futex_wait2(int *loc, int val, void*);

//lab1
int wolfie(void*, uint);
int hi(void);

// ulib.c
int stat(char*, struct stat*);
char* strcpy(char*, char*);
void *memmove(void*, void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, char*, ...);
char* gets(char*, int max);
uint strlen(char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
uint vdso_getticks();
uint vdso_getpid();
void mutex_init(mutex_t *);
void mutex_lock(mutex_t *);
int  mutex_trylock(mutex_t *);
void mutex_unlock(mutex_t *);
void cv_init(cond_var_t *);
void cv_wait(cond_var_t *, mutex_t *);
void cv_bcast(cond_var_t *);

