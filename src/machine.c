/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  machine.c — application controller / machine host
 * ===========================================================================*/
#include "machine.h"
#include "memory.h"
#include "assets.h"
#include "cpu8085.h"
#include "ioports.h"
#include "i8275.h"
#include "i8259.h"
#include "i8257.h"
#include "i8251.h"
#include "floppy.h"
#include "i765a_fdc.h"

#include <glib.h>
#include <string.h>

MachineConfig g_config;

static volatile ExecState state    = EXEC_STOPPED;
static RomVersion         rom_set  = ROS_1_05;
static GThread           *run_thread = NULL;
static char              *roms_dir = NULL;

/* ---- Debug: PC breakpoints (bitmap over the 64K address space) ------------*/
static u8            bp_map[0x10000 / 8];   /* one bit per address            */
static volatile int  bp_count = 0;          /* fast "any breakpoints?" gate   */
static volatile u16  bp_resume_pc = 0;      /* PC we resumed from...          */
static volatile bool bp_resume_armed = false;/* ...don't re-break on it once  */

bool machine_bp_has(u16 pc) { return (bp_map[pc >> 3] >> (pc & 7)) & 1; }

void machine_bp_add(u16 pc)
{
    if (!machine_bp_has(pc)) { bp_map[pc >> 3] |= (u8)(1u << (pc & 7)); bp_count++; }
}
void machine_bp_remove(u16 pc)
{
    if (machine_bp_has(pc)) { bp_map[pc >> 3] &= (u8)~(1u << (pc & 7)); bp_count--; }
}
void machine_bp_toggle(u16 pc) { machine_bp_has(pc) ? machine_bp_remove(pc) : machine_bp_add(pc); }
void machine_bp_clear_all(void) { memset(bp_map, 0, sizeof bp_map); bp_count = 0; }
int  machine_bp_count(void) { return bp_count; }

/* Fill `out` with up to `max` breakpoint addresses (ascending); returns count. */
int machine_bp_list(u16 *out, int max)
{
    int n = 0;
    for (int a = 0; a < 0x10000 && n < max; a++)
        if (machine_bp_has((u16)a)) out[n++] = (u16)a;
    return n;
}

/* --------------------------------------------------------------------------*/
static void resolve_roms_dir(void)
{
    g_free(roms_dir);
    if (g_path_is_absolute(g_config.roms_path))
        roms_dir = g_strdup(g_config.roms_path);
    else
        roms_dir = assets_path(g_config.roms_path);   /* <assets base>/Roms */
}

const char *machine_roms_dir(void) { return roms_dir ? roms_dir : "Roms"; }
ExecState   machine_state(void)    { return state; }
RomVersion  machine_rom_set(void)  { return rom_set; }

/* --------------------------------------------------------------------------*/
static void restore_floppies(void)
{
    for (int i = 0; i < 4; i++) {
        const char *p = g_config.floppy_paths[i];
        if (p && *p && g_file_test(p, G_FILE_TEST_EXISTS))
            floppy_load_disk(i, p, g_config.floppy_wp[i]);
    }
}

void machine_init(void)
{
    config_load(&g_config);
    rom_set    = g_config.rom_set;
    io_country = g_config.language;
    resolve_roms_dir();

    dma_reset();             /* power-on the 8257 DMA controller */
    uart_reset();            /* power-on the 8251 USART */
    fdc_reset();             /* power-on the floppy subsystem (field-init state) */
    machine_initialize();    /* cold power-on, no execution */
    restore_floppies();
}

/* Bring the machine to a cold power-on state, but do NOT start executing.
 * Mirrors MachineHost.InitializeMachine: reload ROM, reset the CRTC and PIT,
 * boot the CPU at 0x0000, and let POST initialise the remaining hardware. */
void machine_initialize(void)
{
    machine_stop();

    cpu8085_reset();                             /* fresh engine, PC=0, cyc=0 */
    int loaded = memory_load_rom_set(rom_set, roms_dir);
    if (loaded == 0)
        g_warning("no ROM images loaded for %s from '%s' — check the Roms folder",
                  rom_version_display_name(rom_set), machine_roms_dir());

    i8275_reset();          /* CRTC status -> 0 (screen blanks until POST)    */
    ioports_reset_pit();    /* recreate just the PIT (POST test 34)           */
    /* NOTE: deliberately not resetting the FDC/8748 here — POST performs the
     * real hardware init, matching the original boot behaviour. */
}

