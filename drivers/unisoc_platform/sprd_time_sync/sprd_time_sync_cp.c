// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Spreadtrum Communications Inc.
 *
 * Used to keep time in sync with AP and other systems.
 */

#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timekeeping.h>
#include <linux/timekeeper_internal.h>
#include <linux/uaccess.h>

#include <linux/soc/sprd/sprd_time_sync.h>
#if IS_ENABLED(CONFIG_ANDROID_VENDOR_HOOKS)
#include <trace/hooks/timekeeping.h>
#endif
#include <linux/timekeeping.h>
#include <linux/workqueue.h>

#define CN_TIMEZONE_OFFSET_SEC		(8 * 60 * 60)

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd_time_sync_cp: " fmt

/* realize the time sync func */
void sprd_time_sync_fn(void *data, struct timekeeper *tk)
{
#ifdef CONFIG_SPRD_DEBUG
	struct tm input_tm;
#endif
	u64 monotime_ns, realtime_s, boottime_ns;

	monotime_ns = tk->tkr_mono.base;
	boottime_ns = monotime_ns + tk->offs_boot;
	realtime_s = tk->xtime_sec;

	/* Send msg to refnotify */
	sprd_send_ap_time();

#ifdef CONFIG_SPRD_DEBUG
	time64_to_tm(realtime_s, CN_TIMEZONE_OFFSET_SEC, &input_tm);

	pr_info("Send realtime: %ld-%d-%d %d:%d:%d monotime: %lldns boottime: %lldns\n",
		1900 + input_tm.tm_year,
		input_tm.tm_mon,
		input_tm.tm_mday,
		input_tm.tm_hour,
		input_tm.tm_min,
		input_tm.tm_sec,
		monotime_ns,
		boottime_ns);
	pr_info("name: %s pid: %d\n", current->comm, current->pid);
#endif
}

#if IS_ENABLED(CONFIG_ANDROID_VENDOR_HOOKS)
static int sprd_time_sync_cp_init(void)
{
	int ret;

	ret = register_trace_android_rvh_tk_based_time_sync(sprd_time_sync_fn, NULL);
	if (ret)
		pr_info("%s: register sprd_time_sync_fn failed(ret=%d)\n",__FUNCTION__, ret);

	return ret;
}
#else
/*
 * Without Android's vendor hook on the timekeeper, a set clock shows as the
 * offset between CLOCK_REALTIME and CLOCK_MONOTONIC moving: look at it every
 * few seconds and tell the CP the time whenever it moved by more than a
 * second (NTP's slewing does not come near that).
 */
#define SPRD_TIME_SYNC_POLL	(10 * HZ)

static s64 sprd_time_sync_offs;

static void sprd_time_sync_poll(struct work_struct *work);
static DECLARE_DELAYED_WORK(sprd_time_sync_work, sprd_time_sync_poll);

static s64 sprd_time_sync_real_offs(void)
{
	return ktime_to_ns(ktime_sub(ktime_get_real(), ktime_get()));
}

static void sprd_time_sync_poll(struct work_struct *work)
{
	s64 offs = sprd_time_sync_real_offs();

	if (abs(offs - sprd_time_sync_offs) > NSEC_PER_SEC) {
		sprd_time_sync_offs = offs;
		sprd_send_ap_time();
	}
	schedule_delayed_work(&sprd_time_sync_work, SPRD_TIME_SYNC_POLL);
}

static int sprd_time_sync_cp_init(void)
{
	sprd_time_sync_offs = sprd_time_sync_real_offs();
	schedule_delayed_work(&sprd_time_sync_work, SPRD_TIME_SYNC_POLL);

	return 0;
}

static void sprd_time_sync_cp_exit(void)
{
	cancel_delayed_work_sync(&sprd_time_sync_work);
}
module_exit(sprd_time_sync_cp_exit);
#endif

module_init(sprd_time_sync_cp_init)

MODULE_AUTHOR("Weidong Guan && Ruifeng Zhang");
MODULE_DESCRIPTION("sprd time sync cp driver");
MODULE_LICENSE("GPL v2");
