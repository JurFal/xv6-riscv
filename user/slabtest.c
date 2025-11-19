#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    printf("Slab Allocator Test Suite\n");
    printf("=========================\n\n");
    
    int total_tests = 0;
    int passed_tests = 0;
    
    // Run kernel-space slab tests
    int max_tests = 12;  // Now we have 12 tests
    if (argc > 1) {
        // If argument provided, run specific test
        int specific_test = atoi(argv[1]);
        if (specific_test >= 1 && specific_test <= max_tests) {
            total_tests = 1;
            printf("Running specific test %d...\n", specific_test);
            int result = (int)(uint64)slab_alloc(specific_test);
            if (result > 0) {
                passed_tests++;
                printf("Test %d: PASSED\n\n", specific_test);
            } else {
                printf("Test %d: FAILED\n\n", specific_test);
            }
        } else {
            printf("Invalid test number. Valid range: 1-%d\n", max_tests);
            exit(1);
        }
    } else {
        // Run all tests
        for (int test_type = 1; test_type <= max_tests; test_type++) {
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