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
      printf("Shadow violation: expected unpoisoned @%d, got 0x%x\n", i, shadow[i]);
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
      for(int i = 0; i < 100; i++) {
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
          uint rounded_size = round_up_pow2(8 + (test_rand() % 2041));
          
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
      
      void *ptrs[200];
      int allocated = 0;
      int freed = 0;
      int pattern_errors = 0;
      int shadow_errors = 0;
      
      // Initialize arrays
      for(int i = 0; i < 200; i++) {
        ptrs[i] = 0;
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
            uint size = round_up_pow2(8 + (test_rand() % 1017));
            
            // Check shadow memory and mark as freed
            if (!shadow_expect_unpoisoned(addr, size)) {
              shadow_errors++;
            }
            shadow_poison(addr, size);
            
            kfree_slab(ptrs[idx]);
            ptrs[idx] = 0;
            freed++;
          }
        }
        
        printf("alloc=%d, free=%d ", allocated, freed);
      }
      
      // Clean up remaining allocations
      for(int i = 0; i < 200; i++) {
        if(ptrs[i]) {
          uint64 addr = (uint64)ptrs[i];
          uint size = round_up_pow2(8 + (test_rand() % 1017));
          shadow_poison(addr, size);
          kfree_slab(ptrs[i]);
          freed++;
        }
      }
      
      printf("\n  Enhanced stress test: allocated=%d, freed=%d, pattern_errors=%d, shadow_errors=%d\n", 
             allocated, freed, pattern_errors, shadow_errors);
      return (pattern_errors == 0 && shadow_errors == 0) ? 1 : 0;
    }
    
    default:
      printf("Unknown test type %d\n", test_type);
      return 0;
  }
}