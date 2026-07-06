#!/usr/bin/env python3
"""STM32F407 multi-OS storage showcase build driver.

One shared, OS-agnostic application (app/ + external/) built four ways:
host (native/POSIX), freertos, threadx, and zephyr - each target links the
exact same app/*.c files, differing only in their OSAL adapter and
composition root (targets/<name>/).

    python3 build.py --target host --build
    python3 build.py --target host --run
    python3 build.py --target freertos --build
    python3 build.py --target freertos --flash
    python3 build.py --target threadx --build
    python3 build.py --target threadx --flash
    python3 build.py --target zephyr --build
    python3 build.py --target zephyr --flash
    python3 build.py --stats
    python3 build.py --all
    python3 build.py --clean
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(REPO_ROOT / "tools"))
from _toolchain import CUBEIDE_GCC_BIN, CUBEIDE_GCC_ROOT, arm_tool, resolve_arm_gcc_bin  # noqa: E402

HOST_DIR = REPO_ROOT / "targets" / "host"
HOST_BUILD_DIR = REPO_ROOT / "build"
HOST_EXE = HOST_BUILD_DIR / "targets" / "host" / "storage_demo_host"

FREERTOS_DIR = REPO_ROOT / "targets" / "freertos"
FREERTOS_MAKEFILE = FREERTOS_DIR / "Makefile.freertos"
FREERTOS_BUILD_DIR = FREERTOS_DIR / "Debug"
FREERTOS_ELF = FREERTOS_BUILD_DIR / "stm32f407.elf"
FREERTOS_BIN = FREERTOS_BUILD_DIR / "stm32f407.bin"
FREERTOS_MAP = FREERTOS_BUILD_DIR / "stm32f407.map"

THREADX_DIR = REPO_ROOT / "targets" / "threadx"
THREADX_MAKEFILE = THREADX_DIR / "Makefile"
THREADX_BUILD_DIR = THREADX_DIR / "Debug"
THREADX_ELF = THREADX_BUILD_DIR / "stm32f407.elf"
THREADX_BIN = THREADX_BUILD_DIR / "stm32f407.bin"
THREADX_MAP = THREADX_BUILD_DIR / "stm32f407.map"

ZEPHYR_DIR = REPO_ROOT / "targets" / "zephyr"
# The Zephyr west workspace (zephyr/, modules/, .west/, .venv/) is NOT part
# of this repo - it's ~9GB of upstream checkout, reproduced via
# `west init && west update`, kept as a separate sibling directory (see
# targets/zephyr's own notes). Override with ZEPHYR_WORKSPACE if yours
# lives elsewhere.
ZEPHYR_WORKSPACE = Path(os.environ.get("ZEPHYR_WORKSPACE", "/home/mrumoy/sandbox/embedded/stm32f407-zephyr"))
ZEPHYR_VENV_PYTHON = ZEPHYR_WORKSPACE / ".venv" / "bin" / "python3"
ZEPHYR_BUILD_DIR = REPO_ROOT / "build_zephyr"
ZEPHYR_BIN = ZEPHYR_BUILD_DIR / "zephyr" / "zephyr.bin"
ZEPHYR_ELF = ZEPHYR_BUILD_DIR / "zephyr" / "zephyr.elf"
ZEPHYR_MAP = ZEPHYR_BUILD_DIR / "zephyr" / "zephyr.map"

# A newer CMake than the system one is needed for the Zephyr build
# (CONFIG_NUM_IRQS/zephyr_default.cmake requires >=3.28) - see targets/zephyr
# notes. Override with CMAKE_BIN if yours lives elsewhere.
ZEPHYR_CMAKE_BIN = Path(os.environ.get("CMAKE_BIN", "/nix/store/5qng39wihv3lfgr03cf7mqbg4lpf4m45-cmake-3.30.5/bin"))

# This board has no ST-LINK - flashing/verification throughout this project
# was done via a real J-Link probe (JLinkExe), not OpenOCD.
JLINK_DEVICE = "STM32F407ZG"


def run(cmd, cwd=None, **kwargs):
    print("+ " + " ".join(str(part) for part in cmd))
    try:
        return subprocess.run(cmd, cwd=cwd or REPO_ROOT, check=True, **kwargs)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
    except subprocess.CalledProcessError as exc:
        print(f"error: command exited with status {exc.returncode}", file=sys.stderr)
        sys.exit(exc.returncode)


def _jlink_flash(bin_path: Path):
    if not bin_path.exists():
        print(f"error: {bin_path} not built yet", file=sys.stderr)
        sys.exit(1)
    # verifybin catches a partially- or un-written flash image (a stale
    # sector left write-protected from a prior debug session, a reset
    # glitch mid-write) before the following r/g resets and runs whatever
    # ended up in flash - without it, JLinkExe can continue past a failed
    # loadbin and this function would report success anyway.
    script = f"""device {JLINK_DEVICE}
