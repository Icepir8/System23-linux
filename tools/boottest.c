/* boottest.c — off-tree sanity harness for the 8085 engine (not shipped).
 * 1) instruction/flag self-tests with hand-computed expected results
 * 2) runs the real ROS ROM and reports how the CPU fares. */
#include "cpu8085.h"
#include "memory.h"
#include "ioports.h"
#include "i8275.h"
#include "i8253.h"
#include "i8259.h"
#include "i8257.h"
#include "i8251.h"
#include "i765a_fdc.h"
#include "floppy.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* Load a program into RAM at 0x8000 and run it until HLT (or step cap). */
static void run_prog(const u8 *prog, int n)
{
    cpu8085_reset();
    for (int i = 0; i < n; i++) memory_write((u16)(0x8000 + i), prog[i]);
    u16 next = 0x8000;
    const char *err = "";
    for (int i = 0; i < 200 && err[0] == '\0'; i++) {
        u16 cur = next;
        err = cpu8085_step(cur, &next);
    }
}

static void selftests(void)
{
    printf("== instruction/flag self-tests ==\n");

    /* ADD: 0x14 + 0x27 = 0x3B, no carry */
    { u8 p[] = {0x3E,0x14, 0x06,0x27, 0x80, 0x76}; run_prog(p,sizeof p);
      CHECK(cpu.a==0x3B, "ADD A=%02X (want 3B)", cpu.a);
      CHECK(!cpu.fC && !cpu.fZ, "ADD flags C=%d Z=%d", cpu.fC, cpu.fZ); }

    /* SUI borrow: 0x05 - 0x0A = 0xFB, carry(borrow)=1, sign=1 */
    { u8 p[] = {0x3E,0x05, 0xD6,0x0A, 0x76}; run_prog(p,sizeof p);
      CHECK(cpu.a==0xFB, "SUI A=%02X (want FB)", cpu.a);
      CHECK(cpu.fC && cpu.fS, "SUI flags C=%d S=%d", cpu.fC, cpu.fS); }

    /* ANI: 0xF0 & 0x0F = 0, Z=1, AC=1 (8085), C=0 */
    { u8 p[] = {0x3E,0xF0, 0xE6,0x0F, 0x76}; run_prog(p,sizeof p);
      CHECK(cpu.a==0x00 && cpu.fZ && cpu.fAC && !cpu.fC, "ANI A=%02X Z=%d AC=%d C=%d", cpu.a,cpu.fZ,cpu.fAC,cpu.fC); }

    /* RLC: 0x85 -> 0x0B, C=1 */
    { u8 p[] = {0x3E,0x85, 0x07, 0x76}; run_prog(p,sizeof p);
      CHECK(cpu.a==0x0B && cpu.fC, "RLC A=%02X C=%d (want 0B,1)", cpu.a, cpu.fC); }

    /* INX B from 0x7FFF -> 0x8000, undocumented K flag set */
    { u8 p[] = {0x01,0xFF,0x7F, 0x03, 0x76}; run_prog(p,sizeof p);
      CHECK(cpu.b==0x80 && cpu.c==0x00 && cpu.fK, "INX BC=%02X%02X K=%d (want 8000,1)", cpu.b,cpu.c,cpu.fK); }

    /* DAD B: FFFF + 0001 = 0000, C=1 */
    { u8 p[] = {0x21,0xFF,0xFF, 0x01,0x01,0x00, 0x09, 0x76}; run_prog(p,sizeof p);
      CHECK(cpu.h==0x00 && cpu.l==0x00 && cpu.fC, "DAD HL=%02X%02X C=%d (want 0000,1)", cpu.h,cpu.l,cpu.fC); }

    /* DAA: 0x99 + 0x99 = 0x32(C,AC) -> DAA -> 0x98, C=1  (BCD 99+99=198) */
    { u8 p[] = {0x3E,0x99, 0x06,0x99, 0x80, 0x27, 0x76}; run_prog(p,sizeof p);
      CHECK(cpu.a==0x98 && cpu.fC, "DAA A=%02X C=%d (want 98,1)", cpu.a, cpu.fC); }

    printf("  %s\n", fails ? "SELF-TESTS FAILED" : "all self-tests passed");
}

