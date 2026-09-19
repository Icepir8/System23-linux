/* ===========================================================================
 *  cpu8085.h — Intel 8085 CPU state + instruction engine
 *
 *  In the C# project the instruction interpreter lived in
 *  Assembler85.RunInstruction() and the register file in the static Registers
 *  class.  In the C port the *execution* engine belongs here (cpu8085_step),
 *  while assembler85.* keeps only the text assembler.  The machine run-loop
 *  calls cpu8085_step().
 *
 *  STATUS: stub.  cpu8085_step() is a no-op until the interpreter is ported.
 * ===========================================================================*/
#ifndef SYSTEM23_CPU8085_H
#define SYSTEM23_CPU8085_H

#include "system23.h"

typedef struct {
    u8  a, b, c, d, e, h, l;   /* general registers                          */
    u8  flags;                 /* S Z x AC x P x C                           */
    u16 pc;                    /* program counter                           */
    u16 sp;                    /* stack pointer                             */
    bool inte;                 /* interrupt enable                          */
} Cpu8085;

extern Cpu8085       cpu;
extern u64           cpu_cycles;     /* was Assembler85.cycles                */
extern volatile bool cpu_isrunning;  /* was I8085.Isrunning                   */

void cpu8085_reset(void);

/* Execute the instruction at `addr`; store the address of the next
 * instruction in *next.  Returns "" on success, or an error description. */
const char *cpu8085_step(u16 addr, u16 *next);

/* True while the interpreter is still a stub (used by the run-loop to idle
 * instead of spinning at 100% CPU).  Remove once cpu8085_step is real. */
bool cpu8085_is_stub(void);

#endif /* SYSTEM23_CPU8085_H */
