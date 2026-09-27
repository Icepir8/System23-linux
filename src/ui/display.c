/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  ui/display.c — primary CRT window (port of Display.cs)
 *
 *  Toolbar (built in code, like the original) + a GtkDrawingArea that renders
 *  the 80x24 text screen from RAM through the character ROM, refreshed ~30x/s.
 *  Before power-on (the CRTC is not yet driving the screen) the CRT area shows
 *  the welcome splash; set SYSTEM23_FONTTEST=1 to preview the character ROM.
 * ===========================================================================*/
#include "ui/display.h"
#include "ui/windows.h"

#include "system23.h"
#include "machine.h"
#include "memory.h"
#include "assets.h"
#include "ioports.h"
#include "i8275.h"
#include "i8259.h"

#include <math.h>
#include <string.h>

/* ---- module state (single Display, like the WinForms singleton) ---------- */
static GtkWidget *window;
static GtkWidget *area;                 /* the CRT GtkDrawingArea            */
static GtkWidget *power_btn;
static GtkWidget *rom_combo;
static GtkWidget *lang_combo;
static GtkWidget *printer_toggle;

static cairo_surface_t *font_normal;    /* char3.bmp        (idx by mode&0x11)*/
static cairo_surface_t *font_hl;        /* char3hl.bmp                        */
static cairo_surface_t *font_inv;       /* char3inv.bmp                       */
static cairo_surface_t *font_invhl;     /* char3invhl.bmp                     */
static gboolean         fonts_ok;

/* Optional welcome-screen logo, loaded from Graphics/ (e.g. the Large Scale
 * Systems Museum logo).  Absent is fine — the splash just omits it. */
static cairo_surface_t *logo_surf;
static int              logo_w, logo_h;
static gboolean         logo_tried;
static cairo_surface_t *crt_surf;       /* native-res (1440x768) frame buffer */

static int      blink;                  /* blink phase counter                */
static gboolean blink_on = TRUE;
static u64      last_sig;
static gboolean have_sig;
static ExecState last_power_state = (ExecState)-1;
static gboolean suppress_combo_events;

/* =====================================================================
 *  Character-ROM surfaces
 * ===================================================================*/
static cairo_surface_t *pixbuf_to_surface(GdkPixbuf *pb)
{
    if (!pb) return NULL;
    cairo_surface_t *s = gdk_cairo_surface_create_from_pixbuf(pb, 1, NULL);
    g_object_unref(pb);
    return s;
}

static void load_fonts(void)
{
    GdkPixbuf *n, *h, *i, *ih;
    fonts_ok = assets_load_char_roms(&n, &h, &i, &ih);
    font_normal = pixbuf_to_surface(n);
    font_hl     = pixbuf_to_surface(h);
    font_inv    = pixbuf_to_surface(i);
    font_invhl  = pixbuf_to_surface(ih);
}

/* Load the welcome-screen logo once (first splash paint).  Tries a few names in
 * Graphics/ so dropping in an image "just works"; a transparent PNG looks best
 * over the black CRT.  Missing => logo_surf stays NULL and the splash omits it. */
static void load_logo_once(void)
{
    if (logo_tried) return;
    logo_tried = TRUE;
    const char *names[] = { "lssm_logo.png", "lssm.png", "museum_logo.png", "logo.png" };
    GdkPixbuf *pb = NULL;
    for (guint k = 0; k < G_N_ELEMENTS(names) && !pb; k++)
        pb = assets_load_graphic(names[k]);
    if (pb) {
        logo_w = gdk_pixbuf_get_width(pb);
        logo_h = gdk_pixbuf_get_height(pb);
        logo_surf = pixbuf_to_surface(pb);   /* consumes (unrefs) pb */
    }
}

/* Blit an 8x14 source cell of `surf` at (sx,sy) into the destination rect,
 * scaled, with nearest-neighbour sampling for a crisp phosphor look. */
static void blit_cell(cairo_t *cr, cairo_surface_t *surf,
                      int sx, int sy, double dx, double dy, double dw, double dh)
{
    if (!surf) return;
    cairo_save(cr);
    cairo_translate(cr, dx, dy);
    cairo_scale(cr, dw / (double)GLYPH_SRC_W, dh / (double)GLYPH_SRC_H);
    cairo_set_source_surface(cr, surf, -sx, -sy);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_rectangle(cr, 0, 0, GLYPH_SRC_W, GLYPH_SRC_H);
    cairo_fill(cr);
    cairo_restore(cr);
}

