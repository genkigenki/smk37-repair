/* SPDX-License-Identifier: MPL-2.0
 * SMK-37 Repair: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The repair's logic, portable C (the device's flash in app.c, a simulated NOR in tests/repair_test.c).
 *
 * The problem: when M-Vave's firmware (V016) starts without its end marker "usrdata" at 0xEAFF8, it rebuilds its key
 * mapping, and its 64 KiB erases at the presets (0xC3000 + n * 0x5000) also erase BTIF (0xC0000, the Bluetooth
 * pairing), USRTRIM (0xC1000, the key calibration, "trimdat" at 0xC1224) and the wheel values in its globals (0xC2060,
 * 0xC2080). A unit whose mapping another firmware overwrote ("every key is C8") needs that rebuild. So:
 *
 *   STASH    the unit's own BTIF and USRTRIM and the 10 wheel bytes are copied, with a CRC, to 0xF0000-0xF2FFF (blank
 *            on a stock unit, outside the rebuild's blocks, below the stock USR resources at 0xF4000); then the marker
 *            is released (its sector erased). M-Vave's firmware then rebuilds a clean mapping.
 *   RESTORE  (this firmware installed again) with a valid stash and the rebuilt mapping in place: BTIF and USRTRIM
 *            are programmed back where they are erased (never erased here: a sector that holds anything but erased
 *            bytes or the stash's own bytes is refused; BTIF re-paired since stays as it is, a calibration that is
 *            not the unit's own keeps the stash), the wheel values patched into the globals, then the stash erased.
 *
 * Stash, sector 0xF0000 (the header is programmed last: a stash is valid only once complete):
 *   +0   "SMKREPR1"
 *   +8   version 1, flags (RP_SF_*), 0, 0
 *   +12  the 10 wheel bytes (0xC2060 4, 0xC2080 6)
 *   +22  0, 0
 *   +24  CRC-32 of the globals (0xC2000) and +28 of the first preset's sector (0xC3000) at the stash: a rebuild changes
 *        them, so a RESTORE runs only after one (and a STASH cut before its release is finished, not repeated)
 *   +32  CRC-32 (zlib) over +0..+31, then the sectors 0xF1000 and 0xF2000
 *   +64  the patched globals' record, programmed during a RESTORE (erased until then): "SMKREPG1", CRC-32 of 0xF3000
 *   0xF1000  BTIF's copy        0xF2000  USRTRIM's copy        0xF3000  the patched globals (during a RESTORE)
 * Every step can be cut by a power loss and run again. */
#pragma once
#include <stdint.h>

#define RP_BTIF      0x000C0000u
#define RP_TRIM      0x000C1000u
#define RP_TRIM_MARK 0x000C1224u                /* "trimdat\0" */
#define RP_GLOB      0x000C2000u                /* the stock globals, magic A3 3A */
#define RP_WHEEL_A   0x000C2060u                /* 4 wheel bytes */
#define RP_WHEEL_B   0x000C2080u                /* 6 wheel bytes */
#define RP_MARK      0x000EAFF8u                /* "usrdata\0" */
#define RP_MARK_SEC  0x000EA000u
#define RP_STASH     0x000F0000u                /* header */
#define RP_S_BTIF    0x000F1000u
#define RP_S_TRIM    0x000F2000u
#define RP_S_GLOB    0x000F3000u
#define RP_STASH_HI  0x000F4000u
#define RP_SEC       0x1000u

enum { RP_SF_GLOB = 1, RP_SF_WHEELS = 2, RP_SF_CAL = 4 };   /* stash flags */

/* the page's states and flags (the SysEx reply F0 7D 52 45 50 02 state flags F7) */
enum { RP_BUSY = 0, RP_IDLE = 1, RP_STASHED = 2, RP_WAIT_STOCK = 3, RP_READY = 4, RP_DONE = 5,
       RP_ERR_FLASH = 0x10, RP_REFUSED = 0x11 };
