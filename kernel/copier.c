#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "copier.h"

struct copier_queue copier_q;
struct proc *copier_proc;

void copier_worker(void *arg) {
    struct copier_task *t;
    char *buf;
    
    printf("copier_worker: started\n");
    
    buf = kalloc();
    if (buf == 0)
        panic("copier_worker: kalloc failed");

    while (1) {
        acquire(&copier_q.lock);
        
        while (copier_q.head == copier_q.tail) {
            sleep(&copier_q, &copier_q.lock);
        }

        t = &copier_q.tasks[copier_q.head];
        
        // Check if process is still valid
        if (t->p && t->p->state != ZOMBIE) {
            // Do the copy
            // Release lock during copy to allow concurrency
            release(&copier_q.lock);
            
            uint64 src = t->src;
            uint64 dst = t->dst;
            int len = t->len;
            int ok = 1;
            
            while (len > 0) {
                int n = len > PGSIZE ? PGSIZE : len;
                if (copyin(t->p->pagetable, buf, src, n) < 0) {
                    printf("copier: copyin failed\n");
                    ok = 0;
                    break;
                }
                if (copyout(t->p->pagetable, dst, buf, n) < 0) {
                    printf("copier: copyout failed\n");
                    ok = 0;
                    break;
                }
                len -= n;
                src += n;
                dst += n;
            }
            
            acquire(&copier_q.lock);
            t->status = ok ? 1 : -1;
        } else {
            t->status = -1;
        }

        // Move head
        copier_q.head = (copier_q.head + 1) % COPIER_QUEUE_SIZE;
        wakeup(&copier_q); // Wake up any waiters (csync)
        release(&copier_q.lock);
    }
}

void copier_init(void) {
    initlock(&copier_q.lock, "copier_queue");
    copier_q.head = 0;
    copier_q.tail = 0;
    
    // Create the kernel thread
    copier_proc = kthread_create(copier_worker, 0, "copier");
    if(copier_proc == 0)
        panic("copier_init: kthread_create failed");
}

int copier_submit(uint64 src, uint64 dst, int len) {
    acquire(&copier_q.lock);
    
    if ((copier_q.tail + 1) % COPIER_QUEUE_SIZE == copier_q.head) {
        release(&copier_q.lock);
        return -1; // Queue full
    }
    
    struct copier_task *t = &copier_q.tasks[copier_q.tail];
    t->src = src;
    t->dst = dst;
    t->len = len;
    t->status = 0;
    t->p = myproc();
    
    copier_q.tail = (copier_q.tail + 1) % COPIER_QUEUE_SIZE;
    
    wakeup(&copier_q); // Wake up worker
    
    release(&copier_q.lock);
    return 0;
}

int copier_wait(uint64 addr, int len) {
    acquire(&copier_q.lock);
    
    while(1) {
        int pending = 0;
        int idx = copier_q.head;
        while(idx != copier_q.tail) {
            struct copier_task *t = &copier_q.tasks[idx];
            if(t->p == myproc()) {
                 // Check overlap if addr is provided
                 // If addr is 0, wait for all tasks of this process
                 if (addr == 0 || (t->dst < addr + len && t->dst + t->len > addr)) {
                     pending = 1;
                     break;
                 }
            }
            idx = (idx + 1) % COPIER_QUEUE_SIZE;
        }
        
        if(!pending) {
            break;
        }
        
        sleep(&copier_q, &copier_q.lock);
    }
    
    release(&copier_q.lock);
    return 0;
}

uint64 sys_amemcpy(void) {
    uint64 src, dst;
    int len;
    
    argaddr(0, &dst);
    argaddr(1, &src);
    argint(2, &len);
        
    return copier_submit(src, dst, len);
}

uint64 sys_csync(void) {
    uint64 addr;
    int len;
    
    argaddr(0, &addr);
    argint(1, &len);
        
    return copier_wait(addr, len);
}
