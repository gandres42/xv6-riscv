#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    // printf("The pid of parent is %d\n", getpid()); 
    // if(fork()==0){ 
    //     printf("This is child. The pid of my parent is %d\n", 
    //     getppid()); 
    //     exit(0); 
    // } 
    // //note: the two pids printed above should be the same! 
    // wait(0);

    // printf("The pid of parent is %d\n", getpid()); 
    // if(fork()==0){ 
    //     exit(0); 
    // }
    // int * cpids = malloc(sizeof(int) * 64);
    // printf("number of child processes: %d\n", getcpids(cpids));
    // for (int i = 0; i < 64; i++)
    // {
    //     printf("%d ", cpids[i]);
    // }
    // printf("\n");
    for (int i = 0; i < 1000; i++)
    {
        if (i % 10 == 0)
        {
            sleep(1);
        }
    }

    printf("%d\n", getswapcount());
    return 1;
}
