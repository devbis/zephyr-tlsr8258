/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * IEEE 802.15.4 driver for the STM32WB5x. CPU2 runs ST's OpenThread RCP firmware
 * (stm32wb5x_Thread_RCP_fw.bin). The two cores exchange HDLC framed Spinel through the IPCC:
 *
 *   CPU1 -> CPU2: a whole HDLC frame in the CLI channel buffer (TL_CLI_SendCmd)
 *   CPU2 -> CPU1: TL_THREAD_NotReceived, packet = [4 B id][u32 length][HDLC data]
 *
 * A frame passed to the RCP carries two bytes of room for the FCS, a received frame ends with
 * the two FCS bytes; the driver adds and removes them.
 */

#define DT_DRV_COMPAT st_stm32wb_ieee802154

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <stm32_ll_ipcc.h>
#include <stm32_ll_pwr.h>

#include "app_conf.h"
#include "hw.h"
#include "shci.h"
#include "shci_tl.h"
#include "stm32_wpan_common.h"
#include "tl.h"

LOG_MODULE_REGISTER(ieee802154_stm32wb, CONFIG_IEEE802154_DRIVER_LOG_LEVEL);

/* Spinel (OpenThread) constants */
#define SPINEL_HEADER_FLAG		0x80
#define SPINEL_CMD_PROP_VALUE_GET	2
#define SPINEL_CMD_PROP_VALUE_SET	3
#define SPINEL_CMD_PROP_VALUE_IS	6

#define SPINEL_PROP_LAST_STATUS		0x00
#define SPINEL_PROP_HWADDR		0x08
#define SPINEL_PROP_PHY_ENABLED		0x20
#define SPINEL_PROP_PHY_CHAN		0x21
#define SPINEL_PROP_PHY_CCA_THRESHOLD	0x24
#define SPINEL_PROP_PHY_TX_POWER	0x25
#define SPINEL_PROP_PHY_RSSI		0x26
#define SPINEL_PROP_MAC_15_4_LADDR	0x34
#define SPINEL_PROP_MAC_15_4_SADDR	0x35
#define SPINEL_PROP_MAC_15_4_PANID	0x36
#define SPINEL_PROP_MAC_RAW_STREAM	0x37
#define SPINEL_PROP_MAC_PROMISCUOUS	0x38
#define SPINEL_PROP_STREAM_RAW		0x71
#define SPINEL_PROP_MAC_SRC_MATCH	0x1303

#define SPINEL_STATUS_OK		0
#define SPINEL_STATUS_NO_ACK		17
#define SPINEL_STATUS_CCA_FAILURE	18

#define SPINEL_PROMISC_FULL		2

/* HDLC */
#define HDLC_FLAG			0x7E
#define HDLC_ESC			0x7D
#define HDLC_FCS_INIT			0xFFFF

#define CLI_PAYLOAD_MAX			255U
#define RCP_CHUNK_MAX			247U
#define SPINEL_FRAME_MAX		260U
#define PSDU_MAX			127U
#define FCS_LEN				2U

#define CLI_CMD_CHANNEL			HW_IPCC_THREAD_CLI_CMD_CHANNEL

#define IPCC_RX_IRQ	DT_INST_IRQ_BY_NAME(0, rx, irq)
#define IPCC_RX_PRIO	DT_INST_IRQ_BY_NAME(0, rx, priority)
#define IPCC_TX_IRQ	DT_INST_IRQ_BY_NAME(0, tx, irq)
#define IPCC_TX_PRIO	DT_INST_IRQ_BY_NAME(0, tx, priority)

BUILD_ASSERT(IPCC_RX_IRQ == IPCC_C1_RX_IRQn, "Unexpected IRQ number for IPCC Rx");
BUILD_ASSERT(IPCC_TX_IRQ == IPCC_C1_TX_IRQn, "Unexpected IRQ number for IPCC Tx");

#define POOL_SIZE (CFG_TLBLE_EVT_QUEUE_LENGTH * 4 * \
		   DIVC((sizeof(TL_PacketHeader_t) + TL_BLE_EVENT_FRAME_SIZE), 4))

IEEE802154_DEFINE_PHY_SUPPORTED_CHANNELS(drv_attr, 11, 26);

/* Buffers shared with CPU2: they have to be in the IPCC memory sections */
PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static TL_TH_Config_t thread_config;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t evt_pool[POOL_SIZE];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t system_cmd_buf;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t
	system_spare_evt_buf[sizeof(TL_PacketHeader_t) + TL_EVT_HDR_SIZE + 255];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t thread_ot_cmd_buf;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t
	thread_not_ack_buf[sizeof(TL_PacketHeader_t) + TL_EVT_HDR_SIZE + 255];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t thread_cli_cmd_buf;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t thread_cli_not_buf;

static const struct stm32_pclken clk_cfg[] = STM32_DT_INST_CLOCKS(0);

struct wb_data {
	struct net_if *iface;
	uint8_t mac[8];

