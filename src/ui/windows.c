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
 *  ui/windows.c — secondary windows/dialogs
 *
 *  About is a real GtkAboutDialog.  The Floppy dialog mounts/ejects images; the
 *  Debug window is a full 8085 debugger (run/stop/step, breakpoints, register &
 *  flag editing, memory poke/search, range disassembly); the Printer window
 *  captures spooled output.
 * ===========================================================================*/
#include "ui/windows.h"
#include "system23.h"
#include "floppy.h"
#include "cpu8085.h"
#include "memory.h"
#include "machine.h"
#include "ioports.h"
#include "disassembler85.h"
#include "i8251.h"
#include "assets.h"
#include "printer.h"
#include <stdlib.h>
#include <math.h>

#include <string.h>

void about_dialog_run(GtkWindow *parent)
{
    GtkWidget *d = gtk_about_dialog_new();
    GtkAboutDialog *a = GTK_ABOUT_DIALOG(d);
    gtk_about_dialog_set_program_name(a, APP_NAME);
    gtk_about_dialog_set_version(a, APP_VERSION);

    /* Header logo: the Large Scale Systems Museum mark, scaled to a header size.
     * (Its black caption sits on the dialog's light background, so no panel is
     * needed here.) */
    GdkPixbuf *logo = assets_load_graphic("lssm_logo.png");
    if (logo) {
        int lw = gdk_pixbuf_get_width(logo), lh = gdk_pixbuf_get_height(logo);
        int tw = (lw > 300) ? 300 : lw;
        int th = (int)((double)lh * tw / lw + 0.5);
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(logo, tw, th, GDK_INTERP_BILINEAR);
        gtk_about_dialog_set_logo(a, scaled ? scaled : logo);
        if (scaled) g_object_unref(scaled);
        g_object_unref(logo);
    }
    gtk_about_dialog_set_comments(a,
        "IBM System/23 Datamaster emulator\n"
        "Linux/GTK3 port of the original C#/WinForms project.");
    gtk_about_dialog_set_copyright(a, "Copyright \xC2\xA9 2026 Owen V. Michael, Jr.");
    gtk_about_dialog_set_license_type(a, GTK_LICENSE_UNKNOWN);

    /* Contact + acknowledgement with live links.  A GtkLabel with <a href>
     * markup emits activate-link, which GTK opens for mailto:/https: by the
     * default handler (browser / mail client on the desktop). */
    GtkWidget *extra = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(extra),
        "<a href=\"mailto:ovmichael@lssmuseum.org\">ovmichael@lssmuseum.org</a>\n\n"
        "With thanks to the "
        "<a href=\"https://www.mact.io/\">Large Scale Systems Museum</a>");
    gtk_label_set_justify(GTK_LABEL(extra), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(extra, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(extra, 8);
    gtk_widget_set_margin_bottom(extra, 8);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d))),
                       extra, FALSE, FALSE, 0);
    gtk_widget_show(extra);

    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(d), parent);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

/* ---- Floppy Drives dialog (port of FloppyDrives.cs) ----------------------*/
typedef struct {
    int        drive;
    GtkWidget *entry;   /* shows the mounted image path */
    GtkWidget *wp;      /* write-protect checkbox        */
    GtkWindow *parent;
} FloppyRow;

static void floppy_row_refresh(FloppyRow *r)
{
    gtk_entry_set_text(GTK_ENTRY(r->entry),
        floppy_drives[r->drive].loaded ? floppy_drives[r->drive].image_path : "");
}

static void on_floppy_browse(GtkButton *b, gpointer data)
{
    (void)b;
    FloppyRow *r = (FloppyRow *)data;
    GtkWidget *chooser = gtk_file_chooser_dialog_new(
        "Select diskette image", r->parent, GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);

    GtkFileFilter *imd = gtk_file_filter_new();
    gtk_file_filter_set_name(imd, "Diskette images (*.imd, *.img, *.bin)");
    gtk_file_filter_add_pattern(imd, "*.imd");  gtk_file_filter_add_pattern(imd, "*.IMD");
    gtk_file_filter_add_pattern(imd, "*.img");  gtk_file_filter_add_pattern(imd, "*.IMG");
    gtk_file_filter_add_pattern(imd, "*.bin");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), imd);
    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), all);

    if (floppy_drives[r->drive].loaded && floppy_drives[r->drive].image_path[0])
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser),
                                      floppy_drives[r->drive].image_path);

    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (path) {
            bool wp = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r->wp));
            floppy_eject(r->drive);
            floppy_load_disk(r->drive, path, wp);
            floppy_row_refresh(r);
            g_free(path);
        }
    }
    gtk_widget_destroy(chooser);
}

static void on_floppy_eject(GtkButton *b, gpointer data)
{
    (void)b;
    FloppyRow *r = (FloppyRow *)data;
    floppy_eject(r->drive);
    floppy_row_refresh(r);
}

void floppy_dialog_run(GtkWindow *parent)
{
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Floppy Drives", parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 620, -1);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    static FloppyRow rows[FLOPPY_DRIVES];   /* static: outlive the modal loop  */
    for (int i = 0; i < FLOPPY_DRIVES; i++) {
        char lbl[16];
        snprintf(lbl, sizeof lbl, "Drive %d:", i + 1);
        GtkWidget *name  = gtk_label_new(lbl);
        gtk_widget_set_halign(name, GTK_ALIGN_START);
        GtkWidget *entry = gtk_entry_new();
        gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
        gtk_widget_set_hexpand(entry, TRUE);
        GtkWidget *wp    = gtk_check_button_new_with_label("WP");
        GtkWidget *browse= gtk_button_new_with_label("Mount…");
        GtkWidget *eject = gtk_button_new_with_label("Eject");

        rows[i].drive  = i;
        rows[i].entry  = entry;
        rows[i].wp     = wp;
        rows[i].parent = GTK_WINDOW(dlg);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wp),
                                     floppy_drives[i].write_protected);
        floppy_row_refresh(&rows[i]);

        g_signal_connect(browse, "clicked", G_CALLBACK(on_floppy_browse), &rows[i]);
        g_signal_connect(eject,  "clicked", G_CALLBACK(on_floppy_eject),  &rows[i]);

        gtk_grid_attach(GTK_GRID(grid), name,   0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), entry,  1, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), wp,     2, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), browse, 3, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), eject,  4, i, 1, 1);
    }

    /* IBM 5247 hard disk — support incomplete, shown disabled as in the C#. */
    GtkWidget *sasi = gtk_label_new("5247 hard disk: (support disabled)");
    gtk_widget_set_halign(sasi, GTK_ALIGN_START);
    gtk_widget_set_sensitive(sasi, FALSE);
    gtk_grid_attach(GTK_GRID(grid), sasi, 0, FLOPPY_DRIVES, 5, 1);

    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), grid);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

