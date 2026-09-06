#include "usbh_conf.h"

#include <zephyr/irq.h>

/* Zephyr owns IRQ routing on this build (HAL's NVIC wrappers were never
 * brought up - no HAL_Init() call anywhere in this target, see
 * hal_shim.c), so registration goes through Zephyr's own IRQ_CONNECT()/
 * irq_enable() instead of HAL_NVIC_*. Registering here (at HCD init time,
 * which only ever runs once) rather than at a static/build-time location
 * matches this driver's runtime-driven bring-up shape and is standard
 * practice for a peripheral Zephyr's own driver model doesn't own.
 * Priority 5 matches every other target's HAL_NVIC priority for the same
 * IRQ. */

extern HCD_HandleTypeDef hhcd_USB_OTG_FS;

void usbHostIrqInit(void)
{
    IRQ_CONNECT(OTG_FS_IRQn, 5, HAL_HCD_IRQHandler, &hhcd_USB_OTG_FS, 0);
    irq_enable(OTG_FS_IRQn);
}

void usbHostIrqDisable(void)
{
    irq_disable(OTG_FS_IRQn);
}
