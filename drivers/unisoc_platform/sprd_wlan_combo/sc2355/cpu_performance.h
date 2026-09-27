/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef __CPU_PERFORMANCE_H__
#define __CPU_PERFORMANCE_H__

#include <linux/pm_qos.h>

#include "tx.h"

#define DISABLE_PD_THRESHOLD (16 * 0x100000)  //128Mbit/s  or 16Mbyte/s
#define SET_UCLAMP_THRESHOLD (16 * 0x100000)  //128Mbit/s  or 16Mbyte/s
#define SET_PS_THRESHOLD (30 * 0x100000)  //04Mbit/s  or 13Mbyte/s
#define TX_LOW_THRESHOLD (7 * 0x100000)

struct throughput_sta {
	unsigned long tx_bytes;
	unsigned long tx_last_time;
	unsigned long rx_bytes;
	unsigned long rx_last_time;
	unsigned long throughput_rx;
	unsigned long throughput_tx;
	bool disable_pd_flag;
	bool uclamp_set_flag;
	int tx_cpu_change_times;
	int rx_cpu_change_times;
	struct  pm_qos_request pm_qos_request_idle;
};

void sc2355_tp_static_init(void);
void sc2355_tp_static_deinit(void);
void sc2355_rx_tp_statistic(unsigned int len);
void sc2355_tx_tp_statistic(unsigned int len);
void sc2355_tp_ctl_core_pd(void);
void sc2355_reset_cpu_prf_param(struct sprd_hif *hif);
void sc2355_tp_ctl_uclamp(struct sprd_hif *hif);
void sc2355_tp_modify_cpu_usage(struct task_struct *thread,
				const char *driect, bool is_sta_ap_coex);
void sc2355_set_cpu_param(const char *param);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
void sc2355_set_wcn_thread_uclamp(void);
#endif
extern int scene_dfs_request(char *scenario);
extern int scene_exit(char *scenario);
#endif
