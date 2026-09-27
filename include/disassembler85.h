/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  disassembler85.h — 8085 disassembler (port of DisAssembler85.cs)
 *  Full 8085 set incl. the undocumented System/23 opcodes; used by the debug
 *  window's live code view and the range-disassembly listing.
 * ===========================================================================*/
#ifndef SYSTEM23_DISASSEMBLER85_H
#define SYSTEM23_DISASSEMBLER85_H

#include "system23.h"

/* Disassemble the instruction at `addr` (reading via memory_read) into `text`.
 * Returns the number of bytes the instruction occupies. */
int disassembler85_at(u16 addr, char *text, size_t text_size);

#endif /* SYSTEM23_DISASSEMBLER85_H */
