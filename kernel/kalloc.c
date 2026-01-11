// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#ifdef LAB_PGTBL
#define NSUPER 8
struct {
  struct spinlock lock;
  void *pages[NSUPER];
  int count;
} supermem;
#endif

void
kinit()
{
  initlock(&kmem.lock, "kmem");
#ifdef LAB_PGTBL
  initlock(&supermem.lock, "supermem");
  supermem.count = 0;
#endif
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
#ifdef LAB_PGTBL
  for(; p + PGSIZE <= (char*)pa_end; ){
    if(supermem.count < NSUPER && (((uint64)p % SUPERPGSIZE) == 0) && (p + SUPERPGSIZE) <= (char*)pa_end){
      acquire(&supermem.lock);
      supermem.pages[supermem.count++] = (void*)p;
      release(&supermem.lock);
      p += SUPERPGSIZE;
      continue;
    }
    kfree(p);
    p += PGSIZE;
  }
#else
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
#endif
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

#ifdef LAB_PGTBL
void *
superalloc(void)
{
  void *p = 0;
  acquire(&supermem.lock);
  if(supermem.count > 0){
    p = supermem.pages[--supermem.count];
  }
  release(&supermem.lock);
  if(p)
    memset(p, 5, SUPERPGSIZE);
  return p;
}

void
superfree(void *pa)
{
  if(((uint64)pa % SUPERPGSIZE) != 0 || (char*)pa < end || (uint64)pa + SUPERPGSIZE > PHYSTOP)
    panic("superfree");
  acquire(&supermem.lock);
  if(supermem.count < NSUPER){
    supermem.pages[supermem.count++] = pa;
  } else {
    // If pool full, fall back to returning to regular allocator.
    // Break 2MB into 4KB pages and free them.
    release(&supermem.lock);
    for(char *p = (char*)pa; p < (char*)pa + SUPERPGSIZE; p += PGSIZE){
      kfree(p);
    }
    return;
  }
  release(&supermem.lock);
}
#endif