	TL_EvtPacket_t *volatile rx_pending;
	struct k_work_q rx_wq;
	struct k_work rx_work;

	/* HDLC decoder, only touched from the RX work item */
	uint8_t hdlc_buf[SPINEL_FRAME_MAX + 2];
	uint16_t hdlc_len;
	bool hdlc_in;
	bool hdlc_esc;

	/* One Spinel request at a time */
	struct k_mutex req_lock;
	struct k_sem rsp_sem;
	uint8_t req_tid;
	uint8_t rsp[SPINEL_FRAME_MAX];
	uint16_t rsp_len;

	struct k_sem c2_ready;
	struct k_sem shci_rsp;

	uint8_t tid_counter;
	uint8_t channel;
	int8_t cca_threshold;
	bool started;

	/* The RCP refuses addresses while the PHY is disabled: keep them and apply on start */
	uint8_t ext_addr[8];	/* Spinel byte order */
	uint16_t short_addr;
	uint16_t pan_id;
	bool have_ext_addr;
	bool have_short_addr;
	bool have_pan_id;
	uint8_t txpower_set;
	bool have_txpower;
	uint8_t promisc;
	ieee802154_event_cb_t event_cb;
};

static struct wb_data wb;
static K_KERNEL_STACK_DEFINE(wb_rx_stack, CONFIG_IEEE802154_STM32WB_RX_STACK_SIZE);

/* ---- HDLC ---- */

static uint16_t fcs16_update(uint16_t crc, uint8_t b)
{
	crc ^= b;
	for (int i = 0; i < 8; i++) {
		crc = (crc & 1U) != 0U ? (crc >> 1) ^ 0x8408U : crc >> 1;
	}
	return crc;
}

static bool hdlc_special(uint8_t b)
{
	return b == HDLC_FLAG || b == HDLC_ESC || b == 0x11 || b == 0x13 || b == 0xF8;
}

/* Encode in[0..n) with FCS and flags into out; returns the length or 0 when it does not fit */
static size_t hdlc_encode(const uint8_t *in, size_t n, uint8_t *out, size_t max)
{
	uint16_t crc = HDLC_FCS_INIT;
	size_t o = 0;
	uint8_t fcs[2];

	for (size_t i = 0; i < n; i++) {
		crc = fcs16_update(crc, in[i]);
	}
	crc ^= 0xFFFFU;
	fcs[0] = crc & 0xFF;
	fcs[1] = crc >> 8;

	if (max < 2U) {
		return 0;
	}
	out[o++] = HDLC_FLAG;
	for (size_t i = 0; i < n + 2U; i++) {
		uint8_t b = i < n ? in[i] : fcs[i - n];

		if (hdlc_special(b)) {
			if (o + 2U > max) {
				return 0;
			}
			out[o++] = HDLC_ESC;
			out[o++] = b ^ 0x20;
		} else {
			if (o + 1U > max) {
				return 0;
			}
			out[o++] = b;
		}
	}
	if (o + 1U > max) {
		return 0;
	}
	out[o++] = HDLC_FLAG;
	return o;
}

static void spinel_handle_frame(const uint8_t *f, size_t n);

static void hdlc_feed(const uint8_t *data, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		uint8_t b = data[i];

		if (b == HDLC_FLAG) {
			if (wb.hdlc_in && wb.hdlc_len >= 5U) {
				uint16_t crc = HDLC_FCS_INIT;
				size_t len = wb.hdlc_len;

				for (size_t k = 0; k < len - 2U; k++) {
					crc = fcs16_update(crc, wb.hdlc_buf[k]);
				}
				crc ^= 0xFFFFU;
				if (crc == sys_get_le16(&wb.hdlc_buf[len - 2U])) {
					spinel_handle_frame(wb.hdlc_buf, len - 2U);
				} else {
					LOG_WRN("bad HDLC FCS, frame of %zu bytes dropped", len);
				}
			}
			wb.hdlc_len = 0;
			wb.hdlc_in = true;
			wb.hdlc_esc = false;
		} else if (wb.hdlc_in) {
			if (b == HDLC_ESC) {
				wb.hdlc_esc = true;
				continue;
			}
			if (wb.hdlc_esc) {
				b ^= 0x20;
				wb.hdlc_esc = false;
			}
			if (wb.hdlc_len < sizeof(wb.hdlc_buf)) {
				wb.hdlc_buf[wb.hdlc_len++] = b;
			} else {
				wb.hdlc_in = false;
			}
		}
	}
}

/* ---- Spinel ---- */

static size_t spinel_put_uint(uint8_t *out, uint32_t v)
{
	size_t n = 0;

	do {
		uint8_t b = v & 0x7F;

		v >>= 7;
		out[n++] = b | (v != 0U ? 0x80 : 0);
	} while (v != 0U);
	return n;
}

