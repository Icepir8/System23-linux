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
 *  printer.c — System/23 serial-printer emulation (the "paper" model)
 *
 *  Interprets the raw byte stream the guest sends to its printer (fed one byte
 *  at a time from i8251.c's transmit path) into a paged grid of character cells.
 *
 *  Best-effort interpretation (refined as per-model detail surfaces):
 *    - character data is EBCDIC (CP037), the System/23's internal code;
 *    - line/page control: CR (0x0D), LF (0x25), NL (0x15), FF (0x0C),
 *      HT (0x05), BS (0x16), and the System/23 line-action commands
 *      0x13 (print-and-advance = CR+LF) and 0x12 (carriage return);
 *    - bold and underline come from overstrike (a cell printed twice with the
 *      same glyph = bold; a glyph overstruck with '_' = underline), the way a
 *      daisy-wheel / matrix printer actually produces them.
 * ===========================================================================*/
#include "printer.h"

#include <stdlib.h>
#include <string.h>

/* ASCII -> EBCDIC (CP037); inverted below to decode the print stream. */
static const unsigned char ascii_to_ebcdic[128] = {
/*00*/ 0x00,0x01,0x02,0x03,0x37,0x2D,0x2E,0x2F,0x16,0x05,0x25,0x0B,0x0C,0x0D,0x0E,0x0F,
/*10*/ 0x10,0x11,0x12,0x13,0x3C,0x3D,0x32,0x26,0x18,0x19,0x3F,0x27,0x1C,0x1D,0x1E,0x1F,
/*20*/ 0x40,0x5A,0x7F,0x7B,0x5B,0x6C,0x50,0x7D,0x4D,0x5D,0x5C,0x4E,0x6B,0x60,0x4B,0x61,
/*30*/ 0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0x7A,0x5E,0x4C,0x7E,0x6E,0x6F,
/*40*/ 0x7C,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,
/*50*/ 0xD7,0xD8,0xD9,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xAD,0xE0,0xBD,0x5F,0x6D,
/*60*/ 0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
/*70*/ 0x97,0x98,0x99,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xC0,0x4F,0xD0,0xA1,0x07,
};

static char ebcdic_to_ascii(unsigned char e)
{
    static char rev[256]; static int built = 0;
    if (!built) {
        for (int i = 0; i < 256; i++) rev[i] = 0;   /* 0 = not printable */
        for (int c = 0x20; c < 0x7F; c++) rev[ascii_to_ebcdic[c]] = (char)c;
        built = 1;
    }
    return rev[e];
}

/* ---- paper state ----------------------------------------------------------*/
#define PRT_MAX_PAGES 300

static PrtPage      *pages;
static int           npages;          /* pages allocated / in use (>= 1)       */
static int           cur, row, col;   /* print head position                   */
static int           used_cols;       /* widest column touched (for layout)    */
static unsigned long serial;          /* bumped on any change                  */
static PrinterModel  model = PRT_5217;

static char          raw[65536];
static int           raw_len;

static void bump(void) { serial++; }

static void ensure_init(void)
{
    if (!pages) {
        pages = calloc(1, sizeof(PrtPage));
        npages = pages ? 1 : 0;
        cur = row = col = 0; used_cols = 0;
    }
}

/* Move to the top of the next page, growing/capping the paper. Column unchanged. */
static void advance_page(void)
{
    row = 0;
    if (cur + 1 >= PRT_MAX_PAGES) return;      /* paper full: overprint last page */
    cur++;
    if (cur >= npages) {
        PrtPage *p = realloc(pages, (size_t)(npages + 1) * sizeof(PrtPage));
        if (p) { pages = p; memset(&pages[npages], 0, sizeof(PrtPage)); npages++; }
        else    cur = npages - 1;              /* out of memory: stay             */
    }
}

static void carriage_return(void) { col = 0; }
static void line_feed(void)       { if (++row >= PRT_ROWS) advance_page(); }
static void form_feed(void)       { col = 0; if (cur + 1 < PRT_MAX_PAGES) advance_page(); else row = 0; }

static void put_char(char c)
{
    ensure_init();
    if (!pages) return;
    if (row < 0) row = 0;
    if (row >= PRT_ROWS) row = PRT_ROWS - 1;
    if (col < 0) col = 0;
    if (col >= PRT_COLS) { carriage_return(); line_feed(); }   /* wrap long lines */

    PrtCell *cell = &pages[cur].cell[row][col];
    if      (cell->ch == 0)                  cell->ch = (unsigned char)c;      /* fresh       */
    else if (cell->ch == (unsigned char)c)   cell->bold = 1;                   /* self-overstrike -> bold */
    else if (c == '_')                       cell->ul = 1;                     /* underscore over glyph   */
    else if (cell->ch == '_')              { cell->ch = (unsigned char)c; cell->ul = 1; } /* glyph over underscore */
    else                                   { cell->ch = (unsigned char)c; cell->bold = 1; } /* blended overstrike */

    if (col + 1 > used_cols) used_cols = col + 1;
    col++;
}

/* ---- public API -----------------------------------------------------------*/
void printer_feed_byte(u8 b)
{
    ensure_init();
    if (raw_len < (int)sizeof raw) raw[raw_len++] = (char)b;

    switch (b) {
        case 0x0D: carriage_return();               bump(); return;  /* CR              */
        case 0x25: line_feed();                     bump(); return;  /* LF              */
        case 0x15: carriage_return(); line_feed();  bump(); return;  /* NL (CR+LF)      */
        case 0x13: carriage_return(); line_feed();  bump(); return;  /* print + advance */
        case 0x12: carriage_return();               bump(); return;  /* carriage return */
        case 0x0C: form_feed();                     bump(); return;  /* form feed       */
        case 0x16: if (col > 0) col--;              bump(); return;  /* backspace       */
        case 0x05:                                                   /* horizontal tab  */
            col = ((col / 8) + 1) * 8;
            if (col >= PRT_COLS) col = PRT_COLS - 1;
            bump(); return;
        default: break;
    }

    char c = ebcdic_to_ascii(b);
    if (c) { put_char(c); bump(); }                 /* printable EBCDIC glyph          */
    /* unmapped control/command bytes are ignored (best-effort) */
}

void printer_clear(void)
{
    free(pages); pages = NULL; npages = 0;
    cur = row = col = 0; used_cols = 0; raw_len = 0;
    ensure_init();
    bump();
}

void printer_reset(void) { printer_clear(); }

int            printer_page_count(void) { ensure_init(); return npages > 0 ? npages : 1; }
const PrtPage *printer_page(int i)      { ensure_init(); return (i >= 0 && i < npages) ? &pages[i] : NULL; }
int            printer_used_cols(void)  { return used_cols > 0 ? used_cols : 1; }
unsigned long  printer_serial(void)     { return serial; }

void           printer_set_model(PrinterModel m) { if (m >= 0 && m < PRT_MODEL_COUNT) { model = m; bump(); } }
PrinterModel   printer_model(void)      { return model; }
bool           printer_is_daisy(PrinterModel m) { return m == PRT_5217; }

const char *printer_model_name(PrinterModel m)
{
    switch (m) {
        case PRT_5217: return "IBM 5217 (daisy-wheel)";
        case PRT_5222: return "IBM 5222 (dot-matrix)";
        case PRT_5241: return "IBM 5241 (dot-matrix)";
        case PRT_5242: return "IBM 5242 (dot-matrix)";
        default:       return "Printer";
    }
}

int printer_raw(char *buf, int size)
{
    if (size <= 0) return 0;
    int n = raw_len < size - 1 ? raw_len : size - 1;
    memcpy(buf, raw, (size_t)n);
    buf[n] = 0;
    return n;
}
