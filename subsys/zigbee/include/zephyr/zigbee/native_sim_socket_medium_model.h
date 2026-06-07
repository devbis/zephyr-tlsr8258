/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_NATIVE_SIM_SOCKET_MEDIUM_MODEL_H_
#define ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_NATIVE_SIM_SOCKET_MEDIUM_MODEL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZB_NATIVE_SIM_SOCKET_MEDIUM_CHANNEL_MIN 11U
#define ZB_NATIVE_SIM_SOCKET_MEDIUM_CHANNEL_MAX 26U

enum zb_native_sim_socket_medium_window_result {
	ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_OK = 0,
	ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_COLLISION = 1,
	ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_INVALID = 2,
};

struct zb_native_sim_socket_medium_model {
	uint64_t channel_busy_from_us[ZB_NATIVE_SIM_SOCKET_MEDIUM_CHANNEL_MAX + 1U];
	uint64_t channel_busy_until_us[ZB_NATIVE_SIM_SOCKET_MEDIUM_CHANNEL_MAX + 1U];
};

void zb_native_sim_socket_medium_model_init(struct zb_native_sim_socket_medium_model *model);
uint32_t zb_native_sim_socket_medium_airtime_us(size_t psdu_len);
bool zb_native_sim_socket_medium_model_channel_busy(
	const struct zb_native_sim_socket_medium_model *model,
	uint8_t channel, uint64_t now_us);
enum zb_native_sim_socket_medium_window_result zb_native_sim_socket_medium_model_reserve_window(
	struct zb_native_sim_socket_medium_model *model,
	uint8_t channel, uint64_t start_us, uint64_t end_us,
	uint64_t *busy_until_us);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_NATIVE_SIM_SOCKET_MEDIUM_MODEL_H_ */
