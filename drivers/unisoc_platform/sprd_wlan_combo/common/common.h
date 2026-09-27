/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef __COMMON_H__
#define __COMMON_H__

#include <linux/atomic.h>
#include <linux/dcache.h>
#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/ieee80211.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/udp.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <linux/wireless.h>
#include <linux/workqueue.h>
#include <net/addrconf.h>
#include <net/cfg80211.h>
#include <net/if_inet6.h>

#include "cfg80211.h"
#include "cmd.h"
#include "debug.h"
#include "hif.h"
#include "iface.h"
#include "tcp_ack.h"
#include "vendor.h"
#include "npi.h"
#include "apf.h"
#include "chr.h"

#define SPRD_DRIVER_VERSION		"v1.0"

#define SPRD_UNALIAGN			1
#ifdef SPRD_UNALIAGN
#define SPRD_PUT_LE16(val, addr)	put_unaligned_le16(val, &(addr))
#define SPRD_PUT_LE32(val, addr)	put_unaligned_le32(val, &(addr))
#define SPRD_GET_LE16(addr)		get_unaligned_le16(&(addr))
#define SPRD_GET_LE32(addr)		get_unaligned_le32(&(addr))
#define SPRD_GET_LE64(addr)		get_unaligned_le64(&(addr))
#else
#define SPRD_PUT_LE16(val, addr)	cpu_to_le16(val, addr)
#define SPRD_PUT_LE32(val, addr)	cpu_to_le32(val, addr)
#define SPRD_GET_LE16(addr)		le16_to_cpu(addr)
#define SPRD_GET_LE32(addr)		le32_to_cpu(addr)
#endif

/* the max length between data_head and net data */
#define SPRD_SKB_HEAD_RESERV_LEN	16
#define SPRD_COUNTRY_CODE_LEN		2

#define SPRD_UPDATE			"000e"
#define SPRD_RESERVE			""
#define MAIN_DRV_VERSION		(1)
#define MAX_API				(256)
#define DEFAULT_COMPAT			(255)

#define VERSION_1			(1)
#define VERSION_2			(2)
#define VERSION_3			(3)
#define VERSION_4			(4)

#define MAC_LEN				(24)
#define ADDR1_OFFSET			(4)
#define ADDR2_OFFSET			(10)
#define ACTION_TYPE			(13)
#define ACTION_SUBTYPE_OFFSET		(30)
#define PUB_ACTION			(0x4)
#define P2P_ACTION			(0x7f)

#define PRINT_BUF_LEN			BIT(10)

#define TX_WITH_CREDIT			(0)
#define TX_NO_CREDIT			(1)
#define OTT_NO_SUPT			(0)
#define OTT_SUPT			(1)

#define WIFI_DRIVER_CONFIG_FILE	"wcn_wifi_driver.conf"
#define GET_STA_AP_COEX(priv)	((priv)->wifi_drv_conf.config_sta_ap_coex)
/*128k*/
#define FW_MAX_SIZE	(128 * 1024)

struct sprd_cmd;

struct sprd_ver {
	char kernel_ver[__NEW_UTS_LEN + 1];
	char drv_ver[8];
	char update[8];
	char reserve[8];
};

struct sprd_api_version_t {
	unsigned char cmd_id;
	unsigned char drv_version;
	unsigned char fw_version;
	char *name;
};

/* struct used for priv to store all info */
struct sync_api_verion_t {
	unsigned int compat;
	unsigned int main_drv;
	unsigned int main_fw;
	struct sprd_api_version_t *api_array;
};

struct sprd_mc_filter {
	bool mc_change;
	u8 subtype;
	u8 mac_num;
	u8 mac_addr[];
};

struct android_wifi_priv_cmd {
	char *buf;
	int used_len;
	int total_len;
};

struct sprd_channel_list {
	int num_channels;
	int channels[SPRD_TOTAL_CHAN_NR];
};

struct wmm_ac_params {
	u8 aci_aifsn;		/* AIFSN, ACM, ACI */
	u8 cw;			/* ECWmin, ECWmax (CW = 2^ECW - 1) */
	u16 txop_limit;
};

struct sprd_roam_capa {
	u32 max_blacklist_size;
	u32 max_whitelist_size;
};

struct sprd_rtt_recv_result {
	u8 peer_num;
	struct rtt_wifi_hal_result *peer_rtt_result[10];
	struct rtt_dot11_rm_ie *ele1[10];
	struct rtt_dot11_rm_ie *ele2[10];
};

