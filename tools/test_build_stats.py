#!/usr/bin/env python3
"""Unit tests for build.py's memory-usage parsing (_size_sections,
_map_region_sizes, _memory_usage). Plain stdlib unittest - no new dependency,
no hardware, no build required.

Run: python3 tools/test_build_stats.py
"""

import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

import build  # noqa: E402


class FakeCompletedProcess:
    def __init__(self, stdout):
        self.stdout = stdout


class SizeSectionsTests(unittest.TestCase):
    """Regression coverage for the dual-purpose CubeIDE/Zephyr section-name
    regex - the exact thing that broke silently once already (Zephyr's
    bare "text"/"bss" section names, no leading dot) before being fixed."""

    def test_dot_prefixed_cubeide_style_sections(self):
        stdout = (
            "build/stm32f407.elf  :\n"
            "section             size        addr\n"
            ".isr_vector           392   134217728\n"
            ".text               98180   134218120\n"
            ".data                 140   536870912\n"
            "Total               98712\n"
        )
        with mock.patch("subprocess.run", return_value=FakeCompletedProcess(stdout)):
            sections = build._size_sections(Path("fake.elf"))

        self.assertEqual(sections[".isr_vector"], {"size": 392, "addr": 134217728})
        self.assertEqual(sections[".text"], {"size": 98180, "addr": 134218120})
        self.assertEqual(sections[".data"], {"size": 140, "addr": 536870912})
        self.assertNotIn("Total", sections)
        self.assertNotIn("section", sections)

    def test_bare_zephyr_style_sections(self):
        stdout = (
            "build_zephyr/zephyr/zephyr.elf  :\n"
            "section             size        addr\n"
            "text                43416   134218064\n"
            "rodata               4216   134262656\n"
            "bss                 11901   536871072\n"
            "Total              996557\n"
        )
        with mock.patch("subprocess.run", return_value=FakeCompletedProcess(stdout)):
            sections = build._size_sections(Path("fake.elf"))

        self.assertEqual(sections["text"], {"size": 43416, "addr": 134218064})
        self.assertEqual(sections["rodata"], {"size": 4216, "addr": 134262656})
        self.assertEqual(sections["bss"], {"size": 11901, "addr": 536871072})
        self.assertNotIn("Total", sections)


class MemoryUsageTests(unittest.TestCase):
    def test_flash_ram_classification_by_address(self):
        sections = {
            ".text": {"size": 1000, "addr": 0x08000000},
            ".data": {"size": 8, "addr": 0x20000000},
            ".bss": {"size": 200, "addr": 0x20000008},
        }
        with mock.patch.object(build, "_size_sections", return_value=sections), \
             mock.patch.object(build, "_map_region_sizes", return_value={}):
            usage = build._memory_usage(Path("fake.elf"), Path("fake.map"))

        # .text -> FLASH; .data counted in RAM AND its init copy in FLASH;
        # .bss -> RAM only.
        self.assertEqual(usage["FLASH"]["used"], 1000 + 8)
        self.assertEqual(usage["RAM"]["used"], 8 + 200)
        self.assertEqual(usage["FLASH"]["total"], build.DEFAULT_MEMORY_SIZES["FLASH"])

    def test_map_region_sizes_override_defaults(self):
        sections = {".text": {"size": 100, "addr": 0x08000000}}
        with mock.patch.object(build, "_size_sections", return_value=sections), \
             mock.patch.object(build, "_map_region_sizes", return_value={"FLASH": 2048}):
            usage = build._memory_usage(Path("fake.elf"), Path("fake.map"))

        self.assertEqual(usage["FLASH"]["total"], 2048)
        # A region absent from the .map's table keeps the built-in default.
        self.assertEqual(usage["RAM"]["total"], build.DEFAULT_MEMORY_SIZES["RAM"])


class MapRegionSizesTests(unittest.TestCase):
    def test_parses_memory_configuration_table(self, tmp_path=None):
        map_text = (
            "Memory Configuration\n\n"
            "Name             Origin             Length             Attributes\n"
            "CCMRAM           0x10000000         0x00010000         xrw\n"
            "RAM              0x20000000         0x00020000         xrw\n"
            "FLASH            0x08000000         0x00100000         rx\n"
            "*default*        0x00000000         0xffffffff\n\n"
            "Linker script and memory map\n"
        )
        with mock.patch.object(Path, "exists", return_value=True), \
             mock.patch.object(Path, "read_text", return_value=map_text):
            regions = build._map_region_sizes(Path("fake.map"))

        self.assertEqual(regions["FLASH"], 0x00100000)
        self.assertEqual(regions["RAM"], 0x00020000)
        self.assertEqual(regions["CCMRAM"], 0x00010000)

    def test_missing_map_file_returns_empty(self):
        with mock.patch.object(Path, "exists", return_value=False):
            self.assertEqual(build._map_region_sizes(Path("missing.map")), {})


if __name__ == "__main__":
    unittest.main()
