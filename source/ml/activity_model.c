#include "activity_model.h"
#include <math.h>
#include <stddef.h>

static const char *const s_classes[] = ACTIVITY_MODEL_CLASS_NAMES;
_Static_assert(sizeof(s_classes) / sizeof(s_classes[0]) == ACTIVITY_MODEL_CLASS_COUNT,
               "Class names must match output count");

const char *activity_model_class_name(uint8_t class_id)
{
    return class_id < ACTIVITY_MODEL_CLASS_COUNT ? s_classes[class_id] : "unknown";
}

#if ACTIVITY_MODEL_ENABLED
#if !ACTIVITY_MODEL_CONTRACT_VERIFIED
#error "Verify generated model units, class order, axes and test vectors first"
#endif
#include "generated/model.h"
#ifndef IMAI_API_QUEUE
#error "Export DEEPCRAFT Queue API without timestamps (IMAI_ prefix)"
#endif
_Static_assert(IMAI_DATA_IN_COUNT == ACTIVITY_MODEL_INPUT_COUNT, "Model input shape mismatch");
_Static_assert(IMAI_DATA_OUT_COUNT == ACTIVITY_MODEL_CLASS_COUNT, "Model class count mismatch");
_Static_assert(ACTIVITY_MODEL_INPUT_COUNT == 3U || ACTIVITY_MODEL_INPUT_COUNT == 6U,
               "Only accelerometer (3) or accelerometer+gyro (6) supported");
static bool s_ready;
static bool s_have_timestamp;
static uint32_t s_previous_ts;

bool activity_model_init(void)
{
    s_have_timestamp = false;
    s_ready = (IMAI_init() == IMAI_RET_SUCCESS);
    return s_ready;
}

bool activity_model_push(const bmi270_reading_t *r, uint32_t ts_ms,
                         activity_model_result_t *out)
{
    if (!s_ready || r == NULL || out == NULL) return false;
    uint32_t dt = ts_ms - s_previous_ts; /* unsigned subtraction handles tick wrap */
    if (s_have_timestamp && (dt < ACTIVITY_MODEL_MIN_PERIOD_MS || dt > ACTIVITY_MODEL_MAX_PERIOD_MS))
    {
        if (!activity_model_init()) return false;
    }
    s_have_timestamp = true;
    s_previous_ts = ts_ms;
    float input[ACTIVITY_MODEL_INPUT_COUNT] = {
        r->ax_mps2 * ACTIVITY_MODEL_ACCEL_SCALE,
        r->ay_mps2 * ACTIVITY_MODEL_ACCEL_SCALE,
        r->az_mps2 * ACTIVITY_MODEL_ACCEL_SCALE,
#if ACTIVITY_MODEL_INPUT_COUNT == 6U
        r->gx_dps * ACTIVITY_MODEL_GYRO_SCALE,
        r->gy_dps * ACTIVITY_MODEL_GYRO_SCALE,
        r->gz_dps * ACTIVITY_MODEL_GYRO_SCALE,
#endif
    };
    for (size_t i = 0; i < ACTIVITY_MODEL_INPUT_COUNT; ++i)
    {
        if (!isfinite(input[i])) { activity_model_init(); return false; }
    }
    if (IMAI_enqueue(input) != IMAI_RET_SUCCESS)
    {
        activity_model_init(); /* discard broken context; warm up again */
        return false;
    }
    float scores[ACTIVITY_MODEL_CLASS_COUNT];
    bool produced = false;
    int status;
    /* Drain output after EVERY enqueue, including warm-up. Studio implements
     * overlap/stride; no application-side windowing or duplicate inputs. */
    while ((status = IMAI_dequeue(scores)) == IMAI_RET_SUCCESS)
    {
        uint8_t best = 0;
        float sum = 0.0f;
        for (size_t i = 0; i < ACTIVITY_MODEL_CLASS_COUNT; ++i)
        {
            if (!isfinite(scores[i]) || scores[i] < 0.0f || scores[i] > 1.0f)
            {
                activity_model_init();
                return false;
            }
            sum += scores[i];
            if (scores[i] > scores[best]) best = (uint8_t)i;
        }
        if (sum < 0.98f || sum > 1.02f) { activity_model_init(); return false; }
        out->confidence = scores[best];
        out->class_id = scores[best] >= ACTIVITY_MODEL_MIN_CONFIDENCE ? best : ACTIVITY_MODEL_UNKNOWN_CLASS;
        produced = true;
    }
    if (status != IMAI_RET_NODATA) { activity_model_init(); return false; }
    return produced;
}
#else
bool activity_model_init(void) { return false; }
bool activity_model_push(const bmi270_reading_t *reading, uint32_t ts_ms,
                         activity_model_result_t *out)
{
    (void)reading; (void)ts_ms; (void)out;
    return false;
}
#endif
