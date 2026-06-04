/* ============================================================================
 * bmi270.c -- Bosch BMI270 driver implementation
 *
 * Init sequence (datasheet section 4.1, "Initialization sequence"):
 *   1. Soft reset -> 450 us
 *   2. Disable advanced power save (PWR_CONF = 0x00)
 *   3. Wait 450 us
 *   4. Disable config loading (INIT_CTRL = 0x00)
 *   5. Burst-write 8 KB blob to INIT_DATA (0x5E)
 *   6. Enable config loading (INIT_CTRL = 0x01)
 *   7. Poll INTERNAL_STATUS for 0x01 ("init OK"), up to ~20 ms
 *   8. Configure accel + gyro ODR/range
 *   9. Power on accel + gyro + temp via PWR_CTRL
 *   10. Wait ~10 ms for sensors to stabilize
 *
 * Step 5 is the funky one -- the chip wants the blob in chunks, with
 * the destination "address" written to INIT_ADDR_0/1 before each chunk.
 * The address is in units of "half-words" (16-bit) and split across two
 * registers (low nibble in 0x5B, rest in 0x5C). Easy to get wrong.
 *
 * I2C bus is shared with DPS368, so every transaction takes the bus mutex
 * via i2c_bus_lock/unlock.
 * ============================================================================ */

#include "bmi270.h"
#include "i2c_bus.h"
#include "cyhal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>  

#define I2C_TIMEOUT_MS               (10U)
#define BUS_LOCK_TIMEOUT_MS          (50U)

/* Chunk size for blob upload. Bosch's docs say "burst write" but the
 * I2C peripheral on PSoC has a finite buffer; 32 bytes per chunk is
 * safe and fast enough. 8192 / 32 = 256 chunks. */
#define BLOB_CHUNK_SIZE              (32U)

/* ----------------------------------------------------------------------------
 * I2C wrappers -- same pattern as DPS368, but locking the shared bus.
 * ---------------------------------------------------------------------------- */
static cy_rslt_t reg_read(bmi270_t *dev, uint8_t reg, uint8_t *buf, size_t n)
{
    if (!i2c_bus_lock(BUS_LOCK_TIMEOUT_MS)) return CY_RSLT_TYPE_ERROR;
    cy_rslt_t r = cyhal_i2c_master_mem_read(dev->i2c, BMI270_I2C_ADDR,
                                            reg, 1, buf, (uint16_t)n,
                                            I2C_TIMEOUT_MS);
    i2c_bus_unlock();
    return r;
}

static cy_rslt_t reg_write(bmi270_t *dev, uint8_t reg, uint8_t val)
{
    if (!i2c_bus_lock(BUS_LOCK_TIMEOUT_MS)) return CY_RSLT_TYPE_ERROR;
    cy_rslt_t r = cyhal_i2c_master_mem_write(dev->i2c, BMI270_I2C_ADDR,
                                             reg, 1, &val, 1,
                                             I2C_TIMEOUT_MS);
    i2c_bus_unlock();
    return r;
}

/* Burst-write N bytes to a register. For the config blob upload only. */
static cy_rslt_t reg_write_burst(bmi270_t *dev, uint8_t reg,
                                 const uint8_t *buf, size_t n)
{
    if (!i2c_bus_lock(BUS_LOCK_TIMEOUT_MS)) return CY_RSLT_TYPE_ERROR;
    cy_rslt_t r = cyhal_i2c_master_mem_write(dev->i2c, BMI270_I2C_ADDR,
                                             reg, 1, (uint8_t *)buf,
                                             (uint16_t)n, I2C_TIMEOUT_MS);
    i2c_bus_unlock();
    return r;
}

/* ----------------------------------------------------------------------------
 * Two's-complement sign-extend a 16-bit raw read into int16_t. The chip
 * gives data as two bytes LSB-first.
 * ---------------------------------------------------------------------------- */
