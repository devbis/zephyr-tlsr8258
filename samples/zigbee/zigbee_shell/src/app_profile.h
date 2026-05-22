/* SPDX-License-Identifier: Apache-2.0 */
/*
 * app_profile.h — Authoritative endpoint / profile / cluster definitions
 * for the zigbee_shell sample.
 *
 * External device contract (locked):
 *   HA profile 0x0104 · device ID 0x0000 · endpoint 1
 *   In-clusters: Basic (0x0000), Identify (0x0003)
 */
#pragma once

/* Profile identity */
#define APP_PROFILE_HA_PROFILE_ID   0x0104U
#define APP_PROFILE_HA_DEVICE_ID    0x0000U
#define APP_PROFILE_ENDPOINT        0x01U

/* Authoritative identity strings (single source of truth) */
#define APP_PROFILE_MFR_NAME        "Telink"
#define APP_PROFILE_MODEL_ID        "tlsr8258-zigbee-shell"

/*
 * Initialize the ZCL layer and register the application endpoint.
 * Calls zcl_init(), af_endpointRegister(..., zcl_rx_handler, NULL),
 * and zcl_register() for the Basic and Identify clusters.
 *
 * Must be called before zb_platform_bdb_init_default().
 */
void app_profile_register(void);

/* Return a pointer to the static simple descriptor. */
const void *app_profile_get_simple_desc(void);

/* Single source of truth for interview-path identity strings. */
const char *app_profile_mfr_name(void);
const char *app_profile_model_id(void);
