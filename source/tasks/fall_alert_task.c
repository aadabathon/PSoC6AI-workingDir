#include "fall_alert_task.h"
#include "fall_alert_policy.h"
#include "logger.h"
#include "resource_map.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cyhal.h"
#include <string.h>

static fall_alert_policy_t s_policy;
static bool s_ready;

static uint32_t now_ms(void) { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

static void post_event(uint8_t action, float confidence, uint32_t source_ts_ms)
{
    log_sample_t event = { .ts_ms = now_ms(), .src = SRC_FALL_ALERT,
                          .d.fall = { .action = action, .confidence = confidence, .source_ts_ms = source_ts_ms } };
    logger_post(&event);
}

bool fall_alert_is_active(void)
{
    taskENTER_CRITICAL();
    bool active = s_policy.active;
    taskEXIT_CRITICAL();
    return active;
}

void fall_alert_observe(const activity_model_result_t *result, uint32_t sample_ts_ms)
{
    if (!s_ready) return;
    bool is_fall = strcmp(activity_model_class_name(result->class_id), "falling") == 0;
    taskENTER_CRITICAL();
    bool changed = fall_alert_policy_observe(&s_policy, is_fall, result->confidence, now_ms());
    /* Queue post is nonblocking and kept in this critical section so cancel
     * cannot overtake the alert event in the output queue. */
    if (changed) post_event(1U, result->confidence, sample_ts_ms);
    taskEXIT_CRITICAL();
}

bool fall_alert_cancel(void)
{
    taskENTER_CRITICAL();
    bool changed = fall_alert_policy_cancel(&s_policy, now_ms());
    if (changed) post_event(0U, 0.0f, now_ms());
    taskEXIT_CRITICAL();
    return changed;
}

bool fall_alert_test(void)
{
    if (!s_ready) return false;
    taskENTER_CRITICAL();
    s_policy.active = true;
    s_policy.cooling_down = false;
    post_event(2U, 1.0f, now_ms());
    taskEXIT_CRITICAL();
    return true;
}

static void indicator_task(void *arg)
{
    (void)arg;
    bool level = false;
    for (;;)
    {
        level = fall_alert_is_active() ? !level : false;
        cyhal_gpio_write(USER_LED2, level);
        vTaskDelay(pdMS_TO_TICKS(125)); /* 4 flashes per second until cancelled */
    }
}

cy_rslt_t fall_alert_task_init(void)
{
#ifdef ENABLE_RADAR
    return CY_RSLT_TYPE_ERROR; /* radar demo owns LED2 */
#else
    cy_rslt_t result = cyhal_gpio_init(USER_LED2, CYHAL_GPIO_DIR_OUTPUT,
                                     CYHAL_GPIO_DRIVE_STRONG, false);
    if (result != CY_RSLT_SUCCESS) return result;
    if (xTaskCreate(indicator_task, "fallled", 512U, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
        return CY_RSLT_TYPE_ERROR;
    s_ready = true;
    return CY_RSLT_SUCCESS;
#endif
}
