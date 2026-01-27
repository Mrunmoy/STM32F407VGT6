#!/usr/bin/env python3
"""
Memory usage reporter for STM32 projects.

Parses ELF and MAP files to show FLASH and RAM usage as percentages.

Usage:
    python memory_usage.py -e Debug/stm32f407.elf
    python memory_usage.py -e Debug/stm32f407.elf -m Debug/stm32f407.map
"""

import argparse
import os
import re
import subprocess
import sys


# Default memory sizes for STM32F407VGT6
DEFAULT_FLASH_SIZE = 1024 * 1024  # 1MB
DEFAULT_RAM_SIZE = 128 * 1024     # 128KB
DEFAULT_CCMRAM_SIZE = 64 * 1024   # 64KB


def run_size(elf_path):
    """Run arm-none-eabi-size and parse the output."""
    try:
        result = subprocess.run(
            ["arm-none-eabi-size", "-A", "-d", elf_path],
            capture_output=True,
            text=True,
            timeout=10
        )
        if result.returncode != 0:
            print(f"Error running arm-none-eabi-size: {result.stderr}")
            return None
        return result.stdout
    except FileNotFoundError:
        print("arm-none-eabi-size not found. Make sure it's in your PATH.")
        return None
    except subprocess.TimeoutExpired:
        print("arm-none-eabi-size timed out.")
        return None


def parse_size_output(output):
    """Parse arm-none-eabi-size -A output into section dict."""
    sections = {}
    for line in output.strip().split('\n'):
        # Match lines like: .text          12345     134217728
        match = re.match(r'^(\.\S+)\s+(\d+)\s+(\d+)', line)
        if match:
            name = match.group(1)
            size = int(match.group(2))
            addr = int(match.group(3))
            sections[name] = {'size': size, 'addr': addr}
    return sections


def parse_map_file(map_path):
    """Parse .map file for memory region info."""
    if not map_path or not os.path.exists(map_path):
        return None

    regions = {}
    try:
        with open(map_path, 'r', encoding='utf-8', errors='replace') as f:
            content = f.read()

        # Look for Memory Configuration section
        mem_config = re.search(
            r'Memory Configuration\s*\n\s*Name\s+Origin\s+Length.*?\n(.*?)(?=\nLinker|$)',
            content,
            re.DOTALL
        )
        if mem_config:
            for line in mem_config.group(1).strip().split('\n'):
                # Match: RAM    0x20000000    0x00020000    xrw
                match = re.match(r'^(\S+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)', line)
                if match:
                    name = match.group(1)
                    origin = int(match.group(2), 16)
                    length = int(match.group(3), 16)
                    regions[name] = {'origin': origin, 'length': length}

        return regions if regions else None
    except Exception as e:
        print(f"Warning: Could not parse map file: {e}")
        return None


def find_map_file(elf_path):
    """Try to find the .map file based on ELF path."""
    map_path = elf_path.replace('.elf', '.map')
    if os.path.exists(map_path):
        return map_path
    return None


def calculate_usage(sections, flash_size, ram_size, ccmram_size):
    """Calculate FLASH and RAM usage from sections."""
    # FLASH = .text + .rodata + .data (init values) + other read-only sections
    # RAM = .data + .bss
    # CCMRAM = .ccmram

    flash_used = 0
    ram_used = 0
    ccmram_used = 0

    # Sections that go into FLASH
    flash_sections = ['.isr_vector', '.text', '.rodata', '.ARM.extab', '.ARM',
                      '.preinit_array', '.init_array', '.fini_array']

    # Sections that go into RAM (runtime)
    ram_sections = ['.data', '.bss']

    # CCMRAM sections
    ccmram_sections = ['.ccmram']

    section_details = {'flash': [], 'ram': [], 'ccmram': []}

    for name, info in sections.items():
        size = info['size']
        addr = info['addr']

        if size == 0:
            continue

        # Determine which memory region based on address or name
        if name in flash_sections or (0x08000000 <= addr < 0x08100000):
            flash_used += size
            section_details['flash'].append((name, size))
        elif name in ram_sections or (0x20000000 <= addr < 0x20020000):
            ram_used += size
            section_details['ram'].append((name, size))
            # .data also takes flash space for init values
            if name == '.data':
                flash_used += size
                section_details['flash'].append((f'{name} (init)', size))
        elif name in ccmram_sections or (0x10000000 <= addr < 0x10010000):
            ccmram_used += size
            section_details['ccmram'].append((name, size))

    return {
        'flash': {'used': flash_used, 'total': flash_size, 'sections': section_details['flash']},
        'ram': {'used': ram_used, 'total': ram_size, 'sections': section_details['ram']},
        'ccmram': {'used': ccmram_used, 'total': ccmram_size, 'sections': section_details['ccmram']}
    }


