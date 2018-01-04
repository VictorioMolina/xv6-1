#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"
#include "vdso.h"

void *vdso_text_page = 0;
vdso_ticks_page_t *vdso_ticks_page = 0;

int
sys_vdso_entry(void) {
  int n;

  // get the vdso function index
  if (argint(0, &n) < 0)
    return -1;

  // call the vdso_get_entry function from the vdso text
  vdso_getentry_t _getentry = (vdso_getentry_t)VDSOTEXT;
  void *res = _getentry(n);

  if (0 == res)
    return -1;
  else
    return (int)res;
}

void
inc_vdso_ticks() {
  if (vdso_ticks_page != 0)
    __sync_add_and_fetch(&(vdso_ticks_page->ticks), 1);
}
