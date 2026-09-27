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
 *  i8275.h — Intel 8275 CRT controller (port of I8275CTRC.cs)
 *
 *  The Display polls status (rendering gates on status & 0x2C), the cursor
 *  position and the underline placement.  The CPU reaches the chip through two
 *  I/O ports: PREG (0x44, parameter) and CSREG (0x45, command/status).
 * ===========================================================================*/
#ifndef SYSTEM23_I8275_H
#define SYSTEM23_I8275_H

#include "system23.h"

extern u8  crtc_status;        /* was I8275CTRC.status         */
extern int crtc_cursor_col;    /* was I8275CTRC.cursorcol      */
extern int crtc_cursor_row;    /* was I8275CTRC.cursorrow      */
extern u8  crtc_ul_placement;  /* was I8275CTRC.ulplacmnt      */

void i8275_reset(void);

u8   i8275_preg_read(void);    /* port 0x44 read  (parameter)          */
void i8275_preg_write(u8 v);   /* port 0x44 write (parameter)          */
u8   i8275_csreg_read(void);   /* port 0x45 read  (status)             */
void i8275_csreg_write(u8 v);  /* port 0x45 write (command)            */

#endif /* SYSTEM23_I8275_H */
