/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#include "dfs.h"
#include "cmdevt.h"
#include "common/delay_work.h"
#include "common/common.h"
#include "common/iface.h"

/* This is work queue function for channel switch handling.
 * This function takes care of updating new channel definitin to
 * bss config structure, restart AP and indicate channel switch success
 * to cfg80211.
 */
static void sc2355_dfs_chan_sw_work_queue(struct work_struct *work)
{
	struct sprd_vif *vif = container_of(work, struct sprd_vif,
					    dfs_chan_sw_work.work);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0))
		cfg80211_ch_switch_notify(vif->ndev, &vif->dfs_chandef, 0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		cfg80211_ch_switch_notify(vif->ndev, &vif->dfs_chandef, 0, 0);
#else
		cfg80211_ch_switch_notify(vif->ndev, &vif->dfs_chandef);
#endif
}

/*This is delayed work emits CAC finished event for cfg80211 if
 * CAC was started earlier.
 */
static void sc2355_dfs_cac_work_queue(struct work_struct *work)
{
	struct cfg80211_chan_def chandef;
	struct sprd_vif *vif =
		container_of(work, struct sprd_vif,
			     dfs_cac_work.work);

	chandef = vif->dfs_chandef;
	if (vif->wdev.cac_started) {
		wl_err("CAC timer finished; No radar detected\n");
		cfg80211_cac_event(vif->ndev, &chandef,
				   NL80211_RADAR_CAC_FINISHED,
				   GFP_KERNEL);
	}
}

int sc2355_init_dfs_master(struct sprd_vif *vif)
{
	vif->dfs_cac_workqueue =
		alloc_ordered_workqueue("SPRD_DFS_CAC", WQ_MEM_RECLAIM |
					WQ_HIGHPRI | WQ_CPU_INTENSIVE);
	if (!vif->dfs_cac_workqueue) {
		wl_err("%s SPRD_DFS_CAC create failed\n", __func__);
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&vif->dfs_cac_work, sc2355_dfs_cac_work_queue);

	vif->dfs_chan_sw_workqueue =
		alloc_ordered_workqueue("SPRD_DFS_CHSW", WQ_MEM_RECLAIM |
					WQ_HIGHPRI | WQ_CPU_INTENSIVE);
	if (!vif->dfs_chan_sw_workqueue) {
		wl_err("%s SPRD_DFS_CHSW create failed\n", __func__);
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&vif->dfs_chan_sw_work,
			  sc2355_dfs_chan_sw_work_queue);

	return 0;
}

void sc2355_deinit_dfs_master(struct sprd_vif *vif)
{
	if (vif->dfs_cac_workqueue) {
		flush_workqueue(vif->dfs_cac_workqueue);
		destroy_workqueue(vif->dfs_cac_workqueue);
		vif->dfs_cac_workqueue = NULL;
	}

	if (vif->dfs_chan_sw_workqueue) {
		flush_workqueue(vif->dfs_chan_sw_workqueue);
		destroy_workqueue(vif->dfs_chan_sw_workqueue);
		vif->dfs_chan_sw_workqueue = NULL;
	}
}

int sc2355_reset_beacon(struct sprd_priv *priv, struct sprd_vif *vif,
			const u8 *beacon, u16 len)
{
	struct sprd_msg *msg;

	msg = get_cmdbuf(priv, vif, len, CMD_RESET_BEACON);
	if (!msg)
		return -ENOMEM;
	memcpy(msg->data, beacon, len);

	return send_cmd_recv_rsp(priv, msg, NULL, NULL);
}

int sc2355_start_radar_detection(struct sprd_vif *vif,
				 struct cfg80211_chan_def *chandef,
				 u32 cac_time_ms)
{
	struct sprd_radar_params radar_params;

	wl_info("%s enter:\n", __func__);
	radar_params.chan_num = chandef->chan->hw_value;
	radar_params.chan_width = chandef->width;
	radar_params.cac_time_ms = cac_time_ms;

	memcpy(&vif->dfs_chandef, chandef, sizeof(vif->dfs_chandef));
	/*send radar detect CMD*/
	sc2355_send_dfs_cmd(vif, &radar_params, sizeof(radar_params));

	schedule_delayed_work(&vif->dfs_cac_work,
			      msecs_to_jiffies(cac_time_ms));

	return 0;
}

