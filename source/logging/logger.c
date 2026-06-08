/* ============================================================================
 * logger.c -- the single output-owner task.
 * ============================================================================ */

#include "logger.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>

#define LOGGER_QUEUE_LEN     (32U)        /* headroom for bursty producers */
#define LOGGER_STACK_WORDS   (2048U)      /* printf with floats is hungry */
#define LOGGER_PRIORITY      (tskIDLE_PRIORITY + 1)  /* below sensors, above idle */
#define CSV_ROW_PERIOD_MS    (50U)        /* 20 Hz merged rows */

static QueueHandle_t s_queue = NULL;
static TaskHandle_t  s_task  = NULL;
static volatile log_mode_t s_mode = LOG_MODE_HUMAN;

/* The logger holds the latest reading from each sensor. In CSV mode it emits
 * all of them together every CSV_ROW_PERIOD_MS -> aligned feature vectors. */
static log_sample_t s_latest[5];      /* indexed by sample_src_t */
static bool         s_seen[5];        /* have we received this sensor yet? */

void logger_post(const log_sample_t *s)
{
    if (s_queue == NULL) return;
    /* Called from both tasks and ISRs (mic). Use the FromISR variant when
     * in ISR context. Detecting context is awkward; simplest robust choice:
     * the mic posts via a task, not its ISR. So plain send here. (If you ever
     * post from a true ISR, switch to xQueueSendFromISR.) */
    (void)xQueueSend(s_queue, s, 0);   /* drop-on-full: freshest wins */
}

void  logger_set_mode(log_mode_t mode) { s_mode = mode; }
log_mode_t logger_get_mode(void)       { return s_mode; }

static void print_human(const log_sample_t *s)
{
    switch (s->src) {
        case SRC_BARO:  printf("[baro] P=%.1f Pa T=%.1f C\r\n", s->d.baro.pa, s->d.baro.temp_c); break;
        case SRC_IMU:   printf("[imu] a=(%.2f %.2f %.2f) g=(%.1f %.1f %.1f)\r\n",
                               s->d.imu.ax,s->d.imu.ay,s->d.imu.az, s->d.imu.gx,s->d.imu.gy,s->d.imu.gz); break;
        case SRC_MAG:   printf("[mag] B=(%.1f %.1f %.1f) uT\r\n", s->d.mag.mx,s->d.mag.my,s->d.mag.mz); break;
        case SRC_RADAR: printf("[radar] present=%ld bin=%ld\r\n", (long)s->d.radar.presence,(long)s->d.radar.range_bin); break;
        case SRC_MIC:   printf("[mic] rms=%.1f peak=%d\r\n", s->d.mic.rms, s->d.mic.peak); break;
    }
}

/* Emit the CSV header once, so Python knows the columns. */
static void print_csv_header(void)
{
    printf("ts_ms,baro_pa,baro_t,ax,ay,az,gx,gy,gz,imu_t,"
           "mx,my,mz,mag_t,radar_present,radar_bin,mic_rms,mic_peak\r\n");
}

/* Emit one merged row from the latest-of-each cache. */
static void print_csv_row(uint32_t ts)
{
    const log_sample_t *b = &s_latest[SRC_BARO];
    const log_sample_t *i = &s_latest[SRC_IMU];
    const log_sample_t *m = &s_latest[SRC_MAG];
    const log_sample_t *r = &s_latest[SRC_RADAR];
    const log_sample_t *c = &s_latest[SRC_MIC];

    printf("%lu,%.1f,%.1f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.1f,"
           "%.1f,%.1f,%.1f,%.1f,%ld,%ld,%.1f,%d\r\n",
        (unsigned long)ts,
        b->d.baro.pa, b->d.baro.temp_c,
        i->d.imu.ax, i->d.imu.ay, i->d.imu.az, i->d.imu.gx, i->d.imu.gy, i->d.imu.gz, i->d.imu.temp_c,
        m->d.mag.mx, m->d.mag.my, m->d.mag.mz, m->d.mag.temp_c,
        (long)r->d.radar.presence, (long)r->d.radar.range_bin,
        c->d.mic.rms, c->d.mic.peak);
}

static void logger_task(void *arg)
{
    (void)arg;
    TickType_t next_csv = xTaskGetTickCount();
    bool header_printed = false;

    for (;;)
    {
        log_sample_t s;
        /* Wait up to the CSV cadence for a new sample. */
        if (xQueueReceive(s_queue, &s, pdMS_TO_TICKS(CSV_ROW_PERIOD_MS)) == pdTRUE)
        {
            s_latest[s.src] = s;       /* update cache */
            s_seen[s.src]   = true;

            if (s_mode == LOG_MODE_HUMAN)
                print_human(&s);        /* human mode: print each as it arrives */
        }

        /* CSV mode: emit a merged row on a fixed cadence regardless of arrivals */
        if (s_mode == LOG_MODE_CSV)
        {
            if (!header_printed) { print_csv_header(); header_printed = true; }
            if (xTaskGetTickCount() - next_csv >= pdMS_TO_TICKS(CSV_ROW_PERIOD_MS))
            {
                print_csv_row(xTaskGetTickCount() * portTICK_PERIOD_MS);
                next_csv = xTaskGetTickCount();
            }
        }
        else
        {
            header_printed = false;     /* reset so header reprints on next CSV switch */
        }
    }
}

cy_rslt_t logger_init(void)
{
    memset(s_latest, 0, sizeof(s_latest));
    memset(s_seen, 0, sizeof(s_seen));

    s_queue = xQueueCreate(LOGGER_QUEUE_LEN, sizeof(log_sample_t));
    if (s_queue == NULL) return CY_RSLT_TYPE_ERROR;

    BaseType_t ok = xTaskCreate(logger_task, "logger",
                                LOGGER_STACK_WORDS, NULL,
                                LOGGER_PRIORITY, &s_task);
    return (ok == pdPASS) ? CY_RSLT_SUCCESS : CY_RSLT_TYPE_ERROR;
}