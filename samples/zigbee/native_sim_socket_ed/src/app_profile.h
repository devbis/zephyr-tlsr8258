/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#define APP_PROFILE_HA_PROFILE_ID 0x0104U
#define APP_PROFILE_HA_DEVICE_ID  0x0000U
#define APP_PROFILE_ENDPOINT      0x01U

#define APP_PROFILE_MFR_NAME      "Telink"
#define APP_PROFILE_MODEL_ID      "native-sim-ed"

void app_profile_register(void);
const void *app_profile_get_simple_desc(void);
const char *app_profile_mfr_name(void);
const char *app_profile_model_id(void);
