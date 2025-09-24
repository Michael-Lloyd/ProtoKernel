/*
 * kernel/drivers/irqchip/riscv-imsic.c
 *
 * RISC-V Incoming MSI Controller (IMSIC) driver skeleton
 */

#ifdef __riscv

#include <irqchip/riscv-imsic.h>
#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <irq/msi.h>
#include <device/device.h>
#include <device/resource.h>
#include <drivers/driver.h>
#include <drivers/driver_module.h>
#include <uart.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <arch_io.h>
#include <irq/irq_domain.h>
#include <irq/msi.h>
#include <lib/ffs.h>

/* Primary controller storage */
static struct imsic_data primary_imsic_data;
static struct imsic_file primary_imsic_file;
static bool imsic_initialized;

/* Forward declarations */
static int imsic_probe(struct device *dev);
static int imsic_attach(struct device *dev);
static int imsic_detach(struct device *dev);
static void imsic_irq_enable(struct irq_desc *desc);
static void imsic_irq_disable(struct irq_desc *desc);
static void imsic_irq_ack(struct irq_desc *desc);
static void imsic_irq_mask(struct irq_desc *desc);
static void imsic_irq_unmask(struct irq_desc *desc);
static int imsic_irq_domain_map(struct irq_domain *d, unsigned int irq, uint32_t hwirq);

/* Driver operations */
static struct driver_ops imsic_driver_ops = {
    .probe = imsic_probe,
    .attach = imsic_attach,
    .detach = imsic_detach,
};

/* Match table */
static const struct device_match imsic_matches[] = {
    { .type = MATCH_COMPATIBLE, .value = "riscv,imsics" },
    { .type = MATCH_COMPATIBLE, .value = "qemu,imsics" },
};

/* Driver instance */
static struct driver imsic_driver = {
    .name = "riscv-imsic",
    .class = DRIVER_CLASS_INTC,
    .ops = &imsic_driver_ops,
    .matches = imsic_matches,
    .num_matches = sizeof(imsic_matches) / sizeof(imsic_matches[0]),
    .priority = 0,
    .flags = DRIVER_FLAG_BUILTIN | DRIVER_FLAG_EARLY,
};

static struct irq_chip imsic_irq_chip = {
    .name = "IMSIC",
    .irq_enable = imsic_irq_enable,
    .irq_disable = imsic_irq_disable,
    .irq_ack = imsic_irq_ack,
    .irq_mask = imsic_irq_mask,
    .irq_unmask = imsic_irq_unmask,
};

static const struct irq_domain_ops imsic_irq_domain_ops = {
    .map = imsic_irq_domain_map,
    .xlate = NULL,
};


static int imsic_probe(struct device *dev) {
    if (!dev || !dev->compatible) {
        return PROBE_SCORE_NONE;
    }

    if (strstr(dev->compatible, "riscv,imsics") ||
        strstr(dev->compatible, "qemu,imsics")) {
        return PROBE_SCORE_EXACT;
    }

    return PROBE_SCORE_NONE;
}

static void imsic_log_basic_info(struct imsic_data *imsic) {
    uart_puts("IMSIC: num_harts=");
    uart_putdec(imsic->num_harts);
    uart_puts(", num_ids=");
    uart_putdec(imsic->num_ids);
    uart_puts("\n");
}

static int imsic_attach(struct device *dev) {
    struct resource *res;
    struct imsic_data *imsic = &primary_imsic_data;
    struct imsic_file *file = &primary_imsic_file;
    uint64_t base_phys;

    uart_puts("IMSIC: Attaching device ");
    uart_puts(dev->name);
    uart_puts("\n");

    if (imsic_initialized) {
        uart_puts("IMSIC: Already initialized, skipping duplicate attach\n");
        return -1;
    }

    res = device_get_resource(dev, RES_TYPE_MEM, 0);
    if (!res) {
        uart_puts("IMSIC: Missing MMIO resource\n");
        return -1;
    }

    if (res->mapped_addr) {
        file->base = res->mapped_addr;
        base_phys = res->start;
    } else {
        file->base = (void *)(uintptr_t)res->start;
        base_phys = res->start;
    }

    file->hart_id = 0;
    file->num_ids = device_get_property_u32(dev, "riscv,num-ids", IMSIC_MAX_IDS);
    file->pending_bitmap = NULL;   // TODO: Allocate and initialize a bitmap to cache pending interrupts for each hart.
    file->enabled_bitmap = NULL;   // TODO: Allocate and initialize a bitmap to cache enabled interrupts for each hart.

    memset(imsic, 0, sizeof(*imsic));
    imsic->files = file;
    imsic->num_harts = 1;          // TODO: Discover the number of harts from the device tree's 'interrupts-extended' property.
    imsic->num_ids = file->num_ids;
    imsic->base_ppn = (base_phys >> 12);
    imsic->domain = irq_domain_create_linear(NULL, imsic->num_ids, &imsic_irq_domain_ops, imsic);
    if (!imsic->domain) {
        uart_puts("IMSIC: Failed to create IRQ domain\n");
        return -1;
    }

    imsic->msi_domain = NULL;      // TODO: Create and register an MSI domain for the IMSIC.

    imsic_log_basic_info(imsic);

    device_set_driver_data(dev, imsic);
    imsic_initialized = true;

    return 0;
}

