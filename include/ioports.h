/* ===========================================================================
 *  ioports.h — 8085 I/O port dispatch (port of IOports.cs)
 *
 *  Central read/write dispatch for every device wired to the I/O and memory
 *  page-register space, plus the shared operator-facing state the UI touches
 *  (keyboard scancode, character-page register, diagnostic port, region).
 *
 *  STATUS: stub.  The read/write dispatch and per-device wiring are TODO; the
 *  shared state fields below are real so the Display can already feed the
 *  keyboard path.
 * ===========================================================================*/
#ifndef SYSTEM23_IOPORTS_H
#define SYSTEM23_IOPORTS_H

#include "system23.h"

/* Shared machine state referenced by the UI / interrupt path. */
extern u8        io_kbscancode;        /* last key scancode handed to the KB  */
extern u32       io_char_page_read;    /* CharPageRead — display char bank    */
extern u8        io_diagnostic_port;   /* Diagnosticport                      */
extern bool      io_past_post;         /* true once a real keystroke arrived  */
extern Countries io_country;           /* keyboard region / language          */

/* I/O dispatch (TODO: wire to the individual device emulations). */
u8   ioports_read(u16 port);
bool ioports_write(u16 port, u8 value);

/* Reset helpers matching the original IOports surface. */
void ioports_reset(void);        /* full device reset                        */
void ioports_reset_pit(void);    /* recreate just the 8253 PIT (warm reset)  */
void ioports_reset_system(void); /* full hardware init                       */

/* Human-friendly region name for the language selector. */
const char *country_name(Countries c);

#endif /* SYSTEM23_IOPORTS_H */
