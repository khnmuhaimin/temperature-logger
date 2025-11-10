#ifndef NVS_HELPERS_H
#define NVS_HELPERS_H


enum nvs_key_e {
    NVS_KEY_CONFIG_SETTINGS = 1
};

struct nvs_fs* get_nvs_fs(void);


#endif