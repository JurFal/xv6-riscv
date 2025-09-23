//
// Slab allocator performance test
// Tests memory allocation performance and compares different allocation patterns
//

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAX_ALLOCS 50
#define TEST_ITERATIONS 5
#define SMALL_SIZE 64
#define MEDIUM_SIZE 256
#define LARGE_SIZE 1024

// Test results structure
struct test_result {
  char name[32];
  uint start_time;
  uint end_time;
  uint duration;
  int allocations;
  int failures;
};

// Get current time (using uptime system call)
uint get_time() {
  return uptime();
}

// Print test results
void print_result(struct test_result *result) {
  printf("Test: %s\n", result->name);
  printf("  Duration: %d ticks\n", (int)result->duration);
  printf("  Allocations: %d\n", result->allocations);
  printf("  Failures: %d\n", result->failures);
  if (result->allocations > 0) {
    printf("  Avg time per alloc: %d ticks\n", 
           (int)(result->duration / result->allocations));
  }
  printf("  Success rate: %d%%\n", 
         result->allocations > 0 ? 
         (result->allocations - result->failures) * 100 / result->allocations : 0);
  printf("\n");
}

// Test 1: Sequential allocation and deallocation
void test_sequential_alloc(struct test_result *result, int size) {
  void *ptrs[MAX_ALLOCS];
  int i;
  
  strcpy(result->name, "Sequential Alloc/Free");
  result->allocations = 0;
  result->failures = 0;
  
  result->start_time = get_time();
  
  // Allocate
  for (i = 0; i < MAX_ALLOCS; i++) {
    ptrs[i] = malloc(size);
    if (ptrs[i] == 0) {
      result->failures++;
      break;
    }
    result->allocations++;
    
    // Write to memory to ensure it's actually allocated
    memset(ptrs[i], i & 0xFF, size);
  }
  
  // Deallocate
  for (int j = 0; j < i; j++) {
    if (ptrs[j] != 0) {
      free(ptrs[j]);
    }
  }
  
  result->end_time = get_time();
  result->duration = result->end_time - result->start_time;
}

// Test 2: Random allocation and deallocation pattern
void test_random_alloc(struct test_result *result, int size) {
  void *ptrs[MAX_ALLOCS];
  int allocated[MAX_ALLOCS];
  int i, idx;
  
  strcpy(result->name, "Random Alloc/Free");
  result->allocations = 0;
  result->failures = 0;
  
  // Initialize arrays
  for (i = 0; i < MAX_ALLOCS; i++) {
    ptrs[i] = 0;
    allocated[i] = 0;
  }
  
  result->start_time = get_time();
  
  // Perform random allocations and deallocations
  for (i = 0; i < MAX_ALLOCS * 2; i++) {
    idx = (i * 17 + 23) % MAX_ALLOCS; // Simple pseudo-random
    
    if (allocated[idx] == 0) {
      // Allocate
      ptrs[idx] = malloc(size);
      if (ptrs[idx] != 0) {
        allocated[idx] = 1;
        result->allocations++;
        memset(ptrs[idx], idx & 0xFF, size);
      } else {
        result->failures++;
      }
    } else {
      // Deallocate
      if (ptrs[idx] != 0) {
        free(ptrs[idx]);
        ptrs[idx] = 0;
        allocated[idx] = 0;
      }
    }
  }
  
  // Clean up remaining allocations
  for (i = 0; i < MAX_ALLOCS; i++) {
    if (ptrs[i] != 0) {
      free(ptrs[i]);
    }
  }
  
  result->end_time = get_time();
  result->duration = result->end_time - result->start_time;
}

// Test 3: Stress test with multiple processes
void test_concurrent_alloc(struct test_result *result, int size) {
  int pid;
  int num_children = 4;
  int child_allocs = MAX_ALLOCS / num_children;
  
  strcpy(result->name, "Concurrent Alloc/Free");
  result->allocations = 0;
  result->failures = 0;
  
  result->start_time = get_time();
  
  // Fork multiple processes
  for (int i = 0; i < num_children; i++) {
    pid = fork();
    if (pid == 0) {
      // Child process
      void *ptrs[child_allocs];
      int j;
      
      for (j = 0; j < child_allocs; j++) {
        ptrs[j] = malloc(size);
        if (ptrs[j] == 0) {
          exit(1); // Signal failure
        }
        memset(ptrs[j], (i + j) & 0xFF, size);
      }
      
      // Free all allocations
      for (j = 0; j < child_allocs; j++) {
        if (ptrs[j] != 0) {
          free(ptrs[j]);
        }
      }
      
      exit(0); // Success
    } else if (pid < 0) {
      result->failures++;
    }
  }
  
  // Wait for all children and count results
  for (int i = 0; i < num_children; i++) {
    int status;
    wait(&status);
    if (status == 0) {
      result->allocations += child_allocs;
    } else {
      result->failures += child_allocs;
    }
  }
  
  result->end_time = get_time();
  result->duration = result->end_time - result->start_time;
}

