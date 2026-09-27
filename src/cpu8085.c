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
 *  cpu8085.c — Intel 8085 instruction engine (port of Assembler85.RunInstruction)
 *
 *  Reads/writes the banked memory via memory_*(), I/O via ioports_*(), and
 *  vectors interrupts through the 8259 / RST 5.5-7.5 lines.  Cycle counts,
 *  flag behaviour and a few original quirks (JC/CMA add no cycles; INR/DCR
 *  preserve carry; PSW carries only S/Z/AC/P/C) are reproduced 1:1.
 * ===========================================================================*/
#include "cpu8085.h"
#include "memory.h"
#include "ioports.h"
#include "i8259.h"
#include "i8253.h"
#include "floppy.h"

Cpu8085       cpu           = {0};
u64           cpu_cycles    = 0;
volatile bool cpu_isrunning = false;

/* Debug interrupt-delivery counters (a boot-diagnostic aid). */
u64 dbg_irq8259 = 0, dbg_rst75 = 0, dbg_rst65 = 0, dbg_rst55 = 0;
u64 dbg_ei_count = 0, dbg_pit75 = 0;

static u64 cyclesav = 0;   /* PIT clock pacing accumulator */

bool cpu8085_is_stub(void) { return false; }

void cpu8085_reset(void)
{
    cpu = (Cpu8085){0};
    cpu.pc = 0x0000;
    cpu_cycles = 0;
    cyclesav = 0;
}

/* ---- ALU ------------------------------------------------------------------
 *  Matches Assembler85.Calculate(byte,...) exactly, including the undocumented
 *  V (overflow) flag; K is cleared here and only set by INX/DCX. */
enum { OP_ADD = 1, OP_SUB, OP_AND, OP_OR, OP_XOR };

static u8 alu8(u8 a, u8 b, u8 carry, int op)
{
    int result = 0;
    cpu.fV = false;
    cpu.fK = false;

    switch (op) {
        case OP_ADD:
            result = a + b + carry;
            cpu.fC = (a + b + carry) > 0xFF;
            cpu.fAC = ((a & 0x0F) + (b & 0x0F) + carry) > 0x0F;
            if (a >= 0x80 && b >= 0x80 && (u8)result <  0x80) cpu.fV = true;
            if (a >= 0x80 && b <  0x80) cpu.fV = false;
            if (a <  0x80 && b >= 0x80) cpu.fV = false;
            if (a <  0x80 && b <  0x80 && (u8)result >= 0x80) cpu.fV = true;
            break;
        case OP_SUB:
            result = a - b - carry;
            cpu.fC = (a - b - carry) < 0;
            cpu.fAC = ((a & 0x0F) - (b & 0x0F) - carry) < 0;
            if (a >= 0x80 && b >= 0x80) cpu.fV = false;
            if (a >= 0x80 && b <  0x80 && (u8)result <  0x80) cpu.fV = true;
            if (a <  0x80 && b >= 0x80 && (u8)result >= 0x80) cpu.fV = true;
            if (a <  0x80 && b <  0x80) cpu.fV = false;
            break;
        case OP_AND: result = a & b; cpu.fC = false; cpu.fAC = true;  break;
        case OP_OR:  result = a | b; cpu.fC = false; cpu.fAC = false; break;
        case OP_XOR: result = a ^ b; cpu.fC = false; cpu.fAC = false; break;
    }

    u8 r = (u8)result;
    cpu.fS = (r & 0x80) != 0;
    cpu.fZ = (r == 0);
    int p = r; p ^= p >> 1; p ^= p >> 2; p ^= p >> 4;
    cpu.fP = (p & 1) == 0;
    return r;
}

/* 16-bit ALU: ADD (DAD) sets only C; SUB (DSUB) sets C,S,Z,P. */
static u16 alu16(u16 a, u16 b, u16 carry, int op)
{
    int result = 0;
    cpu.fV = false;
    cpu.fK = false;

    if (op == OP_ADD) {
        result = a + b + carry;
        cpu.fC = (a + b + carry) > 0xFFFF;
    } else { /* OP_SUB */
        result = a - b - carry;
        cpu.fC = (a - b - carry) < 0;
        u16 r = (u16)result;
        cpu.fS = (r & 0x8000) != 0;
        cpu.fZ = (r == 0);
        int p = r; p ^= p >> 1; p ^= p >> 2; p ^= p >> 4; p ^= p >> 8;
        cpu.fP = (p & 1) == 0;
    }
    return (u16)result;
}

