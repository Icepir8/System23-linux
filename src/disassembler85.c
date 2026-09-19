/* disassembler85.c — 8085 disassembler (STUB). TODO: port DisAssembler85.cs. */
#include "disassembler85.h"
#include "memory.h"
#include <stdio.h>

int disassembler85_at(u16 addr, char *text, size_t text_size)
{
    /* Placeholder: emit the raw byte as data until the decoder is ported. */
    if (text && text_size)
        snprintf(text, text_size, "DB   %02Xh", memory_read(addr));
    return 1;
}
