/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/zigbee/native_sim_socket_medium_model.h>

#include <limits.h>
#include <string.h>

#define ZB_NATIVE_SIM_SOCKET_PHY_BYTE_US 32U
#define ZB_NATIVE_SIM_SOCKET_PHY_SHR_US  192U

static bool zb_native_sim_socket_medium_model_valid_channel(uint8_t channel)
{
	return channel >= ZB_NATIVE_SIM_SOCKET_MEDIUM_CHANNEL_MIN &&
	       channel <= ZB_NATIVE_SIM_SOCKET_MEDIUM_CHANNEL_MAX;
}

void zb_native_sim_socket_medium_model_init(struct zb_native_sim_socket_medium_model *model)
{
	if (model == NULL) {
		return;
	}

	memset(model, 0, sizeof(*model));
}

uint32_t zb_native_sim_socket_medium_airtime_us(size_t psdu_len)
{
	if (psdu_len > ((UINT32_MAX - ZB_NATIVE_SIM_SOCKET_PHY_SHR_US) /
			ZB_NATIVE_SIM_SOCKET_PHY_BYTE_US)) {
		return UINT32_MAX;
	}

	return ZB_NATIVE_SIM_SOCKET_PHY_SHR_US +
	       ((uint32_t)psdu_len * ZB_NATIVE_SIM_SOCKET_PHY_BYTE_US);
}

bool zb_native_sim_socket_medium_model_channel_busy(
	const struct zb_native_sim_socket_medium_model *model,
	uint8_t channel, uint64_t now_us)
{
	if (model == NULL || !zb_native_sim_socket_medium_model_valid_channel(channel)) {
		return false;
	}

	return now_us < model->channel_busy_until_us[channel];
}

enum zb_native_sim_socket_medium_window_result zb_native_sim_socket_medium_model_reserve_window(
	struct zb_native_sim_socket_medium_model *model,
	uint8_t channel, uint64_t start_us, uint64_t end_us,
	uint64_t *busy_until_us)
{
	uint64_t *channel_busy_until;

	if (model == NULL || !zb_native_sim_socket_medium_model_valid_channel(channel) ||
	    end_us <= start_us) {
		return ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_INVALID;
	}

	channel_busy_until = &model->channel_busy_until_us[channel];
	if (model->channel_busy_from_us[channel] == start_us && *channel_busy_until == end_us) {
		if (busy_until_us != NULL) {
			*busy_until_us = *channel_busy_until;
		}
		return ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_OK;
	}

	if (*channel_busy_until > start_us) {
		if (end_us > *channel_busy_until) {
			if (model->channel_busy_from_us[channel] > start_us) {
				model->channel_busy_from_us[channel] = start_us;
			}
			*channel_busy_until = end_us;
		}
		if (busy_until_us != NULL) {
			*busy_until_us = *channel_busy_until;
		}
		return ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_COLLISION;
	}

	model->channel_busy_from_us[channel] = start_us;
	*channel_busy_until = end_us;
	if (busy_until_us != NULL) {
		*busy_until_us = end_us;
	}

	return ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_OK;
}
