#ifndef NVS_HELPERS_H
#define NVS_HELPERS_H

#include "custom-error.h"

enum nvs_key_e {
    NVS_KEY_CONFIG_SETTINGS = 1
};

enum error_e init_nvs(void);
struct nvs_fs* get_nvs_fs(void);


#endif