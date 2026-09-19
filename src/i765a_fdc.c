/* ===========================================================================
 *  i765a_fdc.c — floppy subsystem: FDC-card 8255 + 8748 stepper controller +
 *  NEC765 status registers AND command engine (faithful port of the
 *  boot-critical parts of FloppyController.cs).
 *
 *  Layers:
 *    - FDC-card 8255 PPI handshake (Mode 0 latch test, Mode 2 "8748 walk test")
 *    - 8748 stepper-controller command dispatch + register RAM
 *    - NEC765 status registers (SRB / MSR / DOR)
 *    - NEC765 command engine: SPECIFY / SENSE DRIVE / RECALIBRATE / SEEK /
 *      SENSE INTERRUPT / READ ID / READ DATA (+ WRITE DATA) driven through the
 *      8257 DMA, with the System/23's deferred-arm and software-paced
 *      multi-sector streaming conventions.
 *    - ImageDisk (.IMD) loader + per-track sector I/O (mixed 128/512 geometry).
 *
 *  The NEC765 INTRQ is delivered as 8259 IRQ4 (the original's RST-6.5 path is
 *  vestigial: OnInterrupt is never wired and do655interrupt is commented out).
 * ===========================================================================*/
#include "i765a_fdc.h"
#include "ioports.h"
#include "cpu8085.h"     /* cpu_cycles */
#include "i8259.h"
#include "i8257.h"
#include "memory.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/* Optional command-flow trace (set SYSTEM23_FDCTRACE=1).  A debugging aid for
 * the disk-boot handshake; compiles to nothing hot when the env var is unset. */
