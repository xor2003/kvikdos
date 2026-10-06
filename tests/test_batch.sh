#!/usr/bin/env bash
set -euo pipefail

KVD="${1:-./kvikdos}"
KVD="$(readlink -f "$KVD")"
if [[ ! -x "$KVD" ]]; then
 echo "error: kvikdos binary not found or not executable: $KVD" >&2
 exit 2
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

normalize() {
 # DOS text may contain CR or occasional NUL in buggy paths.
 tr -d '\r' | tr -d '\000' | sed -E '/^C:\\>/d;/^$/d'
}

run_case() {
 local name="$1"
 local bat="$2"
 local expected="$3"
 shift 3
 local out
 out="$($KVD "$bat" "$@" | normalize)"
 if [[ "$out" != "$expected" ]]; then
   echo "FAIL: $name" >&2
   echo "--- expected ---" >&2
   printf '%s\n' "$expected" >&2
   echo "--- got ---" >&2
   printf '%s\n' "$out" >&2
   return 1
 fi
 echo "PASS: $name"
 return 0
}

run_case_unordered_lines() {
 local name="$1"
 local bat="$2"
 local expected="$3"
 shift 3
 local out out_sorted exp_sorted
 out="$($KVD "$bat" "$@" | normalize)"
 out_sorted="$(printf '%s\n' "$out" | LC_ALL=C sort)"
 exp_sorted="$(printf '%s\n' "$expected" | LC_ALL=C sort)"
 if [[ "$out_sorted" != "$exp_sorted" ]]; then
   echo "FAIL: $name" >&2
   echo "--- expected ---" >&2
   printf '%s\n' "$expected" >&2
   echo "--- got ---" >&2
   printf '%s\n' "$out" >&2
   return 1
 fi
 echo "PASS: $name"
 return 0
}

cat > "$TMP/t.bat" <<'BAT'
@echo off
set FOO=BAR
echo %FOO%
if "%FOO%"=="BAR" goto ok
echo BAD1
goto end
:ok
mkdir X
copy t.bat X\C.BAT
if exist X\C.BAT echo COPIED
del X\C.BAT
if not exist X\C.BAT echo DELETED
:end
exit 0
BAT

cat > "$TMP/args.bat" <<'BAT'
@echo off
echo %1
echo %2
shift
echo %1
call echo CALL-%1
exit 0
BAT

cat > "$TMP/cond.bat" <<'BAT'
@echo off
set TARGET=DOS
if NOT "%1" == "" goto first
echo USAGE
goto end
:first
if "%1"=="DOS" echo DOS-OK
if not "%1"=="DOS" echo DOS-BAD
if %TARGET%==DOS echo TARGET-OK
if %TARGET%==OS2 echo TARGET-BAD
:end
exit 0
BAT

cat > "$TMP/errlvl.bat" <<'BAT'
@echo off
if errorlevel 1 echo BAD0
if not errorlevel 1 echo OK0
if errorlevel 0 echo OK1
if errorlevel 1 goto fail
if not errorlevel 1 goto done
:fail
echo BAD1
goto end
:done
echo DONE
:end
exit 0
BAT

cat > "$TMP/pathvar.bat" <<'BAT'
@echo off
set PATH=C:\BIN;%PATH%;
if "%PATH%"=="C:\BIN;%PATH%;" echo BAD
echo %PATH%
exit 0
BAT

cat > "$TMP/redir.bat" <<'BAT'
@echo off
echo AA>out.txt
echo BB>>out.txt
type out.txt
exit 0
BAT

cat > "$TMP/setlocal.bat" <<'BAT'
@echo off
set FOO=OUT
setlocal
set FOO=IN
echo %FOO%
endlocal
echo %FOO%
exit 0
BAT

cat > "$TMP/redir2.bat" <<'BAT'
@echo off
type in.txt > out2.txt
type out2.txt
type no_such_file.txt 2> err.txt
type err.txt
exit 0
BAT

