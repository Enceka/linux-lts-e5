/*
  * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
  * SPDX-License-Identifier: GPL-2.0-only
  */
#include <uapi/linux/sched/types.h>
#include <linux/version.h>
#include <linux/cpumask.h>
#include <linux/soc/sprd/unisoc_compat.h>

#include "cpu_performance.h"
#include "debug.h"

#define UCLAMP_MAX_DEFAULT    1024
#define UCLAMP_MAX_POWERSAVE  800
#define MAX_THREADS  4

static struct throughput_sta throughput_static;
static struct  cpumask tx_small_cpus, rx_big_cpus,s_all_cpus;
static int tx_sched_util_max, rx_sched_util_max;
static unsigned int tx_ps_threshold, rx_ps_threshold;
static unsigned int  tx_sta_ap_ps_th;
void sc2355_tp_set_cpu(u32 bitmaps, struct cpumask *cpus){
	int i,j;
	cpumask_clear(cpus);
	for(i =0; i< 8; i++){
		if(bitmaps & (1 << i))cpumask_set_cpu(i, cpus);
	}
        for_each_cpu(j, cpus){
            wl_err("set_cpu =%d", j);
	}
	wl_err("set_cpu max =0x%x", bitmaps);
}

void sc2355_set_cpu_param(const char * param)
{
   int cpu_param;
   cpu_param = *(int *)param;
   sc2355_tp_set_cpu((u32)cpu_param, &tx_small_cpus);
   cpu_param = *(int *)(param + 4);
   tx_sched_util_max = cpu_param;
   cpu_param = *(int *)(param + 8);
   tx_ps_threshold = cpu_param;
   cpu_param = *(int *)(param + 12);
   sc2355_tp_set_cpu((u32)cpu_param, &rx_big_cpus);
   cpu_param = *(int *)(param + 16);
   rx_sched_util_max = cpu_param;
   cpu_param = *(int *)(param + 20);
   rx_ps_threshold = cpu_param;
   throughput_static.tx_cpu_change_times = 0;
   throughput_static.rx_cpu_change_times = 0;

   wl_info("tx sched: %d, tx threshold: %d, rx sched: %d, rx threshold :%d\n",
                tx_sched_util_max, tx_ps_threshold,
                rx_sched_util_max, rx_ps_threshold);

}

void sc2355_tp_static_init(void)
{
	throughput_static.tx_bytes = 0;
	throughput_static.tx_last_time = jiffies;
	throughput_static.rx_bytes = 0;
	throughput_static.rx_last_time = jiffies;
	throughput_static.disable_pd_flag = false;
	throughput_static.uclamp_set_flag = false;
	throughput_static.tx_cpu_change_times = 0;
	throughput_static.rx_cpu_change_times = 0;
	throughput_static.throughput_tx = 0;
	throughput_static.throughput_rx = 0;
	cpumask_setall(&s_all_cpus);
	cpumask_clear(&rx_big_cpus);
	cpumask_clear(&tx_small_cpus);
        cpumask_set_cpu(6, &rx_big_cpus);
        cpumask_set_cpu(7, &rx_big_cpus);
        cpumask_set_cpu(0, &tx_small_cpus);
        cpumask_set_cpu(1, &tx_small_cpus);
        cpumask_set_cpu(2, &tx_small_cpus);
        cpumask_set_cpu(3, &tx_small_cpus);
        cpumask_set_cpu(4, &tx_small_cpus);
        cpumask_set_cpu(5, &tx_small_cpus);
	rx_sched_util_max = UCLAMP_MAX_POWERSAVE;
	tx_sched_util_max = 50;
	tx_ps_threshold = 30;
	rx_ps_threshold = 30;
	tx_sta_ap_ps_th = 10;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	cpu_latency_qos_add_request(&throughput_static.pm_qos_request_idle,
				    PM_QOS_CPU_LATENCY_DEFAULT_VALUE);
#else
	pm_qos_add_request(&throughput_static.pm_qos_request_idle,
			   PM_QOS_CPU_DMA_LATENCY, PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE);
#endif
}

void sc2355_tp_static_deinit(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	cpu_latency_qos_remove_request(&throughput_static.pm_qos_request_idle);
#else
	pm_qos_remove_request(&throughput_static.pm_qos_request_idle);
#endif
}

