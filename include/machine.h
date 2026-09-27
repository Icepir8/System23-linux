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