enum { RP_F_CAL_NOW = 1, RP_F_STASH_CAL = 2, RP_F_WHEELS = 4, RP_F_BTIF_REFUSED = 8, RP_F_CAL_REFUSED = 16 };

typedef struct {
    void *ctx;
    int (*read)(void *ctx, uint32_t off, void *dst, uint32_t n);          /* 0 = ok */
    int (*erase)(void *ctx, uint32_t off);                                /* one 4 KiB sector, 0 = ok */
    int (*prog)(void *ctx, uint32_t off, const void *src, uint32_t n);    /* within one 256 B page, 0 = ok */
    int (*prog_cal)(void *ctx, uint32_t off, const void *src, uint32_t n);   /* the same, only [0xC0000, 0xC2000) */
    void (*idle)(void);                                                   /* the watchdog; may be 0 */
} rp_io;

static uint32_t rp_crc(uint32_t c, const uint8_t *p, uint32_t n)   /* zlib CRC-32; c: ~0 to start */
{
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return c;
}
static void rp_idle(const rp_io *io)
{
    if (io->idle)
        io->idle();
}
static int rp_same(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint32_t i, d = 0;
    for (i = 0; i < n; i++)
        d |= (uint32_t)(a[i] ^ b[i]);
    return d == 0;
}
static int rp_blank(const uint8_t *a, uint32_t n)
{
    uint32_t i, d = 0xFFu;
    for (i = 0; i < n; i++)
        d &= a[i];
    return d == 0xFFu;
}
static uint32_t rp_le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void rp_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* a sector's CRC, read page by page; < 0 a read error (returned through *err) */
static uint32_t rp_crc_sec(const rp_io *io, uint32_t c, uint32_t off, int *err)
{
    uint8_t b[256];
    uint32_t i;
    for (i = 0; i < RP_SEC; i += 256u) {
        if (io->read(io->ctx, off + i, b, 256))
            *err = -1;
        c = rp_crc(c, b, 256);
    }
    return c;
}

static int rp_has(const rp_io *io, uint32_t off, const char *s, uint32_t n)   /* 1: the bytes there are s; < 0 */
{
    uint8_t b[8];
    if (n > 8u || io->read(io->ctx, off, b, n))
        return -1;
    return rp_same(b, (const uint8_t *)s, n);
}

/* ---------------------------------------------------------------- the stock mapping (as storage.c st_stock_intact) */
static int rp_marked(const rp_io *io) { return rp_has(io, RP_MARK, "usrdata", 8); }
/* 1: the marker over the stock layout (the globals' magic; each preset's head written, its last two sectors erased) */
static int rp_intact(const rp_io *io)
{
    uint8_t b[16];
    uint32_t n;
    int m = rp_marked(io);
    if (m <= 0)
        return m;
    if (io->read(io->ctx, RP_GLOB, b, 2))
        return -1;
    if (b[0] != 0xA3u || b[1] != 0x3Au)
        return 0;
    for (n = 0; n < 8u; n++) {
        uint32_t p = RP_GLOB + 0x1000u + n * 0x5000u;
        if (io->read(io->ctx, p, b, 16))
            return -1;
        if (rp_blank(b, 16))
            return 0;
        if (io->read(io->ctx, p + 0x3000u, b, 16))
            return -1;
        if (!rp_blank(b, 16))
            return 0;
        if (io->read(io->ctx, p + 0x4000u, b, 16))
            return -1;
        if (!rp_blank(b, 16))
            return 0;
    }
    return 1;
}
static int rp_cal_now(const rp_io *io) { return rp_has(io, RP_TRIM_MARK, "trimdat", 8); }

/* ---------------------------------------------------------------- the stash */
typedef struct {
    uint8_t flags;
    uint8_t wheels[10];
    uint32_t crc_glob, crc_preset0;             /* the mapping as it was at the stash: a rebuild changes both */
} rp_stash;

