/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
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
#if defined(CONFIG_SOC_FAMILY_TELINK_TLSR)
#define APP_PROFILE_MFR_NAME        "Telink"
#elif defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
#define APP_PROFILE_MFR_NAME        "Espressif"
#elif defined(CONFIG_SOC_FAMILY_NORDIC_NRF)
#define APP_PROFILE_MFR_NAME        "Nordic Semiconductors"
#else
#define APP_PROFILE_MFR_NAME        "Unknown"
#endif

#if defined(CONFIG_SOC)
#define APP_PROFILE_SOC_NAME        CONFIG_SOC
#else
#define APP_PROFILE_SOC_NAME        "unknown"
#endif

#if defined(CONFIG_ZIGBEE_ROUTER) || defined(CONFIG_ZIGBEE_COORDINATOR)
#define APP_PROFILE_ZIGBEE_ROLE     "router"
#else
#define APP_PROFILE_ZIGBEE_ROLE     "ed"
#endif

#define APP_PROFILE_MODEL_ID        APP_PROFILE_SOC_NAME "-zigbee-" APP_PROFILE_ZIGBEE_ROLE

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
