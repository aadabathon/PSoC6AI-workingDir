#ifndef DPS368_H
#define DPS368_H

/* ============================================================================
 * dps368.h -- Driver for Infineon DPS368 pressure + temperature sensor
 *
 * The DPS368 lives on the CY8CKIT-062S2-AI board on the main I2C bus. It
 * gives you pressure (Pa) and temperature (deg C) from two independent
 * 24-bit ADCs.
 *
 * Usage:
 *   1. Init a cyhal_i2c_t in master mode somewhere (probably in main).
 *   2. dps368_init(&dev, &i2c)        -- probes chip, loads cal coeffs.
 *   3. dps368_read(&dev, &reading)    -- whenever you want a sample.
 *
 * The compensation polynomial (datasheet section 4.9.1) is hidden inside
 * dps368_read, so the values you get are already in real-world units.
 * ============================================================================
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "cy_result.h"
#include "cyhal.h"
#include "cyhal.h"

/* ----------------------------------------------------------------------------
 * I2C address
 *
 * DPS368 is at 0x76 or 0x77 depending on the SDO strap. On the AI kit it's
 * 0x77 per the schematic -- but if init fails with "device not found", this
 * is the very first thing to double-check.
 * ---------------------------------------------------------------------------- */
#define DPS368_I2C_ADDR              (0x77U)

/* PRODUCT_ID register should read this. Sanity check at init time. */
#define DPS368_PRODUCT_ID_VALUE      (0x10U)

/* ----------------------------------------------------------------------------
 * Register map (only the ones we use -- full map in datasheet section 7)
 * ---------------------------------------------------------------------------- */
#define DPS368_REG_PSR_B2            (0x00U)  /* Pressure data MSB     */
#define DPS368_REG_PSR_B1            (0x01U)
#define DPS368_REG_PSR_B0            (0x02U)  /* Pressure data LSB     */
#define DPS368_REG_TMP_B2            (0x03U)  /* Temperature data MSB  */
#define DPS368_REG_TMP_B1            (0x04U)
#define DPS368_REG_TMP_B0            (0x05U)  /* Temperature data LSB  */
#define DPS368_REG_PRS_CFG           (0x06U)  /* Pressure rate + OSR   */
#define DPS368_REG_TMP_CFG           (0x07U)  /* Temp rate + OSR       */
#define DPS368_REG_MEAS_CFG          (0x08U)  /* Mode + status flags   */
#define DPS368_REG_CFG_REG           (0x09U)  /* Interrupts + bit-shift */
#define DPS368_REG_RESET             (0x0CU)  /* Soft reset            */
#define DPS368_REG_PRODUCT_ID        (0x0DU)
#define DPS368_REG_COEF_BASE         (0x10U)  /* 18 bytes of cal data  */
#define DPS368_REG_COEF_SRCE         (0x28U)

/* Status bits inside MEAS_CFG */
#define DPS368_MEAS_COEF_RDY         (1U << 7)  /* Cal coeffs readable */
#define DPS368_MEAS_SENSOR_RDY       (1U << 6)  /* Chip self-test done */
#define DPS368_MEAS_TMP_RDY          (1U << 5)  /* New temp sample     */
#define DPS368_MEAS_PRS_RDY          (1U << 4)  /* New pressure sample */

/* Operating mode (lower 3 bits of MEAS_CFG) */
#define DPS368_MODE_STANDBY          (0x00U)
#define DPS368_MODE_CMD_PRESSURE     (0x01U)
#define DPS368_MODE_CMD_TEMPERATURE  (0x02U)
#define DPS368_MODE_CONT_PRESSURE    (0x05U)
#define DPS368_MODE_CONT_TEMP        (0x06U)
#define DPS368_MODE_CONT_BOTH        (0x07U)

#define DPS368_SOFT_RESET_VALUE      (0x09U)

/* Compensation scale factor for 16x oversampling (datasheet table 9). If you
 * ever change OSR, also change this. */
#define DPS368_SCALE_FACTOR_16X      (253952.0f)

/* ----------------------------------------------------------------------------
 * Calibration coefficients
 *
 * Nine factory-trimmed values, read once at startup and reused forever.
 * Packed weirdly in OTP -- the unpack lives in dps368.c.
 *   c0, c1                  : 12-bit signed, temperature
 *   c00, c10                : 20-bit signed, pressure offsets
 *   c01, c11, c20, c21, c30 : 16-bit signed, pressure-vs-temp cross terms
 * ---------------------------------------------------------------------------- */
typedef struct
{
    int32_t c0;
    int32_t c1;
    int32_t c00;
    int32_t c10;
    int32_t c01;
    int32_t c11;
    int32_t c20;
    int32_t c21;
    int32_t c30;
} dps368_calib_t;

/* ----------------------------------------------------------------------------
 * Driver context -- one per chip. Pass a pointer into every fn so the driver
 * itself stays stateless. Good hygiene; also lets you have multiple sensors.
 * ---------------------------------------------------------------------------- */
typedef struct
{
    cyhal_i2c_t    *i2c;          /* I2C master used to talk to the chip   */
    dps368_calib_t  calib;        /* Cached cal coeffs (loaded once)       */
    float           last_temp_c;  /* Last temp; pressure compensation needs it */
    bool            initialized;
} dps368_t;

/* ----------------------------------------------------------------------------
 * One compensated sample
 * ---------------------------------------------------------------------------- */
typedef struct
{
    float pressure_pa;     /* Pascals (~101325 at sea level)   */
    float temperature_c;   /* Degrees Celsius                  */
} dps368_reading_t;

/* ----------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------- */

/**
 * Probe chip, soft-reset, load calibration, set 16x OSR, kick into
 * continuous "both" mode.
 *
 * @param dev  pointer to a (zero-initialized) driver context to fill in
 * @param i2c  pre-initialized cyhal_i2c_t in master mode
 * @return CY_RSLT_SUCCESS or an error
 */
cy_rslt_t dps368_init(dps368_t *dev, cyhal_i2c_t *i2c);

/**
 * Read latest pressure + temperature and apply the compensation polynomial.
 * Blocks until both PRS_RDY and TMP_RDY are set (<10 ms at 16x OSR).
 *
 * @param dev      initialized driver context
 * @param reading  filled in with real-world values
 * @return CY_RSLT_SUCCESS or an error
 */
cy_rslt_t dps368_read(dps368_t *dev, dps368_reading_t *reading);

#endif /* DPS368_H */