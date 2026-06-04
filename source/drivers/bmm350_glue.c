/* ============================================================================
 * bmm350_glue.c
 * ============================================================================ */

#include "bmm350_glue.h"
#include "i2c_bus.h"
#include "cyhal.h"
#include "cy_pdl.h"          /* for Cy_SysLib_DelayUs */
#include "FreeRTOS.h"
#include "task.h"
#include "bmm350_defs.h"
#include <string.h>

#define I2C_TIMEOUT_MS          (10U)
#define BUS_LOCK_TIMEOUT_MS     (50U)

/* The Bosch API passes our user "intf_ptr" into every callback. We use it
 * to carry the I2C 7-bit address -- so this same glue could drive a second
 * BMM350 on a different address with no code changes. */
static uint8_t s_dev_addr = BMM350_I2C_ADSEL_SET_HIGH;   /* 0x15 on this board */

/* ----------------------------------------------------------------------------
 * I2C read callback. Bosch wants:
 *   - return 0 on success, negative on failure
 *   - read N bytes starting at reg_addr into reg_data
 * Same single-byte-addr / N-byte-payload contract as the other drivers.
 * ---------------------------------------------------------------------------- */
static BMM350_INTF_RET_TYPE bmm350_i2c_read_cb(uint8_t reg_addr,
                                               uint8_t *reg_data,
                                               uint32_t len,
                                               void *intf_ptr)
{
    uint8_t addr = *(uint8_t *)intf_ptr;
    if (!i2c_bus_lock(BUS_LOCK_TIMEOUT_MS)) return BMM350_E_COM_FAIL;
    cy_rslt_t r = cyhal_i2c_master_mem_read(i2c_bus_handle(), addr,
                                            reg_addr, 1,
                                            reg_data, (uint16_t)len,
                                            I2C_TIMEOUT_MS);
    i2c_bus_unlock();
    return (r == CY_RSLT_SUCCESS) ? BMM350_INTF_RET_SUCCESS : BMM350_E_COM_FAIL;
}

static BMM350_INTF_RET_TYPE bmm350_i2c_write_cb(uint8_t reg_addr,
                                                const uint8_t *reg_data,
                                                uint32_t len,
                                                void *intf_ptr)
{
    uint8_t addr = *(uint8_t *)intf_ptr;
    if (!i2c_bus_lock(BUS_LOCK_TIMEOUT_MS)) return BMM350_E_COM_FAIL;
    /* cyhal write is not const-correct; same ugly cast as in bmi270.c. */
    cy_rslt_t r = cyhal_i2c_master_mem_write(i2c_bus_handle(), addr,
                                             reg_addr, 1,
                                             (uint8_t *)reg_data, (uint16_t)len,
                                             I2C_TIMEOUT_MS);
    i2c_bus_unlock();
    return (r == CY_RSLT_SUCCESS) ? BMM350_INTF_RET_SUCCESS : BMM350_E_COM_FAIL;
}

/* ----------------------------------------------------------------------------
 * Delay callback. Bosch asks in microseconds; we have two clocks:
 *   - >= 1 ms : vTaskDelay so we yield the CPU (other tasks can run)
 *   - <  1 ms : Cy_SysLib_DelayUs busy-wait (vTaskDelay is 1-ms granular)
 *
 * Mixing these two is standard -- short sensor delays (e.g. 50 us between
 * register writes) can't yield because the scheduler tick is way coarser.
 * ---------------------------------------------------------------------------- */
static void bmm350_delay_us_cb(uint32_t period, void *intf_ptr)
{
    (void)intf_ptr;
    if (period >= 1000U)
    {
        /* Round up to whole ms so we never under-delay. */
        vTaskDelay(pdMS_TO_TICKS((period + 999U) / 1000U));
    }
    else
    {
        Cy_SysLib_DelayUs((uint16_t)period);
    }
}

cy_rslt_t bmm350_glue_attach(struct bmm350_dev *dev)
{
    memset(dev, 0, sizeof(*dev));
    dev->intf_ptr = &s_dev_addr;
    dev->read     = bmm350_i2c_read_cb;
    dev->write    = bmm350_i2c_write_cb;
    dev->delay_us = bmm350_delay_us_cb;
    return CY_RSLT_SUCCESS;
}