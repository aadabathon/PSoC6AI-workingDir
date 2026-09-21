/* ============================================================================
 * wifi_task.c -- Wi-Fi join + one-directional TCP push server.
 *
 * Setup runs once in wifi_task() (join AP, create/bind/listen the server
 * socket), then everything else is callback-driven by the secure-sockets
 * library: tcp_connection_handler accepts a client and stashes the handle,
 * tcp_disconnection_handler clears it. wifi_tcp_send() (called from
 * logger_task) pushes to whatever's currently stashed -- protected by a
 * mutex the same way i2c_bus.c guards its shared handle, taken with a 0-tick
 * timeout so a contended mutex just means "drop this row", never a stall.
 * ============================================================================ */

#include "wifi_task.h"
#include "wifi_config.h"

#include "cy_wcm.h"
#include "cy_wcm_error.h"
#include "cy_secure_sockets.h"
#include "cy_nw_helper.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define WIFI_TASK_STACK_WORDS       (1024U * 5U)   /* WCM+lwIP+mbedtls init is stack-hungry */
#define WIFI_TASK_PRIORITY          (tskIDLE_PRIORITY + 2)
#define WIFI_TCP_PORT                (50007U)       /* matches Infineon's reference example */
#define WIFI_MAX_PENDING_CONN        (1U)           /* one data client at a time */
#define WIFI_SERVER_RCV_TIMEOUT_MS   (500U)
#define WIFI_CLIENT_SND_TIMEOUT_MS   (50U)           /* caps how long a send can block */
#define WIFI_MAX_JOIN_RETRIES        (10U)
#define WIFI_JOIN_RETRY_INTERVAL_MS  (1000U)
#define WIFI_RECV_SCRATCH_LEN        (32U)            /* one-directional: incoming bytes are drained, not used */

static cy_socket_t s_server_handle;
static cy_socket_t s_client_handle;
static SemaphoreHandle_t s_client_mutex = NULL;
static volatile bool s_client_connected = false;
static volatile bool s_joined = false;
static cy_wcm_ip_address_t s_ip_addr;

static bool wifi_configured(void)
{
    return strcmp(WIFI_SSID, "CHANGE_ME_SSID") != 0;
}

static cy_rslt_t tcp_receive_handler(cy_socket_t socket_handle, void *arg)
{
    (void)arg;
    char scratch[WIFI_RECV_SCRATCH_LEN];
    uint32_t bytes_received = 0;

    /* Nothing to do with whatever the client sends -- this is a push-only
     * stream -- but the byte(s) must still be drained off the socket. */
    cy_rslt_t result = cy_socket_recv(socket_handle, scratch, sizeof(scratch),
                                      CY_SOCKET_FLAGS_NONE, &bytes_received);

    if (result == CY_RSLT_MODULE_SECURE_SOCKETS_CLOSED)
    {
        cy_socket_disconnect(socket_handle, 0);
        cy_socket_delete(socket_handle);
    }
    return CY_RSLT_SUCCESS;
}

