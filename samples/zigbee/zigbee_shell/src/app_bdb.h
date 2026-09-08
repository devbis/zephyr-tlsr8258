/* SPDX-License-Identifier: Apache-2.0 */
/*
 * app_bdb.h — Commissioning / rejoin / poll-rate / leave policy for the
 * zigbee_shell sample.
 *
 * The functions declared here are the implementations of the
 * zb_platform_app_* bootstrap hooks.  main.c delegates to them so it
 * stays thin.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/zigbee/zb_bootstrap.h>

void app_bdb_bootstrap_ready(void);
bool app_bdb_should_start_commissioning(void);
void app_bdb_start_commissioning(void);
void app_bdb_network_left(void);
void app_bdb_commissioning_status(uint8_t status, bool joinedNetwork);
bool app_bdb_get_join_profile(struct zb_platform_bdb_join_profile *profile);
bool app_bdb_get_fixed_join_target(struct zb_platform_bdb_fixed_target *target);
