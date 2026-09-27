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
 *  i8259.h — Intel 8259 programmable interrupt controller (port of I8259PIC.cs)
 *  STATUS: stub.  The Display's keyboard path calls i8259_assert_irq(0).
 * ===========================================================================*/
#ifndef SYSTEM23_I8259_H
#define SYSTEM23_I8259_H

#include "system23.h"

extern volatile bool i8259_pending;   /* was I8259PIC.PendingInterrupt */

void i8259_assert_irq(int irq_line);
void i8259_deassert_irq(int irq_line);
void i8259_reset(void);

/* Acknowledge the highest-priority pending interrupt and return the 8085
 * vector address to jump to (was I8259PIC.InterruptAcknowledge8085). */
u16  i8259_interrupt_acknowledge(void);

/* Command/data I/O ports (0x28 / 0x29).  Minimal for now — accepts the ICW
 * init sequence and OCW1 (mask); full priority resolution is TODO. */
void i8259_write_command(u8 v);
void i8259_write_data(u8 v);
u8   i8259_read_command(void);
u8   i8259_read_data(void);

void i8259_debug_dump(void);   /* print IRR/ISR/IMR/base/init (debug) */

#endif /* SYSTEM23_I8259_H */