/* =====================================================================
 *  Frame change-detection (FNV-1a over the visible cells + a little state)
 * ===================================================================*/
static u64 frame_signature(void)
{
    u64 hash = 1469598103934665603ULL;
    const u64 prime = 1099511628211ULL;
    int pos = CRT_RAM_BASE;
    for (int i = 0; i < CRT_ROWS * CRT_COLS; i++)
        hash = (hash ^ mem_ram[pos++]) * prime;
    hash = (hash ^ (u8)io_char_page_read) * prime;
    hash = (hash ^ (u8)(blink_on ? 1u : 0u)) * prime;
    hash = (hash ^ (u8)crtc_cursor_col) * prime;
    hash = (hash ^ (u8)crtc_cursor_row) * prime;
    hash = (hash ^ (u8)crtc_ul_placement) * prime;
    return hash;
}

/* =====================================================================
 *  Rendering
 * ===================================================================*/
static void set_green(cairo_t *cr, gboolean bright)
{
    if (bright) cairo_set_source_rgb(cr, 0x3F/255.0, 1.0, 0x3F/255.0);
    else        cairo_set_source_rgb(cr, PHOSPHOR_R/255.0, PHOSPHOR_G/255.0, PHOSPHOR_B/255.0);
}

/* Faithful-enough port of the Display.cs per-cell renderer.  Draws into CRT
 * pixel coordinates (0..CRT_PIXELS_W, 0..CRT_PIXELS_H); the caller has already
 * applied the scale-to-fit transform.
 *
 * TODO(display): a few Display.cs subtleties still need a visual pass once the
 * core drives real screen content — the two-pass glyph blit, the 19px graphics
 * character (0xC0-0xFF) width, and exact inverted-cell compositing. */
static void render_screen(cairo_t *cr)
{
    int mode = 0;
    int pos = CRT_RAM_BASE;

    /* Display.cs clears its frame buffer (to transparent) at the start of every
     * frame and then leaves cells that draw nothing showing that cleared
     * background.  Field-attribute cells ((byte & 0xC0) == 0x80) fall into that
     * category: they set the rendering mode for the *following* characters but
     * are themselves shown as blanks.  We clear the whole CRT to black once so
     * those attribute cells (and blanked/off-blink cells) render as blanks. */
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_rectangle(cr, 0, 0, CRT_PIXELS_W, CRT_PIXELS_H);
    cairo_fill(cr);

    for (int row = 0; row < CRT_ROWS; row++) {
        for (int col = 0; col < CRT_COLS; col++) {
            int  cur     = mem_ram[pos++];
            u8   special = (u8)cur;
            double dx = col * CELL_W;
            double dy = row * CELL_H;
            gboolean is_graphics = FALSE;

            if ((cur & 0xC0) == 0x40)
                cur += ((int)io_char_page_read - 1) << 6;

            int srcx = (cur & 7) * 8;
            int srcy = (cur & 0xFF8) << 1;

            if ((special & 0xC0) == 0x80) {         /* field attribute cell   */
                mode = cur;                          /* persists until next one */
            } else if ((special & 0xC0) == 0xC0) {   /* graphics cell          */
                int t = (cur & 0x3F) >> 2;
                t |= 0x200;
                srcx = (t & 7) * 8;
                srcy = (t & 0xFF8) << 1;
                is_graphics = TRUE;
            }

            /* Non-display field (attribute bit 0x08): skip -> stays blank. */
            if ((mode & 0x08) != 0)
                continue;

            /* Blinking field in its off phase: skip -> stays blank (black). */
            if ((mode & 0x02) != 0 && !blink_on)
                continue;

            /* Only genuine character / graphics cells paint anything; a field-
             * attribute cell ((cur & 0x80) set, not graphics) draws nothing, so
             * its background, glyph and underline are all suppressed and it
             * shows through as a blank -- matching Display.cs exactly. */
            gboolean draw_glyph = ((cur & 0x80) == 0) || ((cur & 0xC0) == 0xC0);

            cairo_surface_t *font = font_normal;
            gboolean bg_green = FALSE, bg_bright = FALSE;   /* cell background   */
            gboolean fg_green = TRUE,  fg_bright = FALSE;   /* underline/cursor  */

            if (draw_glyph) {
                switch (mode & 0x11) {
                    default:
                    case 0x00: font = font_normal;                         break;
                    case 0x01: font = font_hl;    fg_bright = TRUE;        break;
                    case 0x10: font = font_inv;   bg_green = TRUE; fg_green = FALSE; break;
                    case 0x11: font = font_invhl; bg_green = TRUE; bg_bright = TRUE; fg_green = FALSE; break;
                }

                /* Cell background. */
                if (bg_green) set_green(cr, bg_bright);
                else          cairo_set_source_rgb(cr, 0, 0, 0);
                cairo_rectangle(cr, dx, dy, CELL_W, CELL_H);
                cairo_fill(cr);

                /* Glyph.  Graphics cells are drawn one pixel wider (19px) to
                 * match Display.cs and close the seam between block-graphics. */
                blit_cell(cr, font, srcx, srcy, dx, dy,
                          is_graphics ? 19.0 : (double)GLYPH_DST_W, GLYPH_DST_H);

                /* Underline bar (mode bit 0x20), in the foreground colour --
                 * green for normal/highlight fields, black for inverted ones. */
                if ((mode & 0x20) != 0) {
                    if (fg_green) set_green(cr, fg_bright);
                    else          cairo_set_source_rgb(cr, 0, 0, 0);
                    cairo_rectangle(cr, dx, dy + (crtc_ul_placement + 1) * 2, CELL_W, 2);
                    cairo_fill(cr);
                }
            }

            /* Cursor. */
            if (blink_on && crtc_cursor_col == col && crtc_cursor_row == row) {
                if (fg_green) set_green(cr, fg_bright);
                else          cairo_set_source_rgb(cr, 0, 0, 0);
                cairo_rectangle(cr, dx, dy + 26, GLYPH_DST_W, 2);
                cairo_fill(cr);
            }
        }
    }
}

