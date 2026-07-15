#include "ieee802154_tlsr8258_tx_irq.h"

#ifndef BIT
#define BIT(n) (1u << (n))
#endif

bool tlsr8258_tx_irq_indicates_success(uint16_t irq)
{
	const uint16_t success_irqs = (uint16_t)((1u << 1) | (1u << 8));

	return (irq & success_irqs) != 0u;
}

uint16_t tlsr8258_tx_irq_session_mask(uint16_t base_mask, bool expect_post_tx_followup)
{
	if (!expect_post_tx_followup) {
		return base_mask;
	}

	return (uint16_t)(base_mask & ~BIT(8));
}

uint16_t tlsr8258_tx_irq_start_clear_mask(bool expect_post_tx_followup)
{
	/*
	 * Vendor 8258 poll TX clears only the plain TX/RX done latches before
	 * manual TX_START(), not the whole RF IRQ bank.
	 */
	if (expect_post_tx_followup) {
		return (uint16_t)(BIT(0) | BIT(1));
	}

	return 0xffffu;
}

bool tlsr8258_tx_force_manual_off_before_start(bool expect_post_tx_followup)
{
	(void)expect_post_tx_followup;

	/*
	 * DISABLED. This used to write reg 0x0f00 = 0x80 before an association
	 * poll TX — but 0x0f00 = 0x80 is the RF-OFF state-machine command (the
	 * same write tlsr8258_rf_off() uses to power the radio down). Nothing on
	 * the post-poll RX re-entry issues a state-machine "go" to undo it
	 * (tlsr8258_rf_set_rxmode() only programs the mode registers 0x0f02 /
	 * 0x0428 / ll_mode, never 0x0f00), so the radio was left OFF for the
	 * whole post-poll window and never heard the coordinator's indirect
	 * ASSOCIATION-RESPONSE — the radio received frames everywhere EXCEPT
	 * that window, is_rf_receiving_pkt read 0, and 8/11 windows expired with
	 * no RX IRQ of any class. At init 0x0f00 sits at its running power-on
	 * default, which is why the normal beacon-RX path is unaffected. Let the
	 * poll TX use the same (proven-working) manual TX path as every other
	 * frame instead of force-stopping the state machine.
	 */
	return false;
}

bool tlsr8258_tx_poll_needs_rx_rearm(bool expect_post_tx_followup, bool completed_in_isr)
{
	/*
	 * A poll that expects an indirect follow-up (the association DataReq)
	 * is ACK-requested, so on this silicon the HW auto-receives the
	 * coordinator's ACK into the active RX DMA buffer and reports the TX as
	 * done via RF_IRQ_TX_DS. That leaves the RX buffer OCCUPIED
	 * (rx_active[0] != 0).
	 *
	 * When the completion is handled inside the RF ISR, the ISR's
	 * double-buffer swap re-arms a fresh RX buffer. But under the Zigbee
	 * async RX sink TX_DS is masked for the poll session
	 * (tlsr8258_tx_irq_session_mask), so no RF ISR fires: tx() instead
	 * finishes via the status-register poll fallback, entirely bypassing
	 * that swap. The DMA is then still pointed at the occupied buffer and
	 * the indirect ASSOCIATION-RESPONSE arriving ~260us later cannot DMA in
	 * — it produces no RX IRQ of any class and the join stalls.
	 *
	 * Re-arm the RX buffer on exactly those ISR-less poll completions.
	 */
	return expect_post_tx_followup && !completed_in_isr;
}
