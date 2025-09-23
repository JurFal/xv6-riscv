// Slab memory allocator implementation
// Provides efficient allocation and deallocation of fixed-size objects

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "slab.h"

// Global cache management
static struct kmem_cache *caches[MAX_CACHES];
static int num_caches = 0;
static struct spinlock cache_list_lock;

// Size classes for general-purpose kmalloc
// Powers of 2 from 8 bytes to 4096 bytes
static struct kmem_cache *size_caches[MAX_SIZE_CLASSES]; // 8, 16, 32, 64, ..., 4096

// Static cache structures for different sizes
static struct kmem_cache size_cache_8;
static struct kmem_cache size_cache_16;
static struct kmem_cache size_cache_32;
static struct kmem_cache size_cache_64;
static struct kmem_cache size_cache_128;
static struct kmem_cache size_cache_256;
static struct kmem_cache size_cache_512;
static struct kmem_cache size_cache_1024;
static struct kmem_cache size_cache_2048;
static struct kmem_cache size_cache_4096;

// Cache for cache descriptors themselves (bootstrap)
static struct kmem_cache cache_cache;

// Size classes array definition
const uint slab_size_classes[MAX_SIZE_CLASSES] = {
  8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

// Internal helper functions
static struct slab* slab_create(struct kmem_cache *cache);
static void __attribute__((unused)) slab_destroy(struct slab *slab);
static void* slab_alloc_obj(struct slab *slab);
static void __attribute__((unused)) slab_free_obj(struct slab *slab, void *obj);
static int find_size_class(uint size);
static uint align_up(uint size, uint alignment);

// Initialize the slab allocator
void
slab_init(void)
{
  // Initialize the cache list lock
  initlock(&cache_list_lock, "cache_list");
  
  // Initialize the cache_cache for bootstrap
  // This cache is used to allocate other cache structures
  strncpy(cache_cache.name, "cache_cache", sizeof(cache_cache.name));
  cache_cache.objsize = align_up(sizeof(struct kmem_cache), 8);
  cache_cache.align = 8;
  cache_cache.ctor = 0;
  cache_cache.dtor = 0;
  cache_cache.partial = 0;
  cache_cache.full = 0;
  cache_cache.empty = 0;
  initlock(&cache_cache.lock, "cache_cache");
  cache_cache.total_slabs = 0;
  cache_cache.active_objs = 0;
  cache_cache.total_objs = 0;
  cache_cache.colour_range = 0;
  cache_cache.colour_next = 0;
  
  // Create size caches for kmalloc
  // Powers of 2 from 8 bytes to 4096 bytes
  uint sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
  struct kmem_cache *static_caches[] = {
    &size_cache_8, &size_cache_16, &size_cache_32, &size_cache_64,
    &size_cache_128, &size_cache_256, &size_cache_512, &size_cache_1024,
    &size_cache_2048, &size_cache_4096
  };
  
  for(int i = 0; i < 10; i++) {
    struct kmem_cache *cache = static_caches[i];
    
    // Initialize cache name
    char *names[] = {"size-8", "size-16", "size-32", "size-64", "size-128",
                     "size-256", "size-512", "size-1024", "size-2048", "size-4096"};
    strncpy(cache->name, names[i], sizeof(cache->name));
    
    // Initialize cache fields
    cache->objsize = align_up(sizes[i], 8);
    cache->align = 8;
    cache->ctor = 0;
    cache->dtor = 0;
    cache->partial = 0;
    cache->full = 0;
    cache->empty = 0;
    initlock(&cache->lock, cache->name);
    cache->total_slabs = 0;
    cache->active_objs = 0;
    cache->total_objs = 0;
    cache->colour_range = 0;
    cache->colour_next = 0;
    
    // Set up size_caches array
    size_caches[i] = cache;
    
    // Add to global cache list
    if(num_caches < MAX_CACHES) {
      caches[num_caches] = cache;
      num_caches++;
    }
  }
  
  // Set up initial state
  // num_caches is already updated in the loop above
  // Just make sure cache_cache is in the global list
  if(num_caches < MAX_CACHES) {
    caches[num_caches] = &cache_cache;
    num_caches++;
  }
}

// Create a new cache for objects of specified size and alignment
struct kmem_cache*
kmem_cache_create(const char *name, uint size, uint align,
                 void (*ctor)(void *), void (*dtor)(void *))
{
  // Check parameters
  if(size == 0 || align == 0) {
    return 0;
  }
  
  // 1. Allocate cache structure from cache_cache
  // Use the cache_cache to allocate kmem_cache structures
  struct kmem_cache *cache = (struct kmem_cache*)kmem_cache_alloc(&cache_cache);
  if(cache == 0) {
    return 0;
  }
  
  // 2. Initialize cache fields
  strncpy(cache->name, name, sizeof(cache->name) - 1);
  cache->name[sizeof(cache->name) - 1] = '\0';
  
  // 3. Calculate aligned object size
  cache->objsize = align_up(size, align);
  cache->align = align;
  
  // Set constructor and destructor
  cache->ctor = ctor;
  cache->dtor = dtor;
  
  // Initialize slab lists
  cache->partial = 0;
  cache->full = 0;
  cache->empty = 0;
  
  // Initialize lock
  initlock(&cache->lock, "kmem_cache");
  
  // Initialize statistics
  cache->total_slabs = 0;
  cache->active_objs = 0;
  cache->total_objs = 0;
  
  // Initialize coloring parameters
  cache->colour_range = 0;
  cache->colour_next = 0;
  
  // 4. Add to global cache list
  acquire(&cache_list_lock);
  if(num_caches < MAX_CACHES) {
    caches[num_caches] = cache;
    num_caches++;
    release(&cache_list_lock);
    return cache;
  } else {
    release(&cache_list_lock);
    // Use cache_cache to free the kmem_cache structure
    kmem_cache_free(&cache_cache, cache);
    return 0;
  }
}

// Destroy a cache and free all its slabs
void
kmem_cache_destroy(struct kmem_cache *cache)
{
  if(cache == 0) {
    return;
  }
  
  acquire(&cache->lock);
  
  // Check that partial and full lists are empty
  // In a production system, we should warn or handle this gracefully
  if(cache->partial != 0 || cache->full != 0) {
    // For now, we'll just proceed but this indicates a potential memory leak
    // In a real implementation, we might want to panic or log a warning
  }
  
  // 1. Free all slabs in empty list
  struct slab *slab = cache->empty;
  while(slab) {
    struct slab *next = slab->next;
    slab_destroy(slab);
    slab = next;
  }
  
  // 2.1. Free all slabs in partial list (if any)
  slab = cache->partial;
  while(slab) {
    struct slab *next = slab->next;
    slab_destroy(slab);
    slab = next;
  }
  
  // 2.2. Free all slabs in full list (if any)
  slab = cache->full;
  while(slab) {
    struct slab *next = slab->next;
    slab_destroy(slab);
    slab = next;
  }
  
  release(&cache->lock);
  
  // 3. Remove from global cache list
  acquire(&cache_list_lock);
  for(int i = 0; i < num_caches; i++) {
    if(caches[i] == cache) {
      // Move last cache to this position
      caches[i] = caches[num_caches - 1];
      num_caches--;
      break;
    }
  }
  release(&cache_list_lock);
  
  // 4. Free cache structure using cache_cache
  kmem_cache_free(&cache_cache, cache);
}

// Allocate an object from the cache
void*
kmem_cache_alloc(struct kmem_cache *cache)
{
  if(cache == 0) {
    return 0;
  }
  
  acquire(&cache->lock);
  
  void *obj = 0;
  
  // 1. Try to allocate from partial slab
  if(cache->partial) {
    obj = slab_alloc_obj(cache->partial);
    if(obj) {
      cache->active_objs++;
      
      // If slab becomes full, move it to full list
      if(cache->partial->nr_free == 0) {
        struct slab *full_slab = cache->partial;
        cache->partial = cache->partial->next;
        full_slab->next = cache->full;
        cache->full = full_slab;
      }
      
      release(&cache->lock);
      return obj;
    }
  }
  
  // 2. If no partial slab, try empty slab
  if(cache->empty) {
    struct slab *empty_slab = cache->empty;
    cache->empty = cache->empty->next;
    
    obj = slab_alloc_obj(empty_slab);
    if(obj) {
      cache->active_objs++;
      
      // Move to partial list
      empty_slab->next = cache->partial;
      cache->partial = empty_slab;
      
      release(&cache->lock);
      return obj;
    }
  }
  
  // 3. If no empty slab, create new slab
  release(&cache->lock);  // Release lock before calling slab_create (which may call kalloc)
  
  struct slab *new_slab = slab_create(cache);
  if(new_slab == 0) {
    return 0;
  }
  
  acquire(&cache->lock);
  
  // Allocate from the new slab
  obj = slab_alloc_obj(new_slab);
  if(obj) {
    cache->active_objs++;
    cache->total_slabs++;
    cache->total_objs += new_slab->nr_objs;
    
    // Add to partial list
    new_slab->next = cache->partial;
    cache->partial = new_slab;
  }
  
  release(&cache->lock);
  return obj;
}

// Free an object back to its cache
void
kmem_cache_free(struct kmem_cache *cache, void *obj)
{
  if(cache == 0 || obj == 0) {
    return;
  }
  
  acquire(&cache->lock);
  
  // 1. Find which slab the object belongs to
  // We need to search through all slab lists
  struct slab *slab = 0;
  struct slab **prev_ptr = 0;
  
  // Search in partial list
  prev_ptr = &cache->partial;
  for(struct slab *s = cache->partial; s; s = s->next) {
    if((char*)obj >= (char*)s->mem && (char*)obj < (char*)s->mem + PGSIZE) {
      slab = s;
      break;
    }
    prev_ptr = &s->next;
  }
  
  // Search in full list if not found in partial
  if(!slab) {
    prev_ptr = &cache->full;
    for(struct slab *s = cache->full; s; s = s->next) {
      if((char*)obj >= (char*)s->mem && (char*)obj < (char*)s->mem + PGSIZE) {
        slab = s;
        break;
      }
      prev_ptr = &s->next;
    }
  }
  
  if(!slab) {
    // Object not found in any slab - this is an error
    release(&cache->lock);
    return;
  }
  
  // 2. Free the object in the slab
  int was_full = (slab->nr_free == 0);
  slab_free_obj(slab, obj);
  cache->active_objs--;
  
  // 3. Update slab lists as needed
  if(slab->nr_free == slab->nr_objs) {
    // Slab is now empty - move to empty list
    *prev_ptr = slab->next;  // Remove from current list
    slab->next = cache->empty;
    cache->empty = slab;
    
    // 4. Check if we should reclaim some empty slabs
    // Count empty slabs
    int empty_count = 0;
    for(struct slab *s = cache->empty; s; s = s->next) {
      empty_count++;
    }
    
    // If we have too many empty slabs, reclaim some
    // Keep at least 1 empty slab for quick allocation, but not more than EMPTY_SLAB_THRESHOLD
    if(empty_count > EMPTY_SLAB_THRESHOLD) {
      // Remove and destroy the oldest empty slab
      struct slab *to_destroy = cache->empty;
      cache->empty = to_destroy->next;
      
      // Update cache statistics
       cache->total_slabs--;
      
      // Destroy the slab (this will call kfree on the memory)
      slab_destroy(to_destroy);
    }
  } else if(was_full) {
    // Slab was full but now has free space - move to partial list
    *prev_ptr = slab->next;  // Remove from full list
    slab->next = cache->partial;
    cache->partial = slab;
  }
  // If slab was already in partial list and is still partial, no move needed
  
  release(&cache->lock);
}

// General-purpose allocation (like malloc)
void*
kmalloc(uint size)
{
  // Handle zero size
  if(size == 0) {
    return 0;
  }
  
  // Find appropriate size class
  int class_idx = find_size_class(size);
  if(class_idx < 0) {
    // Size too large for our size classes, fall back to kalloc
    // This handles allocations larger than 4096 bytes
    return kalloc();
  }
  
  // Get the appropriate size cache
  struct kmem_cache *cache = size_caches[class_idx];
  if(cache == 0) {
    return 0;
  }
  
  // Try to allocate from a partial slab first
  if(cache->partial) {
    void *obj = slab_alloc_obj(cache->partial);
    if(obj) {
      // If slab becomes full, move it to full list
      if(cache->partial->nr_free == 0) {
        struct slab *full_slab = cache->partial;
        cache->partial = cache->partial->next;
        full_slab->next = cache->full;
        cache->full = full_slab;
      }
      return obj;
    }
  }
  
  // No partial slabs available, create a new one
  struct slab *new_slab = slab_create(cache);
  if(new_slab == 0) {
    return 0;
  }
  
  // Add to partial list
  new_slab->next = cache->partial;
  cache->partial = new_slab;
  
  // Allocate from the new slab
  return slab_alloc_obj(new_slab);
}

// Helper function to find which cache and slab an object belongs to
static struct kmem_cache*
find_object_cache(void *ptr, struct slab **found_slab)
{
  if(ptr == 0) {
    return 0;
  }
  
  // Search through all size class caches
  for(int i = 0; i < MAX_SIZE_CLASSES; i++) {
    struct kmem_cache *cache = size_caches[i];
    if(cache == 0) continue;
    
    acquire(&cache->lock);
    
    // Search in all slab lists (partial, full, empty)
    struct slab *lists[] = {cache->partial, cache->full, cache->empty};
    for(int j = 0; j < 3; j++) {
      for(struct slab *slab = lists[j]; slab; slab = slab->next) {
        if((char*)ptr >= (char*)slab->mem && 
           (char*)ptr < (char*)slab->mem + PGSIZE) {
          // Found the slab containing this object
          *found_slab = slab;
          release(&cache->lock);
          return cache;
        }
      }
    }
    
    release(&cache->lock);
  }
  
  // Object not found in any slab cache
  *found_slab = 0;
  return 0;
}

// General-purpose deallocation (like free)
void
kfree_slab(void *ptr)
{
  // Handle null pointer
  if(ptr == 0) {
    return;
  }
  
  // 1. Find which cache and slab this object belongs to
  struct slab *slab = 0;
  struct kmem_cache *cache = find_object_cache(ptr, &slab);
  
  if(cache == 0 || slab == 0) {
    // Object not found in any slab cache, fall back to kfree
    // This handles objects allocated with kalloc() directly (>4096 bytes)
    kfree(ptr);
    return;
  }
  
  // 2. Use the cache's free function to properly handle the object
  kmem_cache_free(cache, ptr);
}

// Internal helper: Create a new slab for a cache
static struct slab*
slab_create(struct kmem_cache *cache)
{
  // Allocate a page for the slab
  void *page = kalloc();
  if(page == 0) {
    return 0;
  }
  
  // Use the beginning of the page for the slab structure
  struct slab *slab = (struct slab*)page;
  
  // Initialize slab structure
  slab->cache = cache;
  slab->mem = page;
  slab->nr_objs = (PGSIZE - align_up(sizeof(struct slab), cache->align)) / cache->objsize;
  slab->nr_free = slab->nr_objs;
  slab->freelist = 0; // Will be set up below
  
  // Set up freelist - simple linked list of free objects
  // Objects start after the slab structure
  char *obj_start = (char*)page + align_up(sizeof(struct slab), cache->align);
  char *obj = obj_start;
  
  // Link all objects in the freelist
  for(uint i = 0; i < slab->nr_objs - 1; i++) {
    *(void**)obj = obj + cache->objsize;
    obj += cache->objsize;
  }
  // Last object points to null
  *(void**)obj = 0;
  
  slab->freelist = (void**)obj_start;
  
  return slab;
}

// Internal helper: Destroy a slab
static void
slab_destroy(struct slab *slab)
{
  if(slab == 0) {
    return;
  }
  
  // Free the page
  kfree(slab->mem);
}

// Internal helper: Allocate one object from a slab
static void*
slab_alloc_obj(struct slab *slab)
{
  if(slab == 0 || slab->nr_free == 0) {
    return 0;
  }
  
  // Get object from freelist
  void *obj = slab->freelist;
  if(obj == 0) {
    return 0;
  }
  
  // Update freelist to next free object
  slab->freelist = *(void**)obj;
  slab->nr_free--;
  
  // Call constructor if present
  if(slab->cache->ctor) {
    slab->cache->ctor(obj);
  }
  
  return obj;
}

// Internal helper: Free one object back to a slab
static void
slab_free_obj(struct slab *slab, void *obj)
{
  if(slab == 0 || obj == 0) {
    return;
  }
  
  // Call destructor if present
  if(slab->cache->dtor) {
    slab->cache->dtor(obj);
  }
  
  // Add object back to freelist
  *(void**)obj = slab->freelist;
  slab->freelist = obj;
  slab->nr_free++;
}

// Internal helper: Align size up to alignment boundary
static uint
align_up(uint size, uint align)
{
  return (size + align - 1) & ~(align - 1);
}

// Internal helper: Find size class index for kmalloc
static int
find_size_class(uint size)
{
  // Find the smallest size class that can accommodate the requested size
  for(int i = 0; i < MAX_SIZE_CLASSES; i++) {
    if(size <= slab_size_classes[i]) {
      return i;
    }
  }
  
  // Size too large for our size classes
  return -1;
}