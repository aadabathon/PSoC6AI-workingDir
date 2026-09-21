/* ============================================================================
 * logger_flash.c -- raw, self-framed ring buffer over the onboard 64MB QSPI
 * NOR flash (S25HS512T). No filesystem library is vendored in this project,
 * so this is deliberately simple: fixed-size binary records, a tiny
 * superblock for the write pointer, halt-when-full (never silently overwrite
 * unlabeled training data).
 *
 * Flash layout (see bsps/.../cycfg_qspi_memslot.c for the chip's hybrid
 * sector map this is built around):
 *   sector 0          addr 0x000000, 4KB   -- superblock (write ptr + bitmap)
 *   ring buffer        addr 0x040000..0x4000000, 255 x 256KB uniform sectors
 * The first 256KB of the chip (32x4KB + one 128KB sector) is skipped entirely
 * so the ring buffer never has to reason about more than one erase
 * granularity -- every ring sector is exactly 256KB.
 *
 * Records are fixed-size (see flash_record_t) so slot->address is a plain
 * multiply; a record never straddles a sector boundary (any slack bytes at
 * the end of a sector are simply left erased/unused).
 *
 * Erase cost: a 256KB sector erase on this chip is ~5.9 seconds. Erasing on
 * the same task that drains the logger queue would stall UART/CSV output for
 * that long roughly once a minute (2912 records/sector at 50 Hz). So all
 * flash I/O -- including that erase -- runs on its own low-priority task fed
 * by a deep queue, decoupled from logger_task entirely.
 * ============================================================================ */

#include "logger_flash.h"
#include "qspi_flash.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdio.h>
#include <string.h>

/* ---- Flash layout ---- */
#define SUPERBLOCK_ADDR         (0x000000UL)
#define SUPERBLOCK_ERASE_SIZE   (0x1000UL)          /* 4KB -- region0 sector size */
#define RING_BASE_ADDR          (0x040000UL)
#define RING_SECTOR_SIZE        (0x40000UL)         /* 256KB -- region2 sector size, uniform */
#define RING_SECTOR_COUNT       (255U)

#define SUPERBLOCK_MAGIC        (0x53464C31UL)      /* 'SFL1' */
#define REC_MAGIC_DATA          (0xA5D5U)
#define REC_MAGIC_SESSION       (0xA5C3U)
#define REC_VERSION             (1U)

#define FLASH_QUEUE_LEN         (320U)   /* covers a full 256KB/~5.9s erase at 50Hz + margin */
#define FLASH_TASK_STACK_WORDS  (1024U)
#define FLASH_TASK_PRIORITY     (tskIDLE_PRIORITY + 1)

/* ---- On-flash record: fixed size, magic-discriminated ---- */
typedef struct __attribute__((packed))
{
    uint16_t magic;
    uint8_t  version;
    uint8_t  seen_mask;      /* bit i = sample_src_t i was valid (data records only) */
    uint32_t ts_ms;
    float    baro_pa, baro_t;
    float    ax, ay, az, gx, gy, gz, imu_t;
    float    mx, my, mz, mag_t;
    int32_t  radar_present, radar_bin;
    float    mic_rms;
    int16_t  mic_peak;
    char     label[LOGGER_LABEL_MAX];
} flash_record_t;

#define RECORD_SIZE          (sizeof(flash_record_t))
#define RECORDS_PER_SECTOR   (RING_SECTOR_SIZE / RECORD_SIZE)
#define TOTAL_RECORDS        ((size_t)RECORDS_PER_SECTOR * RING_SECTOR_COUNT)
#define BITMAP_BYTES         ((RING_SECTOR_COUNT + 7U) / 8U)

/* ---- Superblock: write pointer + per-generation erase bitmap ---- */
typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint32_t next_slot;
    uint8_t  halted;
    uint8_t  reserved[3];
    uint8_t  erased_bitmap[BITMAP_BYTES];   /* bit i = ring sector i erased this generation */
} flash_superblock_t;