if SWD
speed 4000
r
h
loadbin {bin_path} 0x08000000
verifybin {bin_path} 0x08000000
r
g
exit
"""
    with tempfile.NamedTemporaryFile("w", suffix=".jlink", delete=False) as f:
        f.write(script)
        script_path = f.name
    try:
        run(["JLinkExe", "-device", JLINK_DEVICE, "-if", "SWD", "-speed", "4000",
             "-autoconnect", "1", "-ExitOnError", "1", "-CommandFile", script_path])
    finally:
        os.unlink(script_path)


# ── host target ─────────────────────────────────────────────────────────

def build_host():
    HOST_BUILD_DIR.mkdir(parents=True, exist_ok=True)
    run(["cmake", "-S", str(REPO_ROOT), "-B", str(HOST_BUILD_DIR), "-DCMAKE_BUILD_TYPE=Debug"])
    run(["cmake", "--build", str(HOST_BUILD_DIR), "-j"])


def run_host():
    if not HOST_EXE.exists():
        print(f"error: {HOST_EXE} not built yet - run --target host --build first", file=sys.stderr)
        sys.exit(1)
    run([str(HOST_EXE)])


# ── freertos / threadx targets (plain Make) ─────────────────────────────

def _build_make_target(makefile: Path, cwd: Path):
    cmd = ["make", "-f", str(makefile.name)]
    gcc_bin = resolve_arm_gcc_bin()
    if gcc_bin:
        cmd.append(f"GCC_PATH={gcc_bin}")
    elif not shutil.which("arm-none-eabi-gcc"):
        print(
            "warning: arm-none-eabi-gcc not found on PATH, and the known "
            f"STM32CubeIDE-bundled toolchain ({CUBEIDE_GCC_BIN}) doesn't "
            "exist on this machine either - the build will likely fail. "
            "Pass GCC_PATH explicitly.",
            file=sys.stderr,
        )
    run(cmd, cwd=cwd)


def build_freertos():
    _build_make_target(FREERTOS_MAKEFILE, FREERTOS_DIR)


def build_threadx():
    _build_make_target(THREADX_MAKEFILE, THREADX_DIR)


def flash_freertos():
    _jlink_flash(FREERTOS_BIN)


def flash_threadx():
    _jlink_flash(THREADX_BIN)


# ── zephyr target (west + CMake, using the separate west workspace) ────

def build_zephyr():
    if not ZEPHYR_VENV_PYTHON.exists():
        print(
            f"error: {ZEPHYR_VENV_PYTHON} not found - the Zephyr west "
            f"workspace at {ZEPHYR_WORKSPACE} isn't set up. See "
            "targets/zephyr's notes in CLAUDE.md for the west init/update + "
            "Python 3.12 venv + newer CMake setup this needs.",
            file=sys.stderr,
        )
        sys.exit(1)

    env = os.environ.copy()
    env["ZEPHYR_TOOLCHAIN_VARIANT"] = "gnuarmemb"
    env["GNUARMEMB_TOOLCHAIN_PATH"] = str(CUBEIDE_GCC_ROOT)
    env["PATH"] = f"{ZEPHYR_CMAKE_BIN}:{ZEPHYR_VENV_PYTHON.parent}:{env['PATH']}"
    env["VIRTUAL_ENV"] = str(ZEPHYR_VENV_PYTHON.parent.parent)

    run(
        ["west", "build", "-b", "black_f407zg_pro", str(ZEPHYR_DIR), "-d", str(ZEPHYR_BUILD_DIR)],
        cwd=ZEPHYR_WORKSPACE,
        env=env,
    )


def flash_zephyr():
    _jlink_flash(ZEPHYR_BIN)


# ── stats / clean ────────────────────────────────────────────────────────

# STM32F407VGT6 memory sizes, used only when a target's own .map file
# doesn't have a "Memory Configuration" table to read the real numbers from.
DEFAULT_MEMORY_SIZES = {"FLASH": 1024 * 1024, "RAM": 128 * 1024, "CCMRAM": 64 * 1024}

# Section-name/address heuristics for classifying `size -A -d` output into
# FLASH vs RAM vs CCMRAM usage - falls back to address range when a section
# name isn't recognized (covers Zephyr's own section names, which differ
# from the CubeIDE-style ones freertos/threadx use).
FLASH_SECTION_NAMES = {".isr_vector", ".text", ".rodata", ".ARM.extab", ".ARM",
                        ".preinit_array", ".init_array", ".fini_array"}
RAM_SECTION_NAMES = {".data", ".bss"}
CCMRAM_SECTION_NAMES = {".ccmram"}


def _size_sections(elf_path: Path):
    """Run arm-none-eabi-size -A -d and return {section: {"size", "addr"}}."""
    result = subprocess.run([arm_tool("size"), "-A", "-d", str(elf_path)],
                             capture_output=True, text=True, check=False)
    sections = {}
    for line in result.stdout.splitlines():
        # Section names aren't always dot-prefixed - CubeIDE-style linker
        # scripts use ".text"/".bss", but Zephyr's own uses bare "text"/"bss".
        m = re.match(r"^(\.?[A-Za-z_][\w.]*)\s+(\d+)\s+(\d+)", line)
        if m:
            sections[m.group(1)] = {"size": int(m.group(2)), "addr": int(m.group(3))}
    return sections


def _map_region_sizes(map_path: Path):
    """Parse a linker .map file's Memory Configuration table -> {name: length}."""
    if not map_path.exists():
        return {}
    content = map_path.read_text(errors="replace")
    m = re.search(r"Memory Configuration\s*\n\s*Name\s+Origin\s+Length.*?\n(.*?)(?=\nLinker|$)",
                  content, re.DOTALL)
    if not m:
        return {}
    regions = {}
    for line in m.group(1).strip().splitlines():
        rm = re.match(r"^(\S+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)", line)
        if rm:
            regions[rm.group(1)] = int(rm.group(3), 16)
    return regions