void sc2355_tp_ctl_core_pd(void)
{
	if ((throughput_static.throughput_tx >= DISABLE_PD_THRESHOLD) ||
		(throughput_static.throughput_rx >= DISABLE_PD_THRESHOLD)) {
		if (!throughput_static.disable_pd_flag)	{
			throughput_static.disable_pd_flag = true;
			// forbid core powerdown
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
			cpu_latency_qos_update_request(&throughput_static.pm_qos_request_idle, 100);
#else
			pm_qos_update_request(&throughput_static.pm_qos_request_idle, 100);
#endif
		}
	} else {
		if (throughput_static.disable_pd_flag) {
			throughput_static.disable_pd_flag = false;
			//allow core powerdown
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
			cpu_latency_qos_update_request(&throughput_static.pm_qos_request_idle,
						       PM_QOS_CPU_LATENCY_DEFAULT_VALUE);
#else
			pm_qos_update_request(&throughput_static.pm_qos_request_idle,
					      PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE);
#endif
		}
	}
}

void sc2355_rx_tp_statistic(unsigned int len)
{
	throughput_static.rx_bytes += len;
	if (time_after(jiffies, throughput_static.rx_last_time +  msecs_to_jiffies(1000))) {
		throughput_static.rx_last_time = jiffies;
		throughput_static.throughput_rx = throughput_static.rx_bytes;
		throughput_static.rx_bytes = 0;
	}
}

void sc2355_tx_tp_statistic(unsigned int len)
{
	throughput_static.tx_bytes += len;
	if (time_after(jiffies, throughput_static.tx_last_time +  msecs_to_jiffies(1000))) {
		throughput_static.tx_last_time = jiffies;
		throughput_static.throughput_tx = throughput_static.tx_bytes;
		throughput_static.tx_bytes = 0;
	}
}

//set uclamp params for bug 1959864
static int sc2355_set_thread_uclamp(struct task_struct *thread, int sched_util_min)
{
	struct sched_attr attr = {};
	int ret = 0;

	if (!thread) {
		wl_err("%s: failed to set sched attr point thread null\n", __func__);
		return -1;
	}
	attr.sched_policy = thread->policy;
	if (thread->sched_reset_on_fork)
		attr.sched_flags |= SCHED_FLAG_RESET_ON_FORK;
	attr.sched_flags |= (SCHED_FLAG_KEEP_ALL | SCHED_FLAG_UTIL_CLAMP_MIN);
	attr.sched_util_min = sched_util_min;
	ret = sched_setattr(thread, &attr);

	return ret;
}

/* reset pd and uclamp parameters */
void sc2355_reset_cpu_prf_param(struct sprd_hif *hif)
{
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;

	if (throughput_static.disable_pd_flag) {
		throughput_static.disable_pd_flag = false;
		//allow core powerdown
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		cpu_latency_qos_update_request(&throughput_static.pm_qos_request_idle,
					      PM_QOS_CPU_LATENCY_DEFAULT_VALUE);
#else
		pm_qos_update_request(&throughput_static.pm_qos_request_idle,
					      PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE);
#endif
	}

	if (throughput_static.uclamp_set_flag) {
		//reset thread uclamp param
		sc2355_set_thread_uclamp(tx_mgmt->tx_thread, 0);
		throughput_static.uclamp_set_flag = false;
	}

	throughput_static.tx_bytes = 0;
	throughput_static.rx_bytes = 0;
	throughput_static.throughput_tx = 0;
	throughput_static.throughput_rx = 0;
}

void sc2355_tp_ctl_uclamp(struct sprd_hif *hif)
{
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;

	if (!throughput_static.uclamp_set_flag &&
		(throughput_static.throughput_tx >= SET_UCLAMP_THRESHOLD ||
		throughput_static.throughput_rx >= SET_UCLAMP_THRESHOLD)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		sc2355_set_thread_uclamp(tx_mgmt->tx_thread, 600);
#else
		sc2355_set_thread_uclamp(tx_mgmt->tx_thread, 400);
#endif
		throughput_static.uclamp_set_flag = true;
	} else if (throughput_static.uclamp_set_flag &&
			(throughput_static.throughput_tx < SET_UCLAMP_THRESHOLD &&
			throughput_static.throughput_rx < SET_UCLAMP_THRESHOLD)) {
		sc2355_set_thread_uclamp(tx_mgmt->tx_thread, 0);
		throughput_static.uclamp_set_flag = false;
	}
}

static inline bool sc2355_tp_is_low(bool is_sta_ap_coex)
{
	return ((!is_sta_ap_coex && throughput_static.throughput_tx <= tx_ps_threshold * 0x100000) ||
		(is_sta_ap_coex && throughput_static.throughput_tx <= tx_sta_ap_ps_th * 0x100000));
}

