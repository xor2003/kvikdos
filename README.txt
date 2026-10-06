kvikdos: a very fast headless DOS emulator for Linux
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
kvikdos is a very fast emulator for running DOS programs on Linux, both
real-mode and 32-bit protected-mode. Typical targets are old compilers,
assemblers and other build tools, plus text-mode IDEs and editors.
kvikdos implements a very small subset of DOS, BIOS and IBM PC harware, so
it can keep the overhead low, so it can be very fast. It uses Linux KVM
under the hood for emulating the CPU, which is also very fast.

kvikdos is free software, GNU GPL >=2.0. There is NO WARRANTY. Use at your risk.

kvikdos should be pronunced as ``quick DOS''. the initials ``kv'' refers to
KVM, the underlying virtualization technology.

Requirements:

* Linux operating system running on i386 (x86, i686) or amd64 (x86_64)
  architecture.
* Hardware virtualization (Intel VT-x or AMD-V) enabled on the host.
  Modern CPUs (even some in 2008) support it.
* KVM enabled for your Linux user (see below).

Limitations:

* kvikdos needs a Linux i386 or amd64 system with KVM. It has been tested
  and found working with both: Linux compiled for i386 and Linux compiled
  for amd64. (Windows and macOS have similar virtualization technologies,
  but kvikdos hasn't been ported to them yet.  Alternatively, on old Linux
  i386 systems, the vm86(2) system call could be used, but that's not
  implemented in kvikdos either.) macOS users should use udosrun instead of
  kvikdos, others should use DOSBox or DOSBox-X.

* kvikdos can run 16-bit real-mode DOS programs (written for the 8086, 186
  or 286 processors, but not 16-bit 286 protected mode), and it can also
  run 32-bit protected-mode DOS programs (written for 386, 486, Pentium
  processors or above) whose extender uses DPMI or runs in protected mode
  directly on the KVM CPU. See the protected mode section below for the
  supported extenders. kvikdos can't run 64-bit programs.

* kvikdos can run a single DOS program at a time. (But you can run multiple
  independent instances of kvikdos in parallel.) Use udosrun (with the
  `-text' or `-gui' flag) or DOSBox if you want to run multiple DOS programs
  after each other or nested.

* The DOS program can use up to 635 KiB of (conventional) memory, including
  the program code and the variables (data and BSS). The 635 KiB excludes:
  environment variables, command-line arguments (in the Program Segment
  Prefix), Interrupt Vector Table, Program Segment Prefix, program stack
  (last 768 bytes), BIOS Data Area, helper code, the user code written for
  Linux.

  On top of conventional memory, kvikdos provides extended memory (XMS 3.0,
  including the >64 MiB functions 0x88/0x89, and the int 15h AH=88h
  interface): 127 MiB by default, configurable with `--mem-mb=<n>' (1 ..
  1024 MiB of total guest memory). A partial EMS (int 67h) implementation
  exists for toolchain probes. There is no VCPI.

  For DPMI, kvikdos loads an external resident DPMI host (e.g. CWSDPMI or
  HDPMI32) into the guest with `--dpmi=<dos-pathname>', then runs the real
  program on top of it; there is no built-in DPMI server.

* kvikdos doesn't support graphics. Use udosrun or DOSBox instead.

* kvikdos has a minimal 80x25 text mode: it renders the text screen
  (colors, cursor position and shape, blinking) on the Linux terminal, and
  supports the BIOS/DOS video and keyboard calls needed by text-mode IDEs
  and editors (e.g. Watcom VI and Turbo Pascal IDEs work). Full-screen
  programs which need fancy video tricks may still misbehave; use udosrun
  or DOSBox for those. kvikdos can also supply line-based input (terminated
  by <Enter>) to programs which don't need the interactive screen.

* kvikdos doesn't emulate any special hardware (e.g. sound card, MIDI,
  joystick, mouse, CD-ROM). Use DOSBox instead.

* kvikdos implements a tiny subset of the DOS ABI (int 21h etc.), PC BIOS
  ABI (int 10h etc.) and IBM PC hardware interfaces (in and out
  instructions). Thus random DOS programs won't work out of the box. Simple
  API calls can be added on the fly to the int*.c sources. However, many famous build
  tools (e.g. compilers and assemblers) released in the 1980s and 1990s
  already work, see the compatibility list below. To get good chances for
  running any random DOS program, use udosrun or DOSBox instead.

