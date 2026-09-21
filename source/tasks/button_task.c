/* ============================================================================
 * button_task.c -- USER BTN1 toggles recording, USER LED1 mirrors it.
 *
 * The button ISR only flips the logger's enable flag (plain bool writes,
 * ISR-safe) with a tick-based debounce. A small task polls the flag and
 * drives the LED, so the LED also tracks state changes made from the CLI.
 * ============================================================================ */

#include "button_task.h"
#include "logger.h"
#include "resource_map.h"

#include "cyhal.h"
#include "cybsp.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>

#define BTN_IRQ_PRIORITY     (7U)
#define DEBOUNCE_MS          (250U)
#define LED_POLL_MS          (100U)
#define TASK_STACK_WORDS     (512U)
#define TASK_PRIORITY        (tskIDLE_PRIORITY + 1)

static TaskHandle_t             s_task = NULL;
static cyhal_gpio_callback_data_t s_btn_cb_data;
static volatile TickType_t      s_last_press_tick = 0;

static void button_isr(void *arg, cyhal_gpio_event_t event)
{
    (void)arg;
    (void)event;

    /* Tick-based debounce: ignore edges within DEBOUNCE_MS of the last. */
    TickType_t now = xTaskGetTickCountFromISR();
    if ((now - s_last_press_tick) < pdMS_TO_TICKS(DEBOUNCE_MS)) return;
    s_last_press_tick = now;

    logger_set_enabled(!logger_get_enabled());
}

static void led_task(void *arg)
{
    (void)arg;
    bool last = logger_get_enabled();
    cyhal_gpio_write(USER_LED1, last);

    for (;;)
    {
        bool now = logger_get_enabled();
        if (now != last)
        {
            cyhal_gpio_write(USER_LED1, now);
            last = now;
        }
        vTaskDelay(pdMS_TO_TICKS(LED_POLL_MS));
    }
}

cy_rslt_t button_task_init(void)
{
    cy_rslt_t rslt;

    rslt = cyhal_gpio_init(USER_LED1, CYHAL_GPIO_DIR_OUTPUT,
                           CYHAL_GPIO_DRIVE_STRONG, false);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    rslt = cyhal_gpio_init(CYBSP_USER_BTN, CYHAL_GPIO_DIR_INPUT,
                           CYBSP_USER_BTN_DRIVE, true);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    s_btn_cb_data.callback     = button_isr;
    s_btn_cb_data.callback_arg = NULL;
    cyhal_gpio_register_callback(CYBSP_USER_BTN, &s_btn_cb_data);
    /* Button is active-low (pull-up): press = falling edge. */
    cyhal_gpio_enable_event(CYBSP_USER_BTN, CYHAL_GPIO_IRQ_FALL,
                            BTN_IRQ_PRIORITY, true);

    BaseType_t ok = xTaskCreate(led_task, "btnled",
                                TASK_STACK_WORDS, NULL,
                                TASK_PRIORITY, &s_task);
    return (ok == pdPASS) ? CY_RSLT_SUCCESS : CY_RSLT_TYPE_ERROR;
}
