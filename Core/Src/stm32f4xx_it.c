/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usart.h"
#include "gpio.h"
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* Cortex-M4 Fault Status Register Addresses */
#define FAULT_HFSR    ((volatile uint32_t*)0xE000ED2C)  /* HardFault Status Register */
#define FAULT_CFSR    ((volatile uint32_t*)0xE000ED28)  /* Configurable Fault Status Register */
#define FAULT_MMFAR   ((volatile uint32_t*)0xE000ED34)  /* MemManage Fault Address Register */
#define FAULT_BFAR    ((volatile uint32_t*)0xE000ED38)  /* BusFault Address Register */
#define FAULT_AFSR    ((volatile uint32_t*)0xE000ED3C)  /* Auxiliary Fault Status Register */

/* HFSR bit definitions */
#define HFSR_VECTTBL  (1UL << 1)   /* Vector table read fault */
#define HFSR_FORCED   (1UL << 30)  /* Forced HardFault */
#define HFSR_DEBUGEVT (1UL << 31)  /* Debug event */

/* CFSR - MemManage Fault Status (bits 0-7) */
#define MMFSR_IACCVIOL  (1UL << 0)  /* Instruction access violation */
#define MMFSR_DACCVIOL  (1UL << 1)  /* Data access violation */
#define MMFSR_MUNSTKERR (1UL << 3)  /* MemManage on unstacking */
#define MMFSR_MSTKERR   (1UL << 4)  /* MemManage on stacking */
#define MMFSR_MLSPERR   (1UL << 5)  /* MemManage on FP lazy state */
#define MMFSR_MMARVALID (1UL << 7)  /* MMFAR valid */

/* CFSR - BusFault Status (bits 8-15) */
#define BFSR_IBUSERR    (1UL << 8)   /* Instruction bus error */
#define BFSR_PRECISERR  (1UL << 9)   /* Precise data bus error */
#define BFSR_IMPRECISERR (1UL << 10) /* Imprecise data bus error */
#define BFSR_UNSTKERR   (1UL << 11)  /* BusFault on unstacking */
#define BFSR_STKERR     (1UL << 12)  /* BusFault on stacking */
#define BFSR_LSPERR     (1UL << 13)  /* BusFault on FP lazy state */
#define BFSR_BFARVALID  (1UL << 15)  /* BFAR valid */

/* CFSR - UsageFault Status (bits 16-31) */
#define UFSR_UNDEFINSTR (1UL << 16)  /* Undefined instruction */
#define UFSR_INVSTATE   (1UL << 17)  /* Invalid EPSR.T or EPSR.IT */
#define UFSR_INVPC      (1UL << 18)  /* Invalid EXC_RETURN */
#define UFSR_NOCP       (1UL << 19)  /* Coprocessor disabled/absent */
#define UFSR_UNALIGNED  (1UL << 24)  /* Unaligned access */
#define UFSR_DIVBYZERO  (1UL << 25)  /* Divide by zero */

/* Stack frame register indices */
enum { REG_R0, REG_R1, REG_R2, REG_R3, REG_R12, REG_LR, REG_PC, REG_PSR };

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* Exception context structure - stores CPU state at fault time */
typedef struct {
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;
  uint32_t pc;
  uint32_t psr;
  uint32_t sp;
  uint32_t hfsr;
  uint32_t cfsr;
  uint32_t mmfar;
  uint32_t bfar;
} ExceptionContext_t;

