# System/23 — Linux / GTK3 port

A Linux port of the **System23** emulator (IBM System/23 *Datamaster*, an
8085-based business computer, 1981) from the original C# / WinForms project to
**C99 + GTK3**.

This is the **buildable skeleton**: the application window, toolbar, CRT
rendering pipeline, configuration, and ROM/graphics loading are in place and
run on Linux. The emulation core (CPU + peripherals) is stubbed out behind
clean module boundaries, ready to be ported in one module at a time.

---

## Building (Zorin OS / Ubuntu / Debian)

```sh
sudo apt install build-essential pkg-config libgtk-3-dev
make
./bin/system23
```

There are no dependencies beyond GTK3 and the C toolchain. `make run` builds
and launches; `make clean` removes build artifacts.

> Verified: clean, warning-free build against GTK 3.24 on Ubuntu 24.04 (the same
> GTK/Ubuntu lineage Zorin OS is built on).

---

## What works today

- **Primary Display window** with the operator toolbar built in code (as in the
  original): Power (with a Reset / Power Off menu while running), ROM-set
  selector, Language selector, and Floppy / Debug / Printer / About buttons.
- **CRT rendering pipeline** — an 80×24 green-phosphor text screen drawn with
  Cairo from the banked RAM through the character ROM. The green comes straight
  from the character-ROM BMP palettes (`char3*.bmp`), blitted with
  nearest-neighbour scaling. Runs on a 30 Hz refresh timer with the same
  FNV-1a frame-change detection as the original, and scales to fit the window.
  - Until the core drives the CRTC, the screen shows a "no signal" splash.
  - Set `SYSTEM23_FONTTEST=1` to preview the whole character ROM.
- **Banked memory** — full port of `Memory.cs` (256 KB ROM / 256 KB RAM, ROM/RAM
  paging, DMA window, `read/write/read16/write16`).
- **ROM-set loading** — the three ROS revisions (1.01 / 1.04 / 1.05) with the
  exact deduplicated bank tables from the original.
- **Configuration** — ported from `%LOCALAPPDATA%\System23\config.xml` to
  `~/.config/System23/config.ini` (GKeyFile): ROM set, language, four floppy
  mounts + write-protect, and window geometry. Restored at start, saved on exit.
- **Asset resolution** — finds `Roms/` and `Graphics/` next to the binary, one
  level up, in the working directory, or via `$SYSTEM23_HOME`.
- **Keyboard plumbing** — key events map to a scancode and raise IRQ 0 through
  the (stubbed) 8259, end to end. The full System/23 key matrix is still TODO
  (see below).

## What's stubbed (ready to port)

Each of these is a real translation unit with the correct public API and a
`TODO` pointing at its C# source, so the whole project links and runs:

| Module (C)            | Ported from            | Notes                              |
|-----------------------|------------------------|------------------------------------|
| `cpu8085.c`           | `Assembler85.cs` (`RunInstruction`) + `Registers` | instruction engine — the big one |
| `ioports.c`           | `IOports.cs`           | I/O read/write dispatch + wiring    |
| `i8255.c`             | `I8255PPI.cs`          | 3× PPI                              |
| `i8257.c`             | `I8257DMA.cs`          | DMA                                 |
| `i8275.c`             | `I8275CTRC.cs`         | CRTC (display-facing fields real)   |
| `i8259.c`             | `I8259PIC.cs`          | PIC (assert/deassert IRQ)           |
| `i8253.c`             | `I8253PIT.cs`          | PIT                                 |
| `i8251.c`             | `I8251UART.cs`         | USART                               |
| `i765a_fdc.c`         | `I765AFDC.cs` / `NEC765FDC.cs` | floppy controller           |
| `floppy.c`            | `FloppyController.cs`  | mount metadata is real; sector I/O TODO |
| `sasi.c`              | `SasiHostAdapter.cs`   | SASI → IBM 5247 hard disk           |
| `i8748.c`             | `I8748.cs`             | 8748 keyboard controller (MCS-48)   |
| `assembler85.c`       | `Assembler85.cs`       | text assembler (engine split into `cpu8085`) |
| `disassembler85.c`    | `DisAssembler85.cs`    | disassembler                        |
| `ui/windows.c`        | `FormAbout`, `FloppyDrives`, `DebugForm`, `Printer` | About is real; others are placeholders |

## Suggested porting order

1. **`cpu8085.c`** — port `RunInstruction` (opcode interpreter) and the flag
   helpers. The run-loop in `machine.c` already calls `cpu8085_step`; drop the
   stub-idle branch and add the cycle-correct ~3 MHz throttle noted there.
2. **`ioports.c`** — `ReadIOport` / `WriteIOport` and the page registers, then
   wire the CRTC (`i8275`) so the Display lights up with real screen content.
3. **Timers/interrupts** — `i8253` + `i8259` so POST can progress.
4. **Peripherals** — `i8255`, `i8257`, `i8251`, then the FDC/floppy, SASI and
   the 8748 keyboard controller.
5. **`ui/debug_window.c`, `ui/floppy_dialog.c`, `ui/printer_window.c`** — split
   the placeholders in `ui/windows.c` into full windows.
6. **Keyboard matrix** — replace the provisional GDK→scancode table in
   `ui/display.c` with the full mapping, validated against the 8748 firmware.

---

## Layout

```
System23-Linux/
  Makefile
  README.md
  include/            headers (one per subsystem)
  include/ui/         window headers
  src/                implementations + stubs
  src/ui/             GTK windows
  Roms/               ROM bank images (*.bin)     ← copied from the C# project
  Graphics/           character-ROM bitmaps       ← copied from the C# project
```

Config lives at `~/.config/System23/config.ini`.

## Notes

- The emulation thread never touches GTK: it only mutates emulation state, and
  the Display polls that state on its refresh timer — exactly the threading
  model of the WinForms original (`MachineHost` + `Display`).
- The character-cell renderer in `ui/display.c` still has a couple of Display.cs
  subtleties flagged `TODO(display)` (the two-pass glyph blit, the 19px graphics
  character width, exact inverted-cell compositing) to validate once real screen
  content is available.
