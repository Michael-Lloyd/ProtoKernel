/*
 * kernel/drivers/irqchip/riscv-aplic.c
 * 
 * RISC-V Advanced Platform-Level Interrupt Controller (APLIC) driver
 */

// Only compile this driver for RISC-V architecture
#ifdef __riscv

#include <irqchip/riscv-aplic.h>
#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <device/device.h>
#include <device/resource.h>
#include <drivers/driver.h>
#include <drivers/driver_module.h>
#include <uart.h>
#include <arch_io.h>
#include <stddef.h>
#include <string.h>

// Global APLIC instance
struct aplic_data *aplic_primary = NULL;

// Static APLIC data for the primary controller
static struct aplic_data primary_aplic_data;

// Forward declarations for irq_chip operations
static void aplic_irq_mask(struct irq_desc *desc);
static void aplic_irq_unmask(struct irq_desc *desc);
static void aplic_irq_eoi(struct irq_desc *desc);
static int aplic_irq_set_type(struct irq_desc *desc, uint32_t type);

// IRQ chip operations for APLIC
static struct irq_chip aplic_chip = {
    .name = "APLIC",
    .irq_mask = aplic_irq_mask,
    .irq_unmask = aplic_irq_unmask,
    .irq_eoi = aplic_irq_eoi,
    .irq_set_type = aplic_irq_set_type,
    .irq_enable = aplic_irq_unmask,  // Enable is same as unmask
    .irq_disable = aplic_irq_mask,   // Disable is same as mask
    .flags = 0,
};

// Forward declarations for domain operations
static int aplic_domain_map(struct irq_domain *d, uint32_t virq, uint32_t hwirq);
static void aplic_domain_unmap(struct irq_domain *d, uint32_t virq);
static int aplic_domain_xlate(struct irq_domain *d, const uint32_t *intspec,
                              uint32_t intsize, uint32_t *out_hwirq, uint32_t *out_type);

// Domain operations for APLIC
static const struct irq_domain_ops aplic_domain_ops = {
    .map = aplic_domain_map,
    .unmap = aplic_domain_unmap,
    .xlate = aplic_domain_xlate,
};

// Mask an interrupt
static void aplic_irq_mask(struct irq_desc *desc) {
    struct aplic_data *aplic = (struct aplic_data *)desc->chip_data;
    uint32_t hwirq = desc->hwirq;
    
    if (!aplic || hwirq == 0 || hwirq > aplic->nr_sources) return;
    
    // Clear interrupt enable using CLRIENUM register
    aplic_write(aplic, APLIC_CLRIENUM, hwirq);
}

// Unmask an interrupt
static void aplic_irq_unmask(struct irq_desc *desc) {
    struct aplic_data *aplic = (struct aplic_data *)desc->chip_data;
    uint32_t hwirq = desc->hwirq;
    
    if (!aplic || hwirq == 0 || hwirq > aplic->nr_sources) return;
    
    // Set interrupt enable using SETIENUM register
    aplic_write(aplic, APLIC_SETIENUM, hwirq);
}

// End of interrupt
static void aplic_irq_eoi(struct irq_desc *desc) {
    // In APLIC direct mode, EOI is handled automatically when claiming
    // the interrupt via CLAIMI register. The fasteoi flow requires
    // this callback but no action is needed here.
    (void)desc;
}

// Set interrupt type
static int aplic_irq_set_type(struct irq_desc *desc, uint32_t type) {
    struct aplic_data *aplic = (struct aplic_data *)desc->chip_data;
    uint32_t hwirq = desc->hwirq;
    uint32_t val = 0;
    
    if (!aplic || hwirq == 0 || hwirq > aplic->nr_sources) return -1;
    
    switch (type) {
    case IRQ_TYPE_NONE:
        val = APLIC_SOURCECFG_SM_INACTIVE;
        break;
    case IRQ_TYPE_LEVEL_LOW:
        val = APLIC_SOURCECFG_SM_LEVEL_LOW;
        break;
    case IRQ_TYPE_LEVEL_HIGH:
        val = APLIC_SOURCECFG_SM_LEVEL_HIGH;
        break;
    case IRQ_TYPE_EDGE_FALLING:
        val = APLIC_SOURCECFG_SM_EDGE_FALL;
        break;
    case IRQ_TYPE_EDGE_RISING:
        val = APLIC_SOURCECFG_SM_EDGE_RISE;
        break;
    default:
        return -1;
    }
    
    // Write to sourcecfg register for this interrupt
    aplic_write(aplic, aplic_sourcecfg_offset(hwirq), val);
    
    return 0;
}

