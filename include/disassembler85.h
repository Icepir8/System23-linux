/* ===========================================================================
 *  disassembler85.h — 8085 disassembler (port of DisAssembler85.cs)
 *  STATUS: stub.  Used by the debug window's code view.
 * ===========================================================================*/
#ifndef SYSTEM23_DISASSEMBLER85_H
#define SYSTEM23_DISASSEMBLER85_H

#include "system23.h"

/* Disassemble the instruction at `addr` (reading via memory_read) into `text`.
 * Returns the number of bytes the instruction occupies. */
int disassembler85_at(u16 addr, char *text, size_t text_size);

#endif /* SYSTEM23_DISASSEMBLER85_H */
