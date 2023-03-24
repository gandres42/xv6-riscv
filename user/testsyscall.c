#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

#define MAX_LINE_SIZE 128
#define MAX_BUF_SIZE 1280
char buf[MAX_BUF_SIZE];


void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

//create a new process
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


//pull everything from pipe and return the size of the content
int
read1(int *fd)
{
 char ch='a';
 write(fd[1],&ch,1); //write something to the pipe so the next read will not block (because read operation is a blocking operation - if the pipe is empty, read will wait till something is put in)
 int len = read(fd[0],buf,MAX_BUF_SIZE)-1;
 return len;
}

int main() 
{ 
  //start the share scheduler 
  startcfs(); 

  //create 10 child processes of nice=10 (note: the parent process is of nice=0 by default) 
  int ret=0; 
  for(int i=0; i<10; i++){ 
    ret=fork1(); 
    if(ret==0){ 
            nice(10); 
            break; 
    } 
  } 

  //all processes run the same code as follows  
  printf("process (pid=%d) has nice = %d\n", getpid(), nice(-30)); 
  int t=0; 
  while(t++<2){ 
    double x=987654321.9; 
    for(int i=0; i<100000000; i++) {
      x /= 12345.6789; 
    } 
  } 

  //parent process waits for child processes to terminate and then stop share scheduler   
  if(ret>0){ 
    wait(0); 
    stopcfs();  
  } 

  return 0; 
}