int sc2355_channel_switch(struct sprd_vif *vif,
			  struct cfg80211_csa_settings *params)
{
	struct ieee_types_header *chsw_ie;
	struct ieee80211_channel_sw_ie *channel_sw;
	struct cfg80211_beacon_data *beacon = &params->beacon_csa;
	int chsw_msec = 0;

	if (vif->wdev.cac_started)
		return -EBUSY;

	if (cfg80211_chandef_identical(&params->chandef, &vif->dfs_chandef))
		return -EINVAL;

	chsw_ie = (void *)cfg80211_find_ie(WLAN_EID_CHANNEL_SWITCH,
					   params->beacon_csa.tail,
					   params->beacon_csa.tail_len);

	if (!chsw_ie) {
		wl_err("Couldn't parse chan switch announcement IE\n");
		return -EINVAL;
	}

	channel_sw = (void *)(chsw_ie + 1);
	if (channel_sw->mode) {
		if (netif_carrier_ok(vif->ndev))
			netif_carrier_off(vif->ndev);
		/*stop tx Q*/
		if (!netif_queue_stopped(vif->ndev))
			netif_stop_queue(vif->ndev);
	}

	if (beacon->tail_len)
		sc2355_reset_beacon(vif->priv, vif,
				    beacon->tail, beacon->tail_len);

	/*set mgmt ies*/
	if (sprd_cfg80211_set_beacon_ies(vif, beacon)) {
		wl_err("set beacon IE failure\n");
		return -EINVAL;
	}

	memcpy(&vif->dfs_chandef, &params->chandef, sizeof(vif->dfs_chandef));
	chsw_msec = max(channel_sw->count * vif->priv->beacon_period, 500);
	schedule_delayed_work(&vif->dfs_chan_sw_work,
			      msecs_to_jiffies(chsw_msec));
	return 0;
}

static void sc2355_stop_radar_detection(struct sprd_vif *vif,
					struct cfg80211_chan_def *chandef)
{
	struct sprd_work *misc_work;
	struct sprd_radar_params radar_params;

	memset(&radar_params, 0, sizeof(struct sprd_radar_params));
	radar_params.chan_num = chandef->chan->hw_value;
	radar_params.chan_width = chandef->width;
	radar_params.cac_time_ms = 0;

	/*send stop radar detection cmd*/
	misc_work = sprd_alloc_work(sizeof(radar_params));
	if (!misc_work) {
		netdev_err(vif->ndev, "%s out of memory\n", __func__);
		return;
	}
	misc_work->vif = vif;
	misc_work->id = SPRD_WORK_DFS;

	memcpy(misc_work->data, &radar_params, sizeof(radar_params));

	sprd_queue_work(vif->priv, misc_work);
}

/* This function is to abort ongoing CAC upon stopping AP operations
 * or during unload.
 */
void sc2355_abort_cac(struct sprd_vif *vif)
{
	if (vif->wdev.cac_started) {
		sc2355_stop_radar_detection(vif, &vif->dfs_chandef);
		cancel_delayed_work_sync(&vif->dfs_cac_work);
		cfg80211_cac_event(vif->ndev, &vif->dfs_chandef,
				   NL80211_RADAR_CAC_ABORTED, GFP_KERNEL);
	}
}

/* Handler for radar detected event from FW.*/
int sc2355_dfs_handle_radar_detected(struct sprd_vif *vif,
				     u8 *data, u16 len)
{
	struct sprd_radar_event *rdr_event;
	rdr_event = (struct sprd_radar_event *)data;

	wl_debug("radar detected; indicating kernel\n");
	sc2355_stop_radar_detection(vif, &vif->dfs_chandef);
	cfg80211_radar_event(vif->priv->wiphy, &vif->dfs_chandef,
			     GFP_KERNEL);
	cfg80211_cac_event(vif->ndev, &vif->dfs_chandef,
			   NL80211_RADAR_CAC_FINISHED, GFP_KERNEL);
	/*print radar detect reg & type*/
	wl_info("regdomain:%d,radar detection type:%d\n",
		rdr_event->reg_domain, rdr_event->det_type);
	return 0;
}

void sc2355_send_dfs_cmd(struct sprd_vif *vif, void *data, int len)
{
	struct sprd_msg *msg;

	msg = get_cmdbuf(vif->priv, vif, len, CMD_RADAR_DETECT);
	if (!msg)
		return;
	memcpy(msg->data, data, len);
	send_cmd_recv_rsp(vif->priv, msg, NULL, NULL);
}

