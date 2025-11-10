#include "config-settings.h"
#include "zephyr/kernel.h"

static struct config_settings_t config_settings = {0};
static struct k_mutex config_settings_mutex = {0};

struct config_settings_t* get_config_settings(void) {
    return &config_settings;
}

/*
 * Initializes config settings module.
 * 
 * Call this function before using anything else from this module.
 */
static void init_config_settings() {
    k_mutex_init(&config_settings_mutex);
}


/*
 * Loads config settings from NVS.
 */
static enum error_e load_config_settings(struct config_settings_t *c) {

}


/*
 * Sets all config values to their reset state.
 */
void reset_config_settings(struct config_settings_t* c) {
    memset(c->wifi_ssid, 0, sizeof(c->wifi_ssid));
    c->wifi_ssid[0] = RESET_WIFI_SSID_VALUE; 
    memset(c->wifi_password, 0, sizeof(c->wifi_password));
    c->wifi_password[0] = RESET_WIFI_PASSWORD_VALUE; 
}

/*
 * Copies the main config values to the given config settings.
 */
enum error_e copy_config_settings(struct config_settings_t* c) {
    
}


/*
 * Validates and stores the config settings in NVS.
 */
enum error_e store_config_settings(struct config_settings_t* c) {

}

