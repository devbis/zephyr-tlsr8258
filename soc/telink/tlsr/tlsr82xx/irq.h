/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * TLSR82XX Interrupt Mapping (TLSR8258 and TLSR8269 share this layout)
 *
 * IRQ  Mask bit   Name          Type    Clear owner
 * ---  --------  ----          ----    ----------
 *  0   0x000001  TMR0          level   arch timer: parent src, reg_tmr_sta bit0
 *  1   0x000002  TMR1          level   arch timer: parent src, reg_tmr_sta bit1
 *  2   0x000004  TMR2          level   arch timer: parent src, reg_tmr_sta bit2
 *  3   0x000008  USB_PWDN      level   USB driver
 *  4   0x000010  DMA           level   DMA driver
 *  5   0x000020  DFIFO         level   DMA FIFO driver
 *  6   0x000040  UART          level   UART driver
 *  7   0x000080  CMD           level   RF/mix driver (MIX_CMD on 825x, HOST_CMD on 826x)
 *  8   0x000100  EP0_SETUP     level   USB driver
 *  9   0x000200  EP0_DAT       level   USB driver
 * 10   0x000400  EP0_STA       level   USB driver
 * 11   0x000800  SET_INTF      level   USB driver
 * 12   0x001000  EP_DATA       level   USB driver
 * 13   0x002000  ZB_RT         level   RF driver
 * 14   0x004000  SW           level   PWM driver (SW_PWM on 825x, SW_EN on 826x)
 * --   0x008000  (reserved)    --     --
 * 16   0x010000  USB_250US     edge    USB driver
 * 17   0x020000  USB_RST       edge    USB driver
 * 18   0x040000  GPIO          edge    GPIO driver
 * 19   0x080000  PM            edge    PM driver
 * 20   0x100000  SYSTEM_TIMER  edge    stimer driver: parent src
 * 21   0x200000  GPIO_RISC0    edge    GPIO driver
 * 22   0x400000  GPIO_RISC1    edge    GPIO driver
 * 23   0x800000  GPIO_RISC2    edge    GPIO driver (826x only; reserved on 825x)
 *
 * Clear ownership:
 * - The arch dispatcher clears TMR0..TMR2 itself.
 * - Every other source is cleared by the driver that owns it; the
 *   dispatcher leaves those flags alone.
 * - "Parent src" is the IRQSRC bit at 0x648..0x64a. Edge sources (bits
 *   16..22) are cleared with a 24-bit reg_irq_src write, as the vendor SDK
 *   does, through tlsr82xx_irq_clear_parent().
 * - TMR0/TMR1: write FLD_IRQ_TMRx to reg_irq_src and FLD_TMR_STA_TMRx to
 *   reg_tmr_sta, then run the handler.
 *
 * Register addresses (from vendor datasheet):
 *   0x800640: reg_irq_mask (24 bits; shares a 32-bit word with reg_irq_en,
 *             so never access it with a 32-bit read-modify-write)
 *   0x800643: reg_irq_en (global enable, bit 0)
 *   0x800648: reg_irq_src (interrupt source flags)
 *   0x80064a: reg_irq_clear (write 1 to clear edge flags)
 *   0x800623: reg_tmr_sta (timer status, bits 0-2 for TMR0-2)
 */

#ifndef SOC_TELINK_TLSR82XX_IRQ_H_
#define SOC_TELINK_TLSR82XX_IRQ_H_

#include <zephyr/sys/util.h>

#define TLSR82XX_IRQ_TMR0           0
#define TLSR82XX_IRQ_TMR1           1
#define TLSR82XX_IRQ_TMR2           2
#define TLSR82XX_IRQ_USB_PWDN       3
#define TLSR82XX_IRQ_DMA            4
#define TLSR82XX_IRQ_DFIFO          5
#define TLSR82XX_IRQ_UART          6
#define TLSR82XX_IRQ_CMD        7
#define TLSR82XX_IRQ_EP0_SETUP      8
#define TLSR82XX_IRQ_EP0_DAT        9
#define TLSR82XX_IRQ_EP0_STA       10
#define TLSR82XX_IRQ_SET_INTF      11
#define TLSR82XX_IRQ_EP_DATA      12
#define TLSR82XX_IRQ_ZB_RT         13
#define TLSR82XX_IRQ_SW         14
/* IRQ 15 reserved */
#define TLSR82XX_IRQ_USB_250US    16
#define TLSR82XX_IRQ_USB_RST       17
#define TLSR82XX_IRQ_GPIO          18
#define TLSR82XX_IRQ_PM            19
#define TLSR82XX_IRQ_SYSTEM_TIMER 20
#define TLSR82XX_IRQ_GPIO_RISC0   21
#define TLSR82XX_IRQ_GPIO_RISC1   22
#define TLSR82XX_IRQ_GPIO_RISC2   23

