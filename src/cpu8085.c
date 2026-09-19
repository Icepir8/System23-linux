/* ===========================================================================
 *  cpu8085.c — 8085 state + instruction engine (STUB)
 *
 *  TODO: port the opcode interpreter from Assembler85.cs (RunInstruction) and
 *  the register/flag helpers from the Registers class.  Read/write memory via
 *  memory_read/memory_write and raise interrupts via the 8259.
 * ===========================================================================*/
#include "cpu8085.h"

Cpu8085       cpu          = {0};
u64           cpu_cycles   = 0;
volatile bool cpu_isrunning = false;

void cpu8085_reset(void)
{
    cpu = (Cpu8085){0};
    cpu.pc = 0x0000;   /* 8085 reset vector */
    cpu_cycles = 0;
}

const char *cpu8085_step(u16 addr, u16 *next)
{
    /* STUB: no execution yet.  Leave PC where it is so the run-loop can idle. */
    if (next)
        *next = addr;
    return "";
}

bool cpu8085_is_stub(void)
{
    return true;
}
