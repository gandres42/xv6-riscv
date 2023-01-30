
/*
With this program, user type texts line by line, where each line has a maximum length of 127 characters.
Each line is considered as "entered" (i.e., finalized) when the ENTER or RETURN key is clicked.
The program counts the number of characters entered for each minute.
When user types ":exit", the program exits.
The Makefile should be changed to include this program and thus it can be run from the shell of xv6.
*/

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAX_LINE_SIZE 128
#define MAX_BUF_SIZE 1280

char buf[MAX_BUF_SIZE];

void panic(char *s)
{
    fprintf(2, "%s\n", s);
    exit(1);
}

// create a new process
int fork1(void)
{
    int pid;
    pid = fork();
    if (pid == -1)
    {
        panic("fork");
    }
    return pid;
}

// create a pipe
void pipe1(int fd[2])
{
    int rc = pipe(fd);
    if (rc < 0)
    {
        panic("Fail to create a pipe.");
    }
}

// pull everything from pipe and return the size of the content
int read1(int *fd)
{
    char ch = 'a';
    write(fd[1], &ch, 1); // write something to the pipe so the next read will not block (because read operation is a blocking operation - if the pipe is empty, read will wait till something is put in)
    int len = read(fd[0], buf, MAX_BUF_SIZE) - 1;
    return len;
}

int main(int argc, char *argv[])
{

    // create two pipe to share with child
    int fd1[2], fd2[2];
    pipe1(fd1); // a pipe from child to parent - child sends entered texts to parent for counting
    pipe1(fd2); // a pipe from child to parent - child lets parent stop (when user types :exit)

    if (argc < 3)
    {
        printf("Error: missing arguments");
        printf("Usage: robottypist [RUNTIME] [TYPING INTERVAL]\n");
        exit(-1);
    }

    // read arguments
    int runtime = atoi(argv[1]);
    int interval = atoi(argv[2]);

    // A => runtime, B => interval
    // Checks and warns about mismatching interval/runtime and runtimes less than intervals
    if (runtime < interval)
    {
        printf("Warning: runtime is less than typing interval, runtime will default to %d\n", interval);
    }
    else if (runtime % interval != 0)
    {
        printf("Warning: runtime is not a multiple of the typing interval, runtime will default to %d seconds\n", runtime + (runtime % interval));
    }

    int result = fork1(); // create child process

    if (result == 0)
    {
        // child process:
        // close recieve pipes, don't need those anymore
        close(fd1[0]);
        close(fd2[0]);
        
        // record start time to measure runtime
        int start_time = uptime() / 10;
        while (1)
        {
            // print hello and write to the input pipe
            printf("Hello!\n");
            write(fd1[1], "Hello!", 6);
            
            // sleep between typing intervals
            sleep(interval * 10);

            // check for exit, exit if past runtime
            if ((uptime() / 10) - start_time >= runtime)
            {
                // here, take this
                write(fd2[1], "L", 1);
                exit(0);
            }
        }
    }
    else
    {
        // parent process:
        while (1)
        {
            sleep(6 * 10); // sleep between statements

            int len = read1(fd1);
            printf("\nIn last minute, %d characters were entered.\n", len);

            len = read1(fd2);
            if (len > 0)
            {
                // now to terminate
                wait(0);
                exit(0);
            }
        }
    }
}