static int fdc_trace = -1;
static int trace_on(void)
{
    if (fdc_trace < 0) fdc_trace = getenv("SYSTEM23_FDCTRACE") ? 1 : 0;
    return fdc_trace;
}
#define FTRACE(...) do { if (trace_on()) { \
        fprintf(stderr, "[FDC] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

#define DRIVE_COUNT 4
#define DMA_CHANNEL 0

/* ── NEC765 command codes (lower 5 bits) ───────────────────────────────────*/
#define CMD_SPECIFY       0x03
#define CMD_SENSE_DRV     0x04
#define CMD_WRITE_DATA    0x05
#define CMD_READ_DATA     0x06
#define CMD_RECALIBRATE   0x07
#define CMD_SENSE_INT     0x08
#define CMD_WRITE_DELETED 0x09
#define CMD_READ_ID       0x0A
#define CMD_READ_DELETED  0x0C
#define CMD_FORMAT        0x0D
#define CMD_SEEK          0x0F
#define CMD_SCAN_EQ       0x11
#define CMD_SCAN_LE       0x19
#define CMD_SCAN_HE       0x1D

/* ── Main Status Register bits ─────────────────────────────────────────────*/
#define MSR_RQM 0x80
#define MSR_DIO 0x40
#define MSR_CB  0x10

/* ── ST0 ───────────────────────────────────────────────────────────────────*/
#define ST0_IC_NORMAL  0x00
#define ST0_IC_ABNORM  0x40
#define ST0_IC_INVALID 0x80
#define ST0_SE         0x20
#define ST0_EC         0x10
#define ST0_NR         0x08
/* ── ST1 ───────────────────────────────────────────────────────────────────*/
#define ST1_EN 0x80
#define ST1_DE 0x20
#define ST1_OR 0x10
#define ST1_ND 0x04
#define ST1_NW 0x02
#define ST1_MA 0x01
/* ── ST2 ───────────────────────────────────────────────────────────────────*/
#define ST2_CM 0x40
#define ST2_WC 0x10
#define ST2_SH 0x08
#define ST2_SN 0x04

/* DOR bit4 = MOTOR0 / WDATA loopback (mirrored in SRB bit3) */
#define DOR_WDATA 0x10

#define WC_SETTLE_CYCLES        100000
#define CURRENT_SENSE_RISE      40000
#define INDEX_PULSE_PERIOD      150
#define INDEX_PULSE_WIDTH       8

#define MAX_XFER_BYTES 8192   /* largest sector the transfer buffers hold */

/* 8748 register-RAM offsets (from the ROM hardware init at 0x3B2) */
#define R8748_DRV_STATUS 0x1D
#define R8748_CTRL_REG   0x2F
#define R8748_INIT_2C    0x36
#define R8748_CMD_LATCH  0x26

typedef enum { PH_IDLE = 0, PH_COMMAND, PH_EXECUTION, PH_RESULT } FdcPhase;
typedef enum { P8748_NONE = 0, P8748_REGWRITE, P8748_STEP } Pending8748;

/* ── Per-track geometry (System/23 disks mix 128-byte cyl-0 and 512-byte
 *    cyl-1+ tracks, so one uniform CHS cannot describe the disk). ───────────*/
typedef struct {
    int  sector_size;
    u8  *sec_map;       /* logical sector #s, physical order (NULL => absent) */
    int  sec_count;
    u8  *deleted;       /* per-sector deleted-mark, or NULL */
    int  data_offset;   /* start in FdcDrive.data */
} TrackInfo;

typedef struct {
    /* nominal geometry / uniform fallback */
    int  cylinders;
    int  heads;
    int  sectors;
    int  sector_size;
    /* image */
    u8       *data;
    int       data_len;
    TrackInfo *tracks;      /* indexed by cyl*heads+head; NULL => uniform CHS */
    int        n_tracks;
    /* flags */
    bool loaded, present, write_protected;
    /* mechanical */
    bool motor_on;
    int  current_cylinder;
} FdcDrive;

static FdcDrive fd[DRIVE_COUNT];

/* ── NEC765 core state ─────────────────────────────────────────────────────*/
static FdcPhase phase;
static u8   dor_reg;
static int  index_counter;
static s64  wdata_rise_cycle = INT64_MAX;
static s64  wc_settle_cycle[DRIVE_COUNT];

/* Command / result / sense-interrupt queues */
static u8   cmd_buf[9];
static int  cmd_len, cmd_idx;
static u8   res_buf[7];
static int  res_len, res_idx;

#define SINT_QUEUE 8
static u8   sint_st0[SINT_QUEUE];
static u8   sint_pcn[SINT_QUEUE];
static int  sint_count;

static bool drive_busy[DRIVE_COUNT];
static u8   spec_srh, spec_hln;

/* Active transfer context */
static int  x_drive, x_head, x_cyl, x_sector, x_eot, x_sector_size, x_start_sector;
static bool x_write, x_deleted;
static u8   x_buf[MAX_XFER_BYTES];
static int  x_buf_pos;

/* Deferred-DMA arming (host programs the 8257 after the command) */
static bool drq_pending, arm_addr_written, arm_cnt_written;

/* Software-paced multi-sector streaming (ROM re-arms the 8257 per sector) */
static bool stream_active;
static int  stream_next_sector;
static bool stream_rearm_addr, stream_rearm_cnt;
static u8   stream_buf[MAX_XFER_BYTES];

/* ── 8748 stepper controller ───────────────────────────────────────────────*/
static u8   ram8748[64];
static u8   status8748;
static int  pending8748;      /* Pending8748 */
static int  pending_arg;
static u8   r4_8748;
static bool seek_active[DRIVE_COUNT];
static int  active_drive = -1;

/* ── FDC-card 8255 PPI ──────────────────────────────────────────────────────*/
static u8   ppi_pa, ppi_pb, ppi_pc;
static bool mode2_active, pc_upper_input, ibfa, obf_low, intr_b, cmd_raised_interrupt;
static u8   walk_queue[2];
static int  walk_head, walk_count;

static s64 cpu_cyc(void) { return (s64)cpu_cycles; }

static bool drive_ready(int i) { return fd[i].loaded && fd[i].motor_on; }

static int selected_drive_index(void)
{
    for (int i = 0; i < DRIVE_COUNT; i++)
        if (dor_reg & (1 << i)) return i;
    return -1;
}
static int first_spinning_loaded(void)
{
    for (int i = 0; i < DRIVE_COUNT; i++)
        if (fd[i].loaded && fd[i].motor_on) return i;
    return -1;
}
static int  ram_block_to_drive(int block) { return (block - 0x20) >> 3; }
static bool is_drive_block_base(int a) { return a == 0x20 || a == 0x28 || a == 0x30 || a == 0x38; }

/* ===========================================================================
 *  Interrupt (NEC765 INTRQ -> 8259 IRQ4)
 * ===========================================================================*/
static void fdc_raise_interrupt(int port_b_data)
{
    if (port_b_data >= 0) ppi_pb = (u8)port_b_data;
    intr_b = true;
    cmd_raised_interrupt = true;
    io_do655interrupt = true;
    i8259_assert_irq(4);   /* FloppyIrqLine = 4 */
}

/* ===========================================================================
 *  8748 command dispatch (+ seek/step/write execs)
 * ===========================================================================*/
static void exec8748_cylinder_adjust(int block)
{
    if (block < 0 || block + 3 >= (int)sizeof ram8748) return;
    ram8748[block] &= 0x7F;
    u8 raw = ram8748[block + 2];
    ram8748[block + 3] = (u8)((raw + 0xFD) & 0xFC);   /* (raw - 3) & 0xFC */
}

static void exec8748_seek(u8 cmd)
{
    int block = cmd & 0x38;
    int d = ram_block_to_drive(block);
    if ((unsigned)d >= DRIVE_COUNT) { status8748 = 0x40; return; }
    active_drive = d;
    seek_active[d] = true;

    bool step_in = (cmd & 0x01) != 0;
    ram8748[block] &= 0xF7;                 /* clear bit3 (seek-pending) */

    if (step_in) {
        if (fd[d].current_cylinder < fd[d].cylinders - 1) fd[d].current_cylinder++;
    } else {
        fd[d].current_cylinder = 0;         /* recalibrate to track 0 */
    }
    if (block + 2 < (int)sizeof ram8748) ram8748[block + 2] = (u8)fd[d].current_cylinder;

    if (r4_8748 != 0) { exec8748_cylinder_adjust(0x30); exec8748_cylinder_adjust(0x38); }
    status8748 = 0x00;                       /* polled completion (no IRQ) */
}

static void exec8748_step_to(int block, u8 target)
{
    int d = ram_block_to_drive(block);
    if ((unsigned)d >= DRIVE_COUNT) { status8748 = 0x40; return; }

    if (target & 0x80) {                     /* bit7 => register read, not a seek */
        int reg = target & 0x3F;
        status8748 = (reg < (int)sizeof ram8748) ? ram8748[reg] : 0xFF;
        return;
    }
    if (target >= fd[d].cylinders) target = (u8)(fd[d].cylinders - 1);
    fd[d].current_cylinder = target;
    active_drive = d;
    if (block + 2 < (int)sizeof ram8748) ram8748[block + 2] = target;
    ram8748[R8748_CMD_LATCH] = 0x00;
    int status_addr = block - 2;
    if (status_addr >= 0 && status_addr < (int)sizeof ram8748) ram8748[status_addr] |= 0x20;
    status8748 = 0x00;
}

static void exec8748_write_data(u8 cmd)
{
    int block = cmd & 0x38;
    int d = ram_block_to_drive(block);
    if ((unsigned)d >= DRIVE_COUNT) { status8748 = 0x40; return; }
    if (block < (int)sizeof ram8748) ram8748[block] = 0x51;   /* write-complete marker */
    if ((unsigned)d < 4) wc_settle_cycle[d] = cpu_cyc() + WC_SETTLE_CYCLES;
    int latch = block + 6;
    if (latch < (int)sizeof ram8748) ram8748[latch] = 0x00;
    int status_addr = block - 2;
    if (status_addr >= 0 && status_addr < (int)sizeof ram8748) ram8748[status_addr] |= 0x20;
    status8748 = 0x00;
}

static void write8748_command(u8 command)
{
    /* Second phase of an InitDataTransfer handshake. */
    if (pending8748 != P8748_NONE) {
        int kind = pending8748, arg = pending_arg;
        pending8748 = P8748_NONE;
        if (kind == P8748_REGWRITE) {
            if (arg < (int)sizeof ram8748) ram8748[arg] = command;
            if (arg == 4) r4_8748 = command;
            status8748 = 0x00;
        } else { /* STEP */
            exec8748_step_to(arg, command);
        }
        return;
    }

    u8 cmd = command;

    /* Register access (bit6=1) */
    if (cmd & 0x40) {
        int reg = cmd & 0x3F;
        if (cmd & 0x80) {                    /* Reg Wr: two-phase, signal ready */
            pending8748 = P8748_REGWRITE;
            pending_arg = reg;
            status8748 = 0x08;
        } else {                             /* Reg Rd: return RAM[reg], live bit2 */
            if (reg >= (int)sizeof ram8748) {
                status8748 = 0xFF;
            } else {
                u8 st = ram8748[reg];
                if (is_drive_block_base(reg)) {
                    int rd = ram_block_to_drive(reg);
                    bool ready = (unsigned)rd < DRIVE_COUNT && fd[rd].loaded;
                    st = (u8)(ready ? (st | 0x04) : (st & ~0x04));
                    if ((unsigned)rd < DRIVE_COUNT && seek_active[rd]) st |= 0x08;
                    if ((unsigned)rd < 4 && cpu_cyc() >= wc_settle_cycle[rd]) st &= 0xEF;
                }
                status8748 = st;
            }
        }
        return;
    }

    u8 masked = (u8)(cmd & 0xE4);
    if (masked == 0x24) { exec8748_seek(cmd); return; }          /* Type 1: seek */

    masked = (u8)(cmd & 0xE7);
    if (masked == 0x21) {                                         /* Type 2: step */
        int block = cmd & 0x38;
        int d = ram_block_to_drive(block);
        if ((unsigned)d >= DRIVE_COUNT) { status8748 = 0x40; return; }
        pending8748 = P8748_STEP;
        pending_arg = block;
        status8748 = 0x08;
        return;
    }
    if (masked == 0x22) { exec8748_write_data(cmd); return; }     /* Type 3: write */

    status8748 = 0x40;                                            /* unknown */
}

/* ===========================================================================
 *  FDC-card 8255 PPI
 * ===========================================================================*/
static u8 ppi_read_port_a(void)
{
    if (!mode2_active) return ppi_pa;        /* Mode 0: PA output latch */

    u8 val = walk_count > 0 ? walk_queue[walk_head] : status8748;
    if (walk_count > 0) {
        walk_head = (walk_head + 1) % 2;
        walk_count--;
        if (walk_count > 0) ibfa = true; else ibfa = false;
    } else {
        ibfa = false;
    }
    return val;
}

static u8 ppi_read_port_b(void)
{
    /* Reading Port B is the host ISR's acknowledge: clear INTRB / RST 6.5. */
    intr_b = false;
    io_do655interrupt = false;
    u8 v = ppi_pb;
    ppi_pb = 0;
    return v;
}

static u8 ppi_read_port_c(void)
{
    if (mode2_active) {
        bool obf_high = !obf_low;
        if (obf_low) obf_low = false;        /* instant ACK */
        return (u8)((ppi_pc & 0x5C)
                    | (obf_high ? 0x80 : 0x00)
                    | (intr_b ? 0x02 : 0x00)
                    | 0x04
                    | (ibfa ? 0x20 : 0x00)
                    | (intr_b ? 0x01 : 0x00));
    }
    if (pc_upper_input)
        return (u8)((ppi_pc & 0x0F) | 0xA2); /* 8748 P2 pins all HIGH */
    return ppi_pc;                           /* Mode 0 PC output latch */
}

static void ppi_write_port_a(u8 value)
{
    if (!mode2_active) {
        ppi_pa = value;                      /* Mode 0 latch */
        ppi_pb = value;                      /* PA/PB bus wired together */
    } else {
        cmd_raised_interrupt = false;
        write8748_command(value);
        ibfa = !cmd_raised_interrupt;
        obf_low = true;
    }
}

static void ppi_write_control(u8 value)
{
    if (value & 0x80) {                       /* Mode Set */
        ppi_pa = ppi_pb = ppi_pc = 0;
        ibfa = obf_low = false;

        if (value & 0x60) {                   /* Mode 2: PA bidirectional */
            mode2_active = true;
            pc_upper_input = false;
            walk_queue[0] = 0xAA; walk_queue[1] = 0x55;
            walk_head = 0; walk_count = 2;
            ibfa = true;                      /* first value ready */
        } else {                              /* Mode 0 */
            mode2_active = false;
            walk_count = 0;
            pc_upper_input = (value & 0x08) != 0;
        }
    } else {                                  /* Bit Set/Reset on Port C */
        int bit = (value >> 1) & 0x07;
        if (value & 0x01) ppi_pc |= (u8)(1 << bit);
        else              ppi_pc &= (u8)~(1 << bit);
    }
}

/* ===========================================================================
 *  NEC765 status registers
 * ===========================================================================*/
static void write_dor(u8 value)
{
    bool w_was = (dor_reg & DOR_WDATA) != 0;
    bool w_now = (value & DOR_WDATA) != 0;
    if (cpu_cyc() > 0) {
        if (w_now && !w_was) wdata_rise_cycle = cpu_cyc() + CURRENT_SENSE_RISE;
        else if (!w_now)     wdata_rise_cycle = INT64_MAX;
    }
    dor_reg = value;
    for (int i = 0; i < DRIVE_COUNT; i++)
        fd[i].motor_on = ((value >> (4 + i)) & 1) != 0;
    index_counter = 0;
}

static u8 read_srb(void)
{
    u8 srb = 0;
    int sel = selected_drive_index();

    if (sel == 0 && fd[0].loaded && !drive_ready(0)) srb |= 0x01;
    if (DRIVE_COUNT <= 1 || !fd[1].present) srb |= 0x02;
    if (DRIVE_COUNT <= 2 || !fd[2].present) srb |= 0x04;
    if ((dor_reg & DOR_WDATA) && (cpu_cyc() == 0 || cpu_cyc() >= wdata_rise_cycle)) srb |= 0x08;
    if (sel >= 0 && !fd[sel].motor_on) srb |= 0x40;

    int idx = (sel >= 0) ? sel : first_spinning_loaded();
    index_counter++;
    if (idx >= 0 && fd[idx].loaded && fd[idx].motor_on &&
        (index_counter % INDEX_PULSE_PERIOD) < INDEX_PULSE_WIDTH)
        srb |= 0x80;

    srb |= 0x01;   /* +24 sense line always high */
    return srb;
}

static u8 read_msr(void)
{
    u8 msr = 0;
    switch (phase) {
        case PH_IDLE:      msr = MSR_RQM; break;
        case PH_COMMAND:   msr = MSR_RQM | MSR_CB; break;
        case PH_EXECUTION: msr = drq_pending ? (u8)(MSR_RQM | MSR_CB) : MSR_CB; break;
        case PH_RESULT:    msr = MSR_RQM | MSR_DIO | MSR_CB; break;
    }
    for (int i = 0; i < DRIVE_COUNT; i++)
        if (drive_busy[i]) msr |= (u8)(1 << i);
    return msr;
}

/* ===========================================================================
 *  Sector I/O (per-track geometry)
 * ===========================================================================*/
static TrackInfo *track_at(FdcDrive *d, int cyl, int head)
{
    if (!d->tracks || d->heads <= 0) return NULL;
    int i = cyl * d->heads + head;
    if (i < 0 || i >= d->n_tracks) return NULL;
    TrackInfo *t = &d->tracks[i];
    return (t->sec_map != NULL) ? t : NULL;
}
static int sector_size_at(FdcDrive *d, int cyl, int head)
{
    TrackInfo *t = track_at(d, cyl, head);
    return (t && t->sector_size > 0) ? t->sector_size : d->sector_size;
}
static int sectors_at(FdcDrive *d, int cyl, int head)
{
    TrackInfo *t = track_at(d, cyl, head);
    return (t && t->sec_count > 0) ? t->sec_count : d->sectors;
}
static int sector_offset(FdcDrive *d, int cyl, int head, int sector)
{
    TrackInfo *t = track_at(d, cyl, head);
    if (t) {
        for (int i = 0; i < t->sec_count; i++)
            if (t->sec_map[i] == sector) return t->data_offset + i * t->sector_size;
        return -1;
    }
    if (sector < 1 || sector > d->sectors) return -1;
    return ((cyl * d->heads + head) * d->sectors + (sector - 1)) * d->sector_size;
}
static bool sector_is_deleted(FdcDrive *d, int cyl, int head, int sector)
{
    TrackInfo *t = track_at(d, cyl, head);
    if (!t || !t->deleted) return false;
    for (int i = 0; i < t->sec_count; i++)
        if (t->sec_map[i] == sector)
            return (i < t->sec_count) ? (t->deleted[i] != 0) : false;
    return false;
}
static void sector_set_deleted(FdcDrive *d, int cyl, int head, int sector, bool val)
{
    TrackInfo *t = track_at(d, cyl, head);
    if (!t) return;
    if (!t->deleted) t->deleted = (u8 *)calloc((size_t)t->sec_count, 1);
    if (!t->deleted) return;
    for (int i = 0; i < t->sec_count; i++)
        if (t->sec_map[i] == sector) { t->deleted[i] = val ? 1 : 0; return; }
}
static void drive_read_sector(FdcDrive *d, int cyl, int head, int sec,
                              u8 *buf, int pos, int buflen)
{
    if (!d->data) return;
    int size = sector_size_at(d, cyl, head);
    int off = sector_offset(d, cyl, head, sec);
    if (off < 0) return;
    int n = size;
    if (n > buflen - pos) n = buflen - pos;
    if (off + n > d->data_len) n = d->data_len - off;
    if (n > 0) memcpy(buf + pos, d->data + off, (size_t)n);
}
static void drive_write_sector(FdcDrive *d, int cyl, int head, int sec,
                               const u8 *buf, int pos, int buflen)
{
    if (d->write_protected || !d->data) return;
    int size = sector_size_at(d, cyl, head);
    int off = sector_offset(d, cyl, head, sec);
    if (off < 0) return;
    int n = size;
    if (n > buflen - pos) n = buflen - pos;
    if (off + n > d->data_len) n = d->data_len - off;
    if (n > 0) memcpy(d->data + off, buf + pos, (size_t)n);
}

/* ===========================================================================
 *  Disk-image loading (.IMD + raw)
 * ===========================================================================*/
static void free_drive_image(FdcDrive *d)
{
    if (d->tracks) {
        for (int i = 0; i < d->n_tracks; i++) {
            free(d->tracks[i].sec_map);
            free(d->tracks[i].deleted);
        }
        free(d->tracks);
    }
    free(d->data);
    d->tracks = NULL; d->n_tracks = 0;
    d->data = NULL;   d->data_len = 0;
}

#define MAX_IMD_RECS 512
typedef struct {
    int cyl, head, sector_size, nsec;
    const u8 *sec_map_src;   /* into raw */
    u8 *payload;
    int payload_len;
} ImdRec;

static bool load_imd(FdcDrive *d, const u8 *raw, long rawlen)
{
    long pos = 0;
    while (pos < rawlen && raw[pos] != 0x1A) pos++;
    if (pos < rawlen) pos++;                       /* skip the 0x1A */

    ImdRec *recs = (ImdRec *)calloc(MAX_IMD_RECS, sizeof(ImdRec));
    if (!recs) return false;
    int nrecs = 0, maxCyl = 0, maxHead = 0, maxSec = 0;

    while (pos + 5 <= rawlen && nrecs < MAX_IMD_RECS) {
        ImdRec *r = &recs[nrecs];
        r->cyl  = raw[pos + 1];
        r->head = raw[pos + 2] & 0x3F;
        int nsec = raw[pos + 3];
        int ssC  = raw[pos + 4];
        r->sector_size = (ssC < 7) ? (128 << ssC) : 8192;
        bool has_cyl = (raw[pos + 2] & 0x80) != 0;
        bool has_hd  = (raw[pos + 2] & 0x40) != 0;
        pos += 5;

        if (pos + nsec > rawlen) break;
        r->nsec = nsec;
        r->sec_map_src = raw + pos;
        pos += nsec;
        if (has_cyl) pos += nsec;                  /* optional cylinder map */
        if (has_hd)  pos += nsec;                  /* optional head map */

        r->payload_len = nsec * r->sector_size;
        r->payload = (u8 *)calloc(1, r->payload_len ? (size_t)r->payload_len : 1);
        if (!r->payload) break;
        for (int s = 0; s < nsec; s++) {
            if (pos >= rawlen) break;
            u8 stype = raw[pos++];
            int dst = s * r->sector_size;
            if (stype == 0) continue;              /* unavailable -> zeros */
            bool compressed = (stype & 1) == 0;    /* even types are compressed */
            if (compressed) {
                u8 fill = (pos < rawlen) ? raw[pos++] : 0;
                memset(r->payload + dst, fill, (size_t)r->sector_size);
            } else {
                int copy = r->sector_size;
                if (copy > rawlen - pos) copy = (int)(rawlen - pos);
                if (copy > 0) memcpy(r->payload + dst, raw + pos, (size_t)copy);
                pos += r->sector_size;
            }
        }

        if (r->cyl  + 1 > maxCyl)  maxCyl  = r->cyl + 1;
        if (r->head + 1 > maxHead) maxHead = r->head + 1;
        if (nsec > maxSec) maxSec = nsec;
        nrecs++;
    }

    d->cylinders   = maxCyl  > 0 ? maxCyl  : 77;
    d->heads       = maxHead > 0 ? maxHead : 1;
    d->sectors     = maxSec  > 0 ? maxSec  : 26;
    d->sector_size = nrecs   > 0 ? recs[0].sector_size : 128;

    d->n_tracks = d->cylinders * d->heads;
    d->tracks   = (TrackInfo *)calloc((size_t)d->n_tracks, sizeof(TrackInfo));
    long total = 0;
    for (int i = 0; i < nrecs; i++) total += recs[i].payload_len;
    d->data_len = (int)total;
    d->data     = (u8 *)calloc(1, total ? (size_t)total : 1);
    if (!d->tracks || !d->data) {
        for (int i = 0; i < nrecs; i++) free(recs[i].payload);
        free(recs);
        return false;
    }

    int cursor = 0;
    for (int i = 0; i < nrecs; i++) {
        ImdRec *r = &recs[i];
        int idx = r->cyl * d->heads + r->head;
        if (idx < 0 || idx >= d->n_tracks) { cursor += r->payload_len; free(r->payload); continue; }
        TrackInfo *t = &d->tracks[idx];
        free(t->sec_map);                          /* in case of duplicate track */
        t->sector_size = r->sector_size;
        t->sec_count   = r->nsec;
        t->sec_map     = (u8 *)malloc(r->nsec ? (size_t)r->nsec : 1);
        if (t->sec_map) memcpy(t->sec_map, r->sec_map_src, (size_t)r->nsec);
        t->deleted     = NULL;
        t->data_offset = cursor;
        memcpy(d->data + cursor, r->payload, (size_t)r->payload_len);
        cursor += r->payload_len;
        free(r->payload);
    }
    free(recs);
    return true;
}

static bool load_raw(FdcDrive *d, const u8 *raw, long rawlen)
{
    d->cylinders   = 77;
    d->heads       = 1;
    d->sectors     = 26;
    d->sector_size = 128;
    d->tracks = NULL; d->n_tracks = 0;
    int image = d->cylinders * d->heads * d->sectors * d->sector_size;
    int len = (rawlen > image) ? (int)rawlen : image;
    d->data = (u8 *)calloc(1, (size_t)len);
    if (!d->data) return false;
    d->data_len = len;
    memcpy(d->data, raw, (size_t)((rawlen < len) ? rawlen : len));
    return true;
}

static bool has_imd_ext(const char *path)
{
    size_t n = strlen(path);
    if (n < 4) return false;
    const char *e = path + n - 4;
    return (e[0] == '.' &&
            (e[1] == 'i' || e[1] == 'I') &&
            (e[2] == 'm' || e[2] == 'M') &&
            (e[3] == 'd' || e[3] == 'D'));
}

bool fdc_load_image(int drive, const char *path, bool write_protected)
{
    if ((unsigned)drive >= DRIVE_COUNT || !path || !*path) return false;

    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return false; }
    u8 *raw = (u8 *)malloc((size_t)len);
    if (!raw) { fclose(f); return false; }
    size_t got = fread(raw, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(raw); return false; }

    free_drive_image(&fd[drive]);
    bool ok = has_imd_ext(path) ? load_imd(&fd[drive], raw, len)
                                : load_raw(&fd[drive], raw, len);
    free(raw);
    if (!ok) return false;

    fd[drive].current_cylinder = 0;
    fdc_set_disk(drive, true, write_protected);   /* marks loaded + block status */
    FTRACE("loaded drv=%d cyls=%d heads=%d sectors=%d ssize=%d bytes=%d %s",
           drive, fd[drive].cylinders, fd[drive].heads, fd[drive].sectors,
           fd[drive].sector_size, fd[drive].data_len,
           fd[drive].tracks ? "(per-track)" : "(uniform)");
    return true;
}

