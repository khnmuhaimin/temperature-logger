#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/logging/log.h>
#include "app/wifi.h"

LOG_MODULE_REGISTER(app_wifi, LOG_LEVEL_DBG);

#define WIFI_AP_SSID "ESP32-AP"
#define WIFI_AP_PSK "password"
#define WIFI_AP_IP_ADDRESS "192.168.4.1"
#define WIFI_AP_NETMASK "255.255.255.0"
#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"
#define WIFI_STATION_TEST_TIMEOUT K_SECONDS(30)
#define WIFI_EVENTS_HANDLED (NET_EVENT_WIFI_CONNECT_RESULT |    \
                             NET_EVENT_WIFI_DISCONNECT_RESULT | \
                             NET_EVENT_WIFI_AP_ENABLE_RESULT |  \
                             NET_EVENT_WIFI_AP_DISABLE_RESULT | \
                             NET_EVENT_WIFI_AP_STA_CONNECTED |  \
                             NET_EVENT_WIFI_AP_STA_DISCONNECTED)

struct wifi_data_t
{
    struct wifi_state_t wifi_state;
    // Credentials used for the last STA attempt
    char station_ssid[WIFI_SSID_MAX_LENGTH + 1];
    char station_password[WIFI_PASSWORD_MAX_LENGTH + 1];
    bool dhcpv_server_enabled;
    struct k_mutex mutex;                // Protects access to this struct
    struct k_condvar connection_condvar; // signals station is connected or disconnected
    struct wifi_connect_req_params ap_config;
    struct net_mgmt_event_callback wifi_event_cb;
    struct net_mgmt_event_callback ip_event_cb;
};

static struct wifi_data_t w_data = {
    .wifi_state = {
        .station_state = STATION_STATE_DISCONNECTED,
        .ap_state = AP_STATE_DISABLED,
        .logins_state = LOGINS_STATE_NOT_SET,
        .power_saving_mode_enabled = false, // TODO: figure out how power saving mode works
    },
    .dhcpv_server_enabled = false,
    .mutex = Z_MUTEX_INITIALIZER(w_data.mutex),
    .connection_condvar = Z_CONDVAR_INITIALIZER(w_data.connection_condvar),
    .ap_config = {
        .ssid = (const uint8_t *)WIFI_AP_SSID,
        .ssid_length = strlen(WIFI_AP_SSID),
        .psk = (const uint8_t *)WIFI_AP_PSK,
        .psk_length = strlen(WIFI_AP_PSK),
        .channel = WIFI_CHANNEL_ANY,
        .band = WIFI_FREQ_BAND_2_4_GHZ,
        .security = WIFI_SECURITY_TYPE_PSK,
    }};

static enum error_e enable_dhcpv4_server_if_disabled(void)
{
    if (w_data.dhcpv_server_enabled)
    {
        return E_ALREADY_DONE;
    }
    struct in_addr addr;
    struct in_addr netmask_addr;
    struct net_if *ap_iface = net_if_get_wifi_sap();

    if (net_addr_pton(AF_INET, WIFI_AP_IP_ADDRESS, &addr))
    {
        return E_ERROR;
    }

    if (net_addr_pton(AF_INET, WIFI_AP_NETMASK, &netmask_addr))
    {
        return E_ERROR;
    }

    net_if_ipv4_set_gw(ap_iface, &addr);

    if (net_if_ipv4_addr_add(ap_iface, &addr, NET_ADDR_MANUAL, 0) == NULL)
    {
        return E_ERROR;
    }

    if (!net_if_ipv4_set_netmask_by_addr(ap_iface, &addr, &netmask_addr))
    {
        return E_ERROR;
    }

    addr.s4_addr[3] += 10; /* Starting IPv4 address for DHCPv4 address pool. */

    if (net_dhcpv4_server_start(ap_iface, &addr) != 0)
    {
        return E_ERROR;
    }
    w_data.dhcpv_server_enabled = true;

