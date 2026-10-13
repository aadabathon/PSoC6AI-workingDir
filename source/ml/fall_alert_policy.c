#include "fall_alert_policy.h"
#include <math.h>

bool fall_alert_policy_observe(fall_alert_policy_t *s, bool is_fall, float confidence, uint32_t now_ms)
{
    if (s->active) return false; /* one alert per latched event */
    if (s->cooling_down)
    {
        if ((uint32_t)(now_ms - s->cancelled_ms) < FALL_ALERT_COOLDOWN_MS) return false;
        s->cooling_down = false;
    }
    if (!is_fall || !isfinite(confidence) || confidence < FALL_ALERT_CONFIDENCE || confidence > 1.0f)
        return false;
    s->active = true;
    return true;
}

bool fall_alert_policy_cancel(fall_alert_policy_t *s, uint32_t now_ms)
{
    if (!s->active) return false;
    s->active = false;
    s->cooling_down = true;
    s->cancelled_ms = now_ms;
    return true;
}
