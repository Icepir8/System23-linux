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