/* ===========================================================================
 *  NEC765 command engine
 * ===========================================================================*/
static u8 ntocode(int bytes)
{
    if (bytes <= 128)  return 0;
    if (bytes <= 256)  return 1;
    if (bytes <= 512)  return 2;
    if (bytes <= 1024) return 3;
    return 4;
}
static bool motors_running(void) { return (dor_reg & 0xF0) != 0; }

static int physical_drive(int us_bits)
{
    if ((unsigned)active_drive < DRIVE_COUNT) return active_drive;
    int sel = selected_drive_index();
    return (sel >= 0) ? sel : (us_bits & 0x03);
}
static bool drive_ready765(int drv)
{
    return (unsigned)drv < DRIVE_COUNT && fd[drv].loaded
        && motors_running() && selected_drive_index() >= 0;
}

/* ── result helpers ─────────────────────────────────────────────────────────*/
static void set_result_n(int n) { res_len = n; res_idx = 0; phase = PH_RESULT; }
static void set_result1(u8 b0)  { res_buf[0] = b0; set_result_n(1); }
static void build_result7(u8 st0, u8 st1, u8 st2, u8 c, u8 h, u8 r, u8 n)
{
    res_buf[0] = st0; res_buf[1] = st1; res_buf[2] = st2;
    res_buf[3] = c; res_buf[4] = h; res_buf[5] = r; res_buf[6] = n;
    set_result_n(7);
}
static void build_not_ready(int drv, int head)
{
    (void)drv; (void)head;
    /* System/23: not-ready travels over Port B bit5, NOT a 765 result phase —
     * leaving DIO=1 would wedge the not-ready ISR's "wait for DIO=0" loop. */
    phase = PH_IDLE;
    res_idx = res_len = 0;
    FTRACE("not-ready drv=%d (loaded=%d motors=%d sel=%d)",
           drv, (unsigned)drv < DRIVE_COUNT ? fd[drv].loaded : 0,
           motors_running(), selected_drive_index());
    fdc_raise_interrupt(0x20);
}
static void build_write_protect(int drv, int head)
{
    build_result7((u8)(ST0_IC_ABNORM | (head << 2) | drv), ST1_NW, 0, 0, 0, 0, 0);
    fdc_raise_interrupt(0x80);
}

