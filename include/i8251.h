/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Owen V. Michael, Jr.
 */
/* ===========================================================================
 *  i8251.h — Intel 8251 USART + printer/wrap adapter (port of I8251UART.cs +
 *  PrinterWrapAdapter.cs).  Reached through ports 0x48 (data) and 0x49
 *  (control/status); drives 8259 IRQ1 (TxRDY) and IRQ2 (RxRDY) directly.
 * ===========================================================================*/
#ifndef SYSTEM23_I8251_H
#define SYSTEM23_I8251_H

#include "system23.h"

void uart_reset(void);
u8   uart_read_port(u8 port);          /* ports 0x48 / 0x49 read  */
void uart_write_port(u8 port, u8 value);/* ports 0x48 / 0x49 write */
void uart_tick(void);                  /* periodic timing (from ioports_uart_tick) */

/* Printer output captured from the USART (for the Printer window). */
int  uart_get_print_output(char *buf, int size);
void uart_clear_print_output(void);

#endif /* SYSTEM23_I8251_H */