/* ---- Queue item handed from logger_task to the flash-writer task ---- */
typedef struct
{
    bool           is_session_start;
    flash_record_t rec;
} flash_queue_item_t;

static flash_superblock_t s_super;
static QueueHandle_t      s_flash_queue = NULL;
static TaskHandle_t       s_flash_task  = NULL;
static bool                s_inited     = false;

static bool bitmap_get(const uint8_t *bm, uint32_t i) { return (bm[i / 8U] >> (i % 8U)) & 1U; }
static void bitmap_set(uint8_t *bm, uint32_t i)        { bm[i / 8U] |= (uint8_t)(1U << (i % 8U)); }

static void persist_superblock(void)
{
    if (qspi_flash_erase(SUPERBLOCK_ADDR, SUPERBLOCK_ERASE_SIZE) != CY_RSLT_SUCCESS)
    {
        printf("[flash] ERROR: superblock erase failed\r\n");
        return;
    }
    if (qspi_flash_write(SUPERBLOCK_ADDR, &s_super, sizeof(s_super)) != CY_RSLT_SUCCESS)
    {
        printf("[flash] ERROR: superblock write failed\r\n");
    }
}

static void halt_and_warn(void)
{
    if (!s_super.halted)
    {
        s_super.halted = 1;
        persist_superblock();
        printf("[flash] ring buffer full (%lu records) -- halted. "
               "`erase confirm` to reuse.\r\n", (unsigned long)TOTAL_RECORDS);
    }
}

static void write_record(const flash_record_t *rec)
{
    if (s_super.halted) return;

    uint32_t slot = s_super.next_slot;
    if (slot >= (uint32_t)TOTAL_RECORDS)
    {
        halt_and_warn();
        return;
    }

    uint32_t sector_idx = slot / (uint32_t)RECORDS_PER_SECTOR;
    uint32_t addr        = RING_BASE_ADDR + slot * (uint32_t)RECORD_SIZE;

    if (!bitmap_get(s_super.erased_bitmap, sector_idx))
    {
        uint32_t sector_addr = RING_BASE_ADDR + sector_idx * RING_SECTOR_SIZE;
        if (qspi_flash_erase(sector_addr, RING_SECTOR_SIZE) != CY_RSLT_SUCCESS)
        {
            printf("[flash] ERROR: erase failed at sector %lu -- record dropped\r\n",
                   (unsigned long)sector_idx);
            return;
        }
        bitmap_set(s_super.erased_bitmap, sector_idx);
        persist_superblock();   /* checkpoint here -- the erase already cost ~5.9s */
    }

    if (qspi_flash_write(addr, rec, RECORD_SIZE) != CY_RSLT_SUCCESS)
    {
        printf("[flash] ERROR: write failed at slot %lu\r\n", (unsigned long)slot);
        return;
    }

    s_super.next_slot = slot + 1U;
    /* next_slot itself is NOT persisted per-record (a 4KB erase+write would
     * cap throughput far below 50Hz) -- only at sector boundaries (above),
     * on session start, and on erase_all. Worst case on power loss: replay
     * a few already-written-but-unpersisted tail records get overwritten on
     * the next boot -- bounded to under one sector (<~58s of samples). */
}

static void flash_writer_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        flash_queue_item_t item;
        if (xQueueReceive(s_flash_queue, &item, portMAX_DELAY) == pdTRUE)
        {
            write_record(&item.rec);
            if (item.is_session_start)
            {
                persist_superblock();   /* durable session boundary */
            }
        }
    }
}

