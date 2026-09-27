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
 *  i8257.h — Intel 8257 DMA controller (port of I8257DMA.cs)
 *
 *  Four channels; channel 0 is wired to the FDC.  Each channel has a 16-bit
 *  address and count register programmed a byte at a time through a shared
 *  low/high flip-flop.  AssertDRQ performs the burst transfer between the
 *  peripheral (via the registered callbacks) and banked memory.
 * ===========================================================================*/
#ifndef SYSTEM23_I8257_H
#define SYSTEM23_I8257_H

#include "system23.h"

typedef u8   (*DmaReadFn)(int ch);
typedef void (*DmaWriteFn)(int ch, u8 value);
typedef void (*DmaTcFn)(int ch);
typedef void (*DmaChanProgFn)(int ch, bool is_count);

extern u16 dma_addr[4];
extern u16 dma_cnt[4];

/* Peripheral wiring (fixed board connectivity — set once by the FDC). */
extern DmaReadFn     dma_periph_read[4];
extern DmaWriteFn    dma_periph_write[4];
extern DmaTcFn       dma_on_tc;
extern DmaChanProgFn dma_on_channel_programmed;

void dma_reset(void);
u8   dma_port_read(int port);        /* ports 0x00-0x0F */
void dma_port_write(int port, u8 value);
void dma_assert_drq(int ch, bool active);
void dma_reset_byte_pointer(void);   /* force the low/high flip-flop low */

#endif /* SYSTEM23_I8257_H */