static inline bool sc2355_tp_is_high(bool is_sta_ap_coex)
{
	return ((!is_sta_ap_coex && throughput_static.throughput_tx > tx_ps_threshold * 0x100000) ||
		(is_sta_ap_coex && throughput_static.throughput_tx > tx_sta_ap_ps_th * 0x100000));
}

inline void
sc2355_tp_modify_cpu_usage(struct task_struct *thread, const char *direct, bool is_sta_ap_coex)
{
#ifdef CONFIG_UCLAMP_TASK
        if (!strncmp(direct, "RX", 2)) {
		if(throughput_static.rx_cpu_change_times < MAX_THREADS
			&& throughput_static.throughput_rx <= rx_ps_threshold * 0x100000){
		/*enter ps mode*/
			if(thread && (thread->uclamp[UCLAMP_MAX].value != rx_sched_util_max
				|| !cpumask_equal(&thread->cpus_mask, &rx_big_cpus))){
				struct sched_attr attr = {};
				set_cpus_allowed_ptr(thread, &rx_big_cpus);
				attr.sched_util_max = rx_sched_util_max;
				attr.sched_flags = SCHED_FLAG_UTIL_CLAMP_MAX;
				sched_setattr(thread, &attr);
				throughput_static.rx_cpu_change_times ++;
				wl_err("modify_cpu_usage attr.sched_util_max %d",attr.sched_util_max);
			}
		} else if (throughput_static.rx_cpu_change_times > 0
		 && throughput_static.throughput_rx > rx_ps_threshold * 0x100000) {
			/*restore uclamp and cpumask*/
			if(thread && (thread->uclamp[UCLAMP_MAX].value != UCLAMP_MAX_DEFAULT
				||!cpumask_equal(&thread->cpus_mask, &s_all_cpus))){
				struct sched_attr attr = {};
				throughput_static.rx_cpu_change_times--;
				set_cpus_allowed_ptr(thread, &s_all_cpus);
				attr.sched_util_max = UCLAMP_MAX_DEFAULT;
				attr.sched_flags = SCHED_FLAG_UTIL_CLAMP_MAX;
				sched_setattr(thread, &attr);
				wl_err("reset cpu_usage to default .");
			}
		}
	} else {
		if(throughput_static.tx_cpu_change_times < MAX_THREADS
			&& sc2355_tp_is_low(is_sta_ap_coex)){
		/*enter ps mode*/
			if(thread && (thread->uclamp[UCLAMP_MAX].value != tx_sched_util_max
				|| !cpumask_equal(&thread->cpus_mask, &tx_small_cpus))){
				struct sched_attr attr = {};
				set_cpus_allowed_ptr(thread, &tx_small_cpus);
				attr.sched_util_max = tx_sched_util_max;
				attr.sched_flags = SCHED_FLAG_UTIL_CLAMP_MAX;
				sched_setattr(thread, &attr);
				throughput_static.tx_cpu_change_times ++;
				wl_err("modify_cpu_usage attr.sched_util_max %d",attr.sched_util_max);
			}
		   } else if (throughput_static.tx_cpu_change_times > 0
			&& sc2355_tp_is_high(is_sta_ap_coex)) {
			/*restore uclamp and cpumask*/
			if(thread && (thread->uclamp[UCLAMP_MAX].value != UCLAMP_MAX_DEFAULT
				||!cpumask_equal(&thread->cpus_mask, &s_all_cpus))){
				struct sched_attr attr = {};
				throughput_static.tx_cpu_change_times--;
				set_cpus_allowed_ptr(thread, &s_all_cpus);
				attr.sched_util_max = UCLAMP_MAX_DEFAULT;
				attr.sched_flags = SCHED_FLAG_UTIL_CLAMP_MAX;
				sched_setattr(thread, &attr);
				wl_err("reset cpu_usage to default .");
			}

		}
	}
#endif
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
extern int wcn_thread_setattr(unsigned dir, struct sched_attr *attr);
static struct sched_attr attr;
void sc2355_set_wcn_thread_uclamp(void)
{
	attr.sched_flags |= (SCHED_FLAG_KEEP_ALL | SCHED_FLAG_UTIL_CLAMP_MIN);
	attr.sched_policy = SCHED_NORMAL;
	if (attr.sched_util_min != 400 &&
		throughput_static.throughput_rx >= SET_UCLAMP_THRESHOLD) {
		attr.sched_util_min = 400;
		wcn_thread_setattr(0, &attr);
	/*need reset sdiohal_rx_thread util to 0*/
	} else if (attr.sched_util_min &&
		throughput_static.throughput_rx < SET_UCLAMP_THRESHOLD) {
		attr.sched_util_min = 0;
		wcn_thread_setattr(0, &attr);
	}
}
#endif

