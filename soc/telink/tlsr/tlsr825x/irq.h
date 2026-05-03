/*
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * TLSR8258 Interrupt Mapping
 *
 * IRQ  Mask bit   Name          Type    Clear owner
 * ---  --------  ----          ----    ----------
 *  0   0x000001  TMR0          level   timer: reg_tmr_sta bit0 + reg_irq_src
 *  1   0x000002  TMR1          level   timer: reg_tmr_sta bit1 + reg_irq_src
 *  2   0x000004  TMR2          level   timer: reg_tmr_sta bit2 + reg_irq_src
 *  3   0x000008  USB_PWDN      level   USB driver: module status + reg_irq_src
 *  4   0x000010  DMA           level   DMA driver: reg_dma_status + reg_irq_src
 *  5   0x000020  DFIFO         level   DMA FIFO driver + reg_irq_src
 *  6   0x000040  UART          level   UART driver: reg_uart_status + reg_irq_src
 *  7   0x000080  MIX_CMD       level   RF mix driver + reg_irq_src
 *  8   0x000100  EP0_SETUP     level   USB driver + reg_irq_src
 *  9   0x000200  EP0_DAT       level   USB driver + reg_irq_src
 * 10   0x000400  EP0_STA       level   USB driver + reg_irq_src
 * 11   0x000800  SET_INTF      level   USB driver + reg_irq_src
 * 12   0x001000  EP_DATA       level   USB driver + reg_irq_src
 * 13   0x002000  ZB_RT        level   RF driver + reg_irq_src
 * 14   0x004000  SW_PWM        level   PWM driver + reg_irq_src
 * --   0x008000  (reserved)    --     --
 * 16   0x010000  USB_250US     edge    USB driver: reg_usb_status + reg_irq_src
 * 17   0x020000  USB_RST      edge    USB driver + reg_irq_src
 * 18   0x040000  GPIO         level   GPIO driver: reg_gpio_status + reg_irq_src
 * 19   0x080000  PM           level   PM driver + reg_irq_src
 * 20   0x100000  SYSTEM_TIMER edge    stimer driver: reg_irq_src (auto-advancing)
 * 21   0x200000  GPIO_RISC0   edge    GPIO driver: reg_risc0 + reg_irq_src
 * 22   0x400000  GPIO_RISC1   edge    GPIO driver: reg_risc1 + reg_irq_src
 * --   0x800000  (reserved)    --     --
 *
 * Level-triggered (bits 0-14): Clear reg_irq_src first, then module status register.
 * Edge-triggered (bits 16-22): Clear reg_irq_src before or after ISR as needed.
 *
 * Clear sequence per vendor SDK (irq_handler.c):
 *   TMR0/TMR1: reg_irq_src = FLD_IRQ_TMRx, reg_tmr_sta = FLD_TMR_STA_TMRx, then handler
 *
 * Register addresses (from vendor datasheet):
 *   0x800640: reg_irq_mask (24-bit mask/source bits)
 *   0x800643: reg_irq_en (global enable, bit 0)
 *   0x800648: reg_irq_src (interrupt source flags)
 *   0x80064a: reg_irq_clear (write 1 to clear edge flags)
 *   0x800623: reg_tmr_sta (timer status, bits 0-2 for TMR0-2)
 */

#ifndef SOC_TELINK_TLSR825X_IRQ_H_
#define SOC_TELINK_TLSR825X_IRQ_H_

#define TLSR8258_IRQ_TMR0           0
#define TLSR8258_IRQ_TMR1           1
#define TLSR8258_IRQ_TMR2           2
#define TLSR8258_IRQ_USB_PWDN       3
#define TLSR8258_IRQ_DMA            4
#define TLSR8258_IRQ_DFIFO          5
#define TLSR8258_IRQ_UART          6
#define TLSR8258_IRQ_MIX_CMD        7
#define TLSR8258_IRQ_EP0_SETUP      8
#define TLSR8258_IRQ_EP0_DAT        9
#define TLSR8258_IRQ_EP0_STA       10
#define TLSR8258_IRQ_SET_INTF      11
#define TLSR8258_IRQ_EP_DATA      12
#define TLSR8258_IRQ_ZB_RT         13
#define TLSR8258_IRQ_SW_PWM         14
/* IRQ 15 reserved */
#define TLSR8258_IRQ_USB_250US    16
#define TLSR8258_IRQ_USB_RST       17
#define TLSR8258_IRQ_GPIO          18
#define TLSR8258_IRQ_PM            19
#define TLSR8258_IRQ_SYSTEM_TIMER 20
#define TLSR8258_IRQ_GPIO_RISC0   21
#define TLSR8258_IRQ_GPIO_RISC1   22
/* IRQ 23 reserved */

#define TLSR8258_NUM_IRQS          24

#define TLSR8258_IRQ_VALID_MASK    0x007F7FFF

#define TLSR8258_REG_IRQ_MASK  ((volatile uint32_t *)0x00800640u)
#define TLSR8258_REG_IRQ_EN   ((volatile uint8_t *)0x00800643u)
#define TLSR8258_REG_IRQ_SRC ((volatile uint32_t *)0x00800648u)

#define TLSR8258_REG_TMR_STA ((volatile uint8_t *)0x00800623u)

#endif /* SOC_TELINK_TLSR825X_IRQ_H_ */