    return E_SUCCESS;
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
    switch (mgmt_event)
    {
    case NET_EVENT_WIFI_CONNECT_RESULT:
    {
        k_mutex_lock(&w_data.mutex, K_FOREVER);
        LOG_INF("Connected to %s.", w_data.station_ssid);
        if (w_data.wifi_state.station_state == STATION_STATE_CONNECTING_AND_WITH_IP)
        {
            w_data.wifi_state.station_state = STATION_STATE_CONNECTED;
            w_data.wifi_state.logins_state = LOGINS_STATE_SET_AND_VALID;
            LOG_DBG("Broadcasting stable station state.");
            k_condvar_broadcast(&w_data.connection_condvar);
        }
        else
        {
            w_data.wifi_state.station_state = STATION_STATE_CONNECTED_WITHOUT_IP;
        }
        k_mutex_unlock(&w_data.mutex);
        break;
    }
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
    {
        k_mutex_lock(&w_data.mutex, K_FOREVER);
        LOG_INF("Disconnected from %s.", w_data.station_ssid);
        if (STATION_STATE_IS_CONNECTING(w_data.wifi_state.station_state))
        {
            w_data.wifi_state.logins_state = LOGINS_STATE_SET_AND_INVALID;
        }
        w_data.wifi_state.station_state = STATION_STATE_DISCONNECTED;
        LOG_DBG("Broadcasting stable station state.");
        k_condvar_broadcast(&w_data.connection_condvar);
        k_mutex_unlock(&w_data.mutex);
        break;
    }
    case NET_EVENT_WIFI_AP_ENABLE_RESULT:
    {
        k_mutex_lock(&w_data.mutex, K_FOREVER);
        LOG_INF("AP Mode is enabled. Waiting for station to connect");
        w_data.wifi_state.ap_state = AP_STATE_ENABLED;
        k_mutex_unlock(&w_data.mutex);
        break;
    }
    case NET_EVENT_WIFI_AP_DISABLE_RESULT:
    {
        k_mutex_lock(&w_data.mutex, K_FOREVER);
        LOG_INF("AP Mode is disabled.");
        w_data.wifi_state.ap_state = AP_STATE_DISABLED;
        k_mutex_unlock(&w_data.mutex);
        break;
    }
    case NET_EVENT_WIFI_AP_STA_CONNECTED:
    {
        struct wifi_ap_sta_info *sta_info = (struct wifi_ap_sta_info *)cb->info;
        LOG_INF("station: " MACSTR " joined ", sta_info->mac[0], sta_info->mac[1],
                sta_info->mac[2], sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);
        break;
    }
    case NET_EVENT_WIFI_AP_STA_DISCONNECTED:
    {
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
        k_mutex_lock(&w_data.mutex, K_FOREVER);
        LOG_INF("Got an IP address.");
        if (w_data.wifi_state.station_state == STATION_STATE_CONNECTED_WITHOUT_IP)
        {
            w_data.wifi_state.station_state = STATION_STATE_CONNECTED;
            w_data.wifi_state.logins_state = LOGINS_STATE_SET_AND_VALID;
            LOG_DBG("Broadcasting stable station state.");
            k_condvar_broadcast(&w_data.connection_condvar);
        }
        else
        {
            w_data.wifi_state.station_state = STATION_STATE_CONNECTING_AND_WITH_IP;
        }
        k_mutex_unlock(&w_data.mutex);
    }
}

void init_wifi(void)
{
    net_mgmt_init_event_callback(
        &w_data.wifi_event_cb,
        wifi_event_handler,
        WIFI_EVENTS_HANDLED);
    net_mgmt_init_event_callback(
        &w_data.ip_event_cb,
        ip_event_handler,
        NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&w_data.wifi_event_cb);
    net_mgmt_add_event_callback(&w_data.ip_event_cb);
}

enum error_e set_wifi_logins(char *ssid, char *password)
{
    enum error_e err = E_SUCCESS;
    k_mutex_lock(&w_data.mutex, K_FOREVER);
    if (w_data.wifi_state.station_state != STATION_STATE_DISCONNECTED)
    {
        err = E_PERM;
        goto unlock;
    }
    memset(w_data.station_ssid, 0, sizeof(w_data.station_ssid));
    memset(w_data.station_password, 0, sizeof(w_data.station_password));
    strncpy(w_data.station_ssid, ssid, sizeof(w_data.station_ssid));
    strncpy(w_data.station_password, password, sizeof(w_data.station_password));
    w_data.wifi_state.logins_state = LOGINS_STATE_SET_AND_NOT_TESTED;
unlock:
    k_mutex_unlock(&w_data.mutex);
    return err;
}

enum error_e enable_wifi_station(void)
{
    k_mutex_lock(&w_data.mutex, K_FOREVER);
    enum error_e err = E_SUCCESS;
    enum wifi_station_state_e current_station_state = w_data.wifi_state.station_state;
    if (w_data.wifi_state.logins_state == LOGINS_STATE_NOT_SET)
    {
        err = E_WIFI_LOGINS_NOT_SET;
        goto unlock;
    }
    if (STATION_STATE_IS_CONNECTING(current_station_state))
    {
        err = E_IN_PROGRESS;
        goto unlock;
    }
    if (current_station_state == STATION_STATE_CONNECTED)
    {
        err = E_ALREADY_DONE;
        goto unlock;
    }
    if (current_station_state == STATION_STATE_DISCONNECTING)
    {
        err = E_PERM;
        goto unlock;
    }
    bool password_needed = w_data.station_password[0] != '\0';
    struct wifi_connect_req_params station_config =
        {
            .ssid = (const uint8_t *)w_data.station_ssid,
            .ssid_length = strlen(w_data.station_ssid),
            .psk = (const uint8_t *)w_data.station_password,
            .psk_length = strlen(w_data.station_password),
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
        LOG_WRN("Failed to request station to connect (err=%d).", ret);
        err = E_ERROR;
        goto unlock;
    }
    if (err == E_SUCCESS)
    {
        LOG_DBG("Started connecting to %s.", w_data.station_ssid);
        w_data.wifi_state.station_state = STATION_STATE_CONNECTING;
    }
unlock:
    k_mutex_unlock(&w_data.mutex);
    return err;
}

enum error_e disable_wifi_station(void)
{
    k_mutex_lock(&w_data.mutex, K_FOREVER);
    enum error_e err = E_SUCCESS;
    enum wifi_station_state_e current_station_state = w_data.wifi_state.station_state;
    if (current_station_state == STATION_STATE_DISCONNECTING)
    {
        err = E_IN_PROGRESS;
        goto unlock;
    }
    if (current_station_state == STATION_STATE_DISCONNECTED)
    {
        err = E_ALREADY_DONE;
        goto unlock;
    }
    if (STATION_STATE_IS_CONNECTING(current_station_state))
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
        LOG_WRN("Failed to request station to disconnect (err=%d).", ret);
        err = E_ERROR;
        goto unlock;
    }
    if (err == E_SUCCESS)
    {
        LOG_DBG("Started disconnecting from %s.", w_data.station_ssid);
        w_data.wifi_state.station_state = STATION_STATE_DISCONNECTING;
    }
unlock:
    k_mutex_unlock(&w_data.mutex);
    return err;
}

