#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"


typedef struct {
  int val;
  int sync;
} sync_t;

int
main(int argc, char *argv[]) {
  int pid;
  int res;

  printf(1, "parent before waiting\n");
  sync_t *my_struct = (sync_t *) shmbrk(4096);
  memset(my_struct, 0, sizeof(my_struct));

  // fork and continue in the child
  pid = fork();

  if (0 != pid) {
    // parent going to sleep
    printf(1, "parent waiting with bad value\n");
    res = futex_wait(&my_struct->val, 2);
    printf(1, "parent res was %d\n", res);

    printf(1, "parent waking sleeping child\n");
    my_struct->sync = 1;
    res = futex_wake(&my_struct->sync);
    printf(1, "parent res was %d\n", res);

    printf(1, "parent waiting with good value\n");
    res = futex_wait(&my_struct->val, 0);
    printf(1, "parent res was %d\n", res);
  }
  else {
    printf(1, "child waiting to be woken up\n");
    res = futex_wait(&my_struct->sync, 0);
    printf(1, "child res was %d\n", res);

    printf(1, "child waking sleeping parent\n");
    my_struct->val = 1;
    res = futex_wake(&my_struct->val);
    printf(1, "child res was %d\n", res);

    printf(1, "child signing off\n");
    exit();
  }
  wait();

  printf(1, "parent signing off\n");
  exit();

}//futextest
