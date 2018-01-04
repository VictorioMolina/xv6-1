#ifndef VDSO_H_
#define VDSO_H_

#define VDSO_GETTICKS   1
#define VDSO_GETPID     2

#ifndef __ASSEMBLER__

typedef uint (*vdso_getticks_t)(void);
typedef uint (*vdso_getpid_t)(void);
typedef void *(*vdso_getentry_t)(int index);

typedef struct {
  uint pid;
} vdso_pid_page_t;

typedef struct {
  uint ticks;
} vdso_ticks_page_t;

#endif

#endif /* VDSO_H_ */