enum error_e enable_wifi_ap(void)
{
    k_mutex_lock(&w_data.mutex, K_FOREVER);
    enum error_e err = E_SUCCESS;

    if (w_data.wifi_state.ap_state == AP_STATE_ENABLING)
    {
        err = E_IN_PROGRESS;
        goto unlock;
    }
    if (w_data.wifi_state.ap_state == AP_STATE_ENABLED)
    {
        err = E_ALREADY_DONE;
        goto unlock;
    }
    if (w_data.wifi_state.ap_state == AP_STATE_DISABLING)
    {
        err = E_PERM;
        goto unlock;
    }
    err = enable_dhcpv4_server_if_disabled();
    if (err != E_SUCCESS && err != E_ALREADY_DONE)
    {
        LOG_WRN("Failed to start DHCVP4 server.");
        err = E_ERROR;
        goto unlock;
    }

    struct net_if *ap_iface = net_if_get_wifi_sap();
    int ret = net_mgmt(
        NET_REQUEST_WIFI_AP_ENABLE,
        ap_iface,
        &w_data.ap_config,
        sizeof(struct wifi_connect_req_params));
    if (ret != 0)
    {
        LOG_WRN("Failed to request AP to start (err=%d).", ret);
        err = E_ERROR;
        goto unlock;
    }
    if (err == E_SUCCESS)
    {
        LOG_DBG("Starting AP.");
        w_data.wifi_state.ap_state = AP_STATE_ENABLING;
    }

unlock:
    k_mutex_unlock(&w_data.mutex);
    return err;
}

