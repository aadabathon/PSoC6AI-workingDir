#ifndef LOGGER_CLI_H
#define LOGGER_CLI_H

/* ============================================================================
 * logger_cli.h -- tiny serial console for driving the data logger.
 *
 * Replaces the stock radar CLI in the data-collection build. Commands:
 *   start          begin emitting (reprints the CSV header)
 *   stop           stop emitting
 *   mode human     debug prints, one line per sample
 *   mode csv       merged rows for DeepCraft capture
 *   label <name>   set the activity label column (e.g. `label walking`)
 *   sink uart      log to UART only (default)
 *   sink flash     log to onboard QSPI flash only
 *   sink tcp       log to the connected Wi-Fi TCP client only
 *   sink both      log to UART and flash
 *   sink all       log to UART, flash, and TCP
 *   dump           replay all flash-stored sessions as CSV over UART
 *   erase confirm  wipe the flash ring buffer (irreversible)
 *   wifi status    show Wi-Fi join / TCP client state
 *   status         show mode / enabled / sink / flash usage
 *   help           this list
 * ============================================================================ */

#include "cy_result.h"

cy_rslt_t logger_cli_init(void);   /* create the console task */

#endif