def _format_bytes(n):
    if n >= 1024 * 1024:
        return f"{n / (1024 * 1024):.2f} MB"
    if n >= 1024:
        return f"{n / 1024:.2f} KB"
    return f"{n} B"


def _usage_bar(used, total, width=32):
    if total <= 0:
        return "[" + " " * width + "]"
    filled = int(width * min(used / total, 1.0))
    return "[" + "#" * filled + "-" * (width - filled) + "]"


def _memory_usage(elf_path: Path, map_path: Path):
    """Classify an ELF's sections into FLASH/RAM/CCMRAM usage."""
    sections = _size_sections(elf_path)
    sizes = dict(DEFAULT_MEMORY_SIZES)
    sizes.update({name: length for name, length in _map_region_sizes(map_path).items() if name in sizes})

    usage = {region: {"used": 0, "total": total, "sections": []} for region, total in sizes.items()}
    for name, info in sections.items():
        size, addr = info["size"], info["addr"]
        if size == 0:
            continue
        if name in FLASH_SECTION_NAMES or (0x08000000 <= addr < 0x08100000):
            usage["FLASH"]["used"] += size
            usage["FLASH"]["sections"].append((name, size))
        elif name in RAM_SECTION_NAMES or (0x20000000 <= addr < 0x20020000):
            usage["RAM"]["used"] += size
            usage["RAM"]["sections"].append((name, size))
            if name == ".data":
                # .data's initial values also live in FLASH (copied to RAM
                # at startup) - count that flash cost too.
                usage["FLASH"]["used"] += size
                usage["FLASH"]["sections"].append((".data (init)", size))
        elif name in CCMRAM_SECTION_NAMES or (0x10000000 <= addr < 0x10010000):
            usage["CCMRAM"]["used"] += size
            usage["CCMRAM"]["sections"].append((name, size))
    return usage


