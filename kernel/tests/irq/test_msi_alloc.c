#include <irq/msi.h>
#include <irq/irq_domain.h>
#include <irq/irq.h>
#include <device/device.h>
#include <memory/kmalloc.h>
#include <uart.h>
#include <string.h>

// Test statistics
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT_EQ(actual, expected, msg) do { \
    if ((actual) != (expected)) { \
        uart_puts("  [FAIL] "); uart_puts(msg); \
        uart_puts(" (expected "); uart_putdec(expected); \
        uart_puts(", got "); uart_putdec(actual); uart_puts(")\n"); \
        return -1; \
    } \
} while(0)

#define TEST_ASSERT_NE(actual, not_expected, msg) do { \
    if ((actual) == (not_expected)) { \
        uart_puts("  [FAIL] "); uart_puts(msg); \
        uart_puts(" (unexpectedly got "); uart_putdec(not_expected); uart_puts(")\n"); \
        return -1; \
    } \
} while(0)

#define TEST_ASSERT_TRUE(condition, msg) do { \
    if (!(condition)) { \
        uart_puts("  [FAIL] "); uart_puts(msg); uart_puts("\n"); \
        return -1; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    tests_run++; \
    uart_puts("[RUN]    "); uart_puts(#test_func); uart_puts("\n"); \
    int result = test_func(); \
    if (result == 0) { \
        tests_passed++; \
        uart_puts("  [PASS] "); uart_puts(#test_func); uart_puts("\n"); \
    } else { \
        tests_failed++; \
    } \
} while(0)

static struct irq_domain *msi_domain;
static struct device *test_dev;

static int setup(void) {
    msi_domain = irq_domain_create_tree(NULL, NULL, NULL);
    if (!msi_domain) return -1;
    test_dev = kmalloc(sizeof(*test_dev), KM_ZERO);
    if (!test_dev) {
        irq_domain_remove(msi_domain);
        return -1;
    }
    msi_device_init(test_dev);
    test_dev->msi_domain = msi_domain;
    return 0;
}

static void teardown(void) {
    msi_free_vectors(test_dev);
    msi_device_cleanup(test_dev);
    kfree(test_dev);
    irq_domain_remove(msi_domain);
}

static int test_invalid_parameters(void) {
    if (setup() != 0) return -1;
    TEST_ASSERT_EQ(msi_alloc_vectors(test_dev, 0, 5, 0), -1, "min_vecs cannot be 0");
    TEST_ASSERT_EQ(msi_alloc_vectors(test_dev, 5, 4, 0), -1, "min_vecs cannot be > max_vecs");
    TEST_ASSERT_EQ(msi_alloc_vectors(test_dev, 33, 33, 0), -1, "Cannot allocate > MSI_MAX_VECTORS");
    teardown();
    return 0;
}

static int test_power_of_two_allocation(void) {
    if (setup() != 0) return -1;
    TEST_ASSERT_EQ(msi_alloc_vectors(test_dev, 3, 7, 0), 4, "Allocating 3-7 should yield 4 vectors");
    msi_free_vectors(test_dev);
    TEST_ASSERT_EQ(msi_alloc_vectors(test_dev, 8, 15, 0), 8, "Allocating 8-15 should yield 8 vectors");
    msi_free_vectors(test_dev);
    TEST_ASSERT_EQ(msi_alloc_vectors(test_dev, 16, 31, 0), 16, "Allocating 16-31 should yield 16 vectors");
    msi_free_vectors(test_dev);
    TEST_ASSERT_EQ(msi_alloc_vectors(test_dev, 7, 7, 0), -1, "Requesting 7 (not power of 2) should fail if min_vecs is also 7");
    teardown();
    return 0;
}

static int test_allocation_state_verification(void) {
    if (setup() != 0) return -1;
    int nvec = msi_alloc_vectors(test_dev, 8, 8, 0);
    TEST_ASSERT_EQ(nvec, 8, "Should allocate 8 vectors");

    struct msi_device_data *msi_data = test_dev->msi_data;
    TEST_ASSERT_EQ(msi_data->num_vectors, 8, "Device data should report 8 vectors");

    struct msi_desc *desc;
    uint32_t last_hwirq = 0;
    int count = 0;
    for (desc = (struct msi_desc *)msi_data->list.next; &desc->list != &msi_data->list; desc = (struct msi_desc *)desc->list.next) {
        if (count > 0) {
            TEST_ASSERT_EQ(desc->hwirq, last_hwirq + 1, "hwirqs should be consecutive");
        }
        TEST_ASSERT_NE(desc->irq, 0, "VIRQ should be mapped");
        last_hwirq = desc->hwirq;
        count++;
    }
    TEST_ASSERT_EQ(count, 8, "Should find 8 descriptors in list");
    teardown();
    return 0;
}

static int test_stress_allocation_and_free(void) {
    if (setup() != 0) return -1;
    for (int i = 0; i < 100; i++) {
        int nvec = 1 << (i % 5); // 1, 2, 4, 8, 16
        int ret = msi_alloc_vectors(test_dev, nvec, nvec, 0);
        TEST_ASSERT_EQ(ret, nvec, "Stress alloc failed");
        msi_free_vectors(test_dev);
        TEST_ASSERT_EQ(test_dev->msi_data->num_vectors, 0, "Vectors not freed correctly in stress test");
    }
    teardown();
    return 0;
}

static int test_fragmentation_and_allocation(void) {
    if (setup() != 0) return -1;
    
    // Allocate 8 single vectors to create fragmentation
    struct msi_desc *descs[8];
    for(int i = 0; i < 8; i++) {
        descs[i] = NULL; // Ensure it's null
        int ret = msi_alloc_vectors(test_dev, 1, 1, 0);
        TEST_ASSERT_EQ(ret, 1, "Frag alloc failed");
        descs[i] = (struct msi_desc *)test_dev->msi_data->list.prev;
    }

    // Free every other vector
    for(int i = 0; i < 8; i += 2) {
        irq_dispose_mapping(descs[i]->irq);
        irq_domain_free_hwirq_range(test_dev->msi_domain, descs[i]->hwirq, 1);
        descs[i]->list.prev->next = descs[i]->list.next;
        descs[i]->list.next->prev = descs[i]->list.prev;
        test_dev->msi_data->num_vectors--;
        kfree(descs[i]);
    }

    // Try to allocate 4 vectors, it should succeed by finding a new block
    int ret = msi_alloc_vectors(test_dev, 4, 4, 0);
    TEST_ASSERT_EQ(ret, 4, "Allocation after fragmentation failed");

    teardown();
    return 0;
}


void test_msi_allocation_runner(void) {
    uart_puts("\n========== COMPREHENSIVE MSI ALLOCATION TESTS ==========\n");
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;

    RUN_TEST(test_invalid_parameters);
    RUN_TEST(test_power_of_two_allocation);
    RUN_TEST(test_allocation_state_verification);
    RUN_TEST(test_stress_allocation_and_free);
    RUN_TEST(test_fragmentation_and_allocation);

    uart_puts("\n============== MSI ALLOCATION TEST SUMMARY ===============\n");
    uart_puts("Tests run:       "); uart_putdec(tests_run); uart_puts("\n");
    uart_puts("Tests passed:    "); uart_putdec(tests_passed); uart_puts("\n");
    uart_puts("Tests failed:    "); uart_putdec(tests_failed); uart_puts("\n");
    uart_puts("==========================================================\n");
}
