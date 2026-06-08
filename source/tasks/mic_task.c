/* ============================================================================
 * mic_task.c -- owns the PDM mic. The HAL fills audio frames via DMA+ISR;
 * we compute a cheap loudness feature (RMS + peak) per frame and stream it.
 *
 * We deliberately do NOT push raw audio (16kHz*16bit = 256kbps is too much
 * for the shared output path). RMS/peak proves the mic works and is enough
 * for "is it loud / quiet" demos. Full audio capture + features come in the
 * data-pipeline phase.
 * ============================================================================ */

#include "mic_task.h"
#include "pdm_mic.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdio.h>
#include <math.h>

#define QUEUE_LENGTH        (4U)
#define TASK_STACK_WORDS    (1024U)
#define TASK_PRIORITY       (tskIDLE_PRIORITY + 2)
#define PRINT_DECIMATION    (8U)   /* ~16kHz/256 = 62.5 frames/s -> print ~8/s */

static QueueHandle_t s_queue = NULL;
static TaskHandle_t  s_task  = NULL;

/* Called from the PDM ISR each time a 256-sample frame is ready. KEEP SHORT --
 * we compute RMS/peak here (cheap, ~256 MACs) and post to the queue. Doing the
 * math in the ISR is fine for this size; if it grew we'd defer to the task. */
static void on_audio_frame(int16_t *samples, size_t count)
{
    int64_t sum_sq = 0;
    int16_t peak   = 0;
    for (size_t i = 0; i < count; i++)
    {
        int16_t s = samples[i];
        int16_t a = (s < 0) ? (int16_t)(-s) : s;
        if (a > peak) peak = a;
        sum_sq += (int64_t)s * s;
    }

    mic_reading_t r = {
        .rms  = sqrtf((float)sum_sq / (float)count),
        .peak = peak,
    };

    /* ISR-safe queue send. Drop-on-full (freshest wins), like the other sensors. */
    BaseType_t hpw = pdFALSE;
    (void)xQueueSendFromISR(s_queue, &r, &hpw);
    portYIELD_FROM_ISR(hpw);
}

static void mic_task(void *arg)
{
    (void)arg;

    if (pdm_mic_init(on_audio_frame) != CY_RSLT_SUCCESS)
    {
        printf("[mic] pdm_mic_init failed\r\n");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (pdm_mic_start() != CY_RSLT_SUCCESS)
    {
        printf("[mic] pdm_mic_start failed\r\n");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("[mic] IM72D128 streaming at 16 kHz\r\n");

    uint32_t tick = 0;
    for (;;)
    {
        mic_reading_t r;
        /* Block until the ISR posts a frame's features. */
        if (xQueueReceive(s_queue, &r, portMAX_DELAY) == pdTRUE)
        {
            if ((tick++ % PRINT_DECIMATION) == 0)
                printf("[mic] RMS=%7.1f  peak=%5d\r\n", r.rms, r.peak);
        }
    }
}

cy_rslt_t mic_task_init(void)
{
    s_queue = xQueueCreate(QUEUE_LENGTH, sizeof(mic_reading_t));
    if (s_queue == NULL) return CY_RSLT_TYPE_ERROR;

    BaseType_t ok = xTaskCreate(mic_task, "mic",
                                TASK_STACK_WORDS, NULL,
                                TASK_PRIORITY, &s_task);
    return (ok == pdPASS) ? CY_RSLT_SUCCESS : CY_RSLT_TYPE_ERROR;
}

QueueHandle_t mic_task_get_queue(void) { return s_queue; }