/* Preview the whole character ROM (SYSTEM23_FONTTEST=1). */
static void render_fonttest(cairo_t *cr)
{
    if (!font_normal) return;
    int fw = cairo_image_surface_get_width(font_normal);
    int fh = cairo_image_surface_get_height(font_normal);
    double s = fmin(CRT_PIXELS_W / (double)fw, CRT_PIXELS_H / (double)fh);
    cairo_save(cr);
    cairo_translate(cr, (CRT_PIXELS_W - fw * s) / 2.0, 0);
    cairo_scale(cr, s, s);
    cairo_set_source_surface(cr, font_normal, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
}

/* A rounded-rectangle path (for the logo panel). */
static void splash_rrect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -G_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0,        G_PI_2);
    cairo_arc(cr, x + r,     y + h - r, r, G_PI_2,   G_PI);
    cairo_arc(cr, x + r,     y + r,     r, G_PI,     3 * G_PI_2);
    cairo_close_path(cr);
}

/* Welcome / pre-power-on splash (shown until the CRTC drives real content). */
static void render_splash(cairo_t *cr)
{
    load_logo_once();

    const char *lines[] = {
        APP_NAME,                                   /* big heading            */
        "IBM Datamaster — Linux/GTK3 port",
        "Press \xE2\x96\xB6 Power On to cold-boot.",
        "Developed by Owen V. Michael, Jr.",
        "Thanking the Large Scale Systems Museum in New Kensington, PA.",
    };

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

    /* Measure the heading so the logo caption can be matched to it. */
    cairo_set_font_size(cr, 56);
    cairo_text_extents_t hext;
    cairo_text_extents(cr, lines[0], &hext);
    double heading_h   = hext.height;
    double heading_asc = -hext.y_bearing;            /* baseline -> ink top    */

    /* --- Logo panel (top band) --------------------------------------------
     * The art is black text on transparency, so it sits on a white panel.
     * Scale it so the "large scale systems museum" caption (the top ~52px of
     * the 600x230 art) matches the System/23 heading height, and pad the panel
     * by ~one character height on all four sides. */
    const double pad = CELL_H;                       /* one char height (32)   */
    double panel_bottom = CELL_H;                    /* fallback if no logo     */
    if (logo_surf && logo_w > 0 && logo_h > 0) {
        /* Caption band in the source art, calibrated so the rendered caption
         * ("Large Scale Systems Museum") matches the System/23 heading's ink
         * height. */
        const double LOGO_CAPTION_PX = 38.0;
        double sc = heading_h / LOGO_CAPTION_PX;
        double dw = logo_w * sc, dh = logo_h * sc;
        double pw = dw + 2 * pad, ph = dh + 2 * pad;
        double px = (CRT_PIXELS_W - pw) / 2.0;
        double py = 22.0;                            /* top margin              */

        cairo_save(cr);
        splash_rrect(cr, px, py, pw, ph, 18.0);
        cairo_set_source_rgb(cr, 1, 1, 1);           /* white panel            */
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0.72, 0.72, 0.72);  /* subtle border          */
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
        cairo_restore(cr);

        cairo_save(cr);
        cairo_translate(cr, px + pad, py + pad);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, logo_surf, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);

        panel_bottom = py + ph;
    }

    /* --- Text block (below the panel) -------------------------------------*/
    set_green(cr, FALSE);
    double y = panel_bottom + 18.0 + heading_asc;    /* gap panel -> heading   */
    for (guint i = 0; i < G_N_ELEMENTS(lines); i++) {
        cairo_set_font_size(cr, i == 0 ? 56 : 26);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, lines[i], &ext);
        cairo_move_to(cr, (CRT_PIXELS_W - ext.width) / 2.0 - ext.x_bearing, y);
        cairo_show_text(cr, lines[i]);
        y += (i == 0 ? 74 : 40);
    }

    /* Status line: ROM set + where ROMs are being read from. */
    char status[1200];
    g_snprintf(status, sizeof status, "ROM: %s     Roms: %s",
               rom_version_display_name(machine_rom_set()), machine_roms_dir());
    cairo_set_font_size(cr, 20);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, status, &ext);
    cairo_move_to(cr, (CRT_PIXELS_W - ext.width) / 2.0 - ext.x_bearing, CRT_PIXELS_H - 60);
    cairo_show_text(cr, status);
}

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
    (void)data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);

    /* Black everywhere. */
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);

    /* Scale the fixed CRT raster to fit the widget, preserving aspect. */
    double s  = fmin(alloc.width  / (double)CRT_PIXELS_W,
                     alloc.height / (double)CRT_PIXELS_H);
    if (s <= 0) return FALSE;
    double ox = (alloc.width  - CRT_PIXELS_W * s) / 2.0;
    double oy = (alloc.height - CRT_PIXELS_H * s) / 2.0;
    cairo_translate(cr, ox, oy);
    cairo_scale(cr, s, s);

    if (g_getenv("SYSTEM23_FONTTEST")) {
        render_fonttest(cr);
        return FALSE;
    }

    /* The CRTC gates display on (status & 0x2C); until the core runs this is 0
     * and we show the splash instead of a blank screen. */
    gboolean signal_live = (crtc_status & 0x2C) != 0;
    if (signal_live && fonts_ok) {
        /* Render the whole 80x24 grid into a native-resolution offscreen buffer
         * first, then scale that single image to the widget.  Drawing the cells
         * directly into the fractionally-scaled context leaves 1px anti-aliasing
         * seams between adjacent cell fills (visible between green inverted
         * cells); compositing one native-res image has no per-cell boundaries.
         * This mirrors the C# path, which renders to a bitmap and scales it. */
        if (!crt_surf)
            crt_surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                  CRT_PIXELS_W, CRT_PIXELS_H);
        cairo_t *cc = cairo_create(crt_surf);
        render_screen(cc);
        cairo_destroy(cc);

        cairo_set_source_surface(cr, crt_surf, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
    } else {
        render_splash(cr);
    }
    return FALSE;
}

