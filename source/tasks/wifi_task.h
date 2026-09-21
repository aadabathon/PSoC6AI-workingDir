#ifndef WIFI_TASK_H
#define WIFI_TASK_H

/* ============================================================================
 * wifi_task.h -- joins the Wi-Fi AP configured in wifi_config.h and runs a
 * one-directional TCP push server: the logger's CSV rows get streamed to
 * whichever client is currently connected, so analysis/collect_tcp.py can
 * capture over the network instead of the serial port.
 *
 * Unlike Infineon's reference TCP server examples (bidirectional command/
 * ack), this is deliberately push-only -- there's nothing to receive.
 * ============================================================================ */

#include <stdbool.h>
#include <stddef.h>
#include "cy_result.h"

cy_rslt_t wifi_task_init(void);

/* Best-effort push to the connected TCP client, if any. Never blocks the
 * caller on a slow/absent network peer: no-ops immediately if Wi-Fi isn't
 * joined or no client is connected, and the underlying socket send has a
 * short timeout so a stalled peer can't stall logger_task. */
void wifi_tcp_send(const char *buf, size_t len);

bool wifi_task_is_joined(void);
bool wifi_task_is_client_connected(void);

/* Writes a dotted-quad string (or "0.0.0.0" if not joined) into buf. */
void wifi_task_get_ip_str(char *buf, size_t buflen);

#endif
