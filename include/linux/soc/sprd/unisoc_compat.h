/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * What Unisoc's 5.15 drivers use of the kernel that later kernels no longer export to modules, in terms of
 * what they do export: the drivers keep their calls, the headers they include get this one.
 */
#ifndef __LINUX_SOC_SPRD_UNISOC_COMPAT_H
#define __LINUX_SOC_SPRD_UNISOC_COMPAT_H

#include <linux/sched.h>
#include <uapi/linux/sched/types.h>

/* sched_setscheduler() and sched_setattr() are not exported since 5.9; the _nocheck variant is */
static inline int unisoc_sched_setscheduler(struct task_struct *p, int policy,
					    const struct sched_param *param)
{
	struct sched_attr attr = {
		.size		= sizeof(attr),
		.sched_policy	= policy,
		.sched_priority	= param->sched_priority,
	};

	return sched_setattr_nocheck(p, &attr);
}
#define sched_setscheduler(p, policy, param)	unisoc_sched_setscheduler(p, policy, param)
#define sched_setattr(p, attr)			sched_setattr_nocheck(p, attr)

#endif
