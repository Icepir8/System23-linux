/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  i8259.c — Intel 8259A PIC (faithful port of I8259PIC.cs)
 *
 *  IRR/ISR/IMR, the ICW init sequence, OCW1/2/3, priority resolution with
 *  rotation and special-mask mode, and the 8085 interrupt-acknowledge that
 *  returns the CALL vector (vector_addr & 0xFFC0) | (irq<<3).
 * ===========================================================================*/
#include "i8259.h"
#include <stdio.h>

/* ICW1 */
#define ICW1_ICW4   0x01
#define ICW1_SINGLE 0x02
#define ICW1_LTIM   0x08
#define ICW1_INIT   0x10
/* ICW4 */
#define ICW4_8086   0x01
#define ICW4_AEOI   0x02
#define ICW4_SFNM   0x10
/* OCW2 */
#define OCW2_EOI    0x20
#define OCW2_SL     0x40
#define OCW2_ROTATE 0x80
/* OCW3 */
#define OCW3_RIS    0x01
#define OCW3_RR     0x02
#define OCW3_SMM    0x20
#define OCW3_ESMM   0x40

volatile bool i8259_pending = false;    /* was I8259PIC.PendingInterrupt */

static u8   irr, isr, imr;
static u8   vector_base, cascade_id;
static bool icw4_needed, single_mode, level_triggered;
static bool auto_eoi, mode8086, special_fully_nested, special_mask_mode, read_isr;
static u32  vector_addr;                 /* System23vectoraddr */
static int  lowest_priority = 7;

enum { INIT_READY = 0, INIT_ICW2, INIT_ICW3, INIT_ICW4 };
static int init_step = INIT_READY;

/* ---- priority helpers ----------------------------------------------------*/
static int priority_of(int irq) { return (irq - lowest_priority - 1 + 8) & 7; }

static int highest_isr_bit(void)
{
    for (int i = 1; i <= 8; i++) {
        int irq = (lowest_priority + i) & 7;
        if (isr & (1 << irq)) return irq;
    }
    return -1;
}
static int lowest_priority_isr_bit(void)
{
    for (int i = 8; i >= 1; i--) {
        int irq = (lowest_priority + i) & 7;
        if (isr & (1 << irq)) return irq;
    }
    return -1;
}

static int highest_priority_pending_irq(void)
{
    u8 pending = (u8)(irr & ~imr);
    if (pending == 0) return -1;

    int isr_priority = special_fully_nested ? lowest_priority_isr_bit()
                                            : highest_isr_bit();

    for (int i = 1; i <= 8; i++) {
        int irq = (lowest_priority + i) & 7;
        if ((pending & (1 << irq)) == 0) continue;
        if (special_mask_mode) return irq;
        if (isr_priority < 0) return irq;
        if (priority_of(irq) < priority_of(isr_priority)) return irq;
        return -1;
    }
    return -1;
}

static void evaluate_interrupts(void)
{
    i8259_pending = highest_priority_pending_irq() >= 0;
}

static void end_of_interrupt(bool specific, int irq_line)
{
    if (specific && irq_line >= 0) {
        isr &= (u8)~(1 << irq_line);
    } else {
        int bit = highest_isr_bit();
        if (bit >= 0) isr &= (u8)~(1 << bit);
    }
}

/* ---- init sequence -------------------------------------------------------*/
static void begin_initialization(u8 icw1)
{
    i8259_reset();
    icw4_needed     = (icw1 & ICW1_ICW4) != 0;
    single_mode     = (icw1 & ICW1_SINGLE) != 0;
    level_triggered = (icw1 & ICW1_LTIM) != 0;
    vector_addr     = (u32)(icw1 & 0xE0);
    init_step       = INIT_ICW2;
}
static void set_icw2(u8 icw2)
{
    vector_addr |= (u32)(icw2 << 8);
    vector_base  = (u8)(icw2 & 0xF8);
    init_step = single_mode ? (icw4_needed ? INIT_ICW4 : INIT_READY) : INIT_ICW3;
}
static void set_icw3(u8 icw3)
{
    cascade_id = icw3;
    init_step = icw4_needed ? INIT_ICW4 : INIT_READY;
}
static void set_icw4(u8 icw4)
{
    mode8086 = (icw4 & ICW4_8086) != 0;
    auto_eoi = (icw4 & ICW4_AEOI) != 0;
    special_fully_nested = (icw4 & ICW4_SFNM) != 0;
    init_step = INIT_READY;
}

