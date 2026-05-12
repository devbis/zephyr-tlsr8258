/* SPDX-License-Identifier: Apache-2.0 */

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"
#endif
#include "zb_common_stub.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

extern void rf_setChannel(u8 chn);

const addrExt_t g_zero_addr __attribute__((weak)) = {0};
const addrExt_t g_invalid_addr __attribute__((weak)) = {0xFF, 0xFF, 0xFF, 0xFF,
						       0xFF, 0xFF, 0xFF, 0xFF};

const u8 tcLinkKeyCentralDefault[SEC_KEY_LEN] __attribute__((weak)) = {0};
const u8 linkKeyDistributedCertification[SEC_KEY_LEN] __attribute__((weak)) = {0};
const u8 linkKeyDistributedMaster[SEC_KEY_LEN] __attribute__((weak)) = {0};

aps_pib_attributes_t aps_ib __attribute__((weak)) = {
	.aps_channel_mask = (1UL << 11),
	.aps_use_insecure_join = TRUE,
};

ss_info_base_t ss_ib __attribute__((weak)) = {
	.tcLinkKey = (u8 *)tcLinkKeyCentralDefault,
	.distributeLinkKey = (u8 *)linkKeyDistributedMaster,
	.touchLinkKey = (u8 *)linkKeyDistributedCertification,
	.securityLevel = 0,
	.trust_center_address = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
};

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
	rf_setChannel(chan);
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
	ARG_UNUSED(permitDuration);
	return ZDO_NOT_SUPPORTED;
}

__attribute__((weak)) void ss_securityModeSet(ss_securityMode_e m)
{
	ss_ib.securityLevel = (m == SS_SEMODE_DISTRIBUTED) ? 0U : 5U;
}

__attribute__((weak)) u8 zb_apsmeRequestKeyReq(ss_apsmeRequestKeyReq_t *pRequestKeyReq)
{
	ARG_UNUSED(pRequestKeyReq);
	return RET_OPERATION_FAILED;
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
