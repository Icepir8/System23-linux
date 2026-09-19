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

- **8085 CPU engine** — full port of the interpreter from
  `Assembler85.RunInstruction` (now in `cpu8085.c`), including the undocumented
  flags (V, K) and opcodes (DSUB, ARHL, RDEL, LDHI, LDSI, LHLX, SHLX, RSTV,
  JK/JNK), exact flag/PSW behaviour and cycle counts, RST 5.5/6.5/7.5 + INTR
  vectoring, and a cycle-paced ~3 MHz run-loop. `make test` runs instruction
  self-tests and a real-ROM smoke run.
- **I/O + timer + CRTC — POST boots.** `ioports.c` ports the full `IN`/`OUT`
  dispatch (`IOports.cs`) with the three 8255 PPIs inlined and the memory page
  registers; `i8253.c` is a complete 8253 PIT (all six modes, Counter 2 → RST
  7.5); `i8275.c` is the 8275 CRTC. With these, the real ROS 1.05 firmware boots
  through reset into POST — it programs the CRTC, starts the display and renders
  its diagnostic step codes on the green screen. Run `SYSTEM23_AUTOSTART=1
  ./bin/system23` to cold-boot and watch it. The 8259 PIC accepts its init
  sequence (full priority resolution is still TODO).
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
| `i8257.c` / DMA ports | `I8257DMA.cs`          | channel ports are latches; transfers TODO |
| `i8259.c`             | `I8259PIC.cs`          | accepts init seq; priority/vectoring TODO |
| `i8251.c`             | `I8251UART.cs`         | USART / printer                     |
| `i765a_fdc.c`         | `FloppyController.cs`  | FDC-card 8255 + 8748 controller + NEC765 status regs ported (POST drive test 39 passes); NEC765 command engine + disk I/O TODO |
| `floppy.c`            | `FloppyController.cs`  | mount metadata is real; sector I/O TODO |
| `sasi.c`              | `SasiHostAdapter.cs`   | SASI → IBM 5247 hard disk           |
| `i8748.c`             | `I8748.cs`             | 8748 keyboard controller (MCS-48)   |
| `assembler85.c`       | `Assembler85.cs`       | text assembler (engine split into `cpu8085`) |
| `disassembler85.c`    | `DisAssembler85.cs`    | disassembler                        |
| `ui/windows.c`        | `FormAbout`, `FloppyDrives`, `DebugForm`, `Printer` | About is real; others are placeholders |

## Suggested porting order

Done so far: the 8085 CPU, the `IN`/`OUT` dispatch + memory page registers, the
three 8255 PPIs, the 8253 PIT and the 8275 CRTC — enough that the real firmware
boots and runs POST on screen.

1. ~~`cpu8085.c` — 8085 interpreter.~~ **Done.**
2. ~~`ioports.c` + 8255 PPIs + page registers.~~ **Done.**
3. ~~`i8253.c` (PIT) + `i8275.c` (CRTC) — POST runs, display live.~~ **Done.**
4. FDC — **in progress.** The FDC-card 8255 handshake (Mode 0 latch + Mode 2
   8748 walk test), the 8748 stepper-controller command dispatch + register RAM,
   and the NEC765 status registers (SRB/MSR/DOR) are ported in `i765a_fdc.c`.
   POST's drive-0 self-test (39) and test 33 now pass; POST failures dropped from
   5 to 3. Remaining: the NEC765 **command engine** (StartCommand/DispatchCommand
   + read/write data), 8257 DMA transfers, and IMD disk-image loading — the path
   to actually loading a diskette and booting past POST.
5. **`i8259.c`** full priority resolution + `InterruptAcknowledge8085` and real
   interrupt delivery — clears the interrupt/timer tests (36) and makes the
   **keyboard matrix** (`ui/display.c`, validated against the 8748) usable.
6. **`i8251.c`** (USART/printer — POST test 37/38 also poke it), SASI (`sasi.c`),
   and splitting the `ui/windows.c` placeholders into full windows.

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
