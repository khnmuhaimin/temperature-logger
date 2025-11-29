#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
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
};

static struct temperature_logger_data_t t_data = {
    .temperature_list.lock = Z_MUTEX_INITIALIZER(t_data.temperature_list.lock),
    .scratch_temperature_list.lock = Z_MUTEX_INITIALIZER(t_data.scratch_temperature_list.lock),
    .temperature_sensor = DEVICE_DT_GET_ANY(maxim_ds18b20)};

enum error_e init_temperature_logger(void) {
    if (t_data.temperature_sensor == NULL) {
        LOG_ERR("Temperature sensor device was not found.");
        return E_ERROR;
    }

    if (!device_is_ready(t_data.temperature_sensor)) {
        LOG_ERR("Temperature sensor device is not ready.");
        return E_ERROR;
    }
    return E_SUCCESS;
}

EXPOSE_FOR_TESTING enum error_e reset_temperature_list(struct temperature_list_t *t)
{
    if (t == NULL)
    {
        return E_NULL_PTR;
    }
    k_mutex_lock(&t->lock, K_FOREVER);
    memset(t, 0, offsetof(struct temperature_list_t, lock));
    k_mutex_unlock(&t->lock);
    return E_SUCCESS;
}

EXPOSE_FOR_TESTING enum error_e load_temperature_list(struct temperature_list_t *t)
{
    if (t == NULL)
    {
        return E_NULL_PTR;
    }

    struct nvs_fs *fs = get_nvs_fs();
    enum error_e err;

    k_mutex_lock(&t->lock, K_FOREVER);
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
        LOG_ERR("Failed to load temperature list from NVS.");
        err = E_ERROR;
    }
    else
    {
        err = E_SUCCESS;
    }

    k_mutex_unlock(&t->lock);
    return err;
}

EXPOSE_FOR_TESTING enum error_e store_temperature_list(struct temperature_list_t *t)
{
    if (t == NULL)
    {
        return E_NULL_PTR;
    }

    struct nvs_fs *fs = get_nvs_fs();
    k_mutex_lock(&t->lock, K_FOREVER);
    ssize_t bytes_written = nvs_write(fs, NVS_KEY_TEMPERATURE_DATA, t, offsetof(struct temperature_list_t, lock));
    enum error_e err = bytes_written == offsetof(struct temperature_list_t, lock) || bytes_written == 0 ? E_SUCCESS : E_ERROR;
    k_mutex_unlock(&t->lock);
    return err;
}

EXPOSE_FOR_TESTING temperature_t sensor_value_to_temperature(struct sensor_value v)
{
    float whole = (float)v.val1;
    float fractional = (float)v.val2 / 1000000.0f;
    float float_temperature = whole + fractional;
    temperature_t temperature = (temperature_t)(float_temperature * 16);
    return temperature;
}

EXPOSE_FOR_TESTING enum error_e get_temperature_sample(struct temperature_sample_t *t)
{
    if (t == NULL)
    {
        return E_NULL_PTR;
    }
    struct sensor_value temperature;
    int result = sensor_sample_fetch(t_data.temperature_sensor);
    t->uptime = get_uptime_in_minutes();
    if (result != 0)
    {
        LOG_ERR("Failed to request temperature sample. Error code: %d.", result);
        return E_ERROR;
    }
    result = sensor_channel_get(t_data.temperature_sensor, SENSOR_CHAN_AMBIENT_TEMP, &temperature);
    if (result != 0)
    {
        LOG_ERR("Failed to read temperature sample. Error code: %d.", result);
        return E_ERROR;
    }
    t->temperature = sensor_value_to_temperature(temperature);
    return E_SUCCESS;
}

EXPOSE_FOR_TESTING enum error_e append_temperature_sample(struct temperature_list_t *list, struct temperature_sample_t sample)
{
    if (list == NULL)
    {
        return E_NULL_PTR;
    }
    k_mutex_lock(&list->lock, K_FOREVER);
    enum error_e err = E_SUCCESS;
    if (list->length == CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE)
    {
        err = E_NOBUFS;
        goto unlock;
    }
    list->data[list->length] = sample;
    list->length++;
unlock:
    k_mutex_unlock(&list->lock);
    return err;
}

EXPOSE_FOR_TESTING struct temperature_sample_t interpolate(struct temperature_sample_t *t1, struct temperature_sample_t *t2, sys_minutes_t uptime)
{
    struct temperature_sample_t result;
    result.uptime = uptime;
    if (t1->uptime == t2->uptime)
    {
        result.temperature = (t1->temperature + t2->temperature) / 2;
        return result;
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
    uint32_t d_uptime_to_result = (uint32_t)(uptime - earlier->uptime);
    int16_t d_temp_to_result = (int16_t)((float)d_temp * (float)d_uptime_to_result / (float)d_uptime);
    result.temperature = (temperature_t)(earlier->temperature + d_temp_to_result);
    return result;
}

EXPOSE_FOR_TESTING void init_merge_iterator(struct merge_iterator_t *m, struct temperature_list_t *src1, struct temperature_list_t *src2)
{
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
}

EXPOSE_FOR_TESTING enum error_e merge_iterate(struct merge_iterator_t *m, struct temperature_sample_t **sample)
{

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

EXPOSE_FOR_TESTING enum error_e merge_without_decimation(struct temperature_list_t *src1, struct temperature_list_t *src2, struct temperature_list_t *dest)
{
    if (src1 == NULL || src2 == NULL || dest == NULL)
    {
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
            LOG_WRN("Merging without interpolation failed. Error %d.", err);
            return E_ERROR;
        }
        merged[index] = *sample;
        index++;
    }
}

// assumes that the total number of input elements is greater than the capacity of one list
EXPOSE_FOR_TESTING enum error_e merge_with_decimation(struct temperature_list_t *src1, struct temperature_list_t *src2, struct temperature_list_t *dest)
{
    if (src1 == NULL || src2 == NULL || dest == NULL)
    {
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
            merged[merge_index] = interpolate(prev, current, merge_uptime);
            merge_index++;
            merge_uptime += sample_base_period + (merge_index <= long_periods_needed ? 1 : 0);
        }
        else
        {
            prev = current;
            err = merge_iterate(&iterator, &current);
            if (err != E_SUCCESS)
            {
                LOG_WRN("Merge with interpolation failed. Error: %d.", err);
                return E_ERROR;
            }
        }
    }

    // copy the result to dest
    memmove(dest->data, merged, sizeof(struct temperature_sample_t) * CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE);
    dest->length = CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE;
    return E_SUCCESS;
}

EXPOSE_FOR_TESTING enum error_e merge_temperature_lists(struct temperature_list_t *src1, struct temperature_list_t *src2, struct temperature_list_t *dest)
{
    if (src1 == NULL || src2 == NULL || dest == NULL)
    {
        return E_NULL_PTR;
    }
    TRIPLE_LOCK(&src1->lock, &src2->lock, &dest->lock, K_FOREVER);

    enum error_e err;

    if (src1->length + src2->length <= CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE)
    {
        err = merge_without_decimation(src1, src2, dest);
    }
    else
    {
        err = merge_with_decimation(src1, src2, dest);
    }

    TRIPLE_UNLOCK(&src1->lock, &src2->lock, &dest->lock);
    return err;
}


