#include <irq/msi.h>
#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <memory/kmalloc.h>
#include <memory/slab.h>
#include <string.h>
#include <panic.h>

// Helper to allocate a new MSI descriptor
static struct msi_desc *msi_desc_alloc_internal(void) {
    struct msi_desc *desc;
    
    desc = kmalloc(sizeof(*desc), KM_ZERO);
    if (!desc) {
        return NULL;
    }
    
    // Initialize list head
    desc->list.next = &desc->list;
    desc->list.prev = &desc->list;
    desc->refcount = 1;
    
    return desc;
}

// Allocate MSI descriptor for a device
struct msi_desc *msi_desc_alloc(struct device *dev, uint32_t nvec) {
    struct msi_desc *desc;
    
    if (!dev || nvec == 0 || nvec > MSI_MAX_VECTORS) {
        return NULL;
    }
    
    desc = msi_desc_alloc_internal();
    if (!desc) {
        return NULL;
    }
    
    desc->dev = dev;
    desc->multiple = 0;
    
    // Calculate log2 of nvec
    while ((1U << desc->multiple) < nvec) {
        desc->multiple++;
    }
    
    return desc;
}

// Free MSI descriptor
void msi_desc_free(struct msi_desc *desc) {
    if (!desc) {
        return;
    }
    
    desc->refcount--;
    if (desc->refcount > 0) {
        return;
    }
    
    // Remove from list if still linked
    if (desc->list.next != &desc->list) {
        desc->list.next->prev = desc->list.prev;
        desc->list.prev->next = desc->list.next;
    }
    
    kfree(desc);
}

// Add descriptor to device's MSI list
int msi_desc_list_add(struct msi_device_data *msi_data, struct msi_desc *desc) {
    unsigned long flags;
    int ret;
    
    if (!msi_data || !desc) {
        return -1;
    }
    
    spin_lock_irqsave(&msi_data->lock, flags);
    ret = msi_desc_list_add_locked(msi_data, desc);
    spin_unlock_irqrestore(&msi_data->lock, flags);
    
    return ret;
}

// Add descriptor to device's MSI list (caller holds lock)
int msi_desc_list_add_locked(struct msi_device_data *msi_data, struct msi_desc *desc) {
    if (!msi_data || !desc) {
        return -1;
    }
    
    // Add to tail of list
    desc->list.next = &msi_data->list;
    desc->list.prev = msi_data->list.prev;
    msi_data->list.prev->next = &desc->list;
    msi_data->list.prev = &desc->list;
    
    msi_data->num_vectors++;
    desc->refcount++;
    
    return 0;
}

// Initialize MSI support for a device
int msi_device_init(struct device *dev) {
    struct msi_device_data *msi_data;
    
    if (!dev) {
        return -1;
    }
    
    msi_data = kmalloc(sizeof(*msi_data), KM_ZERO);
    if (!msi_data) {
        return -1;
    }
    
    // Initialize list head
    msi_data->list.next = &msi_data->list;
    msi_data->list.prev = &msi_data->list;
    
    spin_lock_init(&msi_data->lock);
    
    dev->msi_data = msi_data;
    
    return 0;
}

// Cleanup MSI support for a device
void msi_device_cleanup(struct device *dev) {
    struct msi_device_data *msi_data;
    struct msi_desc *desc, *next;
    unsigned long flags;
    
    if (!dev || !dev->msi_data) {
        return;
    }
    
    msi_data = dev->msi_data;
    
    spin_lock_irqsave(&msi_data->lock, flags);
    
    // Walk list and free descriptors
    desc = (struct msi_desc *)msi_data->list.next;
    while (&desc->list != &msi_data->list) {
        next = (struct msi_desc *)desc->list.next;
        
        // Remove from list
        desc->list.next->prev = desc->list.prev;
        desc->list.prev->next = desc->list.next;
        
        desc->refcount--;
        if (desc->refcount == 0) {
            kfree(desc);
        }
        
        desc = next;
    }
    
    spin_unlock_irqrestore(&msi_data->lock, flags);
    
    kfree(msi_data);
    dev->msi_data = NULL;
}

