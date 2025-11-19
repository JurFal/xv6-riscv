// khugepaged: background kernel thread that scans processes and
// collapses contiguous 4KB pages into 2MB huge pages when possible.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

extern struct proc proc[NPROC];

static void khugepaged_scan_pagetable(struct proc *p);
static void try_collapse_l1_region(struct proc *p, pte_t *pte1, uint64 va);

void
khugepaged_main(void *arg)
{
  (void)arg;
  for(;;){
    // Scan all processes for collapse opportunities
    struct proc *p;
    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state != RUNNABLE && p->state != RUNNING && p->state != SLEEPING){
        release(&p->lock);
        continue;
      }
      if(p->is_kthread){
        release(&p->lock);
        continue;
      }
      khugepaged_scan_pagetable(p);
      release(&p->lock);
    }

    // Sleep for a while; wake up on each tick
    acquire(&tickslock);
    sleep(&ticks, &tickslock);
    release(&tickslock);
  }
}

static void
khugepaged_scan_pagetable(struct proc *p)
{
  pagetable_t pt = p->pagetable; // L2 root in Sv39

  for(int l2_idx = 0; l2_idx < 512; l2_idx++){
    pte_t *pte2 = &pt[l2_idx];
    if((*pte2 & PTE_V) == 0)
      continue;
    // Skip if L2 is a leaf (1GB mapping)
    if(PTE_LEAF(*pte2))
      continue;

    pagetable_t l1 = (pagetable_t)PTE2PA(*pte2);
    for(int l1_idx = 0; l1_idx < 512; l1_idx++){
      uint64 va = (((uint64)l2_idx) << PXSHIFT(2)) | (((uint64)l1_idx) << PXSHIFT(1));
      if(va >= p->sz)
        break;
      pte_t *pte1 = &l1[l1_idx];
      if((*pte1 & PTE_V) == 0)
        continue;
      // Already a superpage (leaf at L1)? Skip.
      if(PTE_LEAF(*pte1))
        continue;
      try_collapse_l1_region(p, pte1, va);
    }
  }
}

static void
try_collapse_l1_region(struct proc *p, pte_t *pte1, uint64 va)
{
#ifdef LAB_PGTBL
  // pte1 points to a level-0 table; verify all 512 entries are mapped and leaf
  pagetable_t l0 = (pagetable_t)PTE2PA(*pte1);
  for(int j = 0; j < 512; j++){
    pte_t *pte0 = &l0[j];
    if(!(*pte0 & PTE_V) || !PTE_LEAF(*pte0)){
      return;
    }
  }

  // Allocate a new 2MB huge page
  char *hp = (char*)superalloc();
  if(hp == 0)
    return;

  // Copy 512 * 4KB pages into the huge page
  for(int j = 0; j < 512; j++){
    char *old = (char*)PTE2PA(l0[j]);
    memmove(hp + (j * PGSIZE), old, PGSIZE);
  }

  // Replace L1 PTE with a leaf mapping to the huge page
  uint64 pa = (uint64)hp;
  uint flags = (PTE_R | PTE_W | PTE_X | PTE_U | PTE_V);
  *pte1 = PA2PTE(pa) | flags;

  // Flush TLB
  sfence_vma();

  // Free old 4KB pages and the L0 page table
  for(int j = 0; j < 512; j++){
    kfree((void*)PTE2PA(l0[j]));
  }
  kfree((void*)l0);

  printf("khugepaged: collapsed 2MB at va %p (pid=%d)\n", (void*)va, p->pid);
#else
  (void)p; (void)pte1; (void)va;
#endif
}