/*
 * kernel/drivers/irqchip/riscv-intc.c
 * 
 * RISC-V Local Interrupt Controller (INTC) driver implementation
 * Handles local interrupts like timer and software interrupts
 */

/* Only compile this driver for RISC-V architecture */
#ifdef __riscv

#include <irqchip/riscv-intc.h>
#include <irqchip/riscv-plic.h>
#include <irqchip/riscv-aplic.h>
#include <irqchip/riscv-imsic.h>
#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <uart.h>
#include <stddef.h>
#include <string.h>
#include <device/device.h>
#include <drivers/driver.h>
#include <drivers/driver_module.h>

// Global INTC instance
struct intc_data *intc_primary = NULL;

// Static INTC data for the primary controller
static struct intc_data primary_intc_data;

// Forward declarations for irq_chip operations
static void intc_irq_mask(struct irq_desc *desc);
static void intc_irq_unmask(struct irq_desc *desc);
static void intc_irq_eoi(struct irq_desc *desc);

// IRQ chip operations for INTC
static struct irq_chip intc_chip = {
    .name = "RISC-V-INTC",
    .irq_mask = intc_irq_mask,
    .irq_unmask = intc_irq_unmask,
    .irq_eoi = intc_irq_eoi,
    .irq_enable = intc_irq_unmask,
    .irq_disable = intc_irq_mask,
    .flags = 0,
};

// Forward declarations for domain operations
static int intc_domain_map(struct irq_domain *d, uint32_t virq, uint32_t hwirq);
static void intc_domain_unmap(struct irq_domain *d, uint32_t virq);

// Domain operations for INTC
static const struct irq_domain_ops intc_domain_ops = {
    .map = intc_domain_map,
    .unmap = intc_domain_unmap,
};

// IRQ chip operations implementation
static void intc_irq_mask(struct irq_desc *desc) {
    if (!desc) return;
    
    switch (desc->hwirq) {
    case IRQ_S_SOFT:
        arch_irq_disable(SIE_SSIE);
        break;
    case IRQ_S_TIMER:
        arch_irq_disable(SIE_STIE);
        break;
    case IRQ_S_EXT:
        arch_irq_disable(SIE_SEIE);
        break;
    }
}

static void intc_irq_unmask(struct irq_desc *desc) {
    if (!desc) return;
    
    switch (desc->hwirq) {
    case IRQ_S_SOFT:
        arch_irq_enable(SIE_SSIE);
        break;
    case IRQ_S_TIMER:
        arch_irq_enable(SIE_STIE);
        break;
    case IRQ_S_EXT:
        arch_irq_enable(SIE_SEIE);
        break;
    }
}

static void intc_irq_eoi(struct irq_desc *desc) {
    // Local interrupts don't need explicit EOI
    // They are cleared by handling the interrupt source
}

// Domain operations implementation
static int intc_domain_map(struct irq_domain *d, uint32_t virq, uint32_t hwirq) {
    struct intc_data *intc = (struct intc_data *)d->chip_data;
    
    // Only handle local interrupts
    if (hwirq != IRQ_S_SOFT && hwirq != IRQ_S_TIMER && hwirq != IRQ_S_EXT)
        return -1;
    
    struct irq_desc *desc = irq_to_desc(virq);
    if (!desc)
        return -1;
    
    desc->chip = &intc_chip;
    desc->chip_data = intc;
    desc->hwirq = hwirq;
    desc->domain = d;
    
    return 0;
}

static void intc_domain_unmap(struct irq_domain *d, uint32_t virq) {
    struct irq_desc *desc = irq_to_desc(virq);
    if (!desc)
        return;
    
    // Mask the interrupt
    intc_irq_mask(desc);
    
    // Clear descriptor
    desc->chip = NULL;
    desc->chip_data = NULL;
    desc->hwirq = 0;
    desc->domain = NULL;
}

