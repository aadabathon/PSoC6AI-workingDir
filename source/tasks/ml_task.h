#ifndef ML_TASK_H
#define ML_TASK_H

/* ============================================================================
 * ml_task.h -- public interface to the on-device activity classifier task
 *
 * Init AFTER imu_task_init() -- this task subscribes to imu_task's reading
 * queue (imu_task_get_queue()) and has nothing to do if that doesn't exist
 * yet.
 * ============================================================================ */

#include "cy_result.h"

cy_rslt_t ml_task_init(void);

#endif
