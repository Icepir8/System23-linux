/* ===========================================================================
 *  floppy.c — mounted-diskette model (metadata only for now)
 * ===========================================================================*/
#include "floppy.h"

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
    /* TODO: register the image with the FDC for sector I/O. */
    return true;
}

void floppy_eject(int drive)
{
    if (drive < 0 || drive >= FLOPPY_DRIVES)
        return;
    memset(&floppy_drives[drive], 0, sizeof floppy_drives[drive]);
}