/* 1: a complete stash (its CRC), read into *s; 0: none; < 0 a read error */
static int rp_stash_get(const rp_io *io, rp_stash *s)
{
    uint8_t h[36];
    uint32_t c;
    int err = 0;
    if (io->read(io->ctx, RP_STASH, h, sizeof h))
        return -1;
    if (!rp_same(h, (const uint8_t *)"SMKREPR1", 8) || h[8] != 1u)
        return 0;
    c = rp_crc(0xFFFFFFFFu, h, 32);
    c = rp_crc_sec(io, c, RP_S_BTIF, &err);
    c = rp_crc_sec(io, c, RP_S_TRIM, &err);
    if (err)
        return -1;
    if (~c != rp_le32(h + 32))
        return 0;
    if (s) {
        uint32_t i;
        s->flags = h[9];
        for (i = 0; i < 10u; i++)
            s->wheels[i] = h[12 + i];
        s->crc_glob = rp_le32(h + 24);
        s->crc_preset0 = rp_le32(h + 28);
    }
    return 1;
}

/* the globals' record of a RESTORE under way (0xF0040 and 0xF3000): 1 there, 0 not, < 0 a read error */
static int rp_glob_rec(const rp_io *io)
{
    uint8_t rec[12];
    int err = 0, ok;
    if (io->read(io->ctx, RP_STASH + 64u, rec, sizeof rec))
        return -1;
    if (!rp_same(rec, (const uint8_t *)"SMKREPG1", 8))
        return 0;
    ok = ~rp_crc_sec(io, 0xFFFFFFFFu, RP_S_GLOB, &err) == rp_le32(rec + 8);
    return err ? -1 : ok;
}

/* 1: M-Vave's firmware rebuilt its mapping since the stash (the marker over the stock layout, and the globals or the
 * first preset changed), or a RESTORE is under way; 0: not; < 0 a read error */
static int rp_rebuilt(const rp_io *io, const rp_stash *st)
{
    int r = rp_glob_rec(io), err = 0;
    uint32_t g, p;
    if (r)
        return r;
    r = rp_intact(io);
    if (r <= 0)
        return r;
    g = ~rp_crc_sec(io, 0xFFFFFFFFu, RP_GLOB, &err);
    p = ~rp_crc_sec(io, 0xFFFFFFFFu, RP_GLOB + 0x1000u, &err);
    if (err)
        return -1;
    return g != st->crc_glob || p != st->crc_preset0;
}

/* the marker: released (its sector erased and read back), so M-Vave's firmware rebuilds at its next start */
static int rp_release(const rp_io *io)
{
    int r = rp_marked(io);
    if (r <= 0)
        return r;
    rp_idle(io);
    if (io->erase(io->ctx, RP_MARK_SEC) || rp_marked(io) != 0)
        return -2;
    return 0;
}

/* copy one sector to an erased one, page by page, and check it (prog: io->prog) */
static int rp_copy(const rp_io *io, uint32_t from, uint32_t to)
{
    uint8_t b[256], v[256];
    uint32_t i;
    for (i = 0; i < RP_SEC; i += 256u) {
        if (io->read(io->ctx, from + i, b, 256))
            return -1;
        if (!rp_blank(b, 256) && io->prog(io->ctx, to + i, b, 256))
            return -2;
        if (io->read(io->ctx, to + i, v, 256) || !rp_same(b, v, 256))
            return -3;
    }
    return 0;
}

/* STASH: RP_STASHED (flags: RP_F_*), or RP_REFUSED (a stash is already there: it must never be replaced by a
 * rebuilt unit's erased calibration), or RP_ERR_FLASH */