static size_t spinel_get_uint(const uint8_t *in, size_t avail, uint32_t *v)
{
	uint32_t r = 0;
	size_t n = 0;
	unsigned int shift = 0;

	while (n < avail && shift < 32U) {
		uint8_t b = in[n++];

		r |= (uint32_t)(b & 0x7F) << shift;
		shift += 7;
		if ((b & 0x80) == 0U) {
			*v = r;
			return n;
		}
	}
	return 0;
}

static uint8_t spinel_next_tid(void)
{
	wb.tid_counter = (wb.tid_counter % 14U) + 1U;
	return wb.tid_counter;
}

static void wb_deliver_rx(const uint8_t *psdu, size_t len, int8_t rssi, uint8_t lqi)
{
	struct net_pkt *pkt;
	size_t pkt_len = len;

	if (wb.iface == NULL || !wb.started) {
		return;
	}
	if (len < FCS_LEN + 3U) {
		return;
	}
	if (!IS_ENABLED(CONFIG_IEEE802154_L2_PKT_INCL_FCS)) {
		pkt_len -= FCS_LEN;
	}

	pkt = net_pkt_rx_alloc_with_buffer(wb.iface, pkt_len, NET_AF_UNSPEC, 0, K_NO_WAIT);
	if (pkt == NULL) {
		LOG_WRN("no packet for a received frame of %zu bytes", pkt_len);
		return;
	}
	if (net_pkt_write(pkt, psdu, pkt_len) < 0) {
		net_pkt_unref(pkt);
		return;
	}
	net_pkt_set_ieee802154_rssi_dbm(pkt, rssi);
	net_pkt_set_ieee802154_lqi(pkt, lqi);
	if (net_recv_data(wb.iface, pkt) < 0) {
		LOG_WRN("the stack dropped a received frame");
		net_pkt_unref(pkt);
	}
}

/* d: frame, c: rssi, c: noise floor, S: flags, d: phy data (C channel, C lqi, ...) */
static void wb_handle_stream_raw(const uint8_t *p, size_t n)
{
	uint16_t flen;
	uint16_t flags;
	int8_t rssi;
	uint8_t lqi = 0;
	const uint8_t *frame;

	if (n < 2U) {
		return;
	}
	flen = sys_get_le16(p);
	if (flen < FCS_LEN || (size_t)flen + 2U + 2U + 2U > n || flen > PSDU_MAX + FCS_LEN) {
		return;
	}
	frame = p + 2;
	rssi = (int8_t)p[2U + flen];
	flags = sys_get_le16(&p[2U + flen + 2U]);
	if ((flags & 0x0004U) != 0U) { /* bad FCS */
		return;
	}
	if (n >= 2U + flen + 4U + 2U) {
		uint16_t phy_len = sys_get_le16(&p[2U + flen + 4U]);

		if (phy_len >= 2U && n >= 2U + flen + 6U + 2U) {
			lqi = p[2U + flen + 6U + 1U];
		}
	}
	wb_deliver_rx(frame, flen, rssi, lqi);
}

static void spinel_handle_frame(const uint8_t *f, size_t n)
{
	uint32_t prop;
	size_t used;
	uint8_t tid;

	if (n < 3U || (f[0] & 0xC0) != SPINEL_HEADER_FLAG) {
		return;
	}
	tid = f[0] & 0x0F;
	if (f[1] != SPINEL_CMD_PROP_VALUE_IS) {
		return;
	}
	used = spinel_get_uint(&f[2], n - 2U, &prop);
	if (used == 0U) {
		return;
	}

	if (tid == 0U) {
		if (prop == SPINEL_PROP_STREAM_RAW) {
			wb_handle_stream_raw(&f[2U + used], n - 2U - used);
		} else if (prop == SPINEL_PROP_LAST_STATUS) {
			LOG_DBG("RCP notification, status 0x%02x",
				n > 2U + used ? f[2U + used] : 0);
		}
		return;
	}

	if (tid == wb.req_tid && wb.rsp_len == 0U) {
		/* keep the property id in front of the value */
		wb.rsp_len = MIN(n - 2U, sizeof(wb.rsp));
		memcpy(wb.rsp, &f[2], wb.rsp_len);
		k_sem_give(&wb.rsp_sem);
	}
}

