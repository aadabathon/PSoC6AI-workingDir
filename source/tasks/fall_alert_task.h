#ifndef FALL_ALERT_TASK_H
#define FALL_ALERT_TASK_H
#include "activity_model.h"
#include "cy_result.h"

cy_rslt_t fall_alert_task_init(void);
void fall_alert_observe(const activity_model_result_t *result, uint32_t sample_ts_ms);
bool fall_alert_cancel(void); /* true consumes the button press; recording unchanged */
bool fall_alert_is_active(void);
bool fall_alert_test(void);   /* explicit CLI test; logged separately from model alerts */
#endif
