// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Implements a buddy allocator
// that allocates 4KB base pages and 2MB superpages.

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

#define MAX_ORDER 9 // 4KB << 9 = 2MB

struct {
  struct spinlock lock;
  struct run *free[MAX_ORDER + 1];
} buddy;

static inline uint64
order_size(int order)
{
  return ((uint64)PGSIZE) << order;
}

static inline int
is_aligned(uint64 addr, int order)
{
  return (addr % order_size(order)) == 0;
}

static void
add_free_block(uint64 addr, int order)
{
  struct run *r = (struct run *)addr;
  r->next = buddy.free[order];
  buddy.free[order] = r;
}

static int
remove_block_from_freelist(uint64 addr, int order)
{
  struct run *prev = 0;
  struct run *cur = buddy.free[order];
  while (cur) {
    if ((uint64)cur == addr) {
      if (prev)
        prev->next = cur->next;
      else
        buddy.free[order] = cur->next;
      return 1;
    }
    prev = cur;
    cur = cur->next;
  }
  return 0;
}

void
kinit()
{
  initlock(&buddy.lock, "buddy");
  for(int i = 0; i <= MAX_ORDER; i++)
    buddy.free[i] = 0;
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  acquire(&buddy.lock);
  while (p + PGSIZE <= (char*)pa_end) {
    // Find the largest order that fits and is aligned
    int placed = 0;
    for (int order = MAX_ORDER; order >= 0; order--) {
      uint64 sz = order_size(order);
      if (((uint64)p + sz) > (uint64)pa_end)
        continue;
      if (!is_aligned((uint64)p, order))
        continue;
      add_free_block((uint64)p, order);
      p += sz;
      placed = 1;
      break;
    }
    if (!placed) {
      // Fallback: move by 4KB to try next alignment
      add_free_block((uint64)p, 0);
      p += PGSIZE;
    }
  }
  release(&buddy.lock);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  // Buddy free for order-0
  acquire(&buddy.lock);
  // attempt to coalesce upward
  uint64 addr = (uint64)pa;
  int order = 0;
  for (; order < MAX_ORDER; order++) {
    uint64 sz = order_size(order);
    uint64 buddy_addr = addr ^ sz;
    // If buddy block is free, remove it and merge
    if (remove_block_from_freelist(buddy_addr, order)) {
      // merged block address is the lower of the two, aligned to next order
      addr = (addr < buddy_addr) ? addr : buddy_addr;
      addr = addr & ~(order_size(order + 1) - 1);
      // continue to try merging at the next order
      continue;
    }
    // cannot merge at this order
    break;
  }
  add_free_block(addr, order);
  release(&buddy.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  acquire(&buddy.lock);
  // Find a free block of order 0, or split a higher-order block
  int src_order = -1;
  for (int o = 0; o <= MAX_ORDER; o++) {
    if (buddy.free[o] != 0) { src_order = o; break; }
  }
  if (src_order < 0) {
    release(&buddy.lock);
    return 0;
  }
  // take a block from src_order
  uint64 addr = (uint64)buddy.free[src_order];
  buddy.free[src_order] = buddy.free[src_order]->next;
  // split down to order 0
  while (src_order > 0) {
    src_order--;
    uint64 half = addr + order_size(src_order);
    add_free_block(half, src_order);
  }
  release(&buddy.lock);
  memset((char*)addr, 5, PGSIZE); // fill with junk
  return (void*)addr;
}

#ifdef LAB_PGTBL
void *
superalloc(void)
{
  // Allocate a 2MB superpage via buddy allocator (order 9)
  acquire(&buddy.lock);
  int src_order = -1;
  for (int o = MAX_ORDER; o >= 0; o--) {
    if (buddy.free[o] != 0) { src_order = o; break; }
  }
  if (src_order < 9) {
    // find and split a larger block if available
    if (src_order < 0) {
      release(&buddy.lock);
      return 0;
    }
  }
  // take a block from src_order
  uint64 addr = (uint64)buddy.free[src_order];
  buddy.free[src_order] = buddy.free[src_order]->next;
  // split down to order 9
  while (src_order > 9) {
    src_order--;
    uint64 half = addr + order_size(src_order);
    add_free_block(half, src_order);
  }
  release(&buddy.lock);
  memset((char*)addr, 5, SUPERPGSIZE);
  return (void*)addr;
}

void
superfree(void *pa)
{
  if(((uint64)pa % SUPERPGSIZE) != 0 || (char*)pa < end || (uint64)pa + SUPERPGSIZE > PHYSTOP)
    panic("superfree");
  // Buddy free for order-9
  acquire(&buddy.lock);
  uint64 addr = (uint64)pa;
  int order = 9;
  for (; order < MAX_ORDER; order++) {
    uint64 sz = order_size(order);
    uint64 buddy_addr = addr ^ sz;
    if (remove_block_from_freelist(buddy_addr, order)) {
      addr = (addr < buddy_addr) ? addr : buddy_addr;
      addr = addr & ~(order_size(order + 1) - 1);
      continue;
    }
    break;
  }
  add_free_block(addr, order);
  release(&buddy.lock);
}

#endif
