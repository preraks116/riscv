#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    for(int i = 0; i < 10; i++)
    {
        printf("%d\n",i);
        sleep(1);
    }
    exit(0);
}