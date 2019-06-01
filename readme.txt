
ussload is UAE state save file (*.uss) loader designed for real hardware.

v2.0:

- MMU memory and ROM translation support (68030, 68040 and 68060)
- GVP MapROM support added. GVP A530 and most A2000 and A3000 GVP boards.
- Blizzard 1230 MKI/II/III/IV, 1240, 1260 MapROM support added.
- ACA1233n MapROM support added, in both 68030 and 68EC020 modes.
- ACA123x 128M model MapROM support fixed.
- Switch off all floppy drive motors before memory decompression.
- Fixed crash if 68020 or 68030 statefile was loaded and CPU was
  68040 or 68060.
- Fixed uncompressed state file support.
- Load also Kickstart files from current directory.
- 68040 and 68060 state files supported (MMU registers are not restored)
- FPU state file support added.
- Pause mode. Restores state, enables display, waits for mouse button.
- Compatibility improved.


Supported state file hardware configurations:

Common OCS/ECS 68000 A500 configurations. Chip RAM, "Slow" RAM and
Fast RAM supported.
Basic A1200 68020 configuration. "Slow" RAM and Fast RAM is also
supported.

Information:

- Compatible with KS 1.2 and newer.
- CPU should match state file config but 68020 to 68030 most likely works,
  68000 to 68020 or 68030 depends on program, 68020 to 68000 rarely works.
- RAM config must match (can be larger than required) and system must
  have at least 512k more RAM than state file requires.
- Both compressed and uncompressed state files are supported.
- HD compatible (state file is completely loaded before system take over)
- KS ROM does not need to match if loaded program has already completely
  taken over the system or supported Map ROM hardware is available.
- All state files should be supported, at least since UAE 0.8.22.
- State file restore can for example fail if state file was saved when
  blitter was active or program was executing self-modifying code.

Minimum RAM config examples (without MMU):

512k Chip RAM state file: hardware must have 1M Chip or 512k Chip+512k
"Slow" RAM or 512k Chip+512k real Fast.
512k+512k state file: hardware must have 1M+512k or 512k+1M or
512k+512k+512k real Fast.

If MMU is available, fully or partially missing RAM address space
is created with MMU. MMU is also used for Map ROM.

Note that uncompressed state files require at least 1M contiguous extra
RAM because all state file RAM address spaces need to fit in RAM before
system take over.
A1200 chip ram only state files usually require at least 1M Fast ram.

Map ROM hardware support:

ACA500, ACA500plus, ACA1221, ACA1221EC and most ACA123x variants.
GVP A530, A2000 and A3000 G-Force models.
Blizzard 1230 MKI/MKII/MKIII/MKIV, 1240, 1260.

Map ROM hardware is not used or needed if MMU is enabled.

If state file ROM is not same as hardware ROM, ROM image is automatically
loaded from DEVS:Kickstarts or from current directory.
Check WHDLoad documentation for DEVS:Kickstarts files and naming.
If A1200 KS 3.0 ROM is missing: manually copy correct ROM to
DEVS:Kickstarts and name it kick39106.a1200.

Command line parameters:

- nowait = don't wait for return key.
- debug = show debug information.
- test = parse and load state file, exit before system take over.
- nomaprom = do not use Map ROM.
- nommu = do not use MMU.
- mmu = use mmu (If CPU is 68030, MMU is not used automatically)
- nocache = disable caches before starting loaded program (68020+ only)
- pause = restore state, wait left mouse button press.
- pal = force PAL mode (ECS/AGA only)
- ntsc = force NTSC mode (ECS/AGA only)

Background colors:

- purple = Map ROM copy.
- red = decompressing/copying Chip Ram state.
- green = decompressing/copying "Slow" RAM (0x00c00000) state.
- blue = decompressing/copying Fast RAM (0x00200000) state.
- yellow = configuring floppy drives (seek rw head, motor state).

Source: https://github.com/tonioni/ussload
