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
 *  assets.h — locating the bundled ROM images and character-ROM graphics
 *
 *  The original resolved "Roms" and "Graphics" relative to the executable.
 *  Here we search a small list of candidate base directories so the binary
 *  runs whether it is launched from the build tree, an install prefix, or with
 *  assets copied next to it:
 *
 *      1. $SYSTEM23_HOME               (explicit override)
 *      2. the directory of the executable
 *      3. one level above the executable   (e.g. bin/ -> project root)
 *      4. the current working directory
 * ===========================================================================*/
#ifndef SYSTEM23_ASSETS_H
#define SYSTEM23_ASSETS_H

#include "system23.h"
#include <gdk-pixbuf/gdk-pixbuf.h>

/* Base directory that contains "Roms" and/or "Graphics" (cached; do not free).
 * Falls back to the executable directory if nothing matches. */
const char *assets_base(void);

/* Build an absolute path to <base>/<subpath>; caller frees with g_free(). */
char *assets_path(const char *subpath);

/* Load a pixbuf from <base>/Graphics/<name>, or NULL on failure. */
GdkPixbuf *assets_load_graphic(const char *name);

/* Load the four character-ROM bitmaps.  Any output may be NULL on failure.
 * Returns true only if all four loaded. */
bool assets_load_char_roms(GdkPixbuf **normal,
                           GdkPixbuf **highlight,
                           GdkPixbuf **inverted,
                           GdkPixbuf **inverted_hl);

#endif /* SYSTEM23_ASSETS_H */