void machine_load_rom_set(RomVersion v)
{
    rom_set = v;
    machine_initialize();
}

/* ---- run loop -------------------------------------------------------------*/
static gpointer run_loop(gpointer data)
{
    (void)data;
    const char *err  = "";
    u16         next = cpu.pc;

    while (state == EXEC_RUNNING && err[0] == '\0') {
        gint64 start     = g_get_monotonic_time();   /* microseconds */
        u64    start_cyc = cpu_cycles;

        for (int i = 0; i < 1000 && state == EXEC_RUNNING && err[0] == '\0'; i++) {
            u16 current = next;
            /* Stop just before executing an instruction at a breakpoint — but
             * not on the very instruction we just resumed from (or Run/Step at a
             * breakpoint would never make progress). */
            if (bp_count > 0 && machine_bp_has(current)) {
                if (bp_resume_armed && current == bp_resume_pc)
                    bp_resume_armed = false;             /* run the resume insn */
                else { state = EXEC_STOPPED; break; }
            }
            err = cpu8085_step(current, &next);
        }

        /* Cycle-correct ~3 MHz pacing: the 8085 runs 3 cycles per microsecond,
         * so sleep off whatever wall-clock time is left in this batch.
         * SYSTEM23_TURBO (debug) skips the pacing so headless boots finish fast. */
        static int turbo = -1;
        if (turbo < 0) turbo = g_getenv("SYSTEM23_TURBO") ? 1 : 0;
        if (!turbo) {
            u64    spent    = cpu_cycles - start_cyc;
            gint64 expected = (gint64)(spent / 3);
            gint64 elapsed  = g_get_monotonic_time() - start;
            if (expected > elapsed)
                g_usleep((gulong)(expected - elapsed));
        }
    }

    state = EXEC_STOPPED;
    cpu_isrunning = false;
    return NULL;
}

void machine_start(void)
{
    if (state != EXEC_STOPPED)
        return;
    /* Reap a thread that stopped itself at a breakpoint (machine_stop joins in
     * the normal case; a breakpoint hit exits the loop without joining). */
    if (run_thread) { g_thread_join(run_thread); run_thread = NULL; }
    bp_resume_pc = cpu.pc; bp_resume_armed = true;   /* run the resume insn */
    cpu_isrunning = true;
    state = EXEC_RUNNING;
    run_thread = g_thread_new("emulation", run_loop, NULL);
}

/* Execute exactly one instruction; only valid while stopped (no run thread). */
void machine_step(void)
{
    if (state != EXEC_STOPPED) return;
    if (run_thread) { g_thread_join(run_thread); run_thread = NULL; }
    u16 next = cpu.pc;
    cpu8085_step(cpu.pc, &next);    /* updates cpu.pc + all state internally */
}

void machine_stop(void)
{
    state = EXEC_STOPPED;
    cpu_isrunning = false;
    if (run_thread) {
        g_thread_join(run_thread);   /* the loop checks `state` each batch */
        run_thread = NULL;
    }
}

void machine_reset(void)
{
    machine_initialize();
    machine_start();
}

void machine_power_off(void)
{
    machine_initialize();   /* leaves state == EXEC_STOPPED */
}

/* --------------------------------------------------------------------------*/
void machine_shutdown(void)
{
    machine_stop();

    /* Capture live state back into the config (window bounds are written by
     * the UI layer, which owns the widgets). */
    g_config.rom_set  = rom_set;
    g_config.language = io_country;
    g_strlcpy(g_config.roms_path,
              g_path_is_absolute(g_config.roms_path) ? g_config.roms_path : "Roms",
              sizeof g_config.roms_path);

    for (int i = 0; i < 4; i++) {
        g_config.floppy_wp[i] = floppy_drives[i].write_protected;
        g_strlcpy(g_config.floppy_paths[i],
                  floppy_drives[i].loaded ? floppy_drives[i].image_path : "",
                  sizeof g_config.floppy_paths[i]);
    }

    config_save(&g_config);

    g_free(roms_dir);
    roms_dir = NULL;
}