/* Send one request (cmd, prop, value) and wait for the answer with the same tid. */
static int spinel_request(uint8_t cmd, uint32_t prop, const uint8_t *val, size_t vlen,
			  uint8_t *rsp, size_t *rsp_len, k_timeout_t timeout)
{
	uint8_t frame[SPINEL_FRAME_MAX];
	uint8_t enc[CLI_PAYLOAD_MAX];
	size_t n = 0;
	size_t enc_len;
	int ret;

	if (vlen + 8U > sizeof(frame)) {
		return -EMSGSIZE;
	}

	k_mutex_lock(&wb.req_lock, K_FOREVER);

	wb.req_tid = spinel_next_tid();
	wb.rsp_len = 0;
	k_sem_reset(&wb.rsp_sem);

	frame[n++] = SPINEL_HEADER_FLAG | wb.req_tid;
	frame[n++] = cmd;
	n += spinel_put_uint(&frame[n], prop);
	if (vlen > 0U) {
		memcpy(&frame[n], val, vlen);
		n += vlen;
	}

	enc_len = hdlc_encode(frame, n, enc, sizeof(enc));
	if (enc_len == 0U) {
		ret = -EMSGSIZE;
		goto out;
	}

	/* The previous command must have been taken by CPU2 before the buffer is reused */
	for (int i = 0; i < 100 && LL_C1_IPCC_IsActiveFlag_CHx(IPCC, CLI_CMD_CHANNEL); i++) {
		k_busy_wait(100);
	}
	if (LL_C1_IPCC_IsActiveFlag_CHx(IPCC, CLI_CMD_CHANNEL)) {
		ret = -EBUSY;
		goto out;
	}

	memset(thread_cli_cmd_buf.cmdserial.cmd.payload, 0, CLI_PAYLOAD_MAX);
	memcpy(thread_cli_cmd_buf.cmdserial.cmd.payload, enc, enc_len);
	thread_cli_cmd_buf.cmdserial.cmd.plen = enc_len;
	thread_cli_cmd_buf.cmdserial.cmd.cmdcode = 0;
	TL_CLI_SendCmd();

	ret = k_sem_take(&wb.rsp_sem, timeout);
	if (ret != 0) {
		LOG_ERR("Spinel request prop 0x%x timed out", prop);
		ret = -ETIMEDOUT;
		goto out;
	}
	if (rsp != NULL && rsp_len != NULL) {
		*rsp_len = MIN(*rsp_len, (size_t)wb.rsp_len);
		memcpy(rsp, wb.rsp, *rsp_len);
	}
	ret = 0;
out:
	wb.req_tid = 0;
	k_mutex_unlock(&wb.req_lock);
	return ret;
}

/* The answer to a SET is VALUE_IS of the same property (or LAST_STATUS on failure) */
static int spinel_set(uint32_t prop, const uint8_t *val, size_t vlen)
{
	uint8_t rsp[16];
	size_t rlen = sizeof(rsp);
	uint32_t rprop;
	size_t used;
	int ret;

	ret = spinel_request(SPINEL_CMD_PROP_VALUE_SET, prop, val, vlen, rsp, &rlen,
			     K_MSEC(CONFIG_IEEE802154_STM32WB_REQUEST_TIMEOUT_MS));
	if (ret != 0) {
		return ret;
	}
	used = spinel_get_uint(rsp, rlen, &rprop);
	if (used == 0U) {
		return -EIO;
	}
	if (rprop == SPINEL_PROP_LAST_STATUS && rlen > used && rsp[used] != SPINEL_STATUS_OK) {
		LOG_ERR("setting property 0x%x failed, status %u", prop, rsp[used]);
		return -EIO;
	}
	return 0;
}

static int spinel_set_u8(uint32_t prop, uint8_t v)
{
	return spinel_set(prop, &v, 1);
}

static int spinel_get(uint32_t prop, uint8_t *out, size_t *len)
{
	uint8_t rsp[32];
	size_t rlen = sizeof(rsp);
	uint32_t rprop;
	size_t used;
	int ret;

	ret = spinel_request(SPINEL_CMD_PROP_VALUE_GET, prop, NULL, 0, rsp, &rlen,
			     K_MSEC(CONFIG_IEEE802154_STM32WB_REQUEST_TIMEOUT_MS));
	if (ret != 0) {
		return ret;
	}
	used = spinel_get_uint(rsp, rlen, &rprop);
	if (used == 0U || rprop != prop) {
		return -EIO;
	}
	*len = MIN(*len, rlen - used);
	memcpy(out, &rsp[used], *len);
	return 0;
}

/* ---- IPCC / CPU2 start ---- */

void shci_notify_asynch_evt(void *pdata)
{
	ARG_UNUSED(pdata);
	shci_user_evt_proc();
}

void shci_cmd_resp_release(uint32_t flag)
{
	ARG_UNUSED(flag);
	k_sem_give(&wb.shci_rsp);
}

void shci_cmd_resp_wait(uint32_t timeout)
{
	k_sem_take(&wb.shci_rsp, K_MSEC(timeout));
}

static void sys_evt_received(void *pdata)
{
	tSHCI_UserEvtRxParam *param = pdata;
	TL_AsynchEvt_t *evt = (TL_AsynchEvt_t *)param->pckt->evtserial.evt.payload;

	if (evt->subevtcode == SHCI_SUB_EVT_CODE_READY) {
		k_sem_give(&wb.c2_ready);
	}
}

static void sys_cmd_status(SHCI_TL_CmdStatus_t status)
{
	ARG_UNUSED(status);
}

/* The RCP blocks on its trace channel unless the buffers are given back */
void TL_TRACES_EvtReceived(TL_EvtPacket_t *hcievt)
{
	TL_MM_EvtDone(hcievt);
}