/* ---- OCW ------------------------------------------------------------------*/
static void process_ocw2(u8 ocw2)
{
    bool eoi = (ocw2 & OCW2_EOI) != 0;
    bool sl  = (ocw2 & OCW2_SL) != 0;
    bool rotate = (ocw2 & OCW2_ROTATE) != 0;
    int  level = ocw2 & 0x07;

    if (eoi) {
        end_of_interrupt(sl, sl ? level : highest_isr_bit());
        if (rotate) {
            int cleared = sl ? level : highest_isr_bit();
            if (cleared >= 0) lowest_priority = cleared;
        }
    } else if (rotate && sl) {
        lowest_priority = level;
    } else if (!rotate && !sl) {
        lowest_priority = 7;
    }
    evaluate_interrupts();
}
static void process_ocw3(u8 ocw3)
{
    if (ocw3 & OCW3_ESMM) special_mask_mode = (ocw3 & OCW3_SMM) != 0;
    if (ocw3 & OCW3_RR)   read_isr = (ocw3 & OCW3_RIS) != 0;
}

/* ===========================================================================
 *  Public API
 * ===========================================================================*/
void i8259_write_command(u8 value)
{
    if (value & ICW1_INIT) {
        begin_initialization(value);
    } else if (init_step != INIT_READY) {
        /* stray command during init — ignore */
    } else if ((value & 0x18) == 0x00) {
        process_ocw2(value);
    } else if ((value & 0x18) == 0x08) {
        process_ocw3(value);
    }
}

void i8259_write_data(u8 value)
{
    switch (init_step) {
        case INIT_ICW2: set_icw2(value); break;
        case INIT_ICW3: set_icw3(value); break;
        case INIT_ICW4: set_icw4(value); break;
        default:
            imr = value;          /* OCW1 mask */
            evaluate_interrupts();
            break;
    }
}

u8 i8259_read_command(void) { return read_isr ? isr : irr; }
u8 i8259_read_data(void)    { return imr; }

void i8259_assert_irq(int irq_line)
{
    if (irq_line < 0 || irq_line > 7) return;
    irr |= (u8)(1 << irq_line);
    evaluate_interrupts();
}
void i8259_deassert_irq(int irq_line)
{
    if (irq_line < 0 || irq_line > 7) return;
    irr &= (u8)~(1 << irq_line);
    evaluate_interrupts();
}

u16 i8259_interrupt_acknowledge(void)
{
    int irq = highest_priority_pending_irq();
    if (irq < 0) return 0xFF;

    irr &= (u8)~(1 << irq);
    isr |= (u8)(1 << irq);

    u16 vector = (u16)((vector_addr & 0xFFC0) | (irq << 3));

    if (auto_eoi) end_of_interrupt(true, irq);
    evaluate_interrupts();
    return vector;
}

void i8259_debug_dump(void)
{
    fprintf(stderr, "  8259: IRR=%02X ISR=%02X IMR=%02X base=%04X init_step=%d pending=%d\n",
            irr, isr, imr, vector_addr, init_step, i8259_pending);
}

void i8259_reset(void)
{
    irr = isr = imr = 0;
    vector_base = cascade_id = 0;
    icw4_needed = single_mode = level_triggered = false;
    auto_eoi = mode8086 = special_fully_nested = special_mask_mode = read_isr = false;
    vector_addr = 0;
    lowest_priority = 7;
    init_step = INIT_READY;
    i8259_pending = false;
}
