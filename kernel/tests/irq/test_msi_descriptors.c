#include <irq/msi.h>
#include <irq/irq.h>
#include <device/device.h>
#include <memory/kmalloc.h>
#include <uart.h>
#include <panic.h>
#include <string.h>

// Test statistics
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int assertions_run = 0;

// Helper macros for test assertions
#define TEST_ASSERT(condition, msg) do { \
    assertions_run++; \
    if (!(condition)) { \
        uart_puts("  [FAIL] "); \
        uart_puts(msg); \
        uart_puts("\n"); \
        return -1; \
    } \
} while(0)

#define TEST_ASSERT_EQ(actual, expected, msg) do { \
    assertions_run++; \
    if ((actual) != (expected)) { \
        uart_puts("  [FAIL] "); \
        uart_puts(msg); \
        uart_puts(" (expected "); \
        uart_putdec(expected); \
        uart_puts(", got "); \
        uart_putdec(actual); \
        uart_puts(")\n"); \
        return -1; \
    } \
} while(0)

#define TEST_ASSERT_NE(actual, not_expected, msg) do { \
    assertions_run++; \
    if ((actual) == (not_expected)) { \
        uart_puts("  [FAIL] "); \
        uart_puts(msg); \
        uart_puts("\n"); \
        return -1; \
    } \
} while(0)

// Macro to run a test
#define RUN_TEST(test_func) do { \
    tests_run++; \
    int result = test_func(); \
    if (result == 0) { \
        tests_passed++; \
        print_test_result(result); \
    } else { \
        tests_failed++; \
        print_test_result(result); \
    } \
} while(0)

// Test helper functions
static void print_test_header(const char *test_name) {
    uart_puts("\n[TEST] ");
    uart_puts(test_name);
    uart_puts("\n");
}

static void print_test_result(int result) {
    if (result == 0) {
        uart_puts("  [PASS] Test completed successfully\n");
    } else {
        uart_puts("  [FAIL] Test failed\n");
    }
}

// Helper to create a test device
static struct device *create_test_device(const char *name) {
    struct device *dev = kmalloc(sizeof(struct device), KM_ZERO);
    if (!dev) {
        return NULL;
    }
    
    strncpy(dev->name, name, DEVICE_NAME_MAX - 1);
    dev->id = 0x1000;  // Test ID
    dev->type = DEV_TYPE_PLATFORM;
    
    return dev;
}

// Helper to destroy test device
static void destroy_test_device(struct device *dev) {
    if (!dev) {
        return;
    }
    
    if (dev->msi_data) {
        msi_device_cleanup(dev);
    }
    
    kfree(dev);
}

// Test 1: MSI device initialization
static int test_msi_device_init(void) {
    print_test_header("MSI Device Initialization");
    
    struct device *dev = create_test_device("test_msi_device");
    TEST_ASSERT(dev != NULL, "Failed to create test device");
    
    // Initialize MSI for device
    int ret = msi_device_init(dev);
    TEST_ASSERT_EQ(ret, 0, "Failed to initialize MSI for device");
    TEST_ASSERT(dev->msi_data != NULL, "MSI data not allocated");
    TEST_ASSERT_EQ(dev->msi_data->num_vectors, 0, "Initial vector count should be 0");
    
    // Verify list is initialized
    TEST_ASSERT(dev->msi_data->list.next == &dev->msi_data->list, "List not properly initialized");
    TEST_ASSERT(dev->msi_data->list.prev == &dev->msi_data->list, "List not properly initialized");
    
    // Clean up
    destroy_test_device(dev);
    
    return 0;
}

