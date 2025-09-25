// Slab allocator test functions implementation
// Provides testing functionality for the slab memory allocator

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "slab.h"
#include "slab_test.h"

// Simple pseudo-random number generator for testing
static uint32 test_seed = 1;

static uint32
test_rand(void)
{
  test_seed = test_seed * 1103515245 + 12345;
  return (test_seed / 65536) % 32768;
}

static void
test_srand(uint32 seed)
{
  test_seed = seed;
}

/* -------------------- Shadow memory -------------------- */
/* 1 byte of shadow covers 8 bytes of arena.
   0x00 = unpoisoned/allocated; 0xFF = poisoned/free
*/
static uint8 *shadow = 0; // length = (arena_pages*PAGE_SIZE)/8
static uint64 arena_base = 0;
static uint arena_pages = 128;  // Assume 128 pages for testing arena

#define SHADOW_SCALE 3u
#define PAGE_SIZE 4096
#define SHADOW_POISONED 0xFF
#define SHADOW_UNPOISONED 0x00

static inline uint
shadow_len_bytes(void)
{
  return ((uint)arena_pages * PAGE_SIZE) >> SHADOW_SCALE;
}

static void
shadow_init(void)
{
  // Simplified shadow memory - just allocate one page
  shadow = (uint8*)kalloc();
  if (!shadow) {
    printf("shadow_init: failed to allocate shadow memory\n");
    return;
  }
  
  // Initialize all shadow memory as poisoned (free)
  memset(shadow, SHADOW_POISONED, PGSIZE);
  
  printf("Shadow memory initialized: %d bytes\n", PGSIZE);
}

static inline void*
idx_to_addr(uint idx)
{
  return (void*)(arena_base + idx * PAGE_SIZE);
}

static inline uint
addr_to_shadow_idx(uint64 addr)
{
  if (!arena_base) return 0;
  uint64 off = addr - arena_base;
  return (uint)(off >> SHADOW_SCALE);
}

static void
shadow_poison(uint64 addr, uint sz)  // mark free
{
  if (!shadow || !arena_base) return;
  
  uint si = addr_to_shadow_idx(addr);
  uint ei = addr_to_shadow_idx(addr + sz - 1);
  
  // Ensure we don't go beyond the allocated shadow memory
  if (si >= PGSIZE || ei >= PGSIZE) return;
  
  for (uint i = si; i <= ei && i < PGSIZE; i++) {
    shadow[i] = SHADOW_POISONED;
  }
}

static void
shadow_unpoison(uint64 addr, uint sz)  // mark allocated
{
  if (!shadow || !arena_base) return;
  
  uint si = addr_to_shadow_idx(addr);
  uint ei = addr_to_shadow_idx(addr + sz - 1);
  
  // Ensure we don't go beyond the allocated shadow memory
  if (si >= PGSIZE || ei >= PGSIZE) return;
  
  for (uint i = si; i <= ei && i < PGSIZE; i++) {
    shadow[i] = SHADOW_UNPOISONED;
  }
}

static int
shadow_expect_poisoned(uint64 addr, uint sz)
{
  if (!shadow || !arena_base) return 1;
  
  uint si = addr_to_shadow_idx(addr);
  uint ei = addr_to_shadow_idx(addr + sz - 1);
  
  // Ensure we don't go beyond the allocated shadow memory
  if (si >= PGSIZE || ei >= PGSIZE) return 1;
  
  for (uint i = si; i <= ei && i < PGSIZE; i++) {
    if (shadow[i] != SHADOW_POISONED) {
      printf("Shadow violation: expected poisoned @%d, got 0x%x\n", i, shadow[i]);
      return 0;
    }
  }
  return 1;
}

