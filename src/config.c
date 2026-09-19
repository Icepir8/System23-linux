/* ===========================================================================
 *  config.c — persisted operator configuration via GKeyFile
 * ===========================================================================*/
#include "config.h"

#include <glib.h>
#include <string.h>

char *config_file_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "System23", "config.ini", NULL);
}

static void set_defaults(MachineConfig *c)
{
    memset(c, 0, sizeof *c);
    c->rom_set  = ROS_1_05;
    c->language = COUNTRY_USA;
    g_strlcpy(c->roms_path, "Roms", sizeof c->roms_path);
}

static int clamp_enum(int v, int count, int fallback)
{
    return (v >= 0 && v < count) ? v : fallback;
}

void config_load(MachineConfig *c)
{
    set_defaults(c);

    char      *path = config_file_path();
    GKeyFile  *kf   = g_key_file_new();

    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        /* [machine] */
        if (g_key_file_has_key(kf, "machine", "rom_set", NULL))
            c->rom_set = clamp_enum(g_key_file_get_integer(kf, "machine", "rom_set", NULL),
                                    ROM_VERSION_COUNT, ROS_1_05);
        if (g_key_file_has_key(kf, "machine", "language", NULL))
            c->language = clamp_enum(g_key_file_get_integer(kf, "machine", "language", NULL),
                                     COUNTRY_COUNT, COUNTRY_USA);

        char *rp = g_key_file_get_string(kf, "machine", "roms_path", NULL);
        if (rp && *rp) g_strlcpy(c->roms_path, rp, sizeof c->roms_path);
        g_free(rp);

        /* [floppy] */
        for (int i = 0; i < 4; i++) {
            char key[16];
            g_snprintf(key, sizeof key, "path%d", i);
            char *p = g_key_file_get_string(kf, "floppy", key, NULL);
            if (p) g_strlcpy(c->floppy_paths[i], p, sizeof c->floppy_paths[i]);
            g_free(p);

            g_snprintf(key, sizeof key, "wp%d", i);
            c->floppy_wp[i] = g_key_file_get_boolean(kf, "floppy", key, NULL);
        }

        /* [window] */
        WindowBounds *wb[2] = { &c->display_bounds, &c->debug_bounds };
        const char   *grp[2] = { "display", "debug" };
        for (int i = 0; i < 2; i++) {
            if (g_key_file_get_boolean(kf, "window", grp[i], NULL)) {
                char key[24];
                g_snprintf(key, sizeof key, "%s_x", grp[i]);
                wb[i]->x = g_key_file_get_integer(kf, "window", key, NULL);
                g_snprintf(key, sizeof key, "%s_y", grp[i]);
                wb[i]->y = g_key_file_get_integer(kf, "window", key, NULL);
                g_snprintf(key, sizeof key, "%s_w", grp[i]);
                wb[i]->w = g_key_file_get_integer(kf, "window", key, NULL);
                g_snprintf(key, sizeof key, "%s_h", grp[i]);
                wb[i]->h = g_key_file_get_integer(kf, "window", key, NULL);
                wb[i]->has_value = true;
            }
        }
    }

    g_key_file_free(kf);
    g_free(path);
}

void config_save(const MachineConfig *c)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_integer(kf, "machine", "rom_set",  c->rom_set);
    g_key_file_set_integer(kf, "machine", "language", c->language);
    g_key_file_set_string (kf, "machine", "roms_path", c->roms_path);

    for (int i = 0; i < 4; i++) {
        char key[16];
        g_snprintf(key, sizeof key, "path%d", i);
        g_key_file_set_string(kf, "floppy", key, c->floppy_paths[i]);
        g_snprintf(key, sizeof key, "wp%d", i);
        g_key_file_set_boolean(kf, "floppy", key, c->floppy_wp[i]);
    }

    const WindowBounds *wb[2] = { &c->display_bounds, &c->debug_bounds };
    const char         *grp[2] = { "display", "debug" };
    for (int i = 0; i < 2; i++) {
        g_key_file_set_boolean(kf, "window", grp[i], wb[i]->has_value);
        if (wb[i]->has_value) {
            char key[24];
            g_snprintf(key, sizeof key, "%s_x", grp[i]);
            g_key_file_set_integer(kf, "window", key, wb[i]->x);
            g_snprintf(key, sizeof key, "%s_y", grp[i]);
            g_key_file_set_integer(kf, "window", key, wb[i]->y);
            g_snprintf(key, sizeof key, "%s_w", grp[i]);
            g_key_file_set_integer(kf, "window", key, wb[i]->w);
            g_snprintf(key, sizeof key, "%s_h", grp[i]);
            g_key_file_set_integer(kf, "window", key, wb[i]->h);
        }
    }

    char *path = config_file_path();
    char *dir  = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_key_file_save_to_file(kf, path, NULL);   /* errors intentionally ignored */

    g_free(dir);
    g_free(path);
    g_key_file_free(kf);
}
