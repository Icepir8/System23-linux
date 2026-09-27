/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  ioports.h — 8085 I/O port dispatch (port of IOports.cs)
 *
 *  Central read/write dispatch for every device wired to the I/O and memory
 *  page-register space, plus the shared operator-facing state the UI touches
 *  (keyboard scancode, character-page register, diagnostic port, region).
 *
 *  STATUS: stub.  The read/write dispatch and per-device wiring are TODO; the
 *  shared state fields below are real so the Display can already feed the
 *  keyboard path.
 * ===========================================================================*/
#ifndef SYSTEM23_IOPORTS_H
#define SYSTEM23_IOPORTS_H

#include "system23.h"

/* Shared machine state referenced by the UI / interrupt path. */
extern u8        io_kbscancode;        /* last key scancode handed to the KB  */
extern u32       io_char_page_read;    /* CharPageRead — display char bank    */
extern u8        io_diagnostic_port;   /* Diagnosticport                      */
extern bool      io_past_post;         /* true once a real keystroke arrived  */
extern Countries io_country;           /* keyboard region / language          */
extern bool      io_sid;               /* serial input data (read by RIM)     */

/* Interrupt requests raised by devices, consumed by the CPU after each
 * instruction (RST 5.5 / INTR / RST 7.5 paths).  Set false by the CPU. */
extern bool      io_do555interrupt;
extern bool      io_do655interrupt;
extern bool      io_do755interrupt;

/* Advance the printer UART timing by `cycles` (PrinterWrapAdapter.Tick). */
void ioports_uart_tick(int cycles);

/* I/O dispatch (TODO: wire to the individual device emulations). */
u8   ioports_read(u16 port);
bool ioports_write(u16 port, u8 value);

/* Dump the last ~96 I/O accesses to stderr (debugging aid). */
void ioports_dump_ring(void);

/* Reset helpers matching the original IOports surface. */
void ioports_reset(void);        /* full device reset                        */
void ioports_reset_pit(void);    /* recreate just the 8253 PIT (warm reset)  */
void ioports_reset_system(void); /* full hardware init                       */

/* Human-friendly region name for the language selector. */
const char *country_name(Countries c);

#endif /* SYSTEM23_IOPORTS_H */