void TL_OT_CmdEvtReceived(TL_EvtPacket_t *buf)
{
	ARG_UNUSED(buf);
}

void TL_THREAD_CliNotReceived(TL_EvtPacket_t *buf)
{
	ARG_UNUSED(buf);
	TL_THREAD_CliSendAck();
}

/* Interrupt context: the packet is processed (and acknowledged) by the RX work item */
void TL_THREAD_NotReceived(TL_EvtPacket_t *buf)
{
	wb.rx_pending = buf;
	k_work_submit_to_queue(&wb.rx_wq, &wb.rx_work);
}

static void wb_rx_work_fn(struct k_work *work)
{
	const uint8_t *pl = wb.rx_pending->evtserial.evt.payload;
	uint32_t n;

	ARG_UNUSED(work);

	/* [4 bytes id][u32 length][data] */
	memcpy(&n, pl + 4, sizeof(n));
	if (n > RCP_CHUNK_MAX) {
		LOG_WRN("oversized RCP packet (%u bytes)", n);
		n = 0;
	}
	/* a single byte is a command to the application, not Spinel */
	if (n > 1U) {
		hdlc_feed(pl + 8, n);
	}
	TL_THREAD_SendAck();
}

static void wb_ipcc_rx_isr(const void *arg)
{
	ARG_UNUSED(arg);
	HW_IPCC_Rx_Handler();
}

static void wb_ipcc_tx_isr(const void *arg)
{
	ARG_UNUSED(arg);
	HW_IPCC_Tx_Handler();
}

static void wb_ipcc_reset(void)
{
	const uint32_t all = LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 |
			     LL_IPCC_CHANNEL_4 | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6;

	LL_C1_IPCC_ClearFlag_CHx(IPCC, all);
	LL_C2_IPCC_ClearFlag_CHx(IPCC, all);
	LL_C1_IPCC_DisableTransmitChannel(IPCC, all);
	LL_C2_IPCC_DisableTransmitChannel(IPCC, all);
	LL_C1_IPCC_DisableReceiveChannel(IPCC, all);
	LL_C2_IPCC_DisableReceiveChannel(IPCC, all);

	IRQ_CONNECT(IPCC_RX_IRQ, IPCC_RX_PRIO, wb_ipcc_rx_isr, NULL, 0);
	IRQ_CONNECT(IPCC_TX_IRQ, IPCC_TX_PRIO, wb_ipcc_tx_isr, NULL, 0);
}

static int wb_start_cpu2(void)
{
	const struct device *const clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	TL_MM_Config_t mm = {0};
	SHCI_TL_HciInitConf_t shci_cfg;
	WirelessFwInfo_t fw;
	int ret;

	ret = clock_control_configure(clk, (clock_control_subsys_t)&clk_cfg[1], NULL);
	if (ret < 0) {
		return ret;
	}
	ret = clock_control_on(clk, (clock_control_subsys_t)&clk_cfg[0]);
	if (ret < 0) {
		return ret;
	}

	wb_ipcc_reset();

	TL_Init();

	shci_cfg.p_cmdbuffer = (uint8_t *)&system_cmd_buf;
	shci_cfg.StatusNotCallBack = sys_cmd_status;
	shci_init(sys_evt_received, &shci_cfg);

	mm.p_BleSpareEvtBuffer = NULL;
	mm.p_SystemSpareEvtBuffer = system_spare_evt_buf;
	mm.p_AsynchEvtPool = evt_pool;
	mm.AsynchEvtPoolSize = POOL_SIZE;
	TL_MM_Init(&mm);

	TL_Enable();

	if (k_sem_take(&wb.c2_ready, K_SECONDS(3)) != 0) {
		LOG_ERR("CPU2 did not report ready");
		return -ETIMEDOUT;
	}
	if (SHCI_GetWirelessFwInfo(&fw) != SHCI_Success) {
		LOG_ERR("cannot read the CPU2 firmware info");
		return -EIO;
	}
	if (fw.StackType != INFO_STACK_TYPE_THREAD_FTD &&
	    fw.StackType != INFO_STACK_TYPE_THREAD_MTD) {
		LOG_ERR("CPU2 does not run the Thread RCP firmware (stack type 0x%02x)",
			fw.StackType);
		return -ENODEV;
	}
	LOG_INF("CPU2 firmware v%u.%u.%u, stack type 0x%02x", fw.VersionMajor, fw.VersionMinor,
		fw.VersionSub, fw.StackType);

	TL_TRACES_Init();

	thread_config.p_ThreadOtCmdRspBuffer = (uint8_t *)&thread_ot_cmd_buf;
	thread_config.p_ThreadNotAckBuffer = thread_not_ack_buf;
	thread_config.p_ThreadCliRspBuffer = (uint8_t *)&thread_cli_cmd_buf;
	thread_config.p_ThreadCliNotBuffer = (uint8_t *)&thread_cli_not_buf;
	TL_THREAD_Init(&thread_config);

	if (SHCI_C2_THREAD_Init() != SHCI_Success) {
		LOG_ERR("SHCI_C2_THREAD_Init failed");
		return -EIO;
	}

	/* Thread traces off, as in ST's Thread_RCP application */
	static SHCI_C2_DEBUG_TracesConfig_t traces_cfg;
	SHCI_C2_DEBUG_Init_Cmd_Packet_t dbg = {
		{{0, 0, 0}},
		{NULL, (uint8_t *)&traces_cfg, NULL, 0, 0, 0}
	};

	traces_cfg.thread_config = 0;
	(void)SHCI_C2_DEBUG_Init(&dbg);

	return 0;
}

