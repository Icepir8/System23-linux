/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* i8255.h — Intel 8255 PPI (port of I8255PPI.cs). STUB.
 * The machine has three: 8255A/B/C, wired in IOports.cs with per-port
 * read/write callbacks (keyboard 8748 handshake, drive status, etc.). */
#ifndef SYSTEM23_I8255_H
#define SYSTEM23_I8255_H
#include "system23.h"
void i8255_reset_all(void);   /* TODO: reset the three PPIs */
#endif
