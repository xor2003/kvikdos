kvikdos-ng: a very fast headless DOS emulator for Linux and Windows
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
kvikdos-ng is a very fast emulator for running DOS programs on Linux (and,
via the cosmopolitan build, Windows), both real-mode and 32-bit
protected-mode. Typical targets are old compilers, assemblers and other
build tools, plus text-mode IDEs and editors. kvikdos-ng implements a very
small subset of DOS, BIOS and IBM PC harware, so it can keep the overhead
low, so it can be very fast. It uses Linux KVM under the hood for
emulating the CPU (or Windows WHPX), which is also very fast.

kvikdos-ng is a fork of kvikdos by Peter Szabo
(https://github.com/pts/kvikdos), extended with DPMI protected-mode
support (auto-detected hosts plus an embedded HDPMI32 fallback), an
embedded HX DOS Extender for Win32 PE console apps, an 80x25 text
mode for IDEs, and the Windows/WHPX backend. The binary is still
named `kvikdos`.

kvikdos-ng is free software, GNU GPL >=2.0. There is NO WARRANTY. Use at your risk.

kvikdos-ng should be pronunced as ``quick DOS''. the initials ``kv'' refers to
KVM, the original virtualization technology.

Features at a glance:

* Fast: CPU runs natively under Linux KVM (Intel VT-x/AMD-V); I/O calls
  map directly to Linux syscalls; quick startup for short-lived tools.
* Real mode and protected mode: 16-bit DOS programs run directly;
  32-bit protected-mode programs run through a bound extender
  (Phar Lap 386|DOS-Extender/TNT) or a resident DPMI host —
  auto-detected on disk or the embedded HDPMI32 fallback
  (`--dpmi=' to pick one explicitly, `--dpmi=off' to disable).
* Memory: fully-chained conventional MCB arena (msdos_player semantics:
  per-PSP owners, free `Z' tail) plus up to 1 GiB guest memory
  (`--mem-mb=', 128 MiB default) exposed via XMS 3.0 (incl. 0x88/0x89
  for >64 MiB) and int 15h AH=88h; partial EMS (int 67h) for probes.
* DOS ABI: a working subset of int 21h (files, directories, findfirst/
  next, FCB, exec, memory, environment, PSP, exit codes), plus BIOS
  ints 10h (video), 11h, 15h, 16h (keyboard), 1ah (time), multiplex
  int 2fh, and direct IVT writes. int 21h AH=52h returns a real
  List-of-Lists (dos_info_t).
* Console: stdout/stdin/stderr map to Linux streams for pipeline use;
  a minimal 80x25 text mode renders full-screen text programs
  (colors, cursor, blink, Alt keys) on the terminal — IDEs and editors
  like Watcom VI and Turbo Pascal IDE work.
* DOS drives: Linux directories mounted as DOS drive letters with
  configurable case folding; executable auto-resolution with case
  fallback.
* Batch files: a built-in interpreter for .bat driver scripts
  (`set`, `%VAR%`, `%1..%9`, `shift`, `call`, `if`, `goto`, `mkdir`,
  `copy`, `del` ...).
* Integration: propagates DOS exit codes, passes environment variables,
  runs Linux ELF/scripts natively, runs PE executables natively on
  Windows, delegates PE/NE/LE/LX to wine when present, and runs PE32
  console apps via the embedded HX DOS Extender when it isn't.
* Hosts: Linux/KVM natively; the cosmopolitan (APE) build adds
  Windows/WHPX — a single binary picks its backend at runtime.

Emulated DOS environment in more detail:

* Process model: PSP with command tail, environment block and program
  pathname; int 21h AH=4Bh exec for child programs (COM/EXE and
  overlays); TSRs via int 21h AH=31h; exit-code propagation.
* Files: create/open/read/write/seek/close, delete/rename, attributes,
  mkdir/rmdir/chdir/getcwd, findfirst/findnext with FCB and DTA
  wildcard matching, long-name (int 21h AX=71xx) probes, duplicate and
  redirected handles (stdout/stderr piping included).
* Interrupt vectors: get/set via int 21h AH=25h/35h, plus direct writes
  to the IVT (paired offset+segment writes are buffered correctly, as
  TASM 3.0 needs).
* Interrupts served: 10h (video), 11h (equipment), 15h (A20, extended
  memory), 16h (keyboard), 1ah (timer), 20h/22h (terminate), 21h (DOS),
  29h (fast console), 2ah (network install query), 2fh (multiplex:
  XMS/DPMI/redirector/Windows probes), 43h (XMS entry trampoline), 67h
  (EMS subset), plus fault handlers (00h divide, 03h breakpoint used by
  TASM 3.0, 0dh general-protection soft-fail for extender probes).
  Unknown calls either fail gracefully (`--permissive', the default) or
  abort (`--strict').
* Hardware: RTC via CMOS ports 70h/71h, BIOS Data Area (keyboard buffer,
  tick count, video state, memory size), machine-ID and BIOS-date bytes
  in the F-segment, A20 line, keyboard controller enough for Alt-aware
  IDE input.
* EXE loading: MZ with relocations, EXEPACK detection with the
  DOS 5-style fixed unpacking stub (avoids the `Packed file is corrupt'
  error), Phar Lap `P3'/TNT bound overlays, and format detection
  (MZ/NE/LE/LX/PE delegated to wine — or run natively on Windows —
  Linux ELF/scripts run natively).
* Diagnostics and test hooks: `--diag=' categories (compat, exec, int,
  fs, all), `--diag-file=', `--hlt-ok'/`--hlt-dump=' guest RAM dumps,
  `--call-near'/`--call-far'/`--call-ss'/`--call-set' for running and
  probing arbitrary guest subroutines, `--poke-word='.

Requirements:

* Linux running on i386 (x86, i686) or amd64 (x86_64) with KVM
  (see below), or Windows 10/11 on x86_64 with the Windows Hypervisor
  Platform (WHPX) enabled, using the cosmopolitan APE build (see below).
* Hardware virtualization (Intel VT-x or AMD-V) enabled on the host.
  Modern CPUs (even some in 2008) support it.
* KVM enabled for your Linux user (see below); on Windows, the
  "Windows Hypervisor Platform" optional feature enabled.

Limitations:

* On Linux, kvikdos-ng runs the virtual CPU through /dev/kvm; it has been
  tested and found working with both: Linux compiled for i386 and Linux
  compiled for amd64. The cosmopolitan (APE) build adds a Windows host
  path: the same single binary loads WinHvPlatform.dll and runs the guest
  through WHPX when it detects Windows at runtime (the backend layer is
  in hv.h/hv_kvm.c/hv_whpx.c; WHPX exit completion needs the small
  instruction decoder in x86dec.c). macOS users should use udosrun
  instead of kvikdos-ng, others should use DOSBox or DOSBox-X.

* kvikdos-ng can run 16-bit real-mode DOS programs (written for the 8086, 186
  or 286 processors, but not 16-bit 286 protected mode), and it can also
  run 32-bit protected-mode DOS programs (written for 386, 486, Pentium
  processors or above) whose extender uses DPMI or runs in protected mode
  directly on the KVM CPU. See the protected mode section below for the
  supported extenders. kvikdos-ng can't run 64-bit programs.

* kvikdos-ng can run a single DOS program at a time. (But you can run multiple
  independent instances of kvikdos-ng in parallel.) Use udosrun (with the
  `-text' or `-gui' flag) or DOSBox if you want to run multiple DOS programs
  after each other or nested.

* The DOS program can use up to 635 KiB of (conventional) memory, including
  the program code and the variables (data and BSS). The 635 KiB excludes:
  environment variables, command-line arguments (in the Program Segment
  Prefix), Interrupt Vector Table, Program Segment Prefix, program stack
  (last 768 bytes), BIOS Data Area, helper code, the user code written for
  Linux.

  On top of conventional memory, kvikdos-ng provides extended memory (XMS 3.0,
  including the >64 MiB functions 0x88/0x89, and the int 15h AH=88h
  interface): 127 MiB by default, configurable with `--mem-mb=<n>' (1 ..
  1024 MiB of total guest memory). A partial EMS (int 67h) implementation
  exists for toolchain probes. There is no VCPI.

  For DPMI, kvikdos-ng loads an external resident DPMI host (e.g. CWSDPMI or
  HDPMI32) into the guest with `--dpmi=<dos-pathname>', then runs the real
  program on top of it; there is no built-in DPMI server. In most cases
  `--dpmi=' isn't even needed: kvikdos-ng scans the program for DPMI/extender
  markers and auto-loads a resident host found beside the program, on the DOS
  PATH, or on the emulator's own D: mount, falling back to an embedded copy
  of HDPMI32.EXE. `--dpmi=auto' forces the search, `--dpmi=off'/`--dpmi=none'
  disables it entirely.

* kvikdos-ng doesn't support graphics. Use udosrun or DOSBox instead.

* kvikdos-ng has a minimal 80x25 text mode: it renders the text screen
  (colors, cursor position and shape, blinking) on the Linux terminal, and
  supports the BIOS/DOS video and keyboard calls needed by text-mode IDEs
  and editors (e.g. Watcom VI and Turbo Pascal IDEs work). Full-screen
  programs which need fancy video tricks may still misbehave; use udosrun
  or DOSBox for those. kvikdos-ng can also supply line-based input (terminated
  by <Enter>) to programs which don't need the interactive screen.

* kvikdos-ng doesn't emulate any special hardware (e.g. sound card, MIDI,
  joystick, mouse, CD-ROM). Use DOSBox instead.

* kvikdos-ng implements a tiny subset of the DOS ABI (int 21h etc.), PC BIOS
  ABI (int 10h etc.) and IBM PC hardware interfaces (in and out
  instructions). Thus random DOS programs won't work out of the box. Simple
  API calls can be added on the fly to the int*.c sources. However, many famous build
  tools (e.g. compilers and assemblers) released in the 1980s and 1990s
  already work, see the compatibility list below. To get good chances for
  running any random DOS program, use udosrun or DOSBox instead.

* If the target file is Linux-native (ELF or shebang script), kvikdos-ng
  executes it natively with `fork+execvp` instead of DOS emulation.

* If the target file is a PE (32-bit or 64-bit) executable and kvikdos-ng
  itself runs on Windows, it is launched by the host system natively —
  64-bit directly, 32-bit through WoW64 — no emulator or wine involved.

* Otherwise, if the target file is a Windows executable format
  (PE/NE/LE/LX), kvikdos-ng delegates execution to `wine` automatically.
  If wine isn't installed but the file is a PE32 console application,
  kvikdos-ng runs it inside the emulator through an embedded HX DOS
  Extender kit (HDPMI32.EXE + DPMILD32.EXE + the D* DLL set DKRNL32,
  DUSER32, DGDI32, DADVAPI, VERSION, OLE32, OLEAUT32, SECUR32 — the
  KERNEL32/USER32/GDI32/... import closure for console apps), extracted
  to a per-user temp dir and mounted on a spare DOS drive — no external
  files needed. Apps importing other libraries (MSVCRT, sockets, ...)
  still need those DLLs beside them or on the DOS PATH.
  GUI/subsystem != console PEs are not attempted this way.

* `--force-dos' disables all of the above native/wine delegation and always
  runs the program in the DOS emulator.

Features and advantages:

* kvikdos-ng is very fast, it runs CPU-intensive code at almost native speeds
  with Linux KVM. For I/O-intensive code, kvikdos-ng tries to satisfy DOS I/O
  calls using the corresponding Linux system calls (mapping as directly as
  possible), thus it has less overhead than other emulators. See
  benchmark/benchmark.md for a comparison between kvikdos-ng, DOSBox and QEMU.
  For CPU-intensive worklad, kvikdos-ng and QEMU + KVM are on par, each of them
  is about 11.49 times faster than the next emulator. For mixed CPU and I/O
  workload, kvikdos-ng is 4.507 times faster than anything else.

* All features of a modern Intel CPU (such as floating point instructions,
  32-bit registers and 16/32-bit protected mode) are available for DOS
  programs, because the host CPU features are used directly. Guest memory
  is up to 1 GiB (`--mem-mb=<n>', 128 MiB by default).

* Since very little hardware is emulated, kvikdos-ng starts up very quickly,
  it's possible to run dozens of short-lived kvikdos-ng instances per second.

* After installation (and enabling KVM for the Linux user), kvikdos-ng doesn't
  need special privileges (i.e. root or sudo not needed).

* kvikdos-ng integrates DOS command-line tools to the Linux (Unix) command-line:
  its standard input, output etc. can be redirected, it propagatates the DOS
  exit code to Unix, it can pass environment variables to DOS etc. It also
  works very well headless (i.e. without GUI or interactive text UI), e.g.
  as part of continous build pipelines.

* kvikdos-ng runs 32-bit protected-mode DOS programs: Phar Lap
  386|DOS-Extender/TNT bound binaries run directly (Watcom compilers,
  WLINK, VI), and DPMI clients run through a resident host —
  auto-detected beside the program/on PATH/on D:, with embedded
  HDPMI32 as fallback. Win32 PE32 console apps also run through an
  embedded HX kit (HDPMI32 + DPMILD32 + DKRNL32/DUSER32/DGDI32/
  DADVAPI/VERSION/OLE32/OLEAUT32/SECUR32) — JWasm.EXE assembling to
  OMF .obj, Watcom's WHERE.EXE and HX's LOCTIME.EXE verified on a
  bare directory; apps importing other DLLs (MSVCRT, sockets) still
  need them on the DOS PATH. Extended memory is provided via XMS 3.0
  and int 15h AH=88h on top of the conventional arena.

* kvikdos-ng has a minimal 80x25 text mode for IDEs and editors (colors,
  cursor position/shape/blink, Alt-aware keyboard), rendered on the Linux
  terminal.

* kvikdos-ng runs DOS batch files (.bat) with `set`, `%VAR%`, `%1..%9`,
  `shift`, `call`, `if`, `goto`, `mkdir`, `copy`, `del` etc., enough for
  compiler driver scripts.

How to install kvikdos-ng:

* (You don't have to install QEMU, kvikdos-ng doesn't need it.)

* To check for hardware virtualization (Intel VT-x or AMD-V),
  run this command on your Linux system (without the leading `$'):

    $ awk '/^flags/&&/( vmx | svm)/{print"OK";exit}' </proc/cpuinfo 
    OK

  If it doesn't display OK (as indicated above), then consult this
  tutorial: https://www.linux-kvm.org/page/FAQ#How_can_I_tell_if_I_have_Intel_VT_or_AMD-V.3F
  . You may have to enable it in the BIOS.

* To check that KVM works on your Linux system,
  run (just the line starting with `$', without the `$'):

    $ cat /dev/kvm
    cat: /dev/kvm: Invalid argument

  If you get `Invalid argument' (as above), then it's OK.

  If you get `No such file or directory' instead, then your kernel doesn't
  have KVM. Ubuntu and mainstream Linux distributions have it. Consult the
  support channels of your Linux distribution about enabling KVM.

  If you get `Permission denied' instead, then you need to give permissions
  for your Linux user to /dev/kvm. Typically, on Ubuntu, run the following,
  and reboot:

    $ sudo adduser "$(id -nu)" kvm

* Download the precompiled kvikdos-ng binary (kvikdos.linux.i386). Rename the
  file to kvikdos, and make it executable:

    $ mv kvikdos.linux.i386 kvikdos
    $ chmod 755 kvikdos

* Download a test DOS program (guest.com) to the same directory, and run it:

    $ ./kvikdos guest.com
    .
    Hello, World!

  If it displays the dot and the `Hello, World!' message, then it's OK.

Building from source:

* Native Linux build (needs gcc, make, and /dev/kvm at runtime):

    $ make kvikdos

* Portable binary (cosmopolitan APE — the shipped `kvikdos` executable,
  runs on Linux + Windows, stripped):

    $ make dist

  Produces dist/kvikdos.com — a single APE file that runs on both Linux
  and Windows. Get the cosmopolitan toolchain
  from https://cosmo.zip/pub/cosmocc/ (cosmocc-<ver>.zip, unzip anywhere,
  put its bin/ on PATH or pass COSMOCC=/path/to/x86_64-unknown-cosmo-cc
  COOBJCOPY=/path/to/x86_64-linux-cosmo-objcopy). The APE runs DOS
  programs through KVM on Linux; on Windows (as kvikdos.com) it loads
  WinHvPlatform.dll and runs through WHPX (enable the "Windows
  Hypervisor Platform" optional feature). The backend is selected at
  runtime, so the same file works on both hosts. x86-64 is the only
  arch since KVM/WHPX virtualization is x86-specific.

Command-line options (structured):

General:

* `--kvm-check' checks the hypervisor backend only (KVM on Linux, WHPX on
  Windows) by running a minimal guest.
* `--strict' enables strict unsupported-interrupt behavior.
* `--permissive' enables compatibility fallback behavior (default).
* The `KVIKDOS_FLAGS' environment variable holds extra default flags
  (whitespace-separated), prepended to the command line — handy for a
  persistent DOS tree setup, e.g.
  `KVIKDOS_FLAGS="--root=/opt/dos --dpmi=C:/BIN/HDPMI32.EXE"'.

DOS runtime:

* `--toolchain=<name>' applies toolchain presets:
  `msc4', `msc5', `msc6', `masm5', `bc2', `bcpp1', `bc5', `ic86'.
* `--env=<NAME>=<value>' adds one DOS environment variable.
* `--env-file=<file>' loads DOS environment variables from file
  (`NAME=VALUE' lines).
* `--path-dos=<pathlist>' sets DOS PATH explicitly; it is also used to
  resolve a bare program name on the command line.
* `--prog=<dos-pathname>' sets DOS pathname of the running program.
* `--cwd-dos=<path>' sets initial DOS current directory.
* Forward slashes are accepted in the DOS-path flags above (`--path-dos',
  `--prog', `--cwd-dos', `--dpmi') and converted to backslashes.

Mounts:

* `--root=<dirname>' is a shortcut for running a program from a DOS
  directory tree: it mounts <dirname> as C: (uppercase), sets the DOS
  drive and current directory to C:\, and sets PATH to C:\;C:\BIN.
  Equivalent to `--mount=C:<dirname> --drive=C: --cwd-dos=C:\
  --path-dos=C:\;C:\BIN'; flags given after `--root=' still override
  these. `~/' is expanded to $HOME.
* `--mount=<drive><case><dirname>/' mounts Linux directory to DOS drive
  (the trailing `/' is optional).
* `--mount=<drive>0' hides DOS drive.
* `--drive=<drive>' sets initial DOS drive.

Compatibility:

* `--case-fallback=off|prog|all' controls case-insensitive executable
  lookup fallback. Default: `all'.

Diagnostics:

* `--diag=compat|exec|int|fs|all|off' controls runtime diagnostics.
  Default: `compat'.
* `--diag-file=<file>' writes diagnostics to a file.

I/O and memory:

* `--tty-in=<fd>' chooses input source (`-3', `-2', `-1', `>=0').
* `--mem-mb=<n>' total guest memory size in MiB (1 .. 1024). Default: 128.
  The first MiB is conventional memory, the rest is extended memory
  available via XMS 3.0 and int 15h AH=88h.
* `--dpmi=<dos-pathname>' loads the named resident DPMI host
  (e.g. CWSDPMI.EXE or HDPMI32.EXE) as a TSR first, then runs the real
  DOS program on top of it. Use this for 32-bit protected-mode tools
  which need DPMI (e.g. DOS4GW-style programs and the HX loader).
  In most cases the flag is unnecessary: when the program looks like a
  DPMI client (it contains DPMI/extender strings such as `DPMI',
  `DOS/4G', `CAUSEWAY', `PMODEW', `RTM'), kvikdos-ng auto-searches a
  known host — HDPMI32.EXE, CWSDPMI.EXE, HDPMI.EXE, DPMIRES.EXE,
  32RTM.EXE, RTM.EXE, PMODEW.EXE, CWSDPR0.EXE — next to the program, on
  the DOS PATH and on the emulator's own D: mount, and loads it
  resident. Programs which are themselves DPMI hosts/loaders are
  excluded by name. `--dpmi=auto' forces the host search even without
  markers; `--dpmi=off' (or `=none') disables auto-detection for the
  run. As a further shortcut, when a program's real-mode loader is a
  Borland 32STUB (e.g. BCC32.EXE or TLINK32.EXE), kvikdos-ng auto-loads
  the sibling 32RTM.EXE/RTM.EXE as the resident host.
* `--hlt-ok' and `--hlt-dump=<file>' are low-level debug options.

Best defaults:

* `--permissive'
* `--case-fallback=all'
* `--diag=compat'
* no toolchain preset unless requested

Examples:

* Microsoft C 5.1:

    $ ./kvikdos --toolchain=msc5 /home/xor/inertia_player/dos_compilers/Microsoft\ C\ v5/CL.EXE HELLO.C

* Borland C 2 with explicit DOS PATH and cwd:

    $ ./kvikdos --path-dos=C:\\ --cwd-dos=C:\\ /home/xor/inertia_player/dos_compilers/Borland\ Turbo\ C\ v2/TCC.EXE HELLO.C

* Diagnostics to file:

    $ ./kvikdos --diag=all --diag-file=kvikdos.diag /home/xor/inertia_player/masm/BIN/LINK.EXE

Batch regression tests:

* Run all local tests:

    $ make test

* Current batch suite (`tests/test_batch.sh`) covers commands/patterns used
  by DOS compiler scripts: `set`, `%VAR%`, `%1..%9`, `shift`, `call`,
  `if` (`==`, `not`, `errorlevel`, `exist`), `goto`, `mkdir`, `copy`, `del`.

Static analysis and runtime checks:

* cppcheck:

    $ cppcheck --enable=warning,style,performance,portability --std=c89 --force *.c

* clang static analyzer:

    $ clang --analyze -Xanalyzer -analyzer-output=text -std=c89 -Wall -Wextra *.c

* ASan+UBSan build and compiler smoke:

    $ gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-strict-aliasing -o kvikdos_asan *.c
    $ /home/xor/kvikdos/kvikdos_asan /home/xor/inertia_player/dos_compilers/Microsoft\ C\ v5/CL.EXE /c HELLO0.C
    $ /home/xor/kvikdos/kvikdos_asan /home/xor/inertia_player/dos_compilers/Microsoft\ MASM\ v5/BIN/MASM.EXE HELLO.ASM,HELLO.OBJ,NUL.LST,NUL.CRF

* Valgrind smoke:

    $ valgrind --tool=memcheck --leak-check=full --error-exitcode=101 ./kvikdos /home/xor/inertia_player/dos_compilers/Microsoft\ C\ v5/CL.EXE /c HELLO0.C
    $ valgrind --tool=memcheck --leak-check=full --error-exitcode=101 ./kvikdos /home/xor/inertia_player/dos_compilers/Microsoft\ MASM\ v5/BIN/MASM.EXE HELLO.ASM,HELLO.OBJ,NUL.LST,NUL.CRF

Readability and stability improvement roadmap (recommended):

* Priority 1 (safety):
  * Initialize all state structs (`DirState`, `ParsedCmdArgs`, runtime locals)
    before first use; avoid partial-init patterns.
  * Keep parser regression tests for option combinations (`--prog`, mounts,
    drive/cwd/path interactions) and assert "no crash" behavior.
  * Prefer bounded copy helpers (`copy_cstr0`) over ad-hoc `strncpy` usage.

* Priority 2 (clarity):
  * (Done: the former kvikdos.c monolith is now split into ~36 translation
    units of at most ~500 lines, sharing the internal kvikdos.h header;
    remaining work is intra-file.) Keep new functions small and focused.
  * Replace ambiguous locals (`p`, `q`, `r`) with intent names in new code
    (`requested_drive`, `active_drive`, `resolved_prog`).
  * Keep compatibility fallbacks local and commented at point of use.

* Priority 3 (analysis depth):
  * Add a fast static target in CI: `cppcheck` + `clang --analyze`.
  * Add sanitizer targets (ASan+UBSan) and valgrind jobs as separate steps.
  * Add negative tests for malformed paths/env/flags to verify deterministic
    error messages and exit codes.

About making Linux files available for DOS programs:

* kvikdos-ng emulates DOS drives A: .. F: by exposing directories on the Linux
  filesystem as mount points for these DOS drives.

* Use the `--mount=<drive><case><dirname>/' command-line flag to make
  DOS drive <drive>: point to Linux directory named <dirname>. Use `:' as
  <case> for uppercase (see below), and use `-' for lowercase.

* Use the `--mount=<drive>0' command-line flag to make <drive>: invisible
  to DOS programs. This is useful to override some default mounts.

* Use the `--mount=<drive><case>' command-line flag to override case folding
  for default mounts.

* The following drives are visible to DOS by default (i.e. default mounts):

  * C: points to the current directory (.) of the kvikdos-ng Linux process.

  * D: points to the directory containing the kvikdos-ng executable program
    (taken from argv[0]).

  * E: points to the directory containing the <dos-executable-file>
    specified in the command-line, if it was specified as a Linux pathname.

  * A:, B: and F: are not mounted by default.

* The default drive is C:, but if it was disabled (`--mount=C0'), then the
  default drive is E:.

* If you don't specify `--env=PATH=...', then the DOS PATH environment
  variable is set to the directory containing <dos-executable-file>
  (i.e. `--env=PATH=E:\' most of the time).

* The defaults are set up in way that most of the time you don't need to
  specify `--mount=...', and you don't need to specify any directory name in
  DOS pathnames. That's because the current Linux directory is visible as
  C:\ in DOS, and that's the default within DOS as well.

About uppercase and lowercase filenames:

* Filenames in Linux are case sensitive, but in DOS they are case
  insensitive. Thus if a DOS program wants to open or access a file, kvikdos-ng
  has to decide how to case fold the letters in the pathname.

* Only unaccented Latin letters a .. z (and A .. Z) are targets of case
  folding. International characters (typically with code >= 128) are kept
  intact.

* When the DOS program tries to open or access a file, kvikdos-ng generates an
  uppercase or lowercase Linux filename based on the mount flags of the
  emulated drive the DOS file is on. (DOSBox does it differently: on a per
  file basis, it uses uppercase iff the lowercase variant doesn't already
  exists on the filesystem.)

* To specify an uppercase drive, use the `--mount=<drive>:<dirname>' flag.
  To specify a lowercase drive, replace the `:' with `-' above. For
  example, to mount the current Linux directory as C: lowercase,
  specify `--mount=C-' .

* For drives C:, D: and E:, if not explicitly specified as
  `--mount=<drive>...', kvikdos-ng autodetects lowercase based on the
  on the <dos-executable-file>: if there is at least
  one lowercase character, the drive becomes lowercase. For drives D: and E:
  only the last pathname component is considered, for C:, if
  <dos-executable-file> is under C:, then the entire pathname is considered.
  To override autodetection, specify e.g. `--mount=E:' for uppercase and
  `-mount=E-' for lowercase.

Software compatibility, i.e. DOS programs known to work in kvikdos-ng:

Quick start for DOS compilers and assemblers:

* For practical, copy-paste command lines, see TOOLCHAIN.md in this
  repository.

* Recommended layout rules:
  * Use 8.3 filenames for source/object/output files (e.g. HELLO.C).
  * If a toolchain has BIN/LIB/INCLUDE directories, mount the toolchain
    root as C: and run the executable from C:\BIN\...
  * Set DOS environment explicitly with --env=LIB=... and
    --env=INCLUDE=...

* Recently validated under kvikdos-ng (compile, link, run of a tiny HELLO):
  * Microsoft C 4.0
  * Microsoft C 5.1
  * Microsoft C 6ax (from toolchain root, C:\BIN\CL.EXE, LIB=C:\LIB)
  * Intel iC-86 Compiler 4.5
  * Borland Turbo C 2.0
  * Borland Turbo C++ 1.01 (from toolchain root, C:\BIN\TCC.EXE, LIB=C:\LIB)
  * Microsoft MASM 5.0 (MASM direct, LINK via prompt answers or stdin)

* Turbo Pascal 7.0 compiler tpc.exe. It produces .exe program files
  directly.

* A86 macro assembler 3.14 .. 4.05 (practically all popular versions)
  a86.com. It produces .com program files directly or OMF .obj files.

* Turbo Assembler (TASM) 2.51, 3.0, 3.1, 3.2 and 4.0 tasm.exe.
  There is no newer 16-bit real mode TASM, packages 5.0 and 5.2 contain the
  tasm.exe of TASM 4.1. It produces OMF .obj files.

* Turbo Link (TLINK) linker 3.01 and 4.0 tlink.exe. There is no newer 16-bit
  real mode TLINK, newer versions of tlink.exe use 16-bit 286 protected
  mode, which kvikdos-ng doesn't support. It reads OMF .obj and .lib files,
  and it produces .exe and .com program files.

* TLIB library builder 3.01 and 3.02 tlib.exe. There is no newer 16-bit
  real mode TLIB, newer versions of tlib.exe use 32-bit protected
  mode, which should now work through the Phar Lap/DPMI support (not
  yet verified).  It produces OBF .lib files from OMF .obj files.

* Sphinx C-- compiler 1.04 c--.exe: It produces .com program files.

* Microsoft QuickBASIC compiler 4.50 bc.exe and the corresponding linker
  link.exe. The compiler produces OMF .obj files, and the linker reads
  OMF .obj and .lib files, and it produces .exe program files.

  Please note that the generated .exe files are correct (they are identical
  to those generated in DOSBox by the same tools), but they don't work yet
  in kvikdos-ng, even the hello-world doesn't work yet.

* Microsoft BASIC Professional Development System compiler 7.10 bc.exe and
  the corresponding linker link.exe. The compiler produces OMF .obj files,
  and the linker reads OMF .obj and .lib files, and it produces .exe program
  files.

  Please note that the generated .exe files are correct (they are identical
  to those generated in DOSBox by the same tools), but they don't work yet
  in kvikdos-ng, even the hello-world doesn't work yet.

* BAssPasC Compiler v3.0pre2 bapc3.exe. It produces .asm (TASM) assembly
  source files from .bp3 source files.

* Netwide Assembler (NASM) 0.98.39. The 16-bit versions downloaded from
  here:
  https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/devel/asm/nasm/0.98.39/
  This is the last version of nasm which uses 32-bit integers internally
  (thus it compiles on 16-bit systems), newer versions use 64-bit integers
  or even longer.
  It produces OMF .obj files, .com program files or others (use `nasm -hf'
  to get a list of output formats).

  Download links:

  * https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/devel/asm/nasm/0.98.39/8086host/nasm-0.98.39-16bit-8086-12oct2019-Rugxulo.zip
  * https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/devel/asm/nasm/0.98.39/8086host/nasm16.zip
  * https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/devel/asm/nasm/0.98.39/8086host/nasmlite.zip
  * https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/devel/asm/nasm/0.98.39/nsm09839.zip

* Watcom C/C++ 8.5, 9.5, 10.0, 10.6 and 11(b): wcc/wcc386/wpp/wpp386 and
  wlink work through the Phar Lap 386|DOS-Extender bound into the binaries
  (32-bit protected mode). whelp and the VI editor also run; VI renders a
  full screen editor in the 80x25 text mode.

* Phar Lap SDK tools: tntlite.exe (loads unbound .exp files, e.g.
  MEMTEST.EXE allocates megabytes through the extender), tellme.exe
  (full system probe incl. a correct DOS memory map), rebind.exe,
  cfig386.exe, markphar.exe, dosxnt.exe (Microsoft C 8).

* DPMI hosts (as `--dpmi=' resident hosts): CWSDPMI.EXE (DJGPP r7) and
  HDPMI32.EXE/HDPMI16.EXE (Microsoft C 7) install, take over int 2fh/int
  31h and run a 32-bit DPMI client correctly.

* Turbo Pascal IDE (e.g. 5.5) and other text-mode IDEs work in the
  minimal 80x25 text mode.

* Turbo C 2.0 and Turbo C++ 1.01 are validated (see above); later
  Borland C++ versions haven't been tested.

* Borland C++ 4.52 compiles and links: BCC32.EXE auto-loads its sibling
  32RTM.EXE resident host, compiles C to a valid OMF .obj, and drives
  TLINK32.EXE to a valid PE32 .exe (give it access to c0x32.obj and
  import32.lib/cw32.lib, e.g. on the program's drive). The run ends with a
  cosmetic `32loader runtime error: Unhandled exception Exception 0E'
  register dump during the RTM's module teardown -- the output files are
  already written and valid and the process still exits 0.

* Borland C++ 4.5 (16-bit) also compiles and links: BCC.EXE is a 16-bit
  `16STUB' program that loads its own DPMI16BI.OVL DPMI layer and drives
  RTM.EXE -- no `--dpmi=' flag is needed. `BCC.EXE -c file.c' produces a
  valid OMF .obj, and `BCC.EXE file.c' additionally drives the nested
  TLINK.EXE to a valid real-mode .exe (each exec'd tool gets its own
  environment block with argv[0] = its own path, matching DOS EXEC
  semantics, so RTM resolves the right image). Give it access to the
  model's startup object and runtime libraries (e.g. c0s.obj + cs.lib +
  emu.lib + maths.lib for the small model) on the tool's drive.

* (For copy-paste compile-and-link tutorials see TOOLCHAIN.md.)

* (Please note that installers typically don't work. So you should run the
  installer in `udosrun -gui' or DOSBox, and then run the installed programs
  in kvikdos-ng.)

Protected mode support for running 32-bit DOS programs in kvikdos-ng:

* The CPU emulation in KVM runs both 16-bit and 32-bit protected mode
  natively (and even 64-bit long mode would be possible, but no DOS
  program uses it).

* DPMI: kvikdos-ng doesn't have a built-in DPMI server. Instead, it loads an
  external resident DPMI host into the guest (`--dpmi=<dos-pathname>'):
  the host (e.g. CWSDPMI.EXE, HDPMI32.EXE, or a Borland DPMIRES-style
  stub) installs itself as a TSR via int 21h AH=31h, then kvikdos-ng
  preserves low memory and the interrupt vectors and loads the real
  program above it. The program then uses the host's int 2fh AX=1687h
  mode switch and int 31h services as usual.

* Extenders known to work:

  * Phar Lap 386|DOS-Extender (bound `P3' programs, incl. the TNT
    variants): Watcom C/C++ 8.5, 9.5, 10, 10.6 and 11(b) compilers and
    WLINK, the Watcom VI editor, and the Phar Lap SDK tools (TNTLITE,
    TELLME, MEMTEST, REBIND, CFIG386, MARKPHAR, DOSXNT).
  * DPMI hosts CWSDPMI and HDPMI32/HDPMI16 (Microsoft C 7) go resident
    and serve a 32-bit DPMI client correctly.
  * flat assembler 1.73.30 fasmlite.exe.
  * pmode.asm 3.07 example.exe.

* Programs needing DOS4GW, CauseWay, PMODE/W or other extenders may work
  through the `--dpmi=' host mechanism; coverage is incremental.

* Memory available to protected-mode programs: up to 1 GiB guest memory
  (`--mem-mb=<n>', 128 MiB by default), exposed through XMS 3.0 (including
  functions 0x88/0x89 for >64 MiB) and int 15h AH=88h. The conventional
  DOS arena is fully chained per msdos_player semantics (free `Z' tail,
  per-PSP MCB owners), which the extenders' DOS-buffer realloc logic
  relies on.

* There is no VCPI and no built-in DPMI/EMS hardware emulation beyond the
  API surface described above.

Alternatives of kvikdos-ng:

* udosrun (unreleased): A wrapper around pts-fast-dosbox and DOSBox to run
  DOS command-line tools conveniently and directly within the Linux
  terminal. It has fast startup time, because it doesn't open a separate
  window, and it doesn't emulate most of the hardware. It supports 63 MiB of
  memory and it can run 32-bit programs (just like DOSBox).

* DOSBox or DOSBox-X: It can run 32-bit programs, can display graphics,
  emulates lots of hardware (sound card, MIDI, joystick, mouse, CD-ROM etc.)
  Can run many games. It works out of the box, no need to install FreeDOS,
  MS-DOS etc. It supports up to 63 MiB of memory. Disadvantage: it doesn't
  support headless operation or running DOS command-line tools directly
  within the Linux terminal.

* pts-fast-dosbox (unreleased): a fork of DOSBox 0.74 for Linux, optimized
  for fast startup and fast operation (both faster than regular DOSBox, but
  not as fast as kvikdos-ng). By default it doesn't open a separate window, it
  doesn't emulate most of the hardware, and it has an embedded dosbox.conf
  file optimized for (CPU) speed. It also features a command prompt (`C:\>')
  within the Linux terminal. You can use it most conveniently by driving it
  with udosrun (see above).

* EMU2: https://github.com/dmsc/emu2 . It works on the Unix terminal,
  including interactive text-mode DOS programs. It emulates more hardware
  and more DOS functionality than kvikdos-ng. It contains a slow and
  architecture-independent 80286 emulator, without protected mode. It is
  able to run MASM 1.00. It is quite close to kvikdos-ng in the sense that it
  maps DOS standard handles to Unix standard file descriptors (stdin, stdout
  and stderr).

  Unix Makefile example using EMU2 headless:
  https://gist.github.com/dmsc/28d7f4900e7adaf60427a95f9b471813

* MS-DOS Player: https://github.com/cracyc/msdos-player . Uses CPU
  emulation, implements quote a large subset of the MS-DOS API, including
  EMS, XMS, VCPI and long filenames. It can also convert a DOS program to a
  Win32 program by embedding the emulator .exe.

* !! doscmd

* !! dosrun and dosrunner

* !! dosemu (how does it work on amd64?)

  DOSEMU2 also supports KVM: https://github.com/stsp/dosemu2/blob/42bc36c8adee504db92e5de9c6db95633c76813d/src/base/emu-i386/kvm.c

  It would be very interesting to see this supported in DOSBox-X. It would probably enable very good Windows 9x support. DOSEMU2 runs up to Windows 3.x and QEMU with KVM has difficulty with Windows 9x (was quite unstable when I tried it).

  !! Is KVM 64-bit only?

Recommendations on using DOSBox interactively, in a GUI:

* Create a directory, and copy everything relevant (e.g. the DOS program)
  there.

* In your terminal window, cd into that directory.

* Run `dosbox .'.

* Use DOSBox in the appearing (black) window. All programs and files of the
  work directory are there. (Use `dir' to see them.)

* Run `exit' in DOSBox to exit (and destroy the black window).

Future work / TODO:

* Done since the original kvikdos.c TODO list:

  * Run 32-bit (protected-mode) DOS programs: Phar Lap/TNT directly,
    DPMI clients through a resident host (auto-detected, embedded
    HDPMI32 fallback).
  * Run PE32 Win32 console apps through the embedded HX DOS Extender.
  * Run PE executables natively on Windows (WoW64 for 32-bit).
  * More memory: `--mem-mb=' up to 1 GiB, XMS 3.0 and int 15h AH=88h.
  * Run Linux ELF programs and scripts natively.
  * Turbo C compatibility (2.0 validated).
  * Split the single-file source into translation units (~500 lines
    each).

* Still open:

  * Built-in DPMI server (the resident-host model works; a real
    in-emulator server and VCPI remain open).
  * Wider extender coverage: DOS4GW (stub-bound apps hang on timer
    waits), CauseWay, WDOSX — untested; PMODE/W evaluated, rejected
    (exec-loader, not a resident host). Threading apps like HX's
    THREAD.EXE still hang.
  * 16-bit 286 protected-mode programs.
  * Filename mapping exceptions (e.g. `a86 long-filename.8' should
    show LONG-FIL.8 to DOS) and DOSBox-style per-file case fallback
    (try lowercase, then uppercase).
  * Make unp_4.11/unp.exe work.
  * Option to map DOS STDERR to Linux fd 1 (stdout), like nothing else
    does.
  * udosrun integration and udosrun command-line flag compatibility.
  * Graphics modes, mouse, sound and other hardware (use DOSBox if
    needed).
  * Speed measurements for the new protected-mode paths.

* Embed an alternative (but slow) CPU emulator, to make kvikdos-ng
  system-independent (i.e. no KVM) on the host. Example such emulators:

  * https://github.com/ecm-pushbx/8086tiny
  * https://github.com/adriancable/8086tiny  (base of ecm-pushbx/8086tiny)
  * https://github.com/retrohun/8086tiny
  * The CPU emulator of DOSBox. It supports 386, 486, Pentium etc.
  * https://sourceforge.net/projects/fake86/
  * http://ioccc.org/2013/cable3/
  * https://github.com/koutheir/i8086sim
  * https://github.com/DispatchCode/NaTE
  * https://github.com/moesay/Elegant86
  * https://codegolf.stackexchange.com/questions/4732/emulate-an-intel-8086-cpu
  * https://github.com/zsteve/hard86
  * https://github.com/Thraetaona/EXACT (written in WebAssembly)
  * https://github.com/Mati365/ts-c99-compiler (written in TypeScript)

* As an alternative to KVM, use Virtual 8086 mode (vm86(2) system call) on
  Linux i386. Known limitations:

  * It doesn't work if the Linux kernel is targeted to amd64 (i.e. most
    modern systems), because it's not possible to switch back from 64-bit
    mode (long mode) to 32-bit mode, except using virtualization (as done by
    KVM).

  * It won't be able to run protected mode DOS programs (i.e. most 32-bit
    DOS programs), except possibly with a DPMI server. It's not obvious if a
    DPMI server is possible in Linux userspace code (especially the `int
    0x21' remapping within the host process).

  * vm86(2) needs to use the first 1 MiB of the Linux process address space,
    but Linux disallows the mmap(2) of the first 64 KiB for non-root users.
    There may be (slow) workarounds. This effectively redues the memory
    available for the DOS program from 635 KiB to 575 KiB.

__END__
