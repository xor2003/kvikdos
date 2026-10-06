#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CCROOT="${CCROOT:-/home/xor/inertia_player/dos_compilers}"
cd "$ROOT"

SRCS="$(sed -n 's|^KVIKDOS_SRCS = ||p' Makefile)"

echo "[san] build gcc asan+ubsan"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-strict-aliasing -o kvikdos_gcc_san $SRCS
echo "[san] build clang asan+ubsan"
clang -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-strict-aliasing -o kvikdos_clang_san $SRCS

if [[ -x ./kvikdos_gcc_san ]]; then
  echo "[san] self-test: gcc-san binary runs the repo test suite"
  ./tests/test_cli_matrix.sh ./kvikdos_gcc_san
fi

if [[ ! -d "$CCROOT" ]]; then
  echo "[san] CCROOT=$CCROOT not present; skipping DOS-compiler smoke" >&2
  exit 0
fi

cat > HELLO.C <<'EOF'
int main(){return 0;}
EOF
cp HELLO.C "$CCROOT/Microsoft C v5/HELLO.C"

echo "[san] run gcc sanitizer smoke (MSC5)"
./kvikdos_gcc_san --toolchain=msc5 "$CCROOT/Microsoft C v5/CL.EXE" /c HELLO.C >/tmp/kd_san_gcc.txt 2>&1 || true
echo "[san] run clang sanitizer smoke (MSC5)"
./kvikdos_clang_san --toolchain=msc5 "$CCROOT/Microsoft C v5/CL.EXE" /c HELLO.C >/tmp/kd_san_clang.txt 2>&1 || true

echo "[san] gcc sanitizer key lines"
grep -E "AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:" /tmp/kd_san_gcc.txt || true
echo "[san] clang sanitizer key lines"
grep -E "AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:" /tmp/kd_san_clang.txt || true

echo "[san] done (check logs above for UBSan alignment diagnostics)"