/* ---- register pairs + register-field access ------------------------------*/
static u16 rp_bc(void) { return (u16)((cpu.b << 8) | cpu.c); }
static u16 rp_de(void) { return (u16)((cpu.d << 8) | cpu.e); }
static u16 rp_hl(void) { return (u16)((cpu.h << 8) | cpu.l); }
static void set_bc(u16 v) { cpu.b = (u8)(v >> 8); cpu.c = (u8)v; }
static void set_de(u16 v) { cpu.d = (u8)(v >> 8); cpu.e = (u8)v; }
static void set_hl(u16 v) { cpu.h = (u8)(v >> 8); cpu.l = (u8)v; }

/* 3-bit register code: 0=B 1=C 2=D 3=E 4=H 5=L 6=M(=[HL]) 7=A */
static u8 reg_get(int code)
{
    switch (code & 7) {
        case 0: return cpu.b; case 1: return cpu.c;
        case 2: return cpu.d; case 3: return cpu.e;
        case 4: return cpu.h; case 5: return cpu.l;
        case 6: return memory_read(rp_hl());
        default: return cpu.a;
    }
}
static void reg_set(int code, u8 v)
{
    switch (code & 7) {
        case 0: cpu.b = v; break; case 1: cpu.c = v; break;
        case 2: cpu.d = v; break; case 3: cpu.e = v; break;
        case 4: cpu.h = v; break; case 5: cpu.l = v; break;
        case 6: memory_write(rp_hl(), v); cpu_cycles += 1; break;
        default: cpu.a = v; break;
    }
}

/* ---- stack ---------------------------------------------------------------*/
static void push16(u16 v)
{
    cpu.sp--; memory_write(cpu.sp, (u8)(v >> 8));
    cpu.sp--; memory_write(cpu.sp, (u8)v);
}
static u16 pop16(void)
{
    u16 lo = memory_read(cpu.sp); cpu.sp++;
    u16 hi = memory_read(cpu.sp); cpu.sp++;
    return (u16)(lo | (hi << 8));
}

/* Immediate 16-bit operand (bytes after the opcode). */
#define IMM16() ((u16)(memory_read((u16)(cpu.pc + 1)) | \
                       (memory_read((u16)(cpu.pc + 2)) << 8)))
#define IMM8()  (memory_read((u16)(cpu.pc + 1)))

/* ===========================================================================
 *  Instruction step
 * ===========================================================================*/
