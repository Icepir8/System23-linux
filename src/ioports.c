/* ===========================================================================
 *  ioports.c — I/O port dispatch (STUB)
 *
 *  TODO: port ReadIOport / WriteIOport and the device wiring from IOports.cs
 *  (three 8255 PPIs, 8253 PIT, 8257 DMA, 8259 PIC, 8275 CRTC, 8251/printer
 *  UART, SASI host adapter, 8748 keyboard controller).
 * ===========================================================================*/
#include "ioports.h"
#include "i8253.h"

u8        io_kbscancode     = 0;
u32       io_char_page_read = 0;
u8        io_diagnostic_port = 0xFF;
bool      io_past_post      = false;
Countries io_country        = COUNTRY_USA;

u8 ioports_read(u16 port)
{
    (void)port;
    return 0xFF;   /* STUB */
}

bool ioports_write(u16 port, u8 value)
{
    (void)port; (void)value;
    return false;  /* STUB */
}

void ioports_reset(void)
{
    /* TODO: reset every device to power-on state. */
    io_kbscancode = 0;
    io_diagnostic_port = 0xFF;
    io_past_post = false;
    i8253_reset();
}

void ioports_reset_pit(void)
{
    /* Warm reset recreates just the PIT (POST test 34 depends on this). */
    i8253_reset();
}

void ioports_reset_system(void)
{
    ioports_reset();   /* TODO: full hardware init (FDC / 8748 / etc.) */
}

const char *country_name(Countries c)
{
    static const char *const names[COUNTRY_COUNT] = {
        "USA", "Austria/Germany", "Italy", "Spain (Spanish)",
        "United Kingdom", "Switzerland (German)", "Canada (French)",
        "Norway", "France", "Belgium", "Switzerland (French)", "Sweden",
        "International", "Denmark", "Belgium (French)", "Finland", "Japan",
    };
    return (c >= 0 && c < COUNTRY_COUNT) ? names[c] : "?";
}