// Domain map operation
static int aplic_domain_map(struct irq_domain *d, uint32_t virq, uint32_t hwirq) {
    struct aplic_data *aplic = (struct aplic_data *)d->chip_data;
    struct irq_desc *desc = irq_to_desc(virq);
    
    if (!desc) return -1;
    
    desc->hwirq = hwirq;
    desc->chip = &aplic_chip;
    desc->chip_data = aplic;
    // Note: handler will be set by IRQ subsystem based on type
    
    return 0;
}

// Domain unmap operation
static void aplic_domain_unmap(struct irq_domain *d, uint32_t virq) {
    struct irq_desc *desc = irq_to_desc(virq);
    
    if (!desc) return;
    
    desc->hwirq = 0;
    desc->chip = NULL;
    desc->chip_data = NULL;
    // Note: handler managed by IRQ subsystem
}

// Domain translate operation
static int aplic_domain_xlate(struct irq_domain *d, const uint32_t *intspec,
                              uint32_t intsize, uint32_t *out_hwirq, uint32_t *out_type) {
    if (intsize < 2) return -1;
    if (!intspec[0]) return -1;  // IRQ 0 is invalid
    
    *out_hwirq = intspec[0];
    *out_type = intspec[1] & IRQ_TYPE_LEVEL_MASK;
    
    if (*out_type == IRQ_TYPE_NONE) {
        *out_type = IRQ_TYPE_LEVEL_HIGH;  // Default type
    }
    
    return 0;
}

// Initialize hardware global settings
void aplic_init_hw_global(struct aplic_data *aplic) {
    uint32_t val;
    uint32_t i;
    
    // Configure domain
    val = APLIC_DOMAINCFG_IE;  // Enable interrupts
    if (!aplic->msi_mode) {
        val |= APLIC_DOMAINCFG_DM;  // Direct mode
    }
    aplic_write(aplic, APLIC_DOMAINCFG, val);
    
    // Initialize all sources as inactive
    for (i = 1; i <= aplic->nr_sources; i++) {
        aplic_write(aplic, aplic_sourcecfg_offset(i), APLIC_SOURCECFG_SM_INACTIVE);
    }
    
    // Clear all pending interrupts
    for (i = 1; i <= aplic->nr_sources; i++) {
        aplic_write(aplic, APLIC_CLRIPNUM, i);
    }
    
    // Disable all interrupts initially
    for (i = 1; i <= aplic->nr_sources; i++) {
        aplic_write(aplic, APLIC_CLRIENUM, i);
    }
}

// Common APLIC initialization
int aplic_init(struct aplic_data *aplic) {
    uart_puts("APLIC: Initializing controller\n");
    
    // Initialize hardware
    aplic_init_hw_global(aplic);
    
    // Verify configuration
    uint32_t domaincfg = aplic_read(aplic, APLIC_DOMAINCFG);
    if (!(domaincfg & APLIC_DOMAINCFG_IE)) {
        uart_puts("APLIC: Warning - interrupts not enabled (IE=0)\n");
    }
    if (!aplic->msi_mode && !(domaincfg & APLIC_DOMAINCFG_DM)) {
        uart_puts("APLIC: Warning - expected direct mode but DM=0\n");
    }
    
    // Create IRQ domain
    aplic->domain = irq_domain_create_linear(NULL, aplic->nr_sources + 1, 
                                             &aplic_domain_ops, aplic);
    if (!aplic->domain) {
        uart_puts("APLIC: Failed to create IRQ domain\n");
        return -1;
    }
    
    // Initialize mode-specific functionality
    if (aplic->msi_mode) {
        uart_puts("APLIC: Initializing MSI mode\n");
        return aplic_msi_init(aplic);
    } else {
        uart_puts("APLIC: Initializing direct mode\n");
        return aplic_direct_init(aplic);
    }
}

// Driver probe function - check if we can handle this device
static int aplic_probe(struct device *dev) {
    if (!dev || !dev->compatible) {
        return PROBE_SCORE_NONE;
    }
    
    // Check for APLIC compatible strings
    if (strstr(dev->compatible, "riscv,aplic") ||
        strstr(dev->compatible, "qemu,aplic")) {
        // APLIC can be at different addresses depending on mode (M-mode vs S-mode)
        // When OpenSBI configures APLIC in delegation mode, the S-mode APLIC
        // is disabled and delegates to the M-mode APLIC. In this case, we use
        // whichever APLIC is available and enabled.
        return PROBE_SCORE_EXACT;
    }
    
    return PROBE_SCORE_NONE;
}