// Test 2: MSI descriptor allocation
static int test_msi_desc_alloc(void) {
    print_test_header("MSI Descriptor Allocation");
    
    struct device *dev = create_test_device("test_desc_alloc");
    TEST_ASSERT(dev != NULL, "Failed to create test device");
    
    // Allocate descriptor for 1 vector
    struct msi_desc *desc1 = msi_desc_alloc(dev, 1);
    TEST_ASSERT(desc1 != NULL, "Failed to allocate descriptor for 1 vector");
    TEST_ASSERT(desc1->dev == dev, "Device not set correctly");
    TEST_ASSERT_EQ(desc1->refcount, 1, "Initial refcount should be 1");
    TEST_ASSERT_EQ(desc1->multiple, 0, "Multiple should be 0 for 1 vector");
    
    // Allocate descriptor for 4 vectors
    struct msi_desc *desc4 = msi_desc_alloc(dev, 4);
    TEST_ASSERT(desc4 != NULL, "Failed to allocate descriptor for 4 vectors");
    TEST_ASSERT_EQ(desc4->multiple, 2, "Multiple should be 2 for 4 vectors (2^2=4)");
    
    // Allocate descriptor for 16 vectors
    struct msi_desc *desc16 = msi_desc_alloc(dev, 16);
    TEST_ASSERT(desc16 != NULL, "Failed to allocate descriptor for 16 vectors");
    TEST_ASSERT_EQ(desc16->multiple, 4, "Multiple should be 4 for 16 vectors (2^4=16)");
    
    // Test invalid allocations
    struct msi_desc *desc_null = msi_desc_alloc(NULL, 4);
    TEST_ASSERT(desc_null == NULL, "Should fail with NULL device");
    
    struct msi_desc *desc_zero = msi_desc_alloc(dev, 0);
    TEST_ASSERT(desc_zero == NULL, "Should fail with 0 vectors");
    
    struct msi_desc *desc_toomany = msi_desc_alloc(dev, MSI_MAX_VECTORS + 1);
    TEST_ASSERT(desc_toomany == NULL, "Should fail with too many vectors");
    
    // Clean up
    msi_desc_free(desc1);
    msi_desc_free(desc4);
    msi_desc_free(desc16);
    destroy_test_device(dev);
    
    return 0;
}

// Test 3: MSI descriptor list operations
static int test_msi_desc_list_ops(void) {
    print_test_header("MSI Descriptor List Operations");
    
    struct device *dev = create_test_device("test_list_ops");
    TEST_ASSERT(dev != NULL, "Failed to create test device");
    
    int ret = msi_device_init(dev);
    TEST_ASSERT_EQ(ret, 0, "Failed to initialize MSI");
    
    // Create and add descriptors
    struct msi_desc *desc1 = msi_desc_alloc(dev, 1);
    struct msi_desc *desc2 = msi_desc_alloc(dev, 2);
    struct msi_desc *desc3 = msi_desc_alloc(dev, 4);
    
    TEST_ASSERT(desc1 != NULL && desc2 != NULL && desc3 != NULL, "Failed to allocate descriptors");
    
    // Add descriptors to list
    ret = msi_desc_list_add(dev->msi_data, desc1);
    TEST_ASSERT_EQ(ret, 0, "Failed to add first descriptor");
    TEST_ASSERT_EQ(dev->msi_data->num_vectors, 1, "Vector count should be 1");
    TEST_ASSERT_EQ(desc1->refcount, 2, "Refcount should increase to 2");
    
    ret = msi_desc_list_add(dev->msi_data, desc2);
    TEST_ASSERT_EQ(ret, 0, "Failed to add second descriptor");
    TEST_ASSERT_EQ(dev->msi_data->num_vectors, 2, "Vector count should be 2");
    
    ret = msi_desc_list_add(dev->msi_data, desc3);
    TEST_ASSERT_EQ(ret, 0, "Failed to add third descriptor");
    TEST_ASSERT_EQ(dev->msi_data->num_vectors, 3, "Vector count should be 3");
    
    // Test invalid adds
    ret = msi_desc_list_add(NULL, desc1);
    TEST_ASSERT_EQ(ret, -1, "Should fail with NULL msi_data");
    
    ret = msi_desc_list_add(dev->msi_data, NULL);
    TEST_ASSERT_EQ(ret, -1, "Should fail with NULL descriptor");
    
    // Clean up
    destroy_test_device(dev);
    
    return 0;
}