def _print_memory_usage(name, elf_path: Path, map_path: Path, verbose: bool):
    print(f"--- {name} ---")
    usage = _memory_usage(elf_path, map_path)
    for region in ("FLASH", "RAM", "CCMRAM"):
        used, total = usage[region]["used"], usage[region]["total"]
        if total == 0:
            continue
        pct = 100.0 * used / total
        print(f"  {region:8} {_usage_bar(used, total)} {pct:5.1f}%  "
              f"{_format_bytes(used):>10} / {_format_bytes(total)}")
        if verbose:
            for section, size in sorted(usage[region]["sections"], key=lambda x: -x[1]):
                print(f"      {section:20} {_format_bytes(size):>10}")


def stats(verbose: bool = False):
    print("=== Build footprint ===")
    for name, elf, map_path in (
        ("freertos", FREERTOS_ELF, FREERTOS_MAP),
        ("threadx", THREADX_ELF, THREADX_MAP),
        ("zephyr", ZEPHYR_ELF, ZEPHYR_MAP),
    ):
        if elf.exists():
            _print_memory_usage(name, elf, map_path, verbose)
        else:
            print(f"--- {name} --- not built yet - run --target {name} --build first")

    if HOST_EXE.exists():
        print("--- host ---")
        run(["size", str(HOST_EXE)])
    else:
        print("--- host --- not built yet - run --target host --build first")


def clean():
    for build_dir in (HOST_BUILD_DIR, FREERTOS_BUILD_DIR, THREADX_BUILD_DIR, ZEPHYR_BUILD_DIR):
        if build_dir.exists():
            shutil.rmtree(build_dir)
            print(f"removed {build_dir}")
        else:
            print(f"{build_dir} already clean")


# ── CLI ───────────────────────────────────────────────────────────────────

BUILDERS = {"host": build_host, "freertos": build_freertos, "threadx": build_threadx, "zephyr": build_zephyr}
FLASHERS = {"freertos": flash_freertos, "threadx": flash_threadx, "zephyr": flash_zephyr}


def main():
    parser = argparse.ArgumentParser(description="STM32F407 multi-OS storage showcase build driver")
    parser.add_argument("--target", choices=list(BUILDERS), default="host")
    parser.add_argument("--build", action="store_true", help="Build selected target")
    parser.add_argument("--run", action="store_true", help="Run executable (host target only)")
    parser.add_argument("--flash", action="store_true", help="Flash binary to the STM32 board via J-Link")
    parser.add_argument("--stats", action="store_true", help="Print memory footprint for built targets")
    parser.add_argument("-v", "--verbose", action="store_true", help="With --stats, also show per-section breakdown")
    parser.add_argument("--all", action="store_true", help="Build every target and show a size comparison")
    parser.add_argument("--clean", action="store_true", help="Clean all build directories")
    args = parser.parse_args()

    if args.clean:
        clean()
        return

    if args.all:
        for builder in BUILDERS.values():
            builder()
        stats(args.verbose)
        return

    did_something = False

    if args.build:
        did_something = True
        BUILDERS[args.target]()

    if args.run:
        did_something = True
        if args.target != "host":
            print("error: --run only supports --target host (a native executable)", file=sys.stderr)
            sys.exit(1)
        run_host()

    if args.flash:
        did_something = True
        if args.target not in FLASHERS:
            print(f"error: --flash does not apply to --target {args.target}", file=sys.stderr)
            sys.exit(1)
        FLASHERS[args.target]()

    if args.stats:
        did_something = True
        stats(args.verbose)

    if not did_something:
        build_host()


if __name__ == "__main__":
    main()
