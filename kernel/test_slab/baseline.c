#include "../types.h"
#include "../param.h"
#include "../memlayout.h"
#include "../spinlock.h"
#include "../riscv.h"
#include "../defs.h"
#include "../slab.h"
#include "baseline.h"

static struct baseline_pool baseline_pools[10]; // Support up to 10 different sizes
static int num_baseline_pools = 0;
static struct spinlock baseline_lock;

// Initialize baseline pool system
void
baseline_init(void)
{
  initlock(&baseline_lock, "baseline");
  num_baseline_pools = 0;
}

// Find or create a baseline pool for given object size
static struct baseline_pool*
baseline_get_pool(uint obj_size)
{
  acquire(&baseline_lock);
  
  // Find existing pool
  for(int i = 0; i < num_baseline_pools; i++) {
    if(baseline_pools[i].obj_size == obj_size) {
      release(&baseline_lock);
      return &baseline_pools[i];
    }
  }
  
  // Create new pool
  if(num_baseline_pools >= 10) {
    release(&baseline_lock);
    return 0; // Too many pools
  }
  
  struct baseline_pool *pool = &baseline_pools[num_baseline_pools++];
  pool->obj_size = obj_size;
  pool->objects_per_page = PGSIZE / obj_size;
  pool->allocated_count = 0;
  pool->pages = 0;
  pool->num_pages = 0;
  pool->max_pages = 16; // Allow up to 16 pages per pool (64KB)
  pool->free_list = 0;
  initlock(&pool->lock, "baseline_pool");
  
  release(&baseline_lock);
  return pool;
}

// Allocate a new page for the pool
static int
baseline_alloc_page(struct baseline_pool *pool)
{
  // Allocate pages array if not allocated yet
  if(!pool->pages) {
    pool->pages = (void**)kalloc();
    if(!pool->pages) return 0;
    // Clear the pages array
    for(int i = 0; i < PGSIZE / sizeof(void*); i++) {
      pool->pages[i] = 0;
    }
  }
  
  // Check if we can allocate more pages
  if(pool->num_pages >= pool->max_pages) {
    return 0; // Pool full
  }
  
  // Allocate new page
  void *new_page = kalloc();
  if(!new_page) return 0;
  
  pool->pages[pool->num_pages++] = new_page;
  
  // Initialize free list for this page
  char *page = (char*)new_page;
  for(uint i = 0; i < pool->objects_per_page; i++) {
    void *obj = page + i * pool->obj_size;
    *(void**)obj = pool->free_list;
    pool->free_list = obj;
  }
  
  return 1;
}

// Allocate object from baseline pool
void*
baseline_alloc(uint obj_size)
{
  struct baseline_pool *pool = baseline_get_pool(obj_size);
  if(!pool) return 0;
  
  acquire(&pool->lock);
  
  // If no free objects, try to allocate a new page
  if(!pool->free_list) {
    if(!baseline_alloc_page(pool)) {
      release(&pool->lock);
      return 0; // Cannot allocate more pages
    }
  }
  
  // Allocate from free list
  if(!pool->free_list) {
    release(&pool->lock);
    return 0; // Should not happen
  }
  
  void *obj = pool->free_list;
  pool->free_list = *(void**)obj;
  pool->allocated_count++;
  
  release(&pool->lock);
  return obj;
}

// Free object to baseline pool
void
baseline_free(void *ptr, uint obj_size)
{
  if(!ptr) return;
  
  struct baseline_pool *pool = baseline_get_pool(obj_size);
  if(!pool) return;
  
  acquire(&pool->lock);
  
  // Add to free list
  *(void**)ptr = pool->free_list;
  pool->free_list = ptr;
  pool->allocated_count--;
  
  release(&pool->lock);
}

