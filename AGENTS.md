# kvikdos agent rules

## Mandatory static-analysis gate

After ANY change to a `.c` or `.h` file, ALWAYS run the lint gate before
committing:

    make lint        # == ./tests/test_static.sh

It runs four tools over every translation unit in `KVIKDOS_SRCS`:

- **cppcheck** (`--enable=warning --std=c89`)
- **clang-check** (parse/AST per TU — catches e.g. typedef redefinitions)
- **clang-tidy** (`clang-analyzer-*`; the advisory classes
  `security.insecureAPI.*` and `deadcode.DeadStores` are excluded — fix or
  NOLINT anything else)
- **clang --analyze** (hard errors gate; warnings are report-only)

If a finding is a true positive, fix the code. If it is a false positive,
use a `/* NOLINT(check-name): reason */` comment (clang-tidy) or fix the
pattern (cppcheck) — do not weaken the gate to hide diagnostics.

## Build and test

- `make kvikdos` — strict C90 (`-ansi -pedantic -W -Wall -Wextra -Werror`).
  Keep new code C89-clean: no `//` comments, no mixed decls, no `long long`.
- `make test` — functional suite (requires /dev/kvm).
- `make test-x86dec` — decoder unit test.
- `make test-sanitizers` — gcc+clang ASan/UBSan (UBSan reports misaligned
  guest-memory accesses; those are inherent to a DOS emulator).
- `make kvikdos.ape` — Cosmopolitan APE (Linux KVM + Windows WHPX in one
  binary); set `COSMOCC`/`COOBJCOPY` to the cosmo tools
  (`x86_64-unknown-cosmo-cc` + `x86_64-linux-cosmo-objcopy`). The linker
  output is a host ELF wrapping the APE image; objcopy `-O binary`
  flattens it into the real MZqFpD portable executable.
- `make dist` — release staging: the APE is the shipped executable, staged
  as `dist/kvikdos` (Linux) + `dist/kvikdos.com` (Windows).

## Conventions

- Source files target ~500 lines; add a new translation unit to
  `KVIKDOS_SRCS` rather than growing a file past the limit.
- Borrow DOS semantics from the reference implementations under `borrow/`
  (msdos_player for console/BIOS fidelity, ntvdm for compiler-tool
  compatibility, vDos for the broader int10 surface, msdos4 for kernel
  semantics) rather than inventing behavior.
- Guest-memory access is intentionally unaligned/packed — do not "fix" it
  for sanitizer cleanliness.
