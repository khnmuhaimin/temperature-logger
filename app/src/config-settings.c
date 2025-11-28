/*
 * Configuration Management Module (config_store.c)
 * -----------------------------------------------------------------------------
 * This module manages the application's persistent configuration data 
 * (struct config_settings_t), ensuring thread safety and data integrity 
 * when interacting with Non-Volatile Storage (NVS).
 *
 * Design Principles:
 * 1. Single Source of Truth: The configuration is stored in one global, 
 *    static structure: 'config_settings'.
 * 2. Thread Safety: Access to 'config_settings' is protected by 'config_settings_mutex' 
 *    to prevent "torn reads" and race conditions.
 * 3. Minimal Flash Access: Configuration is read from NVS once at boot (config_init) 
 *    and saved only when modified (store_config_settings), minimizing flash wear.
 * 4. Usage Contract: Callers are not given access to config_settings. Threads 
 *    must create a local copy and use the following functions:
 *    - load_config_settings() for reading the active configuration (Safe Read).
 *    - store_config_settings() for validating and writing changes to RAM and Flash.
 *
 * NOTE: This implementation should use CONFIG_NVS_DATA_CRC=y in prj.conf 
 * to guarantee the integrity of the data payload in Flash.
 */

#include <zephyr/kernel.h>
#include <zephyr/fs/nvs.h>
#include "app/config-settings.h"
#include "app/nvs.h"
#include "app/string.h"

static struct config_settings_t config_settings = {0};
static struct k_mutex config_settings_mutex = {0};

/**
 * @brief Validates the Wi-Fi SSID and Password fields against basic rules.
 * 
 * @param c Pointer to the proposed configuration structure.
 * @return true if credentials are valid or correctly unspecified; false otherwise.
 */
static bool validate_wifi_ssid_and_password(struct config_settings_t *c)
{
    if (c == NULL)
    {
        return false;
    }
    // if both are unspecified, return true
    if (c->wifi_ssid[0] == RESET_WIFI_SSID_VALUE && c->wifi_password[0] == RESET_WIFI_PASSWORD_VALUE)
    {
        return true;
    }

    // if only one is given, return false
    if (c->wifi_ssid[0] == RESET_WIFI_SSID_VALUE || c->wifi_password[0] == RESET_WIFI_PASSWORD_VALUE)
    {
        return false;
    }

    // if ssid or password is not an ascii printable string, return false
    if (!is_printable_ascii_string(c->wifi_ssid, sizeof(c->wifi_ssid)))
    {
        return false;
    }
    if (!is_printable_ascii_string(c->wifi_password, sizeof(c->wifi_password)))
    {
        return false;
    }

    // if password is too short, return false
    // zero length means that no password is needed.
    size_t password_length = strlen(c->wifi_password);
    if (password_length != 0 && password_length < WIFI_PASSWORD_MIN_LENGTH)
    {
        return false;
    }
    return true;
}

/**
 * @brief Loads the config settings from flash into the main config settings.
 * 
 * @return E_SUCCESS or E_ERROR
 */
static enum error_e load_main_config_settings(void)
{
    struct nvs_fs *fs = get_nvs_fs();
    enum error_e err;

    k_mutex_lock(&config_settings_mutex, K_FOREVER);
    // try to read config settings
    ssize_t bytes_read = nvs_read(fs, NVS_KEY_CONFIG_SETTINGS, &config_settings, sizeof(struct config_settings_t));
    if (bytes_read == -ENOENT)
    {
        // key does not exist. create config settings
        reset_config_settings(&config_settings);
        nvs_write(fs, NVS_KEY_CONFIG_SETTINGS, &config_settings, sizeof(struct config_settings_t));
        err = E_SUCCESS;
    }
    else if (bytes_read != sizeof(struct config_settings_t))
    {
        // for some reason, the read failed
        reset_config_settings(&config_settings);
        err = E_ERROR;
    } else {
        err = E_SUCCESS;
    }

    k_mutex_unlock(&config_settings_mutex);
    return err;
}

/**
 * @brief Initializes config settings module.
 * Call this function before using anything else from this module.
 * Initialize NVS before calling this function.
 * 
 * @return E_SUCCESS or E_ERROR
 */
enum error_e init_config_settings(void)
{
    k_mutex_init(&config_settings_mutex);
    enum error_e err = load_main_config_settings();
    return err;
}

/**
 * @brief Sets all config settings to their reset state.
 * 
 * @param c Pointer to the config settings struct.
 */
void reset_config_settings(struct config_settings_t *c)
{
    if (c == NULL)
    {
        return;
    }
    memset(c->wifi_ssid, 0, sizeof(c->wifi_ssid));
    c->wifi_ssid[0] = RESET_WIFI_SSID_VALUE;
    memset(c->wifi_password, 0, sizeof(c->wifi_password));
    c->wifi_password[0] = RESET_WIFI_PASSWORD_VALUE;
}

/**
 * @brief Copies the main config settings to the provided config settings.
 * 
 * @param c Pointer to the config settings struct.
 */
void load_config_settings(struct config_settings_t *c)
{
    if (c == NULL)
    {
        return;
    }
    k_mutex_lock(&config_settings_mutex, K_FOREVER);
    memcpy(c, &config_settings, sizeof(struct config_settings_t));
    k_mutex_unlock(&config_settings_mutex);
}

/**
 * @brief Stores the provided config settings in NVS provided that they are valid.
 * 
 * @param c Pointer to the config settings struct.
 * @return E_NULL_PTR if c is NULL. 
 * @return E_INVAL if c is invalid.
 * @return E_SUCCESS if store was successful.
 * @return E_ERROR if an error occured.
 */
enum error_e store_config_settings(struct config_settings_t *c)
{
    if (c == NULL)
    {
        return E_NULL_PTR;
    }

    bool config_settings_valid = validate_wifi_ssid_and_password(c);
    if (!config_settings_valid)
    {
        return E_INVAL;
    }
    struct nvs_fs *fs = get_nvs_fs();
    k_mutex_lock(&config_settings_mutex, K_FOREVER);
    ssize_t bytes_written = nvs_write(fs, NVS_KEY_CONFIG_SETTINGS, c, sizeof(struct config_settings_t));
    enum error_e err = bytes_written == sizeof(struct config_settings_t) || bytes_written == 0 ? E_SUCCESS : E_ERROR;
    if (err == E_SUCCESS)
    {
        memcpy(&config_settings, c, sizeof(struct config_settings_t));
    }
    k_mutex_unlock(&config_settings_mutex);
    return err;
}
