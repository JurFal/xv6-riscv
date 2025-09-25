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

// Initialization
void slab_init(void);

#endif // SLAB_H