/* ---- Disassembly listing window (unassemble an address range) ------------*/
static struct {
    GtkWidget *win;
    GtkWidget *start, *end;   /* hex range entries                            */
    GtkWidget *view;          /* listing text view                            */
} da;

/* Disassemble From..To (inclusive) into the listing view.  Generated on demand
 * — it is NOT on the debug window's refresh timer, so the listing stays put and
 * scrollable while you read it.  With "To" blank it lists 64 instructions from
 * the start (one page); with a range it lists until the range is covered, up to
 * a safety cap.  PC (\xE2\x96\xB6) and breakpoints (\xE2\x97\x8F) are marked. */
static void da_list(void)
{
    const char *st = gtk_entry_get_text(GTK_ENTRY(da.start));
    const char *et = gtk_entry_get_text(GTK_ENTRY(da.end));
    long s = (st && *st) ? (strtol(st, NULL, 16) & 0xFFFF) : cpu.pc;
    long e = (et && *et) ? (strtol(et, NULL, 16) & 0xFFFF) : -1;

    gboolean have_end = (e >= 0 && e >= s);
    int max = have_end ? 4000 : 64;      /* line cap (safety / one page)       */

    GString *g = g_string_new(NULL);
    u16 a = (u16)s;
    for (int i = 0; i < max; i++) {
        char ins[48];
        int n = disassembler85_at(a, ins, sizeof ins);
        if (n < 1) n = 1;

        char bytes[16]; int bo = 0;      /* raw opcode bytes (up to 3)         */
        for (int b = 0; b < n && b < 3; b++)
            bo += snprintf(bytes + bo, sizeof bytes - bo,
                           b ? " %02X" : "%02X", memory_read((u16)(a + b)));

        const char *pcm = (a == cpu.pc)     ? "\xE2\x96\xB6" : " ";
        const char *bpm = machine_bp_has(a) ? "\xE2\x97\x8F" : " ";
        g_string_append_printf(g, "%s%s %04X:  %-8s  %s\n", pcm, bpm, a, bytes, ins);

        long na = (long)a + n;
        if (na > 0xFFFF) break;          /* stop at the top of memory          */
        if (have_end && na > e) break;   /* whole range covered                */
        a = (u16)na;
    }
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(da.view)), g->str, -1);
    g_string_free(g, TRUE);
}

static void     on_da_list(GtkWidget *w, gpointer d) { (void)w; (void)d; da_list(); }
static gboolean da_on_delete(GtkWidget *w, GdkEvent *e, gpointer d)
{
    (void)w; (void)e; (void)d;
    gtk_widget_hide(da.win);
    return TRUE;   /* hide, don't destroy */
}

static void disasm_window_show(GtkWindow *parent)
{
    if (!da.win) {
        da.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(da.win), "Disassembly \xE2\x80\x94 range");
        gtk_window_set_default_size(GTK_WINDOW(da.win), 480, 560);
        if (parent) gtk_window_set_transient_for(GTK_WINDOW(da.win), parent);
        g_signal_connect(da.win, "delete-event", G_CALLBACK(da_on_delete), NULL);

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_container_set_border_width(GTK_CONTAINER(box), 8);

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_box_pack_start(GTK_BOX(row), gtk_label_new("From [hex]:"), FALSE, FALSE, 0);
        da.start = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(da.start), 6);
        gtk_entry_set_max_length(GTK_ENTRY(da.start), 4);
        gtk_box_pack_start(GTK_BOX(row), da.start, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), gtk_label_new("to [hex]:"), FALSE, FALSE, 0);
        da.end = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(da.end), 6);
        gtk_entry_set_max_length(GTK_ENTRY(da.end), 4);
        gtk_box_pack_start(GTK_BOX(row), da.end, FALSE, FALSE, 0);
        GtkWidget *list = gtk_button_new_with_label("List");
        g_signal_connect(da.start, "activate", G_CALLBACK(on_da_list), NULL);
        g_signal_connect(da.end,   "activate", G_CALLBACK(on_da_list), NULL);
        g_signal_connect(list,     "clicked",  G_CALLBACK(on_da_list), NULL);
        gtk_box_pack_start(GTK_BOX(row), list, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

        GtkWidget *hint = gtk_label_new(
            "Leave \"to\" blank to list 64 instructions from the start.");
        gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
        gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

        da.view = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(da.view), FALSE);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(da.view), TRUE);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(da.view), FALSE);
        GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc),
            GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(sc), da.view);
        gtk_box_pack_start(GTK_BOX(box), sc, TRUE, TRUE, 0);

        gtk_container_add(GTK_CONTAINER(da.win), box);
    }
    /* Prefill the start with the current PC when it's empty, so opening the
     * window and clicking List "just works". */
    if (da.start) {
        const char *cur = gtk_entry_get_text(GTK_ENTRY(da.start));
        if (!cur || !*cur) {
            char pc[8]; snprintf(pc, sizeof pc, "%04X", cpu.pc);
            gtk_entry_set_text(GTK_ENTRY(da.start), pc);
        }
    }
    gtk_widget_show_all(da.win);
    gtk_window_present(GTK_WINDOW(da.win));
    da_list();
}

/* ---- Debug window: registers + disassembly + memory + run controls -------*/
static struct {
    GtkWidget *win;
    GtkWidget *state;    /* run-state label                */
    GtkWidget *b_run, *b_stop, *b_step, *b_reset;
    GtkWidget *regs;     /* register/flag line (label)     */
    GtkWidget *code;     /* disassembly text view          */
    GtkWidget *bp_entry; /* breakpoint address entry       */
    GtkWidget *bp_list;  /* breakpoint list (label)        */
    GtkWidget *mem;      /* memory hex text view           */
    GtkWidget *mp_addr, *mp_val;   /* memory poke: addr = value        */
    GtkWidget *pk_port, *pk_val;   /* port out:    port <= value       */
    GtkWidget *rg_combo, *rg_val;  /* register set: reg = value        */
    GtkWidget *fl[7];              /* flag toggles S Z AC P C V K       */
    GtkWidget *srch;              /* memory search entry               */
    GtkWidget *srch_hex;          /* "hex" radio (else EBCDIC text)    */
    GtkWidget *srch_txt;          /* "text" radio                      */
    GtkWidget *srch_res;          /* search result label               */
    GtkWidget *mem_goto;          /* memory view "go to address" entry */
    guint      timer;    /* refresh source id              */
} dbg;

