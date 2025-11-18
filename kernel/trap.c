#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
// Order matters: fs.h (NDIRECT) and sleeplock.h are needed before file.h
#include "fs.h"      // NDIRECT and filesystem structures/constants
#include "sleeplock.h" // struct sleeplock used in struct inode
#include "file.h"    // struct inode definition
#include "stat.h"    // T_FILE constant
extern struct spinlock wait_lock; // for sleep/wakeup coordination

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

// simple integer to string (kernel-side, small helper)
static void itoa(int v, char *buf) {
  char tmp[16]; int i = 0; int n = v;
  if(n == 0){ buf[0] = '0'; buf[1] = '\0'; return; }
  int neg = 0; if(n < 0){ neg = 1; n = -n; }
  while(n){ tmp[i++] = '0' + (n % 10); n /= 10; }
  int pos = 0; if(neg) buf[pos++] = '-';
  while(i) buf[pos++] = tmp[--i];
  buf[pos] = '\0';
}

// Write a minimal core dump capturing process identity and registers.
static void dump_core(struct proc *p, int signum) {
  // Build filename "core.<pid>.<signum>"
  char path[32];
  char name[DIRSIZ];
  char pidbuf[16], sigbuf[16];
  itoa(p->pid, pidbuf);
  itoa(signum, sigbuf);
  // Compose path without slash so it is created in cwd
  // "core.<pid>.<signum>"
  int off = 0;
  const char *prefix = "core.";
  for(int i=0; prefix[i]; i++) path[off++] = prefix[i];
  for(int i=0; pidbuf[i]; i++) path[off++] = pidbuf[i];
  path[off++] = '.';
  for(int i=0; sigbuf[i]; i++) path[off++] = sigbuf[i];
  path[off] = '\0';

  begin_op();
  struct inode *dp = nameiparent(path, name);
  if(dp == 0){
    end_op();
    printf("[core] pid=%d signum=%d failed: nameiparent\n", p->pid, signum);
    return;
  }
  ilock(dp);
  struct inode *ip = dirlookup(dp, name, 0);
  if(ip){
    // Truncate existing file (keep it locked for writing)
    ilock(ip);
    itrunc(ip);
    iunlockput(dp);
  } else {
    // Allocate new inode and link it
    ip = ialloc(dp->dev, T_FILE);
    if(ip == 0){
      iunlockput(dp);
      end_op();
      printf("[core] pid=%d signum=%d failed: ialloc\n", p->pid, signum);
      return;
    }
    ilock(ip);
    ip->major = 0; ip->minor = 0; ip->nlink = 1; iupdate(ip);
    if(dirlink(dp, name, ip->inum) < 0){
      // de-allocate
      ip->nlink = 0; iupdate(ip);
      iunlockput(ip);
      iunlockput(dp);
      end_op();
      printf("[core] pid=%d signum=%d failed: dirlink\n", p->pid, signum);
      return;
    }
    iunlockput(dp);
  }

  // Prepare dump content
  struct {
    int pid;
    int signum;
    char name[16];
    struct trapframe tf;
  } dump;
  dump.pid = p->pid;
  dump.signum = signum;
  safestrcpy(dump.name, p->name, sizeof(dump.name));
  dump.tf = *(p->trapframe);

  // Write dump
  uint offw = 0;
  int wrote = writei(ip, 0, (uint64)&dump, offw, sizeof(dump));
  if(wrote != sizeof(dump)){
    printf("[core] pid=%d signum=%d write failed (%d/%lu)\n", p->pid, signum, wrote, sizeof(dump));
  }
  iunlockput(ip);
  end_op();
}

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from, and returns to, trampoline.S
// return value is user satp for trampoline.S to switch to.
//
uint64
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);  //DOC: kernelvec

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      kexit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else if((r_scause() == 15 || r_scause() == 13) &&
            vmfault(p->pagetable, r_stval(), (r_scause() == 13)? 1 : 0) != 0) {
    // page fault on lazily-allocated page
  } else {
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    kexit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  // Check and deliver pending signals before returning to user space.
  // Only if not already in a signal handler (avoid re-entry).
  if(p->pending_signals != 0 && p->tf_backup_valid == 0){
    printf("[trap] pid=%d has pending=0x%x, delivering...\n", p->pid, p->pending_signals);
    for(int signum = 0; signum < NSIG; signum++){
      if(p->pending_signals & (1U << signum)){
        void (*handler)(int) = p->handlers[signum];
        // 使用 handlers_registered 位掩码判断是否注册了处理器，允许地址为 0
        if(p->handlers_registered & (1U << signum)){
          // Clear pending bit and divert to handler
          p->pending_signals &= ~(1U << signum);
          // Backup current user context
          p->tf_backup = *(p->trapframe);
          p->tf_backup_valid = 1;
          // Prepare arguments and return address for handler
          p->trapframe->a0 = signum;                // first argument: signum
          p->trapframe->ra = 0xffffffffffffffffULL; // fake return address; handler must call sigreturn
          // Jump to handler when returning to user space
          printf("[trap] pid=%d deliver signum=%d handler=0x%lx epc(before)=0x%lx\n", p->pid, signum, (uint64)handler, p->trapframe->epc);
          p->trapframe->epc = (uint64)handler;
          break;
        } else {
          // No handler registered: default action
          switch(signum){
          case 1: // SIGHUP
            printf("[trap] pid=%d signum=SIGHUP default terminate\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 2: // SIGINT
            printf("[trap] pid=%d signum=SIGINT default terminate\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 3: // SIGQUIT
            printf("[trap] pid=%d signum=SIGQUIT default terminate+core\n", p->pid);
            dump_core(p, signum);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 4: // SIGILL
            printf("[trap] pid=%d signum=SIGILL default terminate+core\n", p->pid);
            dump_core(p, signum);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 5: // SIGTRAP
            printf("[trap] pid=%d signum=SIGTRAP default terminate+core\n", p->pid);
            dump_core(p, signum);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 6: // SIGABRT
            printf("[trap] pid=%d signum=SIGABRT default terminate+core\n", p->pid);
            dump_core(p, signum);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 7: // SIGBUS
            printf("[trap] pid=%d signum=SIGBUS default terminate+core\n", p->pid);
            dump_core(p, signum);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 8: // SIGFPE
            printf("[trap] pid=%d signum=SIGFPE default terminate+core\n", p->pid);
            dump_core(p, signum);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 11: // SIGSEGV
            printf("[trap] pid=%d signum=SIGSEGV default terminate+core\n", p->pid);
            dump_core(p, signum);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 9: // SIGKILL (uncatchable)
            printf("[trap] pid=%d signum=SIGKILL default terminate\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 13: // SIGPIPE
            printf("[trap] pid=%d signum=SIGPIPE default terminate\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 16: // SIGSTKFLT
            printf("[trap] pid=%d signum=SIGSTKFLT default terminate\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 15: // SIGTERM
            printf("[trap] pid=%d signum=SIGTERM default terminate\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 17: // SIGCHLD
            printf("[trap] pid=%d signum=SIGCHLD default ignore\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            continue;
          case 18: // SIGCONT
            printf("[trap] pid=%d signum=SIGCONT default continue\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            // If the process was stopped but managed to run to here (unlikely), clear stopped flag.
            // Actual wakeup is handled in sys_sigsend when SIGCONT is sent.
            continue;
          case 19: // SIGSTOP (uncatchable, default stop)
          case 20: // SIGTSTP
          case 21: // SIGTTIN
          case 22: // SIGTTOU
            printf("[trap] pid=%d signum=%d default stop (sleep)\n", p->pid, signum);
            p->pending_signals &= ~(1U << signum);
            // mark stopped; do NOT hold p->lock across sleep() to avoid double-acquire
            p->stopped = 1;
            acquire(&wait_lock);
            sleep(&p->stopped, &wait_lock);
            // woke up (likely via SIGCONT): continue scanning remaining signals
            release(&wait_lock);
            continue;
          case 10: // SIGUSR1
          case 12: // SIGUSR2
          case 14: // SIGALRM
          case 23: // SIGURG
            printf("[trap] pid=%d signum=SIGURG default ignore\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            continue;
          case 24: // SIGXCPU
            printf("[trap] pid=%d signum=SIGXCPU default terminate+core\n", p->pid);
            dump_core(p, signum);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 25: // SIGXFSZ
            printf("[trap] pid=%d signum=SIGXFSZ default terminate+core\n", p->pid);
            dump_core(p, signum);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 26: // SIGVTALRM
            printf("[trap] pid=%d signum=SIGVTALRM default terminate\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 27: // SIGPROF
            printf("[trap] pid=%d signum=SIGPROF default terminate\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 28: // SIGWINCH
            printf("[trap] pid=%d signum=SIGWINCH default ignore\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            continue;
          case 29: // SIGIO / SIGPOLL
            printf("[trap] pid=%d signum=SIGIO default terminate\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 30: // SIGPWR
            printf("[trap] pid=%d signum=SIGPWR default terminate\n", p->pid);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          case 31: // SIGSYS
            printf("[trap] pid=%d signum=SIGSYS default terminate+core\n", p->pid);
            dump_core(p, signum);
            p->pending_signals &= ~(1U << signum);
            setkilled(p);
            break;
          default:
            // Default: ignore and clear pending bit
            printf("[trap] pid=%d signum=%d default ignore\n", p->pid, signum);
            p->pending_signals &= ~(1U << signum);
            // continue scanning other pending signals
            continue;
          }
        }
      }
    }
  }

  prepare_return();

  // the user page table to switch to, for trampoline.S
  uint64 satp = MAKE_SATP(p->pagetable);

  // return to trampoline.S; satp value in a0.
  return satp;
}

//
// set up trapframe and control registers for a return to user space
//
void
prepare_return(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(). because a trap from kernel
  // code to usertrap would be a disaster, turn off interrupts.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if(scause == 0x8000000000000009L){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // timer interrupt.
    clockintr();
    return 2;
  } else {
    return 0;
  }
}

