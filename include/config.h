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
 *  config.h — persisted operator configuration (port of MachineConfig.cs)
 *
 *  The original stored XML under %LOCALAPPDATA%\System23\config.xml.  On Linux
 *  this maps to an INI file (GKeyFile) under $XDG_CONFIG_HOME/System23/, i.e.
 *  ~/.config/System23/config.ini by default.  Loaded at startup, saved when the
 *  primary window closes.
 * ===========================================================================*/
#ifndef SYSTEM23_CONFIG_H
#define SYSTEM23_CONFIG_H

#include "system23.h"

typedef struct {
    bool has_value;
    int  x, y, w, h;
} WindowBounds;

typedef struct {
    RomVersion   rom_set;
    char         roms_path[512];       /* relative "Roms" by default          */
    Countries    language;

    char         floppy_paths[4][1024];
    bool         floppy_wp[4];

    int          printer_model;        /* PrinterModel (see printer.h)        */

    WindowBounds display_bounds;
    WindowBounds debug_bounds;
} MachineConfig;

/* Populate *c with saved values, or defaults if no config file exists. */
void config_load(MachineConfig *c);

/* Write *c to disk (best effort; failure is silently ignored, as in the
 * original). */
void config_save(const MachineConfig *c);

/* Absolute path of the config file (caller frees with g_free). */
char *config_file_path(void);

#endif /* SYSTEM23_CONFIG_H */
