/* ===========================================================================
 *  memory.c — banked memory subsystem (faithful port of Memory.cs)
 * ===========================================================================*/
#include "memory.h"
#include "cpu8085.h"     /* cpu.pc, for the debug write-watch */

#include <stdio.h>
#include <string.h>

/* Debug: when nonzero, log every CPU write to this address with the PC. */
u16 dbg_memwatch = 0;

u8  mem_rom[MEM_SIZE];
u8  mem_ram[MEM_SIZE];

u32 mem_dma_page       = 0;
u32 mem_rom_page       = 0;
u32 mem_ram_page_write = 0;
u32 mem_ram_page_read  = 0;

/* ----------------------------------------------------------------------------
 *  CPU-visible reads/writes
 * --------------------------------------------------------------------------*/
u8 memory_read(u16 addr)
{
    if ((addr & 0x8000) == 0) {
        if ((addr & 0x4000) == 0)
            return mem_rom[addr];                        /* fixed ROM         */
        if ((mem_rom_page & 0x0F) <= 0x0F)
            return mem_rom[(int)addr + (int)((mem_rom_page & 0x0F) * 0x4000)];
        return 0xFF;
    } else {
        if ((addr & 0x7FFF) < 0x4000)
            return mem_ram[addr & 0x7FFF];               /* fixed RAM (low)   */
        return mem_ram[(int)(addr & 0x3FFF) +
                       (int)(((mem_ram_page_read & 0x0F) + 1) * 0x4000)];
    }
}

u16 memory_read16(u16 addr)
{
    u16 lo = memory_read(addr);
    u16 hi = (u16)(memory_read((u16)(addr + 1)) << 8);
    return (u16)(hi | lo);
}

bool memory_write(u16 addr, u8 value)
{
    if (dbg_memwatch && addr == dbg_memwatch)
        fprintf(stderr, "[MEMW] %04X <- %02X  @PC~%04X\n", addr, value, cpu.pc);
    if ((addr & 0x8000) == 0x8000) {
        if ((addr & 0x7FFF) < 0x4000) {
            /* Legacy quirk preserved from the original: swallow a specific
             * diagnostic write so POST behaves as it did on real hardware. */
            if (addr == 0xA009 && value == 0x92)
                value = 0;
            mem_ram[addr & 0x3FFF] = value;
            return true;
        }
        mem_ram[(int)(addr & 0x3FFF) |
                (int)(((mem_ram_page_write & 0x0F) + 1) * 0x4000)] = value;
        return true;
    }
    return false;   /* writes into ROM space are ignored */
}

bool memory_write16(u16 addr, u16 value)
{
    bool res  = memory_write(addr, (u8)value);
    res      &= memory_write((u16)(addr + 1), (u8)(value >> 8));
    return res;
}

/* ----------------------------------------------------------------------------
 *  DMA-visible reads/writes (use mem_dma_page)
 * --------------------------------------------------------------------------*/
u8 memory_dma_read(u16 addr)
{
    if ((addr & 0x8000) == 0) {
        if ((addr & 0x4000) == 0)
            return mem_rom[addr];
        if ((mem_rom_page & 0x0F) <= 0x0F)
            return mem_rom[(int)addr + (int)((mem_dma_page & 0x0F) * 0x4000)];
        return 0xFF;
    } else {
        if ((addr & 0x7FFF) < 0x4000)
            return mem_ram[addr & 0x7FFF];
        return mem_ram[(int)(addr & 0x3FFF) +
                       (int)(((mem_dma_page & 0x0F) + 1) * 0x4000)];
    }
}

bool memory_dma_write(u16 addr, u8 value)
{
    if ((addr & 0x8000) == 0x8000) {
        if ((addr & 0x7FFF) < 0x4000) {
            mem_ram[addr & 0x3FFF] = value;
            return true;
        }
        mem_ram[(int)(addr & 0x3FFF) |
                (int)(((mem_dma_page & 0x0F) + 1) * 0x4000)] = value;
        return true;
    }
    return false;
}

/* ----------------------------------------------------------------------------
 *  Reset / ROM loading
 * --------------------------------------------------------------------------*/
void memory_reset(void)
{
    mem_dma_page = mem_rom_page = mem_ram_page_write = mem_ram_page_read = 0;
    memset(mem_ram, 0, sizeof mem_ram);   /* ROM image is firmware -> kept */
}

void memory_fill_bank(int loadaddr, int length, u8 value)
{
    if (loadaddr < 0) loadaddr = 0;
    for (int i = loadaddr; i < loadaddr + length && i < MEM_SIZE; i++)
        mem_rom[i] = value;
}

