/* SPDX-License-Identifier: MPL-2.0
 * SMK-37 Repair: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* SMK-37 Repair: a status screen, two actions (repair.h), started from the install page (SysEx) or on the unit
 * (SEQ PLAY held 3 s). Nothing is written at boot. Part of the platform's unity build (device/unity.c,
 * host/plat_host.c), after the platform: gfx.c and the flash driver are in scope.
 *
 * SysEx (USB), manufacturer 0x7D, "REP":
 *   F0 7D 52 45 50 01 F7   the state                 F0 7D 52 45 50 03 F7   STASH
 *   F0 7D 52 45 50 04 F7   RESTORE
 *   the answer to each (an action's once it is done): F0 7D 52 45 50 02 state flags F7 (repair.h RP_*, RP_F_*) */
#include "repair.h"

static void app_frame_draw_now(void);

/* ------------------------------------------------------------------ the flash */
#ifdef PLAT_HOST
static uint8_t sim_nor[0x100000];                /* the host: a blank unit */
static uint8_t sim_ready;
static int fr_read(void *c, uint32_t off, void *d, uint32_t n)
{
    uint32_t i;
    (void)c;
    if (!sim_ready) {
        for (i = 0; i < sizeof sim_nor; i++)
            sim_nor[i] = 0xFFu;
        sim_ready = 1;
    }
    if (off >= sizeof sim_nor || n > sizeof sim_nor - off)
        return -1;
    for (i = 0; i < n; i++)
        ((uint8_t *)d)[i] = sim_nor[off + i];
    return 0;
}
static int fr_erase(void *c, uint32_t off)
{
    uint32_t i;
    (void)c;
    for (i = 0; i < 0x1000u; i++)
        sim_nor[off + i] = 0xFFu;
    return 0;
}
static int fr_prog(void *c, uint32_t off, const void *s, uint32_t n)
{
    uint32_t i;
    (void)c;
    for (i = 0; i < n; i++)
        sim_nor[off + i] &= ((const uint8_t *)s)[i];
    return 0;
}
#define fr_prog_cal fr_prog
#define fr_erase_cal fr_erase
static int fr_uid(void *c, uint8_t id[8])
{
    uint32_t i;
    (void)c;
    for (i = 0; i < 8u; i++)
        id[i] = (uint8_t)(0x11u * (i + 1u));
    return 0;
}
static void fr_idle(void) {}
#else
/* the windows repair.h may touch, checked again here (the RAM driver checks FL_GUARD / FL_ERASE_GUARD on top) */
static int fr_erase_ok(uint32_t off)
{
    return off == RP_STASH || off == RP_S_BTIF || off == RP_S_TRIM || off == RP_S_GLOB || off == RP_MARK_SEC ||
           off == RP_GLOB;
}
static int fr_prog_ok(uint32_t off, uint32_t n)
{
    return FL_IN(off, n, RP_STASH, RP_STASH_HI) || FL_IN(off, n, RP_GLOB, RP_GLOB + RP_SEC);
}
static int fr_read(void *c, uint32_t off, void *d, uint32_t n)
{
    (void)c;
    return flash_ok ? st_read(off, d, n) : -1;
}
static int fr_erase(void *c, uint32_t off)
{
    uint32_t took;
    (void)c;
    if (!flash_ok || !fr_erase_ok(off))
        return -8;
    audio_silence();
    return fl_erase4k(off, &took);
}
static int fr_prog(void *c, uint32_t off, const void *s, uint32_t n)
{
    (void)c;
    if (!flash_ok || !fr_prog_ok(off, n))
        return -8;
    return fl_write(off, (const uint8_t *)s, n);
}
/* the calibration and the pairing back: fm1_flash.h's fl_prog_ram with its own guard, [0xC0000, 0xC2000) and nothing
 * else (the driver's FL_GUARD refuses that range for every other caller). One page, into erased flash only (repair.h
 * checks every byte first) */
