#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CCROOT="${CCROOT:-/home/xor/inertia_player/dos_compilers}"
cd "$ROOT"

cat > HELLO.C <<'EOF'
int main(){return 0;}
EOF

echo "[san] build gcc asan+ubsan"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-strict-aliasing -o kvikdos_gcc_san kvikdos.c
echo "[san] build clang asan+ubsan"
clang -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-strict-aliasing -o kvikdos_clang_san kvikdos.c

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