/* ---- radio API ---- */

static enum ieee802154_hw_caps wb_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);

	return IEEE802154_HW_FCS | IEEE802154_HW_FILTER | IEEE802154_HW_PROMISC |
	       IEEE802154_HW_CSMA | IEEE802154_HW_TX_RX_ACK | IEEE802154_HW_RX_TX_ACK;
}

static int wb_cca(const struct device *dev)
{
	uint8_t v;
	size_t len = 1;
	int ret;

	ARG_UNUSED(dev);

	if (!wb.started) {
		return -ENETDOWN;
	}
	ret = spinel_get(SPINEL_PROP_PHY_RSSI, &v, &len);
	if (ret != 0 || len != 1U) {
		return -EIO;
	}
	return (int8_t)v >= wb.cca_threshold ? -EBUSY : 0;
}

static int wb_set_channel(const struct device *dev, uint16_t channel)
{
	int ret;

	ARG_UNUSED(dev);

	if (channel < drv_attr.phy_channel_range.from_channel) {
		return -ENOTSUP;
	}
	if (channel > drv_attr.phy_channel_range.to_channel) {
		return -EINVAL;
	}
	wb.channel = (uint8_t)channel;
	return wb.started ? spinel_set_u8(SPINEL_PROP_PHY_CHAN, (uint8_t)channel) : 0;
}

static int wb_apply_filter(void)
{
	uint8_t v[8];
	int ret = 0;

	if (wb.have_ext_addr) {
		ret = spinel_set(SPINEL_PROP_MAC_15_4_LADDR, wb.ext_addr, 8);
	}
	if (ret == 0 && wb.have_short_addr) {
		sys_put_le16(wb.short_addr, v);
		ret = spinel_set(SPINEL_PROP_MAC_15_4_SADDR, v, 2);
	}
	if (ret == 0 && wb.have_pan_id) {
		sys_put_le16(wb.pan_id, v);
		ret = spinel_set(SPINEL_PROP_MAC_15_4_PANID, v, 2);
	}
	return ret;
}

static int wb_filter(const struct device *dev, bool set, enum ieee802154_filter_type type,
		     const struct ieee802154_filter *filter)
{
	ARG_UNUSED(dev);

	if (!set) {
		return -ENOTSUP;
	}

	switch (type) {
	case IEEE802154_FILTER_TYPE_IEEE_ADDR:
		/* Zephyr: little endian; Spinel EUI-64: most significant byte first */
		for (int i = 0; i < 8; i++) {
			wb.ext_addr[i] = filter->ieee_addr[7 - i];
		}
		wb.have_ext_addr = true;
		break;
	case IEEE802154_FILTER_TYPE_SHORT_ADDR:
		wb.short_addr = filter->short_addr;
		wb.have_short_addr = true;
		break;
	case IEEE802154_FILTER_TYPE_PAN_ID:
		wb.pan_id = filter->pan_id;
		wb.have_pan_id = true;
		break;
	default:
		return -ENOTSUP;
	}

	return wb.started ? wb_apply_filter() : 0;
}

static int wb_set_txpower(const struct device *dev, int16_t dbm)
{
	ARG_UNUSED(dev);

	wb.txpower_set = (uint8_t)(int8_t)CLAMP(dbm, -40, 20);
	wb.have_txpower = true;
	return wb.started ? spinel_set_u8(SPINEL_PROP_PHY_TX_POWER, wb.txpower_set) : 0;
}

static int wb_start(const struct device *dev)
{
	int ret;

	ARG_UNUSED(dev);

	ret = spinel_set_u8(SPINEL_PROP_PHY_ENABLED, 1);
	if (ret == 0) {
		ret = spinel_set_u8(SPINEL_PROP_PHY_CHAN, wb.channel);
	}
	if (ret == 0) {
		ret = wb_apply_filter();
	}
	if (ret == 0 && wb.have_txpower) {
		ret = spinel_set_u8(SPINEL_PROP_PHY_TX_POWER, wb.txpower_set);
	}
	if (ret == 0) {
		ret = spinel_set_u8(SPINEL_PROP_MAC_PROMISCUOUS, wb.promisc);
	}
	if (ret == 0) {
		ret = spinel_set_u8(SPINEL_PROP_MAC_RAW_STREAM, 1);
	}
	if (ret == 0) {
		wb.started = true;
	}
	return ret;
}

