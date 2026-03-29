#!/usr/bin/env python3
"""Serial monitor for the STM32F407 storage showcase, with crash-dump decoding.

Similar to esp-idf's exception decoder: streams UART output live, and when a
crash dump appears (the fault handler's diagnostic printout), automatically
runs addr2line against the target's ELF to show the exact function, source
line, and disassembly for each faulting address.

    python3 tools/serial_monitor.py --target freertos
    python3 tools/serial_monitor.py --target threadx -p /dev/ttyUSB0
    python3 tools/serial_monitor.py -e build_zephyr/zephyr/zephyr.elf
    python3 tools/serial_monitor.py --list-ports
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from _toolchain import arm_tool  # noqa: E402

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial not installed. Run: pip install pyserial", file=sys.stderr)
    sys.exit(1)

CONTEXT_LINES = 5

TARGET_ELF = {
    "freertos": REPO_ROOT / "targets" / "freertos" / "Debug" / "stm32f407.elf",
    "threadx": REPO_ROOT / "targets" / "threadx" / "Debug" / "stm32f407.elf",
    "zephyr": REPO_ROOT / "build_zephyr" / "zephyr" / "zephyr.elf",
}

# Where application/vendor source lives - searched by basename since
# addr2line only gives us the compile-time path, which won't exist on this
# machine for any file that came from outside the repo (e.g. Zephyr's own
# west workspace).
SOURCE_SEARCH_DIRS = [REPO_ROOT / "app", REPO_ROOT / "external", REPO_ROOT / "targets", REPO_ROOT]

CRASH_START_MARKERS = ("HARD FAULT DETECTED", "FAULT DETECTED", "CRASH DETECTED")
CRASH_END_MARKERS = ("Halting", "halting", "reset")
ADDRESS_LINE_RE = re.compile(r"(PC|LR|Fault PC|Fault LR)\s*=\s*0x([0-9A-Fa-f]+)")


def list_serial_ports():
    return list(serial.tools.list_ports.comports())


def guess_serial_port():
    """Pick the first likely-looking serial port when none was given explicitly."""
    ports = list_serial_ports()
    for p in ports:
        if "ttyUSB" in p.device or "ttyACM" in p.device or p.device.upper().startswith("COM"):
            return p.device
    return ports[0].device if ports else None


def find_source_file(filename):
    if Path(filename).exists():
        return Path(filename)
    basename = Path(filename).name
    for base in SOURCE_SEARCH_DIRS:
        if not base.exists():
            continue
        match = next(base.rglob(basename), None)
        if match:
            return match
    return None


def read_source_context(filepath, line_num, context=CONTEXT_LINES):
    try:
        lines = Path(filepath).read_text(errors="replace").splitlines()
    except OSError as exc:
        return [f"  (could not read source: {exc})"]

    start = max(0, line_num - context - 1)
    end = min(len(lines), line_num + context)
    result = []
    for i in range(start, end):
        line_no = i + 1
        marker = ">>>" if line_no == line_num else "   "
        result.append(f"  {marker} {line_no:4d}: {lines[i].rstrip()}")
    return result


def addr2line(elf_path, address):
    try:
        result = subprocess.run(
            [arm_tool("addr2line"), "-e", str(elf_path), "-f", "-C", address],
            capture_output=True, text=True, timeout=5,
        )
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None, None, None

    if result.returncode != 0:
        return None, None, None

    lines = result.stdout.strip().splitlines()
    if len(lines) < 2 or ":" not in lines[1] or "?" in lines[1]:
        return None, None, None

    func = lines[0]
    filepath, _, line_str = lines[1].replace("\\", "/").rpartition(":")
    try:
        return func, filepath, int(line_str)
    except ValueError:
        return None, None, None


def disassemble_around(elf_path, address, window=8):
    """Disassemble a few instructions around `address`, generated on the fly."""
    try:
        addr_int = int(address, 16)
    except ValueError:
        return None

    start, end = max(0, addr_int - window), addr_int + window
    try:
        result = subprocess.run(
            [arm_tool("objdump"), "-d", "--no-show-raw-insn",
             f"--start-address=0x{start:x}", f"--stop-address=0x{end:x}", str(elf_path)],
            capture_output=True, text=True, timeout=5,
        )
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None
    if result.returncode != 0:
        return None

    target_prefix = f"{addr_int:x}:"
    out = []
    for line in result.stdout.splitlines():
        stripped = line.strip()
        # Only real "<hex addr>:\t<instruction>" lines - skip the file
        # format / section header noise objdump prints first.
        if not re.match(r"^[0-9a-fA-F]+:\s", stripped):
            continue
        marker = ">>>" if stripped.lower().startswith(target_prefix) else "   "
        out.append(f"  {marker} {stripped}")
    return out or None


class CrashDecoder:
    """Decodes and prints source context for each new address seen in one crash dump."""

    def __init__(self, elf_path):
        self.elf_path = elf_path
        self.decoded = set()

    def reset(self):
        self.decoded.clear()

    def decode_and_show(self, reg_name, address):
        if not self.elf_path or address in self.decoded:
            return
        self.decoded.add(address)

        func, filepath, line_num = addr2line(self.elf_path, address)
        if not func or "?" in func:
            print(f"         [{reg_name}: could not decode {address}]")
            return

        print(f"\n         --- {reg_name}: {func}() ---")
        print(f"         {filepath}:{line_num}")

        source_file = find_source_file(filepath)
        if source_file:
            print("         Source:")
            for line in read_source_context(source_file, line_num):
                print(line)

        disasm = disassemble_around(self.elf_path, address)
        if disasm:
            print("         Disassembly:")
            for line in disasm:
                print(line)
        print()


def monitor(port, baud, elf_path):
    decoder = CrashDecoder(elf_path)

    try:
        ser = serial.Serial(port, baud, timeout=0.1)
    except serial.SerialException as exc:
        print(f"error opening serial port: {exc}", file=sys.stderr)
        sys.exit(1)

    print(f"Serial: {port} @ {baud} baud")
    print("Press Ctrl+C to exit")
    print("-" * 60)

    in_crash = False
    try:
        while True:
            raw = ser.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", errors="replace").rstrip()
            if not text:
                continue

            if any(marker in text for marker in CRASH_START_MARKERS):
                in_crash = True
                decoder.reset()
                print("\n" + "=" * 60)
                print(" CRASH DETECTED")
                print("=" * 60)

            print(text)

            if in_crash and elf_path:
                m = ADDRESS_LINE_RE.search(text)
                if m:
                    decoder.decode_and_show(m.group(1), "0x" + m.group(2))

            if in_crash and any(marker in text for marker in CRASH_END_MARKERS):
                in_crash = False
                print("=" * 60 + "\n")
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        ser.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", "--port", help="Serial port (e.g. /dev/ttyUSB0, COM3) - auto-detected if omitted")
    parser.add_argument("-b", "--baud", type=int, default=115200)
    parser.add_argument("-t", "--target", choices=sorted(TARGET_ELF), help="Resolve the ELF from a build.py target")
    parser.add_argument("-e", "--elf", help="Explicit ELF path (overrides --target)")
    parser.add_argument("--list-ports", action="store_true", help="List available serial ports and exit")
    args = parser.parse_args()

    if args.list_ports:
        ports = list_serial_ports()
        if not ports:
            print("No serial ports found.")
        for p in ports:
            print(f"{p.device}  {p.description}")
        return

    elf_path = Path(args.elf) if args.elf else (TARGET_ELF.get(args.target) if args.target else None)
    if elf_path and elf_path.exists():
        print(f"ELF file: {elf_path}")
    else:
        if elf_path:
            print(f"warning: {elf_path} not built yet - address decoding disabled", file=sys.stderr)
        else:
            print("warning: no --target/--elf given - address decoding disabled", file=sys.stderr)
        elf_path = None

    port = args.port or guess_serial_port()
    if not port:
        print("error: no serial port given and none could be auto-detected (use --list-ports)", file=sys.stderr)
        sys.exit(1)

    monitor(port, args.baud, elf_path)


if __name__ == "__main__":
    main()
