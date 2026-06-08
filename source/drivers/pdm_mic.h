#ifndef PDM_MIC_H
#define PDM_MIC_H

/* ============================================================================
 * pdm_mic.h -- driver for the IM72D128 PDM MEMS microphone.
 *
 * Unlike the I2C sensors (poll-a-register), audio is a continuous stream.
 * The PSoC PDM-PCM hardware block decimates the mic's 1-bit PDM bitstream
 * into 16-bit PCM samples and DMAs them into RAM buffers. We ping-pong two
 * buffers: process one while the hardware fills the other.
 *
 * Pins (from BSP): CYBSP_PDM_CLK (P10_4), CYBSP_PDM_DATA (P10_5).
 * ============================================================================ */

#include "cy_result.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Audio config */
#define PDM_SAMPLE_RATE_HZ   (16000U)
#define PDM_FRAME_SAMPLES    (256U)     /* samples per buffer/callback */

/* Init the PDM-PCM block + start streaming. Provide a callback that fires
 * each time a frame of PDM_FRAME_SAMPLES int16 samples is ready. The callback
 * runs in ISR context -- keep it short (copy/notify, don't process). */
typedef void (*pdm_frame_cb_t)(int16_t *samples, size_t count);

cy_rslt_t pdm_mic_init(pdm_frame_cb_t cb);
cy_rslt_t pdm_mic_start(void);
void      pdm_mic_stop(void);

#endif