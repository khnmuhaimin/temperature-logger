#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include "app/error.h"
#include "app/temperature-logger.h"
#include "app/nvs.h"
#include "app/lock.h"
#include "app/test.h"

LOG_MODULE_REGISTER(temp_log, LOG_LEVEL_DBG);

struct temperature_logger_data_t
{
    struct temperature_list_t temperature_list;
    struct temperature_list_t scratch_temperature_list; /* used when operating on nvs data */
    struct device *temperature_sensor;
    struct k_work_delayable sampling_task;
};

static void perform_sampling_task(struct k_work *work);

static struct temperature_logger_data_t t_data = {
    .temperature_list.lock = Z_MUTEX_INITIALIZER(t_data.temperature_list.lock),
    .scratch_temperature_list.lock = Z_MUTEX_INITIALIZER(t_data.scratch_temperature_list.lock),
    .temperature_sensor = DEVICE_DT_GET_ANY(maxim_ds18b20),
    .sampling_task = Z_WORK_DELAYABLE_INITIALIZER(perform_sampling_task)};


/**
 * @brief Initializes the temperature logging subsystem.
 * * This includes checking device readiness and scheduling the first sampling task 
 * via the system workqueue. This is the main exposed entry point.
 * * @retval E_SUCCESS Successful initialization.
 * @retval E_ERROR Sensor device not found or not ready.
 */
enum error_e init_temperature_logger(void)
{
    if (t_data.temperature_sensor == NULL)
    {
        LOG_ERR("Temperature sensor device was not found.");
        return E_ERROR;
    }

    if (!device_is_ready(t_data.temperature_sensor))
    {
        LOG_ERR("Temperature sensor device is not ready.");
        return E_ERROR;
    }

    k_work_reschedule(&t_data.sampling_task, K_NO_WAIT);
    return E_SUCCESS;
}

/**
 * @brief Resets a temperature list, zeroing out the data array and setting length to 0.
 * * ASSUMPTION: The caller (perform_sampling_task) MUST hold the list's lock before calling.
 * * @param t Pointer to the temperature list structure to reset.
 * @retval E_SUCCESS List successfully reset.
 * @retval E_NULL_PTR If 't' is NULL.
 */
EXPOSE_FOR_TESTING enum error_e reset_temperature_list(struct temperature_list_t *t)
{
    if (t == NULL)
    {
        LOG_ERR(GENERIC_NULL_PTR_ERROR_MESSAGE);
        return E_NULL_PTR;
    }
    memset(t, 0, offsetof(struct temperature_list_t, lock));
    return E_SUCCESS;
}

/**
 * @brief Reads the temperature list from NVS flash storage into the provided list structure.
 * * If the key does not exist, the list is reset and a zeroed structure is written.
 * ASSUMPTION: The caller MUST hold the list's lock before calling.
 * * @param t Pointer to the list structure where data will be loaded.
 * @retval E_SUCCESS Data successfully loaded or key created.
 * @retval E_ERROR Read failed due to NVS error or size mismatch.
 * @retval E_NULL_PTR If 't' is NULL.
 */
EXPOSE_FOR_TESTING enum error_e load_temperature_list(struct temperature_list_t *t)
{
    if (t == NULL)
    {
        LOG_ERR(GENERIC_NULL_PTR_ERROR_MESSAGE);
        return E_NULL_PTR;
    }

    struct nvs_fs *fs = get_nvs_fs();
    enum error_e err;

    // try to read
    ssize_t bytes_read = nvs_read(fs, NVS_KEY_TEMPERATURE_DATA, t, offsetof(struct temperature_list_t, lock));
    if (bytes_read == -ENOENT)
    {
        // key does not exist. create key
        reset_temperature_list(t);
        nvs_write(fs, NVS_KEY_TEMPERATURE_DATA, t, offsetof(struct temperature_list_t, lock));
        err = E_SUCCESS;
    }
    else if (bytes_read != offsetof(struct temperature_list_t, lock))
    {
        // for some reason, the read failed
        LOG_ERR("Failed to load temperature list from NVS. Expected to read %d bytes. Read %d bytes.", (int)offsetof(struct temperature_list_t, lock), (int)bytes_read);
        err = E_ERROR;
    }
    else
    {
        err = E_SUCCESS;
    }
    return err;
}

