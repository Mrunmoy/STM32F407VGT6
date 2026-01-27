#!/usr/bin/env python3
"""
Serial monitor with automatic crash dump decoding for STM32.

Monitors UART output and when a crash dump is detected, automatically
runs addr2line to show the source file and line number.

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


def addr2line(elf_path, address):
    """Run addr2line and return the result."""
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
                # Clean up the path
                location = location.replace("\\", "/")
                if "?" not in func:
                    return f"{func} at {location}"
        return None
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None


def decode_address(line, elf_path):
    """Check if line contains an address and decode it."""
    # Match patterns like "Fault PC  = 0x080012A2" or "PC  = 0x080012A2"
    match = re.search(r'(PC|LR|Fault PC|Fault LR)\s*=\s*0x([0-9A-Fa-f]+)', line)
    if match:
        reg_name = match.group(1)
        address = "0x" + match.group(2)
        decoded = addr2line(elf_path, address)
        if decoded:
            return f"         >>> {reg_name}: {decoded}"
    return None


def main():
    parser = argparse.ArgumentParser(
        description="Serial monitor with crash dump decoding"
    )
    parser.add_argument("-p", "--port", required=True, help="Serial port (e.g. COM3, /dev/ttyUSB0)")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("-e", "--elf", help="Path to ELF file for addr2line")
    args = parser.parse_args()

    # Find ELF file
    elf_path = args.elf
    if not elf_path:
        elf_path = find_elf_file()

    if elf_path and os.path.exists(elf_path):
        print(f"Using ELF file: {elf_path}")
    else:
        print("Warning: No ELF file found. Address decoding disabled.")
        print("         Use -e option to specify the ELF file path.")
        elf_path = None

    # Open serial port
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
        print(f"Opened {args.port} at {args.baud} baud")
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
                print("\n" + "=" * 60)
                print("CRASH DETECTED - Decoding addresses...")
                print("=" * 60)

            # Print the line
            print(text)

            # If we're in a crash dump and have an ELF, try to decode addresses
            if in_crash_dump and elf_path:
                decoded = decode_address(text, elf_path)
                if decoded:
                    print(decoded)

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
