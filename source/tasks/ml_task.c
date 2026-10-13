/* Feed timestamped IMU samples to the generated streaming preprocessor. */
#include "ml_task.h"
#include "imu_task.h"
#include "activity_model.h"
#include "fall_alert_task.h"
#include "logger.h"
#include "log_sample.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>

#define TASK_STACK_WORDS (1024U * 2U)
#define TASK_PRIORITY (tskIDLE_PRIORITY + 1)

static void ml_task(void *arg)
{
    (void)arg;
    QueueHandle_t queue = imu_task_get_queue();
    if (queue == NULL) { vTaskDelete(NULL); return; }
    if (!activity_model_init())
        printf("[ml] model unavailable; collection continues (see source/ml/generated)\r\n");
    for (;;)
    {
        imu_sample_t sample;
        if (xQueueReceive(queue, &sample, portMAX_DELAY) != pdTRUE) continue;
        activity_model_result_t result;
        if (activity_model_push(&sample.reading, sample.ts_ms, &result))
        {
            fall_alert_observe(&result, sample.ts_ms);
            log_sample_t s = {
                .ts_ms = sample.ts_ms,
                .src = SRC_ML,
                .d.ml = { .class_id = result.class_id, .confidence = result.confidence },
            };
            logger_post(&s);
        }
    }
}

cy_rslt_t ml_task_init(void)
{
    return xTaskCreate(ml_task, "ml", TASK_STACK_WORDS, NULL, TASK_PRIORITY, NULL)
           == pdPASS ? CY_RSLT_SUCCESS : CY_RSLT_TYPE_ERROR;
}