// Initialize INTC
int intc_init(void) {
    struct intc_data *intc = &primary_intc_data;
    
    uart_puts("[INTC] Initializing RISC-V Local Interrupt Controller\n");
    
    // Set as primary INTC
    intc_primary = intc;
    
    // Clear all interrupt enables
    arch_irq_set_ie(0);
    
    // Clear all pending interrupts
    arch_irq_clear_pending(~0UL);
    
    // Create IRQ domain for INTC (only 3 local interrupts, but allocate 16 for safety)
    intc->domain = irq_domain_create_linear(NULL, 16, &intc_domain_ops, intc);
    if (!intc->domain) {
        uart_puts("[INTC] ERROR: Failed to create IRQ domain\n");
        return -1;
    }
    
    uart_puts("[INTC] INTC initialized successfully\n");
    return 0;
}

// Handle local interrupts
void intc_handle_irq(uint64_t cause) {
    struct intc_data *intc = intc_primary;
    if (!intc) return;
    
    uint32_t hwirq = 0;
    
    // Determine which interrupt
    switch (cause) {
    case IRQ_S_SOFT:
        hwirq = IRQ_S_SOFT;
        break;
    case IRQ_S_TIMER:
        hwirq = IRQ_S_TIMER;
        break;
    case IRQ_S_EXT:
        // External interrupt - delegate to APLIC if available, otherwise PLIC
        if (aplic_primary) {
            if (aplic_primary->msi_mode) {
                imsic_handle_irq();
            } else {
                aplic_direct_handle_irq();
            }
        } else if (plic_primary) {
            plic_handle_irq();
        }
        return;
    default:
        return;
    }
    
    // Find virtual IRQ for local interrupt
    uint32_t virq = irq_find_mapping(intc->domain, hwirq);
    if (virq == 0) {
        // No mapping, just clear the interrupt
        return;
    }
    
    // Dispatch to handler
    generic_handle_irq(virq);
}

// Enable external interrupts
void intc_enable_external(void) {
    arch_irq_enable(SIE_SEIE);
}

// Disable external interrupts
void intc_disable_external(void) {
    arch_irq_disable(SIE_SEIE);
}

// Driver probe function - check if we can handle this device
static int intc_driver_probe(struct device *dev) {
    if (!dev || !dev->compatible) {
        return PROBE_SCORE_NONE;
    }
    
    // Check for RISC-V CPU INTC compatible string
    if (strstr(dev->compatible, "riscv,cpu-intc")) {
        return PROBE_SCORE_EXACT;
    }
    
    return PROBE_SCORE_NONE;
}

// Driver attach function - actually initialize the device
static int intc_driver_attach(struct device *dev) {
    uart_puts("INTC: Attaching device ");
    uart_puts(dev->name);
    uart_puts("\n");
    
    // INTC doesn't need any resources, it's built into the CPU
    return intc_init();
}

// Driver detach function
static int intc_driver_detach(struct device *dev) {
    (void)dev; // Not supported yet
    return -1;
}

// Driver operations
static const struct driver_ops intc_driver_ops = {
    .probe = intc_driver_probe,
    .attach = intc_driver_attach,
    .detach = intc_driver_detach,
};

// Driver match table
static const struct device_match intc_matches[] = {
    { .type = MATCH_COMPATIBLE, .value = "riscv,cpu-intc" },
};

// Driver structure
static struct driver intc_driver = {
    .name = "riscv-intc",
    .class = DRIVER_CLASS_INTC,
    .ops = &intc_driver_ops,
    .matches = intc_matches,
    .num_matches = sizeof(intc_matches) / sizeof(intc_matches[0]),
    .priority = 0,  // Highest priority for interrupt controller
    .flags = DRIVER_FLAG_BUILTIN | DRIVER_FLAG_EARLY,
};

// Driver registration
static void intc_driver_init(void) {
    int ret;
    
    uart_puts("INTC: Registering driver\n");
    
    ret = driver_register(&intc_driver);
    if (ret == 0) {
        uart_puts("INTC: Driver registered successfully\n");
    } else {
        uart_puts("INTC: Failed to register driver\n");
    }
}

// Register as an early IRQCHIP driver (must init before PLIC)
IRQCHIP_DRIVER_MODULE(intc_driver_init, DRIVER_PRIO_EARLY);

#else /* !RISC-V */

/* Stub implementations for non-RISC-V architectures */
#include <stdint.h>
#include <stddef.h>

struct intc_data *intc_primary = NULL;

int intc_init(void) { return 0; }
void intc_handle_irq(uint64_t cause) { (void)cause; }
void intc_enable_external(void) { }
void intc_disable_external(void) { }

#endif /* RISC-V */