/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef _DFS_H
#define _DFS_H

#include <linux/ieee80211.h>
#include <uapi/linux/if_arp.h>
#include <net/mac80211.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include "common/iface.h"

struct ieee_types_header {
	u8 element_id;
	u8 len;
} __packed;

struct sprd_radar_params {
	u8 chan_num;
	u8 chan_width;
	__le32 cac_time_ms;
} __packed;

struct sprd_radar_event {
	u8 reg_domain; /*1=fcc, 2=etsi, 3=mic*/
	u8 det_type; /*0=none, 1=pw(chirp), 2=pri(radar) */
} __packed;

int sc2355_init_dfs_master(struct sprd_vif *vif);
void sc2355_deinit_dfs_master(struct sprd_vif *vif);
int sc2355_start_radar_detection(struct sprd_vif *vif,
				 struct cfg80211_chan_def *chandef,
				 u32 cac_time_ms);
int sc2355_channel_switch(struct sprd_vif *vif,
			  struct cfg80211_csa_settings *params);
void sc2355_abort_cac(struct sprd_vif *vif);
int sc2355_dfs_handle_radar_detected(struct sprd_vif *vif,
				     u8 *data, u16 len);
void sc2355_send_dfs_cmd(struct sprd_vif *vif, void *data, int len);
int sc2355_reset_beacon(struct sprd_priv *priv, struct sprd_vif *vif,
			const u8 *beacon, u16 len);
#endif
