
ussload is UAE state save file (*.uss) loader designed for real hardware.

v2.1:

- Accept CD32 state files. Akiko and CD audio playback state
  is restored.
- Accept CDTV state files. DMAC and 6525 state is restored but
  CD drive internal state is not (yet?) restored.
- If running on CD32/CDTV and loading non-CD32/CDTV statefile:
  automatically disable all CD related interrupts to prevent
  possible hang (stuck CD interrupt) when restored program starts.
- ACA1221LC Map ROM support added.
- If <rom name>.a1200 can't be found, try also <rom name>.a500.
  Workaround for statefiles with mismatched hardware settings.

v2.0:

- MMU based memory and Map ROM support (68030, 68040 and 68060)
- GVP Map ROM support added. GVP A530 and most A2000 and A3000 GVP boards.
- Blizzard 1230 MKI/II/III/IV, 1240, 1260 Map ROM support added.
- ACA1233n Map ROM support added, in both 68030 and 68EC020 modes.
- ACA123x 128M model Map ROM support fixed.
- Switch off all floppy drive motors before memory decompression.
- Fixed crash if 68020 or 68030 state file was loaded and CPU was
  68040 or 68060.
- Fixed uncompressed state file support.
- Attempt to load rom image files from current directory if rom
  image file is not found from DEVS:Kickstarts.
- 68040 and 68060 state files supported (MMU registers are not restored)
- FPU state file support added.
- Pause mode. Restores state, enables display, waits for mouse button.
- Nofloppy option, do not initialize or seek floppy drives.
- HRTMon support. If HRTMon is installed, NMI vector is automatically
  set to HRTMon entry point.
- Compatibility improved.

--

ussload is mainly designed to load state files with basic A500 and
A1200 configurations like following:

Common OCS/ECS 68000 A500 configurations. Chip RAM, "Slow" RAM and
Fast RAM supported.
Basic A1200 68020 configuration. "Slow" RAM and Fast RAM is also
supported.
CD32 and CDTV are also partially supported.

Non-RAM expansion hardware is not supported.

Information:

- Compatible with KS 1.2 and newer.
- CPU should match state file config but 68020 to 68030+ most likely
  works, 68000 to 68020+ depends on program, 68020 to 68000 rarely works.
- If system has no MMU, RAM config must match. RAM size can be larger
  than required.
- System must have at least 512k more RAM than state file requires.
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
is created with MMU. MMU is also automatically used for Map ROM.

Note that uncompressed state files require at least 1M contiguous extra
RAM because all state file RAM address spaces need to fit in RAM before
system take over.
A1200 chip ram only state files usually require at least 1M Fast ram.

Map ROM hardware support:

ACA500, ACA500plus, ACA1221, ACA1221EC, ACA1221LC, ACA1233n and most
ACA123x variants. GVP A530, A2000 and A3000 G-Force models.
Blizzard 1230 MKI/MKII/MKIII/MKIV, 1240, 1260.

Map ROM hardware is not used or needed in MMU mode.

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
- mmu = use mmu (If CPU is 68030, MMU mode is not enabled automatically)
- nocache = disable caches before starting loaded program (68020+ only)
- nocache2 = disable caches when taking over the system (68020+ only)
- pause = restore state, wait left mouse button press.
- pal/ntsc = force PAL/NTSC mode (ECS/AGA only)
- nofloppy = don't initialize floppy drives (motor state, seek)
- trap = debugging option, see below.
- generic/cd32/cdtv = override hardware model autodetection.

Background colors:

- purple = Map ROM copy.
- red = decompressing/copying Chip Ram state.
- green = decompressing/copying "Slow" RAM (0x00c00000) state.
- blue = decompressing/copying Fast RAM (0x00200000) state.
- yellow = configuring floppy drives (seek rw head, motor state).

Technical HRTMon support details:

- Bus error and NMI vectors are always set if HRTMon is detected.
- If host CPU is 68010+ and state file is 68000 or VBR=0, VBR is
  moved and redirected to original vectors except NMI and Bus
  error vectors.
- If host CPU is 68000 or state file has non-zero VBR: original
  vectors are modified directly.
- trap <mask> command line option can be used to set other
  exception vectors. It is bit mask where lowest 16 bits maps
  to vectors 0 to 15. Highest 16 bits are traps, for example
  trap ffff0000 sets all trap #0-#15 vectors.
  
Source: https://github.com/tonioni/ussload
