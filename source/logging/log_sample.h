#ifndef LOG_SAMPLE_H
#define LOG_SAMPLE_H

/* ============================================================================
 * log_sample.h -- the one data structure all sensors push to the logger.
 *
 * Every producer (baro/imu/mag/radar/mic task) fills one of these and sends
 * it to the logger queue. The logger owns the UART and formats output, so
 * producers never printf directly -- that's what keeps CSV rows intact.
 *
 * Tagged union: `src` says which sensor, `d` holds that sensor's fields.
 * ============================================================================ */

#include <stdint.h>

typedef enum {
    SRC_BARO  = 0,
    SRC_IMU   = 1,
    SRC_MAG   = 2,
    SRC_RADAR = 3,
    SRC_MIC   = 4,
} sample_src_t;

typedef struct {
    uint32_t     ts_ms;        /* logger-stamped timestamp, one clock */
    sample_src_t src;
    union {
        struct { float pa, temp_c; }                              baro;
        struct { float ax, ay, az, gx, gy, gz, temp_c; }          imu;
        struct { float mx, my, mz, temp_c; }                      mag;
        struct { int32_t presence, range_bin; }                   radar;
        struct { float rms; int16_t peak; }                       mic;
    } d;
} log_sample_t;

#endif