/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "debug.h"
#include "msg.h"

int sprd_init_msg(int num, struct sprd_msg_list *list)
{
	int i;
	struct sprd_msg *msg;
	struct sprd_msg *pos;

	if (!list)
		return -EPERM;
	INIT_LIST_HEAD(&list->freelist);
	INIT_LIST_HEAD(&list->busylist);
	INIT_LIST_HEAD(&list->cmd_to_free);
	list->maxnum = num;
	spin_lock_init(&list->freelock);
	spin_lock_init(&list->busylock);
	spin_lock_init(&list->complock);
	atomic_set(&list->ref, 0);
	atomic_set(&list->flow, 0);
	for (i = 0; i < num; i++) {
		msg = kzalloc(sizeof(*msg), GFP_KERNEL);
		if (msg) {
			INIT_LIST_HEAD(&msg->list);
			list_add_tail(&msg->list, &list->freelist);
		} else {
			wl_err("%s failed to alloc msg!\n", __func__);
			goto err_alloc_buf;
		}
	}

	return 0;

err_alloc_buf:
	list_for_each_entry_safe(msg, pos, &list->freelist, list) {
		list_del(&msg->list);
		kfree(msg);
	}
	return -ENOMEM;
}

s64 sprd_get_ktime(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	struct timespec64 ktime;

	ktime_get_real_ts64(&ktime);
	return timespec64_to_ns(&ktime);
#else
	struct timespec ktime;

	getnstimeofday(&ktime);
	return timespec_to_ns(&ktime);
#endif
}

void sprd_deinit_msg(struct sprd_msg_list *list)
{
	struct sprd_msg *msg;
	struct sprd_msg *pos;
	s64 txmsgftime1, txmsgftime2, time_diffms;

	atomic_add(SPRD_MSG_EXIT_VAL, &list->ref);
	if (atomic_read(&list->ref) > SPRD_MSG_EXIT_VAL)
		wl_err("%s ref not ok! wait for pop!\n", __func__);

	txmsgftime1 = sprd_get_ktime();
	while (atomic_read(&list->ref) > SPRD_MSG_EXIT_VAL) {
		txmsgftime2 = sprd_get_ktime();
		time_diffms = div_s64((txmsgftime2 - txmsgftime1), 1000000);
		if (time_diffms > 3000)
			break;
		usleep_range(2000, 2500);
	}

	wl_debug("%s list->ref ok!\n", __func__);

	if (!list_empty(&list->busylist))
		WARN_ON(1);

	list_for_each_entry_safe(msg, pos, &list->freelist, list) {
		list_del(&msg->list);
		kfree(msg);
	}
}

struct sprd_msg *sprd_alloc_msg(struct sprd_msg_list *list)
{
	struct sprd_msg *msg = NULL;
	unsigned long flags = 0;

	if (atomic_inc_return(&list->ref) >= SPRD_MSG_EXIT_VAL) {
		wl_err("alloc msg failed ref > SPRD_MSG_EXIT_VAL\n");
		atomic_dec(&list->ref);
		return NULL;
	}
	spin_lock_irqsave(&list->freelock, flags);
	if (!list_empty(&list->freelist)) {
		msg = list_first_entry(&list->freelist, struct sprd_msg, list);
		if (!msg) {
			spin_unlock_irqrestore(&list->freelock, flags);
			wl_err("look out alloc msg failed msg = NULL\n");
			atomic_dec(&list->ref);
			return NULL;
		}
		list_del(&msg->list);
	}
	spin_unlock_irqrestore(&list->freelock, flags);

	if (!msg)
		atomic_dec(&list->ref);
	return msg;
}

void sprd_free_msg(struct sprd_msg *msg, struct sprd_msg_list *list)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&list->freelock, flags);
	list_add_tail(&msg->list, &list->freelist);
	atomic_dec(&list->ref);
	spin_unlock_irqrestore(&list->freelock, flags);
}

void sprd_queue_msg(struct sprd_msg *msg, struct sprd_msg_list *list)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&list->busylock, flags);
	list_add_tail(&msg->list, &list->busylist);
	spin_unlock_irqrestore(&list->busylock, flags);
}

struct sprd_msg *sprd_peek_msg(struct sprd_msg_list *list)
{
	struct sprd_msg *msg = NULL;
	unsigned long flags = 0;

	spin_lock_irqsave(&list->busylock, flags);
	if (!list_empty(&list->busylist))
		msg = list_first_entry(&list->busylist, struct sprd_msg, list);
	spin_unlock_irqrestore(&list->busylock, flags);

	return msg;
}

void sprd_dequeue_msg(struct sprd_msg *msg, struct sprd_msg_list *list)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&list->busylock, flags);
	list_del(&msg->list);
	spin_unlock_irqrestore(&list->busylock, flags);
	sprd_free_msg(msg, list);
}