/* private data related to FTM. Part of the priv structure */
struct sprd_rtt_priv {
	/* protect sprd_rtt_priv */
	struct mutex lock;
	u8 session_started;
	u64 session_cookie;
	struct rtt_peer_meas_res *ftm_res;
	u8 has_ftm_res;
	u32 max_ftm_meas;

	/* standalone AOA measurement */
	u8 aoa_started;
	u8 aoa_peer_mac_addr[ETH_ALEN];
	u32 aoa_type;
	struct timer_list aoa_timer;
	struct work_struct aoa_timeout_work;
};

struct sprd_wmmac_params {
	struct wmm_ac_params ac[4];
	struct timer_list wmmac_edcaf_timer;
};

struct sprd_priv;
struct sprd_chip_ops;
struct sprd_chip {
	struct sprd_priv *priv;
	struct sprd_chip_ops *ops;
};

struct sprd_wlan_dt_config {
	bool enable_n79;
	bool enable_chr;
};

struct wifi_driver_config {
	/*0:dynamic_p2p,1:static_p2p */
	u8 config_static_p2p;
	u8 config_enable_n79;
	u8 config_enable_chr;
	u8 config_disable_assert;
	/*1 :support coexistence of  sta and ap*/
	u8 config_sta_ap_coex;
} __packed;

struct sdio_phy_param {
	u16 set_mask;
	u16 calibration_bypass;
	u8 sar_power_config[9];
	u8 cca_threshold_config[4];
} __packed;

struct sipc_phy_param {
    u16 set_mask;
    u16 calibration_bypass;
    u8 desense_rssi_threshold;
    u8 sar_power_config[9];
    u8 desense_code[2];
    u8 cca_threshold_config[4];
    u8 sar_scene_limit;
    u8 ar_auto_power[5];
} __packed;

struct pcie_phy_param {
    u16 set_mask;
    u16 calibration_bypass;
    u8 rf_chain_2g;
    u8 rf_chain_5g;
    u8 desense_rssi_threshold;
    u8 cca_threshold_config[4];
} __packed;

struct phy_ini_param {
	u8 phy_ini_flag;
	/* use union to be compatible with different chips */
	union {
		struct sdio_phy_param sdio_param;
		struct sipc_phy_param sipc_param;
		struct pcie_phy_param pcie_param;
	} __packed;
} __packed;

struct sdio_mac_param {
	u16 set_mask;
	u8 wmm_boost;
	u8 op_mode;
	u8 ps_control;
	u8 mixed_roaming;
	u8 roaming_config[3];
	u8 ce_adapt;
	u16 long_11b_pream;
} __packed;

struct sipc_mac_param {
    u16 set_mask;
    u8 wmm_boost;
    u8 ps_control;
    u8 wfd_ampdu;
    u8 self_cts;
    u8 sleep_filter[4];
    u8 edca_config[5];
    u8 roaming_config[3];
} __packed;

struct section_fourteen_gmp {
	u32 type_mask; //bit0:0x20 bit1:0x21
	u8 switch_config[2];
	u8 sap_ext_cap_ie;
} __packed;

struct sipc_general_mac_param {
	u32 section_mask;
	struct section_fourteen_gmp sec_fourteen_gmp;
} __packed;

struct pcie_mac_param {
    u16 set_mask;
    u8 wmm_boost;
    u8 op_mode;
    u8 ps_control;
    u8 wfd_ampdu;
    u8 sleep_filter[4];
    u8 roaming_config[3];
} __packed;

struct mac_ini_param {
	u8 mac_ini_flag;
	/* use union to be compatible with different chips */
	union {
		struct sdio_mac_param sdio_param;
		struct sipc_mac_param sipc_param;
		struct pcie_mac_param pcie_param;
	} __packed;
	struct sipc_general_mac_param sipc_gmp;
} __packed;

struct sprd_priv {
	struct wiphy *wiphy;
	struct sprd_hif hif;
	struct sprd_chip chip;

	/* virtual interface list */
	spinlock_t list_lock;
	struct list_head vif_list;
	unsigned long active_modes_flags;

	struct sprd_cmd cmd;

	/* necessary info from fw */
	u32 chip_model;
	u32 chip_ver;
	u32 fw_ver;
	u32 fw_std;
	u32 fw_capa;
	u8 max_ap_assoc_sta;
	u8 max_acl_mac_addrs;
	u8 max_mc_mac_addrs;
	u8 wnm_ft_support;

