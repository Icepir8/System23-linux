/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  cpu8085.h — Intel 8085 CPU state + instruction engine
 *
 *  Faithful port of the interpreter that lived in Assembler85.RunInstruction()
 *  plus the Registers class and the flag/interrupt-mask fields.  The machine
 *  run-loop calls cpu8085_step() once per instruction.
 *
 *  Models the undocumented 8085 flags (V = overflow, K/X5 = the INX/DCX
 *  sign-cross flag) and the undocumented opcodes the System/23 ROM uses.
 * ===========================================================================*/
#ifndef SYSTEM23_CPU8085_H
#define SYSTEM23_CPU8085_H

#include "system23.h"

typedef struct {
    /* Registers */
    u8  a, b, c, d, e, h, l;
    u16 pc, sp;

    /* Flags (incl. undocumented V and K) */
    bool fC, fV, fP, fAC, fK, fZ, fS;

    /* Interrupt masks / pending state (RST 5.5 / 6.5 / 7.5) + enable */
    bool m55, m65, m75;        /* SIM masks                              */
    bool ie;                   /* interrupt enable (EI/DI)               */
    bool p55, p65, p75;        /* pending                                */
    bool update_interrupts;    /* legacy flag (set, unused — kept 1:1)   */
    bool sod;                  /* serial output data (SIM bit 7)         */
    bool write_to_display;     /* set by STA 0x1800 (legacy hook)        */
    u32  rimcnt;               /* RIM counter (SIM clears it)            */
    bool f6c0_patched;         /* one-shot drive-0 presence patch        */
} Cpu8085;

extern Cpu8085       cpu;
extern u64           cpu_cycles;     /* was Assembler85.cycles            */
extern volatile bool cpu_isrunning;  /* was I8085.Isrunning               */

/* Debug interrupt-delivery counters. */
extern u64 dbg_irq8259, dbg_rst75, dbg_rst65, dbg_rst55;
extern u64 dbg_ei_count, dbg_pit75;

void cpu8085_reset(void);

/* Execute the instruction at `addr`; store the address of the next
 * instruction in *next.  Returns "" on success, or an error/halt string
 * ("System Halted", "Unknown instruction ..") which stops the run-loop. */
const char *cpu8085_step(u16 addr, u16 *next);

bool cpu8085_is_stub(void);   /* now false — the engine is real */

#endif /* SYSTEM23_CPU8085_H */