int memory_load_rom(const char *filename, int loadaddr)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
        return -1;
    if (loadaddr < 0 || loadaddr >= MEM_SIZE) {
        fclose(f);
        return -1;
    }
    size_t room = (size_t)(MEM_SIZE - loadaddr);
    size_t got  = fread(mem_rom + loadaddr, 1, room, f);
    (void)got;   /* a short read just means a smaller bank image; that's fine */
    fclose(f);
    return 0;
}

/* ----------------------------------------------------------------------------
 *  ROM set tables (bank layout per ROS revision).  Entry i is loaded at
 *  0x2000*i; an empty entry leaves that 8K bank filled with 0xFF.  The three
 *  sets share most images, so the folder is deduplicated onto shared filenames.
 * --------------------------------------------------------------------------*/
static const char *const rom_ros_1_05[] = {
    "02_61c9866a_4481186.bin", "09_07843020_8493747.bin",
    "0a_b9569153_8519402.bin", "0b_4f631183_8519404.bin",
    "0c_48646293_8519403.bin", "0d_bea5a812_8519405.bin",
    "",                        "",
    "10_41e6c232_8519411.bin", "11_b17f5c6e_8519407.bin",
    "12_04dcc52f_8519408.bin", "13_9a3f70c7_4481706.bin",   /* v1.05 */
    "14_91b2969e_8519406.bin", "15_0f9a99fa_8519416.bin",
    "16_e451a5e2_8519409.bin", "17_b9fb8bf1_8519410.bin",
    "18_22cb6de4_8519417.bin", "19_2c3af8f4_6091759.bin",   /* v1.05 */
};

static const char *const rom_ros_1_04[] = {
    "02_61c9866a_4481186.bin", "09_07843020_8493747.bin",
    "0a_b9569153_8519402.bin", "0b_4f631183_8519404.bin",
    "0c_48646293_8519403.bin", "0d_bea5a812_8519405.bin",
    "",                        "",
    "10_41e6c232_8519411.bin", "11_b17f5c6e_8519407.bin",
    "12_04dcc52f_8519408.bin", "13_9a3f70c7_4481706.bin",   /* == v1.05 */
    "14_91b2969e_8519406.bin", "15_0f9a99fa_8519416.bin",
    "16_e451a5e2_8519409.bin", "17_b9fb8bf1_8519410.bin",
    "18_22cb6de4_8519417.bin", "19_2e665945_4481711.bin",   /* v1.04 */
};

static const char *const rom_ros_1_01[] = {
    "02_765abd93_8493746.bin", "09_07843020_8493747.bin",
    "0a_b9569153_8519402.bin", "0b_4f631183_8519404.bin",
    "0c_48646293_8519403.bin", "0d_bea5a812_8519405.bin",
    "",                        "",
    "",                        "",
    "12_04dcc52f_8519408.bin", "13_26869666_8493761.bin",
    "14_91b2969e_8519406.bin", "15_269db39d_8493762.bin",
    "16_e451a5e2_8519409.bin", "17_b9fb8bf1_8519410.bin",
    "10_41e6c232_8519411.bin", "19_73aeeb56_8493727.bin",
};

#define ROM_BANKS 18

int memory_load_rom_set(RomVersion version, const char *roms_dir)
{
    const char *const *banks;
    switch (version) {
        case ROS_1_01: banks = rom_ros_1_01; break;
        case ROS_1_04: banks = rom_ros_1_04; break;
        case ROS_1_05: banks = rom_ros_1_05; break;
        default:       banks = rom_ros_1_05; break;
    }
    if (!roms_dir || !*roms_dir)
        roms_dir = "Roms";

    memory_fill_bank(0, MEM_SIZE, 0xFF);

    int loaded = 0, load_addr = 0;
    for (int i = 0; i < ROM_BANKS; i++, load_addr += 0x2000) {
        if (banks[i] && banks[i][0]) {
            char full[1024];
            snprintf(full, sizeof full, "%s/%s", roms_dir, banks[i]);
            if (memory_load_rom(full, load_addr) == 0)
                loaded++;
        }
    }
    return loaded;
}

const char *rom_version_display_name(RomVersion v)
{
    switch (v) {
        case ROS_1_01: return "ROS 1.01 (14-ROM set)";
        case ROS_1_04: return "ROS 1.04 (16-ROM set)";
        case ROS_1_05: return "ROS 1.05 (16-ROM set)";
        default:       return "ROS (unknown)";
    }
}
