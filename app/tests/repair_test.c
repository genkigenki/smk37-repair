/* SPDX-License-Identifier: MPL-2.0
 * SMK-37 Repair: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* repair.h on a simulated NOR with the device's guards in front of it (app.h's FL_RANGE_OK: the store, the OTA area
 * and the stash window; program into [0xC0000, 0xC2000) only through prog_cal):
 *   - a made-up stock unit: STASH, the stock rebuild (its 64 KiB erases, defaults, the marker), RESTORE: BTIF,
 *     USRTRIM and the wheels as before, nothing else changed but the stash window (erased again) and the marker
 *   - a power cut after every single erase / program of STASH and of RESTORE, then the step run again: the same result
 *   - refusals: a second STASH, RESTORE before the rebuild, a foreign calibration, BTIF re-paired meanwhile
 *   - with REPAIR_ORIG / REPAIR_AFTER set: a real unit's dumps (before, after a rebuild); never committed
 * cc -O1 -Wall -Wextra -I../../base/bsp/boards/smk37 -I.. repair_test.c && ./a.out */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || \
                             FL_IN(off, n, 0xF0000u, 0xF4000u))
#include "fm1_flash_map.h"
#include "repair.h"

static uint8_t nor[0x100000], ref[0x100000];
static long ops, fail_after = -1, refused_calls;

