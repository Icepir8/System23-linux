/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
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