/* ── forward decls for the DMA callbacks ────────────────────────────────────*/
static u8   dma_provide_byte(int ch);
static void dma_receive_byte(int ch, u8 data);
static void dma_terminal_count(int ch);
static void on_dma_channel_programmed(int ch, bool is_count);
static void fire_drq(void);

/* ── SPECIFY ($03) ──────────────────────────────────────────────────────────*/
static void exec_specify(void)
{
    spec_srh = cmd_buf[1];
    spec_hln = cmd_buf[2];
    phase = PH_IDLE;
}

/* ── SENSE DRIVE STATUS ($04) ───────────────────────────────────────────────*/
static void exec_sense_drive_status(void)
{
    int drv  = physical_drive(cmd_buf[1] & 0x03);
    int head = (cmd_buf[1] >> 2) & 0x01;
    u8 st3 = (u8)(drv | (head << 2));
    if ((unsigned)drv < DRIVE_COUNT) {
        FdcDrive *d = &fd[drv];
        if (d->heads > 1)                    st3 |= 0x08;   /* TS  */
        if (d->current_cylinder == 0)        st3 |= 0x10;   /* T0  */
        if (drive_ready(drv))                st3 |= 0x20;   /* RDY */
        if (d->write_protected)              st3 |= 0x40;   /* WP  */
    }
    set_result1(st3);
}