/**
 * @brief Writes the temperature list to NVS flash storage.
 * * ASSUMPTION: The caller MUST hold the list's lock before calling.
 * * @param t Pointer to the list structure whose data will be stored.
 * @retval E_SUCCESS Data successfully written.
 * @retval E_ERROR Write failed due to NVS error.
 * @retval E_NULL_PTR If 't' is NULL.
 */
EXPOSE_FOR_TESTING enum error_e store_temperature_list(struct temperature_list_t *t)
{
    if (t == NULL)
    {
        LOG_ERR(GENERIC_NULL_PTR_ERROR_MESSAGE);
        return E_NULL_PTR;
    }

    struct nvs_fs *fs = get_nvs_fs();
    ssize_t bytes_written = nvs_write(fs, NVS_KEY_TEMPERATURE_DATA, t, offsetof(struct temperature_list_t, lock));
    enum error_e err = bytes_written == offsetof(struct temperature_list_t, lock) || bytes_written == 0 ? E_SUCCESS : E_ERROR;
    if (err != E_SUCCESS)
    {
        LOG_ERR("Failed to write temperature list to NVS. Expected to write %d bytes or 0 bytes. Wrote %d bytes.", (int)offsetof(struct temperature_list_t, lock), (int)bytes_written);
    }
    return err;
}

/**
 * @brief Converts a standard sensor_value struct (val1=whole, val2=micro) into 
 * the 4-bit fixed-point temperature_t format.
 * * @param v The sensor_value struct containing raw temperature readings.
 * @return temperature_t The temperature value scaled by 16 (4 fractional bits).
 */
EXPOSE_FOR_TESTING temperature_t sensor_value_to_temperature(struct sensor_value v)
{
    float whole = (float)v.val1;
    float fractional = (float)v.val2 / 1000000.0f;
    float float_temperature = whole + fractional;
    temperature_t temperature = (temperature_t)(float_temperature * 16);
    return temperature;
}

/**
 * @brief Fetches data from the sensor and creates a single temperature sample.
 * * Reads the current sensor value and combines it with the current system uptime (in minutes).
 * * @param t Pointer to the temperature_sample_t structure to fill.
 * @retval E_SUCCESS Sample successfully acquired and prepared.
 * @retval E_ERROR Sensor fetching/reading failed.
 * @retval E_NULL_PTR If 't' is NULL.
 */
EXPOSE_FOR_TESTING enum error_e get_temperature_sample(struct temperature_sample_t *t)
{
    if (t == NULL)
    {
        LOG_ERR(GENERIC_NULL_PTR_ERROR_MESSAGE);
        return E_NULL_PTR;
    }
    struct sensor_value temperature;
    int result = sensor_sample_fetch(t_data.temperature_sensor);
    t->uptime = get_uptime_in_minutes();
    if (result != 0)
    {
        LOG_ERR("Failed to request temperature sample. Error %d.", result);
        return E_ERROR;
    }
    result = sensor_channel_get(t_data.temperature_sensor, SENSOR_CHAN_AMBIENT_TEMP, &temperature);
    if (result != 0)
    {
        LOG_ERR("Failed to read temperature sample. Error %d.", result);
        return E_ERROR;
    }
    t->temperature = sensor_value_to_temperature(temperature);
    return E_SUCCESS;
}

/**
 * @brief Appends a single temperature sample to the end of a list.
 * * ASSUMPTION: The caller MUST hold the list's lock before calling.
 * * @param list Pointer to the destination list.
 * @param sample The temperature sample to append (copied by value).
 * @retval E_SUCCESS Sample successfully appended.
 * @retval E_NOBUFS The list is already full (length == CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE).
 * @retval E_NULL_PTR If 'list' is NULL.
 */
EXPOSE_FOR_TESTING enum error_e append_temperature_sample(struct temperature_list_t *list, struct temperature_sample_t sample)
{
    if (list == NULL)
    {
        LOG_ERR(GENERIC_NULL_PTR_ERROR_MESSAGE);
        return E_NULL_PTR;
    }
    if (list->length == CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE)
    {
        return E_NOBUFS;
    }
    list->data[list->length] = sample;
    list->length++;
    return E_SUCCESS;
}

/**
 * @brief Performs linear interpolation between two samples (t1 and t2) at a given uptime.
 * * Calculates the synthesized temperature and stores it in the result structure.
 * * @param t1 Pointer to the first sample.
 * @param t2 Pointer to the second sample.
 * @param result Pointer to the structure where the synthesized temperature will be stored. 
 * The result->uptime field MUST be set by the caller before calling.
 * @retval E_SUCCESS Interpolation successful.
 * @retval E_NULL_PTR If t1, t2, or result is NULL.
 */
