# Enhancing xv6 OS

## Syscall Tracing

- added `mask` to `struct proc`
- added `trace` function that sets the `mask` of the current process
- added `struct syscall_info` where all syscalls are indexed.
- modified `syscall` function to print all the syscalls that are called during command execution
- added `strace.c` which computes the arguments and executes the specified command

## Scheduling

### First Come First Serve (FCFS)

- added `ctime` to `struct proc`
- modified `allocproc` function to initialize `ctime` of each process
- disabled preemption of clock interrupts after clock interrupts in `kerneltrap` function
- added FCFS scheduler to `scheduler` function which acquires the process with the lowest `ctime` in the process table, and then switches cpu context to this process.

### Priority Based Scheduling (PBS)

- added `rtime`, `wtime`, `priority`, `niceness`, `tickstorage` and `sched_time` to `struct proc`
- added `set_prority` syscall which sets the priority for a specified process (with process pid)
- added `setprio` function which calculates `DP` for each process in the process table before scheduling
- made changes to `sleep` and `wake` function to update `rtime` and `wtime` when the process is sleeping or not.
- made changes to `clockintr` which updates the `rtime` and `wtime` (but this is wrong ig)
- added PBS scheduler to `scheduler` function which sorts process according to `priority`, `sched_time` and `ctime`, and then switches cpu context to this process.

### Multilevel Feedback Queue Scheduling (MLFQ)

- added `PQwtime[MAXQ]`, `PQIndex`, `timeslices`, `ifqueue`, `Qticks` to `struct proc`
- Each process in first pushed into the queue with the highest priority (index: 0) 
- Using round robin scheduling in the highest priority queue which is not empty, we select the process to be scheduled.
- `timeslices` initialized according the queue it is present in when the process is scheduled, where it preempted and moved to a queue to lower priorty when the `timeslices` becomes `0`.
- `Qticks` is used to check how long the process has been present in the given queue, which is used to implement the `ageing` function, which prevents starvation.
- added custom preemption to `user`

### Processes that can exploit MLFQ 
For any given process, if `timeslices` becomes 0, then the process is moved to lower priority queue, but if the process is relinquished before the time slice expires, then it is added to the same priority queue. So if the process is relinquished always before the time slice expires, then the process keeps getting added the same priority queue. This way, some processes which are being interrupted before their time slices expire can exploit the priority queue system in MLFQ.

### Procdump
- added `PQwtime[MAXQ]` to the `struct proc` in order to display the wait time in each queue in MLFQ which is updated in the `clockintr` function.
- added `total_rtime` to the `struct proc` which is used to display the total run time of process since its creation.
- added `nrun` to the `struct proc` to count the number of times the process has been scheduled by the scheduler.

### Comparison of Schedulers
Here all the calculation has been done using the benchmarking program that has been provided, where for MLFQ, the calculation has been done using 1 CPU and the rest have been calculated using 3 CPUs.
- **RR** : Average rtime 16,  wtime 117
- **FCFS** : Average rtime 39,  wtime 47
- **PBS** : Average rtime 19,  wtime 107
- **MLFQ** : Average rtime 19,  wtime 170