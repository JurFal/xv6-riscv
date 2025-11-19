#ifndef BASELINE_H
#define BASELINE_H

#include "../types.h"

// Baseline object pool structure
struct baseline_pool {
  void **pages;            // Array of allocated pages
  uint num_pages;          // Number of allocated pages
  uint max_pages;          // Maximum number of pages (capacity)
  uint obj_size;           // Object size
  uint objects_per_page;   // Number of objects per page
  uint allocated_count;    // Number of allocated objects
  void *free_list;         // Free object list
  struct spinlock lock;    // Pool lock
};

// Function declarations
void baseline_init(void);
void* baseline_alloc(uint obj_size);
void baseline_free(void *ptr, uint obj_size);
void baseline_get_stats(uint obj_size, uint *total_pages, uint *allocated_objs, 
                       uint *free_objs, uint *total_bytes, uint *allocated_bytes);
void baseline_print_stats(uint obj_size);
void baseline_reset(void);
void perf_test_slab_vs_kalloc(int num_allocs, int obj_size);

#endif // BASELINE_H