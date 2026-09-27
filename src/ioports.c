/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  ioports.c — 8085 I/O port dispatch (port of IOports.cs)
 *
 *  ReadIOport / WriteIOport route the CPU's IN/OUT to the devices and the
 *  memory page registers.  The three 8255 PPIs are modelled inline as the
 *  original's per-port handlers (config switches, keyboard, the data-bus test
 *  / char-ROM page latch).  DMA channel ports are simple latches; the CRTC,
 *  PIT and PIC are handled by their own modules.  SASI / FDC / USART are still
 *  stubbed (return 0x00 / ignore).
 * ===========================================================================*/
#include "ioports.h"
#include "memory.h"
#include "i8275.h"
#include "i8253.h"
#include "i8259.h"
#include "i8257.h"
#include "i765a_fdc.h"
#include "i8251.h"

#include <stdio.h>

/* ---- shared machine state ------------------------------------------------*/
u8        io_kbscancode      = 0;
u32       io_char_page_read  = 0;
u8        io_diagnostic_port = 0xFF;
bool      io_past_post       = false;
Countries io_country         = COUNTRY_USA;
bool      io_sid             = true;

bool      io_do555interrupt  = false;
bool      io_do655interrupt  = false;
bool      io_do755interrupt  = false;

/* ---- 8255 port backing stores + DIP/config latches -----------------------*/
static u8  port1data[4];                 /* 8255A (config switches)          */
static u8  port2data[4];                 /* 8255B (keyboard)                 */
static u8  port3data[4];                 /* 8255C (data-bus test / char page)*/
static u8  floppy_cnfig = 0xC0;
static int cnt4d = 0;

/* 8257 DMA is a real device now (see i8257.c); ports 0x00-0x0F route to it. */

/* ---- optional I/O access ring (SYSTEM23_IOTRACE) — a debugging aid --------*/
#define IO_RING 96
static struct { u16 port; u8 val; u8 wr; } io_ring[IO_RING];
static unsigned io_ring_n = 0;
static void io_ring_put(u16 port, u8 val, int wr)
{
    io_ring[io_ring_n % IO_RING].port = port;
    io_ring[io_ring_n % IO_RING].val  = val;
    io_ring[io_ring_n % IO_RING].wr   = (u8)wr;
    io_ring_n++;
}
void ioports_dump_ring(void)
{
    unsigned start = io_ring_n > IO_RING ? io_ring_n - IO_RING : 0;
    for (unsigned i = start; i < io_ring_n; i++) {
        int k = (int)(i % IO_RING);
        fprintf(stderr, "  %s %02X = %02X\n",
                io_ring[k].wr ? "OUT" : "IN ", io_ring[k].port, io_ring[k].val);
    }
}

/* ===========================================================================
 *  8255 per-port handlers (mirror IOports.cs)
 * ===========================================================================*/
/* 8255A — language/region + memory-config switches (read-mostly). */
static u8  ppiA_portA_read(void) { return (u8)(((int)io_country ^ 0xFF) & 0xBF); }
static u8  ppiA_portB_read(void) { return 0xF0; }
static u8  ppiA_portC_read(void) { return 0x8F; }

/* 8255B — keyboard. */
static u8 ppiB_portA_read(void)
{
    port2data[0] = io_kbscancode;
    if (io_kbscancode != 0) io_past_post = true;   /* first keystroke => past POST */
    io_kbscancode = 0;
    return port2data[0];
}
static void ppiB_portB_write(u8 v)
{
    io_diagnostic_port = v;
    port2data[1] = v;
    if (v == 0x34) {
        memory_write(0xA100, 0x2A);
        memory_write(0xA101, 0x55);
        memory_write(0xA102, 0x00);
    } else if (v == 0x37) {
        memory_write(0xA009, 0x00);
    }
}

/* 8255C — data-bus test (PortA latch) / rotating floppy config (PortB). */
static u8 ppiC_portB_read(void)
{
    if (cnt4d++ > 2) {
        port3data[2] = (u8)(floppy_cnfig | ((port3data[2] + 2) & 0x1E));
        return port3data[2];   /* bit6 (drive-0 presence) intentionally left 0 */
    }
    return 0xFF;
}
static void ppiC_portA_write(u8 v)
{
    io_char_page_read = v;     /* char-ROM pagination */
    port3data[0] = v;
}

/* ===========================================================================
 *  ReadIOport
 * ===========================================================================*/
