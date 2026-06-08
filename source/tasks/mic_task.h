#ifndef MIC_TASK_H
#define MIC_TASK_H

#include "cy_result.h"
#include "FreeRTOS.h"
#include "queue.h"

typedef struct
{
    float    rms;        /* RMS amplitude of the frame (loudness)  */
    int16_t  peak;       /* peak absolute sample                   */
} mic_reading_t;

cy_rslt_t      mic_task_init(void);
QueueHandle_t  mic_task_get_queue(void);

#endif