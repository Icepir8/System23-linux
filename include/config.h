/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
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