static u8 ioports_read_impl(u16 port)
{
    switch (port) {
        /* 8257 DMA channels + mode/status */
        case 0x00: case 0x01: case 0x02: case 0x03:
        case 0x04: case 0x05: case 0x06: case 0x07:
        case 0x08: case 0x09: case 0x0A: case 0x0B:
        case 0x0C: case 0x0D: case 0x0E: case 0x0F: return dma_port_read(port);

        /* memory page registers */
        case 0x20: return (u8)mem_dma_page;
        case 0x21: return (u8)mem_ram_page_write;
        case 0x22: return (u8)mem_ram_page_read;
        case 0x23: return (u8)mem_rom_page;

        /* 8253 PIT */
        case 0x24: return i8253_read_data(0);
        case 0x25: return i8253_read_data(1);
        case 0x26: return i8253_read_data(2);
        case 0x27: return 0x00;

        /* 8259 PIC */
        case 0x28: return i8259_read_command();
        case 0x29: return i8259_read_data();

        /* 8255A (config switches) */
        case 0x2C: return ppiA_portA_read();
        case 0x2D: return ppiA_portB_read();
        case 0x2E: return ppiA_portC_read();
        case 0x2F: return 0xFF;              /* control */

        /* 8255B (keyboard) */
        case 0x40: return ppiB_portA_read();
        case 0x41: return port2data[1];
        case 0x42: return 0xFF;
        case 0x43: return 0xFF;              /* control */

        /* 8275 CRTC */
        case 0x44: return i8275_preg_read();
        case 0x45: return i8275_csreg_read();

        /* 8255C (data-bus test / char page / floppy config) */
        case 0x48: case 0x49: return uart_read_port((u8)port);  /* 8251 USART */

        case 0x4C: return port3data[0];      /* data-bus test read-back */
        case 0x4D: return ppiC_portB_read();
        case 0x4E: return 0x00;
        case 0x4F: return 0xFF;              /* control */

        /* Floppy subsystem (NEC765 + 8748 + FDC-card 8255). */
        default:
            if (port >= 0xF0 && port <= 0xFB) return fdc_read_port((u8)port);
            return 0x00;   /* USART / SASI — TODO */
    }
}

/* ===========================================================================
 *  WriteIOport
 * ===========================================================================*/
static bool ioports_write_impl(u16 port, u8 value)
{
    switch (port) {
        case 0x00: case 0x01: case 0x02: case 0x03:
        case 0x04: case 0x05: case 0x06: case 0x07:
        case 0x08: case 0x09: case 0x0A: case 0x0B:
        case 0x0C: case 0x0D: case 0x0E: case 0x0F: dma_port_write(port, value); break;

        /* memory page registers */
        case 0x20: mem_dma_page       = value; break;
        case 0x21: mem_ram_page_write = value; break;
        case 0x22: mem_ram_page_read  = value; break;
        case 0x23: mem_rom_page       = value; break;

        /* 8253 PIT */
        case 0x24: i8253_write_data(0, value); break;
        case 0x25: i8253_write_data(1, value); break;
        case 0x26:
            if (io_diagnostic_port == 0x33 && value == 0x0F) value = 3;
            i8253_write_data(2, value);
            break;
        case 0x27: i8253_write_control(value); break;

        /* 8259 PIC */
        case 0x28: i8259_write_command(value); break;
        case 0x29: i8259_write_data(value); break;

        /* 8255A */
        case 0x2C: port1data[0] = value; break;
        case 0x2D: port1data[1] = value; break;
        case 0x2E: port1data[2] = value; break;
        case 0x2F: port1data[3] = value; break;

        /* 8255B */
        case 0x40: port2data[0] = value; break;
        case 0x41: ppiB_portB_write(value); break;
        case 0x42: port2data[2] = value; break;
        case 0x43: port2data[3] = value; break;

        /* 8275 CRTC */
        case 0x44: i8275_preg_write(value); break;
        case 0x45: i8275_csreg_write(value); break;

        case 0x48: case 0x49: uart_write_port((u8)port, value); break;  /* 8251 USART */

        /* 8255C */
        case 0x4C: ppiC_portA_write(value); break;
        case 0x4D: port3data[1] = value; break;
        case 0x4E: port3data[2] = value; break;
        case 0x4F: port3data[3] = value; break;

        /* Floppy subsystem (NEC765 + 8748 + FDC-card 8255). */
        default:
            if (port >= 0xF0 && port <= 0xFB) { fdc_write_port((u8)port, value); return true; }
            return false;   /* USART / SASI — TODO */
    }
    return true;
}

/* Public entry points — record each access into the debug ring, then dispatch. */
u8 ioports_read(u16 port)
{
    u8 v = ioports_read_impl(port);
    io_ring_put(port, v, 0);
    return v;
}
bool ioports_write(u16 port, u8 value)
{
    io_ring_put(port, value, 1);
    return ioports_write_impl(port, value);
}

/* ===========================================================================
 *  Resets / misc
 * ===========================================================================*/
void ioports_reset(void)
{
    for (int i = 0; i < 4; i++) { port1data[i] = 0; port2data[i] = 0; port3data[i] = 0; }
    floppy_cnfig = 0xC0;
    cnt4d = 0;
    io_char_page_read = 0;
    io_diagnostic_port = 0xFF;
    io_kbscancode = 0;
    io_do555interrupt = io_do655interrupt = io_do755interrupt = false;
    io_sid = true;
    io_past_post = false;
    i8253_reset();
}

void ioports_reset_pit(void)
{
    i8253_reset();
    io_do755interrupt = false;
}

void ioports_reset_system(void)
{
    ioports_reset();
    i8259_reset();
    i8275_reset();
    memory_reset();
    /* TODO: DMA / FDC / SASI resets once ported. */
}

void ioports_uart_tick(int cycles)
{
    (void)cycles;
    uart_tick();
}

const char *country_name(Countries c)
{
    static const char *const names[COUNTRY_COUNT] = {
        "USA", "Austria/Germany", "Italy", "Spain (Spanish)",
        "United Kingdom", "Switzerland (German)", "Canada (French)",
        "Norway", "France", "Belgium", "Switzerland (French)", "Sweden",
        "International", "Denmark", "Belgium (French)", "Finland", "Japan",
    };
    return (c >= 0 && c < COUNTRY_COUNT) ? names[c] : "?";
}
