/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  system23.h — common types, enums and machine geometry
 *
 *  Linux/GTK3 port of the System/23 (IBM Datamaster) emulator, ported from the
 *  original C# / WinForms project.  This header is dependency-free (only the C
 *  standard library) so every translation unit can include it cheaply.
 * ===========================================================================*/
#ifndef SYSTEM23_H
#define SYSTEM23_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- Fixed-width convenience types --------------------------------------- */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* ---- Application metadata ------------------------------------------------- */
#define APP_NAME    "System/23"
#define APP_TITLE   "System/23 — IBM Datamaster (Linux/GTK3)"
#define APP_VERSION "0.1.0-skeleton"

/* ---- CRT geometry (IBM System/23, 8275 CRTC driving an 80x24 text screen) --
 *  Character cells are decoded from a character-ROM bitmap of 8x14 source
 *  pixels; the original renderer scales each glyph into an 18x32 screen cell
 *  (glyph drawn 16 wide, +2px inter-cell gap).  The visible text buffer starts
 *  at RAM offset 0x200.  These constants centralise the numbers that were
 *  scattered through Display.cs. */
#define CRT_COLS        80
#define CRT_ROWS        24
#define CRT_RAM_BASE    0x200      /* Memory.Ram offset of the top-left cell   */

#define GLYPH_SRC_W     8          /* character-ROM source cell width  (px)    */
#define GLYPH_SRC_H     14         /* character-ROM source cell height (px)    */
#define CELL_W          18         /* on-screen cell pitch, X (px)             */
#define CELL_H          32         /* on-screen cell pitch, Y (px)             */
#define GLYPH_DST_W     16         /* glyph drawn width within the cell (px)   */
#define GLYPH_DST_H     32         /* glyph drawn height within the cell (px)  */

#define CRT_PIXELS_W    (CRT_COLS * CELL_W)   /* 1440 */
#define CRT_PIXELS_H    (CRT_ROWS * CELL_H)   /*  768 */

/* Green-phosphor colours used for the underline bar and the cursor (the glyph
 * colours themselves live in the character-ROM BMP palettes).  Matches the
 * greenBrush / greenHiBrush constants from Display.cs. */
#define PHOSPHOR_R  0x00
#define PHOSPHOR_G  0xCC
#define PHOSPHOR_B  0x00

/* ---- How the emulated CPU is currently being driven (was ExecState) ------- */
typedef enum {
    EXEC_STOPPED = 0,   /* not executing; single-step returns here            */
    EXEC_AUTOSTEP,      /* stepping on the debug timer (trace mode)           */
    EXEC_RUNNING        /* full-speed, cycle-correct execution                */
} ExecState;

/* ---- Selectable ROM (ROS) firmware revisions (was Memory.RomVersion) ------ */
typedef enum {
    ROS_1_01 = 0,       /* "14set" — ROS 1.01 (1980)                          */
    ROS_1_04 = 1,       /* "16set" — ROS 1.04                                 */
    ROS_1_05 = 2,       /* "16set" — ROS 1.05 (1981)                          */
    ROM_VERSION_COUNT
} RomVersion;

/* ---- Keyboard region / language (was IOports.Countries) ------------------- */
typedef enum {
    COUNTRY_USA = 0,
    COUNTRY_AUSTRIA_GERMANY,
    COUNTRY_ITALY,
    COUNTRY_SPAIN_SPANISH,
    COUNTRY_UNITEDKINGDOM,
    COUNTRY_SWITZERLAND_GERMAN,
    COUNTRY_CANADA_FRENCH,
    COUNTRY_NORWAY,
    COUNTRY_FRANCE,
    COUNTRY_BELGIUM,
    COUNTRY_SWITZERLAND_FRENCH,
    COUNTRY_SWEDEN,
    COUNTRY_INTERNATIONAL,
    COUNTRY_DENMARK,
    COUNTRY_BELGIUM_FRENCH,
    COUNTRY_FINLAND,
    COUNTRY_JAPAN,
    COUNTRY_COUNT
} Countries;

#endif /* SYSTEM23_H */
