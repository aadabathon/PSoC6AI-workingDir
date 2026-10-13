#ifndef IMU_TASK_H
#define IMU_TASK_H

/* ============================================================================
 * imu_task.h -- public interface to the BMI270 owner task
 *
 * Mirror of barometer_task: one task owns the chip, others subscribe to
 * its queue. Init once from main() AFTER i2c_bus_init() and
 * cy_retarget_io_init().
 * ============================================================================ */

#include "cy_result.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "bmi270.h"

typedef struct {
    uint32_t ts_ms;
    bmi270_reading_t reading;
} imu_sample_t;

cy_rslt_t       imu_task_init(void);
QueueHandle_t   imu_task_get_queue(void);

#endif
