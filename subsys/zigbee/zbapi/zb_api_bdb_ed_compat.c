/* SPDX-License-Identifier: Apache-2.0 */

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"
#endif
#include "zb_common_stub.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <zephyr/zigbee/zb_radio_port.h>

const addrExt_t g_zero_addr __attribute__((weak)) = {0};
const addrExt_t g_invalid_addr __attribute__((weak)) = {0xFF, 0xFF, 0xFF, 0xFF,
						       0xFF, 0xFF, 0xFF, 0xFF};

const u8 tcLinkKeyCentralDefault[SEC_KEY_LEN] __attribute__((weak)) = {
	'Z', 'i', 'g', 'B', 'e', 'e', 'A', 'l',
	'l', 'i', 'a', 'n', 'c', 'e', '0', '9',
};
const u8 linkKeyDistributedCertification[SEC_KEY_LEN] __attribute__((weak)) = {0};
const u8 linkKeyDistributedMaster[SEC_KEY_LEN] __attribute__((weak)) = {0};

aps_pib_attributes_t aps_ib __attribute__((weak)) = {
	.aps_channel_mask = (1UL << 11),
	.aps_use_insecure_join = TRUE,
};

#if defined(__APPLE__)
/*
 * Mach-O ld64 rejects pointer relocations into the packed ss_info_base_t
 * layout. Populate the pointer members at runtime from zdo_ssInfoInit().
 */
ss_info_base_t ss_ib __attribute__((weak)) = {
	.securityLevel = 0,
	.trust_center_address = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
};
#else
ss_info_base_t ss_ib __attribute__((weak)) = {
	.tcLinkKey = (u8 *)tcLinkKeyCentralDefault,
	.distributeLinkKey = (u8 *)linkKeyDistributedMaster,
	.touchLinkKey = (u8 *)linkKeyDistributedCertification,
	.securityLevel = 0,
	.trust_center_address = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
};
#endif

ss_dev_keyPair_t g_ssDevKeyPair __attribute__((weak));

nwk_ctx_t g_zbNwkCtx __attribute__((weak)) = {
	.is_factory_new = 1,
};

__attribute__((weak)) u8 af_dataSend(u8 srcEp, epInfo_t *pDstEpInfo, u16 clusterId,
				     u16 cmdPldLen, u8 *cmdPld, u8 *apsCnt)
{
	ARG_UNUSED(srcEp);
	ARG_UNUSED(pDstEpInfo);
	ARG_UNUSED(clusterId);
	ARG_UNUSED(cmdPldLen);
	ARG_UNUSED(cmdPld);

	if (apsCnt != NULL) {
		*apsCnt = 0U;
	}

	return APS_STATUS_NOT_SUPPORTED;
}

__attribute__((weak)) u8 zdo_ssInfoKeyGet(void)
{
	return ss_ib.activeSecureMaterialIndex;
}

__attribute__((weak)) zdo_status_t zb_zdoSimpleDescReq(u16 dstNwkAddr, zdo_simple_descriptor_req_t *pReq,
						       u8 *seqNo, zdo_callback indCb)
{
	ARG_UNUSED(dstNwkAddr);
	ARG_UNUSED(pReq);
	ARG_UNUSED(indCb);

	if (seqNo != NULL) {
		*seqNo = 0U;
	}

	return ZDO_NOT_SUPPORTED;
}

__attribute__((weak)) zdo_status_t zb_mgmtPermitJoinReq(u16 dstNwkAddr, u8 permitJoinDuration,
							 u8 tcSignificance, u8 *seqNo, zdo_callback indCb)
{
	ARG_UNUSED(dstNwkAddr);
	ARG_UNUSED(permitJoinDuration);
	ARG_UNUSED(tcSignificance);
	ARG_UNUSED(indCb);

	if (seqNo != NULL) {
		*seqNo = 0U;
	}

	return ZDO_NOT_SUPPORTED;
}

__attribute__((weak)) void zb_zdoSendParentAnnce(void)
{
}

__attribute__((weak)) u8 is_device_factory_new(void)
{
	return g_zbNwkCtx.is_factory_new ? 1U : 0U;
}

