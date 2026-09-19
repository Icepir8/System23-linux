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

#endif /* SYSTEM23_I8259_H */
