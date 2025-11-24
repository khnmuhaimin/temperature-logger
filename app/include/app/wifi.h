#ifndef APP_WIFI_H
#define APP_WIFI_H

#include <zephyr/kernel.h>
#include "app/error.h"
#include "app/constants.h"

#define STATION_STATE_IS_CONNECTING(state) (state == STATION_STATE_CONNECTING || state == STATION_STATE_CONNECTED_WITHOUT_IP || state == STATION_STATE_CONNECTING_AND_WITH_IP)

enum wifi_station_state_e
{
    STATION_STATE_CONNECTED,
    STATION_STATE_CONNECTED_WITHOUT_IP,
    STATION_STATE_CONNECTING_AND_WITH_IP,
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
    LOGINS_STATE_NOT_SET,
    LOGINS_STATE_SET_AND_NOT_TESTED,
    LOGINS_STATE_SET_AND_INVALID,
    LOGINS_STATE_SET_AND_VALID,
};

struct wifi_state_t
{
    enum wifi_station_state_e station_state;
    enum wifi_ap_state_e ap_state;
    enum logins_state_e logins_state;
    bool power_saving_mode_enabled;
};

void init_wifi(void);
enum error_e set_wifi_logins(char *ssid, char *password);
enum error_e enable_wifi_station(void);
enum error_e disable_wifi_station(void);
enum error_e enable_wifi_ap(void);
enum error_e disable_wifi_ap(void);
void get_wifi_state(struct wifi_state_t *w);
enum error_e test_wifi_logins(enum logins_state_e *state);

#endif