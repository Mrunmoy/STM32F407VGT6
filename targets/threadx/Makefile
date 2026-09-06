# ── STM32F407 + Azure ThreadX — command-line build ────────────────────────
# Targets an STM32F407xG (VGT6 / ZGT6, both pin/memory compatible).
# Produces Debug/stm32f407.{elf,hex,bin}. Links this directory's own
# ThreadX-target files (Core/, AZURE_RTOS/, Middlewares/ST/threadx,
# osal/, pal/) plus the shared OS-agnostic application layer at
# ../../app (identical across all RTOS targets and the host build) and
# shared vendor libs at ../../external (the STM32 HAL/CMSIS, FatFS, ST's USB
# Host library, libcfuture).

TARGET = stm32f407
BUILD_DIR = Debug

# The per-file $(eval)'d compile rules below are textually the first rules
# Make encounters (before "all:"), which would otherwise silently make one
# arbitrary .o file - not "all" - the default goal. Pin it explicitly.
.DEFAULT_GOAL := all

# ── Toolchain ─────────────────────────────────────────────────────────────
# By default the arm-none-eabi tools are taken from PATH (this is the case
# inside the dev container). To build against a toolchain that is not on PATH
# (e.g. a nix-store GCC on the host), pass its bin dir explicitly:
#     make GCC_PATH=/nix/store/<hash>-gcc-arm-embedded-15.2.rel1/bin
GCC_PATH ?=
PREFIX = arm-none-eabi-
ifeq ($(strip $(GCC_PATH)),)
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
else
CC = $(GCC_PATH)/$(PREFIX)gcc
AS = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP = $(GCC_PATH)/$(PREFIX)objcopy
SZ = $(GCC_PATH)/$(PREFIX)size
endif
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

# ── MCU / FPU ─────────────────────────────────────────────────────────────
CPU = -mcpu=cortex-m4
FPU = -mfpu=fpv4-sp-d16
FLOAT-ABI = -mfloat-abi=hard
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

# ── Defines ───────────────────────────────────────────────────────────────
C_DEFS = \
  -DUSE_HAL_DRIVER \
  -DSTM32F407xx \
  -DTX_INCLUDE_USER_DEFINE_FILE \
  -DTX_SINGLE_MODE_NON_SECURE=1 \
  -DSTM32_THREAD_SAFE_STRATEGY=2

# ── Shared tree (identical across all targets + host) ──────────────────────
APP_DIR = ../../app
EXTERNAL_DIR = ../../external

# ── Include paths ─────────────────────────────────────────────────────────
C_INCLUDES = \
  -ICore/Inc \
  -Iosal/include \
  -Ipal/include \
  -I$(APP_DIR)/include \
  -I$(APP_DIR)/include/osal \
  -I$(APP_DIR)/include/pal \
  -I$(APP_DIR)/include/core \
  -I$(APP_DIR)/include/diagnostics \
  -I$(APP_DIR)/include/storage \
  -I$(APP_DIR)/include/usb \
  -ICore/ThreadSafe \
  -IAZURE_RTOS/App \
  -I$(EXTERNAL_DIR)/stm32f4xx_hal/STM32F4xx_HAL_Driver/Inc \
  -I$(EXTERNAL_DIR)/stm32f4xx_hal/STM32F4xx_HAL_Driver/Inc/Legacy \
  -I$(EXTERNAL_DIR)/stm32f4xx_hal/CMSIS/Device/ST/STM32F4xx/Include \
  -I$(EXTERNAL_DIR)/stm32f4xx_hal/CMSIS/Include \
  -IMiddlewares/ST/threadx/common/inc \
  -IMiddlewares/ST/threadx/ports/cortex_m4/gnu/inc \
  -I$(EXTERNAL_DIR)/usb_host_lib/Core/Inc \
  -I$(EXTERNAL_DIR)/usb_host_lib/Class/MSC/Inc \
  -I$(EXTERNAL_DIR)/fatfs \
  -I$(EXTERNAL_DIR)/fatfs/src \
  -I$(EXTERNAL_DIR)/cfuture/include

