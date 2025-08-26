#include <tests/irq_tests.h>
#include <uart.h>

// Global test statistics
static int total_tests_run = 0;
static int total_tests_passed = 0;
static int total_tests_failed = 0;

// Run all IRQ subsystem tests
void run_all_irq_tests(void) {
    uart_puts("\n");
    uart_puts("================================================================\n");
    uart_puts("            IRQ SUBSYSTEM TESTS\n");
    uart_puts("================================================================\n");
    
    // Basic IRQ tests
    uart_puts("\n>>> Testing Basic IRQ Functionality...\n");
    run_irq_basic_tests();
    
    // GIC tests (ARM64 only)
#ifdef __aarch64__
    uart_puts("\n>>> Testing ARM GIC...\n");
    run_arm_gic_comprehensive_tests();
    
    // GICv3-specific tests
    // uart_puts("\n>>> Testing GICv3-specific features...\n");
    // test_arm_gic_v3();
#endif
    
    // Comment out less relevant tests for GIC validation
    // Uncomment these if testing the full IRQ subsystem
    /*
    // IRQ descriptor tests
    uart_puts("\n>>> Testing IRQ Descriptors...\n");
    run_irq_descriptor_tests();
    
    // IRQ allocator tests
    uart_puts("\n>>> Testing IRQ Allocator...\n");
    run_irq_allocator_tests();
    
    // Hierarchical domain tests
    uart_puts("\n>>> Testing Hierarchical Domain Support...\n");
    test_hierarchical_domains();
    
    // Radix tree tests (for sparse domains)
    uart_puts("\n>>> Testing Radix Tree (for sparse domains)...\n");
    test_radix_tree_all();
    
    // Tree domain tests
    uart_puts("\n>>> Testing Tree Domain Support...\n");
    test_tree_domains_comprehensive();
    */
    
    uart_puts("\n");
    uart_puts("================================================================\n");
    uart_puts("                    ALL TESTS COMPLETE\n");
    uart_puts("================================================================\n");
}
