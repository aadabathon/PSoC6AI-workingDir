/* ============================================================================
 * activity_model.c -- PLACEHOLDER for the DEEPCRAFT-exported activity model.
 *
 * Kept intentionally inert (activity_model_init() returns false) so the rest
 * of the firmware builds and runs today. See activity_model.h for the
 * replacement steps once you have a DEEPCRAFT Studio export.
 * ============================================================================ */

#include "activity_model.h"
#include <stddef.h>

static bool s_ready = false;

bool activity_model_init(void)
{
    /* TODO: call the generated model's init function here, e.g.
     *   s_ready = (imai_model_init() == IMAI_RET_SUCCESS);
     */
    s_ready = false;
    return s_ready;
}

bool activity_model_run(const bmi270_reading_t *window, activity_model_result_t *out)
{
    (void)window;
    (void)out;

    if (!s_ready) return false;

    /* TODO: call the generated model's inference function on `window`
     * (ACTIVITY_MODEL_WINDOW_LEN samples) and map its output into *out. */
    return false;
}