/* ── CompleteSeek: queue SENSE-INT result + INTRQ ───────────────────────────*/
static void complete_seek(int drv, int ncn)
{
    drive_busy[drv] = false;
    u8 st0;
    if ((unsigned)drv < DRIVE_COUNT && !drive_ready(drv))
        st0 = (u8)(drv | ST0_IC_ABNORM | ST0_SE | ST0_EC);
    else
        st0 = (u8)(drv | ST0_SE);

    if (sint_count < SINT_QUEUE) {
        sint_st0[sint_count] = st0;
        sint_pcn[sint_count] = (u8)(ncn > 255 ? 255 : ncn);
        sint_count++;
    }
    fdc_raise_interrupt(drive_ready(drv) ? 0x10 : 0x20);
}

/* ── RECALIBRATE ($07) ──────────────────────────────────────────────────────*/
static void exec_recalibrate(void)
{
    int drv = physical_drive(cmd_buf[1] & 0x03);
    if ((unsigned)drv < DRIVE_COUNT) {
        drive_busy[drv] = true;
        seek_active[drv] = false;
        fd[drv].current_cylinder = 0;
        complete_seek(drv, 0);
    }
    phase = PH_IDLE;
}

/* ── SEEK ($0F) ─────────────────────────────────────────────────────────────*/
static void exec_seek(void)
{
    int drv = physical_drive(cmd_buf[1] & 0x03);
    int ncn = cmd_buf[2];
    if ((unsigned)drv < DRIVE_COUNT) {
        drive_busy[drv] = true;
        seek_active[drv] = false;
        fd[drv].current_cylinder = (ncn < 255) ? ncn : 255;
        complete_seek(drv, ncn);
    }
    phase = PH_IDLE;
}

