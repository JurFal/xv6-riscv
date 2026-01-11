// Simple user-space signal constants
#ifndef XV6_USER_SIGNAL_H
#define XV6_USER_SIGNAL_H

#define SIGHUP   1   // hangup – default terminate
#define SIGINT   2   // interrupt (ctrl-c) – default terminate
#define SIGQUIT  3   // quit (ctrl-\) – default terminate + core
#define SIGILL   4   // illegal instruction – default terminate + core
#define SIGTRAP  5   // trace/breakpoint trap – default terminate + core
#define SIGABRT  6   // abort() – default terminate + core
#define SIGBUS   7   // bus error – default terminate + core
#define SIGFPE   8   // floating-point exception – default terminate + core
#define SIGKILL  9   // kill – cannot be caught, default terminate
#define SIGUSR1  10  // user-defined 1 – default ignore
#define SIGSEGV  11  // invalid memory reference – default terminate + core
#define SIGUSR2  12  // user-defined 2 – default ignore
#define SIGPIPE  13  // write on a pipe with no reader – default terminate
#define SIGALRM  14  // alarm clock – default ignore
#define SIGTERM  15  // termination request – default terminate
#define SIGSTKFLT 16 // coprocessor stack fault (Linux specific, obsolete) – default terminate
#define SIGCHLD  17  // child status has changed – default ignore
#define SIGCONT  18  // continue executing, if stopped – default continue
#define SIGSTOP  19  // stop process – uncatchable, default stop
#define SIGTSTP  20  // terminal stop (Ctrl+Z) – default stop
#define SIGTTIN  21  // background read from tty – default stop
#define SIGTTOU  22  // background write to tty – default stop
#define SIGURG   23  // urgent data on socket – default ignore
#define SIGXCPU  24  // CPU time limit exceeded – default terminate + core
#define SIGXFSZ  25  // file size limit exceeded – default terminate + core
#define SIGVTALRM 26 // virtual timer expired – default terminate
#define SIGPROF  27  // profiling timer expired – default terminate
#define SIGWINCH 28  // window size change – default ignore
#define SIGIO    29  // I/O now possible (aka SIGPOLL) – default terminate
#define SIGPWR   30  // power failure (System V) – default terminate
#define SIGSYS   31  // bad system call – default terminate + core
#define NSIG     32

#endif // XV6_USER_SIGNAL_H