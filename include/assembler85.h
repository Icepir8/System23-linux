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
