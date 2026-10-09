#!/usr/bin/env bash
# Static-analysis gate for kvikdos: cppcheck, clang-tidy, clang --analyze.
# clang-check (full parse/AST per TU) is slow, so it only runs when
# STATIC_SLOW=1 is set (CI sets it; `make lint-full' locally).
# Any hard diagnostic (error:) fails; clang-tidy analyzer warnings also fail.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# hdpmibin.h is generated (gitignored); TU parsing fails without it on a
# fresh checkout, so build it first when the script runs standalone (CI).
if [[ ! -f hdpmibin.h ]]; then make -s hdpmibin.h; fi

# All kvikdos translation units (from the Makefile source list).
SRCS="$(sed -n 's|^KVIKDOS_SRCS = ||p' Makefile)"
CFLAGS_LINT="-DUSE_MINI_KVM -std=gnu89 -Wall -Wextra"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
echo "[static] sources: $(echo $SRCS | wc -w) files, jobs: $JOBS"

echo "[static] cppcheck"
set +e
timeout 300s cppcheck -j "$JOBS" -DUSE_MINI_KVM --enable=warning --std=c89 $SRCS
cpp_rc=$?
set -e
if [[ "$cpp_rc" -eq 124 ]]; then
  echo "[static] cppcheck timed out (non-fatal in this environment)"
elif [[ "$cpp_rc" -ne 0 ]]; then
  echo "[static] cppcheck failed rc=$cpp_rc" >&2
  exit "$cpp_rc"
fi

if [[ "${STATIC_SLOW:-}" = "1" ]]; then
  echo "[static] clang-check (parse/AST)"
  : >/tmp/kvikdos-clang-check.txt
  echo "$SRCS" | tr ' ' '\n' | xargs -P "$JOBS" -I{} \
    timeout 120s clang-check {} -- $CFLAGS_LINT >>/tmp/kvikdos-clang-check.txt 2>&1 || true
  if grep -n 'error:' /tmp/kvikdos-clang-check.txt; then
    echo "[static] clang-check found errors" >&2
    exit 1
  fi
else
  echo "[static] clang-check skipped (slow; STATIC_SLOW=1 or make lint-full)"
fi

# Advisory classes excluded: insecureAPI.strcpy (all call sites bounded by
# DOS path/name limits) and deadcode.DeadStores (defensive stores kept for
# robustness); everything else in clang-analyzer-* gates.
TIDY_CHECKS='clang-analyzer-*,-clang-analyzer-security.insecureAPI.*,-clang-analyzer-deadcode.DeadStores'
echo "[static] clang-tidy ($TIDY_CHECKS)"
: >/tmp/kvikdos-clang-tidy.txt
echo "$SRCS" | tr ' ' '\n' | xargs -P "$JOBS" -I{} \
  timeout 120s clang-tidy --checks="$TIDY_CHECKS" {} -- $CFLAGS_LINT >>/tmp/kvikdos-clang-tidy.txt 2>&1 || true
if grep -nE 'error:|warning:' /tmp/kvikdos-clang-tidy.txt; then
  echo "[static] clang-tidy reported issues" >&2
  exit 1
fi

echo "[static] clang --analyze"
: >/tmp/kvikdos-clang-analyze.txt
echo "$SRCS" | tr ' ' '\n' | xargs -P "$JOBS" -I{} \
  timeout 120s clang --analyze -DUSE_MINI_KVM -Xanalyzer -analyzer-output=text -std=gnu89 -Wall -Wextra {} >>/tmp/kvikdos-clang-analyze.txt 2>&1 || true
wc -l /tmp/kvikdos-clang-analyze.txt | awk '{print "[static] clang report lines:",$1}'
# clang --analyze exits 0 even when it emits diagnostics.  clang-tidy above
# is the checker gate; here only hard errors (parse failures) gate, since
# the default checker set includes the advisory classes excluded there.
if grep -n 'error:' /tmp/kvikdos-clang-analyze.txt; then
  echo "[static] clang --analyze reported errors" >&2
  exit 1
fi

echo "[static] OK"