static gboolean  dbg_refresh(gpointer data);
static gboolean  dbg_updating = FALSE;   /* suppress toggle callbacks on refresh */
static long      dbg_mem_view = -1;      /* memory-view address (-1 => follow HL)*/
static long      dbg_search_next = 0;    /* where Find-Next resumes             */

/* ASCII -> EBCDIC (CP037).  The System/23 stores text in EBCDIC internally, so
 * a text memory-search converts the typed ASCII before scanning.  Letters and
 * digits are the same across EBCDIC code pages; punctuation follows CP037. */
static const unsigned char ascii_to_ebcdic[128] = {
/*00*/ 0x00,0x01,0x02,0x03,0x37,0x2D,0x2E,0x2F,0x16,0x05,0x25,0x0B,0x0C,0x0D,0x0E,0x0F,
/*10*/ 0x10,0x11,0x12,0x13,0x3C,0x3D,0x32,0x26,0x18,0x19,0x3F,0x27,0x1C,0x1D,0x1E,0x1F,
/*20*/ 0x40,0x5A,0x7F,0x7B,0x5B,0x6C,0x50,0x7D,0x4D,0x5D,0x5C,0x4E,0x6B,0x60,0x4B,0x61,
/*30*/ 0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0x7A,0x5E,0x4C,0x7E,0x6E,0x6F,
/*40*/ 0x7C,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,
/*50*/ 0xD7,0xD8,0xD9,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xAD,0xE0,0xBD,0x5F,0x6D,
/*60*/ 0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
/*70*/ 0x97,0x98,0x99,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xC0,0x4F,0xD0,0xA1,0x07,
};

/* EBCDIC byte -> printable ASCII (or '.'), inverted from the table above so the
 * memory dump reads System/23 text.  Built once on first use. */
static char ebcdic_to_ascii(unsigned char e)
{
    static char rev[256]; static int built = 0;
    if (!built) {
        for (int i = 0; i < 256; i++) rev[i] = '.';
        for (int c = 0x20; c < 0x7F; c++) rev[ascii_to_ebcdic[c]] = (char)c;
        built = 1;
    }
    return rev[e];
}

/* Run controls. */
static void on_dbg_run(GtkButton *b, gpointer d)   { (void)b; (void)d; machine_start(); dbg_refresh(NULL); }
static void on_dbg_stop(GtkButton *b, gpointer d)  { (void)b; (void)d; machine_stop();  dbg_refresh(NULL); }
static void on_dbg_step(GtkButton *b, gpointer d)  { (void)b; (void)d; machine_step();  dbg_refresh(NULL); }
static void on_dbg_reset(GtkButton *b, gpointer d) { (void)b; (void)d; machine_power_off(); dbg_refresh(NULL); }

/* Breakpoints (entry holds a hex PC; Enter or Add toggles/adds it). */
static void on_dbg_bp_add(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    const char *t = gtk_entry_get_text(GTK_ENTRY(dbg.bp_entry));
    if (t && *t) { machine_bp_add((u16)strtol(t, NULL, 16));
                   gtk_entry_set_text(GTK_ENTRY(dbg.bp_entry), ""); dbg_refresh(NULL); }
}
static void on_dbg_bp_remove(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    const char *t = gtk_entry_get_text(GTK_ENTRY(dbg.bp_entry));
    if (t && *t) { machine_bp_remove((u16)strtol(t, NULL, 16));
                   gtk_entry_set_text(GTK_ENTRY(dbg.bp_entry), ""); dbg_refresh(NULL); }
}
static void on_dbg_bp_clear(GtkButton *b, gpointer d)
{
    (void)b; (void)d; machine_bp_clear_all(); dbg_refresh(NULL);
}

/* ---- Pokes: edit memory, write a port, set a register --------------------*/
static long dbg_hex(GtkWidget *e)   /* -1 if the entry is empty */
{
    const char *t = gtk_entry_get_text(GTK_ENTRY(e));
    return (t && *t) ? strtol(t, NULL, 16) : -1;
}

static void on_dbg_mem_set(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    long a = dbg_hex(dbg.mp_addr), v = dbg_hex(dbg.mp_val);
    if (a >= 0 && v >= 0) { memory_write((u16)a, (u8)v); dbg_refresh(NULL); }
}
static void on_dbg_port_out(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    long p = dbg_hex(dbg.pk_port), v = dbg_hex(dbg.pk_val);
    if (p >= 0 && v >= 0) { ioports_write((u16)p, (u8)v); dbg_refresh(NULL); }
}
static void on_dbg_reg_set(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    long v = dbg_hex(dbg.rg_val);
    if (v < 0) return;
    switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dbg.rg_combo))) {
        case 0: cpu.a  = (u8)v;  break;
        case 1: cpu.b  = (u8)v;  break;
        case 2: cpu.c  = (u8)v;  break;
        case 3: cpu.d  = (u8)v;  break;
        case 4: cpu.e  = (u8)v;  break;
        case 5: cpu.h  = (u8)v;  break;
        case 6: cpu.l  = (u8)v;  break;
        case 7: cpu.pc = (u16)v; break;
        case 8: cpu.sp = (u16)v; break;
        default: return;
    }
    dbg_refresh(NULL);
}

/* ---- Flag toggles (S Z AC P C V K) ---------------------------------------*/
static void on_dbg_flag(GtkToggleButton *b, gpointer which)
{
    if (dbg_updating) return;                      /* ignore refresh-driven sets */
    bool v = gtk_toggle_button_get_active(b);
    switch (GPOINTER_TO_INT(which)) {
        case 0: cpu.fS = v; break;  case 1: cpu.fZ = v; break;
        case 2: cpu.fAC = v; break; case 3: cpu.fP = v; break;
        case 4: cpu.fC = v; break;  case 5: cpu.fV = v; break;
        case 6: cpu.fK = v; break;
    }
    dbg_refresh(NULL);
}

