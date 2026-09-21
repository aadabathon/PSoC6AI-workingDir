#ifndef ACTIVITY_MODEL_H
#define ACTIVITY_MODEL_H

/* ============================================================================
 * activity_model.h -- contract between ml_task.c and the DEEPCRAFT-exported
 * activity classifier (DeepCraft/IMUexample, model conv1d-medium-balanced-3:
 * running/standing/walking/sitting/jumping off 50 Hz IMU data).
 *
 * This header/its .c are a PLACEHOLDER. Once you export the model from
 * DEEPCRAFT Studio (Deploy -> C source library), replace the body of
 * activity_model_init/run in activity_model.c with calls into the generated
 * API, and update ACTIVITY_MODEL_WINDOW_LEN below to match the generated
 * header's window/frame size constant.
 * ============================================================================ */

#include "bmi270.h"
#include <stdint.h>
#include <stdbool.h>

/* Consecutive 50 Hz IMU samples per inference (1s window at the imu_task
 * rate). TODO: replace with the DEEPCRAFT-exported window length once known. */
#define ACTIVITY_MODEL_WINDOW_LEN   (50U)

typedef struct {
    uint8_t class_id;    /* index into logger.c's ML_CLASS_NAMES */
    float   confidence;  /* 0.0 - 1.0 */
} activity_model_result_t;

/* One-time init. Returns false if no model is wired in yet (safe to ignore
 * at boot -- ml_task just idles without posting samples). */
bool activity_model_init(void);

/* Runs inference over exactly ACTIVITY_MODEL_WINDOW_LEN consecutive IMU
 * readings, oldest first. Returns false if inference did not run (model not
 * initialized) -- caller should skip posting a log_sample_t in that case. */
bool activity_model_run(const bmi270_reading_t *window, activity_model_result_t *out);

#endif
