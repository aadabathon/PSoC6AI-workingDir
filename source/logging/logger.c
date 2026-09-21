/* ============================================================================
 * logger.c -- the single output-owner task.
 * ============================================================================ */

#include "logger.h"
#include "logger_flash.h"
#include "wifi_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>

#define LOGGER_QUEUE_LEN     (64U)        /* headroom for bursty producers (50 Hz IMU + rest) */
#define LOGGER_STACK_WORDS   (2048U)      /* printf with floats is hungry */
#define LOGGER_PRIORITY      (tskIDLE_PRIORITY + 1)  /* below sensors, above idle */
#define QUEUE_WAIT_MS        (100U)       /* poll granularity when idle */

static QueueHandle_t s_queue = NULL;
static TaskHandle_t  s_task  = NULL;
static volatile log_mode_t s_mode    = LOG_MODE_HUMAN;
static volatile bool       s_enabled = false;   /* recording gate: off until start */
static volatile uint8_t    s_sink    = LOGGER_SINK_UART;   /* default: UART only */

static char s_label[LOGGER_LABEL_MAX] = "unlabeled";

/* The logger holds the latest reading from each sensor. In CSV mode it emits
 * all of them merged, one row per IMU sample -> aligned feature vectors at
 * the IMU rate. */
static log_sample_t s_latest[5];      /* indexed by sample_src_t */
static bool         s_seen[5];        /* have we received this sensor yet? */
static bool         s_header_needed = true;

void logger_post(const log_sample_t *s)
{
    if (s_queue == NULL) return;
    /* All producers post from task context (the mic computes its features in
     * its ISR but hands them to its own task, which posts here). If you ever
     * post from a true ISR, switch to xQueueSendFromISR. */
    (void)xQueueSend(s_queue, s, 0);   /* drop-on-full: freshest wins */
}

void logger_set_mode(log_mode_t mode)
{
    s_mode = mode;
    s_header_needed = true;            /* header reprints when CSV resumes */
}
log_mode_t logger_get_mode(void) { return s_mode; }

void logger_set_enabled(bool on)
{
    if (on && !s_enabled)
    {
        s_header_needed = true;   /* header per session */
        if (s_sink & LOGGER_SINK_FLASH)
        {
            uint32_t ts = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            logger_flash_enqueue_session_start(ts, s_label);
        }
    }
    s_enabled = on;
}
bool logger_get_enabled(void) { return s_enabled; }

void    logger_set_sink(uint8_t mask) { s_sink = mask; }
uint8_t logger_get_sink(void)         { return s_sink; }

void logger_set_label(const char *label)
{
    /* Critical section so the logger task never snapshots a half-written
     * label mid-row. Copies are a few bytes -- negligible IRQ latency. */
    taskENTER_CRITICAL();
    strncpy(s_label, label, LOGGER_LABEL_MAX - 1);
    s_label[LOGGER_LABEL_MAX - 1] = '\0';
    taskEXIT_CRITICAL();
}

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

/* Single source of truth for the header text, so UART and TCP send the
 * exact same bytes -- one printf, one wifi_tcp_send, no risk of drift. */
static const char CSV_HEADER[] =
    "ts_ms,baro_pa,baro_t,ax,ay,az,gx,gy,gz,imu_t,"
    "mx,my,mz,mag_t,radar_present,radar_bin,mic_rms,mic_peak,label\r\n";

/* Emit one merged row from the latest-of-each cache into `row` (NUL
 * terminated, no trailing \r\n -- callers add that per sink). Sensors not
 * yet seen emit empty fields -- pandas turns those into NaN instead of fake
 * zeros. APPEND clamps n so even a pathological float (a glitched sensor
 * value printed with %f can be 40+ chars) truncates the row instead of
 * walking off the end of the buffer. */
#define APPEND(...)                                                      \
    do {                                                                 \
        n += snprintf(row + n, row_size - (size_t)n, __VA_ARGS__);       \
        if (n >= (int)row_size) n = (int)row_size - 1;                   \
    } while (0)

