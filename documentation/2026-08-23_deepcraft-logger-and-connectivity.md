# DeepCraft Logger & Connectivity — Session Changelog

**Date:** 2026-08-23
**Board:** CY8CKIT-062S2-AI (PSoC 6 + CYW43439 Wi-Fi/BT)
**Scope:** Finished the pivot from the stock XENSIV radar-presence demo to a
full DeepCraft data-collection rig — wired the sensor stack into the logger,
added recording controls, and implemented the two remaining peripherals
(QSPI flash logging, Wi-Fi TCP streaming) to make every peripheral on the
board functional.

This file documents everything changed in this session, in the order it
happened, so a future session (or a person) can see what exists, why, and
what's still untested.

---

## 1. Logger integration (all sensors -> single pipeline)

**Problem going in:** only `imu_task` posted samples to the logger
(`source/logging/logger.c`); `barometer_task`, `mag_task`, and `mic_task`
printed directly to UART instead, which (a) meant their CSV columns were
always zero and (b) risked tearing CSV rows mid-line since two things were
writing to the same UART.

**Changes:**
- `source/tasks/barometer_task.c`, `mag_task.c`, `mic_task.c` — replaced
  direct `printf()` calls with `logger_post()` calls carrying a filled
  `log_sample_t`. The logger is now the sole owner of the UART.
