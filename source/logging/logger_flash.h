#ifndef LOGGER_FLASH_H
#define LOGGER_FLASH_H

/* ============================================================================
 * logger_flash.h -- QSPI flash sink for the logger: a raw, self-framed ring
 * buffer (no filesystem lib is vendored in this project). See logger_flash.c
 * for the on-flash layout.
 *
 * All flash I/O (including occasional multi-second sector erases) happens on
 * a dedicated low-priority task fed by its own queue, so a slow erase never
 * stalls logger_task's UART/CSV path -- the two sinks are fully decoupled.
 * ============================================================================ */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cy_result.h"
#include "log_sample.h"
#include "logger.h"       /* LOGGER_LABEL_MAX */

cy_rslt_t logger_flash_init(void);   /* qspi_flash_init() + superblock + writer task */

/* Called from logger_task at the SRC_IMU cadence point with the current
 * merged sensor cache (same s_latest/s_seen logger.c already maintains for
 * the CSV path -- this just serializes that same merged view, it does not
 * duplicate the merge/caching logic itself). Cheap: builds a fixed-size
 * record and enqueues it; never blocks on flash I/O. */
void logger_flash_enqueue_sample(const log_sample_t latest[5], const bool seen[5],
                                  uint32_t ts_ms, const char *label);

/* Called once per `start` (logger_set_enabled false->true) while the flash
 * sink is active, so `dump` can find session boundaries. */
void logger_flash_enqueue_session_start(uint32_t ts_ms, const char *label);

void   logger_flash_dump_as_csv(void);   /* CLI `dump` -- replay all sessions as CSV over UART */
void   logger_flash_erase_all(void);     /* CLI `erase confirm` -- wipe the ring buffer */
bool   logger_flash_is_halted(void);     /* true once the ring buffer is full */
size_t logger_flash_capacity_records(void);
size_t logger_flash_used_records(void);

#endif