void logger_flash_enqueue_sample(const log_sample_t latest[5], const bool seen[5],
                                  uint32_t ts_ms, const char *label)
{
    if (!s_inited || s_super.halted) return;

    flash_queue_item_t item;
    item.is_session_start = false;
    memset(&item.rec, 0, sizeof(item.rec));
    item.rec.magic   = REC_MAGIC_DATA;
    item.rec.version = REC_VERSION;
    item.rec.ts_ms   = ts_ms;

    uint8_t mask = 0;
    if (seen[SRC_BARO])  { mask |= (1U << SRC_BARO);  item.rec.baro_pa = latest[SRC_BARO].d.baro.pa;
                                                        item.rec.baro_t  = latest[SRC_BARO].d.baro.temp_c; }
    if (seen[SRC_IMU])
    {
        mask |= (1U << SRC_IMU);
        item.rec.ax = latest[SRC_IMU].d.imu.ax; item.rec.ay = latest[SRC_IMU].d.imu.ay;
        item.rec.az = latest[SRC_IMU].d.imu.az; item.rec.gx = latest[SRC_IMU].d.imu.gx;
        item.rec.gy = latest[SRC_IMU].d.imu.gy; item.rec.gz = latest[SRC_IMU].d.imu.gz;
        item.rec.imu_t = latest[SRC_IMU].d.imu.temp_c;
    }
    if (seen[SRC_MAG])
    {
        mask |= (1U << SRC_MAG);
        item.rec.mx = latest[SRC_MAG].d.mag.mx; item.rec.my = latest[SRC_MAG].d.mag.my;
        item.rec.mz = latest[SRC_MAG].d.mag.mz; item.rec.mag_t = latest[SRC_MAG].d.mag.temp_c;
    }
    if (seen[SRC_RADAR])
    {
        mask |= (1U << SRC_RADAR);
        item.rec.radar_present = latest[SRC_RADAR].d.radar.presence;
        item.rec.radar_bin     = latest[SRC_RADAR].d.radar.range_bin;
    }
    if (seen[SRC_MIC])
    {
        mask |= (1U << SRC_MIC);
        item.rec.mic_rms  = latest[SRC_MIC].d.mic.rms;
        item.rec.mic_peak = latest[SRC_MIC].d.mic.peak;
    }
    item.rec.seen_mask = mask;
    strncpy(item.rec.label, label, LOGGER_LABEL_MAX - 1);

    (void)xQueueSend(s_flash_queue, &item, 0);   /* drop-on-full: RTOS-queue overflow only,
                                                    * not the same as the flash being full */
}

void logger_flash_enqueue_session_start(uint32_t ts_ms, const char *label)
{
    if (!s_inited || s_super.halted) return;

    flash_queue_item_t item;
    item.is_session_start = true;
    memset(&item.rec, 0, sizeof(item.rec));
    item.rec.magic   = REC_MAGIC_SESSION;
    item.rec.version = REC_VERSION;
    item.rec.ts_ms   = ts_ms;
    strncpy(item.rec.label, label, LOGGER_LABEL_MAX - 1);

    (void)xQueueSend(s_flash_queue, &item, 0);
}

static void print_csv_header(void)
{
    printf("ts_ms,baro_pa,baro_t,ax,ay,az,gx,gy,gz,imu_t,"
           "mx,my,mz,mag_t,radar_present,radar_bin,mic_rms,mic_peak,label\r\n");
}

static void print_data_row(const flash_record_t *r)
{
    char row[256];
    int  n = 0;

#define APPEND(...)                                                      \
    do {                                                                 \
        n += snprintf(row + n, sizeof(row) - (size_t)n, __VA_ARGS__);    \
        if (n >= (int)sizeof(row)) n = (int)sizeof(row) - 1;             \
    } while (0)

    APPEND("%lu,", (unsigned long)r->ts_ms);
    if (r->seen_mask & (1U << SRC_BARO))  APPEND("%.1f,%.1f,", r->baro_pa, r->baro_t);
    else                                   APPEND(",,");
    if (r->seen_mask & (1U << SRC_IMU))
        APPEND("%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.1f,",
               r->ax, r->ay, r->az, r->gx, r->gy, r->gz, r->imu_t);
    else APPEND(",,,,,,,");
    if (r->seen_mask & (1U << SRC_MAG)) APPEND("%.1f,%.1f,%.1f,%.1f,", r->mx, r->my, r->mz, r->mag_t);
    else                                 APPEND(",,,,");
    if (r->seen_mask & (1U << SRC_RADAR)) APPEND("%ld,%ld,", (long)r->radar_present, (long)r->radar_bin);
    else                                   APPEND(",,");
    if (r->seen_mask & (1U << SRC_MIC)) APPEND("%.1f,%d,", r->mic_rms, r->mic_peak);
    else                                 APPEND(",,");

    char label[LOGGER_LABEL_MAX];
    memcpy(label, r->label, sizeof(label));
    label[LOGGER_LABEL_MAX - 1] = '\0';
    APPEND("%s", label);
#undef APPEND

    printf("%s\r\n", row);
}

