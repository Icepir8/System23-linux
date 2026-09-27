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
 *  assets.c — asset-directory resolution and character-ROM loading
 * ===========================================================================*/
#include "assets.h"

#include <glib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

/* Absolute directory of the running executable (Linux: /proc/self/exe). */
static char *executable_dir(void)
{
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0)
        return g_strdup(".");
    buf[n] = '\0';
    return g_path_get_dirname(buf);
}

/* True if dir contains a "Roms" or "Graphics" subfolder. */
static gboolean looks_like_base(const char *dir)
{
    gboolean ok = FALSE;
    char *roms = g_build_filename(dir, "Roms", NULL);
    char *gfx  = g_build_filename(dir, "Graphics", NULL);
    if (g_file_test(roms, G_FILE_TEST_IS_DIR) || g_file_test(gfx, G_FILE_TEST_IS_DIR))
        ok = TRUE;
    g_free(roms);
    g_free(gfx);
    return ok;
}

const char *assets_base(void)
{
    static char *cached = NULL;
    if (cached)
        return cached;

    char *exe_dir  = executable_dir();
    char *exe_up   = g_path_get_dirname(exe_dir);
    char *cwd      = g_get_current_dir();

    const char *env = g_getenv("SYSTEM23_HOME");
    const char *candidates[4] = { env, exe_dir, exe_up, cwd };

    /* NB: iterate a fixed count — a NULL entry (e.g. unset SYSTEM23_HOME) must
     * be skipped, not treated as the end of the list. */
    for (int i = 0; i < 4; i++) {
        if (candidates[i] && *candidates[i] && looks_like_base(candidates[i])) {
            cached = g_strdup(candidates[i]);
            break;
        }
    }
    if (!cached)
        cached = g_strdup(exe_dir);   /* sensible fallback */

    g_free(exe_dir);
    g_free(exe_up);
    g_free(cwd);
    return cached;
}

char *assets_path(const char *subpath)
{
    return g_build_filename(assets_base(), subpath, NULL);
}

GdkPixbuf *assets_load_graphic(const char *name)
{
    char      *rel  = g_build_filename("Graphics", name, NULL);
    char      *path = assets_path(rel);
    GError    *err  = NULL;
    GdkPixbuf *pb   = gdk_pixbuf_new_from_file(path, &err);
    if (!pb) {
        g_warning("could not load graphic '%s': %s", path,
                  err ? err->message : "unknown error");
        if (err) g_error_free(err);
    }
    g_free(rel);
    g_free(path);
    return pb;
}

bool assets_load_char_roms(GdkPixbuf **normal, GdkPixbuf **highlight,
                           GdkPixbuf **inverted, GdkPixbuf **inverted_hl)
{
    *normal      = assets_load_graphic("char3.bmp");
    *highlight   = assets_load_graphic("char3hl.bmp");
    *inverted    = assets_load_graphic("char3inv.bmp");
    *inverted_hl = assets_load_graphic("char3invhl.bmp");
    return *normal && *highlight && *inverted && *inverted_hl;
}
