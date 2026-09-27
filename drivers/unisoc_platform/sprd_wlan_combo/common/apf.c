/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#include "cmd.h"
#include "common.h"
#include "chip_ops.h"
#include "debug.h"
#include "vendor.h"
#include "apf.h"

static struct apf_program_state g_apf_prog;

static const char *apf_cmd2str(u16 cmd)
{
	switch (cmd) {
	case WLAN_SET_PACKET_FILTER:
		return "APF_SET";
	case WLAN_GET_PACKET_FILTER:
		return "APF_GET";
	case WLAN_WRITE_PACKET_FILTER:
		return "APF_WRITE";
	case WLAN_READ_PACKET_FILTER:
		return "APF_READ";
	case WLAN_ENABLE_PACKET_FILTER:
		return "APF_ENABLE";
	case WLAN_DISABLE_PACKET_FILTER:
		return "APF_DISABLE";

	case WLAN_APF_FORCE_DISABLE:
		return "APF_FORCE_DISABLE";
	case WLAN_GET_APF_FORCE_DIS_STATUS:
		return "APF_FORCE_DIS_STATUS";

	default:
		return "APF_CMD_UNKNOWN";
	}
}

static bool is_apf_valid(struct apf_capa *apf_cap)
{
	if (apf_cap && apf_cap->apf_version >= APF_VERSION_4 &&
		apf_cap->max_capa_apf_prog_len) {
		return true;
	} else {
		return false;
	}
}

int validate_apf_program_attr(const struct nlattr *attr,
			    struct netlink_ext_ack *extack)
{
	unsigned int ret = 0, len;
	struct apf_program_state *apf_st = &g_apf_prog;

	if (!attr || !is_apf_valid(&apf_st->apf_cap)) {
		wl_err("%s error", __func__);
		ret = -ENOTSUPP;
		goto validate_fail;
	}

	len = nla_len(attr);
	if (len > apf_st->apf_cap.max_capa_apf_prog_len) {
		wl_err("%s %d prog %d error", __func__, nla_type(attr), len);
		ret = -EINVAL;
		goto validate_fail;
	}

	return ret;

validate_fail:
	NL_SET_ERR_MSG_ATTR(extack, attr, "check apf_prog_len failed");
	return ret;
}

static int apf_subcmd_rsp(struct sprd_vif *vif, struct apf_request *apf_req,
			struct apf_response *apf_rsp)
{
	struct sprd_priv *priv = vif->priv;
	struct wiphy *wiphy = priv->wiphy;
	struct sk_buff *skb;
	struct apf_program_state *apf_st = priv->apf_state;
	u32 apf_version = APF_VERSION_4, apf_max_capa_prog_len = APF_MAX_PROG_SIZE;
	int ret = 0, rsp_hal_len;
	void *slice_prog;

	if (!apf_st || !apf_req) {
		netdev_info(vif->ndev, "%s apf_st error", __func__);
		return -EINVAL;
	}

	if (!is_apf_valid(&apf_st->apf_cap)) {
		netdev_info(vif->ndev, "%s error", __func__);
		return -ENOTSUPP;
	}

	apf_version = apf_st->apf_cap.apf_version;
	apf_max_capa_prog_len = apf_st->apf_cap.max_capa_apf_prog_len;

