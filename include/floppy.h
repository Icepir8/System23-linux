/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  floppy.h — mounted-diskette model (port of FloppyController.Drives)
 *
 *  Four drives.  This skeleton tracks the *mount* metadata (path, loaded,
 *  write-protect) so the operator's disk choices survive a restart via the
 *  saved configuration.  The controller-level sector I/O lives in the FDC
 *  (i765a_fdc) and is TODO.
 * ===========================================================================*/
#ifndef SYSTEM23_FLOPPY_H
#define SYSTEM23_FLOPPY_H

#include "system23.h"

#define FLOPPY_DRIVES 4

typedef struct {
    bool loaded;
    bool write_protected;
    char image_path[1024];
} FloppyDrive;

extern FloppyDrive floppy_drives[FLOPPY_DRIVES];

/* Mount an image (records metadata; returns false on bad drive or missing
 * file).  TODO: hand the image to the FDC for real sector access. */
bool floppy_load_disk(int drive, const char *path, bool write_protected);
void floppy_eject(int drive);

#endif /* SYSTEM23_FLOPPY_H */
