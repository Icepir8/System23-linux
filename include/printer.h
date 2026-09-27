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
 *  printer.h — System/23 serial-printer emulation (the "paper" model)
 *
 *  The 8251 USART hands us the raw byte stream the guest sends to its printer
 *  (see i8251.c on_transmit).  This module interprets that stream — EBCDIC
 *  character data plus line/page control codes — into a paged "paper" model of
 *  character cells that the Printer window renders and can send to a real
 *  printer.  The four supported models share one best-effort interpreter for
 *  now; the model mainly selects the rendered typeface (letter-quality vs.
 *  dot-matrix) and is refined as per-model details surface.
 * ===========================================================================*/
#ifndef SYSTEM23_PRINTER_H
#define SYSTEM23_PRINTER_H

#include "system23.h"

typedef enum {
    PRT_5217 = 0,   /* IBM 5217 (Model C02) — letter-quality daisy-wheel */
    PRT_5222,       /* IBM 5222 — dot-matrix                             */
    PRT_5241,       /* IBM 5241 — dot-matrix                             */
    PRT_5242,       /* IBM 5242 — dot-matrix                             */
    PRT_MODEL_COUNT
} PrinterModel;

/* Fanfold geometry: 132 columns at 10 CPI, 66 lines at 6 LPI (11" page). */
#define PRT_COLS  132
#define PRT_ROWS   66

typedef struct {
    unsigned char ch;    /* printable ASCII glyph (0x20..0x7E); 0 = blank */
    unsigned char bold;  /* overstruck with itself                        */
    unsigned char ul;    /* underscored (overstruck underline)            */
} PrtCell;

typedef struct {
    PrtCell cell[PRT_ROWS][PRT_COLS];
} PrtPage;

void          printer_reset(void);            /* init module + clear paper (startup) */
void          printer_clear(void);            /* clear all paper (operator)          */
void          printer_feed_byte(u8 b);        /* one byte from the USART transmit    */

int           printer_page_count(void);       /* always >= 1                          */
const PrtPage *printer_page(int i);           /* page i (0-based), or NULL            */
int           printer_used_cols(void);        /* widest column touched, for layout    */
unsigned long printer_serial(void);           /* bumps on any change (redraw gate)     */

void          printer_set_model(PrinterModel m);
PrinterModel  printer_model(void);
const char   *printer_model_name(PrinterModel m);
bool          printer_is_daisy(PrinterModel m);   /* letter-quality vs dot-matrix     */

int           printer_raw(char *buf, int size);   /* raw byte capture (hex/debug view) */

#endif /* SYSTEM23_PRINTER_H */
