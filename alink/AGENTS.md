# Repository Notes

## Build

- `make` builds the Linux host binary `alink`
- `make sanitizers` builds `alink.san` with AddressSanitizer and UBSan

## Checks

- `make format-check`
- `make tidy`
- `make cppcheck`
- `make analyze`
- `make valgrind`
- `make ci`

## Conventions

- Source files use uppercase DOS-era filenames and must be compiled as C, not C++
- Linux path handling must continue to accept both `/` and `\`
- Compiler warnings are treated as errors in the maintained build targets

## MS LINK compatibility semantics

- `REL_EXTFRAME` (and target-determined frames collapsed to it) resolve
  against the **group base** when the extern's segment is a group member
  — not the member segment's own base (`OUTPUT.C GetFixupTarget`,
  `FindSegGroup` fallback). 16-bit offsets whose group-relative target
  exceeds 64K are truncated like LINK (warn, keep low 16 bits).
- `matchComDefs` (ALINK.C) puts near COMDEFs in the synthetic `COMDEFS`
  segment added to `DGROUP` and records `pubdef->grpnum` so group-relative
  frame resolution applies; far COMDEFs stay ungrouped.
- Debug env vars: `ALINK_DEBUG_ALLRELOC` (all reloc records + resolved
  targets), `RELOC`, `COMDEF`, `MERGE`, `PUB`, `OVERWRITE`, `IO`,
  `MODULE`, `SEGMENT`, `HEADER`, `ORDER`.

## F-14 validation

`alink -c-` link of the F14GOLD object set (dev `c6` MSC6 toolchain +
`MLIBCA.LIB`/`MPSLIBM.LIB`) produces an EXE whose runtime behavior under
`kvikdos` matches the Microsoft LINK build exactly: identical
`dos_open` sequence (44 calls through `NC07F.WLD` + `F14MD01.DAT`),
identical int21 trace, identical exit status. See
`/home/xor/games/f14src/analysis/README.md` for the comparison setup.
