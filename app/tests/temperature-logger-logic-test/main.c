#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "app/nvs.h"
#include "app/config-settings.h"
#include "app/wifi.h"
#include "app/temperature-logger.h"
#include "./../../src/temperature-logger.c"  // already has a logger set up

// LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

// Converts the 4-bit fixed-point temperature back to float for printing
float temp_to_float(temperature_t t)
{
    // 16 is 2^4 (4 fractional bits)
    return (float)t / 16.0f;
}

// Utility function to print a list
void print_list(const char *name, const struct temperature_list_t *list)
{
    LOG_INF("--- List: %s (Length: %zu) ---", name, list->length);
    for (size_t i = 0; i < list->length; i++)
    {
        float temp = temp_to_float(list->data[i].temperature);
        LOG_INF("[%02zu] Uptime: %u min, Temp: %.2f (Raw: %d)",
                i, list->data[i].uptime, (double)temp, list->data[i].temperature);
    }
}

// Utility function to reset list (without locking the mutex)
void reset_list_data(struct temperature_list_t *t)
{
    memset(t->data, 0, sizeof(t->data));
    t->length = 0;
}

// --- TEST CASES ---

void run_test_cases(struct temperature_list_t *src1, struct temperature_list_t *src2, struct temperature_list_t *dest)
{
    enum error_e err;

    // =======================================================================
    // TEST CASE 1: Sequential Merge (No Overlap, Perfect Uniform Sampling)
    // =======================================================================
    LOG_INF("\n\n=============== STARTING TEST CASE 1: Sequential Merge ===============");
    reset_list_data(src1);
    reset_list_data(src2);
    reset_list_data(dest);

    // src1: 10 min, 10.0°C (160 raw) -> 20 min, 30.0°C (480 raw)
    src1->data[0] = (struct temperature_sample_t){.uptime = 10, .temperature = 160};
    src1->data[1] = (struct temperature_sample_t){.uptime = 20, .temperature = 480};
    src1->length = 2;

    // src2: 30 min, 50.0°C (800 raw) -> 40 min, 70.0°C (1120 raw)
    src2->data[0] = (struct temperature_sample_t){.uptime = 30, .temperature = 800};
    src2->data[1] = (struct temperature_sample_t){.uptime = 40, .temperature = 1120};
    src2->length = 2;

    // We assume CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE >= 4
    err = merge_temperature_lists(src1, src2, dest);

    LOG_INF("Test 1 Inputs:");
    print_list("Src1", src1);
    print_list("Src2", src2);

    if (err == E_SUCCESS)
    {
        LOG_INF("TEST 1 SUCCESS: Sequential Merge completed. Expected length 4.");
        print_list("Result", dest);
    }
    else
    {
        LOG_ERR("TEST 1 FAILED with error: %d", err);
    }

    // =======================================================================
    // TEST CASE 2: Overlap and Interpolation
    // Goal: Test iterator hopping and non-exact-point interpolation.
    // =======================================================================
    LOG_INF("\n\n=============== STARTING TEST CASE 2: Overlap and Interpolation ===============");
    reset_list_data(src1);
    reset_list_data(src2);
    reset_list_data(dest);

    // src1 (Slow change): 10 min, 10.0°C (160) -> 50 min, 50.0°C (800)
    src1->data[0] = (struct temperature_sample_t){.uptime = 10, .temperature = 160};
    src1->data[1] = (struct temperature_sample_t){.uptime = 50, .temperature = 800};
    src1->length = 2;

    // src2 (Fast change): 20 min, 20.0°C (320) -> 30 min, 40.0°C (640)
    src2->data[0] = (struct temperature_sample_t){.uptime = 20, .temperature = 320};
    src2->data[1] = (struct temperature_sample_t){.uptime = 30, .temperature = 640};
    src2->length = 2;

    // Expected: start=10, end=50. Duration=40 min. Target length=5. Period=8 min.
    // Merge Uptime points: 10, 18, 26, 34, 42

    err = merge_temperature_lists(src1, src2, dest);

    LOG_INF("Test 2 Inputs:");
    print_list("Src1", src1);
    print_list("Src2", src2);

    if (err == E_SUCCESS)
    {
        LOG_INF("TEST 2 SUCCESS: Overlap Merge completed. Expected length 5.");
        print_list("Result", dest);
    }
    else
    {
        LOG_ERR("TEST 2 FAILED with error: %d", err);
    }

    // =======================================================================
    // TEST CASE 3: Edge Case (Copy-Only)
    // =======================================================================
    LOG_INF("\n\n=============== STARTING TEST CASE 3: Copy-Only Check ===============");
    reset_list_data(src1);
    reset_list_data(src2);
    reset_list_data(dest);

    // Case 3A: Both empty
    err = merge_temperature_lists(src1, src2, dest);
    if (err == E_SUCCESS && dest->length == 0)
    {
        LOG_INF("TEST 3A SUCCESS: Both empty lists result in empty dest.");
    }
    else
    {
        LOG_ERR("TEST 3A FAILED: Expected success and 0 length, got err=%d, len=%zu", err, dest->length);
    }

    // Case 3B: Only src1 empty (src2 copied to dest)
    reset_list_data(src2);
    src2->data[0] = (struct temperature_sample_t){.uptime = 1, .temperature = 100};
    src2->length = 1;
    err = merge_temperature_lists(src1, src2, dest);
    if (err == E_SUCCESS && dest->length == 1)
    {
        LOG_INF("TEST 3B SUCCESS: One empty list resulted in direct copy.");
    }
    else
    {
        LOG_ERR("TEST 3B FAILED: Expected success and 1 length, got err=%d, len=%zu", err, dest->length);
    }
}

int main(void)
{
    // Initialize test list structs with Z_MUTEX_INITIALIZER
    // Since these are local to main, they won't conflict with global locks.
    static struct temperature_list_t src1 = {.lock = Z_MUTEX_INITIALIZER(src1.lock)};
    static struct temperature_list_t src2 = {.lock = Z_MUTEX_INITIALIZER(src2.lock)};
    static struct temperature_list_t dest = {.lock = Z_MUTEX_INITIALIZER(dest.lock)};

    LOG_INF("Starting temperature list merge tests...");

    run_test_cases(&src1, &src2, &dest);

    LOG_INF("All tests finished.");

    return 0;
}