#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

struct PrQ PQ[MAXQ];

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.



void
addprocPQ(struct PrQ *PQ, struct proc *element)
{
  PQ->proc[PQ->tail] = element;
  PQ->tail++;
  if (PQ->tail == NPROC + 1) {
    PQ->tail = 0;
  }
  PQ->size++;
}

void
popprocPQ(struct PrQ *PQ)
{
  if (PQ->size == 0) 
  {
    panic("Queue is empty");
  }
  PQ->head = (PQ->head+1) % (NPROC+1);
  PQ->size--;
}

struct proc*
getproc(struct PrQ *PQ)
{
  if (PQ->head == PQ->tail) 
  {
    return 0;
  } 
  return PQ->proc[PQ->head];
}

void 
deleteprocPQ(struct PrQ *PQ, int pid) 
{
  for (int curr = PQ->head; curr != PQ->tail; curr = (curr + 1) % (NPROC + 1)) 
  {
    if (PQ->proc[curr]->pid == pid) 
    {
      struct proc *temp = PQ->proc[curr];
      PQ->proc[curr] = PQ->proc[(curr + 1) % (NPROC + 1)];
      PQ->proc[(curr + 1) % (NPROC + 1)] = temp;
    } 
  }
  PQ->size--;
  PQ->tail = PQ->tail - 1 < 0 ? NPROC : PQ->tail - 1;
}

void setrtime()
{
  struct proc* p;
  for(p = proc; p < &proc[NPROC]; p++)
  {
    if(!p)
    {
      continue;
    }
    acquire(&p->lock);
    if(p->state == RUNNING)
    {
      p->total_rtime++;
      #ifdef PBS
      p->rtime++;
      #endif
      #ifdef MLFQ
      p->timeslices--;
      p->PQwtime[p->PQIndex]++;
      #endif
    }
    release(&p->lock);
  }
}

static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  acquire(&tickslock);
  p->ctime = ticks;
  release(&tickslock);
  p->static_priority = 60;
  p->niceness = 5;
  p->nrun = 0;
  p->tickstorage[0] = 0;
  p->ifqueue = 0;
  p->PQIndex = 0;
  p->total_rtime = 0;
  p->tickstorage[1] = 0;
  p->Qticks = ticks;
  for(int i = 0; i < MAXQ; i++)
  {
    p->PQwtime[i] = 0;
  }


  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->ifqueue = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  //copy mask from parent to child 
  np->mask = p->mask;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

void set_priority(int priority, int pid, int* old)
{
  struct proc *p;
  // printf("set_priority: %d %d\n", priority, pid);
  for(p = proc; p < &proc[NPROC]; p++) 
  {
    if(p->pid == pid)
    {
      acquire(&p->lock);
      // printf("%d %d %d\n",p->rtime, p->priority, p->ctime);
      *old = p->static_priority;
      p->static_priority = priority;
      p->niceness = 5;
      release(&p->lock);
      if(*old < priority)
      {
        yield();
      }
    }
  }
}

void setprio()
{
  struct proc *p;
  // int old_priority;
  int wtime;
  int niceness;
  for(p = proc; p < &proc[NPROC]; p++) 
  {
    if(p->pid != 0)
    {
      if(p->rtime == 0)
      {
        niceness = 5;
      }
      else
      {
        wtime = p->sched_end - p->sched_start - p->rtime;
        // printf("wtime: %d rtime: %d schedstart: %d schedend: %d\n", wtime, p->rtime, p->sched_start, p->sched_end);
        niceness = (wtime*10)/(p->rtime + wtime);
      }
      // printf("%d\n",p->niceness);
      int temp2 = p->static_priority - niceness + 5;
      int temp = temp2 > 100 ? 100 : temp2;
      int dp = temp > 0 ? temp : 0;      
      // printf("%d %d %d %d\n",p->rtime,p->wtime, p->priority, dp);
      acquire(&p->lock);
      p->priority = dp;
      release(&p->lock);
    }
  }
}

void 
trace(int mask)
{
  struct proc *p = myproc();
  p->mask = mask;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->etime = ticks;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

int
waitx(uint64 addr, int* rtime, int* wtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          *rtime = np->total_rtime;
          *wtime = np->etime - np->ctime - np->total_rtime;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

void setPQ()
{
  for(int i = 0; i < MAXQ; i++)
  {
    PQ[i].head = 0;
    PQ[i].tail = 0;
    PQ[i].size = 0;
  }
}

void ageing()
{
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == RUNNABLE && ticks - p->Qticks >= AGELIMIT) {
      if (p->ifqueue) {
        deleteprocPQ(&PQ[p->PQIndex], p->pid);
        p->ifqueue = 0;
      }
      if (p->PQIndex != 0) {
        p->PQIndex--;
      }
      p->Qticks = ticks;
    }
  }
}
#ifdef MLFQ
void addnewprocs()
{
  for (struct proc *p = proc; p < &proc[NPROC]; p++) 
  {
    if (p->state == RUNNABLE && p->ifqueue == 0) 
    {
      addprocPQ(&PQ[p->PQIndex], p);
      p->ifqueue = 1;
    }
  }
}