// Test 4: MSI vector allocation
static int test_msi_vector_allocation(void) {
    print_test_header("MSI Vector Allocation");
    
    struct device *dev = create_test_device("test_vector_alloc");
    TEST_ASSERT(dev != NULL, "Failed to create test device");
    
    int ret = msi_device_init(dev);
    TEST_ASSERT_EQ(ret, 0, "Failed to initialize MSI");
    
    // Allocate single vector
    ret = msi_alloc_vectors(dev, 1, 1, 0);
    TEST_ASSERT_EQ(ret, 1, "Should allocate 1 vector");
    TEST_ASSERT_EQ(dev->msi_data->num_vectors, 1, "Should have 1 vector allocated");
    
    // Free vectors
    msi_free_vectors(dev);
    TEST_ASSERT_EQ(dev->msi_data->num_vectors, 0, "Should have 0 vectors after free");
    
    // Allocate multiple vectors
    ret = msi_alloc_vectors(dev, 4, 8, MSI_FLAG_MULTI_VECTOR);
    TEST_ASSERT_EQ(ret, 8, "Should allocate 8 vectors (max)");
    TEST_ASSERT_EQ(dev->msi_data->num_vectors, 8, "Should have 8 vectors allocated");
    
    // Free vectors again
    msi_free_vectors(dev);
    TEST_ASSERT_EQ(dev->msi_data->num_vectors, 0, "Should have 0 vectors after free");
    
    // Test with USE_DEF_NUM_VECS flag
    ret = msi_alloc_vectors(dev, 2, 16, MSI_FLAG_USE_DEF_NUM_VECS | MSI_FLAG_MULTI_VECTOR);
    TEST_ASSERT_EQ(ret, 2, "Should allocate 2 vectors (min with USE_DEF_NUM_VECS)");
    TEST_ASSERT_EQ(dev->msi_data->num_vectors, 2, "Should have 2 vectors allocated");
    
    // Clean up
    destroy_test_device(dev);
    
    return 0;
}

// Test 5: MSI cleanup
static int test_msi_device_cleanup(void) {
    print_test_header("MSI Device Cleanup");
    
    struct device *dev = create_test_device("test_cleanup");
    TEST_ASSERT(dev != NULL, "Failed to create test device");
    
    int ret = msi_device_init(dev);
    TEST_ASSERT_EQ(ret, 0, "Failed to initialize MSI");
    
    // Allocate some vectors
    ret = msi_alloc_vectors(dev, 4, 4, MSI_FLAG_MULTI_VECTOR);
    TEST_ASSERT_EQ(ret, 4, "Should allocate 4 vectors");
    
    // Clean up device MSI
    msi_device_cleanup(dev);
    TEST_ASSERT(dev->msi_data == NULL, "MSI data should be NULL after cleanup");
    
    // Test cleanup of NULL device
    msi_device_cleanup(NULL);  // Should not crash
    
    // Test cleanup of device without MSI
    struct device *dev2 = create_test_device("test_no_msi");
    msi_device_cleanup(dev2);  // Should not crash
    
    // Clean up
    kfree(dev);
    kfree(dev2);
    
    return 0;
}

