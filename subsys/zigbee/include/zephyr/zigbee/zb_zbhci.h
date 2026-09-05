/* SPDX-License-Identifier: Apache-2.0 */
/*
 * ZBHCI (Zigbee Host Command Interface) UART transport and narrow command
 * subset for the coordinator role.
 *
 * Framing and command-ID layout adapted from Telink's tl_zigbee_sdk
 * (zbhci/zbhci.h, zbhci/zbhci.c, zbhci/uart/hci_uart.c), Copyright (c) 2021
 * Telink Semiconductor (Shanghai) Co., Ltd., licensed Apache-2.0. Ported to
 * Zephyr's interrupt-driven UART API instead of the vendor's drv_uart_*
 * primitives, and re-targeted at this port's ZDO/AF/BDB APIs instead of the
 * vendor SDK's. Only a narrow subset of the vendor command set is
 * implemented; see zb_zbhci_cmd.c.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

/** Frame delimiters (0x55 <type:u16 BE> <len:u16 BE> <crc8> <payload> 0xAA). */
#define ZBHCI_MSG_START_FLAG 0x55U
#define ZBHCI_MSG_END_FLAG   0xAAU

/**
 * Command IDs implemented by this port. Values match the vendor protocol so
 * a host tool speaking real ZBHCI can drive the commands this port
 * implements unmodified; unimplemented vendor command IDs are not defined
 * here and are acknowledged with ZBHCI_MSG_STATUS_UNHANDLED_COMMAND.
 */
enum zbhci_cmd_id {
	ZBHCI_CMD_BDB_COMMISSION_FORMATION  = 0x0001,
	ZBHCI_CMD_ACKNOWLEDGE                = 0x8000,

	ZBHCI_CMD_DISCOVERY_ACTIVE_EP_REQ   = 0x0015,
	ZBHCI_CMD_DISCOVERY_ACTIVE_EP_RSP   = 0x8015,
	ZBHCI_CMD_DISCOVERY_SIMPLE_DESC_REQ = 0x0013,
	ZBHCI_CMD_DISCOVERY_SIMPLE_DESC_RSP = 0x8013,

	ZBHCI_CMD_MGMT_PERMIT_JOIN_REQ      = 0x0034,
	ZBHCI_CMD_MGMT_PERMIT_JOIN_RSP      = 0x8034,

	ZBHCI_CMD_GET_CHILD_NODES_REQ       = 0x0046,
	ZBHCI_CMD_GET_CHILD_NODES_RSP       = 0x8046,
	ZBHCI_CMD_NODES_DEV_ANNCE_IND       = 0x8043,

	ZBHCI_CMD_AF_DATA_SEND_TEST_REQ     = 0x0044,
	ZBHCI_CMD_AF_DATA_SEND_TEST_RSP     = 0x8044,
};

/** zbhci_msgStatus_e (vendor zbhci.h), used in ZBHCI_CMD_ACKNOWLEDGE replies. */
enum zbhci_msg_status {
	ZBHCI_MSG_STATUS_SUCCESS = 0,
	ZBHCI_MSG_STATUS_INCORRECT_PARAMETERS = 1,
	ZBHCI_MSG_STATUS_UNHANDLED_COMMAND = 2,
	ZBHCI_MSG_STATUS_BUSY = 3,
	ZBHCI_MSG_STATUS_NO_MEMORY = 4,
};

/**
 * Attach the ZBHCI transport to a UART device and enable RX interrupts.
 *
 * @retval 0 success.
 * @retval -ENODEV @p uart_dev is NULL or not ready.
 */
int zb_zbhci_uart_init(const struct device *uart_dev);

/**
 * Drain and process any bytes waiting on the ZBHCI UART. Non-blocking; must
 * be called periodically from the Zigbee thread's own loop (matching
 * zb_platform_radio_rx_poll()'s pattern), never from an ISR -- Zephyr's
 * interrupt-driven UART API is not used here because the Zigbee thread never
 * yields, which would starve native_sim's interrupt-emulation thread.
 */
void zb_zbhci_uart_poll(void);

/**
 * Frame and transmit one ZBHCI message. Blocking (uart_poll_out); only
 * called from the Zigbee thread, never from the UART RX ISR.
 */
void zb_zbhci_send(uint16_t msg_type, uint16_t len, const uint8_t *payload);

/**
 * Dispatch one fully-framed inbound command. Implemented in zb_zbhci_cmd.c.
 * Called on the Zigbee thread (posted there via tl_zbTaskPost by the UART
 * transport), never directly from the UART RX ISR.
 */
void zb_zbhci_cmd_handle(uint16_t msg_type, uint16_t len, const uint8_t *payload);
