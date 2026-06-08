#ifndef LOGGER_H
#define LOGGER_H

/* ============================================================================
 * logger.h -- single owner of the output stream (UART now, QSPI later).
 *
 * Producers call logger_post() with a filled log_sample_t. The logger task
 * drains the queue, holds the latest value of each sensor, and emits a
 * merged CSV row at a fixed cadence -- one complete feature vector per row,
 * which is exactly what ML wants.
 *
 * Two output modes, switchable at runtime:
 *   LOG_MODE_HUMAN -- pretty [baro] P=... lines, for debugging
 *   LOG_MODE_CSV   -- machine-parseable merged rows, for data collection
 * ============================================================================ */

#include "cy_result.h"
#include "log_sample.h"
#include "FreeRTOS.h"

typedef enum { LOG_MODE_HUMAN = 0, LOG_MODE_CSV = 1 } log_mode_t;

cy_rslt_t logger_init(void);                 /* create queue + logger task */
void      logger_post(const log_sample_t *s);/* producers call this (ISR-safe variant inside) */
void      logger_set_mode(log_mode_t mode);  /* flip human/CSV at runtime */
log_mode_t logger_get_mode(void);

#endif