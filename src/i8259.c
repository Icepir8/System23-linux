/* ===========================================================================
 *  i8259.c — 8259 PIC (STUB)
 *  TODO: port IRR/ISR/IMR, priority resolution and InterruptAcknowledge from
 *  I8259PIC.cs (master + slave cascade on line 2).
 * ===========================================================================*/
#include "i8259.h"

volatile bool i8259_pending = false;

void i8259_assert_irq(int irq_line)
{
    (void)irq_line;
    /* STUB: record that something wants to interrupt so the CPU port, once
     * implemented, can service it. */
    i8259_pending = true;
}

void i8259_deassert_irq(int irq_line)
{
    (void)irq_line;
}

void i8259_reset(void)
{
    i8259_pending = false;
}