static int rp_do_stash(const rp_io *io, uint32_t *flags)
{
    uint8_t h[36], g[2];
    uint32_t s, c, i;
    int r, err = 0;
    rp_stash st;
    *flags = 0;
    r = rp_stash_get(io, &st);
    if (r < 0)
        return RP_ERR_FLASH;
    if (r == 1) {                                   /* a stash already: never replaced (by erased data, after a rebuild) */
        r = rp_rebuilt(io, &st);
        if (r < 0)
            return RP_ERR_FLASH;
        if (r)
            return RP_REFUSED;                      /* rebuilt: RESTORE is next */
        if (rp_release(io) < 0)                     /* a STASH cut before its release: finished */
            return RP_ERR_FLASH;
        if (st.flags & RP_SF_CAL)
            *flags |= RP_F_CAL_NOW | RP_F_STASH_CAL;
        if (st.flags & RP_SF_WHEELS)
            *flags |= RP_F_WHEELS;
        return RP_STASHED;
    }
    for (s = RP_STASH; s < RP_STASH_HI; s += RP_SEC) {     /* the stash window: erased (0xF3000 too) */
        rp_idle(io);
        if (io->erase(io->ctx, s))
            return RP_ERR_FLASH;
    }
    rp_idle(io);
    if (rp_copy(io, RP_BTIF, RP_S_BTIF) || rp_copy(io, RP_TRIM, RP_S_TRIM))
        return RP_ERR_FLASH;
    for (i = 0; i < sizeof h; i++)
        h[i] = 0;
    for (i = 0; i < 8u; i++)
        h[i] = (uint8_t)"SMKREPR1"[i];
    h[8] = 1;
    if (io->read(io->ctx, RP_GLOB, g, 2) || io->read(io->ctx, RP_WHEEL_A, h + 12, 4) ||
        io->read(io->ctx, RP_WHEEL_B, h + 16, 6))
        return RP_ERR_FLASH;
    if (g[0] == 0xA3u && g[1] == 0x3Au)
        h[9] |= RP_SF_GLOB;
    {
        uint32_t z = 0, f = 0xFFu;
        for (i = 0; i < 10u; i++) {
            z |= h[12 + i];
            f &= h[12 + i];
        }
        if (z && f != 0xFFu)
            h[9] |= RP_SF_WHEELS;
    }
    r = rp_cal_now(io);
    if (r < 0)
        return RP_ERR_FLASH;
    if (r)
        h[9] |= RP_SF_CAL;
    rp_put32(h + 24, ~rp_crc_sec(io, 0xFFFFFFFFu, RP_GLOB, &err));
    rp_put32(h + 28, ~rp_crc_sec(io, 0xFFFFFFFFu, RP_GLOB + 0x1000u, &err));
    c = rp_crc(0xFFFFFFFFu, h, 32);
    c = rp_crc_sec(io, c, RP_S_BTIF, &err);
    c = rp_crc_sec(io, c, RP_S_TRIM, &err);
    if (err)
        return RP_ERR_FLASH;
    rp_put32(h + 32, ~c);
    if (io->prog(io->ctx, RP_STASH, h, sizeof h) || rp_stash_get(io, 0) != 1)
        return RP_ERR_FLASH;
    if (rp_release(io) < 0)
        return RP_ERR_FLASH;
    if (h[9] & RP_SF_CAL)
        *flags |= RP_F_CAL_NOW | RP_F_STASH_CAL;
    if (h[9] & RP_SF_WHEELS)
        *flags |= RP_F_WHEELS;
    return RP_STASHED;
}

/* put a stashed sector back: skipped when it already is, programmed when every byte is erased or already the stash's
 * (also after a cut-off run), refused otherwise (1). 0 done, 1 refused, < 0 a flash error */
