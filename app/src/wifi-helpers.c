/**
 * @file wifi-helpers.c
 * @brief This module provides a thread-safe, state-aware abstraction layer for 
 * managing Wi-Fi Station (STA) and Access Point (AP) modes in Zephyr.
 *
 * @section usage Usage:
 * 1. **Initialization:** Call `init_wifi()` once at application startup.
 * 2. **STA Setup:** Use `set_wifi_logins()` to store credentials.
 * 3. **STA Enable:** Use `enable_wifi_station()` to connect. The connection
 * process is asynchronous, and the state will transition via event handlers.
 * 4. **STA Disable:** Use `disable_wifi_station()` to disconnect.
 * 5. **AP Enable/Disable:** Use `enable_wifi_ap()` and `disable_wifi_ap()` to
 * control the Access Point functionality.
 * 6. **Monitoring:** Use `get_wifi_state()` to check the current Wi-Fi status.
 *
 * All public functions are thread-safe due to internal mutex protection.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/logging/log.h>
#include "wifi-helpers.h"

LOG_MODULE_REGISTER(wifi_helpers, LOG_LEVEL_INF);

#define WIFI_AP_SSID "ESP32-AP"
#define WIFI_AP_PSK "password"
#define WIFI_AP_IP_ADDRESS "192.168.4.1"
#define WIFI_AP_NETMASK "255.255.255.0"
#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

static struct wifi_helpers_data_t
{
    struct wifi_state_t wifi_state;
    // Credentials used for the last STA attempt
    char ssid_cache[WIFI_SSID_MAX_LENGTH + 1];
    char password_cache[WIFI_PASSWORD_MAX_LENGTH + 1];
    char ssid_stable_cache[WIFI_SSID_MAX_LENGTH + 1];
    char password_stable_cache[WIFI_PASSWORD_MAX_LENGTH + 1];
    bool dhcpv_server_enabled;
    struct k_mutex lock; // Protects access to this struct
    struct wifi_connect_req_params ap_config;
};

static struct wifi_helpers_data_t wifi_helpers_data;

static reset_internal_wifi_state()
{
    wifi_helpers_data.wifi_state.station_state = STATION_STATE_DISCONNECTED;
    wifi_helpers_data.wifi_state.ap_state = AP_STATE_DISABLED;
    wifi_helpers_data.wifi_state.logins_state = NOT_SET;
    wifi_helpers_data.dhcpv_server_enabled = false;
}

static void set_ap_config()
{
    wifi_helpers_data.ap_config.ssid = (const uint8_t *)WIFI_AP_SSID;
    wifi_helpers_data.ap_config.ssid_length = strlen(WIFI_AP_SSID);
    wifi_helpers_data.ap_config.psk = (const uint8_t *)WIFI_AP_PSK;
    wifi_helpers_data.ap_config.psk_length = strlen(WIFI_AP_PSK);
    wifi_helpers_data.ap_config.channel = WIFI_CHANNEL_ANY;
    wifi_helpers_data.ap_config.band = WIFI_FREQ_BAND_2_4_GHZ;
    wifi_helpers_data.ap_config.security = WIFI_SECURITY_TYPE_PSK;
}

static enum error_e enable_dhcpv4_server(void)
{
    k_mutex_lock(&wifi_helpers_data.lock, K_FOREVER);
    enum error_e err = E_SUCCESS;
    if (wifi_helpers_data.dhcpv_server_enabled)
    {
        err = E_ALREADY_DONE;
        goto unlock;
    }
    struct in_addr addr;
    struct in_addr netmask_addr;
    struct net_if *ap_iface = net_if_get_wifi_sap();

    if (net_addr_pton(AF_INET, WIFI_AP_IP_ADDRESS, &addr))
    {
        err = E_ERROR;
        goto unlock;
    }

    if (net_addr_pton(AF_INET, WIFI_AP_NETMASK, &netmask_addr))
    {
        err = E_ERROR;
        goto unlock;
    }

    net_if_ipv4_set_gw(ap_iface, &addr);

    if (net_if_ipv4_addr_add(ap_iface, &addr, NET_ADDR_MANUAL, 0) == NULL)
    {
        err = E_ERROR;
        goto unlock;
    }

    if (!net_if_ipv4_set_netmask_by_addr(ap_iface, &addr, &netmask_addr))
    {
        err = E_ERROR;
        goto unlock;
    }

    addr.s4_addr[3] += 10; /* Starting IPv4 address for DHCPv4 address pool. */

    if (net_dhcpv4_server_start(ap_iface, &addr) != 0)
    {
        err = E_ERROR;
        goto unlock;
    }
    wifi_helpers_data.dhcpv_server_enabled = true;