static volatile ExceptionContext_t g_ExceptionContext;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
void hard_fault_handler_c(uint32_t *stack_frame);
static void fault_print(const char *str);
static void fault_print_hex(const char *prefix, uint32_t value);
static void fault_delay(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Print string to UART using direct register access (no interrupts) */
static void fault_print(const char *str)
{
  while (*str)
  {
    while (!(USART1->SR & USART_SR_TXE));
    USART1->DR = *str++;
  }
  while (!(USART1->SR & USART_SR_TC));
}

/* Print a 32-bit value as hex with a prefix string */
static void fault_print_hex(const char *prefix, uint32_t value)
{
  static const char hex[] = "0123456789ABCDEF";
  char buf[12];
  int i;

  fault_print(prefix);

  for (i = 7; i >= 0; i--)
  {
    buf[i] = hex[value & 0xF];
    value >>= 4;
  }
  buf[8] = '\r';
  buf[9] = '\n';
  buf[10] = '\0';

  fault_print(buf);
}

/* Rough delay for LED blinking in fault handler */
static void fault_delay(void)
{
  volatile uint32_t i, j;
  for (i = 0; i < 168; i++)
  {
    for (j = 0; j < 100000; j++)
    {
      __NOP();
    }
  }
}

/* Main fault handler - called from assembly wrapper */
void hard_fault_handler_c(uint32_t *stack_frame)
{
#ifndef DEBUG
  uint32_t count = 0;
#endif

  /* Capture registers from stack frame */
  g_ExceptionContext.r0  = stack_frame[REG_R0];
  g_ExceptionContext.r1  = stack_frame[REG_R1];
  g_ExceptionContext.r2  = stack_frame[REG_R2];
  g_ExceptionContext.r3  = stack_frame[REG_R3];
  g_ExceptionContext.r12 = stack_frame[REG_R12];
  g_ExceptionContext.lr  = stack_frame[REG_LR];
  g_ExceptionContext.pc  = stack_frame[REG_PC];
  g_ExceptionContext.psr = stack_frame[REG_PSR];
  g_ExceptionContext.sp  = (uint32_t)stack_frame;

  /* Capture fault status registers */
  g_ExceptionContext.hfsr  = *FAULT_HFSR;
  g_ExceptionContext.cfsr  = *FAULT_CFSR;
  g_ExceptionContext.mmfar = *FAULT_MMFAR;
  g_ExceptionContext.bfar  = *FAULT_BFAR;

  /* Initialize UART if not already done */
  InitUartFromException();

  /* Print crash header */
  fault_print("\r\n\r\n");
  fault_print("========================================\r\n");
  fault_print("          HARD FAULT DETECTED          \r\n");
  fault_print("========================================\r\n\r\n");

  /* Print fault location */
  fault_print_hex("Fault PC  = 0x", g_ExceptionContext.pc);
  fault_print_hex("Fault LR  = 0x", g_ExceptionContext.lr);
  fault_print("\r\n");

  /* Print register dump */
  fault_print("--- Register Dump ---\r\n");
  fault_print_hex("R0  = 0x", g_ExceptionContext.r0);
  fault_print_hex("R1  = 0x", g_ExceptionContext.r1);
  fault_print_hex("R2  = 0x", g_ExceptionContext.r2);
  fault_print_hex("R3  = 0x", g_ExceptionContext.r3);
  fault_print_hex("R12 = 0x", g_ExceptionContext.r12);
  fault_print_hex("LR  = 0x", g_ExceptionContext.lr);
  fault_print_hex("PC  = 0x", g_ExceptionContext.pc);
  fault_print_hex("PSR = 0x", g_ExceptionContext.psr);
  fault_print_hex("SP  = 0x", g_ExceptionContext.sp);
  fault_print("\r\n");

  /* Print fault status registers */
  fault_print("--- Fault Status ---\r\n");
  fault_print_hex("HFSR  = 0x", g_ExceptionContext.hfsr);
  fault_print_hex("CFSR  = 0x", g_ExceptionContext.cfsr);

  /* Decode HFSR */
  if (g_ExceptionContext.hfsr & HFSR_FORCED)
  {
    fault_print("  -> FORCED: Escalated fault\r\n");
  }
  if (g_ExceptionContext.hfsr & HFSR_VECTTBL)
  {
    fault_print("  -> VECTTBL: Vector table read error\r\n");
  }

  /* Decode CFSR - MemManage */
  if (g_ExceptionContext.cfsr & 0xFF)
  {
    fault_print("MemManage Fault:\r\n");
    if (g_ExceptionContext.cfsr & MMFSR_IACCVIOL)
      fault_print("  -> Instruction access violation\r\n");
    if (g_ExceptionContext.cfsr & MMFSR_DACCVIOL)
      fault_print("  -> Data access violation\r\n");
    if (g_ExceptionContext.cfsr & MMFSR_MMARVALID)
      fault_print_hex("  -> MMFAR = 0x", g_ExceptionContext.mmfar);
  }

  /* Decode CFSR - BusFault */
  if (g_ExceptionContext.cfsr & 0xFF00)
  {
    fault_print("BusFault:\r\n");
    if (g_ExceptionContext.cfsr & BFSR_IBUSERR)
      fault_print("  -> Instruction bus error\r\n");
    if (g_ExceptionContext.cfsr & BFSR_PRECISERR)
      fault_print("  -> Precise data bus error\r\n");
    if (g_ExceptionContext.cfsr & BFSR_IMPRECISERR)
      fault_print("  -> Imprecise data bus error\r\n");
    if (g_ExceptionContext.cfsr & BFSR_BFARVALID)
      fault_print_hex("  -> BFAR = 0x", g_ExceptionContext.bfar);
  }

  /* Decode CFSR - UsageFault */
  if (g_ExceptionContext.cfsr & 0xFFFF0000)
  {
    fault_print("UsageFault:\r\n");
    if (g_ExceptionContext.cfsr & UFSR_UNDEFINSTR)
      fault_print("  -> Undefined instruction\r\n");
    if (g_ExceptionContext.cfsr & UFSR_INVSTATE)
      fault_print("  -> Invalid state (EPSR.T)\r\n");
    if (g_ExceptionContext.cfsr & UFSR_INVPC)
      fault_print("  -> Invalid PC (EXC_RETURN)\r\n");
    if (g_ExceptionContext.cfsr & UFSR_NOCP)
      fault_print("  -> Coprocessor error\r\n");
    if (g_ExceptionContext.cfsr & UFSR_UNALIGNED)
      fault_print("  -> Unaligned access\r\n");
    if (g_ExceptionContext.cfsr & UFSR_DIVBYZERO)
      fault_print("  -> Divide by zero\r\n");
  }

  fault_print("\r\n========================================\r\n");

#ifdef DEBUG
  /* In debug mode: breakpoint and halt */
  fault_print("DEBUG MODE: Halting for debugger\r\n");
  __BKPT(0);
  while (1)
  {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    fault_delay();
  }
#else
  /* In release mode: blink LED and reset */
  fault_print("System will reset in 10 seconds...\r\n");

  while (count < 20)  /* ~10 seconds (20 * 0.5s) */
  {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    fault_delay();
    count++;
  }

  /* Reset the system */
  NVIC_SystemReset();
#endif
}

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern TIM_HandleTypeDef htim1;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/* HardFault handler - get the right stack pointer and call C handler */
__attribute__((naked))
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  /* Check LR bit 2 to see if we were using MSP or PSP */
  __asm volatile(
    "TST   LR, #4          \n"
    "ITE   EQ              \n"
    "MRSEQ R0, MSP         \n"
    "MRSNE R0, PSP         \n"
    "B     hard_fault_handler_c \n"
  );
  /* USER CODE END HardFault_IRQn 0 */
}

