/* ============================================================================
 * mag_task.c -- BMM350 owner task using Bosch's BMM350 SensorAPI
 *
 * Unlike the from-scratch drivers, this task is a thin wrapper:
 *   1. Wire up Bosch's callbacks via bmm350_glue_attach().
 *   2. bmm350_init() -- soft-reset, OTP load, comp coeffs cached internally.
 *   3. Configure ODR / averaging, enable XYZ axes, set normal mode.
 *   4. Loop: bmm350_get_compensated_mag_xyz_temp_data() -> queue + print.
 *
 * Earth's field is ~25-65 uT total. At rest on a desk you should see all
 * three axes in the tens-of-uT range. Wave a magnet near the board and
 * watch values pin to the saturation limits (+/- a few hundred uT).
 * ============================================================================ */

#include "mag_task.h"
#include "bmm350.h"
#include "bmm350_defs.h"
#include "bmm350_glue.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>

#define SAMPLE_PERIOD_MS        (200U)       
#define PRINT_DECIMATION        (5U)        /* print every 5th = 4 Hz */
#define QUEUE_LENGTH            (4U)
#define TASK_STACK_WORDS        (2048U)     /* Bosch lib + printf floats = hungry */
#define TASK_PRIORITY           (tskIDLE_PRIORITY + 2)

static struct bmm350_dev  s_dev;
static QueueHandle_t      s_queue       = NULL;
static TaskHandle_t       s_task        = NULL;

/* Convert a Bosch return code (int8_t, 0 = OK) into a cy_rslt_t. The
 * cast keeps the negative-number information so 0x...80 in our error
 * prints maps to Bosch's BMM350_E_NULL_PTR etc. */
static cy_rslt_t to_cy_rslt(int8_t bosch_rslt)
{
    return (bosch_rslt == BMM350_OK)
         ? CY_RSLT_SUCCESS
         : (cy_rslt_t)(0x80000000UL | (uint8_t)bosch_rslt);
}

static void mag_task(void *arg)
{
    (void)arg;
    int8_t    brslt;
    cy_rslt_t rslt;

    /* --- Wire up our cyhal/FreeRTOS callbacks into the Bosch dev struct */
    rslt = bmm350_glue_attach(&s_dev);
    if (rslt != CY_RSLT_SUCCESS)
    {
        printf("[mag] glue attach failed\r\n");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* --- bmm350_init: soft reset, chip-id check, OTP load + compensation
     *     coefficient unpack, magnetic-reset (FGR/BR) pulse sequence. ---- */
    brslt = bmm350_init(&s_dev);
    if (brslt != BMM350_OK)
    {
        printf("[mag] bmm350_init failed: bosch=%d (cy=0x%08lx)\r\n",
               (int)brslt, (unsigned long)to_cy_rslt(brslt));
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("[mag] BMM350 initialized (chip_id=0x%02x)\r\n", s_dev.chip_id);

    /* --- ODR + averaging. 25 Hz ODR with 4x averaging is a sensible
     *     middle ground -- enough rate for compass / orientation work,
     *     enough averaging to keep noise floor sane. Increase averaging
     *     if you see jitter, decrease ODR to save power. ----------------- */
    brslt = bmm350_set_odr_performance(BMM350_DATA_RATE_25HZ,
                                       BMM350_AVERAGING_4,
                                       &s_dev);
    if (brslt != BMM350_OK) { printf("[mag] odr cfg fail: %d\r\n", brslt); }

    /* --- Enable all three axes. --------------------------------------- */
    brslt = bmm350_enable_axes(BMM350_X_EN, BMM350_Y_EN, BMM350_Z_EN, &s_dev);
    if (brslt != BMM350_OK) { printf("[mag] axes en fail: %d\r\n", brslt); }

    /* --- Kick into normal (continuous) measurement mode. -------------- */
    brslt = bmm350_set_powermode(BMM350_NORMAL_MODE, &s_dev);
    if (brslt != BMM350_OK) { printf("[mag] pwr mode fail: %d\r\n", brslt); }

    TickType_t       next_wake = xTaskGetTickCount();
    const TickType_t period    = pdMS_TO_TICKS(SAMPLE_PERIOD_MS);
    uint32_t         tick      = 0;

    for (;;)
    {
        struct bmm350_mag_temp_data raw;
        brslt = bmm350_get_compensated_mag_xyz_temp_data(&raw, &s_dev);

        if (brslt == BMM350_OK)
        {
            mag_reading_t r = {
                .mx_uT  = raw.x,
                .my_uT  = raw.y,
                .mz_uT  = raw.z,
                .temp_c = raw.temperature,
            };
            (void)xQueueSend(s_queue, &r, 0);

            if ((tick++ % PRINT_DECIMATION) == 0)
            {
                /* |B| -- field magnitude. Should hold roughly steady at
                 * ~50 uT (give or take a lot, depending on indoor metal)
                 * as you rotate the board. If it swings wildly, the
                 * compensation isn't doing its job. */
                float mag = sqrtf(r.mx_uT * r.mx_uT
                                + r.my_uT * r.my_uT
                                + r.mz_uT * r.mz_uT);
                printf("[mag] B=(%+7.1f %+7.1f %+7.1f) uT  |B|=%6.1f uT  T=%.1f C\r\n",
                       r.mx_uT, r.my_uT, r.mz_uT, mag, r.temp_c);
            }
        }
        else
        {
            printf("[mag] read err: %d\r\n", brslt);
        }

        vTaskDelayUntil(&next_wake, period);
    }
}

cy_rslt_t mag_task_init(void)
{
    s_queue = xQueueCreate(QUEUE_LENGTH, sizeof(mag_reading_t));
    if (s_queue == NULL) return CY_RSLT_TYPE_ERROR;

    BaseType_t ok = xTaskCreate(mag_task, "mag",
                                TASK_STACK_WORDS, NULL,
                                TASK_PRIORITY, &s_task);
    if (ok == pdPASS)
    {
        printf("[mag] task create passed\r\n");
    }
    return (ok == pdPASS) ? CY_RSLT_SUCCESS : CY_RSLT_TYPE_ERROR;
}

QueueHandle_t mag_task_get_queue(void)
{
    return s_queue;
}