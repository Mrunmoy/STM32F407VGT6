# Coredump Feature

This branch adds crash dump functionality to the STM32F407 ThreadX project.

## What we implemented

When a CPU fault occurs (HardFault, BusFault, MemManage, or UsageFault), the system captures diagnostic info and outputs it over UART before halting or resetting.

### Changes made

**stm32f4xx_it.c**
- Replaced the default fault handlers with naked assembly wrappers that determine which stack (MSP or PSP) was in use and pass it to a C handler
- Added `hard_fault_handler_c()` which extracts the CPU registers from the exception stack frame, reads the fault status registers, and prints everything over UART
- Added `TriggerTestFault()` for testing different fault types

**usart.c**
- Added `InitUartFromException()` which can initialize USART1 directly via registers without using HAL interrupts or RTOS. This is needed because a fault might happen before normal UART init, or while RTOS is in a bad state.

**app_threadx.c**
- Added a test trigger that causes a fault after 5 iterations (for testing purposes)

### How it works

1. Fault occurs (e.g. invalid memory access, undefined instruction)
2. CPU vectors to the fault handler
3. Our naked assembly wrapper checks LR bit 2 to determine if MSP or PSP was active
4. Passes the stack pointer to the C handler
5. C handler extracts r0-r3, r12, lr, pc, psr from the stack frame
6. Reads HFSR, CFSR, MMFAR, BFAR from the System Control Block
7. Initializes UART if needed (direct register writes, no interrupts)
8. Prints the crash dump using blocking UART writes
9. In debug mode: hits breakpoint and blinks LED forever
10. In release mode: blinks LED for 10 seconds then resets

### Fault registers captured

- **HFSR** (0xE000ED2C) - HardFault Status Register
- **CFSR** (0xE000ED28) - Configurable Fault Status Register
  - Bits 0-7: MemManage fault status
  - Bits 8-15: BusFault status
  - Bits 16-31: UsageFault status
- **MMFAR** (0xE000ED34) - MemManage Fault Address (if valid)
- **BFAR** (0xE000ED38) - BusFault Address (if valid)

## Example output

```
========================================
          HARD FAULT DETECTED
========================================

Fault PC  = 0x080012A2
Fault LR  = 0x080012A3

--- Register Dump ---
R0  = 0x080062B8
R1  = 0x00000040
R2  = 0x0000000A
R3  = 0x00000040
R12 = 0x0000000A
LR  = 0x080012A3
PC  = 0x080012A2
PSR = 0x01000000
SP  = 0x200007C0

--- Fault Status ---
HFSR  = 0x40000000
CFSR  = 0x00010000
  -> FORCED: Escalated fault
UsageFault:
  -> Undefined instruction

========================================
DEBUG MODE: Halting for debugger
```

## Testing

The `TriggerTestFault()` function triggers different fault types:

```c
TriggerTestFault(0);  // Divide by zero
TriggerTestFault(1);  // Invalid memory write
TriggerTestFault(2);  // Undefined instruction (most reliable)
```

By default, the app thread calls `TriggerTestFault(2)` after 5 iterations. Change or remove this in `app_threadx.c`.

## Tracing faults to source code

### Using the crash monitor (recommended)

There's a Python script in `tools/crash_monitor.py` that monitors the serial port and automatically decodes crash addresses:

```
cd tools
pip install pyserial
python crash_monitor.py -p COM3 -e ../Debug/stm32f407.elf
```

When a crash occurs, it automatically shows the source location:

```
========================================
          HARD FAULT DETECTED
========================================

Fault PC  = 0x080012A2
         >>> Fault PC: TriggerTestFault at Core/Src/stm32f4xx_it.c:489
Fault LR  = 0x080012A3
         >>> Fault LR: TriggerTestFault at Core/Src/stm32f4xx_it.c:489
```

### Manual decoding with addr2line

You can also manually decode addresses:

```
arm-none-eabi-addr2line -e Debug/stm32f407.elf -f -C 0x080012A2
```

Output:
```
TriggerTestFault
Core/Src/stm32f4xx_it.c:489
```

The `.list` file in the Debug folder has the full disassembly if you need more context.

---

## Future enhancement: Persistent crash data across resets

The current implementation prints the crash dump immediately, but if the system is in a reboot loop, you might not see the output in time. An alternative approach is to save the crash data to a reserved RAM section that survives reset.

### How it would work

1. Reserve a small section of RAM (e.g. 256 bytes) at the end of SRAM
2. Mark it as "noinit" so the startup code doesn't zero it
3. On fault: write crash data to this section, then reset
4. On boot: check if the reserved section contains valid crash data (use a magic number)
5. If valid: print the stored crash data, then clear it
6. Continue normal boot

### Linker script changes

Add a new memory region and section in the linker script:

```ld
MEMORY
{
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K - 256  /* Main RAM, reduced */
  CRASH  (rw)  : ORIGIN = 0x2001FF00, LENGTH = 256         /* Reserved for crash data */
  ...
}

SECTIONS
{
  ...

  /* Crash data section - not initialized by startup */
  .crash_data (NOLOAD) :
  {
    _crash_data_start = .;
    *(.crash_data)
    _crash_data_end = .;
  } > CRASH
}
```

### Code changes

Define the crash data structure with the noinit attribute:

```c
#define CRASH_MAGIC 0xDEADC0DE

typedef struct {
  uint32_t magic;
  uint32_t pc;
  uint32_t lr;
  uint32_t r0, r1, r2, r3, r12;
  uint32_t psr;
  uint32_t sp;
  uint32_t hfsr;
  uint32_t cfsr;
  uint32_t mmfar;
  uint32_t bfar;
} CrashData_t;

__attribute__((section(".crash_data")))
static CrashData_t g_CrashData;
```

In the fault handler, write to this structure and reset:

```c
void hard_fault_handler_c(uint32_t *stack_frame)
{
  g_CrashData.magic = CRASH_MAGIC;
  g_CrashData.pc = stack_frame[REG_PC];
  g_CrashData.lr = stack_frame[REG_LR];
  // ... save other registers ...

  NVIC_SystemReset();
}
```

At startup (in main.c before normal init), check and print:

```c
void CheckCrashData(void)
{
  if (g_CrashData.magic == CRASH_MAGIC)
  {
    InitUartFromException();
    fault_print("=== CRASH DATA FROM PREVIOUS RUN ===\r\n");
    fault_print_hex("PC  = 0x", g_CrashData.pc);
    fault_print_hex("LR  = 0x", g_CrashData.lr);
    // ... print other fields ...

    g_CrashData.magic = 0;  // Clear so we don't print again
  }
}
```

This way, even if the system crashes immediately after boot, you'll see the crash data from the previous run.

### Considerations

- The RAM contents survive a software reset (NVIC_SystemReset) but not a power cycle
- You could also write to backup SRAM (if available) or flash for true persistence
- Make sure the reserved section doesn't overlap with the stack or heap
