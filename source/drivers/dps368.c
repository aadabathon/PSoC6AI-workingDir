/* ============================================================================
 * dps368.c -- DPS368 driver implementation
 *
 * Flow when you call dps368_read:
 *   1. ADC sampled both channels into 24-bit signed raw registers.
 *   2. We read 6 bytes (3 pressure, 3 temp), sign-extend to int32_t.
 *   3. Apply the compensation polynomial (datasheet section 4.9.1) using
 *      the cal coefficients we loaded once at init.
 *
 * Things that bit me / will bite future-you:
 *   - Cal coefficients are packed across byte boundaries with weird widths
 *     (12-, 16-, 20-bit). Unpack in unpack_calibration().
 *   - The chip has TWO temperature sensors. The factory cal'd against one
 *     of them; COEF_SRCE bit 7 tells you which. You MUST put that same bit
 *     in TMP_CFG or your temperatures (and therefore pressures) are wrong.
 *   - At oversampling > 8x, you MUST set the SHIFT bits in CFG_REG or the
 *     raw results overflow silently. We use 16x, so this matters.
 * ============================================================================
 */

#include "dps368.h"
#include "i2c_bus.h"
#include "cy_result.h"
#include "cyhal.h"
#include <string.h>

#define I2C_TIMEOUT_MS          (10U)
#define DPS368_INIT_RETRY_MAX   (100U)   /* ~100ms @ 1ms/tick */

/* ----------------------------------------------------------------------------
 * Tiny I2C wrappers around the cyhal mem_read/write calls. The "1" is the
 * register-address size in bytes (DPS368 uses 8-bit reg addrs).
 * ---------------------------------------------------------------------------- */
static cy_rslt_t reg_read(dps368_t *dev, uint8_t reg, uint8_t *buf, size_t n)
{
    if (!i2c_bus_lock(50)) return CY_RSLT_TYPE_ERROR;
    cy_rslt_t r = cyhal_i2c_master_mem_read(dev->i2c, DPS368_I2C_ADDR,
                                            reg, 1, buf, (uint16_t)n,
                                            I2C_TIMEOUT_MS);
    i2c_bus_unlock();
    return r;
}

static cy_rslt_t reg_write(dps368_t *dev, uint8_t reg, uint8_t val)
{
    if (!i2c_bus_lock(50)) return CY_RSLT_TYPE_ERROR;
    cy_rslt_t r = cyhal_i2c_master_mem_write(dev->i2c, DPS368_I2C_ADDR,
                                             reg, 1, &val, 1,
                                             I2C_TIMEOUT_MS);
    i2c_bus_unlock();
    return r;
}

/* ----------------------------------------------------------------------------
 * Sign-extend an N-bit unsigned into a 32-bit signed int.
 *
 * The chip's raw values are 12, 16, 20, or 24 bits, two's-complement. We
 * read them as unsigned then do the classic "if top bit set, subtract 2^N".
 * ---------------------------------------------------------------------------- */
static int32_t sign_extend(uint32_t val, uint8_t bits)
{
    int32_t sval = (int32_t)val;
    if (sval & (1L << (bits - 1)))
    {
        sval -= (1L << bits);
    }
    return sval;
}

/* ----------------------------------------------------------------------------
 * Unpack the 18-byte coefficient blob (datasheet table 18).
 *
 * Layout reminder (coef[i] is byte i, starting at register 0x10):
 *   c0  [11:4]   = coef[0]
 *   c0  [3:0]    = coef[1] >> 4
 *   c1  [11:8]   = coef[1] & 0x0F
 *   c1  [7:0]    = coef[2]
 *   c00 [19:12]  = coef[3]
 *   c00 [11:4]   = coef[4]
 *   c00 [3:0]    = coef[5] >> 4
 *   c10 [19:16]  = coef[5] & 0x0F
 *   c10 [15:8]   = coef[6]
 *   c10 [7:0]    = coef[7]
 *   c01..c30 are 16-bit big-endian in coef[8..17]
 * ---------------------------------------------------------------------------- */
