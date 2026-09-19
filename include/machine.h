/* ===========================================================================
 *  machine.h — application controller / machine host (port of MachineHost.cs)
 *
 *  Owns the emulated machine (CPU engine + global memory/I/O state) and the
 *  current run state.  Deliberately free of any GTK dependency: the emulation
 *  thread only touches emulation state, and the Display polls that state on its
 *  own refresh timer (exactly as the WinForms original did).
 * ===========================================================================*/
#ifndef SYSTEM23_MACHINE_H
#define SYSTEM23_MACHINE_H

#include "system23.h"
#include "config.h"

/* Live operator configuration (the UI updates window bounds directly). */
extern MachineConfig g_config;

void       machine_init(void);        /* load config + assets, cold init (no run) */
void       machine_shutdown(void);    /* stop, capture state, save config         */

void       machine_initialize(void);  /* cold power-on state, does NOT run         */
void       machine_load_rom_set(RomVersion v);
void       machine_start(void);        /* begin full-speed execution               */
void       machine_stop(void);
void       machine_reset(void);        /* cold reset, then run (reboot)             */
void       machine_power_off(void);    /* cold reset, stay stopped                  */

ExecState  machine_state(void);
RomVersion machine_rom_set(void);
const char *machine_roms_dir(void);    /* resolved ROM directory (for status/UI)   */

#endif /* SYSTEM23_MACHINE_H */
