/* ===========================================================================
 *  i8251.c — Intel 8251 USART + printer/wrap adapter
 *  (faithful port of I8251UART.cs plus the USART-facing parts of
 *  PrinterWrapAdapter.cs).
 *
 *  The System/23 routes only ports 0x48 (data) and 0x49 (control/status) to
 *  this device.  The 8251 drives the 8259 directly: WriteData asserts IRQ1
 *  (TxRDY) and Receive asserts IRQ2 (RxRDY).  POST's USART self-test
 *  (diagnostic steps 0x35/0x36) writes bytes to 0x48; the adapter loops them
 *  back (Wrap) or, for the diagnose command 0x11, feeds a canned reply — each
 *  looped/replied byte is Received, raising IRQ2 so the guest ISR reads
 *  IN 0x49 / IN 0x48 and fills its result buffer at 0xA200.
 * ===========================================================================*/
#include "i8251.h"
#include "i8259.h"
#include "ioports.h"     /* io_diagnostic_port */
#include <stdio.h>
#include <stdlib.h>
static int utr = -1;
static int utrace(void){ if(utr<0) utr = getenv("SYSTEM23_UARTTRACE")?1:0; return utr; }
#define UT(...) do{ if(utrace()){ fprintf(stderr,"[UART] "); fprintf(stderr,__VA_ARGS__); fprintf(stderr,"\n"); } }while(0)

/* ---- 8251 status bits -----------------------------------------------------*/
#define ST_TXRDY   0x01
#define ST_RXRDY   0x02
#define ST_TXEMPTY 0x04
#define ST_PE      0x08
#define ST_OE      0x10
#define ST_FE      0x20
#define ST_BRKDET  0x40
#define ST_DSR     0x80
/* ---- 8251 command bits ----------------------------------------------------*/
#define CMD_TXEN 0x01
#define CMD_DTR  0x02
#define CMD_RXE  0x04
#define CMD_SBRK 0x08
#define CMD_ER   0x10
#define CMD_RTS  0x20
#define CMD_IR   0x40
#define CMD_EH   0x80

/* ---- 8251 core state ------------------------------------------------------*/
static bool expect_mode;
static u8   uart_status, uart_command, uart_mode;
static u8   rx_holding, tx_holding;
static bool tx_loaded;
static bool dsr_pin, cts_pin;

/* ---- adapter state --------------------------------------------------------*/
static bool wrap;                       /* PrinterWrapAdapter.Wrap (default 1) */
#define DIAGNOSE_CMD 0x11
static const u8 diagnose_response[3] = { 0x01, 0x32, 0x00 };
static u8   response_q[16];
static int  resp_head, resp_count;
static u8   status28;                   /* adapter port-0x28 status (bit2=rx) */

/* forward decls */
static void uart_receive(u8 value);
static void on_transmit(u8 b);
static void pump_response(void);

/* ---- 8251 transmit path ---------------------------------------------------*/
static void set_tx_ready_empty(void)
{
    uart_status |= (u8)(ST_TXRDY | ST_TXEMPTY);
    /* TxEmpty→IRQ1 is only gated on in the adapter's (unused) SetTxInterrupt;
     * the real IRQ1 is asserted directly in write_data(). */
}

static void try_transmit(void)
{
    if (!tx_loaded) { set_tx_ready_empty(); return; }
    if ((uart_command & CMD_TXEN) == 0) return;   /* transmitter disabled: hold */
    if (!cts_pin) return;                          /* /CTS not asserted: hold */
    u8 out_byte = tx_holding;
    tx_loaded = false;
    on_transmit(out_byte);
    set_tx_ready_empty();
}

/* ---- 8251 mode / command --------------------------------------------------*/
static void load_mode(u8 m) { uart_mode = m; expect_mode = false; }

static void load_command(u8 c)
{
    uart_command = c;
    if (c & CMD_IR) { uart_reset(); return; }      /* internal reset */
    if (c & CMD_ER) uart_status &= (u8)~(ST_PE | ST_OE | ST_FE);
    if ((c & CMD_RXE) == 0) uart_status &= (u8)~ST_RXRDY;  /* receiver disabled */
    if (c & CMD_TXEN) try_transmit();
}

/* ---- 8251 bus interface ---------------------------------------------------*/
static u8 read_status(void)
{
    u8 s = (u8)(uart_status & ~ST_DSR);
    if (dsr_pin) s |= ST_DSR;
    return s;
}