static int imsic_detach(struct device *dev) {
    (void)dev;
    return -1;  // Not supported yet
}

static int imsic_irq_domain_map(struct irq_domain *d, unsigned int irq, uint32_t hwirq) {
    (void)d;
    (void)hwirq;
    struct irq_desc *desc = irq_to_desc(irq);
    if (!desc) {
        return -1;
    }
    desc->chip = &imsic_irq_chip;
    desc->chip_data = &primary_imsic_file; // TODO: select the correct file for the hart
    return 0;
}

static void imsic_irq_enable(struct irq_desc *desc) {
    struct imsic_file *file = desc->chip_data;
    imsic_set_enabled(file, desc->hwirq, true);
}

static void imsic_irq_disable(struct irq_desc *desc) {
    struct imsic_file *file = desc->chip_data;
    imsic_set_enabled(file, desc->hwirq, false);
}

static void imsic_irq_ack(struct irq_desc *desc) {
    struct imsic_file *file = desc->chip_data;
    imsic_clear_pending(file, desc->hwirq);
}

static void imsic_irq_mask(struct irq_desc *desc) {
    struct imsic_file *file = desc->chip_data;
    imsic_set_enabled(file, desc->hwirq, false);
}

static void imsic_irq_unmask(struct irq_desc *desc) {
    struct imsic_file *file = desc->chip_data;
    imsic_set_enabled(file, desc->hwirq, true);
}

/* Low-level register access */
void imsic_write_reg(struct imsic_file *file, uint32_t reg, uint32_t val) {
    mmio_write32((void *)(file->base + reg), val);
}

uint32_t imsic_read_reg(struct imsic_file *file, uint32_t reg) {
    return mmio_read32((void *)(file->base + reg));
}

/* Interrupt manipulation */
void imsic_set_pending(struct imsic_file *file, uint32_t id) {
    imsic_write_reg(file, IMSIC_REG_SETEIPNUM, id);
}

void imsic_clear_pending(struct imsic_file *file, uint32_t id) {
    imsic_write_reg(file, IMSIC_REG_CLREIPNUM, id);
}

void imsic_set_enabled(struct imsic_file *file, uint32_t id, bool enabled) {
    uint32_t reg = IMSIC_REG_EIE_BASE + (id / 32) * 4;
    uint32_t mask = 1 << (id % 32);
    uint32_t val = imsic_read_reg(file, reg);

    if (enabled) {
        val |= mask;
    } else {
        val &= ~mask;
    }

    imsic_write_reg(file, reg, val);
}

void imsic_set_threshold(struct imsic_file *file, uint32_t threshold) {
    imsic_write_reg(file, IMSIC_REG_EITHRESHOLD, threshold);
}

// Top-level interrupt handler
void imsic_handle_irq(void) {
    struct imsic_data *imsic = &primary_imsic_data;
    struct imsic_file *file = &imsic->files[0]; // TODO: select the correct file for the hart
    uint32_t hwirq = 0;

    // Find the first pending interrupt
    for (int i = 0; i < (imsic->num_ids + 31) / 32; i++) {
        uint32_t pending = imsic_read_reg(file, IMSIC_REG_EIP_BASE + i * 4);
        if (pending) {
            hwirq = i * 32 + ffs(pending) - 1;
            break;
        }
    }

    if (hwirq) {
        uint32_t virq = irq_find_mapping(imsic->domain, hwirq);
        if (virq) {
            generic_handle_irq(virq);
        }
        imsic_clear_pending(file, hwirq);
    }
}

static void imsic_driver_init(void) {
    int ret;

    uart_puts("IMSIC: Registering driver\n");
    ret = driver_register(&imsic_driver);
    if (ret == 0) {
        uart_puts("IMSIC: Driver registered successfully\n");
    } else {
        uart_puts("IMSIC: Driver registration failed\n");
    }
}

IRQCHIP_DRIVER_MODULE(imsic_driver_init, DRIVER_PRIO_EARLY);

#endif /* __riscv */