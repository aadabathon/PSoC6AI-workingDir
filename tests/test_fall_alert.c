#include "fall_alert_policy.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    fall_alert_policy_t state = {0};
    assert(!fall_alert_policy_cancel(&state, 0));
    assert(!fall_alert_policy_observe(&state, false, 1.0f, 0));
    assert(!fall_alert_policy_observe(&state, true, 0.5f, 0));
    assert(!fall_alert_policy_observe(&state, true, NAN, 0));
    assert(fall_alert_policy_observe(&state, true, 0.9f, 100));
    assert(state.active);
    assert(!fall_alert_policy_observe(&state, true, 1.0f, 200)); /* no repeated alert */
    assert(fall_alert_policy_cancel(&state, 300));
    assert(!state.active);
    assert(!fall_alert_policy_cancel(&state, 400)); /* inactive button can toggle recording */
    assert(!fall_alert_policy_observe(&state, true, 1.0f, 30299));
    assert(fall_alert_policy_observe(&state, true, 1.0f, 30300));
    assert(fall_alert_policy_cancel(&state, UINT32_MAX - 999U));
    assert(!fall_alert_policy_observe(&state, true, 1.0f, 1000U));
    assert(fall_alert_policy_observe(&state, true, 1.0f, 29000U));
    puts("fall alert policy tests passed");
    return 0;
}
