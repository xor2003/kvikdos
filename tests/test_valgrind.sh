#!/usr/bin/env bash
set -euo pipefail

KVD="${1:-./kvikdos}"
KVD="$(readlink -f "$KVD")"
if [[ ! -x "$KVD" ]]; then
  echo "error: kvikdos binary not found: $KVD" >&2
  exit 2
fi

CCROOT="${CCROOT:-/home/xor/inertia_player/dos_compilers}"

cat > "$CCROOT/Microsoft C v5/HELLO.C" <<'EOF'
int main(){return 0;}
EOF
cat > "$CCROOT/Microsoft C v5.1/bin/HELLO.C" <<'EOF'
int main(){return 0;}
EOF
cat > "$CCROOT/Microsoft C v6ax/BIN/HELLO.C" <<'EOF'
int main(){return 0;}
EOF

run_vg() {
  local name="$1"; shift
  local log="/tmp/${name}.log"
  echo "[vg] $name"
  set +e
  timeout 90s valgrind --tool=memcheck --leak-check=full --errors-for-leak-kinds=definite --error-exitcode=101 "$@" >"$log" 2>&1
  local rc=$?
  set -e
  echo "[vg] rc=$rc"
  grep -E "ERROR SUMMARY|definitely lost|invalid read|Invalid write" "$log" || true
  sed -n '1,14p' "$log"
}

(
  cd "$CCROOT/Microsoft C v5"
  run_vg vg_msc5 "$KVD" /home/xor/inertia_player/dos_compilers/Microsoft\ C\ v5/CL.EXE /c HELLO.C
  run_vg vg_link5 "$KVD" /home/xor/inertia_player/dos_compilers/Microsoft\ C\ v5/LINK.EXE
)
(
  cd "$CCROOT/Microsoft C v5.1/bin"
  run_vg vg_msc51 "$KVD" /home/xor/inertia_player/dos_compilers/Microsoft\ C\ v5.1/bin/CL.EXE /c HELLO.C
)
(
  cd "$CCROOT/Microsoft C v6ax/BIN"
  run_vg vg_msc6 "$KVD" CL.EXE /c HELLO.C
)

echo "[vg] done"