/* =====================================================================
 *  Power button label tracking
 * ===================================================================*/
static void update_power_button(void)
{
    ExecState s = machine_state();
    if (s == last_power_state) return;
    last_power_state = s;
    gtk_button_set_label(GTK_BUTTON(power_btn),
                         s == EXEC_STOPPED ? "\xE2\x96\xB6 Power On" : "\xE2\x8F\xBB Power");
    gtk_widget_set_tooltip_text(power_btn,
        s == EXEC_STOPPED ? "Power on (cold boot)"
                          : "Running — click for Reset or Power Off");
}

/* =====================================================================
 *  Refresh timer (~30 Hz)
 * ===================================================================*/
static gboolean on_refresh_tick(gpointer data)
{
    (void)data;
    update_power_button();

    if (io_diagnostic_port == 0xFF)
        io_diagnostic_port = 0;

    if (++blink >= 5) { blink = 0; blink_on = !blink_on; }

    gboolean signal_live = (crtc_status & 0x2C) != 0;
    if (signal_live) {
        u64 sig = frame_signature();
        if (!have_sig || sig != last_sig) {
            last_sig = sig; have_sig = TRUE;
            gtk_widget_queue_draw(area);
        }
    } else {
        /* Splash: redraw occasionally so the status line stays fresh. */
        if (have_sig || blink == 0) { have_sig = FALSE; gtk_widget_queue_draw(area); }
    }
    return G_SOURCE_CONTINUE;
}

