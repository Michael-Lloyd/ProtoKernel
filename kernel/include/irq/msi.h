#ifndef _IRQ_MSI_H
#define _IRQ_MSI_H

#include <stdint.h>
#include <stddef.h>
#include <spinlock.h>
#include <device/device.h>

struct device_node;
struct irq_domain;
struct irq_chip;

// MSI capability flags
#define MSI_FLAG_USE_DEF_NUM_VECS   0x0001
#define MSI_FLAG_MULTI_VECTOR       0x0002
#define MSI_FLAG_64BIT              0x0004
#define MSI_FLAG_MASKABLE           0x0008
#define MSI_FLAG_MSIX               0x0010

// MSI vector limits
#define MSI_MAX_VECTORS             32
#define MSIX_MAX_VECTORS            2048

// MSI message format defines
#define MSI_DATA_VECTOR_SHIFT       0
#define MSI_DATA_VECTOR_MASK        0xff
#define MSI_DATA_DELIVERY_SHIFT     8
#define MSI_DATA_DELIVERY_MASK      0x700
#define MSI_DATA_LEVEL_SHIFT        14
#define MSI_DATA_LEVEL_ASSERT       (1 << MSI_DATA_LEVEL_SHIFT)
#define MSI_DATA_TRIGGER_SHIFT      15
#define MSI_DATA_TRIGGER_EDGE       0
#define MSI_DATA_TRIGGER_LEVEL      (1 << MSI_DATA_TRIGGER_SHIFT)

// MSI address format defines (architecture specific values will override)
#define MSI_ADDR_BASE_HI            0
#define MSI_ADDR_BASE_LO            0xfee00000
#define MSI_ADDR_REDIRECTION_SHIFT  12
#define MSI_ADDR_REDIRECTION_MASK   (0xf << MSI_ADDR_REDIRECTION_SHIFT)
#define MSI_ADDR_DEST_MODE_SHIFT    2
#define MSI_ADDR_DEST_MODE_PHYSICAL (0 << MSI_ADDR_DEST_MODE_SHIFT)
#define MSI_ADDR_DEST_MODE_LOGICAL  (1 << MSI_ADDR_DEST_MODE_SHIFT)
#define MSI_ADDR_DEST_ID_SHIFT      12
#define MSI_ADDR_DEST_ID_MASK       0xff000

// MSI capability attributes
#define MSI_CAP_64BIT               (1 << 0)
#define MSI_CAP_MASKABLE            (1 << 1)
#define MSI_CAP_MULTI_VECTOR        (1 << 2)

// MSI-X capability attributes  
#define MSIX_CAP_MASKABLE           (1 << 0)
#define MSIX_CAP_TABLE_SIZE_MASK    0x7ff

// MSI message - what device writes to trigger interrupt
struct msi_msg {
    uint32_t address_lo;
    uint32_t address_hi;
    uint32_t data;
};

// List head structure (same as in irq_domain.h)
struct msi_list_head {
    struct msi_list_head *next, *prev;
};

// MSI descriptor - per-vector information
struct msi_desc {
    struct msi_list_head list;
    
    uint32_t irq;
    uint32_t hwirq;
    
    struct msi_msg msg;
    
    // MSI capability info
    uint16_t msi_attrib;
    uint8_t  multiple;
    uint8_t  multi_cap;
    
    // MSI-X specific
    uint16_t msix_attrib;
    uint32_t mask_pos;
    
    void *mask_base;
    
    // Device association
    struct device *dev;
    void *chip_data;
    
    // Reference counting
    uint32_t refcount;
};

// Per-device MSI information
struct msi_device_data {
    struct msi_list_head list;
    uint32_t num_vectors;
    
    // Capability pointers
    void *msi_cap;
    void *msix_cap;
    
    spinlock_t lock;
};

// MSI domain info - controller capabilities
struct msi_domain_info {
    uint32_t flags;
    struct msi_domain_ops *ops;
    struct irq_chip *chip;
    void *chip_data;
    
    // Capabilities
    uint32_t max_vectors;
    uint32_t required_flags;
    uint32_t supported_flags;
};

// MSI domain operations
struct msi_domain_ops {
    // Calculate number of vectors to allocate
    int (*calc_vectors)(struct msi_domain_info *info,
                       struct device *dev, int nvec);
    
    // Set descriptor for allocated IRQ
    int (*set_desc)(struct msi_domain_info *info,
                   struct msi_desc *desc,
                   unsigned int virq, unsigned int hwirq);
    
    // Handle descriptor
    int (*handle_desc)(struct msi_domain_info *info,
                       struct msi_desc *desc);
    
    // Domain-specific init/cleanup
    int (*domain_init)(struct irq_domain *domain,
                      struct msi_domain_info *info);
    void (*domain_free)(struct irq_domain *domain);
};

// Device MSI Management
int msi_device_init(struct device *dev);
void msi_device_cleanup(struct device *dev);

// MSI Allocation
int msi_alloc_vectors(struct device *dev, uint32_t min_vecs,
                     uint32_t max_vecs, unsigned int flags);
void msi_free_vectors(struct device *dev);

// MSI Programming
void msi_compose_msg(struct msi_desc *desc, struct msi_msg *msg);
void msi_write_msg(struct msi_desc *desc, struct msi_msg *msg);

// MSI Operations
void msi_mask_irq(struct msi_desc *desc);
void msi_unmask_irq(struct msi_desc *desc);
int msi_set_affinity(struct msi_desc *desc, uint32_t cpu_mask);

// MSI Domain Creation
struct irq_domain *msi_create_domain(struct device_node *node,
                                    struct msi_domain_info *info,
                                    struct irq_domain *parent);

// MSI Descriptor Management
struct msi_desc *msi_desc_alloc(struct device *dev, uint32_t nvec);
void msi_desc_free(struct msi_desc *desc);
int msi_desc_list_add(struct msi_device_data *msi_data, struct msi_desc *desc);
int msi_desc_list_add_locked(struct msi_device_data *msi_data, struct msi_desc *desc);

#endif /* _IRQ_MSI_H */