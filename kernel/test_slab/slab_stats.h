#ifndef SLAB_STATS_H
#define SLAB_STATS_H

#include "../types.h"

// Statistics structures
struct slab_cache_stats {
  char name[32];
  uint objsize;
  uint nr_slabs;
  uint nr_objs;
  uint nr_free;
  uint pages_in_use;
  uint allocated_bytes;
  uint total_bytes;
  uint fragmentation_pct;
};

struct slab_global_stats {
  uint total_caches;
  uint total_pages;
  uint total_allocated;
  uint total_free;
  uint global_fragmentation_pct;
};

struct fragmentation_analysis {
  uint internal_fragmentation_pct;  // Unused space within allocated pages
  uint external_fragmentation_pct;  // Free space that cannot be used
  uint utilization_pct;             // Actual usage efficiency
  uint wasted_bytes;                // Total wasted bytes
  uint optimal_pages;               // Theoretical minimum pages needed
  uint actual_pages;                // Actual pages in use
};

// Function declarations
void slab_get_cache_stats(struct kmem_cache *cache, struct slab_cache_stats *stats);
void slab_get_global_stats(struct slab_global_stats *stats);
void slab_print_stats(void);
void slab_analyze_fragmentation(struct kmem_cache *cache, struct fragmentation_analysis *analysis);
void slab_print_fragmentation_report(void);

// Accessor functions for slab internals (to be implemented in slab.c)
int slab_get_num_caches(void);
struct kmem_cache* slab_get_cache(int index);
void slab_acquire_cache_list_lock(void);
void slab_release_cache_list_lock(void);
uint slab_count_slabs_in_list(struct slab *head);

#endif // SLAB_STATS_H