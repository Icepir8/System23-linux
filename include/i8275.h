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
