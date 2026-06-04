#ifndef BMM350_GLUE_H
#define BMM350_GLUE_H

/* ============================================================================
 * bmm350_glue.h -- adapter between our i2c_bus + FreeRTOS and Bosch's
 * BMM350 SensorAPI.
 *
 * The Bosch lib is platform-agnostic: it expects you to plug in three
 * function pointers (read/write/delay) on a struct bmm350_dev, then it
 * does the rest. This file provides those plug-ins.
 *
 * Pattern shows up EVERYWHERE in embedded -- learn it once, recognize it
 * forever. Same shape in Zephyr drivers, ESP-IDF, ST HAL, Nordic SDK.
 * ============================================================================ */

#include "cy_result.h"
#include "bmm350.h"          /* from bmm350_lib/ */

/* Fills out the function pointers + intf_ptr inside dev. Doesn't touch
 * the chip -- you still need to call bmm350_init(&dev) afterward. */
cy_rslt_t bmm350_glue_attach(struct bmm350_dev *dev);

#endif