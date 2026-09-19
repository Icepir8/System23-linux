/* ===========================================================================
 *  i8253.h — Intel 8253 programmable interval timer (port of I8253PIT.cs)
 *  STATUS: stub.  ioports_reset_pit() recreates it on a warm reset.
 * ===========================================================================*/
#ifndef SYSTEM23_I8253_H
#define SYSTEM23_I8253_H

#include "system23.h"

void i8253_reset(void);
void i8253_tick(u64 cycles);   /* advance the counters by `cycles` (TODO) */

#endif /* SYSTEM23_I8253_H */