- `source/tasks/imu_task.c` — sample rate changed from 200 Hz to **50 Hz**
  (20 ms poll of the chip's 200 Hz ODR). The IMU is the CSV cadence master:
  one merged row is emitted per IMU sample. 50 Hz was chosen because (a)
  it's the common DeepCraft activity-classification rate and (b) a full
  merged CSV row (~150 chars) at 50 Hz fits 115200 baud with headroom;
  200 Hz would not.
- `source/logging/logger.c` / `logger.h` — rewritten:
  - Row assembly moved from "the logger prints whatever arrives" to "the
    logger emits one merged row per IMU sample, using the latest cached
    value of every other sensor." Sensors that haven't reported yet emit
    **empty CSV fields** (pandas reads these as NaN), not fake zeros.
  - Added a recording gate: nothing is emitted until
    `logger_set_enabled(true)`. The board boots silent.
  - Added an activity **label** (`logger_set_label()`), appended as the
    last CSV column, settable at collection time instead of hand-aligned
    after the fact.
  - CSV row building is bounds-clamped (`APPEND` macro) so a pathological
    float value can't overflow the row buffer.
  - The CSV header reprints at the start of every recording session, not
    just once at boot.

## 2. Recording controls

**Added `source/logging/logger_cli.c` / `logger_cli.h`** — a small
hand-rolled serial console (no `FreeRTOS_CLI` dependency) replacing the
stock radar CLI in the data-collection build. Original command set:
`start`, `stop`, `mode human|csv`, `label <name>`, `status`, `help`.
(Extended further in later sections below.)

**Added `source/tasks/button_task.c` / `button_task.h`** — USER BTN1
toggles the recording gate (debounced, ISR + polling LED task); USER LED1
mirrors recording state (on = logging). Lets you start/stop a capture
session without a laptop attached.

## 3. Radar demo preserved behind a build flag

The stock XENSIV radar-presence demo (radar SPI driver, presence library,
original radar CLI) was previously disabled with scattered `#if 0` blocks.
Replaced with a single `ENABLE_RADAR` preprocessor guard:

- `Makefile` — added a commented `#DEFINES+=ENABLE_RADAR` line. Uncomment
  to rebuild the radar demo.
- `source/main.c` — all radar-only code (`processing_task`, `init_sensor`,
  `init_leds`, `presence_detection_cb`, the radar SPI globals, etc.) now
  lives behind `#ifdef ENABLE_RADAR` / `#endif` instead of `#if 0`. In the
  `ENABLE_RADAR` build the logger boots enabled in human mode (since that
  build's console owns stdin, not `logger_cli`), and `button_task` /
  `logger_cli` are not started (the stock radar console owns stdin/LEDs
  there instead).
- `presence_detection_cb` in `main.c` now posts a `SRC_RADAR` sample to the
  logger instead of a direct `printf`, so radar events (when that build is
  active) flow through the same pipeline as every other sensor.

Both configurations (`ENABLE_RADAR` on and off) are verified to build
clean with zero errors.

## 4. QSPI flash logging (third output sink)

**New files:** `source/logging/logger_flash.c`, `logger_flash.h`

The board's 64MB QSPI NOR flash (S25HS512T) was previously only used for a
disabled round-trip self-test in `main.c`. It's now a real logging sink:

- **Format:** packed binary, not text CSV, to fit more samples per sector.
  Fixed-size records (magic-tagged: data record vs. session-start record)
  carry the same fields as a CSV row plus the label.
- **Layout:** the ring buffer lives in the flash's uniform 256KB-sector
  region (`0x40000`–`0x4000000`, avoiding the chip's smaller/irregular
  sectors near the bottom). Sector 0 (4KB) holds a superblock: the current
  write offset and an erase-generation bitmap, so the write pointer
  survives a reset mid-session. The superblock is persisted at sector
  boundaries (~every 58s) and on session start/erase, not per-record —
  per-record persistence would cap throughput far below 50 Hz. Worst-case
  data loss on power failure is bounded to under one sector (<~58s of
  unpersisted tail records).
- **Capacity:** 2,912 records/sector x 255 sectors = **742,560 records ≈
  4.1 hours** of continuous 50 Hz IMU-cadence logging (63.7MB of the 64MB
  chip used).
- **Policy:** halts (does not wrap/overwrite) when full. Silently losing
  unlabeled training data by overwriting it was judged worse than making
  the user notice and clear space.
- **Concurrency:** flash writes run on their own dedicated low-priority
  task with its own 320-deep queue, fully decoupled from the UART/CSV
  path. Reason: a 256KB sector erase on this chip takes ~5.9 seconds — if
  that ran inline in the logger task it would stall the sample queue and
  drop ~295 samples' worth of data roughly once a minute.
- `source/main.c` — the old `#if 0`'d QSPI round-trip test was replaced
  with an unconditional `logger_flash_init()` call in the sensor bring-up
  sequence.

**Sink abstraction** (`logger.h`/`logger.c`): the logger's output is now a
bitmask (`LOGGER_SINK_UART`, `LOGGER_SINK_FLASH`, added `LOGGER_SINK_TCP`
in the next section), defaulting to UART-only so existing behavior is
unaffected unless a sink is explicitly opted into via the CLI.

**New CLI commands** (`logger_cli.c`): `sink uart|flash|both`, `dump`
(replay all flash-stored sessions as CSV text over UART, reconstructed
from the binary records — same column order as the live CSV), `erase
confirm` (wipes the ring buffer; bare `erase` warns and requires the exact
confirmation string, since this is destructive and irreversible).
`status` extended to show sink, flash usage, and halted state.

## 5. Wi-Fi TCP streaming (second/network output path)

**New files:** `source/tasks/wifi_task.c`, `wifi_task.h`,
`source/tasks/wifi_config.h`, `analysis/collect_tcp.py`

**New dependency:** `deps/wifi-core-freertos-lwip-mbedtls.mtb` — a single
umbrella `.mtb` entry (`mtb://wifi-core-freertos-lwip-mbedtls#latest-v1.X`)
copied from Infineon's own working reference example
(`Infineon/mtb-example-wifi-tcp-server`) rather than hand-assembled. It
transitively pulls in the Wi-Fi Host Driver (WHD), lwIP, mbedTLS,
secure-sockets, and Wi-Fi Connection Manager (WCM) via `make getlibs`.

The board's BSP (`bsps/TARGET_APP_CY8CKIT-062S2-AI/bsp.mk`) already
declared the CYW43439 chip (`ADDITIONAL_DEVICES:=CYW43439KUBG`,
`BSP_COMPONENTS:=... WIFI_INTERFACE_SDIO`) before this session — no BSP
pin/chip work was needed, only the library and application code.

**Makefile changes:** `COMPONENTS+=LWIP MBEDTLS SECURE_SOCKETS`,
`DEFINES+=CYBSP_WIFI_CAPABLE`, and
`MBEDTLSFLAGS=MBEDTLS_USER_CONFIG_FILE='"mbedtls_user_config.h"'` (mirrors
the reference example's two-step pattern; inlining the quoted string
directly into `DEFINES+=` is quoting-fragile). Deliberately did **not**
add `CY_WIFI_HOST_WAKE_SW_FORCE=0` — that's a workaround in the reference
example for a *different* board where the user button and the Wi-Fi
host-wake pin share a GPIO. Verified via `cycfg_pins.h` that on this board
`CYBSP_USER_BTN1` (P5_2) and `CYBSP_WIFI_HOST_WAKE` (P4_1) are different
pins, so the workaround doesn't apply here.

**Architecture:** deliberately one-directional (push-only), not the
reference example's bidirectional command/response pattern:
- `wifi_task_init()` joins the AP configured in `wifi_config.h` (bounded
  retries with backoff), then opens a listening TCP socket on **port
  50007**, accepting one client via a WCM/secure-sockets connect-request
  callback. Connection state and the client socket handle are stored
  behind a small critical section, matching this codebase's existing
  `i2c_bus.c` mutex-guarding style.
- `source/tasks/wifi_config.h` holds `WIFI_SSID` / `WIFI_PASSWORD` /
  `WIFI_SECURITY_TYPE` as clearly-marked placeholders
  (`WIFI_SSID = "CHANGE_ME_SSID"`). **Must be edited before flashing** for
  Wi-Fi to actually join a network. If left as the placeholder,
  `wifi_task_init()` skips the join attempt entirely (no retry loop, no
  boot delay) and prints a one-line notice pointing back at this file.
- The logger's row-building was factored into a shared `build_csv_row()` /
  `CSV_HEADER` so UART and TCP send byte-identical CSV text. When
  `LOGGER_SINK_TCP` is active and a client is connected, each row is also
  pushed via a `wifi_tcp_send()` wrapper (logger.c has no direct knowledge
  of socket APIs, matching how it already doesn't know about the flash
  sink's binary format). Sends are best-effort and non-blocking-safe: if
  no client is connected or a send fails, the row is dropped — the logger
  task never blocks on a slow or absent network client.
- There is **no back-channel** on the data socket. `label`, `start`, and
  `sink` must be set via the serial CLI before a network capture begins.

**CLI extended:** `sink` now accepts `uart|flash|tcp|both|all`; `status`'s
sink report is a proper bitmask-to-string builder (handles any
combination, not just the four named shortcuts). Added `wifi status`
(shows joined state, IP, connected-client state).

**`analysis/collect_tcp.py`** — network counterpart to
`analysis/collect.py`. Same `--label`/`--duration`/`--outdir` shape and
CSV/label-validation logic, but connects via `--host`/`--port` (TCP
socket) instead of a serial port. Kept as a separate small script rather
than sharing a module with `collect.py`, matching this project's existing
single-file Python tooling style.

**Typical workflow once `wifi_config.h` is edited and the board is
flashed:**
```
(serial CLI)  sink tcp        # or `sink all` to also keep UART/flash going
(serial CLI)  label walking
(serial CLI)  start
(host)        python collect_tcp.py --host <board-ip> --label walking
```
(Board IP is shown by `wifi status` on the serial CLI.)

## 6. Cleanup

- Deleted `source/tasks/rtos_events.h` — an unused leftover from an
  unrelated ECE353 class project; nothing in this codebase included it.

## 7. A mistake caught and fixed during this session

A background implementation pass left `analysis/requirements.txt`
overwritten with a full `pip freeze` dump (dozens of transitive Jupyter
dependencies) encoded in **UTF-16LE** — almost certainly from a `pip
freeze > requirements.txt` run under PowerShell, whose `>` redirection
defaults to UTF-16LE. This clobbered the project's curated 7-line
dependency list (`pyserial`, `pandas`, `numpy`, `scipy`, `scikit-learn`,
`matplotlib`, `jupyter`), even though `collect_tcp.py` never introduced
any new third-party dependency (it's stdlib-only:
`socket`/`csv`/`argparse`/`re`/`sys`/`time`/`pathlib`/`datetime`). Restored
via `git checkout -- requirements.txt`; verified back to plain ASCII.

**Takeaway for future sessions:** if any tooling in this project shells
out through PowerShell with `>` redirection, watch for accidental UTF-16
encoding of files that should be plain ASCII/UTF-8 — `analysis/` has its
own separate git repo from the firmware (`workingDir`), so a `git status`
in the firmware repo will **not** show changes to `analysis/` files; check
both repos separately.

---

## Build verification

Both build configurations compiled with **zero errors** as of this
session's last build:

| Config | Flash used | Notes |
|---|---|---|
| Default (data-logger) | 751,792 / 2,097,152 bytes | Wi-Fi + flash logging both linked in |
| `ENABLE_RADAR` | 338,432 bytes | WHD/lwIP/mbedTLS linked but unreferenced there, so much of it is dead-code-stripped |

Verification commands used (from `workingDir`, with ModusToolbox tools on
`PATH`):
```
export CY_TOOLS_PATHS="C:/Users/adams/ModusToolbox/tools_3.8"
export PATH="/c/Users/adams/ModusToolbox/tools_3.8/modus-shell/bin:$PATH"
make getlibs        # only needed once, to fetch the new Wi-Fi library
make build -j8
```
To test the radar build: uncomment `DEFINES+=ENABLE_RADAR` in `Makefile`,
rebuild, then re-comment it afterward (do **not** pass
`DEFINES+=ENABLE_RADAR` on the `make` command line — that clobbers the
Makefile's other `DEFINES`, dropping `CY_RTOS_AWARE` etc.).

## What has NOT been verified

Nothing in this session was tested on physical hardware — only compiled.
Before relying on this for real data collection, at minimum:

1. Edit `source/tasks/wifi_config.h` with real AP credentials.
2. Flash the board and confirm the sensor stack brings up cleanly over
   serial (baro/mag/mic were previously disabled — first boot with all
   five producers live at once is the first real-world test of that).
3. Try `sink flash` -> log briefly -> `dump`, to confirm the flash
   round-trip actually reads back correct data before trusting it for a
   real capture session.
4. Try `sink tcp` with `collect_tcp.py` to confirm the network path
   end-to-end.
5. Watch `FreeRTOSConfig.h`'s `configTOTAL_HEAP_SIZE` (128KB) if
   `cy_wcm_init()` or socket setup fails at runtime with an allocation
   error — that's the first knob to raise; nothing in the build tooling
   catches heap exhaustion pre-flash.

## Files touched this session

**Added:**
- `source/logging/logger_cli.c`, `logger_cli.h`
- `source/logging/logger_flash.c`, `logger_flash.h`
- `source/tasks/button_task.c`, `button_task.h`
- `source/tasks/wifi_task.c`, `wifi_task.h`, `wifi_config.h`
- `deps/wifi-core-freertos-lwip-mbedtls.mtb`
- `analysis/collect_tcp.py`
- `documentation/2026-08-23_deepcraft-logger-and-connectivity.md` (this file)

**Modified:**
- `Makefile`
- `source/main.c`
- `source/logging/logger.c`, `logger.h`
- `source/tasks/tasks.h`, `barometer_task.c`, `imu_task.c`, `mag_task.c`,
  `mic_task.c`
- `analysis/collect.py` (CLI-driven start/stop/label protocol, 19-column
  CSV with label)
- `analysis/requirements.txt` (accidentally clobbered, then restored — see
  section 7)

**Deleted:**
- `source/tasks/rtos_events.h`
