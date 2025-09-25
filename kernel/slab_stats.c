#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "slab.h"
#include "slab_stats.h"

// Use accessor functions instead of direct access to static variables

// Helper functions for counting

static void
count_objects_in_list(struct slab *head, uint *total_objs, uint *free_objs)
{
  *total_objs = 0;
  *free_objs = 0;
  
  struct slab *slab = head;
  while(slab) {
    *total_objs += slab->nr_objs;
    *free_objs += slab->nr_free;
    slab = slab->next;
  }
}

// Get statistics for a specific cache
void
slab_get_cache_stats(struct kmem_cache *cache, struct slab_cache_stats *stats)
{
  if(!cache || !stats) {
    return;
  }
  
  acquire(&cache->lock);
  
  // Basic cache info
  strncpy(stats->name, cache->name, sizeof(stats->name) - 1);
  stats->name[sizeof(stats->name) - 1] = '\0';
  stats->objsize = cache->objsize;
  
  // Count slabs in each list
  uint partial_slabs = slab_count_slabs_in_list(cache->partial);
  uint full_slabs = slab_count_slabs_in_list(cache->full);
  uint empty_slabs = slab_count_slabs_in_list(cache->empty);
  
  stats->nr_slabs = partial_slabs + full_slabs + empty_slabs;
  
  // Count objects
  uint partial_objs, partial_free;
  uint full_objs, full_free;
  uint empty_objs, empty_free;
  
  count_objects_in_list(cache->partial, &partial_objs, &partial_free);
  count_objects_in_list(cache->full, &full_objs, &full_free);
  count_objects_in_list(cache->empty, &empty_objs, &empty_free);
  
  stats->nr_objs = partial_objs + full_objs + empty_objs;
  stats->nr_free = partial_free + full_free + empty_free;
  
  // Calculate memory usage
  stats->pages_in_use = stats->nr_slabs; // Assuming 1 page per slab
  stats->total_bytes = stats->pages_in_use * PGSIZE;
  stats->allocated_bytes = (stats->nr_objs - stats->nr_free) * cache->objsize;
  
  // Calculate fragmentation percentage
  if(stats->total_bytes > 0) {
    stats->fragmentation_pct = 100 - (stats->allocated_bytes * 100) / stats->total_bytes;
  } else {
    stats->fragmentation_pct = 0;
  }
  
  release(&cache->lock);
}

// Get global statistics across all caches
void
slab_get_global_stats(struct slab_global_stats *stats)
{
  if(!stats) {
    return;
  }
  
  stats->total_caches = 0;
  stats->total_pages = 0;
  stats->total_allocated = 0;
  stats->total_free = 0;
  
  // Acquire cache list lock to prevent race conditions
  slab_acquire_cache_list_lock();
  
  int total_caches = slab_get_num_caches();
  
  for(int i = 0; i < total_caches; i++) {
    struct kmem_cache *cache = slab_get_cache(i);
    if(cache) {
      struct slab_cache_stats cache_stats;
      slab_get_cache_stats(cache, &cache_stats);
      
      stats->total_caches++;
      stats->total_pages += cache_stats.pages_in_use;
      stats->total_allocated += cache_stats.allocated_bytes;
      stats->total_free += (cache_stats.total_bytes - cache_stats.allocated_bytes);
    }
  }
  
  slab_release_cache_list_lock();
  
  // Calculate global fragmentation
  uint total_bytes = stats->total_allocated + stats->total_free;
  if(total_bytes > 0) {
    stats->global_fragmentation_pct = (stats->total_free * 100) / total_bytes;
  } else {
    stats->global_fragmentation_pct = 0;
  }
}

// Print detailed statistics for all caches
void
slab_print_stats(void)
{
  printf("=== Slab Allocator Statistics ===\n");
  
  struct slab_global_stats global;
  slab_get_global_stats(&global);
  
  printf("Global Stats:\n");
  printf("  Total caches: %d\n", global.total_caches);
  printf("  Total pages: %d (%d KB)\n", global.total_pages, global.total_pages * 4);
  printf("  Total allocated: %d bytes\n", global.total_allocated);
  printf("  Total free: %d bytes\n", global.total_free);
  printf("  Global fragmentation: %d%%\n", global.global_fragmentation_pct);
  printf("\n");
  
  printf("Per-Cache Stats:\n");
  printf("%-12s %8s %6s %6s %6s %6s %8s %8s %5s\n",
         "Name", "ObjSize", "Slabs", "Objs", "Free", "Pages", "Alloc", "Total", "Frag%");
  printf("------------------------------------------------------------------------\n");
  
  // Acquire cache list lock to prevent race conditions
  slab_acquire_cache_list_lock();
  
  int total_caches = slab_get_num_caches();
  for(int i = 0; i < total_caches; i++) {
    struct kmem_cache *cache = slab_get_cache(i);
    if(cache) {
      struct slab_cache_stats stats;
      slab_get_cache_stats(cache, &stats);
      
      printf("%-12s %8d %6d %6d %6d %6d %8d %8d %5d\n",
             stats.name, stats.objsize, stats.nr_slabs, stats.nr_objs,
             stats.nr_free, stats.pages_in_use, stats.allocated_bytes,
             stats.total_bytes, stats.fragmentation_pct);
    }
  }
  
  slab_release_cache_list_lock();

  printf("\n");
}

