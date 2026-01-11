#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#ifdef PGTBL_SOL
#include "riscv.h"
#endif
#include "vm.h"
#include "slab.h"
#include "test_slab/slab_test.h"
extern struct spinlock wait_lock; // from proc.c

extern struct proc proc[NPROC];

// Helper: map common signal numbers to names for debug logs
static const char* signame(int s) {
  switch (s) {
    case 1:  return "SIGHUP";
    case 2:  return "SIGINT";
    case 3:  return "SIGQUIT";
    case 4:  return "SIGILL";
    case 5:  return "SIGTRAP";
    case 6:  return "SIGABRT";
    case 7:  return "SIGBUS";
    case 8:  return "SIGFPE";
    case 9:  return "SIGKILL";
    case 10: return "SIGUSR1";
    case 11: return "SIGSEGV";
    case 12: return "SIGUSR2";
    case 13: return "SIGPIPE";
    case 14: return "SIGALRM";
    case 15: return "SIGTERM";
    case 16: return "SIGSTKFLT";
    case 17: return "SIGCHLD";
    case 18: return "SIGCONT";
    case 19: return "SIGSTOP";
    case 20: return "SIGTSTP";
    case 21: return "SIGTTIN";
    case 22: return "SIGTTOU";
    case 23: return "SIGURG";
    case 24: return "SIGXCPU";
    case 25: return "SIGXFSZ";
    case 26: return "SIGVTALRM";
    case 27: return "SIGPROF";
    case 28: return "SIGWINCH";
    case 29: return "SIGIO";    // aka SIGPOLL
    case 30: return "SIGPWR";
    case 31: return "SIGSYS";
    default: return "SIG?";
  }
}

// Helper: describe default action
static const char* sigdefault(int s) {
  switch (s) {
    case 1:  return "terminate";        // SIGHUP
    case 2:  return "terminate";        // SIGINT
    case 3:  return "terminate+core";   // SIGQUIT
    case 4:  return "terminate+core";   // SIGILL
    case 5:  return "terminate+core";   // SIGTRAP
    case 6:  return "terminate+core";   // SIGABRT
    case 7:  return "terminate+core";   // SIGBUS
    case 8:  return "terminate+core";   // SIGFPE
    case 9:  return "terminate";        // SIGKILL
    case 10: return "ignore";           // SIGUSR1
    case 11: return "terminate+core";   // SIGSEGV
    case 12: return "ignore";           // SIGUSR2
    case 13: return "terminate";        // SIGPIPE
    case 14: return "ignore";           // SIGALRM
    case 15: return "terminate";        // SIGTERM
    case 16: return "terminate";        // SIGSTKFLT
    case 17: return "ignore";           // SIGCHLD
    case 18: return "continue";         // SIGCONT
    case 19: return "stop";             // SIGSTOP (uncatchable)
    case 20: return "stop";             // SIGTSTP
    case 21: return "stop";             // SIGTTIN
    case 22: return "stop";             // SIGTTOU
    case 23: return "ignore";           // SIGURG
    case 24: return "terminate+core";   // SIGXCPU
    case 25: return "terminate+core";   // SIGXFSZ
    case 26: return "terminate";        // SIGVTALRM
    case 27: return "terminate";        // SIGPROF
    case 28: return "ignore";           // SIGWINCH
    case 29: return "terminate";        // SIGIO / SIGPOLL
    case 30: return "terminate";        // SIGPWR
    case 31: return "terminate+core";   // SIGSYS
    default: return "ignore";
  }
}

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_slab_alloc(void)
{
  int test_type;
  argint(0, &test_type);
  
  // Run kernel-space slab tests
  return run_slab_test(test_type);
}

uint64
sys_slab_free(void)
{
  // This is now unused, but kept for compatibility
  return 0;
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgpte(void)
{
  uint64 va;
  struct proc *p;  

  p = myproc();
  argaddr(0, &va);
  pte_t *pte = pgpte(p->pagetable, va);
  if(pte != 0) {
      return (uint64) *pte;
  }
  return 0;
}
#endif

#ifdef LAB_PGTBL
int
sys_kpgtbl(void)
{
  struct proc *p;  

  p = myproc();
  vmprint(p->pagetable);
  return 0;
}
#endif


uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Register a signal handler for the current process.
uint64
sys_signal(void)
{
  int signum;
  uint64 handler_addr;
  struct proc *p = myproc();

  // Fetch arguments (argint/argaddr return void)
  argint(0, &signum);
  argaddr(1, &handler_addr);

  // Validate signum range
  if(signum < 0 || signum >= NSIG)
    return -1;
  
  // Disallow catching SIGKILL
  if(signum == 9 || signum == 19){
    printf("[signal] pid=%d cannot register handler for %s (default=%s)\n",
           p->pid, signame(signum), sigdefault(signum));
    return -1;
  }
  
  // Validate handler address range (0 is valid since user text starts at 0)
  if(handler_addr >= p->sz)
    return -1;

  // Register or clear the handler pointer
  p->handlers[signum] = (void (*)(int))handler_addr;
  // 标记该信号已注册处理器，即使地址为 0 也视为已注册
  p->handlers_registered |= (1U << signum);
  printf("[signal] pid=%d register handler for signum=%d (%s) addr=0x%lx default=%s\n",
         p->pid, signum, signame(signum), handler_addr, sigdefault(signum));
  return 0;
}

// Send a signal to a target process: set its pending_signals bit.
uint64
sys_sigsend(void)
{
  int pid;
  int signum;

  argint(0, &pid);
  argint(1, &signum);

  if(signum < 0 || signum >= NSIG)
    return -1;

  for(struct proc *tp = proc; tp < &proc[NPROC]; tp++){
    acquire(&tp->lock);
    if(tp->state != UNUSED && tp->pid == pid){
      tp->pending_signals |= (1U << signum);
      printf("[sigsend] from pid=%d to pid=%d signum=%d (%s) pending=0x%x\n",
             myproc()->pid, tp->pid, signum, signame(signum), tp->pending_signals);
      // SIGKILL: mark killed under lock to avoid re-acquiring the same lock inside setkilled()
      if(signum == 9) {
        tp->killed = 1;
        release(&tp->lock);
        return 0;
      }
      if(signum == 18) {
        // SIGCONT: wake target from STOP safely; do not hold tp->lock while calling wakeup()
        release(&tp->lock);
        acquire(&wait_lock);
        acquire(&tp->lock);
        if(tp->stopped){
          tp->stopped = 0;
          release(&tp->lock);
          wakeup(&tp->stopped);
          printf("[sigsend] wakeup pid=%d from stop via SIGCONT\n", tp->pid);
        } else {
          release(&tp->lock);
        }
        release(&wait_lock);
        return 0;
      }
      // other signals
      release(&tp->lock);
      return 0;
    }
    release(&tp->lock);
  }
  printf("[sigsend] target pid=%d not found for signum=%d\n", pid, signum);
  return -1;
}

// Restore user context after a signal handler returns.
uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  if(p->tf_backup_valid == 0)
    return -1;

  // Restore saved trapframe and clear validity flag.
  *(p->trapframe) = p->tf_backup;
  p->tf_backup_valid = 0;
  printf("[sigreturn] pid=%d restored trapframe, returning to epc=0x%lx\n", p->pid, p->trapframe->epc);
  return 0;
}

uint64
sys_shutdown(void)
{
  printf("System shutdown initiated by user process...\n");
  printf("Goodbye! xv6 system is shutting down.\n");
  printf("All processes will be terminated.\n");
  
  // Use panic to halt the system
  panic("System shutdown requested");
  
  return 0;  // not reached
}