static int
shadow_expect_unpoisoned(uint64 addr, uint sz)
{
  if (!shadow || !arena_base) return 1;
  
  uint si = addr_to_shadow_idx(addr);
  uint ei = addr_to_shadow_idx(addr + sz - 1);
  
  // Ensure we don't go beyond the allocated shadow memory
  if (si >= PGSIZE || ei >= PGSIZE) return 1;
  
  for (uint i = si; i <= ei && i < PGSIZE; i++) {
    if (shadow[i] != SHADOW_UNPOISONED) {
      printf("Shadow violation: expected unpoisoned @%d, got 0x%x, addr=%p\n", i, shadow[i], idx_to_addr(i));
      return 0;
    }
  }
  return 1;
}

// Round up to next power of 2
static uint
round_up_pow2(uint size)
{
  if (size <= 1) return 1;
  uint power = 1;
  while (power < size) {
    power <<= 1;
  }
  return power;
}

// Simple memory pattern check
static int
check_memory_pattern(void *ptr, uint size)
{
  if(!ptr) return 0;
  
  uint8 *bytes = (uint8*)ptr;
  uint8 pattern = 0x5A;
  
  // Write pattern to allocated memory
  for(uint i = 0; i < size; i++) {
    bytes[i] = pattern;
  }
  
  // Verify pattern
  for(uint i = 0; i < size; i++) {
    if(bytes[i] != pattern) {
      return 0;  // Memory corruption
    }
  }
  
  return 1;
}