	if (apf_req->apf_hdr.apf_subcmd == WLAN_READ_PACKET_FILTER) {
		if (!apf_rsp) {
			netdev_info(vif->ndev, "%s slice_prog error", __func__);
			return -EINVAL;
		}
		slice_prog = apf_rsp->rsp_data;
		rsp_hal_len = apf_req->apf_offset_slice_size + sizeof(u32);
	} else {
		rsp_hal_len = sizeof(u32) + sizeof(u32);
	}

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
			NLMSG_HDRLEN + ALIGN(rsp_hal_len, APF_ALIGN_SIZE));
	if (!skb) {
		netdev_info(vif->ndev, "%s skb alloc failed.\n", __func__);
		return -ENOMEM;
	}

	wl_debug("%s cmd%u rsp_hal_len%d.\n", __func__,
		apf_req->apf_hdr.apf_subcmd, rsp_hal_len);
	if (apf_req->apf_hdr.apf_subcmd == WLAN_READ_PACKET_FILTER) {
		if (nla_put_u32(skb, VENDOR_ATTR_PACKET_FILTER_SUB_CMD,
			WLAN_READ_PACKET_FILTER) ||
			nla_put(skb, VENDOR_ATTR_PACKET_FILTER_PROGRAM,
			apf_req->apf_offset_slice_size, slice_prog)) {
			netdev_info(vif->ndev, "%s nla put failed.\n", __func__);
			ret = -EINVAL;
			goto exit;
		}
	} else {
		if (nla_put_u32(skb, VENDOR_ATTR_PACKET_FILTER_VERSION, apf_version) ||
			nla_put_u32(skb, VENDOR_ATTR_PACKET_FILTER_SIZE,
				apf_max_capa_prog_len)) {
			wl_err("%s put fail\n", __func__);
			ret = -EINVAL;
			goto exit;
		}
	}

	ret = cfg80211_vendor_cmd_reply(skb);
	if (ret) {
		netdev_err(vif->ndev, "%s failed %d reply skb!\n", __func__, ret);
	}
	return ret;

exit:
	kfree_skb(skb);
	return ret;
}

static int apf_check_cp2_rsp(struct apf_program_state *apf_st,
				struct apf_request *apf_req, struct apf_response *apf_rsp)
{
	struct apf_capa *apf_cap;
	u16 apf_subcmd = apf_req->apf_hdr.apf_subcmd;
	int ret = 0;

	if (apf_rsp->apf_hdr.apf_subcmd != apf_subcmd) {
		wl_err("%s subcmd %u err %u.\n", __func__, apf_rsp->apf_hdr.apf_subcmd,
			apf_subcmd);
		ret = -EINVAL;
		goto exit;
	} else {
		wl_debug("%s %s(%u) ret %u.\n", __func__, apf_cmd2str(apf_subcmd),
			apf_subcmd, apf_rsp->cmd_ret_value);
		if (apf_subcmd == WLAN_GET_PACKET_FILTER) {
			apf_cap = (struct apf_capa *)apf_rsp->rsp_data;
			if (!is_apf_valid(apf_cap)) {
				wl_err("%s apf_cap %u %u error.\n", __func__,
				       apf_cap->apf_version, apf_cap->max_capa_apf_prog_len);
				ret = -ENOTSUPP;
				goto exit;
			}
			apf_st->apf_cap.apf_version = apf_cap->apf_version;
			apf_st->apf_cap.max_capa_apf_prog_len =
			    apf_cap->max_capa_apf_prog_len;
		} else if (apf_subcmd == WLAN_READ_PACKET_FILTER) {
			u32 rsp_prog_data_len =
			    apf_rsp->apf_hdr.length - sizeof(apf_rsp->cmd_ret_value);
			if (rsp_prog_data_len != apf_req->apf_offset_slice_size) {
				wl_debug("%s expect %u but %u.\n", __func__,
					apf_req->apf_offset_slice_size, rsp_prog_data_len);
			}
		}
	}

exit:
	return ret;
}