/* ── SENSE INTERRUPT STATUS ($08) ───────────────────────────────────────────*/
static void exec_sense_interrupt(void)
{
    if (sint_count > 0) {
        res_buf[0] = sint_st0[0];
        res_buf[1] = sint_pcn[0];
        for (int i = 1; i < sint_count; i++) {
            sint_st0[i - 1] = sint_st0[i];
            sint_pcn[i - 1] = sint_pcn[i];
        }
        sint_count--;
    } else {
        res_buf[0] = ST0_IC_INVALID;
        res_buf[1] = 0;
    }
    set_result_n(2);
}

/* ── READ ID ($0A) ──────────────────────────────────────────────────────────*/
static void exec_read_id(void)
{
    int drv  = physical_drive(cmd_buf[1] & 0x03);
    int head = (cmd_buf[1] >> 2) & 0x01;
    if (!drive_ready765(drv)) { build_not_ready(drv, head); return; }

    FdcDrive *d = &fd[drv];
    build_result7((u8)(drv | (head << 2)), 0, 0,
                  (u8)d->current_cylinder, (u8)head,
                  1, ntocode(sector_size_at(d, d->current_cylinder, head)));
    fdc_raise_interrupt(0x80);
}

/* ── READ/WRITE DATA ($06/$05, $0C/$09) ─────────────────────────────────────*/
static void exec_transfer(bool write, bool deleted)
{
    int drv    = physical_drive(cmd_buf[1] & 0x03);
    int head   = (cmd_buf[1] >> 2) & 0x01;
    int cyl    = cmd_buf[2];
    int hd     = cmd_buf[3];
    int sector = cmd_buf[4];
    int eot    = cmd_buf[6];

    if (!drive_ready765(drv)) { build_not_ready(drv, head); return; }

    FdcDrive *d = &fd[drv];
    if (write && d->write_protected) { build_write_protect(drv, head); return; }

    /* Cylinder mismatch? */
    if (cyl != d->current_cylinder) {
        build_result7((u8)(ST0_IC_ABNORM | (head << 2) | drv),
                      ST1_ND, ST2_WC, (u8)cyl, (u8)hd, (u8)sector, cmd_buf[5]);
        fdc_raise_interrupt(0x80);
        return;
    }

    /* Sector out of range? (per-track bounds) */
    int track_sectors = sectors_at(d, cyl, head);
    if (sector < 1 || sector > track_sectors || eot > track_sectors) {
        build_result7((u8)(ST0_IC_ABNORM | (head << 2) | drv),
                      ST1_ND, 0, (u8)cyl, (u8)hd, (u8)sector, cmd_buf[5]);
        fdc_raise_interrupt(0x80);
        return;
    }

    /* Transfer context — use the PHYSICAL track sector size. */
    x_drive = drv; x_head = head; x_cyl = cyl; x_sector = sector; x_eot = eot;
    x_sector_size = sector_size_at(d, cyl, head);
    if (x_sector_size > MAX_XFER_BYTES) x_sector_size = MAX_XFER_BYTES;
    x_write = write; x_deleted = deleted; x_start_sector = sector;
    x_buf_pos = 0;
    phase = PH_EXECUTION;

    /* Wire the I8257 DMA callbacks. */
    if (write) {
        dma_periph_write[DMA_CHANNEL] = dma_receive_byte;
        dma_periph_read[DMA_CHANNEL]  = NULL;
    } else {
        drive_read_sector(d, cyl, head, sector, x_buf, 0, MAX_XFER_BYTES);  /* pre-load */
        dma_periph_read[DMA_CHANNEL]  = dma_provide_byte;
        dma_periph_write[DMA_CHANNEL] = NULL;
    }
    dma_on_tc = dma_terminal_count;
    dma_on_channel_programmed = on_dma_channel_programmed;

    /* Two DMA-ordering conventions (see FloppyController.cs): if the channel is
     * already loaded (count non-zero) fire immediately; otherwise arm and wait
     * for the host to program it (OnChannelProgrammed). */
    if ((dma_cnt[DMA_CHANNEL] & 0x3FFF) != 0) {
        drq_pending = true;
        fire_drq();
    } else {
        dma_reset_byte_pointer();
        arm_addr_written = false;
        arm_cnt_written  = false;
        drq_pending = true;
    }
}

/* ── DMA callbacks ──────────────────────────────────────────────────────────*/
static u8 dma_provide_byte(int ch)
{
    (void)ch;
    if (x_buf_pos >= x_sector_size) {
        x_sector++;
        if (x_sector > x_eot || x_sector > sectors_at(&fd[x_drive], x_cyl, x_head))
            return 0xFF;
        drive_read_sector(&fd[x_drive], x_cyl, x_head, x_sector, x_buf, 0, MAX_XFER_BYTES);
        x_buf_pos = 0;
    }
    return x_buf[x_buf_pos++];
}

static void dma_receive_byte(int ch, u8 data)
{
    (void)ch;
    if (x_buf_pos < MAX_XFER_BYTES) x_buf[x_buf_pos++] = data;
    if (x_buf_pos >= x_sector_size) {
        drive_write_sector(&fd[x_drive], x_cyl, x_head, x_sector, x_buf, 0, MAX_XFER_BYTES);
        sector_set_deleted(&fd[x_drive], x_cyl, x_head, x_sector, x_deleted);
        x_sector++;
        x_buf_pos = 0;
    }
}

