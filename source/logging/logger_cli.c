/* ============================================================================
 * logger_cli.c -- tiny serial console for the data logger.
 *
 * Same input mechanism as the stock radar CLI (blocking getchar() through
 * retarget-io) and the same idle priority, so a busy UART wait never starves
 * the sensor or logger tasks. No FreeRTOS_CLI dependency -- a hand-rolled
 * parser is 60 lines and this only has six commands.
 * ============================================================================ */

#include "logger_cli.h"
#include "logger.h"
#include "logger_flash.h"
#include "wifi_task.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define CLI_TASK_STACK_WORDS   (2048U)    /* printf + line buffer */
#define CLI_TASK_PRIORITY      (tskIDLE_PRIORITY)
#define CLI_LINE_MAX           (48U)

static TaskHandle_t s_task = NULL;

static void print_help(void)
{
    printf("\r\ncommands:\r\n"
           "  start          begin logging (CSV header reprints)\r\n"
           "  stop           stop logging\r\n"
           "  mode human     per-sample debug lines\r\n"
           "  mode csv       merged rows for DeepCraft\r\n"
           "  label <name>   set activity label (max %u chars)\r\n"
           "  sink uart      log to UART only (default)\r\n"
           "  sink flash     log to onboard QSPI flash only\r\n"
           "  sink tcp       log to the connected Wi-Fi TCP client only\r\n"
           "  sink both      log to UART and flash\r\n"
           "  sink all       log to UART, flash, and TCP\r\n"
           "  dump           replay all flash-stored sessions as CSV over UART\r\n"
           "  erase confirm  wipe the flash ring buffer (irreversible)\r\n"
           "  wifi status    show Wi-Fi join / TCP client state\r\n"
           "  status         show current state\r\n"
           "  help           this list\r\n",
           (unsigned)(LOGGER_LABEL_MAX - 1));
}

static void handle_line(char *line)
{
    /* strip leading spaces */
    while (*line == ' ') line++;
    if (*line == '\0') return;

    if (strcmp(line, "start") == 0)
    {
        logger_set_enabled(true);
        printf("[cli] logging started\r\n");
    }
    else if (strcmp(line, "stop") == 0)
    {
        logger_set_enabled(false);
        printf("[cli] logging stopped\r\n");
    }
    else if (strcmp(line, "mode human") == 0)
    {
        logger_set_mode(LOG_MODE_HUMAN);
        printf("[cli] mode = human\r\n");
    }
    else if (strcmp(line, "mode csv") == 0)
    {
        logger_set_mode(LOG_MODE_CSV);
        printf("[cli] mode = csv\r\n");
    }
    else if (strncmp(line, "label ", 6) == 0 && line[6] != '\0')
    {
        logger_set_label(&line[6]);
        printf("[cli] label = %s\r\n", &line[6]);
    }
    else if (strcmp(line, "sink uart") == 0)
    {
        logger_set_sink(LOGGER_SINK_UART);
        printf("[cli] sink = uart\r\n");
    }
    else if (strcmp(line, "sink flash") == 0)
    {
        logger_set_sink(LOGGER_SINK_FLASH);
        printf("[cli] sink = flash\r\n");
    }
    else if (strcmp(line, "sink tcp") == 0)
    {
        logger_set_sink(LOGGER_SINK_TCP);
        printf("[cli] sink = tcp\r\n");
    }
    else if (strcmp(line, "sink both") == 0)
    {
        logger_set_sink(LOGGER_SINK_UART | LOGGER_SINK_FLASH);
        printf("[cli] sink = both\r\n");
    }
    else if (strcmp(line, "sink all") == 0)
    {
        logger_set_sink(LOGGER_SINK_UART | LOGGER_SINK_FLASH | LOGGER_SINK_TCP);
        printf("[cli] sink = all\r\n");
    }
    else if (strcmp(line, "wifi status") == 0)
    {
        char ip[20];
        wifi_task_get_ip_str(ip, sizeof(ip));
        printf("[cli] wifi joined=%d ip=%s client_connected=%d\r\n",
               (int)wifi_task_is_joined(), ip, (int)wifi_task_is_client_connected());
    }
    else if (strcmp(line, "dump") == 0)
    {
        logger_flash_dump_as_csv();
    }
    else if (strcmp(line, "erase confirm") == 0)
    {
        logger_flash_erase_all();
    }
    else if (strcmp(line, "erase") == 0)
    {
        printf("[cli] this wipes all flash-stored data. "
               "Type `erase confirm` to proceed.\r\n");
    }
    else if (strcmp(line, "status") == 0)
    {
        uint8_t sink = logger_get_sink();
        /* Bitmask -> string: builds e.g. "uart+flash+tcp" for any
         * combination, not just the four the CLI's own shortcuts produce
         * (a future `sink` variant, or a bug, shouldn't misreport here). */
        char sink_str[24] = "";
        if (sink & LOGGER_SINK_UART)  strcat(sink_str, "uart+");
        if (sink & LOGGER_SINK_FLASH) strcat(sink_str, "flash+");
        if (sink & LOGGER_SINK_TCP)   strcat(sink_str, "tcp+");
        size_t sink_len = strlen(sink_str);
        if (sink_len > 0) sink_str[sink_len - 1] = '\0';   /* trim trailing '+' */
        else strcpy(sink_str, "none");

        printf("[cli] mode=%s enabled=%d sink=%s flash_used=%lu/%lu halted=%d\r\n",
               (logger_get_mode() == LOG_MODE_CSV) ? "csv" : "human",
               (int)logger_get_enabled(), sink_str,
               (unsigned long)logger_flash_used_records(),
               (unsigned long)logger_flash_capacity_records(),
               (int)logger_flash_is_halted());
    }
    else if (strcmp(line, "help") == 0)
    {
        print_help();
    }
    else
    {
        printf("[cli] unknown command (try `help`)\r\n");
    }
}

static void logger_cli_task(void *arg)
{
    (void)arg;
    char   line[CLI_LINE_MAX];
    size_t len = 0;

    printf("type `help` for logger commands; `start` or USER BTN1 begins capture\r\n");

    for (;;)
    {
        int c = getchar();
        if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        if (c == '\r' || c == '\n')
        {
            putchar('\r'); putchar('\n');
            line[len] = '\0';
            handle_line(line);
            len = 0;
        }
        else if (c == '\b' || c == 0x7F)          /* backspace / DEL */
        {
            if (len > 0) { len--; printf("\b \b"); }
        }
        else if (len < CLI_LINE_MAX - 1 && c >= 0x20 && c < 0x7F)
        {
            line[len++] = (char)c;
            putchar(c);                            /* echo */
        }
        fflush(stdout);
    }
}

cy_rslt_t logger_cli_init(void)
{
    BaseType_t ok = xTaskCreate(logger_cli_task, "logcli",
                                CLI_TASK_STACK_WORDS, NULL,
                                CLI_TASK_PRIORITY, &s_task);
    return (ok == pdPASS) ? CY_RSLT_SUCCESS : CY_RSLT_TYPE_ERROR;
}
