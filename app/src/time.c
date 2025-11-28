#include <stdint.h>
#include <zephyr/kernel.h>
#include "app/time.h"

sys_minutes_t get_uptime_in_minutes() {
    return (sys_minutes_t)(k_uptime_get() / 1000 / 60);
}