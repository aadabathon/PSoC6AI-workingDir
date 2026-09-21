/* ============================================================================
 * ml_task.c -- windows raw IMU readings and runs on-device activity
 * inference, posting the result into the same logger pipeline every other
 * sensor uses.
 *
 * Taps imu_task's reading queue (imu_task_get_queue()) directly rather than
 * going through the logger -- this task needs raw per-sample readings to
 * build inference windows, not the merged CSV row logger.c produces. The
 * actual model lives behind activity_model.h/.c (source/ml/) -- until that's
 * wired up to a real DEEPCRAFT export, activity_model_init() returns false
 * and this task just idles without posting anything, so the rest of the
 * firmware is unaffected either way.
 * ============================================================================ */

#include "ml_task.h"
#include "imu_task.h"
#include "activity_model.h"
#include "logger.h"
#include "log_sample.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdio.h>
#include <stddef.h>

#define TASK_STACK_WORDS   (1024U * 2U)
#define TASK_PRIORITY      (tskIDLE_PRIORITY + 1)

static void ml_task(void *arg)
{
    (void)arg;

    QueueHandle_t imu_queue = imu_task_get_queue();
    if (imu_queue == NULL)
    {
        printf("[ml] imu queue not available -- task exiting\r\n");
        vTaskDelete(NULL);
        return;
    }

    if (!activity_model_init())
    {
        printf("[ml] no model wired in yet (see source/ml/activity_model.c) "
               "-- task idling, IMU pipeline unaffected\r\n");
    }

    static bmi270_reading_t window[ACTIVITY_MODEL_WINDOW_LEN];
    size_t n = 0;

    for (;;)
    {
        bmi270_reading_t reading;
        if (xQueueReceive(imu_queue, &reading, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        window[n++] = reading;
        if (n < ACTIVITY_MODEL_WINDOW_LEN)
        {
            continue;
        }
        n = 0;  /* non-overlapping windows; switch to a sliding copy for overlap */

        activity_model_result_t result;
        if (activity_model_run(window, &result))
        {
            log_sample_t s = {
                .ts_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                .src   = SRC_ML,
                .d.ml  = { .class_id = result.class_id, .confidence = result.confidence },
            };
            logger_post(&s);
        }
    }
}

cy_rslt_t ml_task_init(void)
{
    BaseType_t ok = xTaskCreate(ml_task, "ml", TASK_STACK_WORDS, NULL,
                                TASK_PRIORITY, NULL);
    return (ok == pdPASS) ? CY_RSLT_SUCCESS : CY_RSLT_TYPE_ERROR;
}
