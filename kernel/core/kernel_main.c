/*
 * kernel/core/kernel_main.c
 *
 * Kernel initialization (post arch init) 
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <uart.h>
#include <arch_interface.h>
#include <panic.h>
#include <memory/pmm.h>
#include <memory/memmap.h>
#include <exceptions/exceptions.h>
#include <drivers/fdt.h>
#include <drivers/fdt_mgr.h>
#include <memory/vmm.h>
#include <memory/devmap.h>
#include <device/device.h>
#include <device/device_tree.h>
#include <drivers/driver.h>
#include <drivers/uart_drivers.h>
#include <irqchip/irqchip.h>
// #include <tests/mmu_tests.h>
// #include <tests/pmm_tests.h>
// #include <tests/memory_tests.h>
// #include <tests/fdt_tests.h>
// #include <tests/fdt_mgr_tests.h>
// #include <tests/test_dmap.h>
#include <tests/slab_tests.h>
#include <tests/slab_edge_tests.h>
#include <tests/slab_destruction_tests.h>
#include <tests/kmalloc_tests.h>
#include <tests/malloc_types_tests.h>
#include <tests/slab_lookup_tests.h>
#include <tests/page_alloc_tests.h>
#include <tests/page_alloc_stress.h>
#include <tests/irq_tests.h>

// External symbols from linker script
extern char __kernel_start;
extern char _kernel_end;

void kernel_main(void* dtb) {

    // Initialize FDT Manager (just preserves pointer) 
    if (!fdt_mgr_init(dtb)) {
        // Cannot output warning - UART not available yet
    }
    
    // Initialize memory subsystems 
    memmap_init();
    
    // Parse Device Tree to get memory information using FDT manager
    memory_info_t mem_info;
    if (!fdt_mgr_get_memory_info(&mem_info)) {
        panic("Failed to parse memory information from FDT");
    }
    
    // Initialize PMM
    pmm_init((uint64_t)&_kernel_end, (struct memory_info *)&mem_info);
    
    // Reserve FDT pages in PMM before any allocations
    // Keep this separate from pmm_init for clean separation of concerns
    if (!fdt_mgr_reserve_pages()) {
        panic("Failed to reserve FDT pages in PMM");
    }
    
    // Initialize VMM
    vmm_init();
    
    // Create DMAP region for all physical memory
    // This must be done before device mappings so PMM can use DMAP for page clearing
    vmm_create_dmap(&mem_info);
    
    // Map FDT to permanent virtual address
    if (!fdt_mgr_map_virtual()) {
        // No UART output for this panic 
        panic("Failed to map FDT to virtual memory");
    }
    
    // Initialize device subsystem (pool, tree parser, enumeration)
    int device_count = device_init(fdt_mgr_get_blob());
    if (device_count < 0) {
        // No UART output for this panic 
        panic("Failed to initialize device subsystem");
    }
    
    // Initialize Device Mapping system
    // Now devmap_init can use the discovered devices
    devmap_init();
    
    // Initialize driver subsystem
    driver_init();
    
    // Initialize UART (registers drivers)
    uart_init();
    
    // Auto-select console UART from FDT
    uart_console_auto_select(fdt_mgr_get_blob());
   
    // KERNEL LOGS START HERE! 
    uart_puts("\n=======================================\n");
    uart_puts("ProtoKernel Booting...\n");
    uart_puts("=======================================\n\n");
    
    uart_puts("Kernel entry point: kernel_main()\n");
    uart_puts("Device Tree Blob at: ");
    uart_puthex((uint64_t)dtb);
    uart_puts(" (");
    if (dtb) {
        uint32_t *magic_ptr = (uint32_t *)dtb;
        uart_puts("magic=");
        uart_puthex(*magic_ptr);
    } else {
        uart_puts("NULL");
    }
    uart_puts(")\n");
    
#ifdef __riscv
    uart_puts("\nCurrent Privilege Mode: S-mode (Supervisor)\n");
#elif defined(__aarch64__)
    uart_puts("\nCurrent Exception Level: EL");
    int level = arch_get_exception_level();
    uart_putc('0' + level);
    uart_puts("\n");
#endif
       
    uart_puts("\nSystem Information:\n");
    
    uart_puts("- Kernel loaded at: ");
    uart_puthex((uint64_t)&__kernel_start);
    uart_puts("\n");
    
    uart_puts("- Kernel ends at: ");
    uart_puthex((uint64_t)&_kernel_end);
    uart_puts("\n");
    
    // Report memory information that was parsed earlier
    uart_puts("\nMemory Configuration:\n");
    fdt_print_memory_info(&mem_info);
    
    // Report FDT manager state
    fdt_mgr_print_info();
    
    // Initialize exception handling (architecture-agnostic)
    uart_puts("\nInitializing exception handling...\n");
    exception_init();
    
    // Initialize interrupt controller drivers (after UART so we get output)
    uart_puts("\nInitializing interrupt controllers...\n");
    irqchip_init();
    
    // Print device mappings
    // devmap_print_mappings();
    
    // Print the device tree
    device_print_tree(NULL, 0);
    
    // Print driver registry
    driver_print_registry();
    
    // Print memory statistics
    pmm_print_stats();
    
    // Print device pool statistics
    // extern void device_pool_print_stats(void);
    // device_pool_print_stats();
   
    // KERNEL TESTS START HERE!
    
    
    // Page allocator tests first (lowest level)
    // page_alloc_run_tests();
    
    // Slab allocator tests (built on page allocator)
    // run_slab_tests();
    
    // run_slab_edge_tests();
    
    // run_slab_destruction_tests();
    
    // run_slab_lookup_tests();
    
    // kmalloc tests (built on slab allocator)
    // run_kmalloc_tests();
    
    // Malloc types tests
    // run_malloc_types_tests();
    
    // Stress tests last (most intensive)
    // page_alloc_stress_tests();  
    
    // Run all IRQ subsystem tests
    run_all_irq_tests();

    uart_puts("\nKernel initialization complete!\n");
    uart_puts("System halted.\n");
    
    while (1) {
        arch_cpu_wait();
    }
}
