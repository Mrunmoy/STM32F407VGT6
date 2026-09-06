/**
 * @file usbh_conf.c
 * @brief HCD MSP init + USBH_LL_* low-level driver callbacks for USB_OTG_FS
 *        in Host mode on this board (FK407M2-ZGT6, STM32F407ZGT6, PA11=DM,
 *        PA12=DP, embedded PHY). Shared across every embedded target (host
 *        has no USB) - see usbh_conf.h's own top comment for what's
 *        genuinely injected per target (IRQ registration, the heap) and
 *        why the rest of this file needs no target-specific variant at all.
 */

#include "usbh_core.h"

#include "usbh_conf.h"

void Error_Handler(void);

/* USB Host Core handle declaration - named to match this project's device-
 * side hpcd_USB_OTG_FS naming convention, on the targets that have one. */
HCD_HandleTypeDef hhcd_USB_OTG_FS;

/*******************************************************************************
                       HCD MSP Routines
*******************************************************************************/

void HAL_HCD_MspInit(HCD_HandleTypeDef *hhcd)
{
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };

    if (hhcd->Instance == USB_OTG_FS)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();

        /* USB_OTG_FS GPIO Configuration
         * PA11 ------> USB_OTG_FS_DM
         * PA12 ------> USB_OTG_FS_DP */
        GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        /* Peripheral clock enable */
        __HAL_RCC_USB_OTG_FS_CLK_ENABLE();

        usbHostIrqInit();
    }
}

void HAL_HCD_MspDeInit(HCD_HandleTypeDef *hhcd)
{
    if (hhcd->Instance == USB_OTG_FS)
    {
        __HAL_RCC_USB_OTG_FS_CLK_DISABLE();

        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);

        usbHostIrqDisable();
    }
}

/*******************************************************************************
                       LL Driver Callbacks (HCD -> USB Host Library)
*******************************************************************************/

void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *hhcd)
{
    USBH_LL_IncTimer(hhcd->pData);
}

void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *hhcd)
{
    USBH_LL_Connect(hhcd->pData);
}

void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hhcd)
{
    USBH_LL_Disconnect(hhcd->pData);
}

void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef *hhcd)
{
    USBH_LL_PortEnabled(hhcd->pData);
}

void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *hhcd)
{
    USBH_LL_PortDisabled(hhcd->pData);
}

void HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef *hhcd, uint8_t chnum, HCD_URBStateTypeDef urbState)
{
    (void)hhcd;
    (void)chnum;
    (void)urbState;
    /* USBH_USE_OS is always 0 in this project (see usbh_conf.h) - the
     * USBH_LL_NotifyURBChange() path is only relevant when USBH_USE_OS==1. */
}

/*******************************************************************************
                       LL Driver Interface (USB Host Library --> HCD)
*******************************************************************************/

USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef *phost)
{
    hhcd_USB_OTG_FS.Instance = USB_OTG_FS;
    hhcd_USB_OTG_FS.Init.Host_channels = 11U;
    hhcd_USB_OTG_FS.Init.dma_enable = 0U;
    hhcd_USB_OTG_FS.Init.low_power_enable = 0U;
    hhcd_USB_OTG_FS.Init.phy_itface = HCD_PHY_EMBEDDED;
    hhcd_USB_OTG_FS.Init.Sof_enable = 0U;
    hhcd_USB_OTG_FS.Init.speed = HCD_SPEED_FULL;
    /* No VBUS-sense GPIO/comparator on this board (schematic confirmed no
     * VBUS switch either - see CLAUDE.md). */
    hhcd_USB_OTG_FS.Init.vbus_sensing_enable = 0U;
    hhcd_USB_OTG_FS.Init.lpm_enable = 0U;

    hhcd_USB_OTG_FS.pData = phost;
    phost->pData = &hhcd_USB_OTG_FS;

    if (HAL_HCD_Init(&hhcd_USB_OTG_FS) != HAL_OK)
    {
        Error_Handler();
    }

    USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&hhcd_USB_OTG_FS));

    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef *phost)
{
    HAL_HCD_DeInit(phost->pData);
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef *phost)
{
    HAL_HCD_Start(phost->pData);
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef *phost)
{
    HAL_HCD_Stop(phost->pData);
    return USBH_OK;
}

USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef *phost)
{
    USBH_SpeedTypeDef speed = USBH_SPEED_FULL;

    switch (HAL_HCD_GetCurrentSpeed(phost->pData))
    {
        case 0U:
            speed = USBH_SPEED_HIGH;
            break;

        case 1U:
            speed = USBH_SPEED_FULL;
            break;

        case 2U:
            speed = USBH_SPEED_LOW;
            break;

        default:
            speed = USBH_SPEED_FULL;
            break;
    }

    return speed;
}

USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef *phost)
{
    HAL_HCD_ResetPort(phost->pData);
    return USBH_OK;
}

uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    return HAL_HCD_HC_GetXferCount(phost->pData, pipe);
}

USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef *phost, uint8_t pipe, uint8_t epnum,
    uint8_t devAddress, uint8_t speed, uint8_t epType, uint16_t mps)
{
    HAL_HCD_HC_Init(phost->pData, pipe, epnum, devAddress, speed, epType, mps);
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    HAL_HCD_HC_Halt(phost->pData, pipe);
    return USBH_OK;
}

/* USBH_IN_NAK_PROCESS is not defined in usbh_conf.h - this definition
 * exists only to satisfy usbh_core.h's unconditional declaration; it is
 * never actually invoked. */
USBH_StatusTypeDef USBH_LL_ActivatePipe(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    (void)phost;
    (void)pipe;
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef *phost, uint8_t pipe, uint8_t direction,
    uint8_t epType, uint8_t token, uint8_t *buffer, uint16_t length, uint8_t doPing)
{
    HAL_HCD_HC_SubmitRequest(phost->pData, pipe, direction, epType, token, buffer, length, doPing);
    return USBH_OK;
}

USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    return (USBH_URBStateTypeDef)HAL_HCD_HC_GetURBState(phost->pData, pipe);
}

/**
 * @brief  Drives VBUS.
 *
 * This board has no VBUS power switch at all (confirmed from schematic -
 * VBUS is wired straight to the 5V rail), so this is unconditionally a
 * no-op on every target.
 */
USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *phost, uint8_t state)
{
    (void)phost;
    (void)state;

    HAL_Delay(200U);

    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *phost, uint8_t pipe, uint8_t toggle)
{
    (void)phost;

    if (hhcd_USB_OTG_FS.hc[pipe].ep_is_in != 0U)
    {
        hhcd_USB_OTG_FS.hc[pipe].toggle_in = toggle;
    }
    else
    {
        hhcd_USB_OTG_FS.hc[pipe].toggle_out = toggle;
    }

    return USBH_OK;
}

uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    uint8_t toggle;

    (void)phost;

    if (hhcd_USB_OTG_FS.hc[pipe].ep_is_in != 0U)
    {
        toggle = hhcd_USB_OTG_FS.hc[pipe].toggle_in;
    }
    else
    {
        toggle = hhcd_USB_OTG_FS.hc[pipe].toggle_out;
    }

    return toggle;
}

/**
 * @brief  Delay routine for the USB Host Library. USBH_USE_OS is always 0
 *         in this project, so this is always the HAL_Delay() path.
 */
void USBH_Delay(uint32_t Delay)
{
    HAL_Delay(Delay);
}
