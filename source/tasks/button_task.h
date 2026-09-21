#ifndef BUTTON_TASK_H
#define BUTTON_TASK_H

/* ============================================================================
 * button_task.h -- hands-free recording control.
 *
 * USER BTN1 toggles logging start/stop (same gate the CLI `start`/`stop`
 * commands use), so you can capture activity data away from the keyboard.
 * USER LED1 mirrors the recording state: on = logging, off = idle.
 * ============================================================================ */

#include "cy_result.h"

cy_rslt_t button_task_init(void);

#endif
