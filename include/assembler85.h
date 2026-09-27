/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  assembler85.h — two-pass 8085 assembler (port of Assembler85.cs)
 *
 *  NOTE: in the C# project this class ALSO held the CPU instruction engine
 *  (RunInstruction).  In the C port that engine lives in cpu8085.*; this module
 *  keeps only the text assembler.  STATUS: stub.
 * ===========================================================================*/
#ifndef SYSTEM23_ASSEMBLER85_H
#define SYSTEM23_ASSEMBLER85_H

#include "system23.h"

/* Assemble `source_text` into `out` (capacity out_size).  On success returns
 * true and sets *out_len; on failure returns false and writes a message into
 * err.  TODO: port the two-pass assembler. */
bool assembler85_assemble(const char *source_text,
                          u8 *out, size_t out_size, size_t *out_len,
                          char *err, size_t err_size);

#endif /* SYSTEM23_ASSEMBLER85_H */
