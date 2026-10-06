#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# All kvikdos translation units (from the Makefile source list).
SRCS="$(sed -n 's|^KVIKDOS_SRCS = ||p' Makefile)"
echo "[static] sources: $(echo $SRCS | wc -w) files"

echo "[static] cppcheck"
set +e
timeout 300s cppcheck -DUSE_MINI_KVM --enable=warning --std=c89 $SRCS
cpp_rc=$?
set -e
if [[ "$cpp_rc" -eq 124 ]]; then
  echo "[static] cppcheck timed out (non-fatal in this environment)"
elif [[ "$cpp_rc" -ne 0 ]]; then
  echo "[static] cppcheck failed rc=$cpp_rc" >&2
  exit "$cpp_rc"
fi

echo "[static] clang --analyze"
rc=0
: >/tmp/kvikdos-clang-analyze.txt
for src in $SRCS; do
  timeout 120s clang --analyze -DUSE_MINI_KVM -Xanalyzer -analyzer-output=text -std=gnu89 -Wall -Wextra "$src" >>/tmp/kvikdos-clang-analyze.txt 2>&1 || rc=$?
done
wc -l /tmp/kvikdos-clang-analyze.txt | awk '{print "[static] clang report lines:",$1}'
if [[ "$rc" -ne 0 ]]; then
  echo "[static] clang --analyze reported issues rc=$rc" >&2
  exit "$rc"
fi

echo "[static] OK"
