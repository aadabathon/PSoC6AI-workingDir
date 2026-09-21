/* ============================================================================
 * imu_task.c -- FreeRTOS task that owns the BMI270 IMU
 *
 * Same pattern as barometer_task: this task is the only thing that touches
 * the chip. The I2C bus itself is shared with the DPS368 and arbitrated by
 * the mutex inside i2c_bus.c -- this task doesn't have to know or care.
 *
 * Sample rate: 50 Hz (20 ms period). The chip's ODR is 200 Hz so we take
 * every 4th sample. 50 Hz is the DeepCraft sweet spot for activity
 * classification, and -- critically -- a full merged CSV row per sample
 * fits comfortably in 115200 baud, which 200 Hz would not.
 * All output goes through the logger (single owner of the UART).
 * ============================================================================ */

#include "imu_task.h"
#include "bmi270.h"
#include "i2c_bus.h"
#include "logger.h"
#include "log_sample.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdio.h>

#define SAMPLE_PERIOD_MS        (20U)       /* 50 Hz -- the CSV cadence master */
#define QUEUE_LENGTH            (8U)        /* a few samples of headroom */
#define TASK_STACK_WORDS        (1024U)
#define TASK_PRIORITY           (tskIDLE_PRIORITY + 2)

static bmi270_t      s_sensor;
static QueueHandle_t s_reading_queue = NULL;
static TaskHandle_t  s_task_handle   = NULL;

static void imu_task(void *arg)
{
    (void)arg;
    /* BMI270 sometimes NAKs on first probe after power-on; give it time + retries */
        cy_rslt_t rslt = CY_RSLT_TYPE_ERROR;
        for (int attempt = 0; attempt < 5; attempt++)
        {
            vTaskDelay(pdMS_TO_TICKS(20));     // settle time
            rslt = bmi270_init(&s_sensor, i2c_bus_handle());
            if (rslt == CY_RSLT_SUCCESS) break;
            printf("[imu] init attempt %d failed, retrying...\r\n", attempt + 1);
        }
        if (rslt != CY_RSLT_SUCCESS)
        {
            printf("[imu] bmi270_init failed after retries: 0x%08lx\r\n", (unsigned long)rslt);
            for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }

    TickType_t       next_wake = xTaskGetTickCount();
    const TickType_t period    = pdMS_TO_TICKS(SAMPLE_PERIOD_MS);

    for (;;)
    {
        bmi270_reading_t reading;
        rslt = bmi270_read(&s_sensor, &reading);

        if (rslt == CY_RSLT_SUCCESS)
        {
            /* Drop-on-full -- freshest sample wins, consumers don't get
             * a stale backlog. Same semantics as the barometer queue. */
            (void)xQueueSend(s_reading_queue, &reading, 0);

            log_sample_t s = {
                .ts_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                .src   = SRC_IMU,
                .d.imu = {
                    .ax = reading.ax_mps2, .ay = reading.ay_mps2, .az = reading.az_mps2,
                    .gx = reading.gx_dps,  .gy = reading.gy_dps,  .gz = reading.gz_dps,
                    .temp_c = reading.temp_c
                }
            };
            logger_post(&s);
        }
        else
        {
            printf("[imu] read err: 0x%08lx\r\n", (unsigned long)rslt);
        }

        vTaskDelayUntil(&next_wake, period);
    }
}

cy_rslt_t imu_task_init(void)
{
    if (i2c_bus_handle() == NULL) return CY_RSLT_TYPE_ERROR;

    s_reading_queue = xQueueCreate(QUEUE_LENGTH, sizeof(bmi270_reading_t));
    if (s_reading_queue == NULL) return CY_RSLT_TYPE_ERROR;

    BaseType_t ok = xTaskCreate(imu_task, "imu",
                                TASK_STACK_WORDS*2, NULL,
                                TASK_PRIORITY, &s_task_handle);
    return (ok == pdPASS) ? CY_RSLT_SUCCESS : CY_RSLT_TYPE_ERROR;
}

QueueHandle_t imu_task_get_queue(void)
{
    return s_reading_queue;
}