__attribute__((weak)) void tl_zbMacChannelSet(u8 chan)
{
	g_zbInfo.macPib.phyChannelCur = chan;
	(void)zb_radio_port_set_channel(chan);
}

__attribute__((weak)) zdo_status_t zb_zdoNodeDescReq(u16 dstNwkAddr, zdo_node_descriptor_req_t *pReq,
						     u8 *seqNo, zdo_callback indCb)
{
	ARG_UNUSED(dstNwkAddr);
	ARG_UNUSED(pReq);
	ARG_UNUSED(indCb);

	if (seqNo != NULL) {
		*seqNo = 0U;
	}

	return ZDO_NOT_SUPPORTED;
}

__attribute__((weak)) u8 zb_nlmePermitJoiningRequest(u8 permitDuration)
{
#if ZB_ROUTER_ROLE
	g_zbMacPib.associationPermit = (permitDuration != 0U) ? 1U : 0U;
	return ZDO_SUCCESS;
#else
	ARG_UNUSED(permitDuration);
	return ZDO_NOT_SUPPORTED;
#endif
}

__attribute__((weak)) void ss_securityModeSet(ss_securityMode_e m)
{
	if (m == SS_SEMODE_DISTRIBUTED) {
		ss_ib.tcPolicy.updateTCLKrequired = 0;
		memcpy(ss_ib.trust_center_address, g_invalid_addr, EXT_ADDR_LEN);
	} else if (m == SS_SEMODE_CENTRALIZED) {
		ss_ib.tcPolicy.updateTCLKrequired = 1;
		memset(ss_ib.trust_center_address, 0, EXT_ADDR_LEN);
	}
}

__attribute__((weak)) u8 zb_apsmeRequestKeyReq(ss_apsmeRequestKeyReq_t *pRequestKeyReq)
{
	ARG_UNUSED(pRequestKeyReq);
	return RET_OPERATION_FAILED;
}

void zb_preConfigNwkKey(u8 *nwkKey, bool enTransKey)
{
	ss_material_set_t *material;

	if ((nwkKey == NULL) || !is_device_factory_new()) {
		return;
	}

	material = &ss_ib.nwkSecurMaterialSet[0];
	memcpy(material->key, nwkKey, SEC_KEY_LEN);
	material->keySeqNum = 0U;
	material->keyType = 1U;

	ss_ib.activeSecureMaterialIndex = 0U;
	ss_ib.activeKeySeqNum = 0U;
	ss_ib.preConfiguredKeyType |= SS_PRECONFIGURED_NWKKEY;

	if (!enTransKey) {
		aps_ib.aps_authenticated = 1U;
	}
}

__attribute__((weak)) bool zb_isDeviceJoinedNwk(void)
{
	return g_zbNwkCtx.joined ? TRUE : FALSE;
}

__attribute__((weak)) u8 zb_nlmeLeaveReq(nlme_leave_req_t *pLeaveReq)
{
	ARG_UNUSED(pLeaveReq);
	return ZDO_NOT_SUPPORTED;
}

__attribute__((weak)) u8 zb_address_ieee_by_short(u16 short_addr, addrExt_t ieee_address)
{
	ARG_UNUSED(short_addr);

	if (ieee_address != NULL) {
		ZB_IEEE_ADDR_INVALID(ieee_address);
	}

	return FALSE;
}

__attribute__((weak)) zdo_status_t zb_zdoIeeeAddrReq(u16 dstNwkAddr, zdo_ieee_addr_req_t *pReq,
						     u8 *seqNo, zdo_callback indCb)
{
	ARG_UNUSED(dstNwkAddr);
	ARG_UNUSED(pReq);
	ARG_UNUSED(indCb);

	if (seqNo != NULL) {
		*seqNo = 0U;
	}

	return ZDO_NOT_SUPPORTED;
}

__attribute__((weak)) zdo_status_t zb_zdoBindUnbindReq(bool isBinding, zdo_bind_req_t *pReq,
						       u8 *seqNo, zdo_callback indCb)
{
	ARG_UNUSED(isBinding);
	ARG_UNUSED(pReq);
	ARG_UNUSED(indCb);

	if (seqNo != NULL) {
		*seqNo = 0U;
	}

	return ZDO_NOT_SUPPORTED;
}