static void stream_next(void)
{
    stream_rearm_addr = false;
    stream_rearm_cnt  = false;
    if (!stream_active) return;

    FdcDrive *d = &fd[x_drive];
    if (stream_next_sector > x_eot || stream_next_sector > sectors_at(d, x_cyl, x_head)) {
        stream_active = false;
        return;
    }

    u16 dst  = dma_addr[DMA_CHANNEL];
    int prog = dma_cnt[DMA_CHANNEL] & 0x3FFF;
    int n = (prog == 0 || prog > x_sector_size) ? x_sector_size : prog;

    drive_read_sector(d, x_cyl, x_head, stream_next_sector, stream_buf, 0, MAX_XFER_BYTES);
    for (int i = 0; i < n; i++) memory_dma_write((u16)(dst + i), stream_buf[i]);

    dma_addr[DMA_CHANNEL] = (u16)(dst + n);
    dma_cnt[DMA_CHANNEL] &= 0xC000;

    stream_next_sector++;
    if (stream_next_sector > x_eot) stream_active = false;
}

static void dma_terminal_count(int ch)
{
    (void)ch;

    if (x_write && x_buf_pos > 0) {           /* flush partial write sector */
        for (int i = x_buf_pos; i < x_sector_size; i++) x_buf[i] = 0;
        drive_write_sector(&fd[x_drive], x_cyl, x_head, x_sector, x_buf, 0, MAX_XFER_BYTES);
        sector_set_deleted(&fd[x_drive], x_cyl, x_head, x_sector, x_deleted);
    }

    dma_assert_drq(DMA_CHANNEL, false);

    /* Result-phase C/H/R advance (uPD765A leaves them at the NEXT sector). */
    int last_done = x_write
        ? (x_buf_pos > 0 ? x_sector : x_sector - 1)
        : (x_buf_pos >= x_sector_size ? x_sector : x_sector - 1);
    int rC = x_cyl, rH = x_head, rR = x_sector;
    if (last_done >= 1) {
        if (last_done >= x_eot) { rR = 1; rC = x_cyl + 1; }
        else                    { rR = last_done + 1; }
    }

    bool sec_deleted = sector_is_deleted(&fd[x_drive], x_cyl, x_head, x_start_sector);
    u8 st2 = (sec_deleted != x_deleted) ? ST2_CM : 0;
    build_result7((u8)(x_drive | (x_head << 2)), 0, st2,
                  (u8)rC, (u8)rH, (u8)rR, ntocode(x_sector_size));
    phase = PH_RESULT;
    FTRACE("TC done drv=%d C=%d H=%d startR=%d -> resC=%d resR=%d st2=%02X",
           x_drive, x_cyl, x_head, x_start_sector, rC, rR, st2);
    fdc_raise_interrupt(0x80);

    /* Multi-sector software-paced streaming. */
    if (!x_write && last_done >= 1 && last_done < x_eot) {
        stream_active = true;
        stream_next_sector = last_done + 1;
        stream_rearm_addr = false;
        stream_rearm_cnt  = false;
    } else {
        stream_active = false;
    }
}

static void on_dma_channel_programmed(int ch, bool is_count)
{
    if (ch != DMA_CHANNEL) return;

    if (drq_pending && phase == PH_EXECUTION) {
        if (is_count) arm_cnt_written = true;
        else          arm_addr_written = true;
        if (arm_addr_written && arm_cnt_written) fire_drq();
        return;
    }

    if (stream_active) {
        if ((dma_cnt[DMA_CHANNEL] & 0xC000) != 0x4000) { stream_active = false; return; }
        if (is_count) stream_rearm_cnt = true;
        else          stream_rearm_addr = true;
        if (stream_rearm_addr && stream_rearm_cnt) stream_next();
    }
}

static void fire_drq(void)
{
    drq_pending = false;

    int sectors_to_move = (x_eot >= x_sector) ? (x_eot - x_sector + 1) : 1;
    int xfer_bytes = sectors_to_move * x_sector_size;
    int dma_mode = dma_cnt[DMA_CHANNEL] & 0xC000;
    int dma_prog = dma_cnt[DMA_CHANNEL] & 0x3FFF;
    int dma_use  = (dma_prog == 0) ? xfer_bytes
                                   : (dma_prog < xfer_bytes ? dma_prog : xfer_bytes);
    dma_cnt[DMA_CHANNEL] = (u16)(dma_mode | (dma_use & 0x3FFF));

    FTRACE("fire_drq dst=%04X cnt=%04X mode=%04X use=%d xfer=%d",
           dma_addr[DMA_CHANNEL], dma_cnt[DMA_CHANNEL], dma_mode, dma_use, xfer_bytes);

    dma_assert_drq(DMA_CHANNEL, true);

    /* Restore any remaining programmed count for one-programming multi-read. */
    int dma_remain = dma_prog - dma_use;
    if (dma_remain > 0)
        dma_cnt[DMA_CHANNEL] = (u16)(dma_mode | (dma_remain & 0x3FFF));
}

/* ── stubbed commands (not needed to boot) ──────────────────────────────────*/
static void exec_format_track(void)
{
    int drv  = physical_drive(cmd_buf[1] & 0x03);
    int head = (cmd_buf[1] >> 2) & 0x01;
    if (!drive_ready765(drv)) { build_not_ready(drv, head); return; }
    if (fd[drv].write_protected) { build_write_protect(drv, head); return; }
    /* TODO: real track format.  Report a normal completion for now. */
    build_result7((u8)(drv | (head << 2)), 0, 0,
                  (u8)fd[drv].current_cylinder, (u8)head, 1, cmd_buf[2] & 0x07);
    fdc_raise_interrupt(0x80);
}
static void exec_scan(int scan_type)
{
    (void)scan_type;
    int drv  = physical_drive(cmd_buf[1] & 0x03);
    int head = (cmd_buf[1] >> 2) & 0x01;
    if (!drive_ready765(drv)) { build_not_ready(drv, head); return; }
    /* TODO: real scan.  Report scan-not-satisfied for now. */
    build_result7((u8)(drv | (head << 2)), 0, ST2_SN,
                  (u8)fd[drv].current_cylinder, (u8)head, cmd_buf[4], cmd_buf[5]);
    fdc_raise_interrupt(0x80);
}

