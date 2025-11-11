#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "nvs-helpers.h"
#include "config-settings.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

int main(void)
{
    LOG_INF("Hello, World!");
    k_sleep(K_SECONDS(10));
    init_nvs();
    init_config_settings();

    struct config_settings_t settings;
    load_config_settings(&settings);

    if (settings.wifi_ssid[0] == 0xFF)
    {
        strcpy(settings.wifi_ssid, "my wifi");
        strcpy(settings.wifi_password, "my password");
        store_config_settings(&settings);
        LOG_INF("Reboot the device now.");
    }
    else
    {
        LOG_INF("Wifi SSID: %s", settings.wifi_ssid);
        LOG_INF("Wifi password: %s", settings.wifi_password);
    }

    while (true)
    {
        k_sleep(K_FOREVER);
    }
}