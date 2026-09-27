/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  disassembler85.c — 8085 disassembler (port of DisAssembler85.cs)
 *
 *  Full documented 8085 instruction set plus the undocumented opcodes the
 *  System/23 ROM uses (DSUB, ARHL, RDEL, LDHI, LDSI, RSTV, SHLX, LHLX, JK/JNK).
 *  Reads operands through memory_read so it decodes live (banked) memory.
 * ===========================================================================*/
#include "disassembler85.h"
#include "memory.h"
#include <stdio.h>

static const char *const R[8]   = { "B","C","D","E","H","L","M","A" };
static const char *const RP[4]  = { "B","D","H","SP" };
static const char *const ALU[8] = { "ADD","ADC","SUB","SBB","ANA","XRA","ORA","CMP" };
static const char *const ALUI[8]= { "ADI","ACI","SUI","SBI","ANI","XRI","ORI","CPI" };
static const char *const CC[8]  = { "NZ","Z","NC","C","PO","PE","P","M" };

int disassembler85_at(u16 addr, char *text, size_t text_size)
{
    u8  op  = memory_read(addr);
    u8  b1  = memory_read((u16)(addr + 1));
    u8  b2  = memory_read((u16)(addr + 2));
    u16 nn  = (u16)(b1 | (b2 << 8));
    int len = 1;
    char t[48];

    /* MOV r,r' (0x40-0x7F, except 0x76 = HLT) */
    if (op >= 0x40 && op <= 0x7F && op != 0x76) {
        snprintf(t, sizeof t, "MOV  %s,%s", R[(op >> 3) & 7], R[op & 7]);
    }
    /* ALU A,r (0x80-0xBF) */
    else if (op >= 0x80 && op <= 0xBF) {
        snprintf(t, sizeof t, "%-4s %s", ALU[(op >> 3) & 7], R[op & 7]);
    }
    else switch (op) {
        case 0x00: snprintf(t, sizeof t, "NOP"); break;
        case 0x76: snprintf(t, sizeof t, "HLT"); break;
        case 0x07: snprintf(t, sizeof t, "RLC"); break;
        case 0x0F: snprintf(t, sizeof t, "RRC"); break;
        case 0x17: snprintf(t, sizeof t, "RAL"); break;
        case 0x1F: snprintf(t, sizeof t, "RAR"); break;
        case 0x27: snprintf(t, sizeof t, "DAA"); break;
        case 0x2F: snprintf(t, sizeof t, "CMA"); break;
        case 0x37: snprintf(t, sizeof t, "STC"); break;
        case 0x3F: snprintf(t, sizeof t, "CMC"); break;
        case 0xEB: snprintf(t, sizeof t, "XCHG"); break;
        case 0xE3: snprintf(t, sizeof t, "XTHL"); break;
        case 0xF9: snprintf(t, sizeof t, "SPHL"); break;
        case 0xE9: snprintf(t, sizeof t, "PCHL"); break;
        case 0xFB: snprintf(t, sizeof t, "EI"); break;
        case 0xF3: snprintf(t, sizeof t, "DI"); break;
        case 0x20: snprintf(t, sizeof t, "RIM"); break;
        case 0x30: snprintf(t, sizeof t, "SIM"); break;

        /* INR/DCR r */
        case 0x04: case 0x0C: case 0x14: case 0x1C:
        case 0x24: case 0x2C: case 0x34: case 0x3C:
            snprintf(t, sizeof t, "INR  %s", R[(op >> 3) & 7]); break;
        case 0x05: case 0x0D: case 0x15: case 0x1D:
        case 0x25: case 0x2D: case 0x35: case 0x3D:
            snprintf(t, sizeof t, "DCR  %s", R[(op >> 3) & 7]); break;

        /* MVI r,d8 */
        case 0x06: case 0x0E: case 0x16: case 0x1E:
        case 0x26: case 0x2E: case 0x36: case 0x3E:
            snprintf(t, sizeof t, "MVI  %s,%02Xh", R[(op >> 3) & 7], b1); len = 2; break;

        /* register-pair ops */
        case 0x01: case 0x11: case 0x21: case 0x31:
            snprintf(t, sizeof t, "LXI  %s,%04Xh", RP[(op >> 4) & 3], nn); len = 3; break;
        case 0x09: case 0x19: case 0x29: case 0x39:
            snprintf(t, sizeof t, "DAD  %s", RP[(op >> 4) & 3]); break;
        case 0x03: case 0x13: case 0x23: case 0x33:
            snprintf(t, sizeof t, "INX  %s", RP[(op >> 4) & 3]); break;
        case 0x0B: case 0x1B: case 0x2B: case 0x3B:
            snprintf(t, sizeof t, "DCX  %s", RP[(op >> 4) & 3]); break;
        case 0xC5: case 0xD5: case 0xE5:
            snprintf(t, sizeof t, "PUSH %s", RP[(op >> 4) & 3]); break;
        case 0xF5: snprintf(t, sizeof t, "PUSH PSW"); break;
        case 0xC1: case 0xD1: case 0xE1:
            snprintf(t, sizeof t, "POP  %s", RP[(op >> 4) & 3]); break;
        case 0xF1: snprintf(t, sizeof t, "POP  PSW"); break;

        case 0x02: snprintf(t, sizeof t, "STAX B"); break;
        case 0x12: snprintf(t, sizeof t, "STAX D"); break;
        case 0x0A: snprintf(t, sizeof t, "LDAX B"); break;
        case 0x1A: snprintf(t, sizeof t, "LDAX D"); break;
        case 0x22: snprintf(t, sizeof t, "SHLD %04Xh", nn); len = 3; break;
        case 0x2A: snprintf(t, sizeof t, "LHLD %04Xh", nn); len = 3; break;
        case 0x32: snprintf(t, sizeof t, "STA  %04Xh", nn); len = 3; break;
        case 0x3A: snprintf(t, sizeof t, "LDA  %04Xh", nn); len = 3; break;

        /* immediate ALU */
        case 0xC6: case 0xCE: case 0xD6: case 0xDE:
        case 0xE6: case 0xEE: case 0xF6: case 0xFE:
            snprintf(t, sizeof t, "%-4s %02Xh", ALUI[(op >> 3) & 7], b1); len = 2; break;

        /* jumps / calls / returns */
        case 0xC3: snprintf(t, sizeof t, "JMP  %04Xh", nn); len = 3; break;
        case 0xCD: snprintf(t, sizeof t, "CALL %04Xh", nn); len = 3; break;
        case 0xC9: snprintf(t, sizeof t, "RET"); break;
        case 0xC2: case 0xCA: case 0xD2: case 0xDA:
        case 0xE2: case 0xEA: case 0xF2: case 0xFA:
            snprintf(t, sizeof t, "J%-3s %04Xh", CC[(op >> 3) & 7], nn); len = 3; break;
        case 0xC4: case 0xCC: case 0xD4: case 0xDC:
        case 0xE4: case 0xEC: case 0xF4: case 0xFC:
            snprintf(t, sizeof t, "C%-3s %04Xh", CC[(op >> 3) & 7], nn); len = 3; break;
        case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        case 0xE0: case 0xE8: case 0xF0: case 0xF8:
            snprintf(t, sizeof t, "R%s", CC[(op >> 3) & 7]); break;
        case 0xC7: case 0xCF: case 0xD7: case 0xDF:
        case 0xE7: case 0xEF: case 0xF7: case 0xFF:
            snprintf(t, sizeof t, "RST  %d", (op >> 3) & 7); break;

        case 0xDB: snprintf(t, sizeof t, "IN   %02Xh", b1); len = 2; break;
        case 0xD3: snprintf(t, sizeof t, "OUT  %02Xh", b1); len = 2; break;

        /* undocumented 8085 opcodes (used by the System/23 ROM) */
        case 0x08: snprintf(t, sizeof t, "DSUB"); break;
        case 0x10: snprintf(t, sizeof t, "ARHL"); break;
        case 0x18: snprintf(t, sizeof t, "RDEL"); break;
        case 0x28: snprintf(t, sizeof t, "LDHI %02Xh", b1); len = 2; break;
        case 0x38: snprintf(t, sizeof t, "LDSI %02Xh", b1); len = 2; break;
        case 0xCB: snprintf(t, sizeof t, "RSTV"); break;
        case 0xD9: snprintf(t, sizeof t, "SHLX"); break;
        case 0xED: snprintf(t, sizeof t, "LHLX"); break;
        case 0xDD: snprintf(t, sizeof t, "JNK  %04Xh", nn); len = 3; break;
        case 0xFD: snprintf(t, sizeof t, "JK   %04Xh", nn); len = 3; break;

        default:   snprintf(t, sizeof t, "DB   %02Xh", op); break;
    }

    if (text && text_size) snprintf(text, text_size, "%s", t);
    return len;
}
