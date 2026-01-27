/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.c
  * @author  MCD Application Team
  * @brief   ThreadX applicative file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
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
#include "app_threadx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "gpio.h"
#include "usart.h"
#include "stm32f4xx_it.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define HEARTBEAT_THREAD_STACK_SIZE     1024
#define APP_THREAD_STACK_SIZE           1024
#define HEARTBEAT_THREAD_PRIORITY       10
#define APP_THREAD_PRIORITY             5
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static TX_THREAD heartbeat_thread;
static TX_THREAD app_thread;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static void heartbeat_thread_entry(ULONG thread_input);
static void app_thread_entry(ULONG thread_input);
/* USER CODE END PFP */

/**
  * @brief  Application ThreadX Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT App_ThreadX_Init(VOID *memory_ptr)
{
  UINT ret = TX_SUCCESS;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;

  /* USER CODE BEGIN App_ThreadX_Init */
  CHAR *pointer;

  /* Allocate stack for heartbeat thread */
  if (tx_byte_allocate(byte_pool, (VOID **)&pointer, HEARTBEAT_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

  /* Create heartbeat thread */
  if (tx_thread_create(&heartbeat_thread, "Heartbeat Thread", heartbeat_thread_entry, 0,
                       pointer, HEARTBEAT_THREAD_STACK_SIZE,
                       HEARTBEAT_THREAD_PRIORITY, HEARTBEAT_THREAD_PRIORITY,
                       TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  /* Allocate stack for application thread */
  if (tx_byte_allocate(byte_pool, (VOID **)&pointer, APP_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

  /* Create application thread */
  if (tx_thread_create(&app_thread, "App Thread", app_thread_entry, 0,
                       pointer, APP_THREAD_STACK_SIZE,
                       APP_THREAD_PRIORITY, APP_THREAD_PRIORITY,
                       TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }
  /* USER CODE END App_ThreadX_Init */

  return ret;
}

/**
  * @brief  MX_ThreadX_Init
  * @param  None
  * @retval None
  */
void MX_ThreadX_Init(void)
{
  /* USER CODE BEGIN  Before_Kernel_Start */

  /* USER CODE END  Before_Kernel_Start */

  tx_kernel_enter();

  /* USER CODE BEGIN  Kernel_Start_Error */

  /* USER CODE END  Kernel_Start_Error */
}

/* USER CODE BEGIN 1 */

/**
  * @brief  Heartbeat thread entry - toggles LED on PC13 every 500ms
  * @param  thread_input: unused
  * @retval None
  */
static void heartbeat_thread_entry(ULONG thread_input)
{
  (void)thread_input;

  while (1)
  {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 2);  /* 500ms */
  }
}

/**
  * @brief  Application thread entry - periodic work with UART output
  * @param  thread_input: unused
  * @retval None
  */
static void app_thread_entry(ULONG thread_input)
{
  (void)thread_input;
  uint32_t counter = 0;
  char msg[64];

  /* Send startup message */
  const char *startup_msg = "Application started\r\n";
  HAL_UART_Transmit(&huart1, (uint8_t *)startup_msg, strlen(startup_msg), HAL_MAX_DELAY);

  while (1)
  {
    /* Do some periodic work */
    counter++;

    /* Print status every iteration */
    int len = snprintf(msg, sizeof(msg), "App running: count=%lu\r\n", counter);
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, len, HAL_MAX_DELAY);

    /* Trigger a test fault after 5 iterations */
    if (counter == 5)
    {
      TriggerTestFault(2);
    }

    /* Sleep for 2 seconds */
    tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND * 2);
  }
}

/* USER CODE END 1 */