/* ---- Memory search (hex bytes or EBCDIC text) ----------------------------*/
static int dbg_parse_pattern(unsigned char *pat, int maxlen)
{
    const char *t = gtk_entry_get_text(GTK_ENTRY(dbg.srch));
    if (!t || !*t) return 0;
    int n = 0;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dbg.srch_hex))) {
        char buf[256]; g_strlcpy(buf, t, sizeof buf);
        char *save = NULL;
        for (char *tok = strtok_r(buf, " ,", &save); tok && n < maxlen;
             tok = strtok_r(NULL, " ,", &save))
            pat[n++] = (unsigned char)strtol(tok, NULL, 16);
    } else {
        for (const char *p = t; *p && n < maxlen; p++)
            pat[n++] = ascii_to_ebcdic[(unsigned char)*p & 0x7F];
    }
    return n;
}
static long dbg_mem_find(const unsigned char *pat, int plen, long start)
{
    for (long a = (start < 0 ? 0 : start); a + plen <= 0x10000; a++) {
        int i; for (i = 0; i < plen; i++)
            if (memory_read((u16)(a + i)) != pat[i]) break;
        if (i == plen) return a;
    }
    return -1;
}
static void dbg_do_search(long start)
{
    unsigned char pat[64];
    int plen = dbg_parse_pattern(pat, (int)sizeof pat);
    if (plen <= 0) { gtk_label_set_text(GTK_LABEL(dbg.srch_res), "(enter a pattern)"); return; }
    long at = dbg_mem_find(pat, plen, start);
    if (at < 0) {
        gtk_label_set_text(GTK_LABEL(dbg.srch_res),
                           start > 0 ? "no more matches" : "not found");
        dbg_search_next = 0;
    } else {
        char msg[64];
        snprintf(msg, sizeof msg, "found at %04lX  (%d byte%s)", at, plen, plen == 1 ? "" : "s");
        gtk_label_set_text(GTK_LABEL(dbg.srch_res), msg);
        dbg_mem_view = at;                 /* jump the memory view there */
        dbg_search_next = at + 1;
    }
    dbg_refresh(NULL);
}
static void on_dbg_find(GtkWidget *w, gpointer d)      { (void)w; (void)d; dbg_do_search(0); }
static void on_dbg_find_next(GtkButton *b, gpointer d) { (void)b; (void)d; dbg_do_search(dbg_search_next); }
static void on_dbg_view_hl(GtkButton *b, gpointer d)   { (void)b; (void)d; dbg_mem_view = -1; dbg_refresh(NULL); }

/* Point the memory dump at a typed address (\xE2\x86\x92HL clears it back to HL). */
static void on_dbg_mem_goto(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    const char *t = gtk_entry_get_text(GTK_ENTRY(dbg.mem_goto));
    if (t && *t) { dbg_mem_view = strtol(t, NULL, 16) & 0xFFFF; dbg_refresh(NULL); }
}

/* Open the range-disassembly listing window. */
static void on_dbg_disasm_range(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    disasm_window_show(dbg.win ? GTK_WINDOW(dbg.win) : NULL);
}

static void dbg_set_monospace(GtkWidget *w)
{
    PangoAttrList *al = pango_attr_list_new();
    pango_attr_list_insert(al, pango_attr_family_new("monospace"));
    gtk_label_set_attributes(GTK_LABEL(w), al);
    pango_attr_list_unref(al);
}

static gboolean dbg_refresh(gpointer data)
{
    (void)data;

    /* Run state + control sensitivity. */
    ExecState st = machine_state();
    gboolean running = (st == EXEC_RUNNING);
    gtk_label_set_text(GTK_LABEL(dbg.state),
                       running ? "\xE2\x97\x8F RUNNING" : "\xE2\x97\x8B STOPPED");
    gtk_widget_set_sensitive(dbg.b_run,  !running);
    gtk_widget_set_sensitive(dbg.b_step, !running);
    gtk_widget_set_sensitive(dbg.b_stop,  running);

    char line[256];
    snprintf(line, sizeof line,
        "A=%02X  B=%02X C=%02X  D=%02X E=%02X  H=%02X L=%02X   PC=%04X SP=%04X\n"
        "flags:  %c%c%c%c%c%c%c    IE=%d",
        cpu.a, cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l, cpu.pc, cpu.sp,
        cpu.fS ? 'S' : '-', cpu.fZ ? 'Z' : '-', cpu.fAC ? 'A' : '-',
        cpu.fP ? 'P' : '-', cpu.fC ? 'C' : '-', cpu.fV ? 'V' : '-',
        cpu.fK ? 'K' : '-', cpu.ie);
    gtk_label_set_text(GTK_LABEL(dbg.regs), line);

    /* Sync the flag toggles (blocked so the set doesn't call back). */
    if (dbg.fl[0]) {
        const bool fv[7] = { cpu.fS, cpu.fZ, cpu.fAC, cpu.fP, cpu.fC, cpu.fV, cpu.fK };
        dbg_updating = TRUE;
        for (int i = 0; i < 7; i++)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dbg.fl[i]), fv[i]);
        dbg_updating = FALSE;
    }

    /* Disassembly: 12 instructions from PC, with PC (\xE2\x96\xB6) and
     * breakpoint (\xE2\x97\x8F) markers in the left gutter. */
    char code[1024]; int off = 0; u16 a = cpu.pc;
    for (int i = 0; i < 12; i++) {
        char ins[48];
        int n = disassembler85_at(a, ins, sizeof ins);
        const char *pcm = (a == cpu.pc)       ? "\xE2\x96\xB6" : " ";
        const char *bpm = machine_bp_has(a)   ? "\xE2\x97\x8F" : " ";
        off += snprintf(code + off, sizeof code - off, "%s%s %04X:  %s\n",
                        pcm, bpm, a, ins);
        if (off >= (int)sizeof code - 64) break;
        a = (u16)(a + (n > 0 ? n : 1));
    }
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(dbg.code)), code, -1);

    /* Breakpoint list. */
    u16 bps[64]; int nbp = machine_bp_list(bps, 64);
    char bl[512]; int bo = 0;
    bo += snprintf(bl + bo, sizeof bl - bo, "Breakpoints (%d): ", nbp);
    for (int i = 0; i < nbp && bo < (int)sizeof bl - 8; i++)
        bo += snprintf(bl + bo, sizeof bl - bo, "%04X ", bps[i]);
    if (nbp == 0) snprintf(bl, sizeof bl, "Breakpoints (0): (none)");
    gtk_label_set_text(GTK_LABEL(dbg.bp_list), bl);

    /* Memory: 8 rows of 16 bytes around HL, or a search hit / manual view. */
    u16 center = (dbg_mem_view >= 0) ? (u16)dbg_mem_view
                                     : (u16)((cpu.h << 8) | cpu.l);
    u16 base = (u16)(center & 0xFFF0);
    char hex[1024]; off = 0;
    for (int r = 0; r < 8; r++) {
        u16 rb = (u16)(base + r * 16);
        off += snprintf(hex + off, sizeof hex - off, "%04X: ", rb);
        for (int c = 0; c < 16; c++)
            off += snprintf(hex + off, sizeof hex - off, "%02X ", memory_read((u16)(rb + c)));
        off += snprintf(hex + off, sizeof hex - off, " ");
        for (int c = 0; c < 16; c++)          /* EBCDIC-decoded text column */
            off += snprintf(hex + off, sizeof hex - off, "%c",
                            ebcdic_to_ascii(memory_read((u16)(rb + c))));
        off += snprintf(hex + off, sizeof hex - off, "\n");
    }
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(dbg.mem)), hex, -1);
    return G_SOURCE_CONTINUE;
}

