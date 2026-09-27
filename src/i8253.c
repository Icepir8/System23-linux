/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  i8253.c — Intel 8253 PIT (faithful port of I8253PIT.cs, PitCounter core)
 *
 *  Only Counter 2's terminal count has an external effect: it sets
 *  io_do755interrupt = OUT (the RST 7.5 line), exactly as the C# wiring did.
 * ===========================================================================*/
#include "i8253.h"
#include "ioports.h"
#include <stdio.h>

/* modes 0..5 */
enum { M_INT_TC = 0, M_ONESHOT, M_RATEGEN, M_SQUARE, M_SW_STROBE, M_HW_STROBE };
/* access 0..3 */
enum { AC_LATCH = 0, AC_LOW, AC_HIGH, AC_LOWHIGH };

typedef struct {
    int  mode, access, index;
    bool bcd;
    u16  count_reg, ce, out_latch;
    bool out;
    bool gate, prev_gate;
    bool latched, loaded, waiting_load, waiting_gate_edge, pulse_pending;
    int  write_phase, read_phase;
    u8   write_lo;
} PitCounter;

static PitCounter ctr[3];

static void set_out(PitCounter *c, bool v) { c->out = v; }

/* Counter 2's terminal-count event drives the RST 7.5 request line. */
static void terminal_count(PitCounter *c)
{
    if (c->index == 2)
        io_do755interrupt = c->out;
}

/* ---- BCD helper ----------------------------------------------------------*/
static u16 bcd_dec(u16 v)
{
    int n = v;
    if ((n & 0x000F) != 0) return (u16)(n - 1);
    if ((n & 0x00F0) != 0) return (u16)(((n - 0x0001) & 0xFFF0) | 0x0009);
    if ((n & 0x0F00) != 0) return (u16)(((n - 0x0001) & 0xFF00) | 0x0099);
    if ((n & 0xF000) != 0) return (u16)(((n - 0x0001) & 0xF000) | 0x0999);
    return 0x9999;
}

static void load_ce(PitCounter *c)
{
    u16 n = c->count_reg;
    if (c->bcd && n == 0) n = 0x9999;   /* binary 0 => 65536 via natural wrap */
    c->ce = n;
    c->loaded = true;
}
static void dec1(PitCounter *c) { c->ce = c->bcd ? bcd_dec(c->ce) : (u16)(c->ce - 1); }
static void dec2(PitCounter *c) { if (c->bcd) c->ce = bcd_dec(bcd_dec(c->ce)); else c->ce = (u16)(c->ce - 2); }

/* ---- control word --------------------------------------------------------*/
static void write_control_word(PitCounter *c, u8 cw)
{
    int acc = (cw >> 4) & 0x03;

    if (acc == AC_LATCH) {                     /* counter latch command */
        if (!c->latched) { c->out_latch = c->ce; c->latched = true; }
        return;
    }

    c->access = acc;
    int mode = (cw >> 1) & 0x07;
    if (mode == 6) mode = 2;
    if (mode == 7) mode = 3;
    c->mode = mode;
    c->bcd = (cw & 0x01) != 0;

    c->write_phase = 0; c->write_lo = 0; c->read_phase = 0;
    c->loaded = c->waiting_load = c->waiting_gate_edge = c->pulse_pending = false;
    c->latched = false;

    set_out(c, c->mode != M_INT_TC);   /* mode 0 -> OUT low, others high */
}

/* ---- data write / arm ----------------------------------------------------*/
static void arm(PitCounter *c)
{
    c->waiting_load = true;
    if (c->mode == M_INT_TC) set_out(c, false);
    if (c->mode == M_ONESHOT || c->mode == M_HW_STROBE) c->waiting_gate_edge = true;
}

static void counter_write_data(PitCounter *c, u8 v)
{
    switch (c->access) {
        case AC_LOW:  c->count_reg = (u16)((c->count_reg & 0xFF00) | v); arm(c); break;
        case AC_HIGH: c->count_reg = (u16)((c->count_reg & 0x00FF) | (v << 8)); arm(c); break;
        case AC_LOWHIGH:
            if (c->write_phase == 0) { c->write_lo = v; c->write_phase = 1; }
            else { c->count_reg = (u16)(c->write_lo | (v << 8)); c->write_phase = 0; arm(c); }
            break;
    }
}