static int wb_stop(const struct device *dev)
{
	ARG_UNUSED(dev);

	wb.started = false;
	(void)spinel_set_u8(SPINEL_PROP_MAC_RAW_STREAM, 0);
	return spinel_set_u8(SPINEL_PROP_PHY_ENABLED, 0);
}

/* The ACK frame of a transmission goes to the L2, which looks at its frame pending bit */
static void wb_handle_ack(const uint8_t *ack, size_t len, int8_t rssi, uint8_t lqi)
{
	struct net_pkt *pkt;
	size_t pkt_len = len;

	if (len < FCS_LEN + 3U) {
		return;
	}
	if (!IS_ENABLED(CONFIG_IEEE802154_L2_PKT_INCL_FCS)) {
		pkt_len -= FCS_LEN;
	}
	pkt = net_pkt_rx_alloc_with_buffer(wb.iface, pkt_len, NET_AF_UNSPEC, 0, K_NO_WAIT);
	if (pkt == NULL) {
		return;
	}
	if (net_pkt_write(pkt, ack, pkt_len) == 0) {
		net_pkt_set_ieee802154_rssi_dbm(pkt, rssi);
		net_pkt_set_ieee802154_lqi(pkt, lqi);
		net_pkt_cursor_init(pkt);
		if (ieee802154_handle_ack(wb.iface, pkt) != NET_OK) {
			LOG_DBG("ACK frame not handled");
		}
	}
	net_pkt_unref(pkt);
}

static int wb_tx(const struct device *dev, enum ieee802154_tx_mode mode, struct net_pkt *pkt,
		 struct net_buf *frag)
{
	uint8_t val[2 + PSDU_MAX + FCS_LEN + 6];
	uint8_t rsp[48];
	size_t rlen = sizeof(rsp);
	uint32_t prop;
	size_t o = 0;
	size_t used;
	uint8_t csma;
	uint8_t backoffs;
	int ret;

	ARG_UNUSED(dev);
	ARG_UNUSED(pkt);

	if (!wb.started) {
		return -ENETDOWN;
	}
	if (frag->len + FCS_LEN > PSDU_MAX + FCS_LEN || frag->len < 3U) {
		return -EINVAL;
	}

	switch (mode) {
	case IEEE802154_TX_MODE_DIRECT:
		csma = 0;
		backoffs = 0;
		break;
	case IEEE802154_TX_MODE_CCA:
		csma = 1;
		backoffs = 0;
		break;
	case IEEE802154_TX_MODE_CSMA_CA:
		csma = 1;
		backoffs = CONFIG_IEEE802154_STM32WB_MAX_CSMA_BACKOFFS;
		break;
	default:
		return -ENOTSUP;
	}

	/*
	 * d: PSDU with room for the FCS, then C channel, C backoffs, C retries, b csma, b retx,
	 * b skip aes
	 */
	sys_put_le16(frag->len + FCS_LEN, &val[o]);
	o += 2;
	memcpy(&val[o], frag->data, frag->len);
	o += frag->len;
	memset(&val[o], 0, FCS_LEN);
	o += FCS_LEN;
	val[o++] = wb.channel;
	val[o++] = backoffs;
	val[o++] = 0; /* the MAC above retries */
	val[o++] = csma;
	val[o++] = 0;
	val[o++] = 1;

	ret = spinel_request(SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_STREAM_RAW, val, o, rsp, &rlen,
			     K_MSEC(CONFIG_IEEE802154_STM32WB_TX_TIMEOUT_MS));
	if (ret != 0) {
		return ret == -ETIMEDOUT ? -EIO : ret;
	}

	/* LAST_STATUS: i status, b pending, b header updated, d ack frame, c rssi, ... */
	used = spinel_get_uint(rsp, rlen, &prop);
	if (used == 0U || prop != SPINEL_PROP_LAST_STATUS || rlen <= used) {
		return -EIO;
	}
	{
		uint32_t status;
		size_t su = spinel_get_uint(&rsp[used], rlen - used, &status);
		size_t p = used + su;

		if (su == 0U) {
			return -EIO;
		}
		switch (status) {
		case SPINEL_STATUS_OK:
			if (rlen >= p + 4U) {
				uint16_t ack_len = sys_get_le16(&rsp[p + 2U]);

				if (ack_len > 0U && rlen >= p + 4U + ack_len + 1U) {
					wb_handle_ack(&rsp[p + 4U], ack_len,
						      (int8_t)rsp[p + 4U + ack_len], 0);
				}
			}
			return 0;
		case SPINEL_STATUS_NO_ACK:
			return -ENOMSG;
		case SPINEL_STATUS_CCA_FAILURE:
			return -EBUSY;
		default:
			LOG_WRN("transmission failed, status %u", status);
			return -EIO;
		}
	}
}