unlock:
    k_mutex_unlock(&wifi_helpers_data.lock);
    return err;
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		LOG_INF("Connected to %s.", wifi_helpers_data.ssid_stable_cache);
        k_mutex_lock(&wifi_helpers_data.lock, K_FOREVER);
        wifi_helpers_data.wifi_state.station_state = STATION_STATE_CONNECTED_WITHOUT_IP;
        k_mutex_unlock(&wifi_helpers_data.lock);
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		LOG_INF("Disconnected from %s.", wifi_helpers_data.ssid_stable_cache);
        k_mutex_lock(&wifi_helpers_data.lock, K_FOREVER);
        wifi_helpers_data.wifi_state.station_state = STATION_STATE_DISCONNECTED;
        k_mutex_unlock(&wifi_helpers_data.lock);
		break;
	}
	case NET_EVENT_WIFI_AP_ENABLE_RESULT: {
		LOG_INF("AP Mode is enabled. Waiting for station to connect");
        k_mutex_lock(&wifi_helpers_data.lock, K_FOREVER);
        wifi_helpers_data.wifi_state.station_state = AP_STATE_ENABLED;
        k_mutex_unlock(&wifi_helpers_data.lock);
		break;
	}
	case NET_EVENT_WIFI_AP_DISABLE_RESULT: {
		LOG_INF("AP Mode is disabled.");
        k_mutex_lock(&wifi_helpers_data.lock, K_FOREVER);
        wifi_helpers_data.wifi_state.station_state = AP_STATE_DISABLED;
        k_mutex_unlock(&wifi_helpers_data.lock);
		break;
	}
	case NET_EVENT_WIFI_AP_STA_CONNECTED: {
		struct wifi_ap_sta_info *sta_info = (struct wifi_ap_sta_info *)cb->info;
		LOG_INF("station: " MACSTR " joined ", sta_info->mac[0], sta_info->mac[1],
			sta_info->mac[2], sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);
		break;
	}
	case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
		struct wifi_ap_sta_info *sta_info = (struct wifi_ap_sta_info *)cb->info;

		LOG_INF("station: " MACSTR " leave ", sta_info->mac[0], sta_info->mac[1],
			sta_info->mac[2], sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);
		break;
	}
	default:
		break;
	}
}

static void ip_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD)
    {
        k_mutex_lock(&wifi_helpers_data.lock, K_FOREVER);
        wifi_helpers_data.wifi_state.station_state = STATION_STATE_CONNECTED;
        k_mutex_unlock(&wifi_helpers_data.lock);
    }
}

void init_wifi(void)
{
    reset_internal_wifi_state();
    k_mutex_init(&wifi_helpers_data.lock);
    set_ap_config();

    struct net_mgmt_event_callback wifi_event_cb;
    struct net_mgmt_event_callback ip_event_cb;
    const uint32_t WIFI_EVENTS_HANDLED = (
        NET_EVENT_WIFI_CONNECT_RESULT |
        NET_EVENT_WIFI_DISCONNECT_RESULT |
        NET_EVENT_WIFI_AP_ENABLE_RESULT |
        NET_EVENT_WIFI_AP_DISABLE_RESULT |
        NET_EVENT_WIFI_AP_STA_CONNECTED |
        NET_EVENT_WIFI_AP_STA_DISCONNECTED);
    net_mgmt_init_event_callback(
        &wifi_event_cb,
        wifi_event_handler,
        WIFI_EVENTS_HANDLED);
    net_mgmt_init_event_callback(
        &ip_event_cb,
        ip_event_handler,
        NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&wifi_event_cb);
    net_mgmt_add_event_callback(&ip_event_cb);
}