	/* scan */
	spinlock_t scan_lock;
	struct sprd_vif *scan_vif;
	struct cfg80211_scan_request *scan_request;
	struct timer_list scan_timer;

	/* schedule scan */
	spinlock_t sched_scan_lock;
	struct sprd_vif *sched_scan_vif;
	struct cfg80211_sched_scan_request *sched_scan_request;

	/* gscan */
	u32 gscan_buckets_num;
	struct sprd_gscan_cached_results *gscan_res;
	int gscan_req_id;
	struct sprd_llstat_radio pre_radio;
	/* default MAC addr */
	unsigned char default_mac[ETH_ALEN];

	/* delayed work */
	spinlock_t work_lock;
	struct work_struct work;
	struct list_head work_list;
	struct workqueue_struct *common_workq;
#ifdef DRV_RESET_SELF
	/* self_reset workq*/
	struct work_struct reset_work;
	struct workqueue_struct *reset_workq;
#endif
	struct delayed_work reset_delay_work;

	struct dentry *debugfs;
	struct sprd_channel_list ch_2g4_info;
	struct sprd_channel_list ch_5g_without_dfs_info;
	struct sprd_channel_list ch_5g_dfs_info;

	__le32 extend_feature;
	u8 tx_mgmt_status;

	int is_screen_off;
	int is_suspending;

	/* SC2332 */
	u8 max_sched_scan_plans;
	u8 max_sched_scan_interval;
	u8 max_sched_scan_iterations;
	u8 random_mac_support;
	u8 scanning_flag;
	struct semaphore scanning_sem;

	/* SC2355 */
	struct sprd_ver wl_ver;
	struct sprd_debug debug;

	u8 mac_addr[ETH_ALEN];
	u8 mac_addr_sta_second[ETH_ALEN];
	u32 wiphy_sec2_flag;
	struct wiphy_sec2_t wiphy_sec2;
	struct sync_api_verion_t sync_api;
	u16 beacon_period;

	struct sprd_gscan_hotlist_results *hotlist_res;

	struct sprd_significant_change_result *significant_res;
	struct sprd_roam_capa roam_capa;

	/* tcp ack management */
	struct sprd_tcp_ack_manage ack_m;

	/* FTM */
	struct sprd_rtt_recv_result rtt_results;
	struct sprd_rtt_priv ftm;

	/* wmmac */
	struct sprd_wmmac_params wmmac;
	/* with credit or without */
	unsigned char credit_capa;

	/* OTT support */
	unsigned char ott_supt;
	unsigned int rand_mac_flag;

	/* power backoff flag */
	atomic_t power_back_off;

	/* sniffer monitor mode */
	atomic_t monitor_mode;
	unsigned long monitor_data_cnt;
	unsigned long monitor_mgmt_cnt;
	volatile bool probe_done;

	struct apf_program_state *apf_state;

	/*dt config */
	struct sprd_wlan_dt_config dt_configs;

	/* npi flag */
	unsigned char npi_sta_wfa;
	struct wifi_driver_config wifi_drv_conf;

	/*mac/phy ini parameters*/
	struct phy_ini_param *phy_param;
	struct mac_ini_param *mac_param;
	u8 roam_pmf;
};

extern unsigned int wfa_cap;
extern struct sprd_wlan_adap_param adap_info;

void sprd_parse_wifi_driver_config(struct sprd_priv *priv);
int sprd_load_fw(struct sprd_priv *priv, const char *path, char **buffer, size_t *buf_size);

static inline void sprd_version_init(struct sprd_ver *ver)
{
	memcpy(ver->kernel_ver, utsname()->release, strlen(utsname()->release));
	memcpy(ver->drv_ver, SPRD_DRIVER_VERSION, strlen(SPRD_DRIVER_VERSION));
	memcpy(ver->update, SPRD_UPDATE, strlen(SPRD_UPDATE));
	memcpy(ver->reserve, SPRD_RESERVE, strlen(SPRD_RESERVE));
}

static inline bool sprd_is_sta_ap_coexist(struct sprd_priv *priv)
{
	if (test_bit(SPRD_MODE_STATION, &priv->active_modes_flags) &&
		test_bit(SPRD_MODE_AP, &priv->active_modes_flags))
		return true;
	else
		return false;
}
#endif
