/* ============================================================================
 * pdm_mic.c -- IM72D128 PDM microphone driver (cyhal_pdm_pcm)
 * ============================================================================ */

#include "pdm_mic.h"
#include "cyhal.h"
#include "cybsp.h"
#include <string.h>

/* Ping-pong buffers: HW fills one while we process the other. */
static int16_t        s_buf_a[PDM_FRAME_SAMPLES];
static int16_t        s_buf_b[PDM_FRAME_SAMPLES];
static int16_t       *s_active_buf = s_buf_a;   /* the one HW is filling now */
static cyhal_pdm_pcm_t s_pdm;
static pdm_frame_cb_t  s_user_cb = NULL;
static bool            s_inited  = false;

/* HAL async-read complete callback. Fires in ISR context when a buffer fills.
 * We hand the user the full buffer, then immediately queue the OTHER buffer
 * for the hardware so we never drop samples. */
static void pdm_event_cb(void *arg, cyhal_pdm_pcm_event_t event)
{
    (void)arg;
    if ((event & CYHAL_PDM_PCM_ASYNC_COMPLETE) != 0u)
    {
        int16_t *full = s_active_buf;
        /* swap to the other buffer for the next HW fill */
        s_active_buf = (s_active_buf == s_buf_a) ? s_buf_b : s_buf_a;
        (void)cyhal_pdm_pcm_read_async(&s_pdm, s_active_buf, PDM_FRAME_SAMPLES);

        if (s_user_cb != NULL)
            s_user_cb(full, PDM_FRAME_SAMPLES);
    }
}

cy_rslt_t pdm_mic_init(pdm_frame_cb_t cb)
{
    if (s_inited) return CY_RSLT_SUCCESS;
    s_user_cb = cb;

    /* PDM-PCM config: 16 kHz, mono (left), 16-bit.
     * The clock divider math (audio clock -> PDM clock -> decimation -> 16kHz)
     * is handled by the HAL given these params on the PSoC6 audio subsystem. */
    static const cyhal_pdm_pcm_cfg_t pdm_cfg =
    {
        .sample_rate     = PDM_SAMPLE_RATE_HZ,
        .decimation_rate = 64,                       /* typical for PDM */
        .mode            = CYHAL_PDM_PCM_MODE_LEFT,  /* mono, left channel */
        .word_length     = 16,                       /* bits per sample */
        .left_gain       = CYHAL_PDM_PCM_MAX_GAIN,   /* tune later if quiet */
        .right_gain      = CYHAL_PDM_PCM_MAX_GAIN,
    };

    /* The PDM block needs an audio clock source. Passing NULL lets the HAL
     * allocate and configure an appropriate clock automatically. */
    cy_rslt_t rslt = cyhal_pdm_pcm_init(&s_pdm,
                                        CYBSP_PDM_DATA,
                                        CYBSP_PDM_CLK,
                                        NULL,           /* HAL-managed clock */
                                        &pdm_cfg);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    cyhal_pdm_pcm_register_callback(&s_pdm, pdm_event_cb, NULL);
    cyhal_pdm_pcm_enable_event(&s_pdm,
                               CYHAL_PDM_PCM_ASYNC_COMPLETE,
                               CYHAL_ISR_PRIORITY_DEFAULT,
                               true);

    s_inited = true;
    return CY_RSLT_SUCCESS;
}

cy_rslt_t pdm_mic_start(void)
{
    if (!s_inited) return CY_RSLT_TYPE_ERROR;
    cy_rslt_t rslt = cyhal_pdm_pcm_start(&s_pdm);
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    /* Kick off the first async read; the callback re-arms subsequent ones. */
    return cyhal_pdm_pcm_read_async(&s_pdm, s_active_buf, PDM_FRAME_SAMPLES);
}

void pdm_mic_stop(void)
{
    if (s_inited) (void)cyhal_pdm_pcm_stop(&s_pdm);
}