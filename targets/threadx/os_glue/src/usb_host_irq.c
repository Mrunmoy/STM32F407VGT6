#include "usbh_conf.h"

/* HAL owns the NVIC on this target - registering OTG_FS_IRQn is a plain
 * HAL_NVIC_* call, same priority (5) this project has always used for it. */

void usbHostIrqInit(void)
{
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

void usbHostIrqDisable(void)
{
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
}
