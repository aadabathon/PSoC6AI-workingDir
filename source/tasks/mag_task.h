#ifndef MAG_TASK_H
#define MAG_TASK_H

#include "cy_result.h"
#include "FreeRTOS.h"
#include "queue.h"

typedef struct
{
    float mx_uT;        /* X-axis field, micro-Tesla   */
    float my_uT;        /* Y-axis field                */
    float mz_uT;        /* Z-axis field                */
    float temp_c;       /* Die temperature, deg C      */
} mag_reading_t;

cy_rslt_t      mag_task_init(void);
QueueHandle_t  mag_task_get_queue(void);

#endif