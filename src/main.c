/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  main.c — entry point (port of Program.cs + the windowless MachineHost boot)
 *
 *  Initialise the machine (load config + ROMs, cold power-on WITHOUT running),
 *  bring up the Display as the primary window, and hand control to the
 *  operator.  Closing the Display saves the configuration and exits.
 * ===========================================================================*/
#include <gtk/gtk.h>

#include "machine.h"
#include "config.h"
#include "ui/display.h"

static GtkWidget *g_display_window;

/* Restore the saved window position/size, if it looks usable. */
static void apply_display_bounds(GtkWindow *win)
{
    const WindowBounds *b = &g_config.display_bounds;
    if (!b->has_value) return;
    if (b->w > 0 && b->h > 0)
        gtk_window_resize(win, b->w, b->h);
    gtk_window_move(win, b->x, b->y);
}

/* Capture the current window geometry back into the config. */
static void capture_display_bounds(GtkWindow *win)
{
    int x = 0, y = 0, w = 0, h = 0;
    gtk_window_get_position(win, &x, &y);
    gtk_window_get_size(win, &w, &h);
    g_config.display_bounds.has_value = true;
    g_config.display_bounds.x = x;
    g_config.display_bounds.y = y;
    g_config.display_bounds.w = w;
    g_config.display_bounds.h = h;
}

static void on_destroy(GtkWidget *w, gpointer data)
{
    (void)w; (void)data;
    capture_display_bounds(GTK_WINDOW(g_display_window));
    machine_shutdown();     /* stops emulation + saves config */
    gtk_main_quit();
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    machine_init();         /* config + ROMs + cold init (no run) */

    g_display_window = display_new();
    apply_display_bounds(GTK_WINDOW(g_display_window));
    g_signal_connect(g_display_window, "destroy", G_CALLBACK(on_destroy), NULL);

    gtk_widget_show_all(g_display_window);
    gtk_widget_grab_focus(g_display_window);

    /* Dev/testing hook: cold-boot immediately instead of waiting for the
     * operator to click Power (default behaviour is unchanged). */
    if (g_getenv("SYSTEM23_AUTOSTART"))
        machine_start();

    gtk_main();
    return 0;
}
