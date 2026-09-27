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
 *  i8253.h — Intel 8253 programmable interval timer (port of I8253PIT.cs)
 *
 *  Three independent 16-bit down-counters clocked from the CPU (the engine
 *  calls i8253_clock_all() every ~0x41 cycles).  Counter 2's terminal count
 *  drives the RST 7.5 request (io_do755interrupt), matching the original wiring.
 * ===========================================================================*/
#ifndef SYSTEM23_I8253_H
#define SYSTEM23_I8253_H

#include "system23.h"

void i8253_reset(void);
void i8253_clock_all(void);            /* one CLK falling edge to all counters */
void i8253_debug_dump(void);           /* print counter states (debug) */

void i8253_write_data(int ch, u8 v);   /* ports 0x24/0x25/0x26 write           */
u8   i8253_read_data(int ch);          /* ports 0x24/0x25/0x26 read            */
void i8253_write_control(u8 cw);       /* port 0x27 write (control word)       */

#endif /* SYSTEM23_I8253_H */