EXPOSE_FOR_TESTING enum error_e interpolate(struct temperature_sample_t *t1, struct temperature_sample_t *t2, struct temperature_sample_t *result)
{
    if (t1 == NULL || t2 == NULL || result == NULL)
    {
        LOG_ERR(GENERIC_NULL_PTR_ERROR_MESSAGE);
        return E_NULL_PTR;
    }
    if (t1->uptime == t2->uptime)
    {
        result->temperature = (t1->temperature + t2->temperature) / 2;
        return E_SUCCESS;
    }
    struct temperature_sample_t *earlier, *later;
    if (t1->uptime < t2->uptime)
    {
        earlier = t1;
        later = t2;
    }
    else
    {
        earlier = t2;
        later = t1;
    }

    int32_t d_temp = (int32_t)t2->temperature - (int32_t)t1->temperature;
    uint32_t d_uptime = (uint32_t)(later->uptime - earlier->uptime);
    uint32_t d_uptime_to_result = (uint32_t)(result->uptime - earlier->uptime);
    int16_t d_temp_to_result = (int16_t)((float)d_temp * (float)d_uptime_to_result / (float)d_uptime);
    result->temperature = (temperature_t)(earlier->temperature + d_temp_to_result);
    return E_SUCCESS;
}

/**
 * @brief Initializes the merge iterator for two source lists.
 * * Sets the initial current list and indices based on which list has the earliest sample.
 * ASSUMPTION: The caller MUST hold the lists' locks before calling.
 * * @param m Pointer to the merge_iterator_t structure to initialize.
 * @param src1 Pointer to the first source list.
 * @param src2 Pointer to the second source list.
 * @retval E_SUCCESS Initialization complete.
 * @retval E_NULL_PTR If any input pointer is NULL.
 */
EXPOSE_FOR_TESTING enum error_e init_merge_iterator(struct merge_iterator_t *m, struct temperature_list_t *src1, struct temperature_list_t *src2)
{
    if (m == NULL || src1 == NULL || src2 == NULL)
    {
        LOG_ERR(GENERIC_NULL_PTR_ERROR_MESSAGE);
        return E_NULL_PTR;
    }
    m->src1 = src1;
    m->src2 = src2;
    m->current_index = 0;

    // if both lists have length zero
    // will return E_END_OF_ITER forever
    if (src1->length == 0 && src2->length == 0)
    {
        m->current_list = NULL;
        return;
    }
    else if (src1->length == 0)
    {
        m->current_list = src2;
        m->next_src1_index = 0;
        m->next_src2_index = 1;
        return;
    }
    else if (src2->length == 0)
    {
        m->current_list = src1;
        m->next_src1_index = 1;
        m->next_src2_index = 0;
        return;
    }
    // current starts at the earliest datapoint
    else if (src1->data[0].uptime < src2->data[0].uptime)
    {
        m->current_list = src1;
        m->next_src1_index = 1;
        m->next_src2_index = 0;
    }
    else
    {
        m->current_list = src2;
        m->next_src1_index = 0;
        m->next_src2_index = 1;
    }
    return E_SUCCESS;
}

/**
 * @brief Advances the merge iterator and returns a pointer to the chronologically next sample.
 * * @param m Pointer to the merge iterator state.
 * @param sample Output pointer (pointer to a pointer) that will be set to the address of the next sample.
 * @retval E_SUCCESS Sample pointer successfully returned and iterator advanced.
 * @retval E_END_OF_ITER No more samples left in either source list.
 * @retval E_NULL_PTR If 'm' or 'sample' is NULL.
 */
