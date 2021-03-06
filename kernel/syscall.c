#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz)
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  int err = copyinstr(p->pagetable, buf, addr, max);
  if(err < 0)
    return err;
  return strlen(buf);
}

static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
  *ip = argraw(n);
  return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
int
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
  return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  if(argaddr(n, &addr) < 0)
    return -1;
  return fetchstr(addr, buf, max);
}

extern uint64 sys_chdir(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_exit(void);
extern uint64 sys_fork(void);
extern uint64 sys_fstat(void);
extern uint64 sys_getpid(void);
extern uint64 sys_kill(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_mknod(void);
extern uint64 sys_open(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_unlink(void);
extern uint64 sys_wait(void);
extern uint64 sys_write(void);
extern uint64 sys_uptime(void);
extern uint64 sys_trace(void);
extern uint64 sys_set_priority(void);
extern uint64 sys_waitx(void);

static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_trace]   sys_trace,
[SYS_set_priority]   sys_set_priority,
[SYS_waitx]   sys_waitx,
};

struct sysindex{
  int val;
  char *name;
};

//struct for syscall info
struct sysindex sysindex[] = 
{
  [SYS_fork] { 0, "fork" },
  [SYS_exit] { 1, "exit" },
  [SYS_wait] { 1, "wait" },
  [SYS_pipe] { 0, "pipe" },
  [SYS_read] { 3, "read" },
  [SYS_kill] { 2, "kill" },
  [SYS_exec] { 2, "exec" },
  [SYS_fstat] { 1, "fstat" },
  [SYS_chdir] { 1, "chdir" },
  [SYS_dup] { 1, "dup" },
  [SYS_getpid] { 0, "getpid" },
  [SYS_sbrk] { 1, "sbrk" },
  [SYS_sleep] { 1, "sleep" },
  [SYS_uptime] { 0, "uptime" },
  [SYS_open] { 2, "open" },
  [SYS_write] { 3, "write" },
  [SYS_mknod] { 3, "mknod" },
  [SYS_unlink] { 1, "unlink" },
  [SYS_link] { 2, "link" },
  [SYS_mkdir] { 1, "mkdir" },
  [SYS_close] { 1, "close" },
  [SYS_trace] { 1, "trace" },
};

void
syscall(void)
{
  struct proc *p = myproc();
  int val = p->trapframe->a7;
  int ret;
  if(val>0)
  {
    if(val < (sizeof(syscalls)/sizeof((syscalls)[0])) && syscalls[val])
    {
      argint(0, &ret);
      p->trapframe->a0 = syscalls[val]();
      int mask = p->mask;
      int r = mask & (1 << val);
      if(r)
      {
        printf("%d: syscall %s (",p->pid,sysindex[val].name);
        if(sysindex[val].val == 1)
        {
          printf("%d ",ret);
        }
        else
        {
          printf("%d ",ret);
          for (int i = 1; i < sysindex[val].val; i++){ int n; argint(i,&n);printf("%d ",n);}
        }
        
        printf("\b) -> %d\n", p->trapframe->a0);
      }
    }
  }
  else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, val);
    p->trapframe->a0 = -1;
  }
}