static inline int16_t le16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* ============================================================================
 * The config blob upload -- step 5 of the init dance.
 *
 * For each 32-byte chunk:
 *   1. Compute the half-word address: byte_offset / 2.
 *   2. Write INIT_ADDR_0 = address & 0x0F  (low nibble)
 *      Write INIT_ADDR_1 = address >> 4    (the rest)
 *   3. Burst-write the 32 bytes to INIT_DATA (auto-increments inside).
 *
 * If you get -2 / -9 errors from Bosch's reference driver, this loop is
 * almost always the spot to look.
 * ============================================================================ */
static cy_rslt_t upload_config_blob(bmi270_t *dev)
{
    cy_rslt_t rslt;
    for (uint16_t offset = 0; offset < BMI270_CONFIG_FILE_SIZE; offset += BLOB_CHUNK_SIZE)
    {
        uint16_t hw_addr = offset / 2;   /* half-words, not bytes */

        rslt = reg_write(dev, BMI270_REG_INIT_ADDR_0, hw_addr & 0x0F);
        if (rslt != CY_RSLT_SUCCESS) return rslt;
        rslt = reg_write(dev, BMI270_REG_INIT_ADDR_1, (hw_addr >> 4) & 0xFF);
        if (rslt != CY_RSLT_SUCCESS) return rslt;

        rslt = reg_write_burst(dev, BMI270_REG_INIT_DATA,
                               &bmi270_config_file[offset], BLOB_CHUNK_SIZE);
        if (rslt != CY_RSLT_SUCCESS) return rslt;
    }
    return CY_RSLT_SUCCESS;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

cy_rslt_t bmi270_init(bmi270_t *dev, cyhal_i2c_t *i2c)
{
    cy_rslt_t rslt;
    uint8_t   buf;

    memset(dev, 0, sizeof(*dev));
    dev->i2c = i2c;

    /* --- Probe: read CHIP_ID. Cheap pre-flight check. ----------------- */
    rslt = reg_read(dev, BMI270_REG_CHIP_ID, &buf, 1);
    if (rslt != CY_RSLT_SUCCESS) return rslt;
    if (buf != BMI270_CHIP_ID_VALUE) return CY_RSLT_TYPE_ERROR;

    printf("[imu] chip_id read ok = 0x%02x\r\n", buf);

    /* --- 1. Soft reset ------------------------------------------------- */
    rslt = reg_write(dev, BMI270_REG_CMD, BMI270_CMD_SOFT_RESET);
    if (rslt != CY_RSLT_SUCCESS) return rslt;
    vTaskDelay(pdMS_TO_TICKS(10));   /* datasheet: 450 us; 10 ms is overkill but safe */

    printf("[imu] soft reset ok\r\n");

    /* --- 2. Disable advanced power save ------------------------------- */
    rslt = reg_write(dev, BMI270_REG_PWR_CONF, 0x00);
    if (rslt != CY_RSLT_SUCCESS) return rslt;
    vTaskDelay(pdMS_TO_TICKS(2));

    printf("[imu] pwr_conf ok\r\n");

    /* --- 4. Prepare for config load ----------------------------------- */
    rslt = reg_write(dev, BMI270_REG_INIT_CTRL, 0x00);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    printf("[imu] init_ctrl=0 ok\r\n");

    /* --- 5. Upload the 8 KB blob -------------------------------------- */
    rslt = upload_config_blob(dev);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    printf("[imu] blob uploaded\r\n");

    /* --- 6. Signal "config done" -------------------------------------- */
    rslt = reg_write(dev, BMI270_REG_INIT_CTRL, 0x01);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    printf("[imu] init_ctrl=1 ok\r\n");

    /* --- 7. Poll INTERNAL_STATUS for init-OK. Datasheet says ~20 ms. - */
    for (int i = 0; i < 30; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
        rslt = reg_read(dev, BMI270_REG_INTERNAL_STATUS, &buf, 1);
        if (rslt != CY_RSLT_SUCCESS) return rslt;
        if ((buf & 0x0F) == BMI270_INIT_STATUS_OK) goto init_ok;
    }
    return CY_RSLT_TYPE_ERROR;     /* timed out waiting for init */

init_ok:
    /* --- 8. Configure ranges and ODR ----------------------------------
     * ACC_CONF  = 0xA9 -> accel ODR=200Hz, normal filter, performance mode
     * ACC_RANGE = 0x01 -> +/- 4g
     * GYR_CONF  = 0xA9 -> gyro  ODR=200Hz, normal filter, performance mode
     * GYR_RANGE = 0x00 -> +/- 2000 dps
     *
     * If you change ACC_RANGE / GYR_RANGE here, also change the
     * BMI270_*_LSB_TO_* macros in bmi270.h or your readings lie.
     */
     printf("[imu] internal_status=0x%02x\r\n", buf);

    rslt = reg_write(dev, BMI270_REG_ACC_CONF,  0xA9); if (rslt != CY_RSLT_SUCCESS) return rslt;
    rslt = reg_write(dev, BMI270_REG_ACC_RANGE, 0x01); if (rslt != CY_RSLT_SUCCESS) return rslt;
    rslt = reg_write(dev, BMI270_REG_GYR_CONF,  0xA9); if (rslt != CY_RSLT_SUCCESS) return rslt;
    rslt = reg_write(dev, BMI270_REG_GYR_RANGE, 0x00); if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* --- 9. Power on accel + gyro + temp ------------------------------ */
    rslt = reg_write(dev, BMI270_REG_PWR_CTRL,
                     BMI270_PWR_CTRL_ACC_EN |
                     BMI270_PWR_CTRL_GYR_EN |
                     BMI270_PWR_CTRL_TEMP_EN);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* --- 10. Stabilization -------------------------------------------- */
    vTaskDelay(pdMS_TO_TICKS(10));

    dev->initialized = true;
    return CY_RSLT_SUCCESS;
}

cy_rslt_t bmi270_read(bmi270_t *dev, bmi270_reading_t *reading)
{
    if (!dev->initialized) return CY_RSLT_TYPE_ERROR;

    cy_rslt_t rslt;
    uint8_t   raw[12];      /* ACC_X/Y/Z (6) + GYR_X/Y/Z (6), LSB-first */
    uint8_t   t_raw[2];

    /* Burst-read accel + gyro in one shot. The 12 bytes at DATA_8..DATA_19
     * are contiguous LE pairs (X_lsb, X_msb, Y_lsb, Y_msb, ...). */
    rslt = reg_read(dev, BMI270_REG_DATA_8, raw, 12);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    int16_t ax = le16(&raw[0]);
    int16_t ay = le16(&raw[2]);
    int16_t az = le16(&raw[4]);
    int16_t gx = le16(&raw[6]);
    int16_t gy = le16(&raw[8]);
    int16_t gz = le16(&raw[10]);

    reading->ax_mps2 = (float)ax * BMI270_ACC_LSB_TO_MPS2;
    reading->ay_mps2 = (float)ay * BMI270_ACC_LSB_TO_MPS2;
    reading->az_mps2 = (float)az * BMI270_ACC_LSB_TO_MPS2;
    reading->gx_dps  = (float)gx * BMI270_GYR_LSB_TO_DPS;
    reading->gy_dps  = (float)gy * BMI270_GYR_LSB_TO_DPS;
    reading->gz_dps  = (float)gz * BMI270_GYR_LSB_TO_DPS;

    /* Temperature: 16-bit signed, datasheet eq:
     *     T_degC = (raw / 512) + 23.0
     * (LSB = 1/512 degC, offset = 23.0 degC). */
    rslt = reg_read(dev, BMI270_REG_TEMPERATURE_0, t_raw, 2);
    if (rslt != CY_RSLT_SUCCESS) return rslt;
    int16_t t = le16(t_raw);
    reading->temp_c = (float)t / 512.0f + 23.0f;

    return CY_RSLT_SUCCESS;
}