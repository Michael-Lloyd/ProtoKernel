#ifndef _IRQ_H
#define _IRQ_H

#include <stdint.h>
#include <irq/irq_domain.h>
#include <device/device.h>

// Request interrupt (virq MUST come from domain mapping)
int request_irq(uint32_t irq, irq_handler_t handler,
               unsigned long flags, const char *name, void *dev);

// Free interrupt
void free_irq(uint32_t irq, void *dev);

// Control
void enable_irq(uint32_t irq);
void disable_irq(uint32_t irq);
void disable_irq_nosync(uint32_t irq);

// MSI allocation
int msi_alloc_irqs(struct device *dev, int nvec);
void msi_free_irqs(struct device *dev);

// Flags
#define IRQF_SHARED         0x0001
#define IRQF_TRIGGER_RISING 0x0002
#define IRQF_TRIGGER_FALLING 0x0004
#define IRQF_TRIGGER_HIGH   0x0008
#define IRQF_TRIGGER_LOW    0x0010
#define IRQF_ONESHOT        0x0020
#define IRQF_NO_THREAD      0x0040

// IRQ types
#define IRQ_TYPE_NONE           0x00000000
#define IRQ_TYPE_EDGE_RISING    0x00000001
#define IRQ_TYPE_EDGE_FALLING   0x00000002
#define IRQ_TYPE_EDGE_BOTH      (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING)
#define IRQ_TYPE_LEVEL_HIGH     0x00000004
#define IRQ_TYPE_LEVEL_LOW      0x00000008
#define IRQ_TYPE_LEVEL_MASK     (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH)

// IRQ status flags
#define IRQ_DISABLED        0x00000001
#define IRQ_PENDING         0x00000002
#define IRQ_INPROGRESS      0x00000004
#define IRQ_REPLAY          0x00000008
#define IRQ_AUTODETECT      0x00000010
#define IRQ_WAITING         0x00000020
#define IRQ_LEVEL           0x00000040
#define IRQ_MASKED          0x00000080
#define IRQ_PER_CPU         0x00000100
#define IRQ_NOPROBE         0x00000200
#define IRQ_NOREQUEST       0x00000400
#define IRQ_NOAUTOEN        0x00000800

// Initialize IRQ subsystem
void irq_init(void);

// Get IRQ descriptor
struct irq_desc *irq_to_desc(uint32_t irq);

// IRQ descriptor management
struct irq_desc *irq_desc_alloc(uint32_t irq);
void irq_desc_free(struct irq_desc *desc);

// Invalid IRQ number
#define IRQ_INVALID     0xFFFFFFFF

// IRQ handling functions
void generic_handle_irq(uint32_t irq);
void irq_domain_handle_irq(struct irq_domain *domain, uint32_t hwirq);

#endif /* _IRQ_H */