static gboolean dbg_on_delete(GtkWidget *w, GdkEvent *e, gpointer d)
{
    (void)w; (void)e; (void)d;
    if (dbg.timer) { g_source_remove(dbg.timer); dbg.timer = 0; }
    gtk_widget_hide(dbg.win);
    return TRUE;   /* hide, don't destroy */
}

static GtkWidget *dbg_make_textview(GtkWidget **view, int rows)
{
    *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(*view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(*view), TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(*view), FALSE);
    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(sc, -1, rows);
    gtk_container_add(GTK_CONTAINER(sc), *view);
    return sc;
}

void debug_window_show(GtkWindow *parent)
{
    if (!dbg.win) {
        dbg.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(dbg.win), "Debug — 8085");
        gtk_window_set_default_size(GTK_WINDOW(dbg.win), 600, 760);
        if (parent) gtk_window_set_transient_for(GTK_WINDOW(dbg.win), parent);
        g_signal_connect(dbg.win, "delete-event", G_CALLBACK(dbg_on_delete), NULL);

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_container_set_border_width(GTK_CONTAINER(box), 8);

        /* Control toolbar: state + Run / Stop / Step / Reset. */
        GtkWidget *ctl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        dbg.state = gtk_label_new("");
        dbg_set_monospace(dbg.state);
        gtk_widget_set_size_request(dbg.state, 110, -1);
        gtk_label_set_xalign(GTK_LABEL(dbg.state), 0.0f);
        dbg.b_run   = gtk_button_new_with_label("Run");
        dbg.b_stop  = gtk_button_new_with_label("Stop");
        dbg.b_step  = gtk_button_new_with_label("Step");
        dbg.b_reset = gtk_button_new_with_label("Reset");
        g_signal_connect(dbg.b_run,   "clicked", G_CALLBACK(on_dbg_run),   NULL);
        g_signal_connect(dbg.b_stop,  "clicked", G_CALLBACK(on_dbg_stop),  NULL);
        g_signal_connect(dbg.b_step,  "clicked", G_CALLBACK(on_dbg_step),  NULL);
        g_signal_connect(dbg.b_reset, "clicked", G_CALLBACK(on_dbg_reset), NULL);
        gtk_box_pack_start(GTK_BOX(ctl), dbg.state,  FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(ctl), dbg.b_run,  FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(ctl), dbg.b_stop, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(ctl), dbg.b_step, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(ctl), dbg.b_reset,FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), ctl, FALSE, FALSE, 0);

        dbg.regs = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(dbg.regs), 0.0f);
        dbg_set_monospace(dbg.regs);
        gtk_box_pack_start(GTK_BOX(box), dbg.regs, FALSE, FALSE, 0);

        /* Flag toggles: click to flip a condition bit. */
        GtkWidget *frow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        gtk_box_pack_start(GTK_BOX(frow), gtk_label_new("Flags:"), FALSE, FALSE, 2);
        const char *fnames[7] = { "S", "Z", "AC", "P", "C", "V", "K" };
        for (int i = 0; i < 7; i++) {
            dbg.fl[i] = gtk_check_button_new_with_label(fnames[i]);
            g_signal_connect(dbg.fl[i], "toggled", G_CALLBACK(on_dbg_flag),
                             GINT_TO_POINTER(i));
            gtk_box_pack_start(GTK_BOX(frow), dbg.fl[i], FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(box), frow, FALSE, FALSE, 0);

        GtkWidget *lcrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *lc = gtk_label_new("Disassembly (from PC):");
        gtk_label_set_xalign(GTK_LABEL(lc), 0.0f);
        GtkWidget *drange = gtk_button_new_with_label("List range\xE2\x80\xA6");
        g_signal_connect(drange, "clicked", G_CALLBACK(on_dbg_disasm_range), NULL);
        gtk_box_pack_start(GTK_BOX(lcrow), lc,     FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(lcrow), drange, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), lcrow, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), dbg_make_textview(&dbg.code, 200), TRUE, TRUE, 0);

        /* Breakpoint controls: hex entry + Add / Remove / Clear, then a list. */
        GtkWidget *bpc = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *bpl = gtk_label_new("BP (hex PC):");
        dbg.bp_entry = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(dbg.bp_entry), 4);
        gtk_entry_set_width_chars(GTK_ENTRY(dbg.bp_entry), 6);
        GtkWidget *bp_add = gtk_button_new_with_label("Add");
        GtkWidget *bp_rem = gtk_button_new_with_label("Remove");
        GtkWidget *bp_clr = gtk_button_new_with_label("Clear all");
        g_signal_connect(dbg.bp_entry, "activate", G_CALLBACK(on_dbg_bp_add),    NULL);
        g_signal_connect(bp_add,       "clicked",  G_CALLBACK(on_dbg_bp_add),    NULL);
        g_signal_connect(bp_rem,       "clicked",  G_CALLBACK(on_dbg_bp_remove), NULL);
        g_signal_connect(bp_clr,       "clicked",  G_CALLBACK(on_dbg_bp_clear),  NULL);
        gtk_box_pack_start(GTK_BOX(bpc), bpl,          FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bpc), dbg.bp_entry, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bpc), bp_add,       FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bpc), bp_rem,       FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bpc), bp_clr,       FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), bpc, FALSE, FALSE, 0);

        dbg.bp_list = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(dbg.bp_list), 0.0f);
        gtk_label_set_line_wrap(GTK_LABEL(dbg.bp_list), TRUE);
        dbg_set_monospace(dbg.bp_list);
        gtk_box_pack_start(GTK_BOX(box), dbg.bp_list, FALSE, FALSE, 0);

        /* Memory search: hex bytes ("3E FF") or EBCDIC text. */
        GtkWidget *srow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_box_pack_start(GTK_BOX(srow), gtk_label_new("Search:"), FALSE, FALSE, 0);
        dbg.srch = gtk_entry_new(); gtk_entry_set_width_chars(GTK_ENTRY(dbg.srch), 14);
        gtk_box_pack_start(GTK_BOX(srow), dbg.srch, TRUE, TRUE, 0);
        dbg.srch_hex = gtk_radio_button_new_with_label(NULL, "hex");
        dbg.srch_txt = gtk_radio_button_new_with_label_from_widget(
                                  GTK_RADIO_BUTTON(dbg.srch_hex), "text");
        gtk_box_pack_start(GTK_BOX(srow), dbg.srch_hex, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(srow), dbg.srch_txt, FALSE, FALSE, 0);
        GtkWidget *find = gtk_button_new_with_label("Find");
        GtkWidget *next = gtk_button_new_with_label("Next");
        GtkWidget *vhl  = gtk_button_new_with_label("\xE2\x86\x92HL");   /* →HL */
        g_signal_connect(dbg.srch, "activate", G_CALLBACK(on_dbg_find),      NULL);
        g_signal_connect(find,     "clicked",  G_CALLBACK(on_dbg_find),      NULL);
        g_signal_connect(next,     "clicked",  G_CALLBACK(on_dbg_find_next), NULL);
        g_signal_connect(vhl,      "clicked",  G_CALLBACK(on_dbg_view_hl),   NULL);
        gtk_box_pack_start(GTK_BOX(srow), find, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(srow), next, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(srow), vhl,  FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), srow, FALSE, FALSE, 0);

        dbg.srch_res = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(dbg.srch_res), 0.0f);
        gtk_box_pack_start(GTK_BOX(box), dbg.srch_res, FALSE, FALSE, 0);

        /* Memory navigation: jump the dump to any address (\xE2\x86\x92HL resets). */
        GtkWidget *grow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_box_pack_start(GTK_BOX(grow), gtk_label_new("Go to [hex]:"), FALSE, FALSE, 0);
        dbg.mem_goto = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(dbg.mem_goto), 6);
        gtk_entry_set_max_length(GTK_ENTRY(dbg.mem_goto), 4);
        GtkWidget *go = gtk_button_new_with_label("Go");
        g_signal_connect(dbg.mem_goto, "activate", G_CALLBACK(on_dbg_mem_goto), NULL);
        g_signal_connect(go,           "clicked",  G_CALLBACK(on_dbg_mem_goto), NULL);
        gtk_box_pack_start(GTK_BOX(grow), dbg.mem_goto, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(grow), go,           FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), grow, FALSE, FALSE, 0);

        GtkWidget *lm = gtk_label_new("Memory (HL / search hit / Go to \xE2\x80\x94 \xE2\x86\x92HL resets):");
        gtk_label_set_xalign(GTK_LABEL(lm), 0.0f);
        gtk_box_pack_start(GTK_BOX(box), lm, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), dbg_make_textview(&dbg.mem, 130), TRUE, TRUE, 0);

        /* Poke controls: edit memory, write an I/O port, set a register.
         * (Most useful while stopped — a set takes effect immediately.) */
        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(box), sep, FALSE, FALSE, 2);

        /* Memory: [addr] = [val] [Set] */
        GtkWidget *mrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        dbg.mp_addr = gtk_entry_new(); gtk_entry_set_width_chars(GTK_ENTRY(dbg.mp_addr), 6);
        dbg.mp_val  = gtk_entry_new(); gtk_entry_set_width_chars(GTK_ENTRY(dbg.mp_val), 4);
        gtk_entry_set_max_length(GTK_ENTRY(dbg.mp_addr), 4);
        gtk_entry_set_max_length(GTK_ENTRY(dbg.mp_val), 2);
        GtkWidget *mset = gtk_button_new_with_label("Set");
        g_signal_connect(dbg.mp_val, "activate", G_CALLBACK(on_dbg_mem_set), NULL);
        g_signal_connect(mset,       "clicked",  G_CALLBACK(on_dbg_mem_set), NULL);
        gtk_box_pack_start(GTK_BOX(mrow), gtk_label_new("Mem [hex]:"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mrow), dbg.mp_addr, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mrow), gtk_label_new("="), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mrow), dbg.mp_val,  FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mrow), mset,        FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), mrow, FALSE, FALSE, 0);

        /* Port: [port] <= [val] [Out] */
        GtkWidget *prow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        dbg.pk_port = gtk_entry_new(); gtk_entry_set_width_chars(GTK_ENTRY(dbg.pk_port), 4);
        dbg.pk_val  = gtk_entry_new(); gtk_entry_set_width_chars(GTK_ENTRY(dbg.pk_val), 4);
        gtk_entry_set_max_length(GTK_ENTRY(dbg.pk_port), 2);
        gtk_entry_set_max_length(GTK_ENTRY(dbg.pk_val), 2);
        GtkWidget *pout = gtk_button_new_with_label("Out");
        g_signal_connect(dbg.pk_val, "activate", G_CALLBACK(on_dbg_port_out), NULL);
        g_signal_connect(pout,       "clicked",  G_CALLBACK(on_dbg_port_out), NULL);
        gtk_box_pack_start(GTK_BOX(prow), gtk_label_new("Port [hex]:"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(prow), dbg.pk_port, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(prow), gtk_label_new("<="), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(prow), dbg.pk_val,  FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(prow), pout,        FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), prow, FALSE, FALSE, 0);

        /* Register: [combo] = [val] [Set] */
        GtkWidget *rrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        dbg.rg_combo = gtk_combo_box_text_new();
        const char *rnames[] = { "A","B","C","D","E","H","L","PC","SP" };
        for (int i = 0; i < 9; i++)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dbg.rg_combo), rnames[i]);
        gtk_combo_box_set_active(GTK_COMBO_BOX(dbg.rg_combo), 0);
        dbg.rg_val = gtk_entry_new(); gtk_entry_set_width_chars(GTK_ENTRY(dbg.rg_val), 6);
        gtk_entry_set_max_length(GTK_ENTRY(dbg.rg_val), 4);
        GtkWidget *rset = gtk_button_new_with_label("Set");
        g_signal_connect(dbg.rg_val, "activate", G_CALLBACK(on_dbg_reg_set), NULL);
        g_signal_connect(rset,       "clicked",  G_CALLBACK(on_dbg_reg_set), NULL);
        gtk_box_pack_start(GTK_BOX(rrow), gtk_label_new("Reg:"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(rrow), dbg.rg_combo, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(rrow), gtk_label_new("= [hex]"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(rrow), dbg.rg_val,   FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(rrow), rset,         FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), rrow, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(dbg.win), box);
    }
    gtk_widget_show_all(dbg.win);
    gtk_window_present(GTK_WINDOW(dbg.win));
    dbg_refresh(NULL);
    if (!dbg.timer) dbg.timer = g_timeout_add(200, dbg_refresh, NULL);
}

