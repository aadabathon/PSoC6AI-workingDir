#ifndef I2C_BUS_H
#define I2C_BUS_H

/* ============================================================================
 * i2c_bus -- shared I2C-master singleton with a mutex
 *
 * Both the DPS368 and BMI270 hang off the same physical bus. Only one driver
 * can talk at a time. This module owns the cyhal handle + a FreeRTOS mutex,
 * and exposes lock/unlock so drivers can wrap their transactions.
 *
 * Init exactly once from main(), AFTER cybsp_init().
 * ============================================================================ */

#include "cyhal.h"
#include "cy_result.h"
#include <stdint.h>
#include <stdbool.h>

cy_rslt_t      i2c_bus_init(void);
cyhal_i2c_t   *i2c_bus_handle(void);                  /* NULL if not inited */
bool           i2c_bus_lock(uint32_t timeout_ms);     /* false on timeout    */
void           i2c_bus_unlock(void);

#endif