/* =====================================================================
 *  Toolbar callbacks
 * ===================================================================*/
static void on_reset(GtkMenuItem *m, gpointer d)     { (void)m; (void)d; machine_reset(); }
static void on_power_off(GtkMenuItem *m, gpointer d)  { (void)m; (void)d; machine_power_off(); }

static void on_power_clicked(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    if (machine_state() == EXEC_STOPPED) {
        machine_start();                 /* power on -> cold boot */
    } else {
        /* Running: offer Reset / Power Off. */
        GtkWidget *menu = gtk_menu_new();
        GtkWidget *mi_reset = gtk_menu_item_new_with_label("Reset");
        GtkWidget *mi_off   = gtk_menu_item_new_with_label("Power Off");
        g_signal_connect(mi_reset, "activate", G_CALLBACK(on_reset), NULL);
        g_signal_connect(mi_off,   "activate", G_CALLBACK(on_power_off), NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_reset);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_off);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_widget(GTK_MENU(menu), power_btn,
                                 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
    }
    update_power_button();
}

static void on_rom_changed(GtkComboBox *c, gpointer d)
{
    (void)d;
    if (suppress_combo_events) return;
    int i = gtk_combo_box_get_active(c);
    if (i >= 0) machine_load_rom_set((RomVersion)i);
    update_power_button();
}

static void on_lang_changed(GtkComboBox *c, gpointer d)
{
    (void)d;
    if (suppress_combo_events) return;
    int i = gtk_combo_box_get_active(c);
    if (i >= 0) io_country = (Countries)i;
}

static void on_floppy(GtkButton *b, gpointer d)  { (void)b; (void)d; floppy_dialog_run(GTK_WINDOW(window)); }
static void on_debug(GtkButton *b, gpointer d)   { (void)b; (void)d; debug_window_show(GTK_WINDOW(window)); }
static void on_about(GtkButton *b, gpointer d)   { (void)b; (void)d; about_dialog_run(GTK_WINDOW(window)); }

static void on_printer_toggled(GtkToggleButton *t, gpointer d)
{
    (void)d;
    printer_window_set_visible(GTK_WINDOW(window), gtk_toggle_button_get_active(t));
}

/* =====================================================================
 *  Keyboard — full System/23 key matrix (port of Display.cs)
 *
 *  The original indexed scancodemap[]/charreleasemap[] by Windows virtual-key
 *  codes.  Those tables are reproduced verbatim here (so the System/23 scan
 *  codes are exact), and gdk_keyval_to_vk() maps each GTK GDK keyval to the
 *  equivalent Windows VK so the same tables drive both.  A key press sends the
 *  make code; a key release sends the break code where the matrix defines one
 *  (mostly Shift/Alt and the numeric-pad +/-), each raising IRQ 0.
 * ===================================================================*/

