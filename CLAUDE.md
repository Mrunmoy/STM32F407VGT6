# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

STM32F407VGTx embedded project using Azure ThreadX RTOS. Target hardware is an STM32F407VGT6 microcontroller (Cortex-M4F, 1MB Flash, 128KB RAM, 64KB CCMRAM).

## Build System

This is an STM32CubeIDE project using Eclipse CDT managed build with GCC ARM toolchain.

**Building:**
- Open project in STM32CubeIDE
- Build: Project → Build Project (Ctrl+B)
- Output files in `Debug/`: `stm32f407.elf`, `stm32f407.hex`, `stm32f407.bin`

**Flashing/Debugging:**
- Requires ST-LINK v2 or compatible debugger
- Run → Debug As → STM32 MCU C/C++ Application

**Compiler flags (from .cproject):**
- Target: `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`
- Key defines: `STM32F407xx`, `USE_HAL_DRIVER`, `TX_INCLUDE_USER_DEFINE_FILE`, `STM32_THREAD_SAFE_STRATEGY=2`

## Architecture

### Boot Flow
```
main() → HAL_Init() → SystemClock_Config() → MX_GPIO_Init() → MX_USART1_UART_Init()
       → MX_ThreadX_Init() → tx_kernel_enter() [control never returns]
           → tx_application_define() → App_ThreadX_Init()
```

### Key Directories
- `Core/Src/` - Application code (main.c, app_threadx.c, peripheral init)
- `Core/Inc/` - Application headers
- `Core/ThreadSafe/` - Newlib thread-safety wrappers for ThreadX
- `AZURE_RTOS/App/` - ThreadX initialization and byte pool setup
- `Middlewares/ST/threadx/` - ThreadX kernel (common/ and ports/cortex_m4/gnu/)
- `Drivers/STM32F4xx_HAL_Driver/` - STM32 HAL library
- `Drivers/CMSIS/` - ARM CMSIS headers and device definitions

### Memory Layout (from STM32F407VGTX_FLASH.ld)
```
FLASH:  0x08000000 (1024KB)
RAM:    0x20000000 (128KB)
CCMRAM: 0x10000000 (64KB)
```

### ThreadX Configuration
- Byte pool: 2048 bytes (TX_APP_MEM_POOL_SIZE in `AZURE_RTOS/App/app_azure_rtos_config.h`)
- Tick rate: 100Hz (10ms per tick), configured in `Core/Src/tx_initialize_low_level.s`
- User config: `Core/Inc/tx_user.h`
- Application entry point: `App_ThreadX_Init()` in `Core/Src/app_threadx.c`

### Configured Peripherals
- **GPIO PC13**: Output (on-board LED)
- **USART1**: PA9 (TX), PA10 (RX) - async serial
- **TIM1**: System timebase for HAL and ThreadX scheduler

## Code Generation

Hardware configuration is defined in `stm32f407.ioc` (STM32CubeMX project file).

**Regenerating code:**
1. Open `stm32f407.ioc` in STM32CubeMX
2. Modify peripheral configuration
3. Click "Generate Code"

User code between `/* USER CODE BEGIN */` and `/* USER CODE END */` markers is preserved during regeneration. Place custom code only within these markers in generated files.

## Adding ThreadX Threads

In `Core/Src/app_threadx.c`, within `App_ThreadX_Init()`:

```c
TX_THREAD my_thread;
CHAR *stack_ptr;
tx_byte_allocate(byte_pool, (VOID**)&stack_ptr, 256, TX_NO_WAIT);
tx_thread_create(&my_thread, "MyThread", my_thread_entry, 0,
                 stack_ptr, 256, 1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);
```

Keep thread stacks small (256-512 bytes) due to limited byte pool.

## Clock Configuration

Currently running on HSI at 16 MHz (PLL disabled). To enable 168 MHz operation, modify `SystemClock_Config()` in `Core/Src/main.c` to configure the PLL.