// Allocate MSI vectors for a device
int msi_alloc_vectors(struct device *dev, uint32_t min_vecs,
                     uint32_t max_vecs, unsigned int flags) {
    struct msi_device_data *msi_data;
    struct msi_desc *desc;
    uint32_t nvec, hwirq_base;
    uint32_t i;
    unsigned long irqflags;

    if (!dev || !dev->msi_data || !dev->msi_domain || min_vecs == 0 ||
        min_vecs > max_vecs || max_vecs > MSI_MAX_VECTORS) {
        return -1;
    }

    msi_data = dev->msi_data;

    // Calculate the number of vectors to allocate.
    // Find the largest power of two such that min_vecs <= nvec <= max_vecs.
    nvec = 1;
    while (nvec <= max_vecs) {
        nvec <<= 1;
    }
    nvec >>= 1;

    if (nvec < min_vecs) {
        return -1;
    }

    spin_lock_irqsave(&msi_data->lock, irqflags);

    if (irq_domain_alloc_hwirq_range(dev->msi_domain, nvec, &hwirq_base) < 0) {
        spin_unlock_irqrestore(&msi_data->lock, irqflags);
        return -1;
    }

    for (i = 0; i < nvec; i++) {
        desc = msi_desc_alloc_internal();
        if (!desc) {
            goto cleanup;
        }

        desc->dev = dev;
        desc->hwirq = hwirq_base + i;
        desc->msi_attrib = flags & 0xFFFF;
        desc->irq = irq_create_mapping(dev->msi_domain, desc->hwirq);
        if (!desc->irq) {
            kfree(desc);
            goto cleanup;
        }

        if (msi_desc_list_add_locked(msi_data, desc) < 0) {
            irq_dispose_mapping(desc->irq);
            kfree(desc);
            goto cleanup;
        }
    }

    spin_unlock_irqrestore(&msi_data->lock, irqflags);
    return nvec;

cleanup:
    // Free already allocated descriptors and mappings
    while (i > 0) {
        i--;
        // This is inefficient, but required for cleanup.
        // A better approach would be to have a way to find a descriptor by hwirq.
        struct msi_desc *d = (struct msi_desc *)msi_data->list.next;
        while (&d->list != &msi_data->list) {
            if (d->hwirq == hwirq_base + i) {
                irq_dispose_mapping(d->irq);
                d->list.next->prev = d->list.prev;
                d->list.prev->next = d->list.next;
                msi_data->num_vectors--;
                kfree(d);
                break;
            }
            d = (struct msi_desc *)d->list.next;
        }
    }
    irq_domain_free_hwirq_range(dev->msi_domain, hwirq_base, nvec);
    spin_unlock_irqrestore(&msi_data->lock, irqflags);
    return -1;
}

// Free all MSI vectors for a device
void msi_free_vectors(struct device *dev) {
    struct msi_device_data *msi_data;
    struct msi_desc *desc, *next;
    unsigned long flags;

    if (!dev || !dev->msi_data || !dev->msi_domain) {
        return;
    }

    msi_data = dev->msi_data;

    spin_lock_irqsave(&msi_data->lock, flags);

    desc = (struct msi_desc *)msi_data->list.next;
    while (&desc->list != &msi_data->list) {
        next = (struct msi_desc *)desc->list.next;

        if (desc->irq) {
            irq_dispose_mapping(desc->irq);
        }
        // TODO: This is inefficient. A better approach would be to find
        // contiguous ranges and free them in batches.
        irq_domain_free_hwirq_range(dev->msi_domain, desc->hwirq, 1);

        desc->list.next->prev = desc->list.prev;
        desc->list.prev->next = desc->list.next;
        msi_data->num_vectors--;

        kfree(desc);
        desc = next;
    }

    spin_unlock_irqrestore(&msi_data->lock, flags);
}

// Compose MSI message
void msi_compose_msg(struct msi_desc *desc, struct msi_msg *msg) {
    if (!desc || !msg) {
        return;
    }
    
    *msg = desc->msg;
}

// Write MSI message to descriptor
void msi_write_msg(struct msi_desc *desc, struct msi_msg *msg) {
    if (!desc || !msg) {
        return;
    }
    
    desc->msg = *msg;
}

// Mask MSI interrupt
void msi_mask_irq(struct msi_desc *desc) {
    if (!desc || !desc->irq) {
        return;
    }
    
    disable_irq_nosync(desc->irq);
}

// Unmask MSI interrupt
void msi_unmask_irq(struct msi_desc *desc) {
    if (!desc || !desc->irq) {
        return;
    }
    
    enable_irq(desc->irq);
}

// Set MSI affinity (stub for now)
int msi_set_affinity(struct msi_desc *desc, uint32_t cpu_mask) {
    (void)desc;
    (void)cpu_mask;
    // TODO: Implement when SMP support is added
    return 0;
}

// Create MSI domain (stub for now)
struct irq_domain *msi_create_domain(struct device_node *node,
                                    struct msi_domain_info *info,
                                    struct irq_domain *parent) {
    (void)node;
    (void)info;
    (void)parent;
    // TODO: Implement MSI domain hierarchy
    return NULL;
}