// Get baseline pool statistics
void
baseline_get_stats(uint obj_size, uint *total_pages, uint *allocated_objs, 
                  uint *free_objs, uint *total_bytes, uint *allocated_bytes)
{
  *total_pages = 0;
  *allocated_objs = 0;
  *free_objs = 0;
  *total_bytes = 0;
  *allocated_bytes = 0;
  
  struct baseline_pool *pool = baseline_get_pool(obj_size);
  if(!pool) return;
  
  acquire(&pool->lock);
  
  if(pool->pages && pool->num_pages > 0) {
    *total_pages = pool->num_pages;
    *allocated_objs = pool->allocated_count;
    uint total_objs = pool->num_pages * pool->objects_per_page;
    *free_objs = total_objs - pool->allocated_count;
    *total_bytes = pool->num_pages * PGSIZE;
    *allocated_bytes = pool->allocated_count * pool->obj_size;
  }
  
  release(&pool->lock);
}

// Print baseline pool statistics in similar format to slab
void
baseline_print_stats(uint obj_size)
{
  uint total_pages, allocated_objs, free_objs, total_bytes, allocated_bytes;
  baseline_get_stats(obj_size, &total_pages, &allocated_objs, &free_objs, 
                    &total_bytes, &allocated_bytes);
  
  uint fragmentation_pct = 0;
  if(total_bytes > 0) {
    fragmentation_pct = ((total_bytes - allocated_bytes) * 100) / total_bytes;
  }
  
  printf("=== Baseline Object Pool Statistics ===\n");
  printf("Global Stats:\n");
  printf("  Total caches: 1\n");
  printf("  Total pages: %d (%d KB)\n", total_pages, total_pages * 4);
  printf("  Total allocated: %d bytes\n", allocated_bytes);
  printf("  Total free: %d bytes\n", total_bytes - allocated_bytes);
  printf("  Global fragmentation: %d%%\n", fragmentation_pct);
  printf("\n");
  
  printf("Per-Cache Stats:\n");
  printf("Name\t\tObjSize\tSlabs\tObjs\tFree\tPages\tAlloc\tTotal\tFrag%%\n");
  printf("------------------------------------------------------------------------\n");
  printf("baseline\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
         obj_size, total_pages, allocated_objs + free_objs, free_objs, 
         total_pages, allocated_bytes, total_bytes, fragmentation_pct);
  printf("\n");
}

// Reset baseline pools (for testing)
void
baseline_reset(void)
{
  acquire(&baseline_lock);
  
  for(int i = 0; i < num_baseline_pools; i++) {
    struct baseline_pool *pool = &baseline_pools[i];
    acquire(&pool->lock);
    
    // Free all allocated pages
    if(pool->pages) {
      for(uint j = 0; j < pool->num_pages; j++) {
        if(pool->pages[j]) {
          kfree(pool->pages[j]);
        }
      }
      kfree(pool->pages);
      pool->pages = 0;
    }
    
    pool->num_pages = 0;
    pool->allocated_count = 0;
    pool->free_list = 0;
    
    release(&pool->lock);
  }
  
  num_baseline_pools = 0;
  release(&baseline_lock);
}

