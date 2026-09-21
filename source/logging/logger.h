#ifndef LOGGER_H
#define LOGGER_H

/* ============================================================================
 * logger.h -- single owner of the output stream(s): UART and/or QSPI flash
 * (see logger_flash.h for the flash ring-buffer format).
 *
 * Producers call logger_post() with a filled log_sample_t. The logger task
 * drains the queue and holds the latest value of each sensor. In CSV mode
 * a merged row is emitted per IMU sample -- the IMU is the cadence master,
 * so every row is one complete feature vector at the IMU rate, which is
 * exactly what ML wants. Sensors that haven't reported yet emit empty
 * fields (pandas reads those as NaN, not fake zeros).
 *
 * Two output modes, switchable at runtime:
 *   LOG_MODE_HUMAN -- pretty [baro] P=... lines, for debugging
 *   LOG_MODE_CSV   -- machine-parseable merged rows, for data collection
 *
 * Recording is gated: nothing is emitted until logger_set_enabled(true)
 * (CLI `start` or the user button). Each start reprints the CSV header so
 * every capture session is self-describing.
 * ============================================================================ */

#include <stdbool.h>
#include <stdint.h>
#include "cy_result.h"
#include "log_sample.h"
#include "FreeRTOS.h"

typedef enum { LOG_MODE_HUMAN = 0, LOG_MODE_CSV = 1 } log_mode_t;

/* Output sink(s). Default is UART only -- flash/TCP must be explicitly
 * opted into via logger_set_sink(), so the existing UART-only workflow is
 * completely unaffected unless the user asks for them. */
typedef enum {
    LOGGER_SINK_UART  = 1U << 0,
    LOGGER_SINK_FLASH = 1U << 1,
    LOGGER_SINK_TCP   = 1U << 2,
} logger_sink_t;

#define LOGGER_LABEL_MAX  (16U)   /* incl. NUL -- keep labels short */

cy_rslt_t logger_init(void);                 /* create queue + logger task */
void      logger_post(const log_sample_t *s);/* producers call this (task context only) */
void      logger_set_mode(log_mode_t mode);  /* flip human/CSV at runtime */
log_mode_t logger_get_mode(void);

void    logger_set_sink(uint8_t mask);       /* bitmask of logger_sink_t */
uint8_t logger_get_sink(void);

/* Recording gate. ISR-safe (plain flag writes). */
void logger_set_enabled(bool on);
bool logger_get_enabled(void);

/* Activity label -- lands in the last CSV column so training data is
 * annotated at collection time instead of hand-aligned later. */
void logger_set_label(const char *label);

#endif
