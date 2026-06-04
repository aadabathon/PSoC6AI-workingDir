#include "i2c_bus.h"
#include "cybsp.h"
#include "FreeRTOS.h"
#include "semphr.h"

#define I2C_FREQ_HZ  (400000U)   /* 400 kHz -- DPS368 max, BMI270 fast mode */

static cyhal_i2c_t       s_i2c;
static SemaphoreHandle_t s_mutex   = NULL;
static bool              s_inited  = false;

cy_rslt_t i2c_bus_init(void)
{
    if (s_inited) return CY_RSLT_SUCCESS;        /* idempotent: safe to call twice */

    cyhal_i2c_cfg_t cfg = {
        .is_slave        = false,
        .address         = 0,
        .frequencyhal_hz = I2C_FREQ_HZ,
    };

    cy_rslt_t rslt = cyhal_i2c_init(&s_i2c, CYBSP_I2C_SDA, CYBSP_I2C_SCL, NULL);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    rslt = cyhal_i2c_configure(&s_i2c, &cfg);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* Mutex (not a semaphore!) -- a mutex tracks the holder for priority
     * inheritance, which prevents a low-prio task holding the bus from
     * blocking a high-prio task forever. Real-world FreeRTOS hygiene. */
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) return CY_RSLT_TYPE_ERROR;

    s_inited = true;
    return CY_RSLT_SUCCESS;
}

cyhal_i2c_t *i2c_bus_handle(void)
{
    return s_inited ? &s_i2c : NULL;
}

bool i2c_bus_lock(uint32_t timeout_ms)
{
    if (!s_inited) return false;
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void i2c_bus_unlock(void)
{
    if (s_inited) (void)xSemaphoreGive(s_mutex);
}