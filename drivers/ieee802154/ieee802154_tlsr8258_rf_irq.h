/* SPDX-License-Identifier: Apache-2.0 */

#ifndef IEEE802154_TLSR8258_RF_IRQ_H_
#define IEEE802154_TLSR8258_RF_IRQ_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint16_t tlsr8258_rf_irq_runtime_mask(void);
bool tlsr8258_rf_irq_has_rx_event(uint16_t irq);
uint16_t tlsr8258_rf_irq_effective_status(uint16_t irq, const uint8_t *rx, size_t rx_size);

#endif /* IEEE802154_TLSR8258_RF_IRQ_H_ */
