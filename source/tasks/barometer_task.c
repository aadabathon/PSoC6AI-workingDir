/* ============================================================================
 * barometer_task.c -- FreeRTOS task that owns the DPS368 sensor
 *
 * Pattern: ONE task owns the chip. Nobody else touches it directly. Other
 * tasks that want pressure/temp data subscribe via the queue.
 *
 * Why this pattern: the DPS368 has internal state (mode, current sample) and
 * the I2C bus has shared state. If two tasks both poked at the sensor you'd
 * need a mutex around every transaction. With a single owner, the only
 * shared resource is the queue, which is already thread-safe.
 *
 * Flow:
 *   1. Init UART + I2C (done in main, handed in via init fn).
 *   2. Task calls dps368_init() ONCE.
 *   3. Loop forever: read sensor, printf it, send to queue, sleep.
 * ============================================================================
 */
#include "dps368.h"
#include "i2c_bus.h"
#include "barometer_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdio.h>

/* ----------------------------------------------------------------------------
 * Tunables -- bump SAMPLE_PERIOD_MS down if you want faster updates. Note
 * 16x OSR + 8 Hz sample rate (set in dps368_init) means the chip itself
 * caps you around 125ms. Asking for faster than that just gets you the
 * same sample twice.
 * ---------------------------------------------------------------------------- */
#define SAMPLE_PERIOD_MS        (2000U)      /* 0.5 Hz -- good for slow weather changes */
#define QUEUE_LENGTH            (4U)
#define TASK_STACK_WORDS        (1024U)
#define TASK_PRIORITY           (tskIDLE_PRIORITY + 2)
/* ----------------------------------------------------------------------------
 * Module-private state. `static` = invisible outside this .c file. The queue
 * handle is exposed via barometer_task_get_queue() so other tasks can
 * subscribe without us making the variable public.
 * ---------------------------------------------------------------------------- */
static dps368_t         s_sensor;
static QueueHandle_t    s_reading_queue = NULL;
static TaskHandle_t     s_task_handle   = NULL;

/* ----------------------------------------------------------------------------
 * The task body. Standard FreeRTOS task signature -- void fn taking a void*.
 * ---------------------------------------------------------------------------- */
static void barometer_task(void *arg)
{
    (void)arg;
    cy_rslt_t rslt;

    /* Init the sensor. If this fails we have nothing useful to do, so park
     * the task forever in an error state rather than spinning on a dead bus. */
    rslt = dps368_init(&s_sensor, i2c_bus_handle());
    if (rslt != CY_RSLT_SUCCESS)
    {
        printf("[baro] dps368_init failed: 0x%08lx\r\n", (unsigned long)rslt);
        for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    printf("[baro] DPS368 initialized. c0=%ld c1=%ld c00=%ld c10=%ld\r\n",
           (long)s_sensor.calib.c0,  (long)s_sensor.calib.c1,
           (long)s_sensor.calib.c00, (long)s_sensor.calib.c10);

    /* xTaskGetTickCount + vTaskDelayUntil gives you a *periodic* wake instead
     * of "sleep 250ms after each iteration finishes". The difference is the
     * sample period stays constant even if a read takes 5ms or 50ms. Habit
     * worth building -- it makes timing reasoning much simpler. */
    TickType_t next_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(SAMPLE_PERIOD_MS);

    for (;;)
    {
        dps368_reading_t reading;
        rslt = dps368_read(&s_sensor, &reading);

        if (rslt == CY_RSLT_SUCCESS)
        {
            /* Print to debug terminal. \r\n because picocom/PuTTY want both. */
            printf("[baro] P=%.2f Pa  T=%.2f C\r\n",
                   reading.pressure_pa, reading.temperature_c);

            /* Push onto queue. If full, *overwrite* the oldest -- consumers
             * always want the freshest reading, not a stale backlog. We
             * achieve that with a 0-tick send: full queue = drop. If you
             * wanted strict no-loss semantics you'd use a larger queue
             * and portMAX_DELAY instead. */
            (void)xQueueSend(s_reading_queue, &reading, 0);
        }
        else
        {
            printf("[baro] read err: 0x%08lx\r\n", (unsigned long)rslt);
        }

        vTaskDelayUntil(&next_wake, period);
    }
}

/* ============================================================================
 * Public API
 * ============================================================================ */

cy_rslt_t barometer_task_init(void)
{
    /* The bus is now someone else's problem (i2c_bus_init in main). */
    if (i2c_bus_handle() == NULL) return CY_RSLT_TYPE_ERROR;

    s_reading_queue = xQueueCreate(QUEUE_LENGTH, sizeof(dps368_reading_t));
    if (s_reading_queue == NULL) return CY_RSLT_TYPE_ERROR;

    BaseType_t ok = xTaskCreate(barometer_task, "baro",
                                TASK_STACK_WORDS, NULL,
                                TASK_PRIORITY, &s_task_handle);
    return (ok == pdPASS) ? CY_RSLT_SUCCESS : CY_RSLT_TYPE_ERROR;
}

QueueHandle_t barometer_task_get_queue(void)
{
    return s_reading_queue;
}