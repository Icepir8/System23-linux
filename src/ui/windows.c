/* ===========================================================================
 *  ui/windows.c — secondary windows/dialogs
 *
 *  About is a real GtkAboutDialog.  The Floppy, Debug and Printer windows are
 *  placeholders that state what remains to be ported; each will graduate into
 *  its own module (ui/floppy_dialog.c, ui/debug_window.c, ui/printer_window.c).
 * ===========================================================================*/
#include "ui/windows.h"
#include "system23.h"

void about_dialog_run(GtkWindow *parent)
{
    GtkWidget *d = gtk_about_dialog_new();
    GtkAboutDialog *a = GTK_ABOUT_DIALOG(d);
    gtk_about_dialog_set_program_name(a, APP_NAME);
    gtk_about_dialog_set_version(a, APP_VERSION);
    gtk_about_dialog_set_comments(a,
        "IBM System/23 Datamaster emulator\n"
        "Linux/GTK3 port of the original C#/WinForms project.");
    gtk_about_dialog_set_license_type(a, GTK_LICENSE_UNKNOWN);
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(d), parent);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

/* Small helper for the not-yet-ported windows. */
static void todo_dialog(GtkWindow *parent, const char *title, const char *body)
{
    GtkWidget *d = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", body);
    gtk_window_set_title(GTK_WINDOW(d), title);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

void floppy_dialog_run(GtkWindow *parent)
{
    todo_dialog(parent, "Floppy Drives",
        "The floppy mount dialog (FloppyDrives.cs) is not yet ported.\n"
        "Drive mounts are still restored from and saved to the config file.");
}

void debug_window_show(GtkWindow *parent)
{
    todo_dialog(parent, "Debug",
        "The debugger window (DebugForm.cs) is not yet ported.\n"
        "It will host the register/memory/disassembly views and single-step.");
}

/* A minimal placeholder window kept between toggles. */
void printer_window_set_visible(GtkWindow *parent, gboolean visible)
{
    static GtkWidget *win = NULL;

    if (visible) {
        if (!win) {
            win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
            gtk_window_set_title(GTK_WINDOW(win), "Printer");
            gtk_window_set_default_size(GTK_WINDOW(win), 480, 320);
            if (parent)
                gtk_window_set_transient_for(GTK_WINDOW(win), parent);
            GtkWidget *lbl = gtk_label_new(
                "Printer output (Printer.cs / PrinterWrapAdapter.cs)\n"
                "is not yet ported.");
            gtk_label_set_justify(GTK_LABEL(lbl), GTK_JUSTIFY_CENTER);
            gtk_container_add(GTK_CONTAINER(win), lbl);
            /* Hide (not destroy) on close so the toggle can re-show it. */
            g_signal_connect(win, "delete-event",
                             G_CALLBACK(gtk_widget_hide_on_delete), NULL);
        }
        gtk_widget_show_all(win);
        gtk_window_present(GTK_WINDOW(win));
    } else if (win) {
        gtk_widget_hide(win);
    }
}