enum error_e disable_wifi_ap(void)
{
    k_mutex_lock(&w_data.mutex, K_FOREVER);
    enum error_e err = E_SUCCESS;
    if (w_data.wifi_state.ap_state == AP_STATE_DISABLING)
    {
        err = E_IN_PROGRESS;
        goto unlock;
    }
    if (w_data.wifi_state.ap_state == AP_STATE_DISABLED)
    {
        err = E_ALREADY_DONE;
        goto unlock;
    }
    if (w_data.wifi_state.ap_state == AP_STATE_ENABLING)
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
        LOG_WRN("Failed to request AP to shut down (err=%d).", ret);
        err = E_ERROR;
        goto unlock;
    }
    if (err == E_SUCCESS)
    {
        LOG_DBG("Shutting down AP.");
        w_data.wifi_state.ap_state = AP_STATE_DISABLING;
    }

unlock:
    k_mutex_unlock(&w_data.mutex);
    return err;
}

void get_wifi_state(struct wifi_state_t *w)
{
    k_mutex_lock(&w_data.mutex, K_FOREVER);
    memcpy(w, &w_data.wifi_state, sizeof(struct wifi_state_t));
    k_mutex_unlock(&w_data.mutex);
}

static enum error_e wait_for_station_steady_state_without_locking(k_timeout_t timeout)
{
    enum wifi_station_state_e current = w_data.wifi_state.station_state;

    if (current == STATION_STATE_CONNECTED || current == STATION_STATE_DISCONNECTED)
    {
        return E_SUCCESS;
    }

    LOG_DBG("Station is transient. Waiting on condvar for timeout.");
    int result = k_condvar_wait(&w_data.connection_condvar, &w_data.mutex, timeout);
    if (result == -EAGAIN)
    {
        LOG_WRN("Station did not reach steady state within the timeout.");
        return E_TIMEOUT;
    }

    return E_SUCCESS;
}

enum error_e wait_for_station_steady_state(k_timeout_t timeout)
{
    k_mutex_lock(&w_data.mutex, K_FOREVER);
    enum error_e err = wait_for_station_steady_state_without_locking(timeout);
    k_mutex_unlock(&w_data.mutex);
    return err;
}

enum error_e test_wifi_logins(enum logins_state_e *state)
{
    k_mutex_lock(&w_data.mutex, K_FOREVER);
    enum error_e err = E_SUCCESS;

    if (w_data.wifi_state.logins_state == LOGINS_STATE_NOT_SET)
    {
        LOG_WRN("Attempted to test wifi without setting logins.");
        *state = LOGINS_STATE_NOT_SET;
        goto unlock;
    }

    LOG_DBG("Starting test for wifi logins.");
    err = wait_for_station_steady_state_without_locking(WIFI_STATION_TEST_TIMEOUT);
    if (err == E_TIMEOUT)
    {
        goto unlock;
    }

    // state is either disconnected or connected now
    // if already connected or logins not set
    // then login state is already known
    if (w_data.wifi_state.station_state == STATION_STATE_DISCONNECTED)
    {
        enable_wifi_station();
        err = wait_for_station_steady_state_without_locking(WIFI_STATION_TEST_TIMEOUT);
        if (err == E_TIMEOUT)
        {
            goto unlock;
        }
    }
    *state = w_data.wifi_state.logins_state;
    LOG_INF("Test complete. Login state determined: %d.", *state);

    disable_wifi_station();
unlock:
    k_mutex_unlock(&w_data.mutex);
    return err;
}