/* ===========================================================================
 *  i8275.c — 8275 CRT controller (STUB)
 *  TODO: port command/parameter register handling and row buffering from
 *  I8275CTRC.cs.  For now the state is inert (status 0 => Display shows the
 *  "no signal" splash).
 * ===========================================================================*/
#include "i8275.h"

u8  crtc_status       = 0;
int crtc_cursor_col   = 0;
int crtc_cursor_row   = 0;
u8  crtc_ul_placement = 0;

void i8275_reset(void)
{
    crtc_status = 0;   /* status back to 0 -> screen blanks until POST re-inits */
}