static cy_rslt_t tcp_disconnection_handler(cy_socket_t socket_handle, void *arg)
{
    (void)arg;
    cy_socket_disconnect(socket_handle, 0);
    cy_socket_delete(socket_handle);

    if (xSemaphoreTake(s_client_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_client_connected = false;
        xSemaphoreGive(s_client_mutex);
    }
    printf("[wifi] client disconnected\r\n");
    return CY_RSLT_SUCCESS;
}

static cy_rslt_t tcp_connection_handler(cy_socket_t socket_handle, void *arg)
{
    (void)arg;
    cy_socket_sockaddr_t peer_addr;
    uint32_t peer_addr_len = sizeof(peer_addr);
    cy_socket_t client_handle;

    cy_rslt_t result = cy_socket_accept(socket_handle, &peer_addr, &peer_addr_len, &client_handle);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("[wifi] accept failed: 0x%08" PRIx32 "\r\n", (uint32_t)result);
        return result;
    }

    /* Cap send blocking time so a stalled/slow client can never stall
     * logger_task -- wifi_tcp_send()'s cy_socket_send() inherits this. */
    uint32_t snd_timeout = WIFI_CLIENT_SND_TIMEOUT_MS;
    cy_socket_setsockopt(client_handle, CY_SOCKET_SOL_SOCKET, CY_SOCKET_SO_SNDTIMEO,
                         &snd_timeout, sizeof(snd_timeout));

    int keep_alive = 1;
    cy_socket_setsockopt(client_handle, CY_SOCKET_SOL_SOCKET, CY_SOCKET_SO_TCP_KEEPALIVE_ENABLE,
                         &keep_alive, sizeof(keep_alive));

    cy_socket_opt_callback_t recv_opt = { .callback = tcp_receive_handler, .arg = NULL };
    cy_socket_setsockopt(client_handle, CY_SOCKET_SOL_SOCKET, CY_SOCKET_SO_RECEIVE_CALLBACK,
                         &recv_opt, sizeof(recv_opt));

    cy_socket_opt_callback_t disc_opt = { .callback = tcp_disconnection_handler, .arg = NULL };
    cy_socket_setsockopt(client_handle, CY_SOCKET_SOL_SOCKET, CY_SOCKET_SO_DISCONNECT_CALLBACK,
                         &disc_opt, sizeof(disc_opt));

    char ip_str[20];
    cy_nw_ip_address_t nw_ip = { .version = NW_IP_IPV4 };
    nw_ip.ip.v4 = peer_addr.ip_address.ip.v4;
    cy_nw_ntoa(&nw_ip, ip_str);

    if (xSemaphoreTake(s_client_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_client_handle    = client_handle;
        s_client_connected = true;
        xSemaphoreGive(s_client_mutex);
    }
    printf("[wifi] client connected: %s\r\n", ip_str);

    return CY_RSLT_SUCCESS;
}

static cy_rslt_t create_tcp_server_socket(void)
{
    cy_rslt_t result = cy_socket_create(CY_SOCKET_DOMAIN_AF_INET, CY_SOCKET_TYPE_STREAM,
                                        CY_SOCKET_IPPROTO_TCP, &s_server_handle);
    if (result != CY_RSLT_SUCCESS) return result;

    uint32_t rcv_timeout = WIFI_SERVER_RCV_TIMEOUT_MS;
    cy_socket_setsockopt(s_server_handle, CY_SOCKET_SOL_SOCKET, CY_SOCKET_SO_RCVTIMEO,
                         &rcv_timeout, sizeof(rcv_timeout));

    cy_socket_opt_callback_t conn_opt = { .callback = tcp_connection_handler, .arg = NULL };
    result = cy_socket_setsockopt(s_server_handle, CY_SOCKET_SOL_SOCKET,
                                  CY_SOCKET_SO_CONNECT_REQUEST_CALLBACK,
                                  &conn_opt, sizeof(conn_opt));
    if (result != CY_RSLT_SUCCESS) return result;

    cy_socket_sockaddr_t server_addr = {
        .ip_address = { .ip.v4 = s_ip_addr.ip.v4, .version = CY_SOCKET_IP_VER_V4 },
        .port       = WIFI_TCP_PORT,
    };

    result = cy_socket_bind(s_server_handle, &server_addr, sizeof(server_addr));
    return result;
}

static cy_rslt_t connect_to_wifi_ap(void)
{
    cy_wcm_connect_params_t params;
    memset(&params, 0, sizeof(params));
    memcpy(params.ap_credentials.SSID, WIFI_SSID, sizeof(WIFI_SSID));
    memcpy(params.ap_credentials.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
    params.ap_credentials.security = WIFI_SECURITY_TYPE;

    printf("[wifi] joining '%s'...\r\n", WIFI_SSID);

    for (uint32_t attempt = 0; attempt < WIFI_MAX_JOIN_RETRIES; attempt++)
    {
        cy_rslt_t result = cy_wcm_connect_ap(&params, &s_ip_addr);
        if (result == CY_RSLT_SUCCESS)
        {
            char ip_str[20];
            cy_nw_ip_address_t nw_ip = { .version = NW_IP_IPV4, .ip.v4 = s_ip_addr.ip.v4 };
            cy_nw_ntoa(&nw_ip, ip_str);
            printf("[wifi] joined. IP=%s\r\n", ip_str);
            return CY_RSLT_SUCCESS;
        }
        printf("[wifi] join attempt %lu failed: 0x%08" PRIx32 " -- retrying\r\n",
               (unsigned long)(attempt + 1), (uint32_t)result);
        vTaskDelay(pdMS_TO_TICKS(WIFI_JOIN_RETRY_INTERVAL_MS));
    }
    return CY_RSLT_TYPE_ERROR;
}

static void wifi_task(void *arg)
{
    (void)arg;

    if (!wifi_configured())
    {
        printf("[wifi] not configured -- edit source/tasks/wifi_config.h "
               "(WIFI_SSID/WIFI_PASSWORD) to enable\r\n");
        vTaskDelete(NULL);
        return;
    }

    cy_wcm_config_t wcm_config = { .interface = CY_WCM_INTERFACE_TYPE_STA };
    cy_rslt_t result = cy_wcm_init(&wcm_config);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("[wifi] cy_wcm_init failed: 0x%08" PRIx32 "\r\n", (uint32_t)result);
        vTaskDelete(NULL);
        return;
    }

    if (connect_to_wifi_ap() != CY_RSLT_SUCCESS)
    {
        printf("[wifi] giving up after %u attempts -- TCP sink unavailable this boot\r\n",
               (unsigned)WIFI_MAX_JOIN_RETRIES);
        vTaskDelete(NULL);
        return;
    }
    s_joined = true;

    result = cy_socket_init();
    if (result != CY_RSLT_SUCCESS)
    {
        printf("[wifi] cy_socket_init failed: 0x%08" PRIx32 "\r\n", (uint32_t)result);
        vTaskDelete(NULL);
        return;
    }

    if (create_tcp_server_socket() != CY_RSLT_SUCCESS)
    {
        printf("[wifi] failed to create/bind TCP server socket\r\n");
        vTaskDelete(NULL);
        return;
    }

    if (cy_socket_listen(s_server_handle, WIFI_MAX_PENDING_CONN) != CY_RSLT_SUCCESS)
    {
        cy_socket_delete(s_server_handle);
        printf("[wifi] cy_socket_listen failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    printf("[wifi] TCP server listening on port %u -- `sink tcp` (or `sink all`) "
           "to stream CSV rows here\r\n", (unsigned)WIFI_TCP_PORT);

    /* Everything past here is callback-driven; this task has no further
     * work. A slow heartbeat keeps it alive/visible rather than deleting
     * it, matching this codebase's other long-lived tasks. */
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

cy_rslt_t wifi_task_init(void)
{
    s_client_mutex = xSemaphoreCreateMutex();
    if (s_client_mutex == NULL) return CY_RSLT_TYPE_ERROR;

    BaseType_t ok = xTaskCreate(wifi_task, "wifi", WIFI_TASK_STACK_WORDS, NULL,
                                WIFI_TASK_PRIORITY, NULL);
    return (ok == pdPASS) ? CY_RSLT_SUCCESS : CY_RSLT_TYPE_ERROR;
}

void wifi_tcp_send(const char *buf, size_t len)
{
    if (!s_joined || s_client_mutex == NULL) return;
    if (xSemaphoreTake(s_client_mutex, 0) != pdTRUE) return;   /* never block the caller */

    if (s_client_connected)
    {
        uint32_t bytes_sent = 0;
        /* Best-effort: a failed/timed-out send just drops this row. The
         * send timeout (WIFI_CLIENT_SND_TIMEOUT_MS) bounds worst case; a
         * hard failure here means the peer is likely already gone and the
         * disconnect callback will clean up s_client_handle separately. */
        (void)cy_socket_send(s_client_handle, buf, len, CY_SOCKET_FLAGS_NONE, &bytes_sent);
    }

    xSemaphoreGive(s_client_mutex);
}

bool wifi_task_is_joined(void)            { return s_joined; }
bool wifi_task_is_client_connected(void)  { return s_client_connected; }

void wifi_task_get_ip_str(char *buf, size_t buflen)
{
    if (!s_joined)
    {
        snprintf(buf, buflen, "0.0.0.0");
        return;
    }
    cy_nw_ip_address_t nw_ip = { .version = NW_IP_IPV4, .ip.v4 = s_ip_addr.ip.v4 };
    char tmp[20];
    cy_nw_ntoa(&nw_ip, tmp);
    snprintf(buf, buflen, "%s", tmp);
}