// Test 4: Mixed size allocation test
void test_mixed_sizes(struct test_result *result) {
  void *ptrs[MAX_ALLOCS];
  int sizes[] = {16, 32, 64, 128, 256, 512, 1024};
  int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
  int i;
  
  strcpy(result->name, "Mixed Size Alloc/Free");
  result->allocations = 0;
  result->failures = 0;
  
  result->start_time = get_time();
  
  // Allocate with different sizes
  for (i = 0; i < MAX_ALLOCS; i++) {
    int size = sizes[i % num_sizes];
    ptrs[i] = malloc(size);
    if (ptrs[i] == 0) {
      result->failures++;
      break;
    }
    result->allocations++;
    memset(ptrs[i], i & 0xFF, size);
  }
  
  // Free in reverse order
  for (int j = i - 1; j >= 0; j--) {
    if (ptrs[j] != 0) {
      free(ptrs[j]);
    }
  }
  
  result->end_time = get_time();
  result->duration = result->end_time - result->start_time;
}

// Test 5: Fragmentation test
void test_fragmentation(struct test_result *result) {
  void *ptrs[MAX_ALLOCS];
  int i;
  
  strcpy(result->name, "Fragmentation Test");
  result->allocations = 0;
  result->failures = 0;
  
  result->start_time = get_time();
  
  // Allocate many small blocks
  for (i = 0; i < MAX_ALLOCS; i++) {
    ptrs[i] = malloc(SMALL_SIZE);
    if (ptrs[i] == 0) {
      result->failures++;
      break;
    }
    result->allocations++;
    memset(ptrs[i], i & 0xFF, SMALL_SIZE);
  }
  
  // Free every other block to create fragmentation
  for (int j = 1; j < i; j += 2) {
    if (ptrs[j] != 0) {
      free(ptrs[j]);
      ptrs[j] = 0;
    }
  }
  
  // Try to allocate larger blocks in the gaps
  for (int j = 1; j < i; j += 2) {
    ptrs[j] = malloc(MEDIUM_SIZE);
    if (ptrs[j] != 0) {
      memset(ptrs[j], j & 0xFF, MEDIUM_SIZE);
    } else {
      result->failures++;
    }
  }
  
  // Clean up
  for (int j = 0; j < i; j++) {
    if (ptrs[j] != 0) {
      free(ptrs[j]);
    }
  }
  
  result->end_time = get_time();
  result->duration = result->end_time - result->start_time;
}

// Run all tests
void run_all_tests() {
  struct test_result results[6];
  int test_count = 0;
  
  printf("=== Memory Allocator Performance Test ===\n");
  printf("Testing with %d allocations per test\n\n", MAX_ALLOCS);
  
  // Test different allocation patterns
  printf("--- Small Object Tests (64 bytes) ---\n");
  test_sequential_alloc(&results[test_count++], SMALL_SIZE);
  print_result(&results[test_count - 1]);
  
  test_random_alloc(&results[test_count++], SMALL_SIZE);
  print_result(&results[test_count - 1]);
  
  printf("--- Medium Object Tests (256 bytes) ---\n");
  test_sequential_alloc(&results[test_count++], MEDIUM_SIZE);
  print_result(&results[test_count - 1]);
  
  printf("--- Concurrent Test ---\n");
  test_concurrent_alloc(&results[test_count++], SMALL_SIZE);
  print_result(&results[test_count - 1]);
  
  printf("--- Mixed Size Test ---\n");
  test_mixed_sizes(&results[test_count++]);
  print_result(&results[test_count - 1]);
  
  printf("--- Fragmentation Test ---\n");
  test_fragmentation(&results[test_count++]);
  print_result(&results[test_count - 1]);
  
  // Calculate and print summary
  uint total_time = 0;
  int total_allocs = 0;
  int total_failures = 0;
  
  for (int i = 0; i < test_count; i++) {
    total_time += results[i].duration;
    total_allocs += results[i].allocations;
    total_failures += results[i].failures;
  }
  
  printf("=== Summary ===\n");
  printf("Total time: %d ticks\n", (int)total_time);
  printf("Total allocations: %d\n", total_allocs);
  printf("Total failures: %d\n", total_failures);
  printf("Overall success rate: %d%%\n", 
         total_allocs > 0 ? (total_allocs - total_failures) * 100 / total_allocs : 0);
  printf("Average time per allocation: %d ticks\n", 
         total_allocs > 0 ? (int)(total_time / total_allocs) : 0);
}

int main(int argc, char *argv[]) {
  printf("Starting memory allocator performance test...\n");
  
  if (argc > 1 && strcmp(argv[1], "quick") == 0) {
    printf("Running quick test...\n");
    struct test_result result;
    test_sequential_alloc(&result, SMALL_SIZE);
    print_result(&result);
  } else {
    run_all_tests();
  }
  
  printf("Test completed.\n");
  exit(0);
}