/* ---- Printer window: renders the paper the printer emulation builds --------
 * The USART feeds bytes to printer.c, which lays them out as pages of cells.
 * Here we draw those pages (per-model typeface, bold/underline) and can send
 * them to a real printer via the native GTK/CUPS print dialog.  A "Raw bytes"
 * toggle shows the underlying stream for interpreter refinement. */
#define PRT_CW   8.0     /* cell width  (px)                 */
#define PRT_CH   15.0    /* cell height (px)                 */
#define PRT_MRG  24.0    /* sheet inner margin               */
#define PRT_OUT  16.0    /* outer padding around the sheets  */
#define PRT_GAP  18.0    /* gap between sheets               */

static struct {
    GtkWidget *win, *area, *area_sc, *raw_view, *raw_sc, *model_combo, *raw_toggle;
    guint      timer;
    unsigned long last_serial;
} prt;

static int prt_doc_cols(void)
{
    int c = printer_used_cols();
    if (c < 40)       c = 40;
    if (c > PRT_COLS) c = PRT_COLS;
    return c;
}

/* Paint one page's cells at the current origin (0,0 = top-left of the grid). */
static void prt_paint_cells(cairo_t *cr, const PrtPage *pg, int cols, PrinterModel m)
{
    if (!pg) return;
    const char *family = printer_is_daisy(m) ? "serif" : "monospace";
    cairo_set_source_rgb(cr, 0.06, 0.06, 0.06);
    for (int r = 0; r < PRT_ROWS; r++)
        for (int c = 0; c < cols; c++) {
            const PrtCell *cell = &pg->cell[r][c];
            double cx = c * PRT_CW, cy = r * PRT_CH;
            if (cell->ul) { cairo_rectangle(cr, cx, cy + PRT_CH * 0.90, PRT_CW, 1.0); cairo_fill(cr); }
            if (cell->ch && cell->ch != ' ') {
                cairo_select_font_face(cr, family, CAIRO_FONT_SLANT_NORMAL,
                    cell->bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, PRT_CH * 0.80);
                char s[2] = { (char)cell->ch, 0 };
                cairo_move_to(cr, cx + PRT_CW * 0.08, cy + PRT_CH * 0.76);
                cairo_show_text(cr, s);
            }
        }
}