EXPOSE_FOR_TESTING enum error_e merge_iterate(struct merge_iterator_t *m, struct temperature_sample_t **sample)
{

    if (m == NULL || sample == NULL)
    {
        LOG_ERR(GENERIC_NULL_PTR_ERROR_MESSAGE);
        return E_NULL_PTR;
    }
    if (m->current_list == NULL)
    {
        return E_END_OF_ITER;
    }
    else
    {
        *sample = &(m->current_list->data[m->current_index]);
    }

    bool at_src1_end = m->next_src1_index == m->src1->length;
    bool at_src2_end = m->next_src2_index == m->src2->length;

    // if at the end of both lists
    if (at_src1_end && at_src2_end)
    {
        // set to null to return E_END_OF_ITER on next call
        m->current_list = NULL;
    }
    else if (at_src1_end)
    {
        // forced to progress src2
        m->current_list = m->src2;
        m->current_index = m->next_src2_index;
        m->next_src2_index++;
    }
    else if (at_src2_end)
    {
        // forced to progress src1
        m->current_list = m->src1;
        m->current_index = m->next_src1_index;
        m->next_src1_index++;
    }
    else if (m->src1->data[m->next_src1_index].uptime < m->src2->data[m->next_src2_index].uptime)
    {
        m->current_list = m->src1;
        m->current_index = m->next_src1_index;
        m->next_src1_index++;
    }
    else
    {
        m->current_list = m->src2;
        m->current_index = m->next_src2_index;
        m->next_src2_index++;
    }
    return E_SUCCESS;
}

/**
 * @brief Merges two source lists chronologically into a destination list without decimation.
 * * Used when the total number of input elements is less than or equal to the capacity.
 * ASSUMPTION: The caller MUST hold the lists' locks before calling.
 * * @param src1 Pointer to the first source list.
 * @param src2 Pointer to the second source list.
 * @param dest Pointer to the destination list where the full, merged set is copied.
 * @retval E_SUCCESS Merge successful.
 * @retval E_ERROR An error occurred during iteration.
 * @retval E_NULL_PTR If any input pointer is NULL.
 */
EXPOSE_FOR_TESTING enum error_e merge_without_decimation(struct temperature_list_t *src1, struct temperature_list_t *src2, struct temperature_list_t *dest)
{
    if (src1 == NULL || src2 == NULL || dest == NULL)
    {
        LOG_ERR(GENERIC_NULL_PTR_ERROR_MESSAGE);
        return E_NULL_PTR;
    }

    struct temperature_sample_t merged[CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE];
    struct merge_iterator_t iterator;
    struct temperature_sample_t *sample;
    size_t index = 0;
    enum error_e err;

    init_merge_iterator(&iterator, src1, src2);
    while (true)
    {
        err = merge_iterate(&iterator, &sample);
        if (err == E_END_OF_ITER)
        {
            err = E_SUCCESS;
            memmove(dest->data, merged, sizeof(struct temperature_sample_t) * index);
            dest->length = index;
            return E_SUCCESS;
        }
        else if (err != E_SUCCESS)
        {
            LOG_ERR("Merging without interpolation failed. Error %d.", err);
            return E_ERROR;
        }
        merged[index] = *sample;
        index++;
    }
}

/**
 * @brief Merges two source lists into a destination list using uniform interpolation (decimation).
 * * Used when the total number of input elements exceeds the list capacity.
 * The result is a list of CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE samples uniformly spaced by time.
 * ASSUMPTION: The caller MUST hold the lists' locks before calling.
 * * @param src1 Pointer to the first source list.
 * @param src2 Pointer to the second source list.
 * @param dest Pointer to the destination list (output size = CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE).
 * @retval E_SUCCESS Merge successful.
 * @retval E_ERROR An error occurred during iteration or interpolation.
 * @retval E_NULL_PTR If any input pointer is NULL.
 */
EXPOSE_FOR_TESTING enum error_e merge_with_decimation(struct temperature_list_t *src1, struct temperature_list_t *src2, struct temperature_list_t *dest)
{
    if (src1 == NULL || src2 == NULL || dest == NULL)
    {
        LOG_ERR(GENERIC_NULL_PTR_ERROR_MESSAGE);
        return E_NULL_PTR;
    }
    struct temperature_sample_t merged[CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE];

    // time
    sys_minutes_t start_uptime = MIN(src1->data[0].uptime, src2->data[0].uptime);
    sys_minutes_t end_uptime = MAX(src1->data[src1->length - 1].uptime, src2->data[src2->length - 1].uptime);
    sys_minutes_t merge_duration = end_uptime - start_uptime;
    sys_minutes_t sample_base_period = merge_duration / (CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE - 1);
    sys_minutes_t long_periods_needed = merge_duration % (CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE - 1);

    struct temperature_sample_t *current = NULL, *prev = NULL;
    struct merge_iterator_t iterator;
    init_merge_iterator(&iterator, src1, src2);
    merge_iterate(&iterator, &prev);
    merge_iterate(&iterator, &current);

    size_t merge_index = 0;
    sys_minutes_t merge_uptime = start_uptime;
    enum error_e err = E_SUCCESS;

    while (merge_index < CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE)
    {
        // if time is between the two points, perform an interpolation
        // otherwise, shift the current and prev samples
        if (merge_uptime >= prev->uptime && merge_uptime <= current->uptime)
        {
            merged[merge_index].uptime = merge_uptime;
            err = interpolate(prev, current, &merged[merge_index]);
            if (err != E_SUCCESS)
            {
                return err;
            }
            merge_index++;
            merge_uptime += sample_base_period + (merge_index <= long_periods_needed ? 1 : 0);
        }
        else
        {
            prev = current;
            err = merge_iterate(&iterator, &current);
            if (err != E_SUCCESS)
            {
                LOG_ERR("Merge with interpolation failed. Error %d.", err);
                return E_ERROR;
            }
        }
    }

    // copy the result to dest
    memmove(dest->data, merged, sizeof(struct temperature_sample_t) * CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE);
    dest->length = CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE;
    return E_SUCCESS;
}

