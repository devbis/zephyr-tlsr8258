/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Queue a callback for the EZSP host from the stack, outside of a command. Returns false when the
 * interface is not attached or its queue is full. Defined by the UART transport.
 */
bool zb_ezsp_callback(uint16_t id, const uint8_t *params, size_t len);
