#!/bin/bash

# Slab Allocator Test Runner for xv6
# This script helps run various slab allocator tests

echo "=== xv6 Slab Allocator Test Runner ==="
echo

# Check if we're in the right directory
if [ ! -f "Makefile" ] || [ ! -d "kernel" ]; then
    echo "Error: Please run this script from the xv6-riscv root directory"
    exit 1
fi

# Function to run a specific test
run_test() {
    local test_num=$1
    local test_name=$2
    
    echo "Running Test $test_num: $test_name"
    echo "----------------------------------------"
    
    # Build xv6 with the test
    make clean > /dev/null 2>&1
    if ! make qemu-gdb CPUS=1 TEST_TYPE=$test_num > test_output_$test_num.log 2>&1 &
    then
        echo "Failed to build/run xv6 for test $test_num"
        return 1
    fi
    
    local qemu_pid=$!
    
    # Wait a bit for the test to run
    sleep 5
    
    # Kill qemu
    kill $qemu_pid 2>/dev/null
    wait $qemu_pid 2>/dev/null
    
    # Show results
    if [ -f "test_output_$test_num.log" ]; then
        echo "Test output:"
        tail -20 "test_output_$test_num.log"
    fi
    
    echo
}

# Function to run all tests
run_all_tests() {
    echo "Running all slab allocator tests..."
    echo
    
    # Basic tests
    run_test 1 "Basic Allocation Test"
    run_test 2 "Size Classes Test"
    run_test 3 "Stress Test"
    run_test 4 "Edge Cases Test"
    run_test 6 "Enhanced Stress Test"
    
    # New comprehensive tests
    run_test 7 "Statistics and Fragmentation Analysis"
    run_test 8 "Performance Comparison (Slab vs Kalloc)"
    run_test 9 "Concurrent Allocation Test"
    run_test 10 "Memory Reclamation Test"
    run_test 11 "Process/File Operations Stress Test"
    run_test 12 "Comprehensive System Test"
    
    echo "All tests completed. Check individual log files for detailed results."
}

# Function to show test menu
show_menu() {
    echo "Available tests:"
    echo "  1  - Basic Allocation Test"
    echo "  2  - Size Classes Test"
    echo "  3  - Stress Test"
    echo "  4  - Edge Cases Test"
    echo "  6  - Enhanced Stress Test"
    echo "  7  - Statistics and Fragmentation Analysis"
    echo "  8  - Performance Comparison (Slab vs Kalloc)"
    echo "  9  - Concurrent Allocation Test"
    echo "  10 - Memory Reclamation Test"
    echo "  11 - Process/File Operations Stress Test"
    echo "  12 - Comprehensive System Test"
    echo "  all - Run all tests"
    echo "  clean - Clean up test output files"
    echo "  quit - Exit"
    echo
}

# Function to clean up
cleanup() {
    echo "Cleaning up test output files..."
    rm -f test_output_*.log
    echo "Cleanup completed."
}

# Main script logic
if [ $# -eq 0 ]; then
    # Interactive mode
    while true; do
        show_menu
        read -p "Select test to run: " choice
        
        case $choice in
            [1-4]|[6-9]|1[0-2])
                case $choice in
                    1) run_test 1 "Basic Allocation Test" ;;
                    2) run_test 2 "Size Classes Test" ;;
                    3) run_test 3 "Stress Test" ;;
                    4) run_test 4 "Edge Cases Test" ;;
                    6) run_test 6 "Enhanced Stress Test" ;;
                    7) run_test 7 "Statistics and Fragmentation Analysis" ;;
                    8) run_test 8 "Performance Comparison (Slab vs Kalloc)" ;;
                    9) run_test 9 "Concurrent Allocation Test" ;;
                    10) run_test 10 "Memory Reclamation Test" ;;
                    11) run_test 11 "Process/File Operations Stress Test" ;;
                    12) run_test 12 "Comprehensive System Test" ;;
                esac
                ;;
            "all")
                run_all_tests
                ;;
            "clean")
                cleanup
                ;;
            "quit"|"q"|"exit")
                echo "Goodbye!"
                break
                ;;
            *)
                echo "Invalid choice. Please try again."
                ;;
        esac
    done
else
    # Command line mode
    case $1 in
        "all")
            run_all_tests
            ;;
        "clean")
            cleanup
            ;;
        [1-4]|[6-9]|1[0-2])
            run_test $1 "Test $1"
            ;;
        *)
            echo "Usage: $0 [test_number|all|clean]"
            echo "Run without arguments for interactive mode"
            exit 1
            ;;
    esac
fi