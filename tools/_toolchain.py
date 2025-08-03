"""Shared ARM GCC toolchain resolution for build.py and tools/ scripts.

Single source of truth for where arm-none-eabi-* binaries come from, so
build.py and every tools/ script agree on the same fallback path instead of
each hardcoding their own copy.
"""

import shutil
from pathlib import Path

# Known-good fallback location for the ARM toolchain when arm-none-eabi-gcc
# isn't on PATH - the STM32CubeIDE-bundled GCC 14.3 this project's Makefiles
# were verified against.
CUBEIDE_GCC_BIN = Path(
    "/opt/st/stm32cubeide_2.2.0/plugins/"
    "com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/"
    "tools/bin"
)
CUBEIDE_GCC_ROOT = CUBEIDE_GCC_BIN.parent


def resolve_arm_gcc_bin():
    """Return the ARM toolchain bin dir to use, or None if arm-none-eabi-gcc is already on PATH."""
    if shutil.which("arm-none-eabi-gcc"):
        return None
    if (CUBEIDE_GCC_BIN / "arm-none-eabi-gcc").exists():
        return CUBEIDE_GCC_BIN
    return None


def arm_tool(name):
    """Resolve one arm-none-eabi-<name> tool to a runnable path or bare command."""
    gcc_bin = resolve_arm_gcc_bin()
    return str(gcc_bin / f"arm-none-eabi-{name}") if gcc_bin else f"arm-none-eabi-{name}"
