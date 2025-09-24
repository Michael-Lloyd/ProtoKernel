/*
 * kernel/include/irqchip/riscv-imsic.h
 *
 * RISC-V Incoming MSI Controller (IMSIC) definitions
 */

#ifndef _IRQCHIP_RISCV_IMSIC_H
#define _IRQCHIP_RISCV_IMSIC_H

#include <stdint.h>
#include <irq/irq_domain.h>

/* IMSIC layout and register definitions */
#define IMSIC_MMIO_STRIDE            0x1000      /* Bytes between per-hart files */
#define IMSIC_MAX_IDS                256         /* QEMU virt exposes 256 IDs */

/* Register offsets (per hart) */
#define IMSIC_REG_SETEIPNUM          0x000
#define IMSIC_REG_CLREIPNUM          0x004
#define IMSIC_REG_SETEIDELIVERY      0x040
#define IMSIC_REG_CLREIDELIVERY      0x044
#define IMSIC_REG_EITHRESHOLD        0x070
#define IMSIC_REG_EIP_BASE           0x080
#define IMSIC_REG_EIE_BASE           0x0C0

struct imsic_file {
    volatile void *base;          /* MMIO base for this hart */
    uint32_t hart_id;             /* Associated hart */
    uint32_t num_ids;             /* Interrupt IDs supported */
    uint64_t *pending_bitmap;     /* TODO: Track pending interrupt cache */
    uint64_t *enabled_bitmap;     /* TODO: Track enabled interrupt cache */
};

struct imsic_data {
    struct imsic_file *files;     /* Per-hart files */
    uint32_t num_harts;           /* Number of harts exposed */
    uint32_t num_ids;             /* Number of interrupt IDs */
    uint64_t base_ppn;            /* MSI base physical page number */
    struct irq_domain *domain;    /* Interrupt domain placeholder */
    struct irq_domain *msi_domain;/* MSI domain placeholder */
};

#endif /* _IRQCHIP_RISCV_IMSIC_H */