* If the target file is Linux-native (ELF or shebang script), kvikdos
  executes it natively with Linux `fork+execvp` instead of DOS emulation.

* If the target file is a Windows executable format (PE/NE/LE/LX), kvikdos
  delegates execution to `wine` automatically.

Features and advantages:

* kvikdos is very fast, it runs CPU-intensive code at almost native speeds
  with Linux KVM. For I/O-intensive code, kvikdos tries to satisfy DOS I/O
  calls using the corresponding Linux system calls (mapping as directly as
  possible), thus it has less overhead than other emulators. See
  benchmark/benchmark.md for a comparison between kvikdos, DOSBox and QEMU.
  For CPU-intensive worklad, kvikdos and QEMU + KVM are on par, each of them
  is about 11.49 times faster than the next emulator. For mixed CPU and I/O
  workload, kvikdos is 4.507 times faster than anything else.

* All features of a modern Intel CPU (such as floating point instructions,
  32-bit registers and 16/32-bit protected mode) are available for DOS
  programs, because the host CPU features are used directly. Guest memory
  is up to 1 GiB (`--mem-mb=<n>', 128 MiB by default).

* Since very little hardware is emulated, kvikdos starts up very quickly,
  it's possible to run dozens of short-lived kvikdos instances per second.

* After installation (and enabling KVM for the Linux user), kvikdos doesn't
  need special privileges (i.e. root or sudo not needed).

* kvikdos integrates DOS command-line tools to the Linux (Unix) command-line:
  its standard input, output etc. can be redirected, it propagatates the DOS
  exit code to Unix, it can pass environment variables to DOS etc. It also
  works very well headless (i.e. without GUI or interactive text UI), e.g.
  as part of continous build pipelines.

* kvikdos runs 32-bit protected-mode DOS programs: Phar Lap
  386|DOS-Extender/TNT bound binaries run directly (Watcom compilers,
  WLINK, VI), and DPMI clients run through an external resident host
  loaded with `--dpmi=' (CWSDPMI, HDPMI32). Extended memory is provided
  via XMS 3.0 and int 15h AH=88h on top of the conventional arena.

* kvikdos has a minimal 80x25 text mode for IDEs and editors (colors,
  cursor position/shape/blink, Alt-aware keyboard), rendered on the Linux
  terminal.

* kvikdos runs DOS batch files (.bat) with `set`, `%VAR%`, `%1..%9`,
  `shift`, `call`, `if`, `goto`, `mkdir`, `copy`, `del` etc., enough for
  compiler driver scripts.

How to install kvikdos:

* (You don't have to install QEMU, kvikdos doesn't need it.)

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

* Download the precompiled kvikdos binary (kvikdos.linux.i386). Rename the
  file to kvikdos, and make it executable:

    $ mv kvikdos.linux.i386 kvikdos
    $ chmod 755 kvikdos

* Download a test DOS program (guest.com) to the same directory, and run it:

    $ ./kvikdos guest.com
    .
    Hello, World!

  If it displays the dot and the `Hello, World!' message, then it's OK.

Command-line options (structured):

General:

* `--kvm-check' checks KVM only.
* `--strict' enables strict unsupported-interrupt behavior.
* `--permissive' enables compatibility fallback behavior (default).

DOS runtime:

* `--toolchain=<name>' applies toolchain presets:
  `msc4', `msc5', `msc6', `masm5', `bc2', `bcpp1', `bc5', `ic86'.
* `--env=<NAME>=<value>' adds one DOS environment variable.
* `--env-file=<file>' loads DOS environment variables from file
  (`NAME=VALUE' lines).
* `--path-dos=<pathlist>' sets DOS PATH explicitly.
* `--prog=<dos-pathname>' sets DOS pathname of the running program.
* `--cwd-dos=<path>' sets initial DOS current directory.

Mounts:

* `--mount=<drive><case><dirname>/' mounts Linux directory to DOS drive.
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

* kvikdos emulates DOS drives A: .. F: by exposing directories on the Linux
  filesystem as mount points for these DOS drives.

* Use the `--mount=<drive><case><dirname>/' command-line flag to make
  DOS drive <drive>: point to Linux directory named <dirname>. Use `:' as
  <case> for uppercase (see below), and use `-' for lowercase.

* Use the `--mount=<drive>0' command-line flag to make <drive>: invisible
  to DOS programs. This is useful to override some default mounts.

* Use the `--mount=<drive><case>' command-line flag to override case folding
  for default mounts.

* The following drives are visible to DOS by default (i.e. default mounts):

  * C: points to the current directory (.) of the kvikdos Linux process.

  * D: points to the directory containing the kvikdos executable program
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
  insensitive. Thus if a DOS program wants to open or access a file, kvikdos
  has to decide how to case fold the letters in the pathname.

* Only unaccented Latin letters a .. z (and A .. Z) are targets of case
  folding. International characters (typically with code >= 128) are kept
  intact.

* When the DOS program tries to open or access a file, kvikdos generates an
  uppercase or lowercase Linux filename based on the mount flags of the
  emulated drive the DOS file is on. (DOSBox does it differently: on a per
  file basis, it uses uppercase iff the lowercase variant doesn't already
  exists on the filesystem.)

* To specify an uppercase drive, use the `--mount=<drive>:<dirname>' flag.
  To specify a lowercase drive, replace the `:' with `-' above. For
  example, to mount the current Linux directory as C: lowercase,
  specify `--mount=C-' .

* For drives C:, D: and E:, if not explicitly specified as
  `--mount=<drive>...', kvikdos autodetects lowercase based on the
  on the <dos-executable-file>: if there is at least
  one lowercase character, the drive becomes lowercase. For drives D: and E:
  only the last pathname component is considered, for C:, if
  <dos-executable-file> is under C:, then the entire pathname is considered.
  To override autodetection, specify e.g. `--mount=E:' for uppercase and
  `-mount=E-' for lowercase.

Software compatibility, i.e. DOS programs known to work in kvikdos:

Quick start for DOS compilers and assemblers:

* For practical, copy-paste command lines, see TOOLCHAIN.md in this
  repository.

* Recommended layout rules:
  * Use 8.3 filenames for source/object/output files (e.g. HELLO.C).
  * If a toolchain has BIN/LIB/INCLUDE directories, mount the toolchain
    root as C: and run the executable from C:\BIN\...
  * Set DOS environment explicitly with --env=LIB=... and
    --env=INCLUDE=...

* Recently validated under kvikdos (compile, link, run of a tiny HELLO):
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
  mode, which kvikdos doesn't support. It reads OMF .obj and .lib files,
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
  in kvikdos, even the hello-world doesn't work yet.

* Microsoft BASIC Professional Development System compiler 7.10 bc.exe and
  the corresponding linker link.exe. The compiler produces OMF .obj files,
  and the linker reads OMF .obj and .lib files, and it produces .exe program
  files.

  Please note that the generated .exe files are correct (they are identical
  to those generated in DOSBox by the same tools), but they don't work yet
  in kvikdos, even the hello-world doesn't work yet.

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

* Turbo C, Turbo C++ and Borland C++ compilers haven't been tested.
  (TODO(pts): Test them.)

* (TODO(pts): Add tutorial for compiling hello-world with the tools above.)

* (Please note that installers typically don't work. So you should run the
  installer in `udosrun -gui' or DOSBox, and then run the installed programs
  in kvikdos.)

Protected mode support for running 32-bit DOS programs in kvikdos:

* The CPU emulation in KVM runs both 16-bit and 32-bit protected mode
  natively (and even 64-bit long mode would be possible, but no DOS
  program uses it).

* DPMI: kvikdos doesn't have a built-in DPMI server. Instead, it loads an
  external resident DPMI host into the guest (`--dpmi=<dos-pathname>'):
  the host (e.g. CWSDPMI.EXE, HDPMI32.EXE, or a Borland DPMIRES-style
  stub) installs itself as a TSR via int 21h AH=31h, then kvikdos
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

Alternatives of kvikdos:

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
  not as fast as kvikdos). By default it doesn't open a separate window, it
  doesn't emulate most of the hardware, and it has an embedded dosbox.conf
  file optimized for (CPU) speed. It also features a command prompt (`C:\>')
  within the Linux terminal. You can use it most conveniently by driving it
  with udosrun (see above).

* EMU2: https://github.com/dmsc/emu2 . It works on the Unix terminal,
  including interactive text-mode DOS programs. It emulates more hardware
  and more DOS functionality than kvikdos. It contains a slow and
  architecture-independent 80286 emulator, without protected mode. It is
  able to run MASM 1.00. It is quite close to kvikdos in the sense that it
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

Future work:

* Embed an alternative (but slow) CPU emulator, to make kvikdos
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

!! Speed measurements.

__END__