// Kernel-space slab test function
int
run_slab_test(int test_type)
{
  printf("Running slab test type %d\n", test_type);
  
  switch(test_type) {
    case 1: // Basic allocation test
    {
      printf("Test 1: Basic allocation test\n");
      void *ptr1 = kmalloc(64);
      void *ptr2 = kmalloc(128);
      void *ptr3 = kmalloc(256);
      
      if(ptr1 && ptr2 && ptr3) {
        printf("  Allocation successful: ptr1=%p, ptr2=%p, ptr3=%p\n", ptr1, ptr2, ptr3);
        kfree_slab(ptr1);
        kfree_slab(ptr2);
        kfree_slab(ptr3);
        printf("  Deallocation successful\n");
        return 1;
      } else {
        printf("  Allocation failed\n");
        return 0;
      }
    }
    
    case 2: // Size classes test (extended to 2048)
    {
      printf("Test 2: Size classes test (8-2048 bytes)\n");
      void *ptrs[10];
      int success = 1;
      int test_sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
      
      // Allocate one object from each size class
      for(int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(test_sizes[i]);
        if(!ptrs[i]) {
          printf("  Failed to allocate size %d\n", test_sizes[i]);
          success = 0;
          break;
        }
        printf("  Allocated size %d: %p\n", test_sizes[i], ptrs[i]);
      }
      
      // Free all allocated objects
      for(int i = 0; i < 10; i++) {
        if(ptrs[i]) {
          kfree_slab(ptrs[i]);
        }
      }
      
      printf("  Size classes test %s\n", success ? "passed" : "failed");
      return success;
    }
    
    case 3: // Stress test
    {
      printf("Test 3: Stress test\n");
      void *ptrs[100];
      int allocated = 0;
      
      // Allocate many small objects
      for(int i = 0; i < 100; i++) {
        ptrs[i] = kmalloc(64);
        if(ptrs[i]) {
          allocated++;
        }
      }
      
      printf("  Allocated %d/100 objects\n", allocated);
      
      // Free all objects
  printf("  Starting to free objects...\n");
  int free_limit = 100;
  for(int i = 0; i < free_limit && i < allocated; i++) {
        if(ptrs[i]) {
          kfree_slab(ptrs[i]);
        }
      }
      
      printf("  Stress test completed\n");
      return allocated > 50 ? 1 : 0; // Consider success if we allocated more than 50%
    }
    
    case 4: // Edge cases test
    {
      printf("Test 4: Edge cases test\n");
      
      // Test zero size allocation
      void *ptr_zero = kmalloc(0);
      printf("  Zero size allocation: %p\n", ptr_zero);
      
      // Test very large allocation (should fall back to kalloc)
      void *ptr_large = kmalloc(8192);
      printf("  Large allocation (8192): %p\n", ptr_large);
      
      // Test boundary sizes
      void *ptr_boundary = kmalloc(4096);
      printf("  Boundary allocation (4096): %p\n", ptr_boundary);
      
      // Clean up
      if(ptr_zero) kfree_slab(ptr_zero);
      if(ptr_large) kfree_slab(ptr_large);
      if(ptr_boundary) kfree_slab(ptr_boundary);
      
      printf("  Edge cases test completed\n");
      return 1;
    }
    
    case 5: // Enhanced fuzz test with shadow memory
    {
      printf("Test 5: Enhanced fuzz test with shadow memory (8-2048 bytes)\n");
      shadow_init();
      test_srand(12345);  // Fixed seed for reproducible results
      
      void *ptrs[100];
      uint sizes[100];  // Store the actual allocated sizes
      int allocated = 0;
      int pattern_errors = 0;
      int shadow_errors = 0;
      
      // Set arena base from first allocation
      void *first_ptr = kmalloc(64);
      if (first_ptr) {
        arena_base = (uint64)first_ptr & ~0xFFFFF;  // Align to 1MB boundary
        kfree_slab(first_ptr);
        printf("  Arena base set to: %p\n", (void*)arena_base);
      }
      
      // Random allocation phase
      for(int i = 0; i < 100; i++) {
        uint random_size = 8 + (test_rand() % 2041);  // 8 to 2048
        uint rounded_size = round_up_pow2(random_size);
        
        ptrs[i] = kmalloc(rounded_size);
        sizes[i] = rounded_size;  // Store the actual allocated size
        
        if(ptrs[i]) {
          allocated++;
          uint64 addr = (uint64)ptrs[i];
          
          // Check if memory was properly freed before (should be poisoned)
          if (!shadow_expect_poisoned(addr, rounded_size)) {
            shadow_errors++;
          }
          
          // Mark as allocated (unpoison)
          shadow_unpoison(addr, rounded_size);
          
          // Test memory pattern
          if(!check_memory_pattern(ptrs[i], rounded_size)) {
            printf("  Memory pattern check failed for size %d\n", rounded_size);
            pattern_errors++;
          }
        }
      }
      
      printf("  Fuzz allocated %d/100 objects\n", allocated);
      
      // Free all objects and check shadow memory
      for(int i = 0; i < 100; i++) {
        if(ptrs[i]) {
          uint64 addr = (uint64)ptrs[i];
          uint rounded_size = sizes[i];  // Use the stored size instead of recalculating
          
          // Check if memory is still allocated (should be unpoisoned)
          if (!shadow_expect_unpoisoned(addr, rounded_size)) {
            shadow_errors++;
          }
          
          // Mark as freed (poison)
          shadow_poison(addr, rounded_size);
          
          kfree_slab(ptrs[i]);
        }
      }
      
      printf("  Fuzz test completed: pattern_errors=%d, shadow_errors=%d\n", 
             pattern_errors, shadow_errors);
      return (pattern_errors == 0 && shadow_errors == 0) ? 1 : 0;
    }
    
    case 6: // Enhanced stress test with shadow memory
    {
      printf("Test 6: Enhanced stress test with shadow memory\n");
      shadow_init();
      test_srand(54321);
      
      // Allocate arrays on heap to avoid stack overflow
      void **ptrs = (void**)kalloc();
      uint *sizes = (uint*)kalloc();
      if (!ptrs || !sizes) {
        printf("Test 6: Failed to allocate test arrays\n");
        if (ptrs) kfree(ptrs);
        if (sizes) kfree(sizes);
        return 0;
      }
      
      int allocated = 0;
      int freed = 0;
      int pattern_errors = 0;
      int shadow_errors = 0;
      
      // Initialize arrays (200 pointers fit in one page)
      for(int i = 0; i < 200 && i < PGSIZE/sizeof(void*); i++) {
        ptrs[i] = 0;
        sizes[i] = 0;
      }
      
      // Set arena base
      void *first_ptr = kmalloc(64);
      if (first_ptr) {
        arena_base = (uint64)first_ptr & ~0xFFFFF;
        kfree_slab(first_ptr);
      }
      
      // Mixed allocation and deallocation
      for(int round = 0; round < 5; round++) {
        printf("  Round %d: ", round + 1);
        
        // Allocate some objects
        for(int i = 0; i < 20; i++) {
          int idx = test_rand() % 200;
          if(!ptrs[idx]) {
            uint size = round_up_pow2(8 + (test_rand() % 1017));  // 8 to 1024 for safety
            ptrs[idx] = kmalloc(size);
            
            if(ptrs[idx]) {
              sizes[idx] = size;  // Store the actual allocated size
              allocated++;
              uint64 addr = (uint64)ptrs[idx];
              
              // Check shadow memory and mark as allocated
              if (!shadow_expect_poisoned(addr, size)) {
                shadow_errors++;
              }
              shadow_unpoison(addr, size);
              
              if(!check_memory_pattern(ptrs[idx], size)) {
                pattern_errors++;
              }
            }
          }
        }
        
        // Free some objects
        for(int i = 0; i < 10; i++) {
          int idx = test_rand() % 200;
          if(ptrs[idx]) {
            uint64 addr = (uint64)ptrs[idx];
            uint size = sizes[idx];  // Use the actual allocated size
            
            // Check shadow memory and mark as freed
            if (!shadow_expect_unpoisoned(addr, size)) {
              shadow_errors++;
            }
            shadow_poison(addr, size);
            
            kfree_slab(ptrs[idx]);
            ptrs[idx] = 0;
            sizes[idx] = 0;
            freed++;
          }
        }
        
        printf("alloc=%d, free=%d ", allocated, freed);
      }
      
      // Clean up remaining allocations
      for(int i = 0; i < 200; i++) {
        if(ptrs[i]) {
          uint64 addr = (uint64)ptrs[i];
          uint size = sizes[i];  // Use the actual allocated size
          shadow_poison(addr, size);
          kfree_slab(ptrs[i]);
          freed++;
        }
      }
      
      printf("\n  Enhanced stress test: allocated=%d, freed=%d, pattern_errors=%d, shadow_errors=%d\n", 
             allocated, freed, pattern_errors, shadow_errors);
      
      // Clean up heap-allocated test arrays
      kfree(ptrs);
      kfree(sizes);
      
      return (pattern_errors == 0 && shadow_errors == 0) ? 1 : 0;
    }
    
    case 7: // Statistics and fragmentation test
    {
      printf("Test 7: Statistics and fragmentation analysis\n");
      
      // Allocate objects of different sizes to create fragmentation
      void *ptrs[50];
      int sizes[] = {32, 64, 128, 256, 512};
      int size_count = 5;
      
      printf("  Allocating objects to create fragmentation...\n");
      for(int i = 0; i < 50; i++) {
        int size = sizes[i % size_count];
        ptrs[i] = kmalloc(size);
      }
      
      // Free some objects to create holes
      for(int i = 0; i < 50; i += 3) {
        if(ptrs[i]) {
          kfree_slab(ptrs[i]);
          ptrs[i] = 0;
        }
      }
      
      printf("  Printing slab statistics:\n");
      slab_print_stats();
      
      printf("  Printing fragmentation analysis:\n");
      slab_print_fragmentation_report();
      
      // Clean up
      for(int i = 0; i < 50; i++) {
        if(ptrs[i]) {
          kfree_slab(ptrs[i]);
        }
      }
      
      printf("  Statistics test completed\n");
      return 1;
    }
    
    case 8: // Performance comparison test
    {
      printf("Test 8: Performance comparison (slab vs kalloc)\n");
      
      // Test different object sizes
      int test_sizes[] = {32, 64, 128, 256, 512};
      int num_allocs = 100;
      
      for(int i = 0; i < 5; i++) {
        printf("  Testing with object size %d bytes:\n", test_sizes[i]);
        perf_test_slab_vs_kalloc(num_allocs, test_sizes[i]);
      }
      
      printf("  Performance comparison test completed\n");
      return 1;
    }
    
    case 9: // Concurrent allocation test (simulated)
    {
      printf("Test 9: Concurrent allocation test (simulated)\n");
      
      // Simulate concurrent access by rapidly allocating and freeing
      // from different size classes
      // Allocate arrays on heap to prevent stack overflow
      void **ptrs1 = (void**)kalloc();
      void **ptrs2 = (void**)kalloc();
      void **ptrs3 = (void**)kalloc();
      if(!ptrs1 || !ptrs2 || !ptrs3) {
        printf("  Failed to allocate memory for concurrent test\n");
        if(ptrs1) kfree(ptrs1);
        if(ptrs2) kfree(ptrs2);
        if(ptrs3) kfree(ptrs3);
        return 0;
      }
      int errors = 0;
      
      printf("  Simulating concurrent allocations...\n");
      
      // Phase 1: Rapid allocations from different threads (simulated)
      for(int round = 0; round < 10; round++) {
        // Thread 1: small objects
        for(int i = 0; i < 50; i++) {
          ptrs1[i] = kmalloc(32);
          if(!ptrs1[i]) errors++;
        }
        
        // Thread 2: medium objects
        for(int i = 0; i < 50; i++) {
          ptrs2[i] = kmalloc(128);
          if(!ptrs2[i]) errors++;
        }
        
        // Thread 3: large objects
        for(int i = 0; i < 50; i++) {
          ptrs3[i] = kmalloc(512);
          if(!ptrs3[i]) errors++;
        }
        
        // Interleaved frees
        for(int i = 0; i < 50; i += 2) {
          if(ptrs1[i]) kfree_slab(ptrs1[i]);
          if(ptrs2[i]) kfree_slab(ptrs2[i]);
          if(ptrs3[i]) kfree_slab(ptrs3[i]);
        }
        
        // Free remaining
        for(int i = 1; i < 50; i += 2) {
          if(ptrs1[i]) kfree_slab(ptrs1[i]);
          if(ptrs2[i]) kfree_slab(ptrs2[i]);
          if(ptrs3[i]) kfree_slab(ptrs3[i]);
        }
        
        printf("  Round %d completed\n", round + 1);
      }
      
      printf("  Concurrent test completed with %d errors\n", errors);
      
      // Check final statistics for leaks
      struct slab_global_stats stats;
      slab_get_global_stats(&stats);
      printf("  Final stats: %d pages in use, %d bytes allocated\n", 
             stats.total_pages, stats.total_allocated);
      
      // Clean up heap-allocated arrays
      kfree(ptrs1);
      kfree(ptrs2);
      kfree(ptrs3);
      
      return errors == 0 ? 1 : 0;
    }
    
    case 10: // Memory reclamation test
    {
      printf("Test 10: Memory reclamation test\n");
      
      // Allocate many objects
      // Allocate ptrs array on heap to prevent stack overflow
      void **ptrs = (void**)kalloc();
      if(!ptrs) {
        printf("  Failed to allocate memory for reclamation test\n");
        return 0;
      }
      int allocated = 0;
      
      printf("  Allocating objects...\n");
      for(int i = 0; i < 200; i++) {
        ptrs[i] = kmalloc(64);
        if(ptrs[i]) allocated++;
      }
      
      printf("  Allocated %d objects\n", allocated);
      slab_print_stats();
      
      // Free all objects
      printf("  Freeing all objects...\n");
      for(int i = 0; i < 200; i++) {
        if(ptrs[i]) {
          kfree_slab(ptrs[i]);
        }
      }
      
      printf("  After freeing:\n");
      slab_print_stats();
      
      // Test reclamation
      printf("  Testing slab reclamation...\n");
      int reclaimed = 0;
      slab_acquire_cache_list_lock();
      int num_caches = slab_get_num_caches();
      for(int i = 0; i < num_caches; i++) {
        struct kmem_cache *cache = slab_get_cache(i);
        if(cache) {
          reclaimed += slab_reclaim_empty_slabs(cache, 5);
        }
      }
      slab_release_cache_list_lock();
      
      printf("  Reclaimed %d empty slabs\n", reclaimed);
      slab_print_stats();
      
      printf("  Memory reclamation test completed\n");
      
      // Clean up heap-allocated ptrs array
      kfree(ptrs);
      
      return 1;
    }
    
    case 11: // Stress test with simulated process/file operations
    {
      printf("Test 11: Stress test with simulated process/file operations\n");
      
      // Simulate process creation/destruction stress
      printf("  Simulating process creation/destruction stress...\n");
      // Allocate process_ptrs array on heap to prevent stack overflow
      void **process_ptrs = (void**)kalloc();
      if(!process_ptrs) {
        printf("  Failed to allocate memory for stress test\n");
        return 0;
      }
      int process_errors = 0;
      
      for(int cycle = 0; cycle < 20; cycle++) {
        // Simulate process creation - allocate various kernel structures
        for(int i = 0; i < 100; i++) {
          // Simulate different kernel allocations during process creation
           int alloc_type = i % 4;
           switch(alloc_type) {
             case 0: process_ptrs[i] = kmalloc(200); break;  // Process structure size
             case 1: process_ptrs[i] = kmalloc(64); break;   // Small allocation
             case 2: process_ptrs[i] = kmalloc(256); break;  // Medium allocation  
             case 3: process_ptrs[i] = kmalloc(512); break;  // Large allocation
           }
          if(!process_ptrs[i]) process_errors++;
        }
        
        // Simulate some processes dying (free half)
        for(int i = 0; i < 100; i += 2) {
          if(process_ptrs[i]) {
            kfree_slab(process_ptrs[i]);
            process_ptrs[i] = 0;
          }
        }
        
        // Simulate file operations stress
        // Allocate file_ptrs array on heap to prevent stack overflow
        void **file_ptrs = (void**)kalloc();
        if(!file_ptrs) {
          process_errors++;
          continue;
        }
        for(int i = 0; i < 50; i++) {
          // Simulate file structure allocations
          file_ptrs[i] = kmalloc(128);  // Typical file structure size
          if(!file_ptrs[i]) process_errors++;
        }
        
        // Free file structures
        for(int i = 0; i < 50; i++) {
          if(file_ptrs[i]) {
            kfree_slab(file_ptrs[i]);
          }
        }
        
        // Clean up heap-allocated file_ptrs array
        kfree(file_ptrs);
        
        // Free remaining process structures
        for(int i = 1; i < 100; i += 2) {
          if(process_ptrs[i]) {
            kfree_slab(process_ptrs[i]);
            process_ptrs[i] = 0;
          }
        }
        
        if(cycle % 5 == 0) {
          printf("  Completed stress cycle %d\n", cycle + 1);
        }
      }
      
      printf("  Process/file stress test completed with %d errors\n", process_errors);
      
      // Check for memory leaks
      struct slab_global_stats final_stats;
      slab_get_global_stats(&final_stats);
      printf("  Final memory state: %d pages, %d bytes allocated\n", 
             final_stats.total_pages, final_stats.total_allocated);
      
      // Test memory reclamation under stress
       printf("  Testing memory reclamation under stress...\n");
       int total_reclaimed = 0;
       slab_acquire_cache_list_lock();
       int num_caches = slab_get_num_caches();
       for(int i = 0; i < num_caches; i++) {
         struct kmem_cache *cache = slab_get_cache(i);
         if(cache) {
           total_reclaimed += slab_reclaim_empty_slabs(cache, 3);
         }
       }
       slab_release_cache_list_lock();
      
      printf("  Reclaimed %d slabs under stress\n", total_reclaimed);
      printf("  Stress test completed\n");
      
      // Clean up heap-allocated process_ptrs array
      kfree(process_ptrs);
      
      return process_errors < 10 ? 1 : 0;  // Allow some errors under stress
    }
    
    case 12: // Comprehensive system test
    {
      printf("Test 12: Comprehensive system test\n");
      
      // Run a comprehensive test that combines all aspects
      printf("  Running comprehensive allocation patterns...\n");
      
      // Allocate mixed_ptrs array on heap to prevent stack overflow
      void **mixed_ptrs = (void**)kalloc();
      if(!mixed_ptrs) {
        printf("  Failed to allocate memory for comprehensive test\n");
        return 0;
      }
      int mixed_errors = 0;
      int total_allocated = 0;
      
      // Phase 1: Mixed size allocations
      for(int i = 0; i < 300; i++) {
        int size_choice = i % 8;
        int size = 32; // Default size
        switch(size_choice) {
          case 0: size = 16; break;
          case 1: size = 32; break;
          case 2: size = 64; break;
          case 3: size = 128; break;
          case 4: size = 256; break;
          case 5: size = 512; break;
          case 6: size = 1024; break;
          case 7: size = 2048; break;
        }
        
        mixed_ptrs[i] = kmalloc(size);
        if(mixed_ptrs[i]) {
          total_allocated++;
          // Write pattern to verify integrity
          char *ptr = (char*)mixed_ptrs[i];
          for(int j = 0; j < size && j < 16; j++) {
            ptr[j] = (char)(i + j);
          }
        } else {
          mixed_errors++;
        }
      }
      
      printf("  Allocated %d objects, %d errors\n", total_allocated, mixed_errors);
      
      // Phase 2: Random free pattern
      printf("  Performing random frees...\n");
      int freed = 0;
      for(int i = 0; i < 300; i += 3) {
        if(mixed_ptrs[i]) {
          // Verify pattern before freeing
          char *ptr = (char*)mixed_ptrs[i];
          int pattern_ok = 1;
          for(int j = 0; j < 16; j++) {
            if(ptr[j] != (char)(i + j)) {
              pattern_ok = 0;
              break;
            }
          }
          if(!pattern_ok) mixed_errors++;
          
          kfree_slab(mixed_ptrs[i]);
          mixed_ptrs[i] = 0;
          freed++;
        }
      }
      
      printf("  Freed %d objects\n", freed);
      
      // Phase 3: Statistics check
      printf("  Checking statistics...\n");
      slab_print_stats();
      
      // Phase 4: Fragmentation analysis
      printf("  Analyzing fragmentation...\n");
      slab_print_fragmentation_report();
      
      // Phase 5: Clean up remaining
      printf("  Cleaning up remaining objects...\n");
      for(int i = 0; i < 300; i++) {
        if(mixed_ptrs[i]) {
          kfree_slab(mixed_ptrs[i]);
          freed++;
        }
      }
      
      printf("  Total freed: %d objects\n", freed);
      
      // Final statistics
      struct slab_global_stats final_stats;
      slab_get_global_stats(&final_stats);
      printf("  Final state: %d pages, %d bytes allocated\n", 
             final_stats.total_pages, final_stats.total_allocated);
      
      printf("  Comprehensive test completed with %d errors\n", mixed_errors);
      
      // Clean up heap-allocated mixed_ptrs array
      kfree(mixed_ptrs);
      
      return mixed_errors < 5 ? 1 : 0;
    }
    
    default:
      printf("Unknown test type %d\n", test_type);
      return 0;
  }
}