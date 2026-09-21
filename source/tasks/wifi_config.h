#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

/* ============================================================================
 * wifi_config.h -- EDIT THIS FILE before you want Wi-Fi TCP streaming to
 * actually join a network. See source/tasks/wifi_task.c for how these are
 * used.
 *
 * If WIFI_SSID is left as the placeholder below, wifi_task_init() skips the
 * join attempt entirely (no retry loop, no boot delay) and just prints a
 * one-line notice pointing back at this file.
 * ============================================================================ */

#include "cy_wcm.h"

#define WIFI_SSID           "CHANGE_ME_SSID"
#define WIFI_PASSWORD        "CHANGE_ME_PASSWORD"
#define WIFI_SECURITY_TYPE   CY_WCM_SECURITY_WPA2_AES_PSK

#endif
