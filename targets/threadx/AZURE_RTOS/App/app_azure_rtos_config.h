
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_azure_rtos_config.h
  * @author  MCD Application Team
  * @brief   azure_rtos config header file
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef APP_AZURE_RTOS_CONFIG_H
#define APP_AZURE_RTOS_CONFIG_H
#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* define the size of static threadX byte memory pools */
/* Bumped from the original 4096 (4 KB). Lesson learned porting the same
 * storage-service showcase (USB Host MSC + FatFS + libcfuture, see
 * ~/sandbox/embedded/stm32f407-threadx main branch's CLAUDE.md "Real
 * hardware bring-up" section) to FreeRTOS on the sibling board: its
 * equivalent heap needed ~80 KB once thread stacks (4 client tasks +
 * servicer, each several KB) + the USBH library's own thread were all
 * accounted for. Sized generously here from the start rather than
 * iteratively bumping and re-flashing against silent tx_byte_allocate()
 * failures. 128 KB SRAM total. */
#define TX_APP_MEM_POOL_SIZE                     65536

/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

#ifdef __cplusplus
}
#endif

#endif /* APP_AZURE_RTOS_CONFIG_H */