void set_wifi_logins(char *ssid, char *password)
{
    k_mutex_lock(&wifi_helpers_data.lock, K_FOREVER);
    memset(wifi_helpers_data.ssid_cache, 0, sizeof(wifi_helpers_data.ssid_cache));
    memset(wifi_helpers_data.password_cache, 0, sizeof(wifi_helpers_data.password_cache));
    strncpy(wifi_helpers_data.ssid_cache, ssid, sizeof(wifi_helpers_data.ssid_cache));
    strncpy(wifi_helpers_data.password_cache, password, sizeof(wifi_helpers_data.password_cache));
    wifi_helpers_data.wifi_state.logins_state = SET_AND_NOT_TESTED;
    k_mutex_unlock(&wifi_helpers_data.lock);
}

enum error_e enable_wifi_station(void)
{
    k_mutex_lock(&wifi_helpers_data.lock, K_FOREVER);
    enum error_e err = E_SUCCESS;
    if (wifi_helpers_data.wifi_state.logins_state == NOT_SET)
    {
        err = E_WIFI_LOGINS_NOT_SET;
        goto unlock;
    }
    if (wifi_helpers_data.wifi_state.logins_state == SET_AND_INVALID)
    {
        err = E_WIFI_LOGINS_INVALID;
        goto unlock;
    }
    if (wifi_helpers_data.wifi_state.station_state == STATION_STATE_CONNECTING)
    {
        err = E_IN_PROGRESS;
        goto unlock;
    }
    if (wifi_helpers_data.wifi_state.station_state == STATION_STATE_CONNECTED)
    {
        err = E_ALREADY_DONE;
        goto unlock;
    }
    if (wifi_helpers_data.wifi_state.station_state == STATION_STATE_DISCONNECTING)
    {
        err = E_PERM;
        goto unlock;
    }
    bool password_needed = wifi_helpers_data.password_cache[0] != '\0';
    strncpy(wifi_helpers_data.ssid_stable_cache, wifi_helpers_data.ssid_cache, sizeof(wifi_helpers_data.ssid_stable_cache));
    strncpy(wifi_helpers_data.password_stable_cache, wifi_helpers_data.password_cache, sizeof(wifi_helpers_data.password_stable_cache));

    struct wifi_connect_req_params station_config =
        {
            .ssid = (const uint8_t *)wifi_helpers_data.ssid_stable_cache,
            .ssid_length = strlen(wifi_helpers_data.ssid_stable_cache),
            .psk = (const uint8_t *)wifi_helpers_data.password_stable_cache,
            .psk_length = strlen(wifi_helpers_data.password_stable_cache),
            .security = password_needed ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE,
            .channel = WIFI_CHANNEL_ANY,
            .band = WIFI_FREQ_BAND_2_4_GHZ,
        };

    int ret = net_mgmt(
        NET_REQUEST_WIFI_CONNECT,
        net_if_get_wifi_sta(),
        &station_config,
        sizeof(struct wifi_connect_req_params));
    if (ret != 0)
    {
        err = E_ERROR;
        goto unlock;
    }
    if (err == E_SUCCESS)
    {
        wifi_helpers_data.wifi_state.station_state = STATION_STATE_CONNECTING;
    }
unlock:
    k_mutex_unlock(&wifi_helpers_data.lock);
    return err;
}