#define APF_DRIVER_TEST 0
static int apf_subcmd_send_recv(struct sprd_vif *vif, struct apf_request *apf_req,
				void *src_slice_prog, struct apf_response *apf_rsp)
{
	struct sprd_priv *priv = vif->priv;
	struct apf_program_state *apf_st = priv->apf_state;
	struct apf_capa *apf_cap;
	int ret = 0;
	u16 apf_subcmd, expect_rsp_len, rsp_len;

	if (!apf_st || !apf_req || !apf_rsp) {
		netdev_info(vif->ndev, "%s error params", __func__);
		return -EINVAL;
	}
	apf_subcmd = apf_req->apf_hdr.apf_subcmd;
	if ((apf_subcmd == WLAN_SET_PACKET_FILTER || apf_subcmd == WLAN_WRITE_PACKET_FILTER) &&
		!src_slice_prog) {
		netdev_info(vif->ndev, "%s NULL program", __func__);
		return -EINVAL;
	}
	expect_rsp_len = apf_rsp->apf_hdr.length + sizeof(struct apf_cmd_header);
	rsp_len = expect_rsp_len;

	if (APF_DRIVER_TEST) {
		apf_rsp->apf_hdr.apf_subcmd = apf_subcmd;
		if (apf_subcmd == WLAN_GET_PACKET_FILTER) {
			apf_cap = (struct apf_capa *)apf_rsp->rsp_data;
			apf_cap->apf_version = APF_VERSION_4;
			apf_cap->max_capa_apf_prog_len = APF_MAX_PROG_SIZE;
		} else if (apf_subcmd == WLAN_READ_PACKET_FILTER) {
			apf_rsp->apf_hdr.length = expect_rsp_len - sizeof(struct apf_cmd_header);
		}
	} else {
		if (apf_st->apf_req_send_rcv) {
			ret = apf_st->apf_req_send_rcv(vif, apf_req, src_slice_prog,
						       apf_rsp, &rsp_len);
			if (ret) {
				wl_err("%s apf_req_send_rcv ret %d.\n", __func__, ret);
				goto exit;
			}
		}
	}
	wl_all("%s-%s %s(%u)-%u-%u-%u-%u-(%lu-%u).\n", __func__, current->comm,
		apf_cmd2str(apf_req->apf_hdr.apf_subcmd), apf_req->apf_hdr.apf_subcmd,
		apf_req->apf_currt_offset, apf_req->apf_offset_slice_size,
		apf_req->apf_trans_size, apf_req->apf_prog_len,
		apf_req->apf_hdr.length + sizeof(struct apf_cmd_header), expect_rsp_len);

	if (rsp_len != expect_rsp_len) {
		wl_err("%s rsp_len %u err %u.\n", __func__, rsp_len, expect_rsp_len);
		ret = -EINVAL;
		goto exit;
	} else {
		return apf_check_cp2_rsp(apf_st, apf_req, apf_rsp);
	}

exit:
	return ret;
}

static int apf_set_request_params(struct apf_program_state *apf_st,
			struct nlattr **tb, struct apf_request *apf_req)
{
	if (apf_req->apf_hdr.apf_subcmd != WLAN_SET_PACKET_FILTER) {
		wl_err("%s cmd %u err.\n", __func__, apf_req->apf_hdr.apf_subcmd);
		return -EINVAL;
	}

	if (tb[VENDOR_ATTR_PACKET_FILTER_ID] &&
		nla_len(tb[VENDOR_ATTR_PACKET_FILTER_ID]) == sizeof(u32)) {
		u32 apf_id = nla_get_u32(tb[VENDOR_ATTR_PACKET_FILTER_ID]);
		if (apf_id != PACKET_FILTER_ID) {
			wl_err("%s apf_id %u err.\n", __func__, apf_id);
			return -EINVAL;
		}
	} else {
		wl_err("%s get apf_id err.\n", __func__);
		return -EINVAL;
	}

	if (tb[VENDOR_ATTR_PACKET_FILTER_SIZE] &&
		nla_len(tb[VENDOR_ATTR_PACKET_FILTER_SIZE]) == sizeof(u32)) {
		apf_req->apf_prog_len = nla_get_u32(tb[VENDOR_ATTR_PACKET_FILTER_SIZE]);
		if (apf_req->apf_prog_len > apf_st->apf_cap.max_capa_apf_prog_len) {
			wl_err("%s prog_len %u err %u.\n", __func__, apf_req->apf_prog_len,
				apf_st->apf_cap.max_capa_apf_prog_len);
			return -EINVAL;
		}
		apf_req->apf_trans_size = apf_req->apf_prog_len;
	} else {
		wl_err("%s get prog_len err.\n", __func__);
		return -EINVAL;
	}

