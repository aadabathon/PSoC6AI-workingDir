#ifndef WEARABLE_CONFIG_H
#define WEARABLE_CONFIG_H
/* Application task switches; not a measured low-power/battery configuration.
 * Set WEARABLE_PROFILE=1 at build time to acquire only the IMU by default.
 * Flash, CLI, button and model remain available. */
#ifndef WEARABLE_PROFILE
#define WEARABLE_PROFILE 0
#endif
#ifndef WEARABLE_AUX_SENSORS
#define WEARABLE_AUX_SENSORS (!WEARABLE_PROFILE)
#endif
#ifndef WEARABLE_WIFI
#define WEARABLE_WIFI (!WEARABLE_PROFILE)
#endif
#endif
