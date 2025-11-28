#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "app/nvs.h"
#include "app/config-settings.h"
#include "app/wifi.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

int main(void)
{
    LOG_INF("Hello, World!");
    k_sleep(K_SECONDS(10));
    init_nvs();
    init_config_settings();
    init_wifi();
    k_sleep(K_SECONDS(10));
    LOG_DBG("Init complete.");

    enum error_e err;
    // test wifi logins
    // set_wifi_logins("Openserve-8B43", "RctVkh8VLh");
    // set_wifi_logins("wrong ssid", "RctVkh8VLh");
    set_wifi_logins("Openserve-8B43", "wrong password");
    enum logins_state_e state;
    // LOG_DBG("Testing wifi...");
    err = test_wifi_logins(&state);
    LOG_DBG("Test wifi logins result: %d.", state);
    // err = enable_wifi_station();
    if (err != E_SUCCESS) {
        LOG_ERR("Wifi test failed. Error %d.", err);
    } else {
        LOG_DBG("Wifi test completed successfully.");
    }
    while (true)
    {
        k_sleep(K_SECONDS(10));
    }

    // struct config_settings_t settings;
    // load_config_settings(&settings);

    // if (settings.wifi_ssid[0] == 0xFF)
    // {
    //     strcpy(settings.wifi_ssid, "my wifi");
    //     strcpy(settings.wifi_password, "my password");
    //     store_config_settings(&settings);
    //     LOG_INF("Reboot the device now.");
    // }
    // else
    // {
    //     LOG_INF("Wifi SSID: %s", settings.wifi_ssid);
    //     LOG_INF("Wifi password: %s", settings.wifi_password);
    // }

    // while (true)
    // {
    //     k_sleep(K_FOREVER);
    // }
}