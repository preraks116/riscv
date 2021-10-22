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

- added `rtime`, `wtime`, `priority`, `niceness` and `sched_time` to `struct proc`
- added `set_prority` syscall which sets the priority for a specified process (with process pid)
- added `setprio` function which calculates `DP` for each process in the process table before scheduling
- added PBS scheduler to `scheduler` function which sorts process according to `priority`, `sched_time` and `ctime`, and then switches cpu context to this process.