/**
 * @brief Dispatcher function that selects between chronological merge and decimation/interpolation.
 * * If total length <= capacity, it calls merge_without_decimation.
 * Otherwise, it calls merge_with_decimation.
 * ASSUMPTION: The caller MUST hold the lists' locks before calling.
 * * @param src1 Pointer to the first source list.
 * @param src2 Pointer to the second source list.
 * @param dest Pointer to the destination list.
 * @retval E_SUCCESS Merge successful.
 * @retval E_NULL_PTR If any input pointer is NULL.
 * @retval E_ERROR Propagated error from helper functions.
 */
EXPOSE_FOR_TESTING enum error_e merge_temperature_lists(struct temperature_list_t *src1, struct temperature_list_t *src2, struct temperature_list_t *dest)
{
    if (src1 == NULL || src2 == NULL || dest == NULL)
    {
        return E_NULL_PTR;
    }

    enum error_e err;
    if (src1->length + src2->length <= CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE)
    {
        err = merge_without_decimation(src1, src2, dest);
    }
    else
    {
        err = merge_with_decimation(src1, src2, dest);
    }
    return err;
}

static bool temperature_list_is_full(struct temperature_list_t *t)
{
    return t != NULL && t->length == CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE;
}


/**
 * @brief The handler function executed by the k_work_delayable structure.
 * * This is the central synchronization point. It acquires the locks, checks if the 
 * RAM list is full, performs the merge/store cycle if needed, takes a new sample, 
 * appends it, and reschedules itself.
 * * Synchronization: Acquires DOUBLE_LOCK on t_data.temperature_list and 
 * t_data.scratch_temperature_list for the entire execution.
 * * @param work Pointer to the k_work structure (unused but required).
 */
static void perform_sampling_task(struct k_work *work)
{
    LOG_DBG("Performing sampling task");
    DOUBLE_LOCK(&t_data.temperature_list.lock, &t_data.scratch_temperature_list.lock, K_FOREVER);

    enum error_e err;
    // if RAM list is not full, sample the temperature
    // if the RAM list is full
    // read the NVS list, merge the two lists, store the result in NVS, clear the RAM list, sample the temperature

    if (temperature_list_is_full(&t_data.temperature_list))
    {
        err = load_temperature_list(&t_data.scratch_temperature_list);
        if (err != E_SUCCESS)
        {
            goto reschedule;
        }
        err = merge_temperature_lists(&t_data.temperature_list, &t_data.scratch_temperature_list, &t_data.scratch_temperature_list);
        if (err != E_SUCCESS)
        {
            goto reschedule;
        }
        err = store_temperature_list(&t_data.scratch_temperature_list);
        if (err != E_SUCCESS)
        {
            goto reschedule;
        }
        err = reset_temperature_list(&t_data.temperature_list);
        if (err != E_SUCCESS)
        {
            goto reschedule;
        }
    }

    struct temperature_sample_t sample;
    err = get_temperature_sample(&sample);
    if (err != E_SUCCESS)
    {
        goto reschedule;
    }
    err = append_temperature_sample(&t_data.temperature_list, sample);
reschedule:
    if (err != E_SUCCESS)
    {
        LOG_ERR("Failed to complete sampling task. Error %d.", err);
    }
    k_work_reschedule(&t_data.sampling_task, K_SECONDS(30));
    DOUBLE_UNLOCK(&t_data.temperature_list.lock, &t_data.scratch_temperature_list.lock);
}