#define TLSR82XX_NUM_IRQS          24

/*
 * Line the dispatcher serves first and alone: the radio. Its handler has to
 * turn a frame around within the IEEE 802.15.4 ACK time, which a timer tick
 * serviced in the same entry (20 to 100 us) can break.
 */
#define TLSR82XX_IRQ_PREFERRED     TLSR82XX_IRQ_ZB_RT

/* 826x: bit 15 (AN_EN) is not used by this port, bit 23 is GPIO_RISC2. */
#define TLSR82XX_IRQ_RESERVED_MASK \
	(BIT(15) | (IS_ENABLED(CONFIG_SOC_SERIES_TLSR826X) ? 0u : BIT(23)))
#define TLSR82XX_IRQ_TIMER_MASK    (BIT(TLSR82XX_IRQ_TMR0) | \
				    BIT(TLSR82XX_IRQ_TMR1) | \
				    BIT(TLSR82XX_IRQ_TMR2))
#define TLSR82XX_IRQ_LEVEL_MASK    GENMASK(15, 0)
#define TLSR82XX_IRQ_EDGE_MASK     GENMASK(23, 16)
#define TLSR82XX_IRQ_VALID_MASK    (~TLSR82XX_IRQ_RESERVED_MASK & GENMASK(23, 0))

/* Addresses without a type, for the assembly entry and exit paths. */
#define TLSR82XX_REG_IRQ_MASK_ADDR 0x00800640
#define TLSR82XX_REG_IRQ_EN_ADDR   0x00800643

#define TLSR82XX_REG_IRQ_MASK  ((volatile uint8_t *)TLSR82XX_REG_IRQ_MASK_ADDR)
#define TLSR82XX_REG_IRQ_EN   ((volatile uint8_t *)TLSR82XX_REG_IRQ_EN_ADDR)
/*
 * A word store here also covers 0x64b. The vendor SDK clears edge sources
 * with this 32-bit write and so does tlsr82xx_irq_clear_parent().
 */
#define TLSR82XX_REG_IRQ_SRC ((volatile uint32_t *)0x00800648u)

#define TLSR82XX_REG_TMR_STA ((volatile uint8_t *)0x00800623u)

#ifndef _ASMLANGUAGE
/* reg_irq_mask is 24-bit and is adjacent to the global enable byte. */
static inline uint32_t tlsr82xx_irq_mask_read(void)
{
	return (uint32_t)TLSR82XX_REG_IRQ_MASK[0] |
		((uint32_t)TLSR82XX_REG_IRQ_MASK[1] << 8) |
		((uint32_t)TLSR82XX_REG_IRQ_MASK[2] << 16);
}

static inline void tlsr82xx_irq_mask_write(uint32_t mask)
{
	TLSR82XX_REG_IRQ_MASK[0] = (uint8_t)mask;
	TLSR82XX_REG_IRQ_MASK[1] = (uint8_t)(mask >> 8);
	TLSR82XX_REG_IRQ_MASK[2] = (uint8_t)(mask >> 16);
}

static inline uint32_t tlsr82xx_irq_bit(unsigned int irq)
{
	return irq < TLSR82XX_NUM_IRQS ? BIT(irq) : 0u;
}

static inline bool tlsr82xx_irq_is_valid(unsigned int irq)
{
	return (tlsr82xx_irq_bit(irq) & TLSR82XX_IRQ_VALID_MASK) != 0u;
}

static inline bool tlsr82xx_irq_is_edge(unsigned int irq)
{
	return (tlsr82xx_irq_bit(irq) & TLSR82XX_IRQ_EDGE_MASK) != 0u;
}

static inline void tlsr82xx_irq_clear_parent(unsigned int irq)
{
	uint32_t bit = tlsr82xx_irq_bit(irq);

	if ((bit & TLSR82XX_IRQ_VALID_MASK) != 0u) {
		*TLSR82XX_REG_IRQ_SRC = bit;
	}
}

static inline void tlsr82xx_irq_clear_edge(unsigned int irq)
{
	if (tlsr82xx_irq_is_edge(irq)) {
		tlsr82xx_irq_clear_parent(irq);
	}
}
#endif /* _ASMLANGUAGE */

#endif /* SOC_TELINK_TLSR82XX_IRQ_H_ */
