#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/logging/log.h>
#include "app/error.h"

LOG_MODULE_REGISTER(nvs_helpers, LOG_LEVEL_WRN);

#define NVS_PARTITION storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)

static struct nvs_fs fs = {0};
static struct flash_pages_info info = {0};

/*
 * Initialize NVS.
 *
 * Code was adapted from zephyr/samples/subsys/nvs.
 */
enum error_e init_nvs()
{

    int err;

    fs.flash_device = NVS_PARTITION_DEVICE;
    if (!device_is_ready(fs.flash_device))
    {
        LOG_ERR("Flash device was not ready.");
        return E_ERROR;
    }
    fs.offset = NVS_PARTITION_OFFSET;
    err = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
    if (err)
    {
        LOG_ERR("Page of the offset doesn't exist.");
        return E_ERROR;
    }
    fs.sector_size = info.size;
    fs.sector_count = 3U;  // TODO: figure out the best sector count size

    err = nvs_mount(&fs);
    if (err)
    {
        LOG_ERR("Failed to mount NVS file system.");
        return E_ERROR;
    }
    return E_SUCCESS;
}

struct nvs_fs* get_nvs_fs(void) {
    return &fs;
}