/* System/23 make codes, indexed by Windows virtual-key code (0x00-0xFF). */
static const u8 s23_scancode[256] = {
    /*00*/ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x42,0x5F,0x00,0x00,0x00,0x52,0x00,0x00,
    /*10*/ 0x28,0x00,0x01,0x00,0x0B,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /*20*/ 0x70,0x00,0x00,0x00,0x00,0x71,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /*30*/ 0x45,0x4E,0x4D,0x4C,0x4B,0x4A,0x49,0x48,0x47,0x46,0x00,0x00,0x00,0x00,0x00,0x00,
    /*40*/ 0x00,0x6E,0x7A,0x7C,0x6C,0x5C,0x6B,0x6A,0x69,0x57,0x68,0x67,0x66,0x78,0x79,0x56,
    /*50*/ 0x55,0x5E,0x5B,0x6D,0x5A,0x58,0x7B,0x5D,0x7D,0x59,0x7E,0x00,0x00,0x00,0x00,0x00,
    /*60*/ 0x3F,0x3E,0x3D,0x3C,0x3B,0x3A,0x02,0x38,0x37,0x36,0x35,0x34,0x31,0x2A,0x31,0x30,
    /*70*/ 0x03,0x10,0x13,0x12,0x11,0x02,0x17,0x18,0x19,0x1A,0x00,0x00,0x00,0x00,0x00,0x00,
    /*80*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /*90*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /*A0*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /*B0*/ 0,0,0,0,0,0,0,0,0,0,0x65,0x43,0x77,0x44,0x76,0x75,
    /*C0*/ 0x71,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /*D0*/ 0,0,0,0,0,0,0,0,0,0,0,0,0x53,0x00,0x64,0x00,
    /*E0*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /*F0*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

/* System/23 break codes, indexed by VK; -1 = no release event for that key. */
static const int s23_release[256] = {
    /*00*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*10*/ 0x08,-1,0x21,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*20*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*30*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*40*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*50*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*60*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0x32,-1,0x31,-1,-1,
    /*70*/ -1,0x00,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*80*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*90*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*A0*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*B0*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*C0*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*D0*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*E0*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /*F0*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

/* Map a GTK GDK keyval to the Windows virtual-key code the tables expect, so a
 * physical key produces the same scan code regardless of Shift.  Returns -1 for
 * keys the System/23 matrix does not carry. */
static int gdk_keyval_to_vk(guint kv)
{
    if (kv >= GDK_KEY_a && kv <= GDK_KEY_z) return (int)(kv - GDK_KEY_a) + 0x41;
    if (kv >= GDK_KEY_A && kv <= GDK_KEY_Z) return (int)(kv - GDK_KEY_A) + 0x41;
    if (kv >= GDK_KEY_0 && kv <= GDK_KEY_9) return (int)(kv - GDK_KEY_0) + 0x30;
    if (kv >= GDK_KEY_F1 && kv <= GDK_KEY_F12) return (int)(kv - GDK_KEY_F1) + 0x70;
    if (kv >= GDK_KEY_KP_0 && kv <= GDK_KEY_KP_9) return (int)(kv - GDK_KEY_KP_0) + 0x60;
    switch (kv) {
        case GDK_KEY_space:                            return 0x20;
        case GDK_KEY_Return: case GDK_KEY_KP_Enter:    return 0x0D;
        case GDK_KEY_BackSpace:                        return 0x08;
        case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab:   return 0x09;
        case GDK_KEY_Escape:                           return 0x1B;
        case GDK_KEY_Shift_L: case GDK_KEY_Shift_R:    return 0x10;
        case GDK_KEY_Control_L: case GDK_KEY_Control_R:return 0x11;
        case GDK_KEY_Alt_L: case GDK_KEY_Alt_R:        return 0x12;
        case GDK_KEY_Caps_Lock:                        return 0x14;
        case GDK_KEY_Left:                             return 0x25;
        case GDK_KEY_Up:                               return 0x26;
        case GDK_KEY_Right:                            return 0x27;
        case GDK_KEY_Down:                             return 0x28;
        /* Shift maps the physical key back to its base VK. */
        case GDK_KEY_semicolon: case GDK_KEY_colon:        return 0xBA;
        case GDK_KEY_equal:     case GDK_KEY_plus:         return 0xBB;
        case GDK_KEY_comma:     case GDK_KEY_less:         return 0xBC;
        case GDK_KEY_minus:     case GDK_KEY_underscore:   return 0xBD;
        case GDK_KEY_period:    case GDK_KEY_greater:      return 0xBE;
        case GDK_KEY_slash:     case GDK_KEY_question:     return 0xBF;
        case GDK_KEY_grave:     case GDK_KEY_asciitilde:   return 0xC0;
        case GDK_KEY_backslash: case GDK_KEY_bar:          return 0xDC;
        case GDK_KEY_apostrophe:case GDK_KEY_quotedbl:     return 0xDE;
        /* Shifted number row -> the base digit key. */
        case GDK_KEY_exclam:      return 0x31;   /* 1 */
        case GDK_KEY_at:          return 0x32;   /* 2 */
        case GDK_KEY_numbersign:  return 0x33;   /* 3 */
        case GDK_KEY_dollar:      return 0x34;   /* 4 */
        case GDK_KEY_percent:     return 0x35;   /* 5 */
        case GDK_KEY_asciicircum: return 0x36;   /* 6 */
        case GDK_KEY_ampersand:   return 0x37;   /* 7 */
        case GDK_KEY_asterisk:    return 0x38;   /* 8 */
        case GDK_KEY_parenleft:   return 0x39;   /* 9 */
        case GDK_KEY_parenright:  return 0x30;   /* 0 */
        default:                  return -1;
    }
}

static gboolean on_key_press(GtkWidget *w, GdkEventKey *e, gpointer d)
{
    (void)w; (void)d;
    int vk = gdk_keyval_to_vk(e->keyval);
    if (vk < 0) return FALSE;
    io_kbscancode = s23_scancode[vk];
    io_past_post  = true;            /* any real keystroke => past POST */
    i8259_assert_irq(0);
    return TRUE;                      /* consumed */
}

static gboolean on_key_release(GtkWidget *w, GdkEventKey *e, gpointer d)
{
    (void)w; (void)d;
    int vk = gdk_keyval_to_vk(e->keyval);
    if (vk < 0) return FALSE;
    int rel = s23_release[vk];
    if (rel < 0) return FALSE;        /* this key has no break code */
    io_kbscancode = (u8)rel;
    i8259_assert_irq(0);
    return TRUE;
}

/* =====================================================================
 *  Construction
 * ===================================================================*/
static GtkWidget *make_toolbar(void)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(bar), 3);

    suppress_combo_events = TRUE;

    power_btn = gtk_button_new_with_label("\xE2\x96\xB6 Power On");
    g_signal_connect(power_btn, "clicked", G_CALLBACK(on_power_clicked), NULL);

    rom_combo = gtk_combo_box_text_new();
    for (int v = 0; v < ROM_VERSION_COUNT; v++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rom_combo),
                                       rom_version_display_name((RomVersion)v));
    gtk_combo_box_set_active(GTK_COMBO_BOX(rom_combo), machine_rom_set());
    g_signal_connect(rom_combo, "changed", G_CALLBACK(on_rom_changed), NULL);

    lang_combo = gtk_combo_box_text_new();
    for (int c = 0; c < COUNTRY_COUNT; c++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(lang_combo),
                                       country_name((Countries)c));
    gtk_combo_box_set_active(GTK_COMBO_BOX(lang_combo), io_country);
    g_signal_connect(lang_combo, "changed", G_CALLBACK(on_lang_changed), NULL);

    GtkWidget *floppy_btn = gtk_button_new_with_label("Floppy\xE2\x80\xA6");
    g_signal_connect(floppy_btn, "clicked", G_CALLBACK(on_floppy), NULL);
    GtkWidget *debug_btn = gtk_button_new_with_label("Debug");
    g_signal_connect(debug_btn, "clicked", G_CALLBACK(on_debug), NULL);
    printer_toggle = gtk_toggle_button_new_with_label("Printer");
    g_signal_connect(printer_toggle, "toggled", G_CALLBACK(on_printer_toggled), NULL);
    GtkWidget *about_btn = gtk_button_new_with_label("About");
    g_signal_connect(about_btn, "clicked", G_CALLBACK(on_about), NULL);

    gtk_box_pack_start(GTK_BOX(bar), power_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(bar), gtk_label_new("ROM:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), rom_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), gtk_label_new("Language:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), lang_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(bar), floppy_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), debug_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), printer_toggle, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(bar), about_btn, FALSE, FALSE, 0);

    suppress_combo_events = FALSE;
    return bar;
}

GtkWidget *display_new(void)
{
    load_fonts();

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), APP_TITLE);
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 620);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), make_toolbar(), FALSE, FALSE, 0);

    area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 640, 384);       /* min; scales up      */
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    g_signal_connect(area, "draw", G_CALLBACK(on_draw), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), area, TRUE, TRUE, 0);

    /* Keyboard: the top-level window takes focus and feeds the KB/interrupt. */
    gtk_widget_set_can_focus(window, TRUE);
    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), NULL);
    g_signal_connect(window, "key-release-event", G_CALLBACK(on_key_release), NULL);

    g_timeout_add(1000 / 30, on_refresh_tick, NULL);
    update_power_button();
    return window;
}
