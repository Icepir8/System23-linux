/* ===========================================================================
 *  ui/display.h — primary CRT window (port of Display.cs)
 *
 *  The System/23's main window: an operator toolbar (power, ROM-set and
 *  language selectors, floppy/debug/printer/about) above the 80x24 green
 *  character display rendered from RAM through the character ROM.
 * ===========================================================================*/
#ifndef SYSTEM23_UI_DISPLAY_H
#define SYSTEM23_UI_DISPLAY_H

#include <gtk/gtk.h>

/* Create and return the top-level Display window (not yet shown). */
GtkWidget *display_new(void);

#endif /* SYSTEM23_UI_DISPLAY_H */