static u8 counter_read_data(PitCounter *c)
{
    u16 src = c->latched ? c->out_latch : c->ce;
    u8 result;
    switch (c->access) {
        case AC_LOW:  result = (u8)(src & 0xFF); c->latched = false; break;
        case AC_HIGH: result = (u8)(src >> 8);   c->latched = false; break;
        case AC_LOWHIGH:
        default:
            if (c->read_phase == 0) { result = (u8)(src & 0xFF); c->read_phase = 1; }
            else { result = (u8)(src >> 8); c->read_phase = 0; c->latched = false; }
            break;
    }
    return result;
}

/* ---- one CLK falling edge ------------------------------------------------*/
static void clock_edge(PitCounter *c)
{
    if (!c->gate) {
        switch (c->mode) {
            case M_INT_TC: case M_RATEGEN: case M_SQUARE: case M_SW_STROBE:
                return;   /* level-low gate inhibits counting */
        }
    }
    if (c->waiting_load && !c->waiting_gate_edge) { load_ce(c); c->waiting_load = false; }
    if (!c->loaded) return;

    switch (c->mode) {
        case M_INT_TC:
            dec1(c);
            if (c->ce == 0) { set_out(c, true); terminal_count(c); }
            break;
        case M_ONESHOT:
            if (c->waiting_gate_edge) break;
            dec1(c);
            if (c->ce == 0) { set_out(c, true); terminal_count(c); }
            break;
        case M_RATEGEN:
            dec1(c);
            if (c->ce == 1) set_out(c, false);
            else if (c->ce == 0) { set_out(c, true); load_ce(c); terminal_count(c); }
            break;
        case M_SQUARE:
            dec2(c);
            if (c->ce == 0) { set_out(c, !c->out); load_ce(c); terminal_count(c); }
            break;
        case M_SW_STROBE:
            if (c->pulse_pending) { set_out(c, true); c->pulse_pending = false; break; }
            dec1(c);
            if (c->ce == 0) { set_out(c, false); c->pulse_pending = true; terminal_count(c); }
            break;
        case M_HW_STROBE:
            if (c->waiting_gate_edge) break;
            if (c->pulse_pending) { set_out(c, true); c->pulse_pending = false; break; }
            dec1(c);
            if (c->ce == 0) { set_out(c, false); c->pulse_pending = true; terminal_count(c); }
            break;
    }
}

/* ===========================================================================
 *  Public chip interface
 * ===========================================================================*/
void i8253_reset(void)
{
    for (int i = 0; i < 3; i++) {
        PitCounter z = {0};
        z.index = i;
        z.gate = true;      /* GATE tied high by default */
        ctr[i] = z;
    }
}

void i8253_clock_all(void)
{
    clock_edge(&ctr[0]);
    clock_edge(&ctr[1]);
    clock_edge(&ctr[2]);
}

void i8253_write_data(int ch, u8 v) { if (ch >= 0 && ch < 3) counter_write_data(&ctr[ch], v); }
u8   i8253_read_data(int ch)        { return (ch >= 0 && ch < 3) ? counter_read_data(&ctr[ch]) : 0xFF; }

void i8253_debug_dump(void)
{
    for (int i = 0; i < 3; i++) {
        PitCounter *c = &ctr[i];
        fprintf(stderr, "  ctr%d mode=%d access=%d count=%u ce=%u out=%d gate=%d "
                        "loaded=%d wload=%d\n",
                i, c->mode, c->access, c->count_reg, c->ce, c->out, c->gate,
                c->loaded, c->waiting_load);
    }
}

void i8253_write_control(u8 cw)
{
    int sc = (cw >> 6) & 0x03;
    if (sc == 3) return;   /* SC=11 is 8254 read-back; ignored on the 8253 */
    write_control_word(&ctr[sc], cw);
}
