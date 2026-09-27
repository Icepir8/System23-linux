/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Owen V. Michael, Jr.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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