/* MemManage handler */
__attribute__((naked))
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */
  __asm volatile(
    "TST   LR, #4          \n"
    "ITE   EQ              \n"
    "MRSEQ R0, MSP         \n"
    "MRSNE R0, PSP         \n"
    "B     hard_fault_handler_c \n"
  );
  /* USER CODE END MemoryManagement_IRQn 0 */
}

/* BusFault handler */
__attribute__((naked))
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */
  __asm volatile(
    "TST   LR, #4          \n"
    "ITE   EQ              \n"
    "MRSEQ R0, MSP         \n"
    "MRSNE R0, PSP         \n"
    "B     hard_fault_handler_c \n"
  );
  /* USER CODE END BusFault_IRQn 0 */
}

/* UsageFault handler */
__attribute__((naked))
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */
  __asm volatile(
    "TST   LR, #4          \n"
    "ITE   EQ              \n"
    "MRSEQ R0, MSP         \n"
    "MRSNE R0, PSP         \n"
    "B     hard_fault_handler_c \n"
  );
  /* USER CODE END UsageFault_IRQn 0 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles TIM1 update interrupt and TIM10 global interrupt.
  */
void TIM1_UP_TIM10_IRQHandler(void)
{
  /* USER CODE BEGIN TIM1_UP_TIM10_IRQn 0 */

  /* USER CODE END TIM1_UP_TIM10_IRQn 0 */
  HAL_TIM_IRQHandler(&htim1);
  /* USER CODE BEGIN TIM1_UP_TIM10_IRQn 1 */

  /* USER CODE END TIM1_UP_TIM10_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/*
 * Trigger a test fault. Options:
 *   0 = divide by zero
 *   1 = invalid memory write
 *   2 = undefined instruction (most reliable)
 */
void TriggerTestFault(uint32_t fault_type)
{
  volatile uint32_t *bad_addr;
  volatile uint32_t result;

  switch (fault_type)
  {
    case 0:
      fault_print("Triggering divide by zero...\r\n");
      SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
      result = 1 / fault_type;
      (void)result;
      break;

    case 1:
      fault_print("Triggering invalid memory write...\r\n");
      bad_addr = (volatile uint32_t *)0xCCCCCCCC;
      *bad_addr = 0xDEADBEEF;
      (void)result;
      break;

    case 2:
      fault_print("Triggering undefined instruction...\r\n");
      __asm volatile(".word 0xFFFFFFFF");
      break;

    default:
      fault_print("Unknown fault type\r\n");
      break;
  }
}

/* USER CODE END 1 */
