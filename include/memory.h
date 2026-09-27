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
 *  memory.h — banked memory subsystem (port of Memory.cs)
 *
 *  256 KB ROM + 256 KB RAM presented through a paged 64 KB window:
 *    0x0000-0x3FFF  fixed ROM
 *    0x4000-0x7FFF  paged ROM   (RomPage)
 *    0x8000-0xBFFF  fixed RAM   (low)
 *    0xC000-0xFFFF  paged RAM   (RamPageRead / RamPageWrite)
 *  DMA accesses use a separate page register (DMAPage).
 * ===========================================================================*/
#ifndef SYSTEM23_MEMORY_H
#define SYSTEM23_MEMORY_H

#include "system23.h"

#define MEM_SIZE 0x44000   /* 272 KB backing store for both ROM and RAM */

extern u8  mem_rom[MEM_SIZE];
extern u8  mem_ram[MEM_SIZE];

/* Page registers (written by the I/O layer). */
extern u32 mem_dma_page;
extern u16 dbg_memwatch;   /* debug: watch CPU writes to this address */
extern u32 mem_rom_page;
extern u32 mem_ram_page_write;
extern u32 mem_ram_page_read;

/* CPU-visible access. */
u8   memory_read(u16 addr);
u16  memory_read16(u16 addr);
bool memory_write(u16 addr, u8 value);
bool memory_write16(u16 addr, u16 value);

/* DMA-visible access (uses mem_dma_page). */
u8   memory_dma_read(u16 addr);
bool memory_dma_write(u16 addr, u8 value);

/* Return RAM + page registers to power-on state (ROM image is preserved). */
void memory_reset(void);

/* ROM image helpers. */
int  memory_load_rom(const char *filename, int loadaddr); /* 0 ok, -1 fail   */
void memory_fill_bank(int loadaddr, int length, u8 value);

/* Load a complete ROS set from roms_dir into the ROM image.
 * Returns the number of bank images actually loaded. */
int  memory_load_rom_set(RomVersion version, const char *roms_dir);

/* Human-friendly label for a ROM set (for UI dropdowns). */
const char *rom_version_display_name(RomVersion v);

#endif /* SYSTEM23_MEMORY_H */
