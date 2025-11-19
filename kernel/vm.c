#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

#ifdef LAB_NET
  // PCI-E ECAM (configuration space), for pci.c
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

  // pci.c maps the e1000's registers here.
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
#endif  

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the kernel_pagetable, shared by all CPUs.
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
#ifdef LAB_PGTBL
      if(PTE_LEAF(*pte)) {
        return pte;
      }
#endif
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
#ifdef LAB_PGTBL
  // If this PTE is an L1 leaf (superpage), add VPN[0] contribution.
  pte_t *pte2 = &pagetable[PX(2, va)];
  if((*pte2 & PTE_V)) {
    pagetable_t l1 = (pagetable_t)PTE2PA(*pte2);
    pte_t *pte1 = &l1[PX(1, va)];
    if(pte == pte1 && PTE_LEAF(*pte1)){
      pa += ((uint64)PX(0, va) << PGSHIFT);
    }
  }
#endif
  return pa;
}


#if defined(LAB_PGTBL) || defined(SOL_MMAP) || defined(SOL_COW)
void
vmprint(pagetable_t pagetable) {
  // your code here
}
#endif



// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("mappages: va not aligned");

  if((size % PGSIZE) != 0)
    panic("mappages: size not aligned");

  if(size == 0)
    panic("mappages: size");
  
  a = va;
  last = va + size - PGSIZE;
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
#ifdef LAB_PGTBL
int
mappages_super(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  if((va % SUPERPGSIZE) != 0)
    panic("mappages_super: va not aligned");
  if((pa % SUPERPGSIZE) != 0)
    panic("mappages_super: pa not aligned");
  if((size % SUPERPGSIZE) != 0)
    panic("mappages_super: size not aligned");
  if(size == 0)
    panic("mappages_super: size");
  a = va;
  last = va + size - SUPERPGSIZE;
  for(;;){
    // ensure level-1 page table exists
    pte_t *pte2 = &pagetable[PX(2, a)];
    if((*pte2 & PTE_V) == 0){
      pagetable_t l1 = (pagetable_t)kalloc();
      if(l1 == 0)
        return -1;
      memset(l1, 0, PGSIZE);
      *pte2 = PA2PTE(l1) | PTE_V;
    }
    pagetable_t l1 = (pagetable_t)PTE2PA(*pte2);
    pte_t *pte1 = &l1[PX(1, a)];
    if(*pte1 & PTE_V)
      panic("mappages_super: remap");
    *pte1 = PA2PTE(pa) | perm | PTE_U | PTE_V;
    if(a == last)
      break;
    a += SUPERPGSIZE;
    pa += SUPERPGSIZE;
  }
  return 0;
}
#endif

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;
  int sz = PGSIZE;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

#ifdef LAB_PGTBL
  uint64 unmap_start = va;
  uint64 unmap_end = va + npages*PGSIZE;
#endif
  for(a = va; a < va + npages*PGSIZE; a += sz){
#ifdef LAB_PGTBL
    // Check if this address lives in a superpage mapping at level-1
    pte_t *pte2 = &pagetable[PX(2, a)];
    if((*pte2 & PTE_V)){
      pagetable_t l1 = (pagetable_t)PTE2PA(*pte2);
      pte_t *pte1 = &l1[PX(1, a)];
      if((*pte1 & PTE_V) && PTE_LEAF(*pte1)){
        uint64 sp_base = SUPERPGROUNDDOWN(a);
        if(unmap_start <= sp_base && unmap_end >= sp_base + SUPERPGSIZE){
          // unmap whole superpage
          sz = SUPERPGSIZE;
          if(do_free){
            uint64 pa = PTE2PA(*pte1);
            superfree((void*)pa);
          }
          *pte1 = 0;
          continue;
        } else {
          // demote superpage into 4KB pages to allow partial free
          uint flags = PTE_FLAGS(*pte1);
          uint64 pa_super = PTE2PA(*pte1);
          pagetable_t l0 = (pagetable_t)kalloc();
          if(l0 == 0)
            panic("uvmunmap: demote no mem");
          memset(l0, 0, PGSIZE);
          *pte1 = PA2PTE(l0) | PTE_V;
          for(int idx = 0; idx < 512; idx++){
            char *pg = (char*)kalloc();
            if(pg == 0)
              panic("uvmunmap: demote alloc");
            memmove(pg, (void*)(pa_super + ((uint64)idx * PGSIZE)), PGSIZE);
            l0[idx] = PA2PTE(pg) | (flags & (PTE_R|PTE_W|PTE_X|PTE_U)) | PTE_V;
          }
          superfree((void*)pa_super);
          // fall through to unmap 4KB below
        }
      }
    }
#endif
    if((pte = walk(pagetable, a, 0)) == 0) // leaf page table entry allocated?
      continue;
    if((*pte & PTE_V) == 0)  // has physical page been allocated?
      continue;
    sz = PGSIZE;
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}


// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;
  int sz;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += sz){
    sz = PGSIZE;
#ifdef LAB_PGTBL
    // Try to allocate superpage if we can fit a complete 2MB block
    uint64 superpage_start = SUPERPGROUNDUP(a);
    if(superpage_start + SUPERPGSIZE <= newsz && superpage_start > a) {
      // We can allocate a superpage starting at superpage_start
      // First, allocate 4KB pages up to the superpage boundary
      for(uint64 addr = a; addr < superpage_start; addr += PGSIZE) {
        // Check if this 4KB page is already covered by a superpage
        pte_t *pte2_b = &pagetable[PX(2, addr)];
        if(*pte2_b & PTE_V) {
          pagetable_t l1_b = (pagetable_t)PTE2PA(*pte2_b);
          pte_t *pte1_b = &l1_b[PX(1, addr)];
          if((*pte1_b & PTE_V) && PTE_LEAF(*pte1_b)) {
            // This 4KB page is covered by a superpage, skip it
            continue;
          }
        }
        
        char *mem_b = kalloc();
        if(mem_b == 0){
          uvmdealloc(pagetable, addr, oldsz);
          return 0;
        }
#ifndef LAB_SYSCALL
        memset(mem_b, 0, PGSIZE);
#endif
        printf("uvmalloc: mappages of address: %lu\n", addr);
        if(mappages(pagetable, addr, PGSIZE, (uint64)mem_b, PTE_R|xperm) != 0){
          kfree(mem_b);
          uvmdealloc(pagetable, addr, oldsz);
          return 0;
        }
      }
      
      // Before mapping superpage, check if L1 slot already exists and points to L0 table
      pte_t *pte2 = &pagetable[PX(2, superpage_start)];
      if((*pte2 & PTE_V)){
        pagetable_t l1 = (pagetable_t)PTE2PA(*pte2);
        pte_t *pte1 = &l1[PX(1, superpage_start)];
        if((*pte1 & PTE_V)){
          if(PTE_LEAF(*pte1)){
            // Already a superpage mapped here; skip and advance
            a = superpage_start + SUPERPGSIZE - PGSIZE;
            sz = PGSIZE;
            continue;
          } else {
            // Upgrade existing L0 table to a superpage: consolidate content then replace
            pagetable_t l0 = (pagetable_t)PTE2PA(*pte1);
            char *smem_up = (char*)superalloc();
            if(smem_up == 0){
              uvmdealloc(pagetable, superpage_start, oldsz);
              return 0;
            }
#ifndef LAB_SYSCALL
            memset(smem_up, 0, SUPERPGSIZE);
#endif
            for(int idx = 0; idx < 512; idx++){
              if(l0[idx] & PTE_V){
                uint64 pa_page = PTE2PA(l0[idx]);
                memmove(smem_up + idx * PGSIZE, (void*)pa_page, PGSIZE);
                kfree((void*)pa_page);
              }
            }
            kfree((void*)l0);
            *pte1 = PA2PTE((uint64)smem_up) | (PTE_R|xperm) | PTE_U | PTE_V;
            a = superpage_start + SUPERPGSIZE - PGSIZE;
            sz = PGSIZE;
            continue;
          }
        }
      }
      
      // Now allocate the superpage
      char *smem = (char*)superalloc();
      if(smem == 0){
        uvmdealloc(pagetable, superpage_start, oldsz);
        return 0;
      }
#ifndef LAB_SYSCALL
      memset(smem, 0, SUPERPGSIZE);
#endif
      if(mappages_super(pagetable, superpage_start, SUPERPGSIZE, (uint64)smem, PTE_R|xperm) != 0){
        superfree(smem);
        uvmdealloc(pagetable, superpage_start, oldsz);
        return 0;
      }
      
      // Update a to point after the superpage
      a = superpage_start + SUPERPGSIZE - PGSIZE;
      sz = PGSIZE;
      continue;
    }
    
    // Check if this 4KB page is already covered by a superpage
    pte_t *pte2 = &pagetable[PX(2, a)];
    if(*pte2 & PTE_V) {
      pagetable_t l1 = (pagetable_t)PTE2PA(*pte2);
      pte_t *pte1 = &l1[PX(1, a)];
      if((*pte1 & PTE_V) && PTE_LEAF(*pte1)) {
        // This 4KB page is covered by a superpage, skip it
        continue;
      }
    }
#endif
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
#ifndef LAB_SYSCALL
    memset(mem, 0, sz);
#endif
    printf("uvmalloc: mappages of address: %lu\n", a);
    if(mappages(pagetable, a, sz, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      // backtrace();
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;
  int szinc = PGSIZE;

  for(i = 0; i < sz; i += szinc){
    szinc = PGSIZE;
#ifdef LAB_PGTBL
    // Check if we're at the start of a potential superpage
    if((i % SUPERPGSIZE) == 0 && (sz - i) >= SUPERPGSIZE) {
        // Handle superpage copy in one shot
        pte_t *pte2 = &old[PX(2, i)];
        if((*pte2 & PTE_V)){
            pagetable_t l1 = (pagetable_t)PTE2PA(*pte2);
            pte_t *pte1 = &l1[PX(1, i)];
            if((*pte1 & PTE_V) && PTE_LEAF(*pte1)){
                szinc = SUPERPGSIZE;
                uint64 pa_super = PTE2PA(*pte1);
                flags = PTE_FLAGS(*pte1);
                char *smem = (char*)superalloc();
                if(smem == 0)
                    goto err;
                memmove(smem, (char*)pa_super, SUPERPGSIZE);

                // 如果子进程 L1 条目已存在
                pte_t *pte2_new = &new[PX(2, i)];
                if((*pte2_new & PTE_V)){
                    pagetable_t l1_new = (pagetable_t)PTE2PA(*pte2_new);
                    pte_t *pte1_new = &l1_new[PX(1, i)];
                    if((*pte1_new & PTE_V)) {
                        if(PTE_LEAF(*pte1_new)) {
                            // 子进程已经是巨页映射，直接拷贝内容到现有物理页
                            uint64 child_pa = PTE2PA(*pte1_new);
                            memmove((void*)child_pa, (void*)pa_super, SUPERPGSIZE);
                            superfree(smem);
                            continue;
                        } else {
                            // 释放已有的L0页表及其叶子页，改写为巨页叶子
                            pagetable_t l0 = (pagetable_t)PTE2PA(*pte1_new);
                            for(int idx = 0; idx < 512; idx++){
                                if(l0[idx] & PTE_V){
                                    uint64 pa_free = PTE2PA(l0[idx]);
                                    kfree((void*)pa_free);
                                }
                            }
                            kfree((void*)l0);
                            *pte1_new = PA2PTE((uint64)smem) | (flags & (PTE_R|PTE_W|PTE_X)) | PTE_U | PTE_V;
                            continue;
                        }
                    }
                }

                // 子进程该区域未有L1/L0条目，直接用巨页映射
                if(mappages_super(new, i, SUPERPGSIZE, (uint64)smem, flags & (PTE_R|PTE_W|PTE_X)) != 0){
                    superfree(smem);
                    goto err;
                }
                continue;
            }
        }
    }
#endif
    if((pte = walk(old, i, 0)) == 0)
      continue;
    if((*pte & PTE_V) == 0) {
      continue;
    }
    szinc = PGSIZE;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    printf("uvmcopy: mappages of address: %lu\n", i);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;

    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    if((pte = walk(pagetable, va0, 0)) == 0) {
      // printf("copyout: pte should exist %lx %ld\n", dstva, len);
      return -1;
    }


    // forbid copyout over read-only user text pages.
    if((*pte & PTE_W) == 0)
      return -1;
    
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;
  
  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}




// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
uint64
vmfault(pagetable_t pagetable, uint64 va, int read)
{
  uint64 mem;
  struct proc *p = myproc();
  

  if (va >= p->sz)
    return 0;
  va = PGROUNDDOWN(va);
  if(ismapped(pagetable, va)) {
    return 0;
  }
  mem = (uint64) kalloc();
  if(mem == 0)
    return 0;
  memset((void *) mem, 0, PGSIZE);
    printf("vmfault: mappages of address: %lu\n", va);
  if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W|PTE_U|PTE_R) != 0) {
    kfree((void *)mem);
    return 0;
  }
  return mem;
}

int
ismapped(pagetable_t pagetable, uint64 va) {
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V){
    return 1;
  }
  return 0;
}



#ifdef LAB_PGTBL
pte_t*
pgpte(pagetable_t pagetable, uint64 va) {
  return walk(pagetable, va, 0);
}
#endif
