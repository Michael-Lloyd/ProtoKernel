/*
 * kernel/drivers/irqchip/riscv-aplic-msi.c
 * 
 * RISC-V APLIC MSI Mode Implementation
 */

#ifdef __riscv

#include <irqchip/riscv-aplic.h>
#include <irq/irq.h>
#include <uart.h>
#include <stddef.h>

// Initialize APLIC in MSI mode
int aplic_msi_init(struct aplic_data *aplic) {
    uint32_t i;
    
    uart_puts("APLIC-MSI: Initializing MSI mode\n");
    
    // 1. Configure MSI address registers (mmsiaddrcfg, mmsiaddrcfgh)
    // These registers determine the target address for generated MSIs.
    // This address should point to the IMSIC.
    // TODO: Get the real MSI address from the IMSIC driver.
    uint64_t msi_addr = 0; // Placeholder for IMSIC address
    
    aplic_write(aplic, APLIC_MMSIADDRCFG, (uint32_t)msi_addr);
    aplic_write(aplic, APLIC_MMSIADDRCFGH, (uint32_t)(msi_addr >> 32));
    
    // 2. Configure all sources for MSI mode
    // In MSI mode, the target register format is different:
    // Bits [10:0]: External Interrupt ID (EIID)
    for (i = 1; i <= aplic->nr_sources; i++) {
        // Map the hardware IRQ to an EIID.
        // For simplicity, we can use a direct mapping for now.
        // TODO: Coordinate with IMSIC for EIID allocation.
        uint32_t eiid = i;
        uint32_t target = (eiid << APLIC_TARGET_EIID_SHIFT) & (APLIC_TARGET_EIID_MASK << APLIC_TARGET_EIID_SHIFT);
        aplic_write(aplic, aplic_target_offset(i), target);
    }
    
    // 3. Configure sourcecfg for each interrupt source
    // This is already done in aplic_init_hw_global, but we might need
    // to re-configure it here if MSI mode requires different settings.
    // For now, we assume the default (inactive) is fine.
    
    uart_puts("APLIC-MSI: MSI mode initialization complete\n");
    
    return 0;
}

#endif // __riscv