void logger_flash_dump_as_csv(void)
{
    if (!s_inited)
    {
        printf("[flash] not initialized\r\n");
        return;
    }

    printf("[flash] dumping %lu record(s)...\r\n", (unsigned long)s_super.next_slot);
    print_csv_header();

    flash_record_t rec;
    for (uint32_t slot = 0; slot < s_super.next_slot; slot++)
    {
        uint32_t addr = RING_BASE_ADDR + slot * (uint32_t)RECORD_SIZE;
        if (qspi_flash_read(addr, &rec, RECORD_SIZE) != CY_RSLT_SUCCESS)
        {
            printf("[flash] ERROR: read failed at slot %lu, stopping\r\n", (unsigned long)slot);
            break;
        }
        if (rec.magic == REC_MAGIC_DATA)
        {
            print_data_row(&rec);
        }
        /* session-start records carry no CSV row of their own -- each data
         * record already embeds its own label, so boundaries are implicit. */
    }
    printf("[flash] dump complete\r\n");
}

void logger_flash_erase_all(void)
{
    if (!s_inited) return;

    if (s_flash_queue != NULL)
    {
        xQueueReset(s_flash_queue);   /* discard anything from the previous generation */
    }

    memset(&s_super, 0, sizeof(s_super));
    s_super.magic = SUPERBLOCK_MAGIC;
    persist_superblock();

    printf("[flash] erased -- ring buffer reset (%lu records capacity)\r\n",
           (unsigned long)TOTAL_RECORDS);
}

bool logger_flash_is_halted(void)
{
    return s_inited && s_super.halted;
}

size_t logger_flash_capacity_records(void) { return TOTAL_RECORDS; }
size_t logger_flash_used_records(void)     { return s_inited ? s_super.next_slot : 0; }

cy_rslt_t logger_flash_init(void)
{
    cy_rslt_t rslt = qspi_flash_init();
    if (rslt != CY_RSLT_SUCCESS) return rslt;

    if (qspi_flash_read(SUPERBLOCK_ADDR, &s_super, sizeof(s_super)) != CY_RSLT_SUCCESS)
    {
        return CY_RSLT_TYPE_ERROR;
    }

    if (s_super.magic != SUPERBLOCK_MAGIC)
    {
        /* virgin or corrupted superblock -- start a fresh generation */
        memset(&s_super, 0, sizeof(s_super));
        s_super.magic = SUPERBLOCK_MAGIC;
        persist_superblock();
    }

    s_flash_queue = xQueueCreate(FLASH_QUEUE_LEN, sizeof(flash_queue_item_t));
    if (s_flash_queue == NULL) return CY_RSLT_TYPE_ERROR;

    BaseType_t ok = xTaskCreate(flash_writer_task, "flashlog",
                                FLASH_TASK_STACK_WORDS, NULL,
                                FLASH_TASK_PRIORITY, &s_flash_task);
    if (ok != pdPASS) return CY_RSLT_TYPE_ERROR;

    s_inited = true;
    printf("[flash] ring buffer ready: %lu records capacity (%lu used, halted=%d)\r\n",
           (unsigned long)TOTAL_RECORDS, (unsigned long)s_super.next_slot, (int)s_super.halted);
    return CY_RSLT_SUCCESS;
}
