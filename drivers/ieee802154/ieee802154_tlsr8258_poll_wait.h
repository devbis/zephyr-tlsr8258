/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_DRIVERS_IEEE802154_TLSR8258_POLL_WAIT_H_
#define ZEPHYR_DRIVERS_IEEE802154_TLSR8258_POLL_WAIT_H_

#include <stdbool.h>
#include <stdint.h>

uint32_t tlsr8258_post_poll_wait_us(uint16_t frame_total_wait_symbols);
bool tlsr8258_post_poll_should_stop(bool ack_seen, bool ack_pending, bool rx_is_pending_response);

#endif /* ZEPHYR_DRIVERS_IEEE802154_TLSR8258_POLL_WAIT_H_ */
