#ifndef IEEE802154_TLSR8258_TX_IRQ_H_
#define IEEE802154_TLSR8258_TX_IRQ_H_

#include <stdbool.h>
#include <stdint.h>

bool tlsr8258_tx_irq_indicates_success(uint16_t irq);
uint16_t tlsr8258_tx_irq_session_mask(uint16_t base_mask, bool expect_post_tx_followup);
uint16_t tlsr8258_tx_irq_start_clear_mask(bool expect_post_tx_followup);
bool tlsr8258_tx_force_manual_off_before_start(bool expect_post_tx_followup);
bool tlsr8258_tx_poll_needs_rx_rearm(bool expect_post_tx_followup, bool completed_in_isr);

#endif /* IEEE802154_TLSR8258_TX_IRQ_H_ */
