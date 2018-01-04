#include "types.h"
#include "memlayout.h"
#include "mmu.h"
#include "vdso.h"

static uint
_vdso_getticks(void) {
  vdso_ticks_page_t *tics_page = (vdso_ticks_page_t *)(VDSOTEXT + VDSO_GETTICKS*PGSIZE);
  return tics_page->ticks;
}


static uint
_vdso_getpid(void) {
  vdso_pid_page_t *pid_page = (vdso_pid_page_t *)(VDSOTEXT + VDSO_GETPID*PGSIZE);
  return pid_page->pid;
}

void *
_vdso_getentry(int index) {
  switch(index) {
  case VDSO_GETTICKS:
    return _vdso_getticks;
  case VDSO_GETPID:
    return _vdso_getpid;
  default:
    return 0;
  }
}
