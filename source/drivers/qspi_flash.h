#ifndef QSPI_FLASH_H
#define QSPI_FLASH_H

/* ============================================================================
 * qspi_flash.h -- driver for the 512Mb (64MB) external QSPI NOR flash.
 *
 * NOR flash semantics (the one gotcha): you can READ any byte freely, but
 * WRITES can only flip bits 1->0. To set bits back to 1 you must ERASE,
 * which works on whole sectors (typ. 4KB) at a time and sets them to all-0xFF.
 * So the write pattern is always: erase sector -> program bytes.
 *
 * The flash chip's command set / timing is described by the Device
 * Configurator in cycfg_qspi_memslot.c. We just point the serial-flash
 * library at that and call read/erase/write.
 *
 * Pins (BSP): D0-D3 P11_6..P11_3, SCK P11_7, SS P11_2.
 * ============================================================================ */

#include "cy_result.h"
#include <stdint.h>
#include <stddef.h>

cy_rslt_t qspi_flash_init(void);
cy_rslt_t qspi_flash_read (uint32_t addr, void *buf, size_t len);
cy_rslt_t qspi_flash_erase(uint32_t addr, size_t len);   /* len rounded to sectors */
cy_rslt_t qspi_flash_write(uint32_t addr, const void *buf, size_t len);

size_t    qspi_flash_total_size(void);   /* bytes */
size_t    qspi_flash_sector_size(void);  /* erase granularity */

#endif