static int rp_put_back(const rp_io *io, uint32_t from, uint32_t to)
{
    uint8_t b[256], v[256];
    uint32_t i, k, any = 0;
    for (i = 0; i < RP_SEC; i += 256u) {        /* a stashed sector that was erased: nothing to put back */
        if (io->read(io->ctx, from + i, b, 256))
            return -1;
        any |= !rp_blank(b, 256);
    }
    if (!any)
        return 0;
    for (i = 0; i < RP_SEC; i += 256u) {        /* first the check, over the whole sector: nothing written if refused */
        if (io->read(io->ctx, from + i, b, 256) || io->read(io->ctx, to + i, v, 256))
            return -1;
        for (k = 0; k < 256u; k++)
            if (v[k] != 0xFFu && v[k] != b[k])
                return 1;
    }
    for (i = 0; i < RP_SEC; i += 256u) {
        if (io->read(io->ctx, from + i, b, 256) || io->read(io->ctx, to + i, v, 256))
            return -1;
        if (rp_same(b, v, 256))
            continue;
        if (io->prog_cal(io->ctx, to + i, b, 256))
            return -2;
        if (io->read(io->ctx, to + i, v, 256) || !rp_same(b, v, 256))
            return -3;
    }
    return 0;
}

/* the wheel values into the globals: the patched sector goes to 0xF3000 first (with its record at 0xF0040), so a cut
 * between the erase of 0xC2000 and its programming is finished by the next run. 0 done or nothing to do, < 0 error */
static int rp_wheels_back(const rp_io *io, const rp_stash *st, uint32_t *flags)
{
    uint8_t b[256], v[256], rec[12];
    uint32_t i, c;
    int err = 0, have;
    if (!(st->flags & RP_SF_GLOB) || !(st->flags & RP_SF_WHEELS))
        return 0;
    have = rp_glob_rec(io);
    if (have < 0 || io->read(io->ctx, RP_STASH + 64u, rec, sizeof rec))
        return -1;
    if (!have) {
        if (!rp_blank(rec, sizeof rec))
            return -4;                              /* a record but not its sector: never seen, not guessed */
        if (io->read(io->ctx, RP_GLOB, b, 2))
            return -1;
        if (b[0] != 0xA3u || b[1] != 0x3Au)
            return 0;                               /* no rebuilt globals to patch */
        if (io->read(io->ctx, RP_WHEEL_A, b, 4) || io->read(io->ctx, RP_WHEEL_B, b + 4, 6))
            return -1;
        if (rp_same(b, st->wheels, 10)) {
            *flags |= RP_F_WHEELS;
            return 0;
        }
        if (io->erase(io->ctx, RP_S_GLOB))          /* (erased by the stash; again, in case) */
            return -2;
        for (i = 0; i < RP_SEC; i += 256u) {
            if (io->read(io->ctx, RP_GLOB + i, b, 256))
                return -1;
            if (i == 0) {
                uint32_t k;
                for (k = 0; k < 4u; k++)
                    b[0x60 + k] = st->wheels[k];
                for (k = 0; k < 6u; k++)
                    b[0x80 + k] = st->wheels[4 + k];
            }
            if (!rp_blank(b, 256) && io->prog(io->ctx, RP_S_GLOB + i, b, 256))
                return -2;
            if (io->read(io->ctx, RP_S_GLOB + i, v, 256) || !rp_same(b, v, 256))
                return -3;
        }
        c = ~rp_crc_sec(io, 0xFFFFFFFFu, RP_S_GLOB, &err);
        if (err)
            return -1;
        for (i = 0; i < 8u; i++)
            rec[i] = (uint8_t)"SMKREPG1"[i];
        rp_put32(rec + 8, c);
        if (io->prog(io->ctx, RP_STASH + 64u, rec, sizeof rec))
            return -2;
    }
    /* 0xF3000 holds the globals as they must be: 0xC2000 becomes that */
    for (i = 0; i < RP_SEC; i += 256u) {
        if (io->read(io->ctx, RP_S_GLOB + i, b, 256) || io->read(io->ctx, RP_GLOB + i, v, 256))
            return -1;
        if (!rp_same(b, v, 256))
            break;
    }
    if (i < RP_SEC) {
        rp_idle(io);
        if (io->erase(io->ctx, RP_GLOB))
            return -2;
        rp_idle(io);
        if (rp_copy(io, RP_S_GLOB, RP_GLOB))
            return -3;
    }
    *flags |= RP_F_WHEELS;
    return 0;
}