# ── Sources ───────────────────────────────────────────────────────────────
FATFS_SOURCES = $(filter-out %/diskio.c %/ff_gen_drv.c,$(wildcard $(EXTERNAL_DIR)/fatfs/src/*.c))
CFUTURE_SOURCES = \
  $(EXTERNAL_DIR)/cfuture/src/cfuture.c \
  $(EXTERNAL_DIR)/cfuture/src/cfuture_pal.c \
  $(filter-out %_posix.c %_win32.c,$(wildcard $(EXTERNAL_DIR)/cfuture/src/adapters/*.c))

C_SOURCES = \
  $(wildcard Core/Src/*.c) \
  $(wildcard osal/src/*.c) \
  $(wildcard pal/src/*.c) \
  $(wildcard $(APP_DIR)/src/*/*.c) \
  $(wildcard Core/ThreadSafe/*.c) \
  $(wildcard AZURE_RTOS/App/*.c) \
  $(wildcard $(EXTERNAL_DIR)/gcc_newlib_stubs/*.c) \
  $(EXTERNAL_DIR)/stm32f4xx_hal/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c \
  $(wildcard $(EXTERNAL_DIR)/stm32f4xx_hal/STM32F4xx_HAL_Driver/Src/*.c) \
  $(wildcard Middlewares/ST/threadx/common/src/*.c) \
  $(wildcard $(EXTERNAL_DIR)/usb_host_lib/Core/Src/*.c) \
  $(wildcard $(EXTERNAL_DIR)/usb_host_lib/Class/MSC/Src/*.c) \
  $(FATFS_SOURCES) \
  $(CFUTURE_SOURCES)

ASM_SOURCES_S   = $(wildcard Core/Src/*.s) $(wildcard Core/Startup/*.s)
ASM_SOURCES_UP  = $(wildcard Middlewares/ST/threadx/ports/cortex_m4/gnu/src/*.S)

# ── Flags ─────────────────────────────────────────────────────────────────
# -O0 gives linear, line-by-line single-stepping during bring-up debugging.
# Switch back to -Og (or -O2) for size/speed once debugging is done.
OPT = -O0
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections -g -gdwarf-2
ASFLAGS = $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections -g -gdwarf-2

LDSCRIPT = STM32F407VGTX_FLASH.ld
LIBS = -lc -lm -lnosys
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) \
  -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

# ── Object list ────────────────────────────────────────────────────────────
# Sources reached via ../../app or ../../external live OUTSIDE this
# directory - strip "../" from the object path (not the source path used to
# compile) so every object lands under Debug/ instead of literally escaping
# it (see targets/freertos/Makefile.freertos for the full rationale).
sanitize = $(subst ../,,$(1))

C_OBJECTS = $(addprefix $(BUILD_DIR)/,$(call sanitize,$(C_SOURCES:.c=.o)))
ASM_S_OBJECTS = $(addprefix $(BUILD_DIR)/,$(call sanitize,$(ASM_SOURCES_S:.s=.o)))
ASM_UP_OBJECTS = $(addprefix $(BUILD_DIR)/,$(call sanitize,$(ASM_SOURCES_UP:.S=.o)))
OBJECTS = $(C_OBJECTS) $(ASM_S_OBJECTS) $(ASM_UP_OBJECTS)

define C_COMPILE_RULE
$(BUILD_DIR)/$(call sanitize,$(1:.c=.o)): $(1)
	@mkdir -p $$(dir $$@)
	$(CC) -c $(CFLAGS) -MMD -MP -MF"$$(@:.o=.d)" $$< -o $$@
endef
$(foreach src,$(C_SOURCES),$(eval $(call C_COMPILE_RULE,$(src))))

define ASM_S_COMPILE_RULE
$(BUILD_DIR)/$(call sanitize,$(1:.s=.o)): $(1)
	@mkdir -p $$(dir $$@)
	$(AS) -c $(ASFLAGS) $$< -o $$@
endef
$(foreach src,$(ASM_SOURCES_S),$(eval $(call ASM_S_COMPILE_RULE,$(src))))

define ASM_UP_COMPILE_RULE
$(BUILD_DIR)/$(call sanitize,$(1:.S=.o)): $(1)
	@mkdir -p $$(dir $$@)
	$(AS) -c $(ASFLAGS) $$< -o $$@
endef
$(foreach src,$(ASM_SOURCES_UP),$(eval $(call ASM_UP_COMPILE_RULE,$(src))))

# ── Rules ─────────────────────────────────────────────────────────────────
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(BIN) $< $@

clean:
	rm -rf $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/**/*.d)

.PHONY: all clean
