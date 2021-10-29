#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int pid = fork();
    if(pid < 0) {
        printf("fork error\n");
        exit(1);
    }
    else if (pid == 0) {
        if(argc == 1)
        {
            sleep(10);
            exit(0);
        }
        else
        {
            exec(argv[1], argv + 1);
            printf("exec error\n");
            exit(1);
        }
    }
    else {
        int rtime, wtime;
        waitx(0,&rtime, &wtime);
        printf("rtime: %d, wtime: %d\n", rtime, wtime);
    }
    exit(0);
}