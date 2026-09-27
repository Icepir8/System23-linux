/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  i8257.c — Intel 8257 DMA controller (faithful port of I8257DMA.cs)
 *
 *  Four channels; channel 0 is wired to the FDC.  Each 16-bit address/count
 *  register is programmed a byte at a time through a shared low/high flip-flop.
 *  AssertDRQ performs the burst transfer between the peripheral (via the
 *  registered callbacks) and banked memory through the DMA page register.
 *
 *  The System/23 ROM programs channel 0 (OUT $01/$01/$00/$00) WITHOUT first
 *  resetting the flip-flop and relies on it being low, so the FDC calls
 *  dma_reset_byte_pointer() when it arms a deferred transfer.  A stray read of
 *  an address/count register also toggles the flip-flop, which would otherwise
 *  swap the low/high bytes of the programmed word.
 *
 *  The mode/status port has the 8257's split-register quirk faithfully
 *  preserved: reading returns the *status* register (and clears its low
 *  nibble — the per-channel terminal-count flags are read-once), while writing
 *  sets the *mode* register and resets the byte flip-flop.  The compound
 *  Mode_Status |= / &= in AssertDRQ therefore read status but write mode,
 *  exactly as the reference emulator does.
 * ===========================================================================*/
#include "i8257.h"
#include "memory.h"

/* Channel address/count registers (top two bits of count = transfer mode). */
u16 dma_addr[4];
u16 dma_cnt[4];

/* Fixed board wiring — set once by the FDC, left intact across resets. */
DmaReadFn     dma_periph_read[4]        = { 0, 0, 0, 0 };
DmaWriteFn    dma_periph_write[4]       = { 0, 0, 0, 0 };
DmaTcFn       dma_on_tc                 = 0;
DmaChanProgFn dma_on_channel_programmed = 0;

static u8   mode;
static u8   status;
static bool highlow;
static bool autoload;
static bool tcstop;

static const u8 active_mask[4] = { 0x01, 0x02, 0x04, 0x08 };

/* ---- mode/status port -----------------------------------------------------*/
static u8 mode_status_read(void)
{
    u8 s = status;
    status &= 0xF0;             /* read-clears the per-channel TC flags */
    return s;
}
static void mode_status_write(u8 v)
{
    mode     = v;
    autoload = (v & 0x80) != 0;
    tcstop   = (v & 0x40) != 0;
    highlow  = false;
}

/* ---- shared low/high byte flip-flop --------------------------------------*/
static void write_word(u8 in, u16 *reg)
{
    if (highlow) {
        *reg = (u16)(((u16)in << 8) | (*reg & 0x00FF));
        highlow = false;
    } else {
        *reg = (u16)(in | (*reg & 0xFF00));
        highlow = true;
    }
}
static u8 read_word(u16 reg)
{
    if (highlow) {
        highlow = false;
        return (u8)(reg >> 8);
    }
    highlow = true;
    return (u8)reg;
}

void dma_reset_byte_pointer(void) { highlow = false; }

void dma_reset(void)
{
    mode = status = 0;
    highlow = autoload = tcstop = false;
    for (int ch = 0; ch < 4; ch++) { dma_addr[ch] = 0; dma_cnt[ch] = 0; }
    /* peripheral wiring is fixed board connectivity, not runtime state. */
}

/* ---- ports 0x00-0x0F ------------------------------------------------------
 * 0x00-0x07: the eight channel registers (addr0, cnt0, addr1, cnt1, addr2,
 * cnt2, addr3, cnt3); 0x08-0x0F alias the mode/status register. */
u8 dma_port_read(int port)
{
    switch (port & 0x0F) {
        case 0x00: return read_word(dma_addr[0]);
        case 0x01: return read_word(dma_cnt[0]);
        case 0x02: return read_word(dma_addr[1]);
        case 0x03: return read_word(dma_cnt[1]);
        case 0x04: return read_word(dma_addr[2]);
        case 0x05: return read_word(dma_cnt[2]);
        case 0x06: return read_word(dma_addr[3]);
        case 0x07: return read_word(dma_cnt[3]);
        default:   return mode_status_read();
    }
}

void dma_port_write(int port, u8 value)
{
    switch (port & 0x0F) {
        case 0x00:
            write_word(value, &dma_addr[0]);
            if (!highlow && dma_on_channel_programmed)
                dma_on_channel_programmed(0, false);
            break;
        case 0x01:
            write_word(value, &dma_cnt[0]);
            if (!highlow && dma_on_channel_programmed)
                dma_on_channel_programmed(0, true);
            break;
        case 0x02: write_word(value, &dma_addr[1]); break;
        case 0x03: write_word(value, &dma_cnt[1]);  break;
        case 0x04:
            write_word(value, &dma_addr[2]);
            if (autoload) dma_addr[3] = dma_addr[2];
            break;
        case 0x05:
            write_word(value, &dma_cnt[2]);
            if (autoload) dma_cnt[3] = dma_cnt[2];
            break;
        case 0x06: write_word(value, &dma_addr[3]); break;
        case 0x07: write_word(value, &dma_cnt[3]);  break;
        default:   mode_status_write(value);        break;
    }
}

/* ---- the burst transfer ---------------------------------------------------*/
void dma_assert_drq(int ch, bool active)
{
    if (!active) {
        mode_status_write((u8)(mode_status_read() & (u8)~active_mask[ch]));
        return;
    }
    mode_status_write((u8)(mode_status_read() | active_mask[ch]));

    u16 addr = dma_addr[ch];
    int cnt   = dma_cnt[ch] & 0x3FFF;

    switch (dma_cnt[ch] & 0xC000) {
        case 0x0000:                       /* validate — no transfer */
            break;
        case 0x4000:                       /* peripheral -> memory (disk read) */
            do {
                u8 val = dma_periph_read[ch] ? dma_periph_read[ch](ch) : 0xFF;
                memory_dma_write(addr++, val);
            } while (--cnt > 0);
            dma_addr[ch] = addr;
            dma_cnt[ch] &= 0xC000;
            break;
        case 0x8000:                       /* memory -> peripheral (disk write) */
            do {
                u8 val = memory_dma_read(addr++);
                if (dma_periph_write[ch]) dma_periph_write[ch](ch, val);
            } while (--cnt > 0);
            dma_addr[ch] = addr;
            dma_cnt[ch] &= 0xC000;
            break;
        default:                           /* illegal (0xC000) — ignore */
            break;
    }

    if (tcstop)
        mode_status_write((u8)(mode_status_read() & (u8)~active_mask[ch]));

    status |= active_mask[ch];

    if (dma_on_tc) dma_on_tc(ch);
}