static struct proc*
getminproc(void)
{
  for (int i = 0; i < MAXQ; i++) 
  {
    while (PQ[i].size) 
    {
      struct proc *p = getproc(&PQ[i]);
      popprocPQ(&PQ[i]);
      p->ifqueue = 0;
      if (p->state == RUNNABLE) {
        p->Qticks = ticks;
        return p;
      }
    } 
  }
  return 0;
}
#endif


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();  
  c->proc = 0;
  #ifdef DEFAULT  // default is to use round-robin
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
  #endif

  #ifdef FCFS
  for(;;)
  {
    // struct proc *alottedP = 0;
    intr_on();
    struct proc *minproc = 0;

    //finding proc that was made earliest - sorting by ctime
    for(p = proc; p < &proc[NPROC]; p++) 
    {
      if(p->state == RUNNABLE) 
      {
        if(!minproc)
        {
          minproc = p;
        }
        // printf("runningpid: %d\n",p->pid);
        else if(p->ctime < minproc->ctime)
        {
          minproc = p;
        }
      }
    }
    // in case there are no runnable processes in ptable
    if(!minproc)
    {
      continue;
    }
    //context switching for minproc
    acquire(&minproc->lock);
    if(minproc->state == RUNNABLE) 
    {
      minproc->state = RUNNING;
      c->proc = minproc;
      swtch(&c->context, &minproc->context);
      c->proc = 0;
    }
    release(&minproc->lock);
  }
  #endif
  
  #ifdef PBS
  // PBS scheduler
  for(;;)
  {
    intr_on();
    setprio();
    struct proc *minproc = 0;
    int min_priority = 101;
    int sameprio = -1;
    int samesched = -1;
    for(p = proc; p < &proc[NPROC]; p++) 
    {
      if(p->state == RUNNABLE) 
      {
        if(p->priority < min_priority)
        {
          sameprio = 0;
          min_priority = p->priority;
          minproc = p;
        }
        else if(p->priority == min_priority)
        {
          sameprio++;
        }
      }
    }
    // incase no process in ptable is runnable
    if(!minproc)
    {
      continue;
    }
    if(sameprio > 0)
    {
      // if there are multiple processes with same priority,
      // then we will choose one that has been scheduled more
      for(p = proc; p < &proc[NPROC]; p++) 
      {
        if(p->state == RUNNABLE) 
        {
          if(p->priority == min_priority)
          {
            //nrun is the number of time proc has been scheduled
            if(p->nrun > minproc->nrun)
            {
              samesched = 0;
              minproc = p;
            }
            else if(p->nrun == minproc->nrun)
            {
              samesched++;
            }
          }
        }
      }
    }
    if(samesched > 0)
    {
      // if there are multiple processes with same priority and same nrun,
      // then we will choose one that has lower ctime
      for(p = proc; p < &proc[NPROC]; p++) 
      {
        if(p->state == RUNNABLE) 
        {
          if(p->priority == min_priority && p->nrun == minproc->nrun)
          {
            if(p->ctime < minproc->ctime)
            {
              minproc = p;
            }
          }
        }
      }
    }
    acquire(&minproc->lock);
    if(minproc->state == RUNNABLE) 
    {
      minproc->state = RUNNING;
    minproc->sched_start = ticks;
      minproc->nrun++;
      c->proc = minproc;
      minproc->rtime = 0;
      // minproc->wtime = 0;
      swtch(&c->context, &minproc->context);
      minproc->sched_end = ticks;
      c->proc = 0;
    }
    release(&minproc->lock);
  }

  #endif

  #ifdef MLFQ

  // MLFQ scheduler

  for(;;)
  {
    intr_on();
    ageing();
    addnewprocs();
    p  = getminproc();

    if (p) {
      acquire(&p->lock);
      p->timeslices = 1 << p->PQIndex;
      c->proc = p;
      p->state = RUNNING;
      p->Qticks = ticks;
      p->nrun++;
      swtch(&c->context, &p->context);
      c->proc = 0;
      p->Qticks = ticks;
      release(&p->lock);
    }
  }
  
  #endif 
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  #ifdef PBS
  // p->sched_end = ticks;
  #endif
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  // #ifdef PBS
  // p->tickstorage[0] = ticks;
  // p->rtime += p->tickstorage[0] - p->tickstorage[1];
  // #endif
  // printf("%d: sleep %d %d\n", p->pid, p->rtime,p->wtime);
  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
          #ifdef PBS
          // p->sched_end = ticks;
          #endif
        p->state = RUNNABLE;
        // #ifdef PBS
        // p->tickstorage[1] = ticks;
        // p->wtime += p->tickstorage[1] - p->tickstorage[0];
        // #endif
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
          #ifdef PBS
          // p->sched_end = ticks;
          #endif
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process PQing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    #ifdef DEFAULT
    printf("%d %s %s", p->pid, state, p->name);
    #endif
    #ifdef FCFS
    printf("%d %s %s", p->pid, state, p->name);
    #endif
    #ifdef PBS
    setprio();
    printf("%d %d %s %d %d %d", p->pid, p->priority, state, p->total_rtime, ticks - p->ctime - p->total_rtime, p->nrun);
    #endif
    #ifdef MLFQ
    printf("%d %d %s %d %d %d %d %d %d %d %d", p->pid, p->PQIndex, state, p->total_rtime, ticks - p->Qticks, p->nrun, p->PQwtime[0], p->PQwtime[1], p->PQwtime[2], p->PQwtime[3], p->PQwtime[4]);
    #endif
    printf("\n");
  }
}
