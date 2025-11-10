#ifndef CONFIG_SETTINGS_H
#define CONFIG_SETTINGS_H

#include "custom-error.h"
#include "constants.h"

#define RESET_WIFI_SSID_VALUE 0xFF
#define RESET_WIFI_PASSWORD_VALUE 0xFF

struct config_settings_t
{
    char wifi_ssid[WIFI_SSID_MAX_LENGTH + 1];
    char wifi_password[WIFI_PASSWORD_MAX_LENGTH + 1];
};

enum error_e init_config_settings(void);
void reset_config_settings(struct config_settings_t *c);
void load_config_settings(struct config_settings_t *c);
enum error_e store_config_settings(struct config_settings_t *c);

#endif