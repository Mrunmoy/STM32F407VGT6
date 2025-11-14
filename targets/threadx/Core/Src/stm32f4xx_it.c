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
#include "crash_dump.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern TIM_HandleTypeDef htim1;

/* USER CODE BEGIN EV */
extern HCD_HandleTypeDef hhcd_USB_OTG_FS;
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

/* HardFault/MemManage/BusFault/UsageFault below are hand-edited outside the
 * USER CODE markers - CubeMX isn't available in this environment to
 * regenerate this file (see repo CLAUDE.md), same as OTG_FS_IRQHandler
 * further down. Each is a naked trampoline: figure out whether MSP or PSP
 * was the active stack at fault time (LR bit 2), then tail-branch to
 * crashDumpFaultEntry() (app/, shared across every embedded target) with
 * that stack pointer in r0 - the exception stack frame (r0-r3, r12, lr, pc,
 * psr) lives at that address in standard ARMv7-M layout. Naked functions
 * can only contain asm - no C statements, no prologue/epilogue - so this
 * can't be done from inside a USER CODE block. */

/**
  * @brief This function handles Hard fault interrupt.
  */
__attribute__((naked)) void HardFault_Handler(void)
{
  __asm volatile(
    "tst   lr, #4      \n"
    "ite    eq          \n"
    "mrseq  r0, msp      \n"
    "mrsne  r0, psp      \n"
    "b      crashDumpFaultEntry \n"
  );
}

/**
  * @brief This function handles Memory management fault.
  */
__attribute__((naked)) void MemManage_Handler(void)
{
  __asm volatile(
    "tst   lr, #4      \n"
    "ite    eq          \n"
    "mrseq  r0, msp      \n"
    "mrsne  r0, psp      \n"
    "b      crashDumpFaultEntry \n"
  );
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
__attribute__((naked)) void BusFault_Handler(void)
{
  __asm volatile(
    "tst   lr, #4      \n"
    "ite    eq          \n"
    "mrseq  r0, msp      \n"
    "mrsne  r0, psp      \n"
    "b      crashDumpFaultEntry \n"
  );
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
__attribute__((naked)) void UsageFault_Handler(void)
{
  __asm volatile(
    "tst   lr, #4      \n"
    "ite    eq          \n"
    "mrseq  r0, msp      \n"
    "mrsne  r0, psp      \n"
    "b      crashDumpFaultEntry \n"
  );
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

/**
  * @brief This function handles USB On The Go FS global interrupt.
  *
  * Not CubeMX-generated (no CubeMX available in this environment, per repo
  * CLAUDE.md) - added by hand when USB Host support was ported in from the
  * sibling stm32f407-threadx main/FreeRTOS branch. Without this, OTG_FS_IRQn
  * falls through to the weak Default_Handler (an infinite loop) instead of
  * ever reaching HAL_HCD_IRQHandler - confirmed on real hardware via JLink:
  * ICSR.VECTACTIVE pointed at OTG_FS_IRQn (67) with PC stuck in that
  * infinite loop the moment the cable's device-connect interrupt fired.
  */
void OTG_FS_IRQHandler(void)
{
  HAL_HCD_IRQHandler(&hhcd_USB_OTG_FS);
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
