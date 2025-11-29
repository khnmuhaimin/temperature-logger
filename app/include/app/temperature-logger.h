#ifndef APP_TEMPERATURE_LOGGER_H
#define APP_TEMPERATURE_LOGGER_H

#include <stdint.h>
#include <zephyr/drivers/sensor.h>
#include "app/time.h"
#include "app/error.h"

#ifndef CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE
// this is never used. im putting it there so that intellisense doesnt get confused
#define CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE 100
#endif

typedef int16_t temperature_t;  // 1 sign bit; 11 whole bits; 4 fractional bits

struct temperature_sample_t {
    temperature_t temperature;
    sys_minutes_t uptime;
};

struct temperature_list_t
{
    struct temperature_sample_t data[CONFIG_TEMPERATURE_LOGGER_BUFFER_SIZE];
    size_t length;
    struct k_mutex lock;
};

struct merge_iterator_t
{
    struct temperature_list_t *src1;
    struct temperature_list_t *src2;
    struct temperature_list_t *current_list;
    size_t current_index;
    size_t next_src1_index;
    size_t next_src2_index;
};



enum error_e init_temperature_logger(void);


#if CONFIG_BUILD_TEST_APP
enum error_e reset_temperature_list(struct temperature_list_t *t);
enum error_e load_temperature_list(struct temperature_list_t *t);
enum error_e store_temperature_list(struct temperature_list_t *t);
temperature_t sensor_value_to_temperature(struct sensor_value v);
enum error_e get_temperature_sample(struct temperature_sample_t *t);
enum error_e append_temperature_sample(struct temperature_list_t *list, struct temperature_sample_t sample);
enum error_e interpolate(struct temperature_sample_t *t1, struct temperature_sample_t *t2, struct temperature_sample_t* result);
void init_merge_iterator(struct merge_iterator_t *m, struct temperature_list_t *src1, struct temperature_list_t *src2);
enum error_e merge_iterate(struct merge_iterator_t *m, struct temperature_sample_t **sample);
enum error_e merge_temperature_lists(struct temperature_list_t *src1, struct temperature_list_t *src2, struct temperature_list_t *dest);
#endif

#endif