static int wb_configure(const struct device *dev, enum ieee802154_config_type type,
			const struct ieee802154_config *config)
{
	ARG_UNUSED(dev);

	switch (type) {
	case IEEE802154_CONFIG_AUTO_ACK_FPB:
		/*
		 * Zigbee: the pending bit is set for every data request. The RCP does that while
		 * its source match table is off.
		 */
		if (config->auto_ack_fpb.mode == IEEE802154_FPB_ADDR_MATCH_ZIGBEE ||
		    !config->auto_ack_fpb.enabled) {
			/* source match is off after the RCP's reset: nothing to send */
			return 0;
		}
		return -ENOTSUP;
	case IEEE802154_CONFIG_PROMISCUOUS:
		wb.promisc = config->promiscuous ? SPINEL_PROMISC_FULL : 0;
		return wb.started ? spinel_set_u8(SPINEL_PROP_MAC_PROMISCUOUS, wb.promisc) : 0;
	case IEEE802154_CONFIG_EVENT_HANDLER:
		wb.event_cb = config->event_handler;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int wb_attr_get(const struct device *dev, enum ieee802154_attr attr,
		       struct ieee802154_attr_value *value)
{
	ARG_UNUSED(dev);

	return ieee802154_attr_get_channel_page_and_range(
		attr, IEEE802154_ATTR_PHY_CHANNEL_PAGE_ZERO_OQPSK_2450_BPSK_868_915,
		&drv_attr.phy_supported_channels, value);
}

static void wb_iface_init(struct net_if *iface)
{
	wb.iface = iface;
	net_if_set_link_addr(iface, wb.mac, sizeof(wb.mac), NET_LINK_IEEE802154);
	ieee802154_init(iface);
}

static int wb_init(const struct device *dev)
{
	uint8_t eui[8];
	size_t len = sizeof(eui);
	uint8_t v;
	size_t vlen = 1;
	int ret;

	ARG_UNUSED(dev);

	k_mutex_init(&wb.req_lock);
	k_sem_init(&wb.rsp_sem, 0, 1);
	k_sem_init(&wb.c2_ready, 0, 1);
	k_sem_init(&wb.shci_rsp, 0, 1);
	k_work_init(&wb.rx_work, wb_rx_work_fn);
	k_work_queue_init(&wb.rx_wq);
	k_work_queue_start(&wb.rx_wq, wb_rx_stack, K_KERNEL_STACK_SIZEOF(wb_rx_stack),
			   K_PRIO_COOP(CONFIG_IEEE802154_STM32WB_RX_PRIORITY), NULL);
	k_thread_name_set(&wb.rx_wq.thread, "wb_802154_rx");

	ret = wb_start_cpu2();
	if (ret != 0) {
		return ret;
	}

	/* The RCP reports its reset first; give it time to come up */
	k_msleep(20);

	ret = spinel_get(SPINEL_PROP_HWADDR, eui, &len);
	if (ret != 0 || len != sizeof(eui)) {
		LOG_ERR("cannot read the EUI-64 from the RCP (%d)", ret);
		return ret != 0 ? ret : -EIO;
	}
	/* The link address is big endian, as the RCP reports the EUI-64 */
	memcpy(wb.mac, eui, sizeof(wb.mac));

	wb.cca_threshold = -75;
	if (spinel_get(SPINEL_PROP_PHY_CCA_THRESHOLD, &v, &vlen) == 0 && vlen == 1U) {
		wb.cca_threshold = (int8_t)v;
	}
	wb.channel = 11;

	LOG_INF("STM32WB 802.15.4 radio ready, EUI-64 %02x%02x%02x%02x%02x%02x%02x%02x", eui[0],
		eui[1], eui[2], eui[3], eui[4], eui[5], eui[6], eui[7]);
	return 0;
}

static const struct ieee802154_radio_api wb_radio_api = {
	.iface_api.init = wb_iface_init,
	.get_capabilities = wb_get_capabilities,
	.cca = wb_cca,
	.set_channel = wb_set_channel,
	.filter = wb_filter,
	.set_txpower = wb_set_txpower,
	.start = wb_start,
	.stop = wb_stop,
	.tx = wb_tx,
	.configure = wb_configure,
	.attr_get = wb_attr_get,
};

#if defined(CONFIG_NET_L2_IEEE802154)
#define L2		IEEE802154_L2
#define L2_CTX_TYPE	NET_L2_GET_CTX_TYPE(IEEE802154_L2)
#define MTU		IEEE802154_MTU
#elif defined(CONFIG_NET_L2_CUSTOM_IEEE802154)
#define L2		CUSTOM_IEEE802154_L2
#define L2_CTX_TYPE	NET_L2_GET_CTX_TYPE(CUSTOM_IEEE802154_L2)
#define MTU		CONFIG_NET_L2_CUSTOM_IEEE802154_MTU
#endif

NET_DEVICE_DT_INST_DEFINE(0, wb_init, NULL, &wb, NULL, CONFIG_IEEE802154_STM32WB_INIT_PRIO,
			  &wb_radio_api, L2, L2_CTX_TYPE, MTU);