// Test 6: MSI message operations
static int test_msi_message_ops(void) {
    print_test_header("MSI Message Operations");
    
    struct device *dev = create_test_device("test_msg_ops");
    TEST_ASSERT(dev != NULL, "Failed to create test device");
    
    struct msi_desc *desc = msi_desc_alloc(dev, 1);
    TEST_ASSERT(desc != NULL, "Failed to allocate descriptor");
    
    // Test message writing
    struct msi_msg msg = {
        .address_lo = 0xFEE00000,
        .address_hi = 0,
        .data = 0x1234
    };
    
    msi_write_msg(desc, &msg);
    TEST_ASSERT_EQ(desc->msg.address_lo, 0xFEE00000, "Address low not set correctly");
    TEST_ASSERT_EQ(desc->msg.address_hi, 0, "Address high not set correctly");
    TEST_ASSERT_EQ(desc->msg.data, 0x1234, "Data not set correctly");
    
    // Test message composition
    struct msi_msg composed_msg;
    msi_compose_msg(desc, &composed_msg);
    TEST_ASSERT_EQ(composed_msg.address_lo, 0xFEE00000, "Composed address low incorrect");
    TEST_ASSERT_EQ(composed_msg.address_hi, 0, "Composed address high incorrect");
    TEST_ASSERT_EQ(composed_msg.data, 0x1234, "Composed data incorrect");
    
    // Test with NULL parameters
    msi_write_msg(NULL, &msg);  // Should not crash
    msi_write_msg(desc, NULL);  // Should not crash
    msi_compose_msg(NULL, &composed_msg);  // Should not crash
    msi_compose_msg(desc, NULL);  // Should not crash
    
    // Clean up
    msi_desc_free(desc);
    destroy_test_device(dev);
    
    return 0;
}

// Test 7: Memory leak detection
static int test_memory_leak_detection(void) {
    print_test_header("Memory Leak Detection");
    
    // Perform multiple allocation/free cycles
    int i;
    for (i = 0; i < 10; i++) {
        struct device *dev = create_test_device("test_leak");
        TEST_ASSERT(dev != NULL, "Failed to create test device");
        
        int ret = msi_device_init(dev);
        TEST_ASSERT_EQ(ret, 0, "Failed to initialize MSI");
        
        // Allocate and free vectors multiple times
        ret = msi_alloc_vectors(dev, 8, 8, MSI_FLAG_MULTI_VECTOR);
        TEST_ASSERT_EQ(ret, 8, "Should allocate 8 vectors");
        
        msi_free_vectors(dev);
        
        ret = msi_alloc_vectors(dev, 16, 16, MSI_FLAG_MULTI_VECTOR);
        TEST_ASSERT_EQ(ret, 16, "Should allocate 16 vectors");
        
        // Clean up
        destroy_test_device(dev);
    }
    
    uart_puts("  [INFO] Completed 10 allocation/free cycles\n");
    uart_puts("  [INFO] Memory leak detection relies on external monitoring\n");
    
    return 0;
}

// Main test runner
void test_msi_descriptors(void) {
    uart_puts("\n================== MSI DESCRIPTOR TESTS ==================\n");
    
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;
    assertions_run = 0;
    
    // Run all tests
    RUN_TEST(test_msi_device_init);
    RUN_TEST(test_msi_desc_alloc);
    RUN_TEST(test_msi_desc_list_ops);
    RUN_TEST(test_msi_vector_allocation);
    RUN_TEST(test_msi_device_cleanup);
    RUN_TEST(test_msi_message_ops);
    RUN_TEST(test_memory_leak_detection);
    
    // Print summary
    uart_puts("\n================ MSI DESCRIPTOR TEST SUMMARY ================\n");
    uart_puts("Tests run:       "); uart_putdec(tests_run); uart_puts("\n");
    uart_puts("Tests passed:    "); uart_putdec(tests_passed); uart_puts("\n");
    uart_puts("Tests failed:    "); uart_putdec(tests_failed); uart_puts("\n");
    uart_puts("Assertions run:  "); uart_putdec(assertions_run); uart_puts("\n");
    
    if (tests_failed == 0) {
        uart_puts("\n[SUCCESS] All MSI descriptor tests passed!\n");
    } else {
        uart_puts("\n[FAILURE] Some MSI descriptor tests failed\n");
    }
    
    uart_puts("==============================================================\n");
}