#ifndef APP_NVS_H
#define APP_NVS_H

#include "app/error.h"

enum nvs_key_e {
    NVS_KEY_CONFIG_SETTINGS = 1,
    NVS_KEY_TEMPERATURE_DATA,
};

enum error_e init_nvs(void);
struct nvs_fs* get_nvs_fs(void);


#endif