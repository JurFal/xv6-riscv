// Slab memory allocator for kernel objects
// Provides efficient allocation and deallocation of fixed-size objects

#ifndef SLAB_H
#define SLAB_H

#include "types.h"
#include "spinlock.h"

// Forward declarations
struct kmem_cache;
struct slab;

// Slab structure - container for objects of the same type
struct slab {
  struct slab     *next;        // Next slab in the list (partial/full/empty)
  struct kmem_cache *cache;     // Pointer to the cache this slab belongs to
  char            *mem;         // Start address of object area in this slab
  uint             nr_objs;     // Total number of objects in this slab
  uint             nr_free;     // Number of free objects in this slab
  
  // Free object management - using freelist approach for simplicity
  void           **freelist;    // Stack/linked list of free objects
  
  // Optional: bitmap for tracking allocated objects
  uint8           *freemap;     // Bitmap (optional, can be NULL if using freelist only)
  
  // Optional: color offset for cache line optimization
  uint             color_off;   // Color offset to reduce cache conflicts
};

// Cache structure - manages objects of a specific type/size
struct kmem_cache {
  char             name[32];    // Cache name for debugging
  uint             objsize;     // Size of each object (including alignment overhead)
  uint             align;       // Alignment requirement (typically cache line aligned)
  
  // Constructor and destructor function pointers
  void           (*ctor)(void *); // Constructor function (can be NULL)
  void           (*dtor)(void *); // Destructor function (can be NULL)
  
  // Slab lists for different states
  struct slab     *partial;     // Partially filled slabs (preferred for allocation)
  struct slab     *full;        // Completely full slabs
  struct slab     *empty;       // Empty slabs (candidates for deallocation)
  
  // Synchronization
  struct spinlock  lock;        // Protects all fields of this cache
  
  // Cache statistics (optional)
  uint             total_slabs; // Total number of slabs in this cache
  uint             active_objs; // Number of allocated objects
  uint             total_objs;  // Total number of objects across all slabs
  
  // Optional: coloring parameters for cache optimization
  uint             colour_range; // Range of possible color offsets
  uint             colour_next;  // Next color to use
};

// Constants
#define MAX_CACHES 32
#define MAX_SIZE_CLASSES 10  // Number of size classes (8, 16, 32, ..., 4096)
#define MIN_SLAB_SIZE 8      // Minimum object size
#define MAX_SLAB_SIZE 4096   // Maximum object size for slab allocation
#define EMPTY_SLAB_THRESHOLD 3  // Maximum number of empty slabs to keep

// Size classes array - powers of 2 from 8 to 4096 bytes
extern const uint slab_size_classes[MAX_SIZE_CLASSES];

// Function declarations

// Cache management functions
struct kmem_cache* kmem_cache_create(const char *name, uint size, uint align,
                                   void (*ctor)(void *), void (*dtor)(void *));
void kmem_cache_destroy(struct kmem_cache *cache);
void* kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *obj);

// General-purpose allocation functions
void* kmalloc(uint size);
void kfree_slab(void *ptr);

// Statistics structures
struct slab_cache_stats {
  char name[32];           // Cache name
  uint objsize;            // Object size
  uint nr_slabs;           // Total number of slabs
  uint nr_objs;            // Total number of objects
  uint nr_free;            // Number of free objects
  uint pages_in_use;       // Number of pages in use
  uint allocated_bytes;    // Total allocated bytes
  uint total_bytes;        // Total bytes (including free space)
  uint fragmentation_pct;  // Fragmentation percentage (0-100)
};

struct slab_global_stats {
  uint total_caches;       // Number of active caches
  uint total_pages;        // Total pages used by slab allocator
  uint total_allocated;    // Total allocated bytes across all caches
  uint total_free;         // Total free bytes across all caches
  uint global_fragmentation_pct; // Global fragmentation percentage
};

// Statistics and monitoring functions
void slab_print_stats(void);
void slab_get_cache_stats(struct kmem_cache *cache, struct slab_cache_stats *stats);
void slab_get_global_stats(struct slab_global_stats *stats);
int slab_reclaim_empty_slabs(struct kmem_cache *cache, int max_reclaim);

// Fragmentation analysis functions
struct fragmentation_analysis {
  uint internal_fragmentation_pct;  // Unused space within allocated pages
  uint external_fragmentation_pct;  // Free space that cannot be used
  uint utilization_pct;             // Actual usage efficiency
  uint wasted_bytes;                // Total wasted bytes
  uint optimal_pages;               // Theoretical minimum pages needed
  uint actual_pages;                // Actual pages in use
};

void slab_analyze_fragmentation(struct kmem_cache *cache, struct fragmentation_analysis *analysis);
void slab_print_fragmentation_report(void);

// Performance comparison functions
struct perf_stats {
  uint64 start_time;
  uint64 end_time;
  uint allocations;
  uint deallocations;
  uint memory_used;
  uint peak_memory;
};

void perf_test_slab_vs_kalloc(int num_allocs, int obj_size);

// Helper functions for testing
int slab_get_num_caches(void);
struct kmem_cache* slab_get_cache(int index);
void slab_acquire_cache_list_lock(void);
void slab_release_cache_list_lock(void);

// Initialization
void slab_init(void);

#endif // SLAB_H