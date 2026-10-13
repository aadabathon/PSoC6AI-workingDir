#include "activity_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int init_count, enqueued, pending, fail_enqueue;
static float captured[3];
static float scores[6] = {0.02f, 0.9f, 0.02f, 0.02f, 0.02f, 0.02f};
int IMAI_init(void) { init_count++; pending = enqueued = 0; return 0; }
int IMAI_enqueue(const float *in)
{
    memcpy(captured, in, sizeof(captured));
    pending = (++enqueued % 2) == 0;
    return fail_enqueue ? -2 : 0;
}
int IMAI_dequeue(float *out)
{
    if (!pending) return -1;
    pending = 0;
    memcpy(out, scores, sizeof(scores));
    return 0;
}
int main(void)
{
    bmi270_reading_t r = {.ax_mps2 = 9.80665f, .ay_mps2 = -9.80665f};
    activity_model_result_t out;
    assert(activity_model_init());
    assert(!activity_model_push(&r, 100, &out));
    assert(activity_model_push(&r, 120, &out));
    assert(fabsf(captured[0] - 1.0f) < 1e-6f && fabsf(captured[1] + 1.0f) < 1e-6f);
    assert(out.class_id == 1 && strcmp(activity_model_class_name(out.class_id), "running") == 0);
    assert(!activity_model_push(&r, 200, &out));
    assert(init_count == 2); /* dropped samples reset temporal context */
    assert(activity_model_push(&r, 220, &out));
    assert(!activity_model_push(&r, 220, &out));
    assert(init_count == 3); /* duplicate timestamp */
    for (int i = 0; i < 6; i++) scores[i] = 1.0f / 6.0f;
    assert(activity_model_push(&r, 240, &out));
    assert(out.class_id == ACTIVITY_MODEL_UNKNOWN_CLASS);
    scores[0] = NAN;
    assert(!activity_model_push(&r, 260, &out));
    assert(!activity_model_push(&r, 280, &out)); /* invalid output discarded */
    scores[0] = 1.0f / 6.0f;
    fail_enqueue = 1;
    assert(!activity_model_push(&r, 300, &out));
    fail_enqueue = 0;
    r.ax_mps2 = NAN;
    assert(!activity_model_push(&r, 320, &out));
    r.ax_mps2 = 9.80665f;
    activity_model_init();
    assert(!activity_model_push(&r, UINT32_MAX - 9U, &out));
    int before = init_count;
    assert(activity_model_push(&r, 10U, &out)); /* 20 ms across uint32 wrap */
    assert(init_count == before);
    puts("activity adapter tests passed (mock model)");
    return 0;
}
