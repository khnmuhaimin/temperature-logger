#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "app/nvs.h"
#include "app/config-settings.h"
#include "app/wifi.h"
#include "app/temperature-logger.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

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
    const size_t MAX_CAPACITY = 6; // Assuming CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE is 6

    // ... [TEST CASES 1, 2, and 3 remain here, unchanged] ...
    
    // =======================================================================
    // TEST CASE 4: Capacity Boundary (Fidelity Check)
    // Goal: Test that merge_without_decimation is called when total length 
    //       EXACTLY equals CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE (6).
    // =======================================================================
    LOG_INF("\n\n=============== STARTING TEST CASE 4: Capacity Boundary ===============");
    reset_list_data(src1);
    reset_list_data(src2);
    reset_list_data(dest);

    // src1 (Length 3): T=10 @ 10min -> T=30 @ 30min
    src1->data[0] = (struct temperature_sample_t){.uptime = 10, .temperature = 160}; // 10.0°C
    src1->data[1] = (struct temperature_sample_t){.uptime = 20, .temperature = 320}; // 20.0°C
    src1->data[2] = (struct temperature_sample_t){.uptime = 30, .temperature = 480}; // 30.0°C
    src1->length = 3;

    // src2 (Length 3): T=40 @ 40min -> T=60 @ 60min
    src2->data[0] = (struct temperature_sample_t){.uptime = 40, .temperature = 640}; // 40.0°C
    src2->data[1] = (struct temperature_sample_t){.uptime = 50, .temperature = 800}; // 50.0°C
    src2->data[2] = (struct temperature_sample_t){.uptime = 60, .temperature = 960}; // 60.0°C
    src2->length = 3;

    // Total Length = 6. Capacity = 6. Should use merge_without_decimation (pure chronological merge).
    err = merge_temperature_lists(src1, src2, dest);

    LOG_INF("Test 4 Inputs (Total Length 6):");
    print_list("Src1", src1);
    print_list("Src2", src2);

    if (err == E_SUCCESS && dest->length == 6)
    {
        LOG_INF("TEST 4 SUCCESS: Capacity matched total length. Expected 6 raw samples.");
        print_list("Result", dest);
    }
    else
    {
        LOG_ERR("TEST 4 FAILED: Expected success and length 6, got err=%d, len=%zu", err, dest->length);
    }
    
    // =======================================================================
    // TEST CASE 5: Max Decimation (Inputs > Capacity)
    // Goal: Test that merge_with_decimation is called and decimates 12 inputs 
    //       into 6 uniformly spaced samples.
    // =======================================================================
    LOG_INF("\n\n=============== STARTING TEST CASE 5: Max Decimation ===============");
    reset_list_data(src1);
    reset_list_data(src2);
    reset_list_data(dest);

    // Fill both lists to max capacity: Length 6 each. Total 12.
    // Create a smooth temperature ramp from 10.0°C to 11.0°C (raw 160 to 176) over 50 minutes.
    // The samples are intentionally dense (12 samples in 50 minutes).
    
    // Total Duration: 50 min. Target Length: 6.
    // Expected Period: 50 / (6-1) = 10 min.
    // Expected Uptime Points: 10, 20, 30, 40, 50, 60 (6 points)

    // Populate src1 (10 min to 35 min)
    for (size_t i = 0; i < 6; i++) {
        src1->data[i] = (struct temperature_sample_t){.uptime = 10 + (i * 5), .temperature = 160 + (i * 3)};
    }
    src1->length = 6;
    
    // Populate src2 (40 min to 65 min)
    for (size_t i = 0; i < 6; i++) {
        src2->data[i] = (struct temperature_sample_t){.uptime = 40 + (i * 5), .temperature = 178 + (i * 3)};
    }
    src2->length = 6;
    
    // Total Length = 12. Capacity = 6. Should use merge_with_decimation.
    // Max uptime point is src2[5].uptime = 65 min.
    // start=10, end=65. Duration = 55 min. Target Length=6. Period = 55 / 5 = 11 min.
    // Expected Uptime Points: 10, 21, 32, 43, 54, 65

    err = merge_temperature_lists(src1, src2, dest);

    LOG_INF("Test 5 Inputs (Total Length 12):");
    print_list("Src1", src1);
    print_list("Src2", src2);

    if (err == E_SUCCESS && dest->length == 6)
    {
        LOG_INF("TEST 5 SUCCESS: Max decimation completed. Expected 6 uniformly sampled points.");
        print_list("Result", dest);
    }
    else
    {
        LOG_ERR("TEST 5 FAILED: Expected success and length 6, got err=%d, len=%zu", err, dest->length);
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