static void build_csv_row(char *row, size_t row_size, uint32_t ts)
{
    int n = 0;

    APPEND("%lu,", (unsigned long)ts);

    if (s_seen[SRC_BARO])
        APPEND("%.1f,%.1f,",
               s_latest[SRC_BARO].d.baro.pa, s_latest[SRC_BARO].d.baro.temp_c);
    else
        APPEND(",,");

    if (s_seen[SRC_IMU])
        APPEND("%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.1f,",
               s_latest[SRC_IMU].d.imu.ax, s_latest[SRC_IMU].d.imu.ay, s_latest[SRC_IMU].d.imu.az,
               s_latest[SRC_IMU].d.imu.gx, s_latest[SRC_IMU].d.imu.gy, s_latest[SRC_IMU].d.imu.gz,
               s_latest[SRC_IMU].d.imu.temp_c);
    else
        APPEND(",,,,,,,");

    if (s_seen[SRC_MAG])
        APPEND("%.1f,%.1f,%.1f,%.1f,",
               s_latest[SRC_MAG].d.mag.mx, s_latest[SRC_MAG].d.mag.my,
               s_latest[SRC_MAG].d.mag.mz, s_latest[SRC_MAG].d.mag.temp_c);
    else
        APPEND(",,,,");

    if (s_seen[SRC_RADAR])
        APPEND("%ld,%ld,",
               (long)s_latest[SRC_RADAR].d.radar.presence,
               (long)s_latest[SRC_RADAR].d.radar.range_bin);
    else
        APPEND(",,");

    if (s_seen[SRC_MIC])
        APPEND("%.1f,%d,", s_latest[SRC_MIC].d.mic.rms, s_latest[SRC_MIC].d.mic.peak);
    else
        APPEND(",,");

    char label[LOGGER_LABEL_MAX];
    taskENTER_CRITICAL();
    memcpy(label, s_label, sizeof(label));
    taskEXIT_CRITICAL();
    APPEND("%s", label);
}
#undef APPEND

static void logger_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        log_sample_t s;
        if (xQueueReceive(s_queue, &s, pdMS_TO_TICKS(QUEUE_WAIT_MS)) != pdTRUE)
        {
            continue;                  /* idle -- nothing arrived */
        }

        s_latest[s.src] = s;           /* update cache */
        s_seen[s.src]   = true;

        if (!s_enabled)
        {
            continue;                  /* recording stopped: swallow silently */
        }

        if (s_mode == LOG_MODE_HUMAN)
        {
            print_human(&s);           /* human mode: print each as it arrives */
        }
        else if (s.src == SRC_IMU)
        {
            /* CSV mode: the IMU is the cadence master. One row/record per
             * IMU sample = one feature vector at exactly the IMU rate,
             * fanned out to whichever sink(s) are active. UART and TCP
             * share one built row (build_csv_row) so the two streams can
             * never drift from each other. */
            if (s_sink & (LOGGER_SINK_UART | LOGGER_SINK_TCP))
            {
                if (s_header_needed)
                {
                    if (s_sink & LOGGER_SINK_UART) printf("%s", CSV_HEADER);
                    if (s_sink & LOGGER_SINK_TCP)   wifi_tcp_send(CSV_HEADER, sizeof(CSV_HEADER) - 1);
                    s_header_needed = false;
                }

                char row[256];
                build_csv_row(row, sizeof(row), s.ts_ms);

                if (s_sink & LOGGER_SINK_UART)
                {
                    printf("%s\r\n", row);
                }
                if (s_sink & LOGGER_SINK_TCP)
                {
                    char line[sizeof(row) + 2];
                    int m = snprintf(line, sizeof(line), "%s\r\n", row);
                    if (m > 0) wifi_tcp_send(line, (size_t)m);
                }
            }
            if (s_sink & LOGGER_SINK_FLASH)
            {
                logger_flash_enqueue_sample(s_latest, s_seen, s.ts_ms, s_label);
            }
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