static void unpack_calibration(const uint8_t coef[18], dps368_calib_t *out)
{
    uint32_t raw;

    /* c0: 12-bit signed */
    raw = ((uint32_t)coef[0] << 4) | ((uint32_t)coef[1] >> 4);
    out->c0 = sign_extend(raw, 12);

    /* c1: 12-bit signed */
    raw = (((uint32_t)coef[1] & 0x0F) << 8) | (uint32_t)coef[2];
    out->c1 = sign_extend(raw, 12);

    /* c00: 20-bit signed */
    raw = ((uint32_t)coef[3] << 12)
        | ((uint32_t)coef[4] << 4)
        | ((uint32_t)coef[5] >> 4);
    out->c00 = sign_extend(raw, 20);

    /* c10: 20-bit signed */
    raw = (((uint32_t)coef[5] & 0x0F) << 16)
        | ((uint32_t)coef[6] << 8)
        |  (uint32_t)coef[7];
    out->c10 = sign_extend(raw, 20);

    /* c01..c30: 16-bit signed, big-endian */
    out->c01 = sign_extend(((uint32_t)coef[8]  << 8) | coef[9],  16);
    out->c11 = sign_extend(((uint32_t)coef[10] << 8) | coef[11], 16);
    out->c20 = sign_extend(((uint32_t)coef[12] << 8) | coef[13], 16);
    out->c21 = sign_extend(((uint32_t)coef[14] << 8) | coef[15], 16);
    out->c30 = sign_extend(((uint32_t)coef[16] << 8) | coef[17], 16);
}

/* ----------------------------------------------------------------------------
 * Poll MEAS_CFG until (status & mask) == mask, or we time out.
 * Used for SENSOR_RDY / COEF_RDY at init, and PRS_RDY / TMP_RDY at read.
 * ---------------------------------------------------------------------------- */
