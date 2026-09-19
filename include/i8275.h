/* ===========================================================================
 *  i8275.h — Intel 8275 CRT controller (port of I8275CTRC.cs)
 *
 *  The Display polls a few pieces of CRTC state each frame: the status byte
 *  (rendering is gated on status & 0x2C), the cursor position, and the
 *  underline placement.  Those fields are real here; the register interface
 *  and row/DMA timing are TODO.
 * ===========================================================================*/
#ifndef SYSTEM23_I8275_H
#define SYSTEM23_I8275_H

#include "system23.h"

extern u8  crtc_status;        /* was I8275CTRC.status         */
extern int crtc_cursor_col;    /* was I8275CTRC.cursorcol      */
extern int crtc_cursor_row;    /* was I8275CTRC.cursorrow      */
extern u8  crtc_ul_placement;  /* was I8275CTRC.ulplacmnt      */

void i8275_reset(void);

#endif /* SYSTEM23_I8275_H */
