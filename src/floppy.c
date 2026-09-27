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
 *  floppy.c — mounted-diskette model (metadata only for now)
 * ===========================================================================*/
#include "floppy.h"
#include "i765a_fdc.h"

#include <stdio.h>
#include <string.h>

FloppyDrive floppy_drives[FLOPPY_DRIVES];

bool floppy_load_disk(int drive, const char *path, bool write_protected)
{
    if (drive < 0 || drive >= FLOPPY_DRIVES || !path || !*path)
        return false;

    FILE *f = fopen(path, "rb");   /* verify the image exists / is readable */
    if (!f)
        return false;
    fclose(f);

    floppy_drives[drive].loaded          = true;
    floppy_drives[drive].write_protected = write_protected;
    snprintf(floppy_drives[drive].image_path,
             sizeof floppy_drives[drive].image_path, "%s", path);
    /* Parse the image and hand its sectors to the FDC (also marks it loaded). */
    if (!fdc_load_image(drive, path, write_protected)) {
        fdc_set_disk(drive, true, write_protected);   /* at least mark present */
    }
    return true;
}

void floppy_eject(int drive)
{
    if (drive < 0 || drive >= FLOPPY_DRIVES)
        return;
    memset(&floppy_drives[drive], 0, sizeof floppy_drives[drive]);
    fdc_set_disk(drive, false, false);
}
