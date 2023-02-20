#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;
  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

//create a pipe
void
pipe1(int fd[2])
{
 int rc = pipe(fd);
 if(rc<0){
   panic("Fail to create a pipe.");
 }
}





void 
testppid()
{
	int fd[2];
	pipe1(fd);
	int ret=fork1();
	if(ret==0){
		printf("\nThis is child process with pid=%d. My current parent has pid=%d.\n", getpid(), getppid());
		write(fd[1], "a", 1); //ask parent to exit
		sleep(3); //make sure the parent has exited
		printf("\nThis is child process with pid=%d. My current parent has pid=%d as my previous parent has exited.\n", getpid(), getppid());
		printf("\nDone.\n");
		exit(0);
	}else{
		char buf[10];
		sleep(1); //avoid crash between printfs
		printf("\nThis is parent process with pid=%d. Now I am going to exit.\n", getpid());
		read(fd[0],buf,1);
	}
}


void 
testcpids()
{
	int n_child_plan = 5; //test on how many child processes?
	for(int i=0; i<n_child_plan; i++){
		if(fork1()==0){
			printf("\nThis is child #%d with pid %d.\n", i, getpid());
			sleep(n_child_plan+2-i); //make sure the child processes are alive when the parent calls getcpids
			exit(0);
		}
		sleep(1); //avoid crash between printfs 
	}
	int cpids[64];
	int n_child_get = getcpids(cpids);
	sleep(1); //avoid crash between printfs
	printf("\nThis is parent process with pid %d. I have %d child processes of the following pids:\n", getpid(), n_child_get);
        for(int i=0; i<n_child_get; i++) printf("%d\n", cpids[i]);
	printf("\nDone.\n");
}


void
testswapcount()
{
	int out_loops=10;
	for(int i=0; i<out_loops; i++){
		int in_loops=5;
	        for(int i=0; i<in_loops; i++){
			sleep(1); //swap happens for each sleep syscall call
		}	
		printf("\nOut Loop Iteration #%d: swapcount=%d\n", i, getswapcount());
	}
	printf("\nDone.\n");
}



int
main(int argc, char*argv[])
{
	if(argc!=2){
		printf("Usage: %s 1|2|3  1-getppid, 2-getcpids, 3-getswapcount\n", argv[0]);
		return -1;
	}
	int choice = atoi(argv[1]);
	printf("Your choice is %d\n", choice);
	if(choice<1||choice>3){
		printf("Error: Argument must be 1, 2 or 3.\n");
		return -1;
	}

	int ret=fork1();
	if(ret==0){
		switch(choice){
			case 1: 
				printf("\n\nNow test getppid():\n\n");
				testppid(); 
				break;
			case 2:
				printf("\n\nNow test getcpids(int*):\n\n");
				testcpids();
				break;
			case 3:
				printf("\n\nNow test getswapcount():\n\n");
				testswapcount();
				break;
		}
		exit(0);
	}else{
		wait(0);
		sleep(3); //make sure grandchildren (if applicable) are all done!
	}
	return 0;
}




