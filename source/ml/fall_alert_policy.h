#ifndef FALL_ALERT_POLICY_H
#define FALL_ALERT_POLICY_H
#include <stdbool.h>
#include <stdint.h>

/* Provisional operating point: tune with held-out validation events. */
#define FALL_ALERT_CONFIDENCE 0.85f
#define FALL_ALERT_COOLDOWN_MS 30000U
typedef struct {
    bool active;
    bool cooling_down;
    uint32_t cancelled_ms;
} fall_alert_policy_t;

bool fall_alert_policy_observe(fall_alert_policy_t *state, bool is_fall, float confidence, uint32_t now_ms);
bool fall_alert_policy_cancel(fall_alert_policy_t *state, uint32_t now_ms);
#endif
