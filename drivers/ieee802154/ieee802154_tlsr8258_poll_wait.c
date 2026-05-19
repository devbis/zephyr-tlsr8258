/* SPDX-License-Identifier: Apache-2.0 */

#include "ieee802154_tlsr8258_poll_wait.h"

#define TLSR8258_POST_POLL_WAIT_DEFAULT_SYMBOLS 1220u
#define TLSR8258_POST_POLL_WAIT_MIN_US          6000u

uint32_t tlsr8258_post_poll_wait_us(uint16_t frame_total_wait_symbols)
{
	uint32_t wait_symbols = frame_total_wait_symbols;
	uint32_t wait_us;

	if (wait_symbols == 0u) {
		wait_symbols = TLSR8258_POST_POLL_WAIT_DEFAULT_SYMBOLS;
	}

	wait_us = wait_symbols * 16u;
	return (wait_us < TLSR8258_POST_POLL_WAIT_MIN_US) ?
		       TLSR8258_POST_POLL_WAIT_MIN_US :
		       wait_us;
}

bool tlsr8258_post_poll_should_stop(bool ack_seen, bool ack_pending, bool rx_is_pending_response)
{
	return ack_seen && ack_pending && rx_is_pending_response;
}
