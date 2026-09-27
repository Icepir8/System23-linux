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
