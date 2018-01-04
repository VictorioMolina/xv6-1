// Test that fork fails gracefully.
// Tiny executable so that the limit can be filling the proc table.

#include "types.h"
#include "stat.h"
#include "user.h"

#define N  1000

void
printf(int fd, char *s, ...)
{
  write(fd, s, strlen(s));
}

void
forktest(void)
{
  int n, pid;

  printf(1, "fork test\n");

  for(n=0; n<N; n++){
    pid = fork();
    if(pid < 0)
      break;
    if(pid == 0)
      exit();
  }

  if(n == N){
    printf(1, "fork claimed to work N times!\n", N);
    exit();
  }

  for(; n > 0; n--){
    if(wait() < 0){
      printf(1, "wait stopped early\n");
      exit();
    }
  }

  if(wait() != -1){
    printf(1, "wait got too many\n");
    exit();
  }

  printf(1, "fork test OK\n");
}

void
forktest2(void)
{
	printf(1, "fork test2\n");
	int pid = fork();
	if(pid < 0)
	  printf(1, "fork test2: fork failed\n");
	if(pid == 0){//child
		printf(1, "fork test2: run wolfietest\n");
		char* argv[] = { "wolfietest" };
		exec("wolfietest",argv);
	}
	else{//parent
		wait();
		printf(1, "fork test2: PASS\n");
	}

}

void
forktest3(void)
{
	printf(1, "fork test3\n");
	int pid = fork();
	if(pid < 0)
	  printf(1, "fork test3: fork failed\n");
	if(pid == 0){//child
		pid = fork();
		if(pid < 0)
		  printf(1, "fork test3: fork failed\n");
		if(pid == 0){//child
			int i = 10;
			i = i + 10;
			exit();
		}
		else{//parent
			wait();
		}
		exit();
	}
	else{//parent
		pid = fork();
		if(pid < 0)
		  printf(1, "fork test3: fork failed\n");
		if(pid == 0){//child
			int i = 10;
			i = i + 10;
			exit();
		}
		else{//parent
			wait();
		}
		wait();
		printf(1, "fork test3: PASS\n");
	}

}

void
forktest4(void)
{
	printf(1, "fork test4\n");
	int pid = fork();

	//5 page

	if(pid < 0)
	  printf(1, "fork test4: fork failed\n");
	if(pid == 0){//child
		int pid2 = fork();
		if(pid2 < 0)
		  printf(1, "fork test4: fork failed\n");
		if(pid2 == 0){//child
			printf(1, "GOING TO PAGE FAULT 1\n");
			int* p = (int*)0x80100000;
			*p = 7;
			exit();
		}
		else{//parent
			wait();
			exit();
		}	
	}
	else{//parent
		int pid2 = fork();
		if(pid2 < 0)
		  printf(1, "fork test4: fork failed\n");
		if(pid2 == 0){//child
			printf(1, "GOING TO PAGE FAULT 2\n");
			int* p = (int*)0x80100000;
			*p = 7;
			exit();
		}
		else{//parent
			wait();
		}
		
		wait();
		printf(1, "fork test4: PASS\n");
		
	}

}

void forktest5()
{
	char buf[2000];
	memset(buf,0,2000);
	uint size = 2000;


	printf(1, "fork test5\n");
	int pid = fork();
	if(pid < 0)
	  printf(1, "fork test5: fork failed\n");
	if(pid == 0){//child
		wolfie(buf,size);
		printf(1,buf);
	}
	else{//parent
		wait();
		if(strcmp("",buf)==0){
			printf(1, "fork test5: PASS\n");
		}
		else{
			printf(1,"buf contains:\n");
			printf(1,buf);
			printf(1,"fork test5: Failed\n");
		}
	}
}


int
main(void)
{
  forktest();
  forktest2();
  forktest3();
  forktest4();
  forktest5();
  exit();
}
