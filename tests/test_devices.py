#!/usr/bin/env python3
"""Exercise port-poll IRQs, chained keyboard IRQs, and ANSI/PNG frames."""
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time
import tty

ROOT = Path(__file__).resolve().parent.parent
EMU = str(Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "kvikdos").resolve())


def assemble(name, directory, defines=()):
    out = Path(directory) / (name + "-" + "-".join(defines) + ".com")
    subprocess.run(["nasm", "-f", "bin", str(ROOT / "tests" / (name + ".nasm")), "-o", str(out), *("-D" + d for d in defines)], check=True)
    return str(out)


def keyboard(program):
    master, slave = pty.openpty()
    tty.setraw(slave)
    p = subprocess.Popen([EMU, "--video=off", Path(program).name], cwd=Path(program).parent, stdin=slave, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    os.close(slave)
    try:
        os.write(master, b"x")
        data = b""
        deadline = time.monotonic() + 5
        while b"IRQKEYS-READY" not in data and time.monotonic() < deadline:
            if select.select([p.stdout], [], [], .1)[0]:
                chunk = os.read(p.stdout.fileno(), 65536)
                if not chunk:
                    break
                data += chunk
        assert b"IRQKEYS-READY" in data, data
        os.write(master, b"\x1b[A\r\x1b")
        out, err = p.communicate(timeout=5)
        assert p.returncode == 0, (p.returncode, data + out, err)
    finally:
        if p.poll() is None:
            p.kill()
        p.wait()
        os.close(master)


def video(program, directory, cols, rows):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    png = str(Path(directory) / "frame.png")
    p = subprocess.Popen([EMU, "--video=ansi", "--video-dump=" + png, Path(program).name], cwd=Path(program).parent, stdin=slave, stdout=slave, stderr=subprocess.PIPE)
    os.close(slave)
    data = b""
    deadline = time.monotonic() + 5
    try:
        while time.monotonic() < deadline:
            if select.select([master], [], [], .1)[0]:
                try:
                    chunk = os.read(master, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                data += chunk
            elif p.poll() is not None:
                break
        assert p.wait(timeout=1) == 0, p.stderr.read()
    finally:
        if p.poll() is None:
            p.kill()
        p.wait()
        os.close(master)
    text = data.decode("utf-8")
    row = col = 1
    painted = 0
    for token in re.findall(r"\x1b\[[0-9;?<> ]*[A-Za-z]|[^\x1b]", text):
        if token.startswith("\x1b["):
            if token.endswith("H"):
                coords = token[2:-1].split(";")
                row = int(coords[0] or 1)
                col = int(coords[1] or 1) if len(coords) > 1 else 1
                assert row <= rows and col <= cols, (row, col, cols, rows)
        elif token == "▀":
            assert row <= rows and col <= cols, (row, col, cols, rows)
            col += 1
            painted += 1
    assert painted, text
    assert "38;2;170;0;0m" in text, "top half missing"
    assert "48;2;0;0;170m" in text, "bottom half missing"
    header = Path(png).read_bytes()
    assert header[:8] == b"\x89PNG\r\n\x1a\n"
    assert struct.unpack(">II", header[16:24]) == (320, 200)


with tempfile.TemporaryDirectory(prefix="kvikdos-devices-") as directory:
    wrap = assemble("a20wrap", directory)
    subprocess.run([EMU, "--video=off", Path(wrap).name], cwd=directory, check=True, timeout=5, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    poll = assemble("irqpoll", directory)
    subprocess.run([EMU, "--video=off", Path(poll).name], cwd=directory, check=True, timeout=5, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    for defines in ((), ("DOS_WAIT",), ("DOS_POLL",)):
        wait = assemble("irqwait", directory, defines)
        subprocess.run([EMU, "--video=off", Path(wait).name], cwd=directory, stdin=subprocess.DEVNULL, check=True, timeout=5, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    keyboard(assemble("irqkeys", directory))
    frame = assemble("vgafit", directory)
    for size in ((80, 24), (160, 50), (40, 12), (200, 20), (1, 1)):
        video(frame, directory, *size)
print("PASS: A20 alias/toggles, PIT rate, IRQ0 during BIOS/DOS waits and polling, chained IRQ1, escape deadline, ANSI fitting, PNG")
