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

    // there are three sub-tests below
    // 1. correct logins
    // 2. incorrect ssid
    // 3. incorrect password
    // uncomment one at a time to verify that each case works as epxected

    // set_wifi_logins("Openserve-8B43", "RctVkh8VLh");
    // set_wifi_logins("wrong ssid", "RctVkh8VLh");
    // set_wifi_logins("Openserve-8B43", "wrong password");
    enum logins_state_e state;
    err = test_wifi_logins(&state);
    LOG_DBG("Test wifi logins result: %d.", state);
    if (err != E_SUCCESS) {
        LOG_ERR("Wifi test failed. Error %d.", err);
    } else {
        LOG_DBG("Wifi test completed successfully.");
    }
    while (true)
    {
        k_sleep(K_SECONDS(10));
    }
}