	if (tb[VENDOR_ATTR_PACKET_FILTER_CURRENT_OFFSET] &&
		nla_len(tb[VENDOR_ATTR_PACKET_FILTER_CURRENT_OFFSET]) == sizeof(u32)) {
		apf_req->apf_currt_offset =
			nla_get_u32(tb[VENDOR_ATTR_PACKET_FILTER_CURRENT_OFFSET]);
		if (apf_req->apf_currt_offset > apf_req->apf_prog_len) {
			wl_err("%s overrid %u err %u.\n", __func__, apf_req->apf_currt_offset,
				apf_req->apf_prog_len);
			return -EINVAL;
		}
	} else {
		wl_err("%s get apf_currt_offset err.\n", __func__);
		return -EINVAL;
	}

	if (tb[VENDOR_ATTR_PACKET_FILTER_PROGRAM] &&
		nla_len(tb[VENDOR_ATTR_PACKET_FILTER_PROGRAM])) {
		apf_req->apf_offset_slice_size =
			nla_len(tb[VENDOR_ATTR_PACKET_FILTER_PROGRAM]);
		if (apf_req->apf_currt_offset + apf_req->apf_offset_slice_size >
			apf_req->apf_prog_len) {
			wl_err("%s overrid %u %u err %u.\n", __func__, apf_req->apf_currt_offset,
				apf_req->apf_offset_slice_size, apf_req->apf_prog_len);
			return -EINVAL;
		}

#if 0
		mutex_lock(&apf_st->apf_lock);
		memcpy(apf_st->apf_program + apf_req->apf_currt_offset,
			nla_data(tb[VENDOR_ATTR_PACKET_FILTER_PROGRAM]),
			apf_req->apf_offset_slice_size);
		apf_st->apf_program_len = apf_req->apf_prog_len;
		if (apf_req->apf_currt_offset + apf_req->apf_offset_slice_size ==
			apf_req->apf_prog_len) {
			// update program finish
			apf_st->afp_checksum = 0x12345678;
			apf_st->afp_md5 = 0x0123456789ABCDEF;
		}
		mutex_unlock(&apf_st->apf_lock);
#endif
	} else {
		wl_err("%s get apf_program err.\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int apf_write_request_params(struct apf_program_state *apf_st,
			struct nlattr **tb, struct apf_request *apf_req)
{
	if (apf_req->apf_hdr.apf_subcmd != WLAN_WRITE_PACKET_FILTER) {
		wl_err("%s cmd %u err.\n", __func__, apf_req->apf_hdr.apf_subcmd);
		return -EINVAL;
	}

	if (tb[VENDOR_ATTR_PACKET_FILTER_ID] &&
		nla_len(tb[VENDOR_ATTR_PACKET_FILTER_ID]) == sizeof(u32)) {
		u32 apf_id = nla_get_u32(tb[VENDOR_ATTR_PACKET_FILTER_ID]);
		if (apf_id != PACKET_FILTER_ID) {
			wl_err("%s apf_id %u err.\n", __func__, apf_id);
			return -EINVAL;
		}
	} else {
		wl_err("%s get apf_id err.\n", __func__);
		return -EINVAL;
	}

	if (tb[VENDOR_ATTR_PACKET_FILTER_PROG_LENGTH] &&
		nla_len(tb[VENDOR_ATTR_PACKET_FILTER_PROG_LENGTH]) == sizeof(u32)) {
		apf_req->apf_prog_len =
		    nla_get_u32(tb[VENDOR_ATTR_PACKET_FILTER_PROG_LENGTH]);
		if (apf_req->apf_prog_len > apf_st->apf_cap.max_capa_apf_prog_len) {
			wl_err("%s prog_len %u err %u.\n", __func__, apf_req->apf_prog_len,
				apf_st->apf_cap.max_capa_apf_prog_len);
			return -EINVAL;
		}
	} else {
		wl_err("%s get apf_program_len err.\n", __func__);
		return -EINVAL;
	}

	if (tb[VENDOR_ATTR_PACKET_FILTER_SIZE] &&
		nla_len(tb[VENDOR_ATTR_PACKET_FILTER_SIZE]) == sizeof(u32)) {
		apf_req->apf_trans_size = nla_get_u32(tb[VENDOR_ATTR_PACKET_FILTER_SIZE]);
		if (apf_req->apf_trans_size > apf_req->apf_prog_len) {
			wl_err("%s tran_size %u err %u.\n", __func__, apf_req->apf_prog_len,
				apf_req->apf_trans_size);
			return -EINVAL;
		}
	} else {
		wl_err("%s get apf_size err.\n", __func__);
		return -EINVAL;
	}

	if (tb[VENDOR_ATTR_PACKET_FILTER_CURRENT_OFFSET] &&
		nla_len(tb[VENDOR_ATTR_PACKET_FILTER_CURRENT_OFFSET]) == sizeof(u32)) {
		apf_req->apf_currt_offset =
			nla_get_u32(tb[VENDOR_ATTR_PACKET_FILTER_CURRENT_OFFSET]);
		if (apf_req->apf_currt_offset > apf_req->apf_prog_len) {
			wl_err("%s cur_offset %u err %u.\n", __func__,
			       apf_req->apf_currt_offset, apf_req->apf_prog_len);
			return -EINVAL;
		}
	} else {
		wl_err("%s get apf_currt_offset err.\n", __func__);
		return -EINVAL;
	}

	if (tb[VENDOR_ATTR_PACKET_FILTER_PROGRAM] &&
		nla_len(tb[VENDOR_ATTR_PACKET_FILTER_PROGRAM])) {
		apf_req->apf_offset_slice_size =
			nla_len(tb[VENDOR_ATTR_PACKET_FILTER_PROGRAM]);

		if (apf_req->apf_offset_slice_size > apf_req->apf_trans_size ||
			apf_req->apf_currt_offset + apf_req->apf_offset_slice_size >
			apf_req->apf_prog_len) {
			wl_err("%s overrid %u %u err %u %u.\n", __func__,
			       apf_req->apf_prog_len, apf_req->apf_trans_size,
			       apf_req->apf_currt_offset, apf_req->apf_offset_slice_size);
			return -EINVAL;
		}

#if 0
		mutex_lock(&apf_st->apf_lock);
		memcpy(apf_st->apf_program + apf_req->apf_currt_offset,
			nla_data(tb[VENDOR_ATTR_PACKET_FILTER_PROGRAM]),
			apf_req->apf_offset_slice_size);
		apf_st->apf_program_len = apf_req->apf_prog_len;
		// program updated, calc checksum
		apf_st->afp_checksum = 0x12345678;
		apf_st->afp_md5 = 0x0123456789ABCDEF;
		mutex_unlock(&apf_st->apf_lock);
#endif
	} else {
		wl_err("%s get apf_program err.\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int apf_read_request_params(struct apf_program_state *apf_st,
			struct nlattr **tb, struct apf_request *apf_req)
{
	if (apf_req->apf_hdr.apf_subcmd != WLAN_READ_PACKET_FILTER) {
		wl_err("%s cmd %u err.\n", __func__, apf_req->apf_hdr.apf_subcmd);
		return -EINVAL;
	}

	if (tb[VENDOR_ATTR_PACKET_FILTER_CURRENT_OFFSET] &&
		nla_len(tb[VENDOR_ATTR_PACKET_FILTER_CURRENT_OFFSET]) == sizeof(u32)) {
		apf_req->apf_currt_offset =
		    nla_get_u32(tb[VENDOR_ATTR_PACKET_FILTER_CURRENT_OFFSET]);
		if (apf_req->apf_currt_offset > apf_st->apf_cap.max_capa_apf_prog_len) {
			wl_err("%s overrid %u err %u.\n", __func__, apf_req->apf_currt_offset,
				apf_st->apf_cap.max_capa_apf_prog_len);
			return -EINVAL;
		}
	} else {
		wl_err("%s get apf_currt_offset err.\n", __func__);
		return -EINVAL;
	}

	if (tb[VENDOR_ATTR_PACKET_FILTER_SIZE] &&
		nla_len(tb[VENDOR_ATTR_PACKET_FILTER_SIZE]) == sizeof(u32)) {
		apf_req->apf_offset_slice_size =
		    nla_get_u32(tb[VENDOR_ATTR_PACKET_FILTER_SIZE]);
		if (apf_req->apf_currt_offset + apf_req->apf_offset_slice_size >
			apf_st->apf_cap.max_capa_apf_prog_len) {
			wl_err("%s overrid %u %u err %u.\n", __func__,
			       apf_req->apf_currt_offset, apf_req->apf_offset_slice_size,
			       apf_st->apf_cap.max_capa_apf_prog_len);
			return -EINVAL;
		}
	} else {
		wl_err("%s get apf_size err.\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int get_apf_request(struct sprd_vif *vif, struct nlattr **tb,
			struct apf_request *apf_req, u16 *expect_rsp_len)
{
	struct apf_program_state *apf_st;
	int ret = 0;
	u16 rsp_len = 0;

	if (!vif || !vif->priv || !vif->priv->apf_state || !tb || !apf_req) {
		wl_err("%s(%d).\n", __func__, __LINE__);
		return -EINVAL;
	}
	apf_st = vif->priv->apf_state;

	if (tb[VENDOR_ATTR_PACKET_FILTER_SUB_CMD] &&
		nla_len(tb[VENDOR_ATTR_PACKET_FILTER_SUB_CMD]) == sizeof(u32)) {
		apf_req->apf_hdr.apf_subcmd =
		    nla_get_u32(tb[VENDOR_ATTR_PACKET_FILTER_SUB_CMD]);
		if (!apf_req->apf_hdr.apf_subcmd ||
			apf_req->apf_hdr.apf_subcmd > WLAN_DISABLE_PACKET_FILTER) {
			wl_err("%s subcmd %u err.\n", __func__, apf_req->apf_hdr.apf_subcmd);
			return -EINVAL;
		}
	} else {
		wl_err("%s get subcmd err.\n", __func__);
		return -EINVAL;
	}

	switch (apf_req->apf_hdr.apf_subcmd) {
	case WLAN_SET_PACKET_FILTER:
		if (apf_set_request_params(apf_st, tb, apf_req)) {
			return -EINVAL;
		}
		apf_req->apf_hdr.length = sizeof(struct apf_request) -
			sizeof(struct apf_cmd_header) +	apf_req->apf_offset_slice_size;
		rsp_len = sizeof(struct apf_response);
		break;
	case WLAN_GET_PACKET_FILTER:
		apf_req->apf_hdr.length = 0;
		rsp_len = sizeof(struct apf_response) + sizeof(struct apf_capa);
		break;
	case WLAN_WRITE_PACKET_FILTER:
		wl_err("%s apf_write no used in HAL, so just return.\n", __func__);
		return -EINVAL;

		if (apf_write_request_params(apf_st, tb, apf_req)) {
			return -EINVAL;
		}
		apf_req->apf_hdr.length = sizeof(struct apf_request) -
			sizeof(struct apf_cmd_header) +	apf_req->apf_offset_slice_size;
		rsp_len = sizeof(struct apf_response);
		break;
	case WLAN_READ_PACKET_FILTER:
		if (apf_read_request_params(apf_st, tb, apf_req)) {
			return -EINVAL;
		}
		apf_req->apf_hdr.length =
		    sizeof(struct apf_request) - sizeof(struct apf_cmd_header);
		rsp_len = sizeof(struct apf_response) + apf_req->apf_offset_slice_size;
		break;
	case WLAN_ENABLE_PACKET_FILTER:
	case WLAN_DISABLE_PACKET_FILTER:
		apf_req->apf_hdr.length = 0;
		rsp_len = sizeof(struct apf_response);
		break;
	default:
		netdev_info(vif->ndev, "%s error %u params", __func__,
			apf_req->apf_hdr.apf_subcmd);
		return -EINVAL;
	}
	*expect_rsp_len = rsp_len;

	return ret;
}

int vendor_apf_packet_filter(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    const void *data, int len)
{
	struct nlattr *tb[VENDOR_ATTR_PACKET_FILTER_MAX + 1] = {0};
	struct sprd_vif *vif = netdev_priv(wdev->netdev);
	struct sprd_priv *priv = vif->priv;
	struct apf_program_state *apf_st = priv->apf_state;
	struct apf_request apf_req = {0};
	struct apf_response *apf_rsp = NULL;
	void *slice_prog_data = NULL;
	int ret = 0;
	u16 expect_rsp_len;

	if (!apf_st) {
		wl_err("%s(%d) apf_st.\n", __func__, __LINE__);
		return -EOPNOTSUPP;
	}

	if (nla_parse(tb, VENDOR_ATTR_PACKET_FILTER_MAX, data, len, NULL, NULL)) {
		wl_err("%s parse error.\n", __func__);
		return -EINVAL;
	}

	if (get_apf_request(vif, tb, &apf_req, &expect_rsp_len)) {
		wl_err("%s param error.\n", __func__);
		return -EINVAL;
	}

	wl_debug("%s-%s %s(%u)-%u-%u-%u-%u-(%lu-%u).\n", __func__, current->comm,
		apf_cmd2str(apf_req.apf_hdr.apf_subcmd), apf_req.apf_hdr.apf_subcmd,
		apf_req.apf_currt_offset, apf_req.apf_offset_slice_size,
		apf_req.apf_trans_size, apf_req.apf_prog_len,
		apf_req.apf_hdr.length + sizeof(struct apf_cmd_header), expect_rsp_len);

	if (apf_req.apf_hdr.apf_subcmd == WLAN_GET_PACKET_FILTER) {
		if (is_apf_valid(&apf_st->apf_cap)) {
			// apf_cap valid, just rsp to HAL.
			goto apf_rsp_lable;
		}
	} else {
		if (!is_apf_valid(&apf_st->apf_cap)) {
			netdev_info(vif->ndev, "%s cmd %u error %u", __func__,
				apf_req.apf_hdr.apf_subcmd, apf_st->apf_cap.apf_version);
			return -ENOTSUPP;
		}
	}

	slice_prog_data = tb[VENDOR_ATTR_PACKET_FILTER_PROGRAM] ?
			nla_data(tb[VENDOR_ATTR_PACKET_FILTER_PROGRAM]) : NULL;
	apf_rsp = kzalloc(ALIGN(expect_rsp_len, APF_ALIGN_SIZE), GFP_KERNEL);
	if (!apf_rsp) {
		netdev_info(vif->ndev, "%s alloc failed", __func__);
		return -ENOMEM;
	}

	apf_rsp->apf_hdr.length = expect_rsp_len - sizeof(struct apf_cmd_header);
	ret = apf_subcmd_send_recv(vif, &apf_req, slice_prog_data, apf_rsp);
	if (ret) {
		wl_err("%s apf_send err %d.\n", __func__, ret);
		kfree(apf_rsp);
		return ret;
	}

	if (apf_req.apf_hdr.apf_subcmd == WLAN_SET_PACKET_FILTER ||
		apf_req.apf_hdr.apf_subcmd == WLAN_WRITE_PACKET_FILTER ||
		apf_req.apf_hdr.apf_subcmd == WLAN_ENABLE_PACKET_FILTER ||
		apf_req.apf_hdr.apf_subcmd == WLAN_DISABLE_PACKET_FILTER) {
		// just return for APF RD/WR/EN/DIS to HAL
		goto exit;
	}

apf_rsp_lable:
	ret = apf_subcmd_rsp(vif, &apf_req, apf_rsp);
	if (ret) {
		wl_err("%s apf_rsp err %d.\n", __func__, ret);
	}
exit:
	if (apf_rsp) {
		kfree(apf_rsp);
	}

	return ret;
}

int apf_force_disable(struct sprd_vif *vif, bool force_disable)
{
	struct apf_program_state *apf_st;
	struct apf_request apf_req = {0};
	struct apf_response apf_rsp = {0};
	u16 expect_rsp_len = sizeof(struct apf_response);
	int ret;

	if (!vif || !vif->priv || !vif->priv->apf_state) {
		wl_err("%s error params", __func__);
		return -EINVAL;
	}

	apf_st = vif->priv->apf_state;
	if (!is_apf_valid(&apf_st->apf_cap) ||
		(vif->mode != SPRD_MODE_STATION &&
		vif->mode != SPRD_MODE_STATION_SECOND)) {
		wl_err("%s mode %u cmd %u error %u-%u", __func__, vif->mode,
			apf_req.apf_hdr.apf_subcmd, apf_st->apf_cap.apf_version,
			apf_st->apf_cap.max_capa_apf_prog_len);
		return -ENOTSUPP;
	}

	apf_req.apf_hdr.apf_subcmd = WLAN_APF_FORCE_DISABLE;
	apf_req.apf_force_disable = force_disable;
	apf_req.apf_hdr.length = sizeof(apf_req.apf_force_disable);

	apf_rsp.apf_hdr.length = expect_rsp_len - sizeof(struct apf_cmd_header);
	ret = apf_subcmd_send_recv(vif, &apf_req, NULL, &apf_rsp);
	if (ret) {
		wl_err("%s apf_send err %d.\n", __func__, ret);
	}

	return ret;
}

int apf_force_disable_status(struct sprd_vif *vif, u8 *disable_status)
{
	struct apf_program_state *apf_st;
	struct apf_request apf_req = {0};
	struct apf_response *apf_rsp = NULL;
	u16 expect_rsp_len = sizeof(struct apf_response) + sizeof(u8);
	int ret;

	if (!vif || !vif->priv || !vif->priv->apf_state) {
		wl_err("%s error params", __func__);
		return -EINVAL;
	}

	apf_st = vif->priv->apf_state;
	if (!is_apf_valid(&apf_st->apf_cap) ||
		(vif->mode != SPRD_MODE_STATION &&
		vif->mode != SPRD_MODE_STATION_SECOND)) {
		wl_err("%s mode %u cmd %u error %u-%u", __func__, vif->mode,
			apf_req.apf_hdr.apf_subcmd, apf_st->apf_cap.apf_version,
			apf_st->apf_cap.max_capa_apf_prog_len);
		return -ENOTSUPP;
	}

	apf_req.apf_hdr.apf_subcmd = WLAN_GET_APF_FORCE_DIS_STATUS;

	apf_rsp = kzalloc(ALIGN(expect_rsp_len, APF_ALIGN_SIZE), GFP_KERNEL);
	if (!apf_rsp) {
		netdev_info(vif->ndev, "%s alloc failed", __func__);
		return -ENOMEM;
	}

	apf_rsp->apf_hdr.length = expect_rsp_len - sizeof(struct apf_cmd_header);
	ret = apf_subcmd_send_recv(vif, &apf_req, NULL, apf_rsp);
	if (ret) {
		wl_err("%s apf_send err %d.\n", __func__, ret);
	} else {
		*disable_status = *apf_rsp->rsp_data;
	}
	kfree(apf_rsp);

	return ret;
}

int apf_init(struct sprd_priv *priv)
{
	if (!priv->apf_state) {
		priv->apf_state = &g_apf_prog;
		mutex_init(&priv->apf_state->apf_lock);
		wl_debug("%s.\n", __func__);

		#if 0
		priv->apf_state->apf_cap.apf_version = APF_VERSION_4;
		priv->apf_state->apf_cap.max_capa_apf_prog_len = APF_MAX_PROG_SIZE;
		priv->apf_state->apf_program =
			kzalloc(priv->apf_state->apf_cap.max_capa_apf_prog_len, GFP_KERNEL);
		#endif
	}

	return 0;
}

int apf_deinit(struct sprd_priv *priv)
{
	if (priv->apf_state) {
		wl_debug("%s.\n", __func__);
		mutex_lock(&priv->apf_state->apf_lock);
		if (priv->apf_state->apf_program) {
			kfree(priv->apf_state->apf_program);
			priv->apf_state->apf_program = NULL;
		}
		mutex_unlock(&priv->apf_state->apf_lock);
		mutex_destroy(&priv->apf_state->apf_lock);
		priv->apf_state = NULL;
		memset(&g_apf_prog, 0, sizeof(g_apf_prog));
	}

	return 0;
}

