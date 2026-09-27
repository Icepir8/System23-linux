/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
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
