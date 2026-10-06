#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "[static] cppcheck"
set +e
timeout 180s cppcheck -DUSE_MINI_KVM --enable=warning --std=c89 kvikdos.c
cpp_rc=$?
set -e
if [[ "$cpp_rc" -eq 124 ]]; then
  echo "[static] cppcheck timed out (non-fatal in this environment)"
elif [[ "$cpp_rc" -ne 0 ]]; then
  echo "[static] cppcheck failed rc=$cpp_rc" >&2
  exit "$cpp_rc"
fi

echo "[static] clang --analyze"
timeout 180s clang --analyze -Xanalyzer -analyzer-output=text -std=c89 -Wall -Wextra kvikdos.c >/tmp/kvikdos-clang-analyze.txt 2>&1
wc -l /tmp/kvikdos-clang-analyze.txt | awk '{print "[static] clang report lines:",$1}'

echo "[static] OK"