static cy_rslt_t wait_for_status(dps368_t *dev, uint8_t mask, uint32_t retries)
{
    uint8_t status;
    while (retries--)
    {
        cy_rslt_t r = reg_read(dev, DPS368_REG_MEAS_CFG, &status, 1);
        if (r != CY_RSLT_SUCCESS) return r;
        if ((status & mask) == mask) return CY_RSLT_SUCCESS;
        cyhal_system_delay_ms(1);
    }
    return CY_RSLT_TYPE_ERROR;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

cy_rslt_t dps368_init(dps368_t *dev, cyhal_i2c_t *i2c)
{
    cy_rslt_t rslt;
    uint8_t   buf[18];

    /* Wipe so any partial-init failure leaves a clean (initialized=false) state */
    memset(dev, 0, sizeof(*dev));
    dev->i2c = i2c;

    /* --- Soft reset, let it settle ------------------------------------- */
    rslt = reg_write(dev, DPS368_REG_RESET, DPS368_SOFT_RESET_VALUE);
    if (rslt != CY_RSLT_SUCCESS) return rslt;
    cyhal_system_delay_ms(40);     /* t_startup <= 40ms in datasheet */

    /* --- WHO_AM_I check ------------------------------------------------- */
    rslt = reg_read(dev, DPS368_REG_PRODUCT_ID, buf, 1);
    if (rslt != CY_RSLT_SUCCESS) return rslt;
    if (buf[0] != DPS368_PRODUCT_ID_VALUE) return CY_RSLT_TYPE_ERROR;

    /* --- Wait for self-init: SENSOR_RDY *and* COEF_RDY ------------------ */
    rslt = wait_for_status(dev,
                           DPS368_MEAS_SENSOR_RDY | DPS368_MEAS_COEF_RDY,
                           DPS368_INIT_RETRY_MAX);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* --- Read + unpack 18 bytes of cal --------------------------------- */
    rslt = reg_read(dev, DPS368_REG_COEF_BASE, buf, 18);
    if (rslt != CY_RSLT_SUCCESS) return rslt;
    unpack_calibration(buf, &dev->calib);

    /* --- Find out which on-chip temp sensor was cal'd ------------------ */
    rslt = reg_read(dev, DPS368_REG_COEF_SRCE, buf, 1);
    if (rslt != CY_RSLT_SUCCESS) return rslt;
    uint8_t tmp_src_bit = buf[0] & 0x80;   /* bit 7 only */

    /* --- Configure measurement ----------------------------------------
     *   PRS_CFG = 0x34 -> 8 samples/s, 16x oversampling
     *   TMP_CFG = 0x34 | tmp_src_bit (preserve sensor choice)
     */
    rslt = reg_write(dev, DPS368_REG_PRS_CFG, 0x34);
    if (rslt != CY_RSLT_SUCCESS) return rslt;
    rslt = reg_write(dev, DPS368_REG_TMP_CFG, 0x34 | tmp_src_bit);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* --- Required at OSR > 8x: enable result-shift bits.
     *     CFG_REG bit 3 = T_SHIFT, bit 2 = P_SHIFT.
     */
    rslt = reg_write(dev, DPS368_REG_CFG_REG, 0x0C);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* --- Continuous "measure both" mode -------------------------------- */
    rslt = reg_write(dev, DPS368_REG_MEAS_CFG, DPS368_MODE_CONT_BOTH);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    dev->initialized = true;
    return CY_RSLT_SUCCESS;
}

/* ----------------------------------------------------------------------------
 * Apply the datasheet compensation polynomial.
 *
 *   T_sc = T_raw / kT
 *   T_c  = c0 * 0.5 + c1 * T_sc
 *
 *   P_sc = P_raw / kP
 *   P_pa = c00
 *        + P_sc * (c10 + P_sc * (c20 + P_sc * c30))    (pressure-only cubic)
 *        + T_sc * c01                                  (temp-only cross)
 *        + T_sc * P_sc * (c11 + P_sc * c21)            (mixed cross)
 *
 * Same expression as datasheet eq. 2, just Horner-factored for clarity
 * and one fewer multiply.
 * ---------------------------------------------------------------------------- */
static void compensate(const dps368_calib_t *c,
                       int32_t p_raw, int32_t t_raw,
                       float *p_pa, float *t_c)
{
    const float kP = DPS368_SCALE_FACTOR_16X;
    const float kT = DPS368_SCALE_FACTOR_16X;

    float t_sc = (float)t_raw / kT;
    float p_sc = (float)p_raw / kP;

    *t_c  = (float)c->c0 * 0.5f + (float)c->c1 * t_sc;

    *p_pa = (float)c->c00
          + p_sc * ((float)c->c10 + p_sc * ((float)c->c20 + p_sc * (float)c->c30))
          + t_sc * (float)c->c01
          + t_sc * p_sc * ((float)c->c11 + p_sc * (float)c->c21);
}

cy_rslt_t dps368_read(dps368_t *dev, dps368_reading_t *reading)
{
    if (!dev->initialized) return CY_RSLT_TYPE_ERROR;

    cy_rslt_t rslt;
    uint8_t   buf[6];

    /* Wait for fresh samples in both channels. <10ms at 16x / 8Hz. */
    rslt = wait_for_status(dev,
                           DPS368_MEAS_PRS_RDY | DPS368_MEAS_TMP_RDY,
                           100);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* Burst-read PSR_B2..B0, TMP_B2..B0 (6 contiguous regs). */
    rslt = reg_read(dev, DPS368_REG_PSR_B2, buf, 6);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* Pack big-endian 3-byte runs into 24-bit unsigned, then sign-extend. */
    uint32_t p_raw_u = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
    uint32_t t_raw_u = ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | buf[5];
    int32_t  p_raw   = sign_extend(p_raw_u, 24);
    int32_t  t_raw   = sign_extend(t_raw_u, 24);

    compensate(&dev->calib, p_raw, t_raw,
               &reading->pressure_pa, &reading->temperature_c);

    dev->last_temp_c = reading->temperature_c;
    return CY_RSLT_SUCCESS;
}