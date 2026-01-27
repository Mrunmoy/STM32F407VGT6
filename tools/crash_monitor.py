#!/usr/bin/env python3
"""
Serial monitor with automatic crash dump decoding for STM32.

Monitors UART output and when a crash dump is detected, automatically
runs addr2line to show the source file, line number, and surrounding code.

Usage:
    python crash_monitor.py -p COM3 -e Debug/stm32f407.elf
    python crash_monitor.py -p /dev/ttyUSB0 -e Debug/stm32f407.elf -b 115200
"""

import argparse
import re
import subprocess
import sys
import os

try:
    import serial
except ImportError:
    print("pyserial not installed. Run: pip install pyserial")
    sys.exit(1)


# How many lines of context to show around the crash
CONTEXT_LINES = 5


def find_elf_file():
    """Try to find the ELF file automatically."""
    candidates = [
        "Debug/stm32f407.elf",
        "../Debug/stm32f407.elf",
        "build/stm32f407.elf",
    ]
    for path in candidates:
        if os.path.exists(path):
            return path
    return None


def find_source_file(filename, search_paths):
    """Try to find the source file in various locations."""
    # If it's already an absolute path or exists
    if os.path.exists(filename):
        return filename

    # Extract just the filename
    basename = os.path.basename(filename)

    for base in search_paths:
        # Try exact path
        candidate = os.path.join(base, filename)
        if os.path.exists(candidate):
            return candidate

        # Try with just filename in common directories
        for subdir in ["Core/Src", "Core/Inc", "Src", "Inc", ""]:
            candidate = os.path.join(base, subdir, basename)
            if os.path.exists(candidate):
                return candidate

    return None


def read_source_context(filepath, line_num, context=CONTEXT_LINES):
    """Read source file and return lines around the crash location."""
    try:
        with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()

        start = max(0, line_num - context - 1)
        end = min(len(lines), line_num + context)

        result = []
        for i in range(start, end):
            line_no = i + 1
            marker = ">>>" if line_no == line_num else "   "
            result.append(f"  {marker} {line_no:4d}: {lines[i].rstrip()}")

        return result
    except Exception as e:
        return [f"  (Could not read source: {e})"]


def addr2line(elf_path, address):
    """Run addr2line and return function, file, line."""
    try:
        result = subprocess.run(
            ["arm-none-eabi-addr2line", "-e", elf_path, "-f", "-C", address],
            capture_output=True,
            text=True,
            timeout=5
        )
        if result.returncode == 0:
            lines = result.stdout.strip().split('\n')
            if len(lines) >= 2:
                func = lines[0]
                location = lines[1]
                # Parse file:line
                if ":" in location and "?" not in location:
                    parts = location.rsplit(":", 1)
                    filepath = parts[0].replace("\\", "/")
                    try:
                        line_num = int(parts[1])
                        return func, filepath, line_num
                    except ValueError:
                        pass
        return None, None, None
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None, None, None


def get_disassembly(list_path, address):
    """Extract disassembly around an address from the .list file."""
    if not list_path or not os.path.exists(list_path):
        return None

    # Convert address to match format in .list file (without 0x prefix, lowercase)
    addr_clean = address.lower().replace("0x", "").lstrip("0") or "0"

    try:
        with open(list_path, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()

        # Find the line with our address
        target_idx = None
        for i, line in enumerate(lines):
            if re.match(r'\s*' + addr_clean + r':', line, re.IGNORECASE):
                target_idx = i
                break

        if target_idx is None:
            return None

        # Get context around it
        start = max(0, target_idx - 3)
        end = min(len(lines), target_idx + 4)

        result = []
        for i in range(start, end):
            marker = ">>>" if i == target_idx else "   "
            result.append(f"  {marker} {lines[i].rstrip()}")

        return result
    except:
        return None


class CrashDecoder:
    def __init__(self, elf_path, project_root):
        self.elf_path = elf_path
        self.project_root = project_root
        self.search_paths = [project_root, ".", ".."]

        # Find .list file
        if elf_path:
            self.list_path = elf_path.replace(".elf", ".list")
            if not os.path.exists(self.list_path):
                self.list_path = None
        else:
            self.list_path = None

        self.decoded_addresses = set()

    def decode_and_show(self, address, reg_name):
        """Decode an address and show source context."""
        if not self.elf_path:
            return

        # Don't decode the same address twice
        if address in self.decoded_addresses:
            return
        self.decoded_addresses.add(address)

        func, filepath, line_num = addr2line(self.elf_path, address)

        if not func or "?" in func:
            print(f"         [{reg_name}: Could not decode {address}]")
            return

        print(f"\n         --- {reg_name}: {func}() ---")
        print(f"         {filepath}:{line_num}")

        # Try to show source code
        source_file = find_source_file(filepath, self.search_paths)
        if source_file:
            print(f"         Source:")
            for line in read_source_context(source_file, line_num):
                print(line)

        # Show disassembly if available
        disasm = get_disassembly(self.list_path, address)
        if disasm:
            print(f"         Disassembly:")
            for line in disasm:
                print(line)

        print()

    def reset(self):
        """Reset for next crash."""
        self.decoded_addresses.clear()


def decode_address_from_line(line):
    """Extract address from a crash dump line."""
    match = re.search(r'(PC|LR|Fault PC|Fault LR)\s*=\s*0x([0-9A-Fa-f]+)', line)
    if match:
        reg_name = match.group(1)
        address = "0x" + match.group(2)
        return reg_name, address
    return None, None


def main():
    parser = argparse.ArgumentParser(
        description="Serial monitor with crash dump decoding"
    )
    parser.add_argument("-p", "--port", required=True,
                        help="Serial port (e.g. COM3, /dev/ttyUSB0)")
    parser.add_argument("-b", "--baud", type=int, default=115200,
                        help="Baud rate (default: 115200)")
    parser.add_argument("-e", "--elf",
                        help="Path to ELF file for addr2line")
    parser.add_argument("-r", "--root", default="..",
                        help="Project root directory (default: ..)")
    args = parser.parse_args()

    # Find ELF file
    elf_path = args.elf
    if not elf_path:
        elf_path = find_elf_file()

    if elf_path and os.path.exists(elf_path):
        print(f"ELF file: {elf_path}")
    else:
        print("Warning: No ELF file found. Address decoding disabled.")
        print("         Use -e option to specify the ELF file path.")
        elf_path = None

    decoder = CrashDecoder(elf_path, args.root)

    # Open serial port
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
        print(f"Serial: {args.port} @ {args.baud} baud")
        print("Press Ctrl+C to exit")
        print("-" * 60)
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)

    in_crash_dump = False

    try:
        while True:
            line = ser.readline()
            if not line:
                continue

            try:
                text = line.decode('utf-8', errors='replace').rstrip()
            except:
                continue

            if not text:
                continue

            # Detect crash dump start
            if "HARD FAULT DETECTED" in text or "FAULT DETECTED" in text:
                in_crash_dump = True
                decoder.reset()
                print("\n" + "=" * 60)
                print(" CRASH DETECTED")
                print("=" * 60)

            # Print the line
            print(text)

            # If in crash dump, try to decode PC/LR addresses
            if in_crash_dump and elf_path:
                reg_name, address = decode_address_from_line(text)
                if reg_name and address:
                    decoder.decode_and_show(address, reg_name)

            # Detect crash dump end
            if in_crash_dump and ("Halting" in text or "reset" in text.lower()):
                in_crash_dump = False
                print("=" * 60 + "\n")

    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
