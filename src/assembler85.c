/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* assembler85.c — two-pass 8085 assembler (STUB). TODO: port Assembler85.cs. */
#include "assembler85.h"
#include <string.h>
#include <stdio.h>

bool assembler85_assemble(const char *source_text,
                          u8 *out, size_t out_size, size_t *out_len,
                          char *err, size_t err_size)
{
    (void)source_text; (void)out; (void)out_size;
    if (out_len) *out_len = 0;
    if (err && err_size) snprintf(err, err_size, "assembler not yet ported");
    return false;
}
