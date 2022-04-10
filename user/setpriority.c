#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int priority, pid;
    priority = atoi(argv[1]);
    pid = atoi(argv[2]);
    set_priority(priority, pid);
    exit(0);
}