cat > "$TMP/forpipe.bat" <<'BAT'
@echo off
for %%M in (A B C) do echo %%M
echo P1|type
exit 0
BAT

cat > "$TMP/delwild.bat" <<'BAT'
@echo off
echo X>a1.tmp
echo X>a2.tmp
del a?.tmp
if not exist a1.tmp echo D1
if not exist a2.tmp echo D2
exit 0
BAT

cat > "$TMP/copywild.bat" <<'BAT'
@echo off
mkdir dst
echo X>w1.exe
echo Y>w2.exe
copy w?.exe dst
if exist dst\w1.exe echo C1
if exist dst\w2.exe echo C2
exit 0
BAT

cat > "$TMP/ifexistwild.bat" <<'BAT'
@echo off
echo X>p1.tmp
if exist p?.tmp echo IW
del p1.tmp
exit 0
BAT

cat > "$TMP/typewild.bat" <<'BAT'
@echo off
echo A>ta.lst
echo B>tb.lst
type t?.lst
exit 0
BAT

cat > "$TMP/fdredir.bat" <<'BAT'
@echo off
echo ERR1 1>out1.txt
echo ERR2 2>out2.txt
type out1.txt
type out2.txt
exit 0
BAT

cat > "$TMP/gotoeof.bat" <<'BAT'
@echo off
echo BEFORE
goto :eof
echo AFTER
BAT

pushd "$TMP" >/dev/null
fails=0
run_case "env-if-goto-copy-del" "$TMP/t.bat" $'BAR\nCOPIED\nDELETED' || fails=$((fails + 1))
run_case "args-shift-call" "$TMP/args.bat" $'ONE\nTWO\nTWO\nCALL-TWO' ONE TWO || fails=$((fails + 1))
run_case "if-not-and-eq" "$TMP/cond.bat" $'USAGE' || fails=$((fails + 1))
run_case "if-not-and-eq-with-arg" "$TMP/cond.bat" $'DOS-OK\nTARGET-OK' DOS || fails=$((fails + 1))
run_case "if-errorlevel" "$TMP/errlvl.bat" $'OK0\nOK1\nDONE' || fails=$((fails + 1))
run_case "set-percent-var-expansion" "$TMP/pathvar.bat" $'C:\\BIN;;' || fails=$((fails + 1))
run_case "redir-out-append" "$TMP/redir.bat" $'AA\nBB' || fails=$((fails + 1))
run_case "setlocal-endlocal" "$TMP/setlocal.bat" $'IN\nOUT' || fails=$((fails + 1))
printf 'IN-LINE\n' > "$TMP/in.txt"
run_case "redir-in-and-stderr" "$TMP/redir2.bat" $'IN-LINE\nFile not found - no_such_file.txt' || fails=$((fails + 1))
run_case "for-and-pipe" "$TMP/forpipe.bat" $'echo A\nA\necho B\nB\necho C\nC\nP1' || fails=$((fails + 1))
run_case "del-wildcard" "$TMP/delwild.bat" $'D1\nD2' || fails=$((fails + 1))
run_case "copy-wildcard" "$TMP/copywild.bat" $'C1\nC2' || fails=$((fails + 1))
run_case "if-exist-wildcard" "$TMP/ifexistwild.bat" $'IW' || fails=$((fails + 1))
run_case_unordered_lines "type-wildcard" "$TMP/typewild.bat" $'A\nB' || fails=$((fails + 1))
run_case_unordered_lines "fd-redirection" "$TMP/fdredir.bat" $'ERR1 \nERR2 ' || fails=$((fails + 1))
run_case "goto-eof" "$TMP/gotoeof.bat" $'BEFORE' || fails=$((fails + 1))
popd >/dev/null

if [[ "$fails" -ne 0 ]]; then
 echo "Batch tests failed: $fails case(s)." >&2
 exit 1
fi
echo "All batch tests passed."