// Analyze fragmentation for a specific cache
void
slab_analyze_fragmentation(struct kmem_cache *cache, struct fragmentation_analysis *analysis)
{
  if(!cache || !analysis) {
    return;
  }
  
  struct slab_cache_stats stats;
  slab_get_cache_stats(cache, &stats);
  
  // Calculate optimal pages needed (theoretical minimum)
  uint allocated_objects = stats.nr_objs - stats.nr_free;
  
  // Prevent division by zero
  if(cache->objsize == 0) {
    analysis->optimal_pages = 0;
    analysis->actual_pages = stats.pages_in_use;
    analysis->internal_fragmentation_pct = 0;
    analysis->external_fragmentation_pct = 0;
    analysis->utilization_pct = 0;
    analysis->wasted_bytes = 0;
    return;
  }
  
  uint objects_per_page = PGSIZE / cache->objsize;
  analysis->optimal_pages = (allocated_objects + objects_per_page - 1) / objects_per_page;
  analysis->actual_pages = stats.pages_in_use;
  
  // Calculate different types of fragmentation
  uint total_capacity = stats.pages_in_use * PGSIZE;
  uint used_space = allocated_objects * cache->objsize;
  uint free_object_space = stats.nr_free * cache->objsize;
  
  // Internal fragmentation: space lost due to object alignment and slab overhead
  uint usable_space_per_page = objects_per_page * cache->objsize;
  uint internal_waste_per_page = PGSIZE - usable_space_per_page;
  uint total_internal_waste = internal_waste_per_page * stats.pages_in_use;
  
  // External fragmentation: free objects that could be used
  uint external_waste = free_object_space;
  
  analysis->wasted_bytes = total_internal_waste + external_waste;
  
  // Calculate percentages
  if(total_capacity > 0) {
    analysis->internal_fragmentation_pct = (total_internal_waste * 100) / total_capacity;
    analysis->external_fragmentation_pct = (external_waste * 100) / total_capacity;
    analysis->utilization_pct = (used_space * 100) / total_capacity;
  } else {
    analysis->internal_fragmentation_pct = 0;
    analysis->external_fragmentation_pct = 0;
    analysis->utilization_pct = 0;
  }
}

// Print comprehensive fragmentation report for all caches
void
slab_print_fragmentation_report(void)
{
  printf("=== Slab Fragmentation Analysis ===\n");
  
  uint total_internal_waste = 0;
  uint total_external_waste = 0;
  uint total_capacity = 0;
  uint total_used = 0;
  
  printf("%-12s %8s %6s %6s %8s %8s %6s %6s %6s\n",
         "Cache", "ObjSize", "OptPg", "ActPg", "IntFrag%", "ExtFrag%", "Util%", "Waste", "Effic");
  printf("--------------------------------------------------------------------------------\n");
  
  // Acquire cache list lock to prevent race conditions
  slab_acquire_cache_list_lock();
  
  int total_caches = slab_get_num_caches();
  for(int i = 0; i < total_caches; i++) {
    struct kmem_cache *cache = slab_get_cache(i);
    if(cache) {
      struct fragmentation_analysis analysis;
      struct slab_cache_stats stats;
      
      slab_analyze_fragmentation(cache, &analysis);
      slab_get_cache_stats(cache, &stats);
      
      uint allocated_objects = stats.nr_objs - stats.nr_free;
      uint used_space = allocated_objects * cache->objsize;
      uint capacity = stats.pages_in_use * PGSIZE;
      
      total_internal_waste += (analysis.internal_fragmentation_pct * capacity) / 100;
      total_external_waste += (analysis.external_fragmentation_pct * capacity) / 100;
      total_capacity += capacity;
      total_used += used_space;
      
      // Calculate efficiency (how close to optimal page usage)
      uint efficiency = 0;
      if(analysis.actual_pages > 0) {
        efficiency = (analysis.optimal_pages * 100) / analysis.actual_pages;
        if(efficiency > 100) efficiency = 100;
      }
      
      printf("%-12s %8d %6d %6d %8d %8d %6d %6d %6d\n",
             stats.name, stats.objsize, analysis.optimal_pages, analysis.actual_pages,
             analysis.internal_fragmentation_pct, analysis.external_fragmentation_pct,
             analysis.utilization_pct, analysis.wasted_bytes, efficiency);
    }
  }
  
  slab_release_cache_list_lock();

  
  printf("--------------------------------------------------------------------------------\n");
  
  // Global fragmentation summary
  if(total_capacity > 0) {
    uint global_internal_pct = (total_internal_waste * 100) / total_capacity;
    uint global_external_pct = (total_external_waste * 100) / total_capacity;
    uint global_utilization_pct = (total_used * 100) / total_capacity;
    uint total_waste = total_internal_waste + total_external_waste;
    
    printf("Global Summary:\n");
    printf("  Total capacity: %d bytes (%d pages)\n", total_capacity, total_capacity / PGSIZE);
    printf("  Total used: %d bytes\n", total_used);
    printf("  Internal fragmentation: %d%% (%d bytes)\n", global_internal_pct, total_internal_waste);
    printf("  External fragmentation: %d%% (%d bytes)\n", global_external_pct, total_external_waste);
    printf("  Overall utilization: %d%%\n", global_utilization_pct);
    printf("  Total waste: %d bytes\n", total_waste);
    printf("  Memory efficiency: %d%%\n", 100 - ((total_waste * 100) / total_capacity));
  }
   printf("\n");
}