static RAMFN int fr_prog_cal_ram(uint32_t off, const uint8_t *src, uint32_t n, uint32_t *took_us)
{
    fl_saved_t s;
    int rc;
    uint32_t i;
    if (!FL_IN(off, n, 0x000C0000u, 0x000C2000u) || n == 0 || n > 256u || ((off & 0xFFu) + n) > 256u)
        return -1;
    if (fl_enter(&s))
        return -2;
    fl_wren();
    if (!(fl_rdsr(0x05) & 2u))
        rc = -3;
    else {
        fl_cs(0);
        fl_tx(0x02);
        fl_addr(off);
        for (i = 0; i < n; i++)
            fl_tx(src[i]);
        fl_cs(1);
        fl_inval(off, n);
        rc = fl_wait_ready(10000u, took_us);
    }
    fl_exit(&s);
    return rc;
}
static int fr_prog_cal(void *c, uint32_t off, const void *s, uint32_t n)
{
    static uint8_t page[256];                    /* (the RAM routine reads its source with XIP off: a RAM copy) */
    uint32_t i, took, f;
    int rc;
    (void)c;
    if (!flash_ok || !FL_IN(off, n, 0x000C0000u, 0x000C2000u) || n > 256u)
        return -8;
    for (i = 0; i < n; i++)
        page[i] = ((const uint8_t *)s)[i];
    f = irq_save();
    rc = FL_FAR(fr_prog_cal_ram)(off, page, n, &took);
    irq_restore(f);
    return rc;
}
/* ... and erased first when M-Vave's firmware wrote its own defaults there: fm1_flash.h's fl_erase4k_ram with its own
 * guard, the two sectors 0xC0000 and 0xC1000 and nothing else (repair.h: only with a stash of this unit) */
static RAMFN int fr_erase_cal_ram(uint32_t off, uint32_t *took_us)
{
    fl_saved_t s;
    int rc;
    if (off != 0x000C0000u && off != 0x000C1000u)
        return -1;
    if (fl_enter(&s))
        return -2;
    fl_wren();
    if (!(fl_rdsr(0x05) & 2u))
        rc = -3;
    else {
        fl_cs(0);
        fl_tx(0x20);
        fl_addr(off);
        fl_cs(1);
        fl_inval(off, 4096u);
        rc = fl_wait_ready(500000u, took_us);
    }
    fl_exit(&s);
    return rc;
}
static int fr_erase_cal(void *c, uint32_t off)
{
    uint32_t took, f;
    int rc;
    (void)c;
    if (!flash_ok || (off != 0x000C0000u && off != 0x000C1000u))
        return -8;
    audio_silence();
    f = irq_save();
    rc = FL_FAR(fr_erase_cal_ram)(off, &took);
    irq_restore(f);
    return rc;
}
/* the flash chip's unique ID: 0x4B, four dummy bytes, eight bytes of ID (in the style of fm1_flash.h fl_jedec_ram) */
static RAMFN int fr_uid_ram(uint8_t *id)
{
    fl_saved_t s;
    uint32_t i;
    if (fl_enter(&s))
        return -2;
    fl_cs(0);
    fl_tx(0x4B);
    for (i = 0; i < 4u; i++)
        fl_tx(0xFF);
    for (i = 0; i < 8u; i++)
        id[i] = fl_rx();
    fl_cs(1);
    fl_exit(&s);
    return 0;
}
static int fr_uid(void *c, uint8_t id[8])
{
    static uint8_t buf[8];                       /* (RAM: the routine runs with XIP off) */
    uint32_t i, f;
    int rc;
    (void)c;
    if (!flash_ok)
        return -8;
    f = irq_save();
    rc = FL_FAR(fr_uid_ram)(buf);
    irq_restore(f);
    for (i = 0; i < 8u; i++)
        id[i] = buf[i];
    return rc;
}
static void fr_idle(void) { fm1_wdt_feed(); }
#endif
static const rp_io IO = {0, fr_read, fr_erase, fr_prog, fr_prog_cal, fr_erase_cal, fr_uid, fr_idle};

/* ------------------------------------------------------------------ the state */
enum { OP_NONE, OP_STASH = 3, OP_RESTORE = 4 };
static struct {
    int state;                                   /* RP_*: as found at boot, then the last action's result */
    uint32_t flags;
    int op;                                      /* an action asked for (SysEx or the hold), run by app_frame */
    int op_reply;                                /* answer the page when it is done */
    uint32_t play_at;                            /* SEQ PLAY went down (0: not held) */
    uint32_t btn;
    int drawn_state, drawn_hold;
    uint32_t drawn_flags;
    char msg[2][24];
    uint32_t msg_until;
} ui;

static void reply(void)
{
    uint8_t f[9] = {0xF0, 0x7D, 0x52, 0x45, 0x50, 0x02, 0, 0, 0xF7};
    f[6] = (uint8_t)(ui.state & 0x7F);
    f[7] = (uint8_t)(ui.flags & 0x7F);
    plat_sysex_send(f, sizeof f);
}

