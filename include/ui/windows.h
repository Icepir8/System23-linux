/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  ui/windows.h — the on-demand secondary windows/dialogs
 *
 *  Placeholders for the WinForms windows still to be ported.  Each maps to a
 *  future module of its own (noted beside it).  About is already a real dialog.
 * ===========================================================================*/
#ifndef SYSTEM23_UI_WINDOWS_H
#define SYSTEM23_UI_WINDOWS_H

#include <gtk/gtk.h>

void about_dialog_run(GtkWindow *parent);                       /* FormAbout   */
void floppy_dialog_run(GtkWindow *parent);                      /* FloppyDrives*/
void debug_window_show(GtkWindow *parent);                      /* DebugForm   */
void printer_window_set_visible(GtkWindow *parent, gboolean v); /* Printer     */

#endif /* SYSTEM23_UI_WINDOWS_H */
