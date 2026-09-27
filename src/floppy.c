/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
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