static void rom_run(const char *roms_dir, const char *disk_path)
{
    printf("\n== real ROM execution (ROS 1.05) ==\n");
    int loaded = memory_load_rom_set(ROS_1_05, roms_dir);
    printf("  banks loaded: %d\n", loaded);
    printf("  reset vector bytes @0000: %02X %02X %02X %02X\n",
           memory_read(0), memory_read(1), memory_read(2), memory_read(3));

    { const char *w = getenv("SYSTEM23_MEMWATCH"); if (w) dbg_memwatch = (u16)strtol(w, 0, 16); }

    /* Power-on the machine the way machine_init/machine_initialize does:
     * reset the PIT (gate high -> counters run), DMA, USART, then the FDC. */
    i8253_reset();
    dma_reset();
    uart_reset();
    fdc_reset();
    if (disk_path && *disk_path) {
        bool ok = floppy_load_disk(0, disk_path, false);
        printf("  disk drive 0   : %s  (%s)\n", disk_path, ok ? "loaded" : "FAILED");
    }

    cpu8085_reset();
    u16 next = 0x0000, pcmin = 0xFFFF, pcmax = 0;
    const char *err = "";
    long steps = 0;
    const long CAP = 20L * 1000 * 1000;
    u16 ring[48]; int rn = 0;
    u8  in_ports[256]  = {0};
    u8  out_ports[256] = {0};
    int inject_keys = getenv("SYSTEM23_INJECTKEY") ? 1 : 0;
    long stuck = 0; u16 last_pc = 0xFFFF; int injected = 0; long last_inject = 0;
    for (; steps < CAP && err[0] == '\0'; steps++) {
        u16 cur = next;
        if (cur < pcmin) pcmin = cur;
        if (cur > pcmax) pcmax = cur;
        u8 opc = memory_read(cur);
        if (opc == 0xDB) in_ports[memory_read((u16)(cur+1))]  = 1;   /* IN  */
        if (opc == 0xD3) out_ports[memory_read((u16)(cur+1))] = 1;   /* OUT */
        ring[rn++ % 48] = cur;
        if (cur == 0x06FD) {   /* POST failure-record entry */
            u16 ret = (u16)(memory_read(cpu.sp) | (memory_read((u16)(cpu.sp+1)) << 8));
            fprintf(stderr, "  [FAIL-REC] step=%ld diagport=%02X caller~=%04X IMR=%02X ie=%d "
                    "A200:%02X %02X %02X %02X\n",
                    steps, io_diagnostic_port, ret, i8259_read_data(), cpu.ie,
                    memory_read(0xA200), memory_read(0xA201),
                    memory_read(0xA202), memory_read(0xA203));
        }
        if (cur == 0x099D || cur == 0x0988)   /* USART/service RX ISR entry */
            fprintf(stderr, "  [RX-ISR] step=%ld pc=%04X\n", steps, cur);
        /* Log PIC mask writes (OUT 0x29) in the USART-test window. */
        if (opc == 0xD3 && memory_read((u16)(cur+1)) == 0x29 &&
            1)
            fprintf(stderr, "  [OUT29] step=%ld IMR<=%02X\n", steps, cpu.a);
        /* When parked in the boot-prompt wait loop, inject an operator keystroke
         * on a step cadence (PIT interrupts otherwise reset a tight-loop counter). */
        if (inject_keys && (cur >= 0x4933 && cur <= 0x4937) &&
            steps - last_inject > 1500000 && injected < 20) {
            static const u8 keys[] = { 0x10, 0x1C, 0x0D, 0x31, 0x20 };
            io_kbscancode = keys[injected % 5]; io_past_post = true;
            i8259_assert_irq(0);
            fprintf(stderr, "  [inject] key %02X #%d at step %ld (PC~%04X)\n",
                    io_kbscancode, injected + 1, steps, cur);
            injected++; last_inject = steps;
        }
        (void)stuck; (void)last_pc;
        last_pc = cur;
        err = cpu8085_step(cur, &next);
    }
    printf("  last PCs       :");
    for (int i = (rn>24?rn-24:0); i < rn; i++) printf(" %04X", ring[i%48]);
    printf("\n  IN ports seen  :"); for (int i=0;i<256;i++) if (in_ports[i]) printf(" %02X", i);
    printf("\n  OUT ports seen :"); for (int i=0;i<256;i++) if (out_ports[i]) printf(" %02X", i);
    printf("\n");
    printf("  steps executed : %ld%s\n", steps, (steps==CAP?" (hit cap — still running)":""));
    printf("  stop reason    : %s\n", err[0] ? err : "(cap reached, no error)");
    printf("  final PC       : %04X  (opcode %02X)\n", next, memory_read(next));
    printf("  PC range seen  : %04X .. %04X\n", pcmin, pcmax);
    printf("  cycles         : %llu\n", (unsigned long long)cpu_cycles);
    printf("  crtc_status    : %02X  (display %s)\n", crtc_status,
           (crtc_status & 0x2C) ? "LIVE" : "blanked");
    fprintf(stderr, "  IRQ delivered  : 8259=%llu rst7.5=%llu rst6.5=%llu rst5.5=%llu\n",
            (unsigned long long)dbg_irq8259, (unsigned long long)dbg_rst75,
            (unsigned long long)dbg_rst65, (unsigned long long)dbg_rst55);
    fprintf(stderr, "  EI executed    : %llu   PIT ctr2->75 fired: %llu   ie now=%d  m55=%d m65=%d m75=%d\n",
            (unsigned long long)dbg_ei_count, (unsigned long long)dbg_pit75, cpu.ie,
            cpu.m55, cpu.m65, cpu.m75);
    i8253_debug_dump();
    i8259_debug_dump();
    fprintf(stderr, "  flag [0x89A5]  : %02X   [0x89A4]=%02X [0x89A6]=%02X\n",
            memory_read(0x89A5), memory_read(0x89A4), memory_read(0x89A6));
    fprintf(stderr, "  --- last I/O accesses ---\n");
    ioports_dump_ring();
    /* Dump the full CPU address space as mapped at the hang, for offline
     * disassembly (SYSTEM23_MEMDUMP=path). */
    { const char *mp = getenv("SYSTEM23_MEMDUMP");
      if (mp) { FILE *f = fopen(mp, "wb");
                if (f) { for (int a = 0; a < 0x10000; a++) fputc(memory_read((u16)a), f); fclose(f);
                         fprintf(stderr, "  wrote memory image to %s\n", mp); } } }
    printf("  char_page      : %u   cursor r%d c%d\n",
           io_char_page_read, crtc_cursor_row, crtc_cursor_col);

    /* Dump the 80x24 video RAM (mem_ram[0x200..]) as printable chars. */
    int nonblank = 0;
    for (int i = 0; i < 24 * 80; i++) if (mem_ram[0x200 + i] != 0 && mem_ram[0x200 + i] != 0x20) nonblank++;
    printf("  screen non-blank cells: %d\n", nonblank);
    if (nonblank) {
        printf("  --- video RAM 0x200 (printable 0x20-0x7E as-is) ---\n");
        for (int r = 0; r < 24; r++) {
            char line[81];
            for (int c = 0; c < 80; c++) {
                u8 ch = mem_ram[0x200 + r * 80 + c];
                line[c] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '.';
            }
            line[80] = 0;
            printf("  |%s|\n", line);
        }
    }
}

int main(int argc, char **argv)
{
    selftests();
    rom_run(argc > 1 ? argv[1] : "Roms", argc > 2 ? argv[2] : NULL);
    return fails ? 1 : 0;
}