/* ── command parsing ────────────────────────────────────────────────────────*/
static void dispatch_command(void)
{
    FTRACE("CMD %02X  b1=%02X C=%d H=%d R=%d N=%d EOT=%d",
           cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3],
           cmd_buf[4], cmd_buf[5], cmd_buf[6]);
    switch (cmd_buf[0] & 0x1F) {
        case CMD_SPECIFY:       exec_specify(); break;
        case CMD_SENSE_DRV:     exec_sense_drive_status(); break;
        case CMD_WRITE_DATA:    exec_transfer(true,  false); break;
        case CMD_READ_DATA:     exec_transfer(false, false); break;
        case CMD_RECALIBRATE:   exec_recalibrate(); break;
        case CMD_SENSE_INT:     exec_sense_interrupt(); break;
        case CMD_WRITE_DELETED: exec_transfer(true,  true); break;
        case CMD_READ_ID:       exec_read_id(); break;
        case CMD_READ_DELETED:  exec_transfer(false, true); break;
        case CMD_FORMAT:        exec_format_track(); break;
        case CMD_SEEK:          exec_seek(); break;
        case CMD_SCAN_EQ:       exec_scan(0); break;
        case CMD_SCAN_LE:       exec_scan(1); break;
        case CMD_SCAN_HE:       exec_scan(2); break;
        default:                set_result1(ST0_IC_INVALID); break;
    }
}

static void start_command(u8 first)
{
    drq_pending   = false;
    stream_active = false;
    cmd_buf[0] = first;
    cmd_idx = 1;
    phase = PH_COMMAND;

    switch (first & 0x1F) {
        case CMD_SPECIFY:       cmd_len = 3; break;
        case CMD_SENSE_DRV:     cmd_len = 2; break;
        case CMD_WRITE_DATA:    cmd_len = 9; break;
        case CMD_READ_DATA:     cmd_len = 9; break;
        case CMD_RECALIBRATE:   cmd_len = 2; break;
        case CMD_SENSE_INT:     cmd_len = 1; break;
        case CMD_WRITE_DELETED: cmd_len = 9; break;
        case CMD_READ_ID:       cmd_len = 2; break;
        case CMD_READ_DELETED:  cmd_len = 9; break;
        case CMD_FORMAT:        cmd_len = 6; break;
        case CMD_SEEK:          cmd_len = 3; break;
        case CMD_SCAN_EQ:       cmd_len = 9; break;
        case CMD_SCAN_LE:       cmd_len = 9; break;
        case CMD_SCAN_HE:       cmd_len = 9; break;
        default:                set_result1(ST0_IC_INVALID); return;
    }
    if (cmd_len == 1) dispatch_command();
}

static u8 read_fifo(void)
{
    if (phase != PH_RESULT) return 0xFF;
    u8 val = res_buf[res_idx++];
    if (res_idx >= res_len) { res_idx = 0; res_len = 0; phase = PH_IDLE; }
    return val;
}

static void write_fifo(u8 value)
{
    switch (phase) {
        case PH_IDLE: start_command(value); break;
        case PH_COMMAND:
            cmd_buf[cmd_idx++] = value;
            if (cmd_idx >= cmd_len) dispatch_command();
            break;
        default: break;   /* Execution/Result: ignore padding bytes */
    }
}

/* ── NEC765 hardware reset (queues one SENSE-INT result per drive) ──────────*/
static void reset_fdc(void)
{
    phase = PH_IDLE;
    drq_pending = false;
    cmd_idx = cmd_len = 0;
    res_idx = res_len = 0;
    sint_count = 0;
    for (int i = 0; i < DRIVE_COUNT; i++) drive_busy[i] = false;

    for (int i = 0; i < DRIVE_COUNT && sint_count < DRIVE_COUNT; i++) {
        sint_st0[sint_count] = (u8)(0xC0 | i);   /* IC=11: ready-change */
        sint_pcn[sint_count] = 0;
        sint_count++;
    }
    fdc_raise_interrupt(0x80);
}

/* ===========================================================================
 *  Port dispatch (0xF0-0xFB)
 * ===========================================================================*/
u8 fdc_read_port(u8 port)
{
    switch (port) {
        case 0xF1: return read_srb();
        case 0xF4: return read_msr();
        case 0xF5: return read_fifo();
        case 0xF8: return ppi_read_port_a();
        case 0xF9: return ppi_read_port_b();
        case 0xFA: return ppi_read_port_c();
        case 0xFB: return 0xFF;              /* control word: write-only */
        default:   return 0xFF;
    }
}

void fdc_write_port(u8 port, u8 value)
{
    switch (port) {
        case 0xF0: write_dor(value); break;
        case 0xF4: break;                    /* MSR read-only */
        case 0xF5: write_fifo(value); break;
        case 0xF8: ppi_write_port_a(value); break;
        case 0xF9: break;                    /* Port B is input */
        case 0xFA: ppi_pc = value; break;
        case 0xFB: ppi_write_control(value); break;
        default: break;
    }
}

/* ===========================================================================
 *  Reset / disk sync
 * ===========================================================================*/
void fdc_set_disk(int drive, bool loaded, bool write_protected)
{
    if ((unsigned)drive >= DRIVE_COUNT) return;
    fd[drive].loaded = loaded;
    fd[drive].write_protected = write_protected;
    int block = 0x20 + drive * 8;
    if (loaded) ram8748[block] = 0x04; else ram8748[block] &= (u8)~0x04;
}

void fdc_reset(void)
{
    dor_reg = 0;
    index_counter = 0;
    status8748 = 0;

    ppi_pa = ppi_pb = ppi_pc = 0;
    mode2_active = false;
    pc_upper_input = true;
    ibfa = obf_low = intr_b = false;
    walk_count = walk_head = 0;

    memset(ram8748, 0, sizeof ram8748);
    ram8748[R8748_DRV_STATUS] = 0x0F;   /* all 4 drives present */
    ram8748[R8748_CTRL_REG]   = 0xC0;
    ram8748[R8748_INIT_2C]    = 0x2C;

    for (int d = 0; d < DRIVE_COUNT; d++) {
        fd[d].present = true;
        if (fd[d].cylinders <= 0)   fd[d].cylinders = 77;
        if (fd[d].heads <= 0)       fd[d].heads = 1;
        if (fd[d].sectors <= 0)     fd[d].sectors = 26;
        if (fd[d].sector_size <= 0) fd[d].sector_size = 128;
        fd[d].motor_on = false;
        seek_active[d] = false;
        wc_settle_cycle[d] = 0;
        int block = 0x20 + d * 8;
        if (fd[d].loaded) ram8748[block] = 0x04;
    }
    pending8748 = P8748_NONE;
    pending_arg = 0;
    r4_8748 = 0;
    active_drive = -1;

    wdata_rise_cycle = INT64_MAX;

    /* NEC765 command engine reset (queues the post-reset SENSE-INT results). */
    reset_fdc();
}
