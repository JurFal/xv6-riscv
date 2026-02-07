#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define ITERATIONS 20

void work(int n) {
    volatile int x = 0;
    for(int i=0; i<n; i++) {
        x += i * x + 1;
    }
}

void run_test(int size, char *name) {
    printf("--- Testing %s (Size: %d bytes) ---\n", name, size);
    
    char *src = malloc(size);
    char *dst = malloc(size);
    
    if(!src || !dst) {
        printf("malloc failed for size %d\n", size);
        if(src) free(src);
        if(dst) free(dst);
        return;
    }
    
    // Initialize src
    for(int i=0; i<size; i++) src[i] = (char)(i & 0xff);
    
    // 1. Sync Copy Benchmark
    int start = uptime();
    for(int i=0; i<ITERATIONS; i++) {
        memcpy(dst, src, size);
        work(1000000); // Simulate work
    }
    int end = uptime();
    int sync_time = end - start;
    printf("Sync copy time: %d ticks\n", sync_time);
    
    // Verify sync copy
    for(int i=0; i<size; i++) {
        if(dst[i] != src[i]) {
            printf("Sync copy failed at %d\n", i);
            break;
        }
    }
    
    // Clear dst
    memset(dst, 0, size);
    
    // 2. Async Copy Benchmark
    start = uptime();
    for(int i=0; i<ITERATIONS; i++) {
        // Retry submission until success
        while(amemcpy(dst, src, size) < 0) {
            // Queue full, sleep a bit
            pause(1);
        }
        
        work(1000000); // Overlap work
        
        csync(dst, size); // Wait for completion
    }
    end = uptime();
    int async_time = end - start;
    printf("Async copy time: %d ticks\n", async_time);
    
    // Verify async copy
    int failed = 0;
    for(int i=0; i<size; i++) {
        if(dst[i] != src[i]) {
            printf("Async copy failed at %d: expected %d, got %d\n", i, (char)(i & 0xff), dst[i]);
            failed = 1;
            break;
        }
    }
    if (!failed) {
        printf("Async copy verification: PASSED\n");
    }

    if (async_time < sync_time) {
        printf("RESULT: Async FASTER (diff: %d ticks)\n", sync_time - async_time);
    } else {
        printf("RESULT: Async SLOWER/EQUAL (diff: %d ticks)\n", async_time - sync_time);
    }
    printf("\n");

    free(src);
    free(dst);
}

int main(int argc, char *argv[]) {
    printf("Starting Multi-Size Async Copy Benchmark...\n");
    
    // 4KB
    run_test(4096, "4KB");
    
    // 64KB
    run_test(64 * 1024, "64KB");
    
    // 256KB
    run_test(256 * 1024, "256KB");
    
    // 512KB
    run_test(512 * 1024, "512KB");

    // 1MB
    run_test(1024 * 1024, "1MB");

    // 2MB
    run_test(2 * 1024 * 1024, "2MB");

    // 4MB
    run_test(4 * 1024 * 1024, "4MB");

    // 8MB
    run_test(8 * 1024 * 1024, "8MB");

    printf("Benchmark suite completed.\n");
    shutdown();
    exit(0);
}
