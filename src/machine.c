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
#include "floppy.h"

#include <glib.h>

MachineConfig g_config;

static volatile ExecState state    = EXEC_STOPPED;
static RomVersion         rom_set  = ROS_1_05;
static GThread           *run_thread = NULL;
static char              *roms_dir = NULL;

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

    machine_initialize();   /* cold power-on, no execution */
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
        for (int i = 0; i < 1000 && state == EXEC_RUNNING; i++) {
            u16 current = next;
            err = cpu8085_step(current, &next);
        }

        if (cpu8085_is_stub()) {
            /* CPU not ported yet: idle instead of spinning at 100% CPU. */
            g_usleep(10 * 1000);
        } else {
            /* TODO: cycle-correct ~3 MHz throttle.  Track cpu_cycles across the
             * batch and sleep until (cycles * 1e6 / 3_000_000) microseconds of
             * wall-clock (g_get_monotonic_time) have elapsed. */
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
    cpu_isrunning = true;
    state = EXEC_RUNNING;
    run_thread = g_thread_new("emulation", run_loop, NULL);
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