/* RESTORE: RP_DONE (flags), RP_WAIT_STOCK (no rebuilt mapping yet: nothing written), RP_IDLE (no stash), RP_REFUSED
 * (flags: which sector), RP_ERR_FLASH */
static int rp_do_restore(const rp_io *io, uint32_t *flags)
{
    rp_stash st;
    uint32_t s;
    int r = rp_stash_get(io, &st), refused = 0;
    *flags = 0;
    if (r < 0)
        return RP_ERR_FLASH;
    if (r == 0) {                                   /* no stash: a RESTORE cut while it erased the stash is finished */
        uint8_t b[256];
        uint32_t i, left = 0;
        for (s = RP_S_BTIF; s < RP_STASH_HI; s += RP_SEC) {
            for (i = 0; i < RP_SEC; i += 256u) {
                if (io->read(io->ctx, s + i, b, 256))
                    return RP_ERR_FLASH;
                if (!rp_blank(b, 256))
                    break;
            }
            if (i < RP_SEC) {
                rp_idle(io);
                if (io->erase(io->ctx, s))
                    return RP_ERR_FLASH;
                left = 1;
            }
        }
        return left ? RP_DONE : RP_IDLE;
    }
    r = rp_rebuilt(io, &st);
    if (r < 0)
        return RP_ERR_FLASH;
    if (r == 0)
        return RP_WAIT_STOCK;
    if (st.flags & RP_SF_CAL)
        *flags |= RP_F_STASH_CAL;
    r = rp_put_back(io, RP_S_BTIF, RP_BTIF);
    if (r < 0)
        return RP_ERR_FLASH;
    if (r)
        *flags |= RP_F_BTIF_REFUSED;              /* (pairings made since: they stay, the old ones are let go) */
    rp_idle(io);
    if (st.flags & RP_SF_CAL) {                     /* (no calibration in the stash: nothing to put back) */
        r = rp_put_back(io, RP_S_TRIM, RP_TRIM);
        if (r < 0)
            return RP_ERR_FLASH;
        if (r) {
            *flags |= RP_F_CAL_REFUSED;
            refused = 1;
        }
    }
    rp_idle(io);
    if (rp_wheels_back(io, &st, flags) < 0)
        return RP_ERR_FLASH;
    r = rp_cal_now(io);
    if (r < 0)
        return RP_ERR_FLASH;
    if (r)
        *flags |= RP_F_CAL_NOW;
    if (refused)
        return RP_REFUSED;                          /* the calibration: the stash stays, nothing of it is lost */
    for (s = RP_STASH; s < RP_STASH_HI; s += RP_SEC) {   /* the header first: the stash is gone in one step */
        rp_idle(io);
        if (io->erase(io->ctx, s))
            return RP_ERR_FLASH;
    }
    return RP_DONE;
}

/* the state at boot / on a query: RP_IDLE (no stash; or a stash whose release was cut: STASH finishes it),
 * RP_WAIT_STOCK (stashed, the marker released: M-Vave's firmware next) or RP_READY (rebuilt: RESTORE next) */
static int rp_state(const rp_io *io, uint32_t *flags)
{
    rp_stash st;
    int r = rp_stash_get(io, &st), c = rp_cal_now(io);
    *flags = c > 0 ? RP_F_CAL_NOW : 0u;
    if (r < 0 || c < 0)
        return RP_ERR_FLASH;
    if (r == 0)
        return RP_IDLE;
    if (st.flags & RP_SF_CAL)
        *flags |= RP_F_STASH_CAL;
    if (st.flags & RP_SF_WHEELS)
        *flags |= RP_F_WHEELS;
    r = rp_rebuilt(io, &st);
    if (r < 0)
        return RP_ERR_FLASH;
    if (r)
        return RP_READY;
    r = rp_marked(io);
    if (r < 0)
        return RP_ERR_FLASH;
    return r ? RP_IDLE : RP_WAIT_STOCK;
}