static int cut(void) { return fail_after >= 0 && ops >= fail_after; }
static int n_read(void *c, uint32_t off, void *d, uint32_t n)
{
    (void)c;
    if (off >= sizeof nor || n > sizeof nor - off)
        return -1;
    memcpy(d, nor + off, n);
    return 0;
}
static int n_erase(void *c, uint32_t off)
{
    (void)c;
    if (!FL_ERASE_GUARD(off, 0x1000u) || (off & 0xFFFu)) {
        refused_calls++;
        return -8;
    }
    if (cut())
        return -9;
    ops++;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int n_prog_any(uint32_t off, const void *s, uint32_t n)
{
    uint32_t i;
    if (n == 0 || n > 256u || (off & 0xFFu) + n > 256u) {
        refused_calls++;
        return -8;
    }
    if (cut())
        return -9;
    ops++;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)s)[i];
    return 0;
}
static int n_prog(void *c, uint32_t off, const void *s, uint32_t n)
{
    (void)c;
    if (!FL_GUARD(off, n)) {
        refused_calls++;
        return -8;
    }
    return n_prog_any(off, s, n);
}
static int n_prog_cal(void *c, uint32_t off, const void *s, uint32_t n)
{
    (void)c;
    if (!FL_IN(off, n, 0xC0000u, 0xC2000u)) {
        refused_calls++;
        return -8;
    }
    return n_prog_any(off, s, n);
}
static int n_erase_cal(void *c, uint32_t off)
{
    (void)c;
    if (off != 0xC0000u && off != 0xC1000u) {
        refused_calls++;
        return -8;
    }
    if (cut())
        return -9;
    ops++;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static uint8_t sim_uid[8];                      /* the chip's unique ID (stock_unit: this unit's) */
static int n_uid(void *c, uint8_t id[8])
{
    (void)c;
    memcpy(id, sim_uid, 8);
    return 0;
}
static const rp_io IO = {0, n_read, n_erase, n_prog, n_prog_cal, n_erase_cal, n_uid, 0};

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int same(uint32_t lo, uint32_t hi, const uint8_t *a, const uint8_t *b) { return !memcmp(a + lo, b + hi - hi + lo, hi - lo); }

/* a made-up stock unit: pairing, calibration, globals with wheels, 8 presets, the marker, USR, key_mac */
static void stock_unit(void)
{
    uint32_t i, n;
    static const uint8_t UID[8] = {0x11, 0x20, 0x05, 0x02, 0x04, 0x01, 0x05, 0x21};
    memcpy(sim_uid, UID, 8);
    memset(nor, 0xFF, sizeof nor);
    for (i = 0; i < 0x4000u; i++)
        nor[i] = (uint8_t)(i * 7u + 1u);
    for (i = 0x4000u; i < 0x9D000u; i++)
        nor[i] = (uint8_t)(i >> 5);
    for (i = 0; i < 0xE10u; i++)
        nor[0xC0000u + i] = (uint8_t)(i * 13u);          /* BTIF */
    for (i = 0; i < 0x224u; i++)
        nor[0xC1000u + i] = (uint8_t)(i * 3u + 5u);      /* USRTRIM */
    memcpy(nor + 0xC1224u, "trimdat", 8);
    nor[0xC2000u] = 0xA3u;
    nor[0xC2001u] = 0x3Au;
    for (i = 2; i < 0x60u; i++)
        nor[0xC2000u + i] = (uint8_t)i;
    memcpy(nor + 0xC2060u, "\x91\x00\x8f\x00", 4);
    memcpy(nor + 0xC2080u, "\x50\x01\x48\x01\x00\x01", 6);
    for (n = 0; n < 8u; n++)
        for (i = 0; i < 0x29E3u; i++)
            nor[0xC3000u + n * 0x5000u + i] = (uint8_t)(i ^ n ^ 0x5Au);
    memcpy(nor + 0xEAFF8u, "usrdata", 8);
    for (i = 0xF4000u; i < 0xFE000u; i++)
        nor[i] = (uint8_t)(i >> 3);
    for (i = 0xFF000u; i < 0x100000u; i++)
        nor[i] = (uint8_t)(i * 11u);
}

/* what M-Vave's firmware does at its start without the marker (seen on a unit): 64 KiB erases at the presets
 * (rounded to the blocks 0xC0000, 0xD0000, 0xE0000), the globals written with defaults (wheels 0), the presets and
 * the marker */
static void stock_rebuild(void)
{
    uint32_t i, n;
    if (!memcmp(nor + 0xEAFF8u, "usrdata", 8))
        return;
    memset(nor + 0xC0000u, 0xFF, 0x30000u);
    nor[0xC2000u] = 0xA3u;
    nor[0xC2001u] = 0x3Au;
    for (i = 2; i < 0x90u; i++)
        nor[0xC2000u + i] = (uint8_t)(i < 0x60u ? i + 1u : 0u);
    for (n = 0; n < 8u; n++)
        for (i = 0; i < 0x29E3u; i++)
            nor[0xC3000u + n * 0x5000u + i] = (uint8_t)(i ^ n);
    memcpy(nor + 0xEAFF8u, "usrdata", 8);
}

/* what M-Vave's firmware does later, running on the rebuilt unit (seen on a unit): its own default calibration (a
 * valid "trimdat", 16 values 0x0032 from 0xC1008 every 10) and a new BTIF, written into the erased sectors */
static void stock_defaults(void)
{
    uint32_t i;
    for (i = 0; i < 0x224u; i++)
        nor[0xC1000u + i] &= 0x00u;
    for (i = 0; i < 16u; i++) {
        nor[0xC1008u + i * 10u] = 0x32u;
        nor[0xC1009u + i * 10u] = 0x00u;
    }
    memcpy(nor + 0xC1224u, "trimdat", 8);
    for (i = 0; i < 0x40u; i++)
        nor[0xC0000u + i] = (uint8_t)(0xA0u + i);
}

static void check_restored(const char *what)
{
    uint32_t i, d = 0;
    int f0 = fails;
    CHECK(!memcmp(nor + 0xC0000u, ref + 0xC0000u, 0x2000u));                 /* BTIF, USRTRIM */
    CHECK(!memcmp(nor + 0xC2060u, ref + 0xC2060u, 4) && !memcmp(nor + 0xC2080u, ref + 0xC2080u, 6));
    for (i = 0xF0000u; i < 0xF4000u; i++)
        d += nor[i] != 0xFFu;
    CHECK(d == 0);                                                            /* the stash gone */
    CHECK(!memcmp(nor, ref, 0xC0000u) && !memcmp(nor + 0xF4000u, ref + 0xF4000u, 0xC000u));
    if (fails != f0)
        printf("  (%s)\n", what);
}

static int restore_run(uint32_t *fl)
{
    int r;
    r = rp_do_restore(&IO, fl);
    return r;
}

static void test_basic(void)
{
    uint32_t fl;
    int r;
    stock_unit();
    memcpy(ref, nor, sizeof nor);
    CHECK(rp_state(&IO, &fl) == RP_IDLE && (fl & RP_F_CAL_NOW));
    CHECK(rp_do_restore(&IO, &fl) == RP_IDLE);                               /* nothing stashed: nothing done */
    CHECK(!memcmp(nor, ref, sizeof nor));
    r = rp_do_stash(&IO, &fl);
    CHECK(r == RP_STASHED && (fl & RP_F_STASH_CAL) && (fl & RP_F_WHEELS));
    CHECK(memcmp(nor + 0xEAFF8u, "usrdata", 8));                              /* released */
    CHECK(!memcmp(nor, ref, 0xEA000u) && !memcmp(nor + 0xF4000u, ref + 0xF4000u, 0xC000u));
    CHECK(rp_do_stash(&IO, &fl) == RP_STASHED);                              /* again: nothing new written */
    CHECK(!memcmp(nor, ref, 0xEA000u));
    CHECK(rp_state(&IO, &fl) == RP_WAIT_STOCK);
    CHECK(rp_do_restore(&IO, &fl) == RP_WAIT_STOCK);                         /* before the rebuild: nothing */
    stock_rebuild();
    CHECK(rp_state(&IO, &fl) == RP_READY && !(fl & RP_F_CAL_NOW));
    CHECK(rp_do_stash(&IO, &fl) == RP_REFUSED);                              /* rebuilt: never stashed over */
    r = restore_run(&fl);
    CHECK(r == RP_DONE && (fl & RP_F_CAL_NOW) && (fl & RP_F_WHEELS) && !(fl & (RP_F_BTIF_REFUSED | RP_F_CAL_REFUSED)));
    check_restored("basic");
    CHECK(rp_state(&IO, &fl) == RP_IDLE);
    CHECK(refused_calls == 0);
}

/* a power cut after k operations, the step run again from the start */
static void test_cuts(void)
{
    long k, total, n_stash;
    uint32_t fl;
    int r;
    stock_unit();
    memcpy(ref, nor, sizeof nor);
    ops = 0;
    rp_do_stash(&IO, &fl);
    total = n_stash = ops;
    for (k = 0; k < total; k++) {
        stock_unit();
        ops = 0;
        fail_after = k;
        r = rp_do_stash(&IO, &fl);
        CHECK(r == RP_ERR_FLASH);
        fail_after = -1;
        /* stock may start in between when the marker is already gone: the stash must then already be complete */
        if (memcmp(nor + 0xEAFF8u, "usrdata", 8))
            CHECK(rp_stash_get(&IO, 0) == 1);
        r = rp_do_stash(&IO, &fl);
        CHECK(r == RP_STASHED || r == RP_REFUSED);
        stock_rebuild();
        r = restore_run(&fl);
        CHECK(r == RP_DONE);
        check_restored("cut in stash");
    }
    stock_unit();
    rp_do_stash(&IO, &fl);
    stock_rebuild();
    {
        static uint8_t rebuilt[0x100000];
        memcpy(rebuilt, nor, sizeof nor);
        ops = 0;
        restore_run(&fl);
        total = ops;
        for (k = 0; k < total; k++) {
            memcpy(nor, rebuilt, sizeof nor);
            ops = 0;
            fail_after = k;
            r = restore_run(&fl);
            CHECK(r == RP_ERR_FLASH);
            fail_after = -1;
            r = restore_run(&fl);
            CHECK(r == RP_DONE);
            check_restored("cut in restore");
        }
    }
    printf("power cuts: after each of the %ld steps of the stash and the %ld of the restore, run again: ok\n",
           n_stash, total);
}

static void test_refusals(void)
{
    uint32_t fl;
    int r;
    /* M-Vave's own defaults written after the rebuild (calibration, BTIF): this unit's stash replaces them */
    stock_unit();
    memcpy(ref, nor, sizeof nor);
    rp_do_stash(&IO, &fl);
    stock_rebuild();
    stock_defaults();
    CHECK(rp_state(&IO, &fl) == RP_READY && (fl & RP_F_CAL_NOW));
    r = rp_do_restore(&IO, &fl);
    CHECK(r == RP_DONE && !(fl & (RP_F_BTIF_REFUSED | RP_F_CAL_REFUSED)));
    check_restored("defaults replaced");
    /* the same with a power cut after every step of that restore (the erases of 0xC0000 and 0xC1000 among them) */
    {
        static uint8_t rebuilt[0x100000];
        long k, total;
        stock_unit();
        rp_do_stash(&IO, &fl);
        stock_rebuild();
        stock_defaults();
        memcpy(rebuilt, nor, sizeof nor);
        ops = 0;
        rp_do_restore(&IO, &fl);
        total = ops;
        for (k = 0; k < total; k++) {
            memcpy(nor, rebuilt, sizeof nor);
            ops = 0;
            fail_after = k;
            r = rp_do_restore(&IO, &fl);
            CHECK(r == RP_ERR_FLASH);
            fail_after = -1;
            CHECK(rp_stash_get(&IO, 0) == 1 || memcmp(nor + 0xC0000u, ref + 0xC0000u, 0x2000u) == 0);
            r = rp_do_restore(&IO, &fl);
            CHECK(r == RP_DONE);
            check_restored("cut in restore over defaults");
        }
        printf("power cuts over M-Vave's defaults: after each of the %ld steps, run again: ok\n", total);
    }
    /* a stash of another unit (its Bluetooth address bytes differ): refused, nothing written, the stash kept */
    stock_unit();
    rp_do_stash(&IO, &fl);
    stock_rebuild();
    stock_defaults();
    memcpy(ref, nor, sizeof nor);
    nor[0xFFF40u] ^= 0x5Au;
    ref[0xFFF40u] ^= 0x5Au;
    r = rp_do_restore(&IO, &fl);
    CHECK(r == RP_REFUSED && (fl & RP_F_CAL_REFUSED) && (fl & RP_F_BTIF_REFUSED));
    CHECK(!memcmp(nor, ref, sizeof nor) && rp_stash_get(&IO, 0) == 1);
    /* another chip (its unique ID differs, the address bytes the same, FF as on the SMK-37s): refused, nothing
     * written */
    stock_unit();
    memset(nor + 0xFFF40u, 0xFF, 8);
    rp_do_stash(&IO, &fl);
    stock_rebuild();
    stock_defaults();
    memcpy(ref, nor, sizeof nor);
    sim_uid[7] ^= 1u;
    r = rp_do_restore(&IO, &fl);
    CHECK(r == RP_REFUSED && (fl & RP_F_CAL_REFUSED));
    CHECK(!memcmp(nor, ref, sizeof nor) && rp_stash_get(&IO, 0) == 1);
    sim_uid[7] ^= 1u;                                   /* the right chip again: done */
    r = rp_do_restore(&IO, &fl);
    CHECK(r == RP_DONE && nor[0xC1008u] != 0x32u);
    /* no usable unique ID (all FF, or all 00): erased sectors only, M-Vave's defaults there are refused */
    {
        int z;
        for (z = 0; z < 2; z++) {
            static uint8_t rebuilt[0x100000];
            stock_unit();
            memcpy(ref, nor, sizeof nor);
            memset(sim_uid, z ? 0x00 : 0xFF, 8);
            rp_do_stash(&IO, &fl);
            stock_rebuild();
            memcpy(rebuilt, nor, sizeof nor);
            r = rp_do_restore(&IO, &fl);
            CHECK(r == RP_DONE);
            check_restored(z ? "uid 00, erased sectors" : "uid FF, erased sectors");
            memcpy(nor, rebuilt, sizeof nor);
            stock_defaults();
            r = rp_do_restore(&IO, &fl);
            CHECK(r == RP_REFUSED && (fl & RP_F_CAL_REFUSED) && nor[0xC1008u] == 0x32u && rp_stash_get(&IO, 0) == 1);
        }
    }
    /* a stash of 0.1 (version 1, no address): put back only into erased sectors; defaults there: refused, kept */
    {
        uint8_t h[36];
        uint32_t c;
        stock_unit();
        memcpy(ref, nor, sizeof nor);
        rp_do_stash(&IO, &fl);
        memcpy(h, nor + 0xF0000u, 32);
        h[8] = 1;
        c = rp_crc(0xFFFFFFFFu, h, 32);
        c = rp_crc(c, nor + 0xF1000u, 0x1000u);
        c = rp_crc(c, nor + 0xF2000u, 0x1000u);
        rp_put32(h + 32, ~c);
        memset(nor + 0xF0000u, 0xFF, 64);
        memcpy(nor + 0xF0000u, h, 36);
        CHECK(rp_stash_get(&IO, 0) == 1);
        {
            static uint8_t v1[0x100000];
            stock_rebuild();
            memcpy(v1, nor, sizeof nor);
            r = rp_do_restore(&IO, &fl);                /* the sectors erased: put back */
            CHECK(r == RP_DONE);
            check_restored("0.1 stash, erased sectors");
            memcpy(nor, v1, sizeof nor);
            stock_defaults();
            r = rp_do_restore(&IO, &fl);                /* M-Vave's defaults there: refused */
            CHECK(r == RP_REFUSED && (fl & RP_F_CAL_REFUSED) && nor[0xC1008u] == 0x32u && rp_stash_get(&IO, 0) == 1);
        }
    }
    /* no calibration on the unit: stashed without it; the restore leaves USRTRIM alone */
    stock_unit();
    memset(nor + 0xC1000u, 0xFF, 0x1000u);
    r = rp_do_stash(&IO, &fl);
    CHECK(r == RP_STASHED && !(fl & RP_F_STASH_CAL));
    stock_rebuild();
    memset(nor + 0xC1000u, 0x33, 0x10);                 /* (something there now: not touched) */
    r = rp_do_restore(&IO, &fl);
    CHECK(r == RP_DONE && nor[0xC1000u] == 0x33u);
    /* a broken mapping (another firmware's bytes under the marker): the stash still takes the unit's own data */
    stock_unit();
    memset(nor + 0xC3000u, 0x00, 0x8000u);
    memcpy(ref, nor, sizeof nor);
    CHECK(rp_intact(&IO) == 0);
    r = rp_do_stash(&IO, &fl);
    CHECK(r == RP_STASHED);
    CHECK(rp_state(&IO, &fl) == RP_WAIT_STOCK);
    stock_rebuild();
    stock_defaults();
    r = rp_do_restore(&IO, &fl);
    CHECK(r == RP_DONE && !memcmp(nor + 0xC0000u, ref + 0xC0000u, 0x2000u));
    CHECK(refused_calls == 0);
}

static int load(const char *path, uint8_t *dst)
{
    FILE *f = path ? fopen(path, "rb") : 0;
    size_t n;
    if (!f)
        return 0;
    n = fread(dst, 1, 0x100000, f);
    fclose(f);
    return n == 0x100000;
}

/* a real unit: its dump as the "before", a dump of the same unit after a rebuild for the rebuilt blocks */
static void test_dumps(void)
{
    static uint8_t after[0x100000];
    uint32_t fl;
    int r, f0 = fails;
    if (!load(getenv("REPAIR_ORIG"), nor) || !load(getenv("REPAIR_AFTER"), after)) {
        printf("real dumps: skipped (REPAIR_ORIG, REPAIR_AFTER)\n");
        return;
    }
    memcpy(ref, nor, sizeof nor);
    CHECK(rp_intact(&IO) == 1 && rp_cal_now(&IO) == 1);
    r = rp_do_stash(&IO, &fl);
    CHECK(r == RP_STASHED && (fl & RP_F_STASH_CAL) && (fl & RP_F_WHEELS));
    memcpy(nor + 0xC0000u, after + 0xC0000u, 0x30000u);    /* what the stock firmware did */
    CHECK(rp_state(&IO, &fl) == RP_READY && !(fl & RP_F_CAL_NOW));
    r = rp_do_restore(&IO, &fl);
    CHECK(r == RP_DONE && (fl & RP_F_CAL_NOW));
    CHECK(!memcmp(nor + 0xC0000u, ref + 0xC0000u, 0x2000u));
    CHECK(!memcmp(nor + 0xC2060u, ref + 0xC2060u, 4) && !memcmp(nor + 0xC2080u, ref + 0xC2080u, 6));
    nor[0xC2060u] = after[0xC2060u]; /* (the rest of the globals: the rebuilt ones) */
    memcpy(nor + 0xC2060u, after + 0xC2060u, 4);
    memcpy(nor + 0xC2080u, after + 0xC2080u, 6);
    CHECK(!memcmp(nor + 0xC2000u, after + 0xC2000u, 0x28000u));
    /* again, with M-Vave's defaults written over the rebuilt unit before the restore */
    memcpy(nor, ref, sizeof nor);
    r = rp_do_stash(&IO, &fl);
    memcpy(nor + 0xC0000u, after + 0xC0000u, 0x30000u);
    stock_defaults();
    r = rp_do_restore(&IO, &fl);
    CHECK(r == RP_DONE && !memcmp(nor + 0xC0000u, ref + 0xC0000u, 0x2000u));
    printf("real dumps: %s\n", fails != f0 ? "FAILED" : "ok");
}

int main(void)
{
    (void)same;
    test_basic();
    test_cuts();
    test_refusals();
    test_dumps();
    printf(fails ? "repair_test: %d FAILED\n" : "repair_test: ok\n", fails);
    return fails != 0;
}