int app_sysex(const uint8_t *p, uint32_t n)
{
    if (n < 5u || p[0] != 0x7Du || p[1] != 0x52u || p[2] != 0x45u || p[3] != 0x50u)
        return 0;
    if (p[4] == 0x01u) {
        if (ui.op != OP_NONE) {                  /* (busy: the answer comes when it is done) */
            uint8_t f[9] = {0xF0, 0x7D, 0x52, 0x45, 0x50, 0x02, RP_BUSY, 0, 0xF7};
            plat_sysex_send(f, sizeof f);
        } else {
            reply();
        }
    } else if ((p[4] == OP_STASH || p[4] == OP_RESTORE) && ui.op == OP_NONE) {
        ui.op = p[4];
        ui.op_reply = 1;
    }
    return 1;
}

static int can_stash(void) { return ui.state == RP_IDLE; }
static int can_restore(void) { return ui.state == RP_READY; }

static void run_op(void)
{
    int op = ui.op;
    ui.state = RP_BUSY;
    ui.drawn_state = -1;
    app_frame_draw_now();
    if (op == OP_STASH)
        ui.state = rp_do_stash(&IO, &ui.flags);
    else
        ui.state = rp_do_restore(&IO, &ui.flags);
    ui.op = OP_NONE;
    if (ui.op_reply)
        reply();
    ui.op_reply = 0;
}

/* ------------------------------------------------------------------ the platform's calls */
void app_init(void)
{
    ui.state = rp_state(&IO, &ui.flags);
    ui.drawn_state = -1;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, pkt;
    int port;
    (void)master_q12;
    while (plat_midi_in(&pkt, &port))
        ;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}

void app_panic(void) {}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1500u;
    ui.drawn_state = -1;
}

#define HOLD_MS 3000u
static int hold_pct(void)
{
    uint32_t t;
    if (!ui.play_at)
        return -1;
    t = plat_ms() - ui.play_at;
    return t >= HOLD_MS ? 100 : (int)(t * 100u / HOLD_MS);
}

static void input(void)
{
    uint32_t btn = plat_buttons(), down = btn & ~ui.btn, now = plat_ms();
    if (down & (1u << B_PLAY))
        ui.play_at = now ? now : 1u;
    if (!(btn & (1u << B_PLAY)))
        ui.play_at = 0;
    if (ui.play_at && now - ui.play_at >= HOLD_MS && ui.op == OP_NONE) {
        ui.play_at = 0;
        if (can_stash())
            ui.op = OP_STASH;
        else if (can_restore())
            ui.op = OP_RESTORE;
        ui.op_reply = 1;                         /* (the page, if it is open, hears it too) */
    }
    ui.btn = btn;
    plat_leds(can_stash() || can_restore() ? 1u << B_PLAY : 0u, 0);
}

void app_input_only(void) { input(); }

/* ------------------------------------------------------------------ the screen */
typedef struct {
    const char *head;
    uint16_t col;
    const char *line[4];
    const char *act;                             /* what SEQ PLAY held does, or 0 */
} screen_t;

static screen_t screen_for(void)
{
    screen_t s = {"", C_HI, {0, 0, 0, 0}, 0};
    switch (ui.state) {
    case RP_BUSY:
        s.head = "WORKING";
        s.line[0] = "Keep the keyboard on";
        s.line[1] = "and connected.";
        break;
    case RP_IDLE:
        s.head = "STEP 1 OF 2";
        s.line[0] = "Saves this unit's";
        s.line[1] = "calibration, then lets";
        s.line[2] = "M-Vave's firmware";
        s.line[3] = "rebuild its key mapping.";
        s.act = "SAVE";
        break;
    case RP_STASHED:
    case RP_WAIT_STOCK:
        s.head = "SAVED";
        s.line[0] = "Now install M-Vave V016.";
        s.line[1] = "Let it start once,";
        s.line[2] = "then install SMK REPAIR";
        s.line[3] = "again for step 2.";
        break;
    case RP_READY:
        s.head = "STEP 2 OF 2";
        s.line[0] = "The key mapping is new.";
        s.line[1] = "Puts this unit's";
        s.line[2] = "calibration and wheels";
        s.line[3] = "back.";
        s.act = "RESTORE";
        break;
    case RP_DONE:
        s.head = "REPAIRED";
        s.line[0] = "Calibration is back.";
        s.line[1] = "Install M-Vave V016";
        s.line[2] = "to finish.";
        break;
    case RP_REFUSED:
        s.head = "STOPPED";
        s.col = C_WHITE;
        if (ui.flags & RP_F_CAL_REFUSED) {
            s.line[0] = "The calibration there";
            s.line[1] = "is not the saved one:";
            s.line[2] = "nothing written. The";
            s.line[3] = "saved copy is kept.";
        } else {
            s.line[0] = "Already saved: install";
            s.line[1] = "M-Vave V016, then this";
            s.line[2] = "again for step 2.";
        }
        break;
    default:
        s.head = "FLASH ERROR";
        s.col = C_WHITE;
        s.line[0] = "Nothing is lost.";
        s.line[1] = "Switch off and on,";
        s.line[2] = "then try again.";
        break;
    }
    return s;
}