enum error_e disable_wifi_station(void)
{
    k_mutex_lock(&wifi_helpers_data.lock, K_FOREVER);
    enum error_e err = E_SUCCESS;
    if (wifi_helpers_data.wifi_state.station_state == STATION_STATE_DISCONNECTING)
    {
        err = E_IN_PROGRESS;
        goto unlock;
    }
    if (wifi_helpers_data.wifi_state.station_state == STATION_STATE_DISCONNECTED)
    {
        err = E_ALREADY_DONE;
        goto unlock;
    }
    if (wifi_helpers_data.wifi_state.station_state == STATION_STATE_CONNECTING)
    {
        err = E_PERM;
        goto unlock;
    }

    int ret = net_mgmt(
        NET_REQUEST_WIFI_DISCONNECT,
        net_if_get_wifi_sta(),
        NULL,
        0);
    if (ret != 0)
    {
        err = E_ERROR;
        goto unlock;
    }
    if (err == E_SUCCESS)
    {
        wifi_helpers_data.wifi_state.station_state = STATION_STATE_DISCONNECTING;
    }
unlock:
    k_mutex_unlock(&wifi_helpers_data.lock);
    return err;
}

enum error_e enable_wifi_ap(void)
{
    k_mutex_lock(&wifi_helpers_data.lock, K_FOREVER);
    enum error_e err = E_SUCCESS;

    if (wifi_helpers_data.wifi_state.ap_state == AP_STATE_ENABLING)
    {
        err = E_IN_PROGRESS;
        goto unlock;
    }
    if (wifi_helpers_data.wifi_state.ap_state == AP_STATE_ENABLED)
    {
        err = E_ALREADY_DONE;
        goto unlock;
    }
    if (wifi_helpers_data.wifi_state.ap_state == AP_STATE_DISABLING)
    {
        err = E_PERM;
        goto unlock;
    }
    err = enable_dhcpv4_server();
    if (err != E_SUCCESS && err != E_ALREADY_DONE)
    {
        err = E_ERROR;
        goto unlock;
    }

    struct net_if *ap_iface = net_if_get_wifi_sap();
    int ret = net_mgmt(
        NET_REQUEST_WIFI_AP_ENABLE,
        ap_iface,
        &wifi_helpers_data.ap_config,
        sizeof(struct wifi_connect_req_params));
    if (ret != 0)
    {
        err = E_ERROR;
        goto unlock;
    }
    if (err == E_SUCCESS)
    {
        wifi_helpers_data.wifi_state.ap_state = AP_STATE_ENABLING;
    }

unlock:
    k_mutex_unlock(&wifi_helpers_data.lock);
    return err;
}

enum error_e disable_wifi_ap(void)
{
    k_mutex_lock(&wifi_helpers_data.lock, K_FOREVER);
    enum error_e err = E_SUCCESS;
    if (wifi_helpers_data.wifi_state.ap_state == AP_STATE_DISABLING)
    {
        err = E_IN_PROGRESS;
        goto unlock;
    }
    if (wifi_helpers_data.wifi_state.ap_state == AP_STATE_DISABLED)
    {
        err = E_ALREADY_DONE;
        goto unlock;
    }
    if (wifi_helpers_data.wifi_state.ap_state == AP_STATE_ENABLING)
    {
        err = E_PERM;
        goto unlock;
    }
    int ret = net_mgmt(
        NET_REQUEST_WIFI_AP_DISABLE,
        net_if_get_wifi_sap(),
        NULL,
        0);
    if (ret != 0)
    {
        err = E_ERROR;
        goto unlock;
    }
    if (err == E_SUCCESS)
    {
        wifi_helpers_data.wifi_state.ap_state = AP_STATE_DISABLING;
    }

unlock:
    k_mutex_unlock(&wifi_helpers_data.lock);
    return err;
}

void get_wifi_state(struct wifi_state_t *w)
{
    k_mutex_lock(&wifi_helpers_data.lock, K_FOREVER);
    memcpy(w, &wifi_helpers_data.wifi_state, sizeof(struct wifi_state_t));
    k_mutex_unlock(&wifi_helpers_data.lock);
}