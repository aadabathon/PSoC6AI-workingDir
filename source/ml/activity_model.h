#ifndef ACTIVITY_MODEL_H
#define ACTIVITY_MODEL_H
#include "bmi270.h"
#include "activity_model_config.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t class_id;
    float confidence;
} activity_model_result_t;

bool activity_model_init(void);
/* Feed one 50 Hz sample. Returns true only when a new valid output exists.
 * Timestamp discontinuities reset generated preprocessing state. */
bool activity_model_push(const bmi270_reading_t *reading, uint32_t ts_ms,
                         activity_model_result_t *out);
const char *activity_model_class_name(uint8_t class_id);
#endif