// Driver attach function - actually initialize the device
static int aplic_attach(struct device *dev) {
    struct aplic_data *aplic = &primary_aplic_data;
    struct resource *res;
    uint32_t num_sources;
    
    uart_puts("APLIC: Attaching device ");
    uart_puts(dev->name);
    uart_puts("\n");
    
    // Get MMIO resource
    res = device_get_resource(dev, RES_TYPE_MEM, 0);
    if (!res) {
        uart_puts("APLIC: Failed to get memory resource\n");
        return -1;
    }
    
    // Use mapped virtual address if available, otherwise physical
    if (res->mapped_addr) {
        aplic->base = res->mapped_addr;
    } else {
        aplic->base = (void *)res->start;
    }
    
    // Store the base address
    
    // Get number of interrupt sources using the new property getter
    num_sources = device_get_property_u32(dev, "riscv,num-sources", 96);  // Default to 96 for QEMU
    aplic->nr_sources = num_sources;
    uart_puts("APLIC: Number of sources: ");
    uart_putdec(aplic->nr_sources);
    uart_puts("\n");
    
    // Check for MSI mode capability (msi-parent property)
    if (device_get_property_bool(dev, "msi-parent")) {
        uart_puts("APLIC: MSI-capable hardware detected, enabling MSI mode\n");
        aplic->msi_mode = true;
    } else {
        aplic->msi_mode = false;
        uart_puts("APLIC: Direct mode only hardware\n");
    }
    
    // Parse interrupts-extended to map harts to IDCs
    // This property contains pairs of (interrupt-parent-phandle, interrupt-type)
    // We need to count how many IDCs we have and map them to hart IDs
    
    // Try to get interrupts-extended property
    if (dev->fdt_offset >= 0) {
        // For now, we'll default to a simple mapping
        // TODO: Implement proper FDT parsing of interrupts-extended
        uart_puts("APLIC: TODO - Parse interrupts-extended from device tree\n");
    }
    
    // Default mapping for QEMU: assume IDC 0 maps to hart 0
    aplic->nr_idcs = 1;
    aplic->nr_harts = 1;
    aplic->hart_index_map[0] = 0;  // Hart 0 -> IDC 0
    
    uart_puts("APLIC: Configured with ");
    uart_putdec(aplic->nr_idcs);
    uart_puts(" IDC(s) for ");
    uart_putdec(aplic->nr_harts);
    uart_puts(" hart(s)\n");
    
    // Initialize APLIC
    if (aplic_init(aplic) != 0) {
        uart_puts("APLIC: Initialization failed\n");
        return -1;
    }
    
    // Set as primary APLIC
    aplic_primary = aplic;
    
    uart_puts("APLIC: Successfully initialized\n");
    return 0;
}

// Driver detach function
static int aplic_detach(struct device *dev) {
    (void)dev;  // Not supported yet
    return -1;
}

// Driver operations
static struct driver_ops aplic_driver_ops = {
    .probe = aplic_probe,
    .attach = aplic_attach,
    .detach = aplic_detach,
};

// Driver match table
static const struct device_match aplic_matches[] = {
    { .type = MATCH_COMPATIBLE, .value = "riscv,aplic" },
    { .type = MATCH_COMPATIBLE, .value = "qemu,aplic" },
};

// Driver structure
static struct driver aplic_driver = {
    .name = "riscv-aplic",
    .class = DRIVER_CLASS_INTC,
    .ops = &aplic_driver_ops,
    .matches = aplic_matches,
    .num_matches = sizeof(aplic_matches) / sizeof(aplic_matches[0]),
    .priority = 0,  // Highest priority for interrupt controller
    .flags = DRIVER_FLAG_BUILTIN | DRIVER_FLAG_EARLY,
};

// Driver registration
static void aplic_driver_init(void) {
    int ret;
    
    uart_puts("APLIC: Registering driver\n");
    
    ret = driver_register(&aplic_driver);
    if (ret == 0) {
        uart_puts("APLIC: Driver registered successfully\n");
    } else {
        uart_puts("APLIC: Failed to register driver\n");
    }
}

// Register as an early IRQCHIP driver
IRQCHIP_DRIVER_MODULE(aplic_driver_init, DRIVER_PRIO_EARLY);

#endif // __riscv