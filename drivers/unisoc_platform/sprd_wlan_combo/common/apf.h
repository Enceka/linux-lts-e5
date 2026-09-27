/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef __APF_H__
#define __APF_H__

#include <linux/types.h>

#define APF_ALIGN_SIZE      (4)
#define APF_VERSION_4       (4)
#define APF_MAX_PROG_SIZE   (2048)
#define PACKET_FILTER_ID    (0)

/* enum packet_filter_sub_cmd - Packet filter sub commands */
enum packet_filter_subcmd {
	WLAN_SET_PACKET_FILTER = 1,
	WLAN_GET_PACKET_FILTER = 2,
	WLAN_WRITE_PACKET_FILTER = 3,
	WLAN_READ_PACKET_FILTER = 4,
	WLAN_ENABLE_PACKET_FILTER = 5,
	WLAN_DISABLE_PACKET_FILTER = 6,

	WLAN_APF_FORCE_DISABLE = 0x20,
	WLAN_GET_APF_FORCE_DIS_STATUS = 0x21,
};

/* enum qca_wlan_vendor_attr_packet_filter */
enum vendor_attr_packet_filter {
	VENDOR_ATTR_PACKET_FILTER_INVALID = 0,
	VENDOR_ATTR_PACKET_FILTER_SUB_CMD,
	VENDOR_ATTR_PACKET_FILTER_VERSION,
	VENDOR_ATTR_PACKET_FILTER_ID,
	VENDOR_ATTR_PACKET_FILTER_SIZE,
	VENDOR_ATTR_PACKET_FILTER_CURRENT_OFFSET,
	VENDOR_ATTR_PACKET_FILTER_PROGRAM,
	VENDOR_ATTR_PACKET_FILTER_PROG_LENGTH = 7,

	/* keep last */
	VENDOR_ATTR_PACKET_FILTER_AFTER_LAST,
	VENDOR_ATTR_PACKET_FILTER_MAX =
	VENDOR_ATTR_PACKET_FILTER_AFTER_LAST - 1,
};

struct apf_capa {
	u8 apf_version;
	u16 max_capa_apf_prog_len;
} __packed;

struct apf_cmd_header {
	u16 apf_subcmd;
	u16 length; // length of data
	u8 data[];
} __packed;

struct apf_request {
	struct apf_cmd_header apf_hdr;
	union {
		struct {
			u16 apf_currt_offset;
			u16 apf_offset_slice_size;
			u16 apf_trans_size;
			u16 apf_prog_len;
			u8 apf_prog[]; // apf_offset_slice_size of apf_prog.
		};
		u8 apf_force_disable;
	};
} __packed;

struct apf_response {
	struct apf_cmd_header apf_hdr;
	u8 cmd_ret_value;
	u8 rsp_data[]; // apf_capa, program_slice_data or apf_force_disable_status
} __packed;

struct apf_program_state {
	struct apf_capa apf_cap;

	u8 apf_cmd_id;
	int (*apf_req_send_rcv)(struct sprd_vif *vif,
			struct apf_request *apf_req, void *src_slice_prog,
			struct apf_response *apf_rsp, u16 *r_len);

	struct mutex apf_lock;
	u32 apf_program_len;
	u32 afp_checksum;
	u64 afp_md5;
	u8 *apf_program; // current program
};

int apf_init(struct sprd_priv *priv);
int apf_deinit(struct sprd_priv *priv);
int apf_force_disable(struct sprd_vif *vif, bool force_disable);
int apf_force_disable_status(struct sprd_vif *vif, u8 *disable_status);

int validate_apf_program_attr(const struct nlattr *attr,
			    struct netlink_ext_ack *extack);
int vendor_apf_packet_filter(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    const void *data, int len);

static const struct
nla_policy apf_vendor_attr_policy[VENDOR_ATTR_PACKET_FILTER_MAX + 1] = {
	[VENDOR_ATTR_PACKET_FILTER_SUB_CMD] = { .type = NLA_U32 },
	[VENDOR_ATTR_PACKET_FILTER_VERSION] = { .type = NLA_U32 },
	[VENDOR_ATTR_PACKET_FILTER_ID] = { .type = NLA_U32 },

	[VENDOR_ATTR_PACKET_FILTER_SIZE] = NLA_POLICY_RANGE(NLA_U32, 0, S16_MAX),
	[VENDOR_ATTR_PACKET_FILTER_CURRENT_OFFSET] = NLA_POLICY_RANGE(NLA_U32, 0, S16_MAX),
	[VENDOR_ATTR_PACKET_FILTER_PROG_LENGTH] = NLA_POLICY_RANGE(NLA_U32, 0, S16_MAX),

	[VENDOR_ATTR_PACKET_FILTER_PROGRAM] =
		NLA_POLICY_VALIDATE_FN(NLA_BINARY, validate_apf_program_attr),

//	[VENDOR_ATTR_PACKET_FILTER_PROGRAM] = { .type = NLA_BINARY, .len = U8_MAX * sizeof(u32) },
};

#endif
