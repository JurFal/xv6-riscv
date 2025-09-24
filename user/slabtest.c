#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    printf("Slab Allocator Test Suite\n");
    printf("=========================\n\n");
    
    int total_tests = 0;
    int passed_tests = 0;
    
    // Run kernel-space slab tests
    for (int test_type = 1; test_type <= 4; test_type++) {
        total_tests++;
        printf("Running test %d...\n", test_type);
        
        int result = (int)(uint64)slab_alloc(test_type); // Now this calls run_slab_test in kernel
        if (result > 0) {
            passed_tests++;
            printf("Test %d: PASSED\n\n", test_type);
        } else {
            printf("Test %d: FAILED\n\n", test_type);
        }
    }
    
    printf("Test Results: %d/%d tests passed\n", passed_tests, total_tests);
    
    if (passed_tests == total_tests) {
        printf("All tests PASSED!\n");
        exit(0);
    } else {
        printf("Some tests FAILED!\n");
        exit(1);
    }
}