static u8 read_data(void)
{
    uart_status &= (u8)~ST_RXRDY;
    status28 |= 0x04;                              /* RxReady callback side effect */
    u8 d = rx_holding;
    /* Multi-byte reply pacing: the guest ISR reads one byte per interrupt, so
     * hand it the next queued reply byte once the receiver is free. */
    if (resp_count > 0 && (uart_status & ST_RXRDY) == 0) {
        u8 b = response_q[resp_head];
        resp_head = (resp_head + 1) & 15;
        resp_count--;
        uart_receive(b);
        i8259_assert_irq(1);
    }
    return d;
}

static void write_data(u8 value)
{
    UT("OUT48 val=%02X diag=%02X wrap=%d cmd=%02X(TXEN=%d RXE=%d) cts=%d",
       value, io_diagnostic_port, wrap, uart_command,
       (uart_command & CMD_TXEN) != 0, (uart_command & CMD_RXE) != 0, cts_pin);
    tx_holding = value;
    tx_loaded  = true;
    uart_status &= (u8)~(ST_TXRDY | ST_TXEMPTY);
    try_transmit();                                /* InstantTransmit */
    i8259_assert_irq(1);
}

static void write_control(u8 value)
{
    if (expect_mode) load_mode(value);
    else             load_command(value);
}

/* ---- 8251 wire interface --------------------------------------------------*/
static void uart_receive(u8 value)
{
    if ((uart_command & CMD_RXE) == 0) return;     /* receiver disabled */
    bool was_ready = (uart_status & ST_RXRDY) != 0;
    if (was_ready) uart_status |= ST_OE;           /* overrun */
    rx_holding = value;
    uart_status |= ST_RXRDY;
    i8259_assert_irq(2);                           /* RxRDY -> IRQ2 (latched) */

    if (!was_ready) status28 |= 0x04;              /* RxReady side effect (0->1 edge) */
    /* The guest fills its result buffer from its own interrupt handler: the
     * TX-empty ISR (IRQ1, asserted by write_data) reads IN $49 / IN $48 and
     * stores {status,data}.  We only latch RxRDY + IRQ2 here; the ISR's IN $48
     * (read_data) clears RxRDY and pumps the next queued reply byte. */
}

/* ---- adapter glue ---------------------------------------------------------*/
static void pump_response(void)
{
    if (resp_count > 0 && (uart_status & ST_RXRDY) == 0) {
        u8 b = response_q[resp_head];
        resp_head = (resp_head + 1) & 15;
        resp_count--;
        uart_receive(b);
    }
}

static void on_transmit(u8 b)
{
    UT("on_transmit b=%02X wrap=%d diag=%02X", b, wrap, io_diagnostic_port);
    if (b == DIAGNOSE_CMD) {                        /* ROUTINE 37: canned reply */
        resp_head = resp_count = 0;
        for (int i = 0; i < 3; i++) response_q[resp_count++] = diagnose_response[i];
        pump_response();
        return;
    }
    if (wrap && io_diagnostic_port == 0x35) {       /* ROUTINE 36: electronic wrap */
        uart_receive(b);
        return;
    }
    /* else: real print output — not needed to boot. */
}

/* ===========================================================================
 *  Public interface (called from ioports.c for ports 0x48 / 0x49)
 * ===========================================================================*/
u8 uart_read_port(u8 port)
{
    switch (port) {
        case 0x48: return read_data();
        case 0x49: return read_status();
        default:   return 0xFF;
    }
}

void uart_write_port(u8 port, u8 value)
{
    switch (port) {
        case 0x48: write_data(value); break;
        case 0x49: write_control(value); break;
        default: break;
    }
}

void uart_tick(void) { /* printer flow-control timing — not needed to boot */ }

void uart_reset(void)
{
    expect_mode = true;
    uart_command = 0;
    uart_mode = 0;
    tx_loaded = false;
    rx_holding = 0;
    tx_holding = 0;
    uart_status = (u8)(ST_TXRDY | ST_TXEMPTY);
    dsr_pin = true;    /* adapter asserts /DSR */
    cts_pin = true;    /* /CTS asserted by default -> transmit allowed */

    wrap = true;
    resp_head = resp_count = 0;
    status28 = 0;
}