static gboolean prt_draw(GtkWidget *w, cairo_t *cr, gpointer d)
{
    (void)w; (void)d;
    int cols = prt_doc_cols(), count = printer_page_count();
    PrinterModel m = printer_model();
    double sw = 2 * PRT_MRG + cols * PRT_CW, sh = 2 * PRT_MRG + PRT_ROWS * PRT_CH;
    cairo_set_source_rgb(cr, 0.50, 0.50, 0.52); cairo_paint(cr);     /* platen */
    double y = PRT_OUT;
    for (int p = 0; p < count; p++) {
        double x = PRT_OUT;
        cairo_set_source_rgb(cr, 1, 1, 1); cairo_rectangle(cr, x, y, sw, sh); cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.68, 0.68, 0.68); cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, x + 0.5, y + 0.5, sw - 1, sh - 1); cairo_stroke(cr);
        cairo_save(cr); cairo_translate(cr, x + PRT_MRG, y + PRT_MRG);
        prt_paint_cells(cr, printer_page(p), cols, m);
        cairo_restore(cr);
        y += sh + PRT_GAP;
    }
    return FALSE;
}

static void prt_update_size(void)
{
    int cols = prt_doc_cols(), count = printer_page_count();
    double sw = 2 * PRT_MRG + cols * PRT_CW, sh = 2 * PRT_MRG + PRT_ROWS * PRT_CH;
    gtk_widget_set_size_request(prt.area, (int)(sw + 2 * PRT_OUT),
                                          (int)(count * (sh + PRT_GAP) + 2 * PRT_OUT));
}

static void prt_update_raw(void)
{
    static char raw[65536];
    int n = printer_raw(raw, (int)sizeof raw);
    GString *g = g_string_new(NULL);
    for (int i = 0; i < n; i += 16) {
        g_string_append_printf(g, "%04X  ", i);
        for (int j = 0; j < 16; j++)
            if (i + j < n) g_string_append_printf(g, "%02X ", (unsigned char)raw[i + j]);
            else           g_string_append(g, "   ");
        g_string_append(g, "  ");
        for (int j = 0; j < 16 && i + j < n; j++) {
            unsigned char b = (unsigned char)raw[i + j];
            g_string_append_c(g, (b >= 0x20 && b < 0x7F) ? (char)b : '.');
        }
        g_string_append_c(g, '\n');
    }
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(prt.raw_view)), g->str, -1);
    g_string_free(g, TRUE);
}

static gboolean prt_refresh(gpointer data)
{
    (void)data;
    unsigned long s = printer_serial();
    if (s == prt.last_serial) return G_SOURCE_CONTINUE;
    prt.last_serial = s;
    prt_update_size();
    gtk_widget_queue_draw(prt.area);
    if (prt.raw_toggle && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(prt.raw_toggle)))
        prt_update_raw();
    return G_SOURCE_CONTINUE;
}

static void prt_on_model(GtkComboBox *cb, gpointer d)
{
    (void)d;
    int i = gtk_combo_box_get_active(cb);
    if (i < 0) return;
    printer_set_model((PrinterModel)i);
    g_config.printer_model = i;              /* saved on exit */
    gtk_widget_queue_draw(prt.area);
}

