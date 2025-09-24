/*
 * kernel/drivers/irqchip/riscv-imsic.c
 *
 * RISC-V Incoming MSI Controller (IMSIC) driver skeleton
 */

#ifdef __riscv

#include <irqchip/riscv-imsic.h>
#include <irq/irq.h>
#include <device/device.h>
#include <device/resource.h>
#include <drivers/driver.h>
#include <drivers/driver_module.h>
#include <uart.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Primary controller storage */
static struct imsic_data primary_imsic_data;
static struct imsic_file primary_imsic_file;
static bool imsic_initialized;

/* Forward declarations */
static int imsic_probe(struct device *dev);
static int imsic_attach(struct device *dev);
static int imsic_detach(struct device *dev);

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
    file->pending_bitmap = NULL;   // TODO: Allocate per-hart pending cache
    file->enabled_bitmap = NULL;   // TODO: Allocate per-hart enable cache

    memset(imsic, 0, sizeof(*imsic));
    imsic->files = file;
    imsic->num_harts = 1;          // TODO: Discover additional harts via interrupts-extended
    imsic->num_ids = file->num_ids;
    imsic->base_ppn = (base_phys >> 12);
    imsic->domain = NULL;          // TODO: Create IRQ domain once handlers exist
    imsic->msi_domain = NULL;      // TODO: Hook up MSI domain support

    imsic_log_basic_info(imsic);

    device_set_driver_data(dev, imsic);
    imsic_initialized = true;

    return 0;
}

static int imsic_detach(struct device *dev) {
    (void)dev;
    return -1;  // Not supported yet
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
