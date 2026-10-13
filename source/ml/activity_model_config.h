#ifndef ACTIVITY_MODEL_CONFIG_H
#define ACTIVITY_MODEL_CONFIG_H

/* Enable only after placing the Studio Queue API export in ml/generated/.
 * No synthetic predictions are produced in a collection-only build. */
#ifndef ACTIVITY_MODEL_ENABLED
#define ACTIVITY_MODEL_ENABLED 0
#endif

/* Must be set to 1 after checking the generated header, class order, units,
 * board orientation and test vectors. Count checks alone cannot verify order. */
#ifndef ACTIVITY_MODEL_CONTRACT_VERIFIED
#define ACTIVITY_MODEL_CONTRACT_VERIFIED 0
#endif

#define ACTIVITY_MODEL_INPUT_COUNT 3U
#define ACTIVITY_MODEL_CLASS_COUNT 6U
#define ACTIVITY_MODEL_CLASS_NAMES {"standing", "running", "walking", "sitting", "jumping", "unlabeled"}
#define ACTIVITY_MODEL_UNKNOWN_CLASS 255U
#define ACTIVITY_MODEL_ACCEL_SCALE (1.0f / 9.80665f) /* m/s^2 -> g */
#define ACTIVITY_MODEL_GYRO_SCALE 1.0f               /* deg/s, for a future 6-axis model */
#define ACTIVITY_MODEL_MIN_CONFIDENCE 0.70f          /* provisional; calibrate on validation data */
#define ACTIVITY_MODEL_MIN_PERIOD_MS 10U
#define ACTIVITY_MODEL_MAX_PERIOD_MS 30U

/* Existing IMUexample: 50 Hz, window [50,3], stride 30. The generated
 * preprocessor owns these windows; firmware must enqueue each sample ONCE.
 * Source test-output header defines the six classes above, including unlabeled.
 * A newly trained model can have different dimensions/classes; update here. */
#endif
