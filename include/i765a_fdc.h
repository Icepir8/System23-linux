/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
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