static void prt_on_clear(GtkButton *b, gpointer d) { (void)b; (void)d; printer_clear(); prt_refresh(NULL); }

static void prt_on_raw(GtkToggleButton *t, gpointer d)
{
    (void)d;
    gboolean on = gtk_toggle_button_get_active(t);
    gtk_widget_set_visible(prt.raw_sc,  on);
    gtk_widget_set_visible(prt.area_sc, !on);
    if (on) prt_update_raw();
}

/* Native print: render each page onto the print context, scaled to the paper. */
static void prt_draw_page_cb(GtkPrintOperation *op, GtkPrintContext *ctx, gint page_nr, gpointer d)
{
    (void)op; (void)d;
    const PrtPage *pg = printer_page(page_nr);
    if (!pg) return;
    cairo_t *cr = gtk_print_context_get_cairo_context(ctx);
    double pw = gtk_print_context_get_width(ctx), ph = gtk_print_context_get_height(ctx);
    int cols = prt_doc_cols();
    double gw = cols * PRT_CW, gh = PRT_ROWS * PRT_CH;
    double sc = fmin(pw / gw, ph / gh);
    cairo_save(cr);
    cairo_translate(cr, (pw - gw * sc) / 2.0, 0);
    cairo_scale(cr, sc, sc);
    prt_paint_cells(cr, pg, cols, printer_model());
    cairo_restore(cr);
}

static void prt_on_print(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    GtkPrintOperation *op = gtk_print_operation_new();
    gtk_print_operation_set_n_pages(op, printer_page_count());
    gtk_print_operation_set_job_name(op, "System/23 printout");
    g_signal_connect(op, "draw-page", G_CALLBACK(prt_draw_page_cb), NULL);
    gtk_print_operation_run(op, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                            prt.win ? GTK_WINDOW(prt.win) : NULL, NULL);
    g_object_unref(op);
}

static gboolean prt_on_delete(GtkWidget *w, GdkEvent *e, gpointer d)
{
    (void)w; (void)e; (void)d;
    if (prt.timer) { g_source_remove(prt.timer); prt.timer = 0; }
    gtk_widget_hide(prt.win);
    return TRUE;
}

void printer_window_set_visible(GtkWindow *parent, gboolean visible)
{
    if (visible) {
        if (!prt.win) {
            prt.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
            gtk_window_set_title(GTK_WINDOW(prt.win), "Printer");
            gtk_window_set_default_size(GTK_WINDOW(prt.win), 720, 580);
            if (parent) gtk_window_set_transient_for(GTK_WINDOW(prt.win), parent);
            g_signal_connect(prt.win, "delete-event", G_CALLBACK(prt_on_delete), NULL);

            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            gtk_container_set_border_width(GTK_CONTAINER(box), 8);

            /* Toolbar: model selector, Print…, Clear, Raw-bytes toggle. */
            GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            gtk_box_pack_start(GTK_BOX(bar), gtk_label_new("Printer:"), FALSE, FALSE, 0);
            prt.model_combo = gtk_combo_box_text_new();
            for (int i = 0; i < PRT_MODEL_COUNT; i++)
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(prt.model_combo),
                                               printer_model_name((PrinterModel)i));
            gtk_combo_box_set_active(GTK_COMBO_BOX(prt.model_combo), (int)printer_model());
            g_signal_connect(prt.model_combo, "changed", G_CALLBACK(prt_on_model), NULL);
            gtk_box_pack_start(GTK_BOX(bar), prt.model_combo, FALSE, FALSE, 0);

            GtkWidget *print = gtk_button_new_with_label("Print\xE2\x80\xA6");
            g_signal_connect(print, "clicked", G_CALLBACK(prt_on_print), NULL);
            gtk_box_pack_start(GTK_BOX(bar), print, FALSE, FALSE, 0);
            GtkWidget *clear = gtk_button_new_with_label("Clear");
            g_signal_connect(clear, "clicked", G_CALLBACK(prt_on_clear), NULL);
            gtk_box_pack_start(GTK_BOX(bar), clear, FALSE, FALSE, 0);

            prt.raw_toggle = gtk_toggle_button_new_with_label("Raw bytes");
            g_signal_connect(prt.raw_toggle, "toggled", G_CALLBACK(prt_on_raw), NULL);
            gtk_box_pack_end(GTK_BOX(bar), prt.raw_toggle, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);

            /* Page render. */
            prt.area = gtk_drawing_area_new();
            g_signal_connect(prt.area, "draw", G_CALLBACK(prt_draw), NULL);
            prt.area_sc = gtk_scrolled_window_new(NULL, NULL);
            gtk_container_add(GTK_CONTAINER(prt.area_sc), prt.area);
            gtk_box_pack_start(GTK_BOX(box), prt.area_sc, TRUE, TRUE, 0);

            /* Raw hex view (hidden until toggled). */
            prt.raw_view = gtk_text_view_new();
            gtk_text_view_set_editable(GTK_TEXT_VIEW(prt.raw_view), FALSE);
            gtk_text_view_set_monospace(GTK_TEXT_VIEW(prt.raw_view), TRUE);
            prt.raw_sc = gtk_scrolled_window_new(NULL, NULL);
            gtk_container_add(GTK_CONTAINER(prt.raw_sc), prt.raw_view);
            gtk_box_pack_start(GTK_BOX(box), prt.raw_sc, TRUE, TRUE, 0);

            gtk_container_add(GTK_CONTAINER(prt.win), box);
            prt.last_serial = ~printer_serial();
        }
        gtk_widget_show_all(prt.win);
        {   /* honor the current Raw-bytes toggle state */
            gboolean raw_on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(prt.raw_toggle));
            gtk_widget_set_visible(prt.raw_sc,  raw_on);
            gtk_widget_set_visible(prt.area_sc, !raw_on);
        }
        gtk_window_present(GTK_WINDOW(prt.win));
        prt_update_size();
        prt.last_serial = ~printer_serial();     /* force a repaint */
        prt_refresh(NULL);
        if (!prt.timer) prt.timer = g_timeout_add(300, prt_refresh, NULL);
    } else if (prt.win) {
        if (prt.timer) { g_source_remove(prt.timer); prt.timer = 0; }
        gtk_widget_hide(prt.win);
    }
}