static void draw(int hold)
{
    screen_t s = screen_for();
    int32_t y, i;
    char t[32];
    cv_begin(240, 64, C_BLACK);
    cv_text(12, 6, &FONT_B, "SMK-37 REPAIR", C_WHITE);
    cv_text(240 - 12 - text_w(&FONT_XS, APP_VERSION), 12, &FONT_XS, APP_VERSION, C_DIM);
    cv_rect(12, 34, 216, 1, C_LINE);
    cv_text(12, 42, &FONT_S, ui.msg_until && plat_ms() < ui.msg_until ? ui.msg[0] : s.head, s.col);
    cv_blit(0, 0);
    cv_begin(240, 64, C_BLACK);
    for (i = 0, y = 0; i < 4; i++, y += 16)
        if (s.line[i])
            cv_text(12, y, &FONT_XS, s.line[i], C_GRAY);
    cv_blit(0, 70);
    cv_begin(240, 48, C_BLACK);
    t[0] = 0;
    {
        const char *a = "CALIBRATION ", *b = ui.flags & RP_F_CAL_NOW ? "FOUND" : "NOT FOUND";
        uint32_t k = 0;
        while (*a)
            t[k++] = *a++;
        while (*b)
            t[k++] = *b++;
        t[k] = 0;
    }
    cv_text(12, 0, &FONT_XS, t, ui.flags & RP_F_CAL_NOW ? C_HI : C_DIM);
    if (ui.flags & RP_F_STASH_CAL)
        cv_text(12, 16, &FONT_XS, "SAVED COPY ON THE UNIT", C_HI);
    if (ui.flags & RP_F_BTIF_REFUSED)
        cv_text(12, 32, &FONT_XS, "PAIRING: KEPT THE NEW ONE", C_GRAY);
    cv_blit(0, 140);
    cv_begin(240, 44, C_BLACK);
    if (s.act) {
        const char *a = "HOLD SEQ PLAY 3 S: ";
        uint32_t k = 0;
        const char *b = s.act;
        while (*a)
            t[k++] = *a++;
        while (*b)
            t[k++] = *b++;
        t[k] = 0;
        cv_text(12, 0, &FONT_XS, t, C_WHITE);
        cv_rect(12, 22, 216, 10, C_LINE);
        if (hold > 0)
            cv_rect(12, 22, 216 * hold / 100, 10, C_HI);
    } else if (ui.state == RP_STASHED || ui.state == RP_WAIT_STOCK || ui.state == RP_DONE) {
        cv_text(12, 0, &FONT_XS, "genkigenki.github.io/smk37", C_DIM);
    }
    cv_blit(0, 190);
}

static void app_frame_draw_now(void)
{
    draw(-1);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.drawn_state = -1;
}

void app_frame(void)
{
    int hold;
    input();
    if (ui.op != OP_NONE) {
        run_op();
        ui.drawn_state = -1;
    }
    hold = hold_pct();
    if (ui.drawn_state != ui.state || ui.drawn_flags != ui.flags || ui.drawn_hold != hold ||
        (ui.msg_until && plat_ms() >= ui.msg_until)) {
        if (ui.msg_until && plat_ms() >= ui.msg_until)
            ui.msg_until = 0;
        draw(hold);
        ui.drawn_state = ui.state;
        ui.drawn_flags = ui.flags;
        ui.drawn_hold = hold;
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "state"))
        *got = ui.state;
    else if (!strcmp(what, "flags"))
        *got = (int)ui.flags;
    else
        return 0;
    return 1;
}
#endif