// Performance comparison: slab vs baseline object pool
void
perf_test_slab_vs_kalloc(int num_allocs, int obj_size)
{
  printf("=== Performance Test: Slab vs Baseline Object Pool ===\n");
  printf("Test parameters: %d allocations of %d bytes each\n", num_allocs, obj_size);
  printf("\n");
  
  void **ptrs = (void**)kalloc();  // Use one page to store pointers
  if(!ptrs) {
    printf("Failed to allocate memory for performance test\n");
    return;
  }
  
  // Initialize baseline pool system
  baseline_init();
  
  // Test 1: Slab allocator performance
  printf("=== Testing Slab Allocator ===\n");
  uint64 slab_start_time = r_time();
  
  // Allocation phase
  int slab_successful_allocs = 0;
  for(int i = 0; i < num_allocs; i++) {
    ptrs[i] = kmalloc(obj_size);
    if(ptrs[i]) {
      slab_successful_allocs++;
    }
  }
  
  uint64 slab_alloc_time = r_time();
  
  printf("Slab allocator state after allocation:\n");
  slab_print_stats();
  
  // Deallocation phase
  for(int i = 0; i < slab_successful_allocs; i++) {
    if(ptrs[i]) {
      kfree_slab(ptrs[i]);
    }
  }
  
  uint64 slab_end_time = r_time();
  
  // Calculate slab performance metrics
  uint64 slab_alloc_cycles = slab_alloc_time - slab_start_time;
  uint64 slab_total_cycles = slab_end_time - slab_start_time;
  
  printf("Slab Performance Results:\n");
  printf("  Successful allocations: %d/%d\n", slab_successful_allocs, num_allocs);
  printf("  Allocation time: %ld cycles\n", slab_alloc_cycles);
  printf("  Total time: %ld cycles\n", slab_total_cycles);
  printf("  Allocation throughput: %ld allocs/1000cycles\n", 
         slab_alloc_cycles > 0 ? (slab_successful_allocs * 1000) / slab_alloc_cycles : 0);
  printf("\n");
  
  // Clear pointer array before second test
  for(int i = 0; i < num_allocs; i++) {
    ptrs[i] = 0;
  }
  
  // Reset baseline pools for fair comparison
  baseline_reset();
  
  // Test 2: Baseline object pool performance
  printf("=== Testing Baseline Object Pool ===\n");
  uint64 baseline_start_time = r_time();
  
  // Allocation phase
  int baseline_successful_allocs = 0;
  for(int i = 0; i < num_allocs; i++) {
    ptrs[i] = baseline_alloc(obj_size);
    if(ptrs[i]) {
      baseline_successful_allocs++;
    }
  }
  
  uint64 baseline_alloc_time = r_time();
  
  printf("Baseline object pool state after allocation:\n");
  baseline_print_stats(obj_size);
  
  // Deallocation phase
  for(int i = 0; i < baseline_successful_allocs; i++) {
    if(ptrs[i]) {
      baseline_free(ptrs[i], obj_size);
    }
  }
  
  uint64 baseline_end_time = r_time();
  
  // Calculate baseline performance metrics
  uint64 baseline_alloc_cycles = baseline_alloc_time - baseline_start_time;
  uint64 baseline_total_cycles = baseline_end_time - baseline_start_time;
  
  printf("Baseline Performance Results:\n");
  printf("  Successful allocations: %d/%d\n", baseline_successful_allocs, num_allocs);
  printf("  Allocation time: %ld cycles\n", baseline_alloc_cycles);
  printf("  Total time: %ld cycles\n", baseline_total_cycles);
  printf("  Allocation throughput: %ld allocs/1000cycles\n", 
         baseline_alloc_cycles > 0 ? (baseline_successful_allocs * 1000) / baseline_alloc_cycles : 0);
  printf("\n");
  
  // Performance comparison
  printf("=== Performance Comparison ===\n");
  if(slab_alloc_cycles > 0 && baseline_alloc_cycles > 0) {
    printf("  Allocation speed ratio (slab/baseline): %ld%%\n", 
           (baseline_alloc_cycles * 100) / slab_alloc_cycles);
    printf("  Total speed ratio (slab/baseline): %ld%%\n", 
           (baseline_total_cycles * 100) / slab_total_cycles);
    
    if(slab_alloc_cycles < baseline_alloc_cycles) {
      printf("  Slab is %ld%% faster than baseline\n", 
             ((baseline_alloc_cycles - slab_alloc_cycles) * 100) / baseline_alloc_cycles);
    } else {
      printf("  Baseline is %ld%% faster than slab\n", 
             ((slab_alloc_cycles - baseline_alloc_cycles) * 100) / slab_alloc_cycles);
    }
  }
  
  printf("  Success rate comparison:\n");
  printf("    Slab: %d%% (%d/%d)\n", 
         (slab_successful_allocs * 100) / num_allocs, slab_successful_allocs, num_allocs);
  printf("    Baseline: %d%% (%d/%d)\n", 
         (baseline_successful_allocs * 100) / num_allocs, baseline_successful_allocs, num_allocs);
  
  printf("\n");
  
  // Clean up
  baseline_reset();
  kfree(ptrs);
}