def format_size(size):
    """Format size in human-readable form."""
    if size >= 1024 * 1024:
        return f"{size / (1024 * 1024):.2f} MB"
    elif size >= 1024:
        return f"{size / 1024:.2f} KB"
    else:
        return f"{size} B"


def print_bar(used, total, width=40):
    """Print a progress bar."""
    if total == 0:
        return "[" + " " * width + "]"

    pct = used / total
    filled = int(width * pct)
    bar = "#" * filled + "-" * (width - filled)
    return f"[{bar}]"


def print_usage(usage, verbose=False):
    """Print memory usage report."""
    print("\n" + "=" * 60)
    print("                    MEMORY USAGE")
    print("=" * 60)

    for region_name in ['flash', 'ram', 'ccmram']:
        region = usage[region_name]
        used = region['used']
        total = region['total']

        if total == 0:
            continue

        pct = (used / total) * 100
        free = total - used

        print(f"\n{region_name.upper()}:")
        print(f"  {print_bar(used, total)}  {pct:.1f}%")
        print(f"  Used: {format_size(used):>10}  /  Total: {format_size(total)}")
        print(f"  Free: {format_size(free):>10}")

        if verbose and region['sections']:
            print(f"  Sections:")
            for name, size in sorted(region['sections'], key=lambda x: -x[1]):
                print(f"    {name:20} {format_size(size):>10}")

    print("\n" + "=" * 60)


def main():
    parser = argparse.ArgumentParser(
        description="Show memory usage for STM32 ELF files"
    )
    parser.add_argument("-e", "--elf", required=True,
                        help="Path to ELF file")
    parser.add_argument("-m", "--map",
                        help="Path to MAP file (auto-detected if not specified)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Show section breakdown")
    parser.add_argument("--flash", type=int, default=DEFAULT_FLASH_SIZE,
                        help=f"Flash size in bytes (default: {DEFAULT_FLASH_SIZE})")
    parser.add_argument("--ram", type=int, default=DEFAULT_RAM_SIZE,
                        help=f"RAM size in bytes (default: {DEFAULT_RAM_SIZE})")
    parser.add_argument("--ccmram", type=int, default=DEFAULT_CCMRAM_SIZE,
                        help=f"CCMRAM size in bytes (default: {DEFAULT_CCMRAM_SIZE})")
    args = parser.parse_args()

    if not os.path.exists(args.elf):
        print(f"ELF file not found: {args.elf}")
        sys.exit(1)

    # Try to find map file
    map_path = args.map or find_map_file(args.elf)

    # Get memory sizes from map file if available
    flash_size = args.flash
    ram_size = args.ram
    ccmram_size = args.ccmram

    if map_path:
        regions = parse_map_file(map_path)
        if regions:
            if 'FLASH' in regions:
                flash_size = regions['FLASH']['length']
            if 'RAM' in regions:
                ram_size = regions['RAM']['length']
            if 'CCMRAM' in regions:
                ccmram_size = regions['CCMRAM']['length']

    # Run arm-none-eabi-size
    size_output = run_size(args.elf)
    if not size_output:
        sys.exit(1)

    # Parse sections
    sections = parse_size_output(size_output)
    if not sections:
        print("No sections found in ELF file.")
        sys.exit(1)

    # Calculate usage
    usage = calculate_usage(sections, flash_size, ram_size, ccmram_size)

    # Print report
    print(f"ELF: {args.elf}")
    if map_path:
        print(f"MAP: {map_path}")
    print_usage(usage, args.verbose)


if __name__ == "__main__":
    main()
