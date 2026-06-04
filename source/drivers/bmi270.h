#ifndef BMI270_H
#define BMI270_H

/* ============================================================================
 * bmi270.h -- Driver for Bosch BMI270 6-axis IMU (accel + gyro + temp)
 *
 * On CY8CKIT-062S2-AI the BMI270 is on the main I2C bus at 0x68 and shares
 * that bus with the DPS368 (at 0x77). The shared bus is owned by i2c_bus.c
 * and arbitrated by its mutex; this driver just calls i2c_bus_lock/unlock
 * around its transactions.
 *
 * Unlike the DPS368, the BMI270 is not usable out of cold-boot. Its on-die
 * feature engine (a tiny coprocessor inside the chip) has empty RAM. You
 * MUST upload an 8 KB firmware blob ("config file") on every power-up
 * before the chip will produce any sensor data. The blob lives as a const
 * array in bmi270_config_file.c.
 * ============================================================================
 */

#include <stdint.h>
#include <stdbool.h>
#include "cyhal.h"
#include "cy_result.h"

/* ----------------------------------------------------------------------------
 * I2C address (SDO strap -- 0x68 by default on this board; 0x69 if R66
 * is populated, but it isn't from the factory).
 * ---------------------------------------------------------------------------- */
#define BMI270_I2C_ADDR              (0x68U)

/* CHIP_ID register reads this value when alive. */
#define BMI270_CHIP_ID_VALUE         (0x24U)

/* ----------------------------------------------------------------------------
 * Register map (only the ones we use; full map in datasheet section 5)
 * ---------------------------------------------------------------------------- */
#define BMI270_REG_CHIP_ID           (0x00U)
#define BMI270_REG_ERR               (0x02U)
#define BMI270_REG_STATUS            (0x03U)

#define BMI270_REG_DATA_8            (0x0CU)  /* ACC_X LSB; 12 bytes accel+gyro */
#define BMI270_REG_TEMPERATURE_0     (0x22U)  /* 16-bit, signed, LSB-first      */
#define BMI270_REG_INTERNAL_STATUS   (0x21U)

#define BMI270_REG_ACC_CONF          (0x40U)
#define BMI270_REG_ACC_RANGE         (0x41U)
#define BMI270_REG_GYR_CONF          (0x42U)
#define BMI270_REG_GYR_RANGE         (0x43U)

#define BMI270_REG_INIT_CTRL         (0x59U)
#define BMI270_REG_INIT_ADDR_0       (0x5BU)  /* low/high nibble offset    */
#define BMI270_REG_INIT_ADDR_1       (0x5CU)
#define BMI270_REG_INIT_DATA         (0x5EU)  /* burst-write blob here     */

#define BMI270_REG_PWR_CONF          (0x7CU)
#define BMI270_REG_PWR_CTRL          (0x7DU)
#define BMI270_REG_CMD               (0x7EU)

#define BMI270_CMD_SOFT_RESET        (0xB6U)

/* PWR_CTRL bits */
#define BMI270_PWR_CTRL_AUX_EN       (1U << 0)
#define BMI270_PWR_CTRL_GYR_EN       (1U << 1)
#define BMI270_PWR_CTRL_ACC_EN       (1U << 2)
#define BMI270_PWR_CTRL_TEMP_EN      (1U << 3)

/* INTERNAL_STATUS: 0x01 == "init OK". Anything else after init = bad. */
#define BMI270_INIT_STATUS_OK        (0x01U)

/* Config file is 8192 bytes -- see bmi270_config_file.c */
#define BMI270_CONFIG_FILE_SIZE      (8192U)
extern const uint8_t bmi270_config_file[BMI270_CONFIG_FILE_SIZE];

/* ----------------------------------------------------------------------------
 * Range / scale factor -- we hardcode ±4g and ±2000 dps as a reasonable
 * default for motion/gesture work. If you change RANGE in init, also
 * change the LSB-to-physical conversion in bmi270_read.
 *
 *   Accel: ±4g, 16-bit signed -> LSB = 4g / 2^15 = 1.22e-4 g/LSB
 *          Multiply by 9.80665 to get m/s^2.
 *   Gyro:  ±2000 dps, 16-bit signed -> LSB = 2000 / 2^15 = 0.061 dps/LSB
 * ---------------------------------------------------------------------------- */
#define BMI270_ACC_LSB_TO_MPS2       (4.0f * 9.80665f / 32768.0f)
#define BMI270_GYR_LSB_TO_DPS        (2000.0f / 32768.0f)

/* ----------------------------------------------------------------------------
 * Driver context -- pattern matches dps368_t.
 * ---------------------------------------------------------------------------- */
typedef struct
{
    cyhal_i2c_t *i2c;
    bool         initialized;
} bmi270_t;

/* ----------------------------------------------------------------------------
 * One compensated sample. SI units everywhere.
 * ---------------------------------------------------------------------------- */
typedef struct
{
    float ax_mps2, ay_mps2, az_mps2;   /* Linear accel, m/s^2  (~9.8 on Z at rest) */
    float gx_dps,  gy_dps,  gz_dps;    /* Angular rate, deg/s  (~0 at rest)        */
    float temp_c;                       /* Die temperature, deg C                  */
} bmi270_reading_t;

/* ----------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------- */

/**
 * Reset chip, upload 8 KB config blob, configure accel/gyro at 200 Hz,
 * power on accel + gyro + temp. ~80 ms total -- mostly the blob upload.
 *
 * @return CY_RSLT_SUCCESS or an error from the I2C / verification step
 */
cy_rslt_t bmi270_init(bmi270_t *dev, cyhal_i2c_t *i2c);

/**
 * Read latest accel + gyro + temperature and convert to SI units.
 * Does NOT wait for "data ready" -- with 200 Hz ODR you can poll at
 * any rate <=200 Hz and just get the last available sample.
 */
cy_rslt_t bmi270_read(bmi270_t *dev, bmi270_reading_t *reading);

#endif /* BMI270_H */