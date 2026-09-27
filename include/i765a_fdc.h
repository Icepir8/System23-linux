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
 *  i765a_fdc.h — floppy subsystem port interface (port of FloppyController.cs)
 *
 *  Models the FDC-card 8255 PPI handshake (Mode 0 latch test, Mode 2 "8748
 *  walk test"), the 8748 stepper-controller command dispatch + register RAM,
 *  and the NEC765 status registers (SRB / MSR / DOR) — i.e. the presence /
 *  handshake / status layer POST exercises.  The NEC765 command engine and
 *  disk-image sector I/O (for actually loading a diskette) are the follow-on.
 * ===========================================================================*/
#ifndef SYSTEM23_I765A_FDC_H
#define SYSTEM23_I765A_FDC_H

#include "system23.h"

void fdc_reset(void);

u8   fdc_read_port(u8 port);          /* ports 0xF0-0xFB read  */
void fdc_write_port(u8 port, u8 value);/* ports 0xF0-0xFB write */

/* Sync a mounted diskette's state into the FDC drive table (from floppy.c). */
void fdc_set_disk(int drive, bool loaded, bool write_protected);

/* Load a disk image (.IMD parsed per-track; anything else raw) into a drive,
 * so the NEC765 command engine can read sectors from it.  Returns false if the
 * file cannot be read.  Called by floppy.c on mount. */
bool fdc_load_image(int drive, const char *path, bool write_protected);

#endif /* SYSTEM23_I765A_FDC_H */
