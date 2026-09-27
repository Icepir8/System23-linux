/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
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

/* ---- Debugger control (used by the Debug window) --------------------------*/
void machine_step(void);               /* one instruction (only while stopped)     */

void machine_bp_add(u16 pc);
void machine_bp_remove(u16 pc);
void machine_bp_toggle(u16 pc);
bool machine_bp_has(u16 pc);
void machine_bp_clear_all(void);
int  machine_bp_count(void);
int  machine_bp_list(u16 *out, int max);   /* fills ascending; returns count       */

#endif /* SYSTEM23_MACHINE_H */
