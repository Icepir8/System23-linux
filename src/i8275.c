/* ===========================================================================
 *  i8275.c — Intel 8275 CRT controller (faithful port of I8275CTRC.cs)
 * ===========================================================================*/
#include "i8275.h"

u8  crtc_status       = 0;
int crtc_cursor_col   = 0;
int crtc_cursor_row   = 0;
u8  crtc_ul_placement = 0;

/* Internal sequencing / latched timing parameters. */
static int  writecnt = 0;
static int  readcnt  = 0;
static int  rdcnt    = 0;
static u8   cmdsav   = 0;

static bool spaced_rows = false;
static u8   hrzchars = 0;
static u8   vrrcnt = 0;
static u8   vrtrows = 0;
static u8   lncharrow = 0;
static bool lncntmode = false;
static bool fldatrbmode = false;
static u8   curfmt = 0;

void i8275_reset(void)
{
    crtc_status = 0;
    writecnt = readcnt = rdcnt = 0;
    cmdsav = 0;
    spaced_rows = false;
    hrzchars = vrrcnt = vrtrows = lncharrow = curfmt = 0;
    crtc_ul_placement = 0;
    lncntmode = fldatrbmode = false;
    crtc_cursor_col = crtc_cursor_row = 0;
}

/* ---- command register (0x45 write) ---------------------------------------*/
void i8275_csreg_write(u8 cmd)
{
    if (writecnt != 0 || readcnt != 0)
        crtc_status |= 0x08;
    cmdsav = (u8)(cmd >> 5);

    switch (cmd >> 5) {
        case 0: writecnt = 4; crtc_status = 0;       break;  /* reset        */
        case 1: crtc_status |= 0x44;                 break;  /* start display*/
        case 2: crtc_status &= 0xBB;                 break;  /* stop display */
        case 3: readcnt = 2;                         break;  /* read lightpen*/
        case 4: writecnt = 2;                        break;  /* load cursor  */
        case 5: crtc_status |= 0x40;                 break;  /* enable int   */
        case 6: crtc_status &= 0xBF;                 break;  /* disable int  */
        case 7:                                      break;  /* preset ctrs  */
    }
}

/* ---- status register (0x45 read) -----------------------------------------*/
u8 i8275_csreg_read(void)
{
    u8 tstatus = crtc_status;
    crtc_status &= 0x44;
    if (rdcnt++ >= 0x800) { tstatus |= 0x30; rdcnt = 0; }
    writecnt = 0;
    readcnt = 0;
    return tstatus;
}

/* ---- parameter register (0x44 write) -------------------------------------*/
void i8275_preg_write(u8 p)
{
    if (writecnt == 0 || readcnt != 0)
        crtc_status |= 0x08;

    switch (cmdsav) {
        case 0:   /* reset parameters */
            switch (writecnt) {
                case 4: hrzchars = (u8)(p & 0x7F); spaced_rows = (p & 0x80) != 0; break;
                case 3: vrtrows = (u8)((p & 0xC0) >> 6); vrrcnt = (u8)(p & 0x3F); break;
                case 2: crtc_ul_placement = (u8)((p & 0xF0) >> 4); lncharrow = (u8)(p & 0x0F); break;
                case 1: curfmt = (u8)((p & 0x30) >> 4); lncntmode = (p & 0x80) != 0; fldatrbmode = (p & 0x40) != 0; break;
            }
            break;
        case 4:   /* load cursor */
            switch (writecnt) {
                case 2: crtc_cursor_col = p; break;
                case 1: crtc_cursor_row = p; break;
            }
            break;
    }
    writecnt--;
}

/* ---- parameter register (0x44 read) --------------------------------------*/
u8 i8275_preg_read(void)
{
    if (writecnt != 0 || readcnt == 0)
        crtc_status |= 0x08;
    readcnt--;
    return (readcnt == 1) ? 0x49 : 0x17;
}
