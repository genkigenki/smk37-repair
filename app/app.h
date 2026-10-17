/* SPDX-License-Identifier: MPL-2.0
 * SMK-37 Repair: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* SMK-37 Repair: puts M-Vave's key mapping back on an SMK-37 whose mapping another firmware overwrote, and keeps the
 * unit's own calibration, Bluetooth pairing and wheel values through the stock firmware's rebuild (repair.h). The
 * app's half of the contract with the platform (base/device/unity.c, base/host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "SMK REPAIR"
#define APP_SPLASH_RGB RGB(64, 168, 220)
#define APP_TAGLINE "key mapping repair"
#ifndef APP_VERSION
#define APP_VERSION "0.1.1"                /* (tools/build.py --release passes its own) */
#endif
#define APP_BLOCK 256u
#define APP_CANVAS_PX (240u * 64u)
#define APP_BIG_FONT FONT_M
#define FM1B_UAC 0                       /* no USB audio: a MIDI port only */
#define FM1B_UAC_OUT 0
#define FM1B_PASSIVE_BOOT                /* nothing written at boot: the store is not this app's (it has none) */
#define APP_SYSEX                        /* the page's commands (app.c): F0 7D 52 45 50 op F7 */
int app_sysex(const uint8_t *p, uint32_t n);

/* where the RAM driver may erase and program, for this app: the platform's own windows, and the stash
 * (repair.h: 0xF0000-0xF3FFF, clear of every protected range: FL_PROTECTED still applies on top). Programming the
 * calibration and pairing back ([0xC0000, 0xC2000), protected) has its own routine in app.c, that range only. */
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || \
                             FL_IN(off, n, 0xF0000u, 0xF4000u))

/* the store: the panel calibration only (the platform's); never saved by this app */
enum { OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#ifdef PLAT_HOST
#define APP_HOST_EXPECT                  /* the host simulator's checks: state, flags */
int app_expect(const char *what, int *got);
#endif