const char *cpu8085_step(u16 addr, u16 *next)
{
    cpu.pc = addr;
    u8 op = memory_read(cpu.pc);
    const char *err = "";

    /* ---- MOV r,r / r,M / M,r  (0x40-0x7F, except 0x76 = HLT) ------------- */
    if (op >= 0x40 && op <= 0x7F && op != 0x76) {
        int dst = (op >> 3) & 7, src = op & 7;
        u8 v = reg_get(src);
        if (dst == 6) memory_write(rp_hl(), v);   /* MOV M,r */
        else          reg_set(dst, v);            /* MOV r,r / r,M */
        cpu.pc++;
        cpu_cycles += (src == 6 || dst == 6) ? 7 : 4;
    }
    /* ---- ALU A,r group  (0x80-0xBF) ------------------------------------- */
    else if (op >= 0x80 && op <= 0xBF) {
        int src = op & 7, grp = (op >> 3) & 7;
        u8 v = reg_get(src);
        u8 cy = cpu.fC ? 1 : 0;
        switch (grp) {
            case 0: cpu.a = alu8(cpu.a, v, 0,  OP_ADD); break;  /* ADD */
            case 1: cpu.a = alu8(cpu.a, v, cy, OP_ADD); break;  /* ADC */
            case 2: cpu.a = alu8(cpu.a, v, 0,  OP_SUB); break;  /* SUB */
            case 3: cpu.a = alu8(cpu.a, v, cy, OP_SUB); break;  /* SBB */
            case 4: cpu.a = alu8(cpu.a, v, 0,  OP_AND); break;  /* ANA */
            case 5: cpu.a = alu8(cpu.a, v, 0,  OP_XOR); break;  /* XRA */
            case 6: cpu.a = alu8(cpu.a, v, 0,  OP_OR ); break;  /* ORA */
            default:      alu8(cpu.a, v, 0,  OP_SUB); break;    /* CMP (discard) */
        }
        cpu.pc++;
        cpu_cycles += 4;
        if (src == 6) cpu_cycles += 3;
    }
    else switch (op) {
        /* ---- misc / control ---- */
        case 0x00: cpu.pc++; cpu_cycles += 4; break;                 /* NOP */
        case 0x76: cpu_cycles += 5; err = "System Halted"; break;    /* HLT */
        case 0x2F: cpu.a = (u8)(0xFF - cpu.a); cpu.pc++; break;       /* CMA (no cyc) */
        case 0x37: cpu.fC = true;  cpu.pc++; cpu_cycles += 4; break;  /* STC */
        case 0x3F: cpu.fC = !cpu.fC; cpu.pc++; cpu_cycles += 4; break;/* CMC */
        case 0xF3: cpu.ie = false; cpu.pc++; cpu_cycles += 4; break;  /* DI */
        case 0xFB: cpu.ie = true;  cpu.pc++; cpu_cycles += 4; dbg_ei_count++; break;  /* EI */
        case 0xEB: { u8 t; t=cpu.d; cpu.d=cpu.h; cpu.h=t;             /* XCHG */
                     t=cpu.l; cpu.l=cpu.e; cpu.e=t;
                     cpu.pc++; cpu_cycles += 4; } break;

        /* ---- DAA ---- */
        case 0x27: {
            u8 low = cpu.a & 0x0F, high = cpu.a & 0xF0;
            if (low > 0x09 || cpu.fAC) {
                low += 0x06;
                if (low > 0x0F) { if (high == 0xF0) cpu.fC = true; high += 0x10; low &= 0x0F; }
            }
            if (high > 0x90 || cpu.fC) { cpu.fC = true; high += 0x60; }
            cpu.a = (u8)(high + low);
            cpu.pc++; cpu_cycles += 4;
        } break;

        /* ---- rotates ---- */
        case 0x07: cpu.fC = (cpu.a & 0x80) != 0;                     /* RLC */
                   cpu.a = (u8)(cpu.a << 1); if (cpu.fC) cpu.a |= 0x01;
                   cpu.pc++; cpu_cycles += 4; break;
        case 0x0F: cpu.fC = (cpu.a & 0x01) != 0;                     /* RRC */
                   cpu.a = (u8)(cpu.a >> 1); if (cpu.fC) cpu.a |= 0x80;
                   cpu.pc++; cpu_cycles += 4; break;
        case 0x17: { u8 ac = cpu.a, sc = cpu.fC ? 1 : 0; u8 prev = ac; /* RAL */
                     ac = (u8)(ac * 2); cpu.fC = ac < prev; ac += sc;
                     cpu.a = ac; cpu.pc++; cpu_cycles += 4; } break;
        case 0x1F: { u8 ac = cpu.a, sc = cpu.fC ? 1 : 0;             /* RAR */
                     cpu.fC = (ac & 0x01) == 0x01; ac = (u8)(ac / 2);
                     ac += (u8)(sc * 0x80); cpu.a = ac; cpu.pc++; cpu_cycles += 4; } break;

        /* ---- INR / DCR (carry preserved) ---- */
        case 0x04: case 0x0C: case 0x14: case 0x1C:
        case 0x24: case 0x2C: case 0x34: case 0x3C: {               /* INR */
            bool sc = cpu.fC; int d = (op >> 3) & 7;
            u8 v = alu8(reg_get(d), 1, 0, OP_ADD); cpu.fC = sc;
            if (d == 6) { memory_write(rp_hl(), v); cpu_cycles += 10; }
            else        { reg_set(d, v);            cpu_cycles += 4;  }
            cpu.pc++;
        } break;
        case 0x05: case 0x0D: case 0x15: case 0x1D:
        case 0x25: case 0x2D: case 0x35: case 0x3D: {               /* DCR */
            bool sc = cpu.fC; int d = (op >> 3) & 7;
            u8 v = alu8(reg_get(d), 1, 0, OP_SUB); cpu.fC = sc;
            if (d == 6) { memory_write(rp_hl(), v); cpu_cycles += 10; }
            else        { reg_set(d, v);            cpu_cycles += 4;  }
            cpu.pc++;
        } break;

        /* ---- MVI ---- */
        case 0x06: case 0x0E: case 0x16: case 0x1E:
        case 0x26: case 0x2E: case 0x36: case 0x3E: {
            int d = (op >> 3) & 7; u8 v = IMM8();
            if (d == 6) { memory_write(rp_hl(), v); cpu_cycles += 10; }
            else        { reg_set(d, v);            cpu_cycles += 7;  }
            cpu.pc += 2;
        } break;

        /* ---- LXI ---- */
        case 0x01: set_bc(IMM16()); cpu.pc += 3; cpu_cycles += 10; break;
        case 0x11: set_de(IMM16()); cpu.pc += 3; cpu_cycles += 10; break;
        case 0x21: set_hl(IMM16()); cpu.pc += 3; cpu_cycles += 10; break;
        case 0x31: cpu.sp = IMM16(); cpu.pc += 3; cpu_cycles += 10; break;

        /* ---- INX / DCX (undocumented K flag on 0x7FFF/0x8000 cross) ---- */
        case 0x03: { int v = rp_bc(); if (v == 0x7FFF) cpu.fK = true; set_bc((u16)(v + 1)); cpu.pc++; cpu_cycles += 6; } break;
        case 0x13: { int v = rp_de(); if (v == 0x7FFF) cpu.fK = true; set_de((u16)(v + 1)); cpu.pc++; cpu_cycles += 6; } break;
        case 0x23: { int v = rp_hl(); if (v == 0x7FFF) cpu.fK = true; set_hl((u16)(v + 1)); cpu.pc++; cpu_cycles += 6; } break;
        case 0x33: if (cpu.sp == 0x7FFF) cpu.fK = true; cpu.sp++; cpu.pc++; cpu_cycles += 6; break;
        case 0x0B: { int v = rp_bc(); if (v == 0x8000) cpu.fK = true; set_bc((u16)(v - 1)); cpu.pc++; cpu_cycles += 6; } break;
        case 0x1B: { int v = rp_de(); if (v == 0x8000) cpu.fK = true; set_de((u16)(v - 1)); cpu.pc++; cpu_cycles += 6; } break;
        case 0x2B: { int v = rp_hl(); if (v == 0x8000) cpu.fK = true; set_hl((u16)(v - 1)); cpu.pc++; cpu_cycles += 6; } break;
        case 0x3B: if (cpu.sp == 0x8000) cpu.fK = true; cpu.sp--; cpu.pc++; cpu_cycles += 6; break;

        /* ---- DAD ---- */
        case 0x09: set_hl(alu16(rp_hl(), rp_bc(), 0, OP_ADD)); cpu.pc++; cpu_cycles += 10; break;
        case 0x19: set_hl(alu16(rp_hl(), rp_de(), 0, OP_ADD)); cpu.pc++; cpu_cycles += 10; break;
        case 0x29: set_hl(alu16(rp_hl(), rp_hl(), 0, OP_ADD)); cpu.pc++; cpu_cycles += 10; break;
        case 0x39: set_hl(alu16(cpu.sp,  rp_hl(), 0, OP_ADD)); cpu.pc++; cpu_cycles += 10; break;

        /* ---- load/store A ---- */
        case 0x02: memory_write(rp_bc(), cpu.a); cpu.pc++; cpu_cycles += 7; break;  /* STAX B */
        case 0x12: memory_write(rp_de(), cpu.a); cpu.pc++; cpu_cycles += 7; break;  /* STAX D */
        case 0x0A: cpu.a = memory_read(rp_bc()); cpu.pc++; cpu_cycles += 7; break;  /* LDAX B */
        case 0x1A: cpu.a = memory_read(rp_de()); cpu.pc++; cpu_cycles += 7; break;  /* LDAX D */
        case 0x3A: cpu.a = memory_read(IMM16()); cpu.pc += 3; cpu_cycles += 13; break; /* LDA */
        case 0x32: { u16 a = IMM16(); memory_write(a, cpu.a);                       /* STA */
                     if (a == 0x1800) cpu.write_to_display = true;
                     cpu.pc += 3; cpu_cycles += 13; } break;
        case 0x2A: { u16 a = IMM16(); cpu.l = memory_read(a);                       /* LHLD */
                     cpu.h = memory_read((u16)(a + 1)); cpu.pc += 3; cpu_cycles += 16; } break;
        case 0x22: { u16 a = IMM16(); memory_write(a, cpu.l);                       /* SHLD */
                     memory_write((u16)(a + 1), cpu.h); cpu.pc += 3; cpu_cycles += 16; } break;

        /* ---- immediate ALU ---- */
        case 0xC6: cpu.a = alu8(cpu.a, IMM8(), 0, OP_ADD); cpu.pc += 2; cpu_cycles += 7; break; /* ADI */
        case 0xCE: cpu.a = alu8(cpu.a, IMM8(), cpu.fC?1:0, OP_ADD); cpu.pc += 2; cpu_cycles += 7; break; /* ACI */
        case 0xD6: cpu.a = alu8(cpu.a, IMM8(), 0, OP_SUB); cpu.pc += 2; cpu_cycles += 7; break; /* SUI */
        case 0xDE: cpu.a = alu8(cpu.a, IMM8(), cpu.fC?1:0, OP_SUB); cpu.pc += 2; cpu_cycles += 7; break; /* SBI */
        case 0xE6: cpu.a = alu8(cpu.a, IMM8(), 0, OP_AND); cpu.pc += 2; cpu_cycles += 7; break; /* ANI */
        case 0xEE: cpu.a = alu8(cpu.a, IMM8(), 0, OP_XOR); cpu.pc += 2; cpu_cycles += 7; break; /* XRI */
        case 0xF6: cpu.a = alu8(cpu.a, IMM8(), 0, OP_OR ); cpu.pc += 2; cpu_cycles += 7; break; /* ORI */
        case 0xFE: alu8(cpu.a, IMM8(), 0, OP_SUB); cpu.pc += 2; cpu_cycles += 7; break;         /* CPI */

        /* ---- IN / OUT ---- */
        case 0xDB: cpu.a = ioports_read(IMM8()); cpu.pc += 2; cpu_cycles += 10; break; /* IN */
        case 0xD3: ioports_write(IMM8(), cpu.a); cpu.pc += 2; cpu_cycles += 10; break; /* OUT */

        /* ---- jumps ---- */
        case 0xC3: cpu.pc = IMM16(); cpu_cycles += 10; break;                          /* JMP */
        case 0xDA: if (cpu.fC) cpu.pc = IMM16(); else cpu.pc += 3; break;               /* JC  (no cyc) */
        case 0xD2: if (!cpu.fC){ cpu.pc = IMM16(); cpu_cycles += 10;} else { cpu.pc += 3; cpu_cycles += 7; } break; /* JNC */
        case 0xCA: if (cpu.fZ){ cpu.pc = IMM16(); cpu_cycles += 10;} else { cpu.pc += 3; cpu_cycles += 7; } break;  /* JZ  */
        case 0xC2: if (!cpu.fZ){ cpu.pc = IMM16(); cpu_cycles += 10;} else { cpu.pc += 3; cpu_cycles += 7; } break; /* JNZ */
        case 0xFA: if (cpu.fS){ cpu.pc = IMM16(); cpu_cycles += 10;} else { cpu.pc += 3; cpu_cycles += 7; } break;  /* JM  */
        case 0xF2: if (!cpu.fS){ cpu.pc = IMM16(); cpu_cycles += 10;} else { cpu.pc += 3; cpu_cycles += 7; } break; /* JP  */
        case 0xEA: if (cpu.fP){ cpu.pc = IMM16(); cpu_cycles += 10;} else { cpu.pc += 3; cpu_cycles += 7; } break;  /* JPE */
        case 0xE2: if (!cpu.fP){ cpu.pc = IMM16(); cpu_cycles += 10;} else { cpu.pc += 3; cpu_cycles += 7; } break; /* JPO */

        /* ---- calls ---- */
        case 0xCD: push16((u16)(cpu.pc + 3)); cpu.pc = IMM16(); cpu_cycles += 18; break; /* CALL */
        case 0xDC: if (cpu.fC){ push16((u16)(cpu.pc+3)); cpu.pc = IMM16(); cpu_cycles += 18;} else { cpu.pc += 3; cpu_cycles += 17; } break; /* CC  */
        case 0xD4: if (!cpu.fC){ push16((u16)(cpu.pc+3)); cpu.pc = IMM16(); cpu_cycles += 18;} else { cpu.pc += 3; cpu_cycles += 17; } break; /* CNC */
        case 0xCC: if (cpu.fZ){ push16((u16)(cpu.pc+3)); cpu.pc = IMM16(); cpu_cycles += 18;} else { cpu.pc += 3; cpu_cycles += 17; } break; /* CZ  */
        case 0xC4: if (!cpu.fZ){ push16((u16)(cpu.pc+3)); cpu.pc = IMM16(); cpu_cycles += 18;} else { cpu.pc += 3; cpu_cycles += 17; } break; /* CNZ */
        case 0xFC: if (cpu.fS){ push16((u16)(cpu.pc+3)); cpu.pc = IMM16(); cpu_cycles += 18;} else { cpu.pc += 3; cpu_cycles += 17; } break; /* CM  */
        case 0xF4: if (!cpu.fS){ push16((u16)(cpu.pc+3)); cpu.pc = IMM16(); cpu_cycles += 18;} else { cpu.pc += 3; cpu_cycles += 17; } break; /* CP  */
        case 0xEC: if (cpu.fP){ push16((u16)(cpu.pc+3)); cpu.pc = IMM16(); cpu_cycles += 18;} else { cpu.pc += 3; cpu_cycles += 17; } break; /* CPE */
        case 0xE4: if (!cpu.fP){ push16((u16)(cpu.pc+3)); cpu.pc = IMM16(); cpu_cycles += 18;} else { cpu.pc += 3; cpu_cycles += 17; } break; /* CPO */

        /* ---- returns ---- */
        case 0xC9: cpu.pc = pop16(); cpu_cycles += 10; break;                          /* RET */
        case 0xD8: if (cpu.fC){ cpu.pc = pop16(); cpu_cycles += 12;} else { cpu.pc++; cpu_cycles += 11; } break; /* RC  */
        case 0xD0: if (!cpu.fC){ cpu.pc = pop16(); cpu_cycles += 12;} else { cpu.pc++; cpu_cycles += 11; } break;/* RNC */
        case 0xC8: if (cpu.fZ){ cpu.pc = pop16(); cpu_cycles += 12;} else { cpu.pc++; cpu_cycles += 11; } break; /* RZ  */
        case 0xC0: if (!cpu.fZ){ cpu.pc = pop16(); cpu_cycles += 12;} else { cpu.pc++; cpu_cycles += 11; } break;/* RNZ */
        case 0xF8: if (cpu.fS){ cpu.pc = pop16(); cpu_cycles += 12;} else { cpu.pc++; cpu_cycles += 11; } break; /* RM  */
        case 0xF0: if (!cpu.fS){ cpu.pc = pop16(); cpu_cycles += 12;} else { cpu.pc++; cpu_cycles += 11; } break;/* RP  */
        case 0xE8: if (cpu.fP){ cpu.pc = pop16(); cpu_cycles += 12;} else { cpu.pc++; cpu_cycles += 11; } break; /* RPE */
        case 0xE0: if (!cpu.fP){ cpu.pc = pop16(); cpu_cycles += 12;} else { cpu.pc++; cpu_cycles += 11; } break;/* RPO */

        /* ---- PUSH / POP ---- */
        case 0xC5: push16(rp_bc()); cpu.pc++; cpu_cycles += 12; break;                 /* PUSH B */
        case 0xD5: push16(rp_de()); cpu.pc++; cpu_cycles += 12; break;                 /* PUSH D */
        case 0xE5: push16(rp_hl()); cpu.pc++; cpu_cycles += 12; break;                 /* PUSH H */
        case 0xF5: { u8 f = 0;                                                         /* PUSH PSW */
                     if (cpu.fS)  f += 0x80;
                     if (cpu.fZ)  f += 0x40;
                     if (cpu.fAC) f += 0x10;
                     if (cpu.fP)  f += 0x04;
                     if (cpu.fC)  f += 0x01;
                     cpu.sp--; memory_write(cpu.sp, cpu.a);
                     cpu.sp--; memory_write(cpu.sp, f);
                     cpu.pc++; cpu_cycles += 12; } break;
        case 0xC1: cpu.c = memory_read(cpu.sp++); cpu.b = memory_read(cpu.sp++); cpu.pc++; cpu_cycles += 10; break; /* POP B */
        case 0xD1: cpu.e = memory_read(cpu.sp++); cpu.d = memory_read(cpu.sp++); cpu.pc++; cpu_cycles += 10; break; /* POP D */
        case 0xE1: cpu.l = memory_read(cpu.sp++); cpu.h = memory_read(cpu.sp++); cpu.pc++; cpu_cycles += 10; break; /* POP H */
        case 0xF1: { u8 f = memory_read(cpu.sp++); cpu.a = memory_read(cpu.sp++);      /* POP PSW */
                     cpu.fC = (f & 0x01) != 0; cpu.fP = (f & 0x04) != 0;
                     cpu.fAC = (f & 0x10) != 0; cpu.fZ = (f & 0x40) != 0; cpu.fS = (f & 0x80) != 0;
                     cpu.pc++; cpu_cycles += 10; } break;

        /* ---- HL/SP/PC transfers ---- */
        case 0xE9: cpu.pc = rp_hl(); cpu_cycles += 6; break;                           /* PCHL */
        case 0xF9: cpu.sp = rp_hl(); cpu.pc++; cpu_cycles += 6; break;                 /* SPHL */
        case 0xE3: { u8 l = cpu.l, h = cpu.h;                                          /* XTHL */
                     cpu.l = memory_read(cpu.sp); memory_write(cpu.sp, l);
                     cpu.h = memory_read((u16)(cpu.sp + 1)); memory_write((u16)(cpu.sp + 1), h);
                     cpu.pc++; cpu_cycles += 16; } break;

        /* ---- RST 0..7 ---- */
        case 0xC7: push16((u16)(cpu.pc + 1)); cpu.pc = 0x0000; cpu_cycles += 12; break;
        case 0xCF: push16((u16)(cpu.pc + 1)); cpu.pc = 0x0008; cpu_cycles += 12; break;
        case 0xD7: push16((u16)(cpu.pc + 1)); cpu.pc = 0x0010; cpu_cycles += 12; break;
        case 0xDF: push16((u16)(cpu.pc + 1)); cpu.pc = 0x0018; cpu_cycles += 12; break;
        case 0xE7: push16((u16)(cpu.pc + 1)); cpu.pc = 0x0020; cpu_cycles += 12; break;
        case 0xEF: push16((u16)(cpu.pc + 1)); cpu.pc = 0x0028; cpu_cycles += 12; break;
        case 0xF7: push16((u16)(cpu.pc + 1)); cpu.pc = 0x0030; cpu_cycles += 12; break;
        case 0xFF: push16((u16)(cpu.pc + 1)); cpu.pc = 0x0038; cpu_cycles += 12; break;

        /* ---- RIM / SIM ---- */
        case 0x20:                                                                     /* RIM */
            cpu.a = 0;
            if (cpu.m55) cpu.a += 0x01;
            if (cpu.m65) cpu.a += 0x02;
            if (cpu.m75) cpu.a += 0x04;
            if (cpu.ie)  cpu.a += 0x08;
            if (cpu.p55) cpu.a += 0x10;
            if (cpu.p65) cpu.a += 0x20;
            if (cpu.p75) cpu.a += 0x40;
            if (io_sid)  cpu.a += 0x80;
            cpu.pc++; cpu.rimcnt++; cpu_cycles += 4;
            break;
        case 0x30:                                                                     /* SIM */
            if (cpu.a & 0x08) { cpu.m55 = (cpu.a & 0x01) != 0; cpu.m65 = (cpu.a & 0x02) != 0; cpu.m75 = (cpu.a & 0x04) != 0; }
            if (cpu.a & 0x10) { cpu.p75 = false; cpu.rimcnt = 0; }
            if (cpu.a & 0x40) { cpu.sod = (cpu.a & 0x80) != 0; }
            cpu.pc++; cpu_cycles += 4;
            break;

        /* ---- undocumented ---- */
        case 0x08: set_hl(alu16(rp_hl(), rp_bc(), 0, OP_SUB)); cpu.pc++; cpu_cycles += 10; break; /* DSUB */
        case 0x10: { u8 h = cpu.h, l = cpu.l, sc = cpu.fC ? 1 : 0;                      /* ARHL */
                     cpu.fC = (l & 0x01) == 0x01; l = (u8)(l / 2); if (h & 0x01) l += 0x80;
                     h = (u8)(h / 2); h += (u8)(sc * 0x80); cpu.h = h; cpu.l = l;
                     cpu.pc++; cpu_cycles += 7; } break;
        case 0x18: { u8 d = cpu.d, e = cpu.e, sc = cpu.fC ? 1 : 0;                      /* RDEL */
                     cpu.fC = (d & 0x08) == 0x08; d = (u8)(d / 2); e = (u8)(e / 2);
                     e += (u8)(sc * 0x01); cpu.d = d; cpu.e = e; cpu.pc++; cpu_cycles += 10; } break;
        case 0x28: { int n = IMM8() + rp_de(); set_hl((u16)n); cpu.pc += 2; cpu_cycles += 10; } break; /* LDHI */
        case 0x38: { int n = cpu.sp + IMM8(); set_de((u16)n); cpu.pc += 2; cpu_cycles += 10; } break;  /* LDSI */
        case 0xED: { u16 a = rp_de(); cpu.l = memory_read(a); cpu.h = memory_read((u16)(a + 1)); /* LHLX */
                     cpu.pc++; cpu_cycles += 10; } break;
        case 0xD9: { u16 a = rp_de(); memory_write(a, cpu.l); memory_write((u16)(a + 1), cpu.h); /* SHLX */
                     cpu.pc++; cpu_cycles += 10; } break;
        case 0xCB: if (cpu.fV) { push16(cpu.pc); cpu.pc = 0x0040; cpu_cycles += 12; }   /* RSTV */
                   else { cpu.pc++; cpu_cycles += 6; } break;
        case 0xFD: if (cpu.fK){ cpu.pc = IMM16(); cpu_cycles += 10;} else { cpu.pc += 3; cpu_cycles += 7; } break; /* JK  */
        case 0xDD: if (!cpu.fK){ cpu.pc = IMM16(); cpu_cycles += 10;} else { cpu.pc += 3; cpu_cycles += 7; } break;/* JNK */

        default:
            err = "Unknown instruction";
            break;
    }

    if (err[0]) { *next = cpu.pc; return err; }

    /* ---- per-instruction housekeeping (matches the tail of RunInstruction) --*/

    /* 8253 PIT: advance roughly every 0x41 CPU cycles. */
    {
        u64 diff = cpu_cycles - cyclesav;
        if (diff >= 0x41) { i8253_clock_all(); cyclesav = cpu_cycles - diff + 0x41; }
    }

    /* Floppy/timer interrupt requests raised by the (stubbed) peripherals. */
    if (io_do555interrupt) { cpu.p55 = true; cpu.update_interrupts = true; io_do555interrupt = false; }
    if (io_do655interrupt) {                 cpu.update_interrupts = true; io_do655interrupt = false; }

    /* Post-POST: advertise drive 0 present by patching the cached word at
     * 0xF6C0 (bit4), so DIR finds the drive without re-running POST test 39.
     *
     * The write MUST be unbanked: the C# reference pokes this through its clsMEM
     * indexer, which for a >=0x8000 address is Memory.Ram[addr & 0x7FFF] with no
     * RAM-page arithmetic — i.e. always the fixed slot mem_ram[0x76C0] (the
     * page-0 mapping of 0xF6C0), regardless of which bank is selected at this
     * arbitrary instant.  DIR's 0x4153 check reads f6c0 with RAM page 0, so it
     * looks at that same slot.  Using the *banked* memory_write here would store
     * the presence bit in whatever page mem_ram_page_write happens to hold right
     * now (rarely page 0), so DIR would never see it and report "drive not
     * attached" (the ERROR that then wedges keyboard input). */
    if (io_past_post && !cpu.f6c0_patched && floppy_drives[0].loaded) {
        mem_ram[0xF6C0 & 0x7FFF] = (u8)(mem_ram[0xF6C0 & 0x7FFF] | 0x10);
        cpu.f6c0_patched = true;
    }
    if (io_do755interrupt) { cpu.p75 = true; cpu.update_interrupts = true; io_do755interrupt = false; dbg_pit75++; }

    ioports_uart_tick(4);   /* printer UART timing */

    /* Deliver interrupts (8259 INTR first, then RST 5.5/6.5/7.5). */
    if (cpu.ie) {
        if (i8259_pending) {
            push16(cpu.pc);
            cpu.pc = i8259_interrupt_acknowledge();
            cpu_cycles += 12;
            dbg_irq8259++;
        } else if (!cpu.m55 && cpu.p55) {
            push16(cpu.pc); cpu.pc = 0x002C; cpu_cycles += 12; cpu.p55 = false; dbg_rst55++;
        } else if (!cpu.m65 && cpu.p65) {
            push16(cpu.pc); cpu.pc = 0x0034; cpu_cycles += 12; cpu.p65 = false; dbg_rst65++;
        } else if (!cpu.m75 && cpu.p75) {
            push16(cpu.pc); cpu.pc = 0x003C; cpu_cycles += 12; cpu.p75 = false; dbg_rst75++;
        }
    }

    if (cpu_cycles > (UINT64_MAX - 20)) cpu_cycles = 0;

    *next = cpu.pc;
    return "";
}
