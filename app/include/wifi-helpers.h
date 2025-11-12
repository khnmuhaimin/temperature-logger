#ifndef WIFI_HELPERS_H
#define WIFI_HELPERS_H

#include <zephyr/kernel.h>
#include "custom-error.h"
#include "constants.h"

/**
 * Events:
 * 1. station enabled
 * 2. ap enabled
 * 3. logins tested
 * 4. logins valid
 */

enum wifi_station_state_e
{
    STATION_STATE_CONNECTED,
    STATION_STATE_CONNECTED_WITHOUT_IP,
    STATION_STATE_DISCONNECTED,
    STATION_STATE_CONNECTING,
    STATION_STATE_DISCONNECTING,
};

enum wifi_ap_state_e
{
    AP_STATE_ENABLED,
    AP_STATE_DISABLED,
    AP_STATE_ENABLING,
    AP_STATE_DISABLING,
};

enum logins_state_e
{
    NOT_SET,
    SET_AND_NOT_TESTED,
    SET_AND_INVALID,
    SET_AND_VALID,
};

struct wifi_state_t
{
    enum wifi_station_state_e station_state;
    enum wifi_ap_state_e ap_state;
    enum logins_state_e logins_state;
    bool power_saving_mode_enabled
};

void init_wifi(void);
void set_wifi_logins(char *ssid, char *password);
enum error_e enable_wifi_station(void);
enum error_e disable_wifi_station(void);
enum error_e enable_wifi_ap(void);
enum error_e disable_wifi_ap(void);
void get_wifi_state(struct wifi_state_t *s);

#endif