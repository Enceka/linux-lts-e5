/*
* DX-FileCopyrightText: 2021-2022 Unisoc (Shanghai) Technologies Co., Ltd
* SPDX-License-Identifier: GPL-2.0
*
* Copyright 2021-2022 Unisoc (Shanghai) Technologies Co., Ltd
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of version 2 of the GNU General Public License
* as published by the Free Software Foundation.
*/

#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/of_device.h>

#include "marlin_platform.h"

#include "common/common.h"
#include "common/chip_ops.h"
#include "common/delay_work.h"
#include "common/debug.h"
#include "common/iface.h"
#include "rx.h"
#include "tx.h"
#include "qos.h"
#include "cmdevt.h"
#include "txrx.h"
#include "sipc_buf.h"
#include "defrag.h"
#include "cpu_performance.h"

#define SPRD_NORMAL_MEM	0
#define SPRD_DEFRAG_MEM	1
#define INIT_INTF_SC2355(num, type, out, interval, bsize, psize, max, \
		threshold, time, in_irq, pending, pop, push, complete, suspend) \
		{ .channel = num, .hif_type = type, .inout = out, .intr_interval = interval, \
		.buf_size = bsize, .pool_size = psize, .once_max_trans = max, \
		.rx_threshold = threshold, .timeout = time, .cb_in_irq = in_irq, \
		.max_pending = pending, .pop_link = pop, .push_link = push, \
		.tx_complete = complete, .power_notify = suspend }

#if defined(MORE_DEBUG)
static void sipc_dump_stats(struct sprd_hif *hif)
{
	wl_err("++print txrx statistics++\n");
	wl_err("tx packets: %lu, tx bytes: %lu\n", hif->stats.tx_packets,
	       hif->stats.tx_bytes);
	wl_err("tx filter num: %lu\n", hif->stats.tx_filter_num);
	wl_err("tx errors: %lu, tx dropped: %lu\n", hif->stats.tx_errors,
	       hif->stats.tx_dropped);
	wl_err("tx avg time: %lu\n", hif->stats.tx_avg_time);
	wl_err("tx realloc: %lu\n", hif->stats.tx_realloc);
	wl_err("tx arp num: %lu\n", hif->stats.tx_arp_num);
	wl_err("rx packets: %lu, rx bytes: %lu\n", hif->stats.rx_packets,
	       hif->stats.rx_bytes);
	wl_err("rx errors: %lu, rx dropped: %lu\n", hif->stats.rx_errors,
	       hif->stats.rx_dropped);
	wl_err("rx multicast: %lu, tx multicast: %lu\n",
	       hif->stats.rx_multicast, hif->stats.tx_multicast);
	wl_err("--print txrx statistics--\n");
}

static void sipc_clear_stats(struct sprd_hif *hif)
{
	memset(&hif->stats, 0x0, sizeof(struct txrx_stats));
}

/*calculate packets  average sent time from received
 *from network stack to freed by HIF every STATS_COUNT packets
 */
static void sipc_get_tx_avg_time(struct sprd_hif *hif,
				 s64 tx_start_time)
{
	s64 tx_end;

	tx_end = sprd_get_ktime();
	hif->stats.tx_cost_time += tx_end - tx_start_time;

	if (hif->stats.gap_num >= STATS_COUNT) {
		hif->stats.tx_avg_time =
		    div_s64(hif->stats.tx_cost_time, hif->stats.gap_num);
		sipc_dump_stats(hif);
		hif->stats.gap_num = 0;
		hif->stats.tx_cost_time = 0;
		wl_info("%s:%d packets avg cost time: %lld\n",
			__func__, __LINE__, hif->stats.tx_avg_time);
	}
}
#endif

static int sipc_tx_one(struct sprd_hif *hif, unsigned char *data,
		       int len, int chn)
{
	int ret;
	struct mbuf_t *head = NULL, *tail = NULL, *mbuf = NULL;
	int num = 1;

	ret = sprdwcn_bus_list_alloc(chn, &head, &tail, &num);
	//ret = 0;
	if (ret || !head || !tail) {
		wl_err("%s:%d sprdwcn_bus_list_alloc fail\n",
		       __func__, __LINE__);
		return -1;
	}

	mbuf = head;
	mbuf->buf = data;
	mbuf->len = len;
	mbuf->next = NULL;
	print_hex_dump_debug("tx to cp2 cmd data dump: ", DUMP_PREFIX_OFFSET,
			     16, 1, data + 4, len, 0);
	ret = sprdwcn_bus_push_list(chn, head, tail, num);
	if (ret) {
		mbuf = head;
		mbuf->buf = NULL;

		sprdwcn_bus_list_free(chn, head, tail, num);
	}

	return ret;
}

#define ADDR_OFFSET 7
static inline struct sipc_addr_buffer
*sipc_alloc_pcie_addr_buf(int tx_count)
{
	struct sipc_addr_buffer *addr_buffer;

	addr_buffer =
	    kzalloc(sizeof(struct sipc_addr_buffer) +
		    tx_count * SPRD_PHYS_LEN, GFP_ATOMIC);
	mb();
	if (!addr_buffer) {
		wl_err("%s:%d alloc sipc addr buf fail\n", __func__, __LINE__);
		return NULL;
	}
	addr_buffer->common.type = SPRD_TYPE_DATA_PCIE_ADDR;
	addr_buffer->common.direction_ind = 0;
	addr_buffer->common.buffer_type = 1;
	addr_buffer->number = tx_count;
	addr_buffer->offset = ADDR_OFFSET;
	addr_buffer->buffer_ctrl.buffer_inuse = 1;

	return addr_buffer;
}

static inline struct sipc_addr_buffer
*sipc_set_addr_to_mbuf(struct tx_mgmt *tx_mgmt, struct mbuf_t *mbuf,
		       int tx_count)
{
	struct sipc_addr_buffer *addr_buffer;

	addr_buffer = sipc_alloc_pcie_addr_buf(tx_count);
	if (!addr_buffer)
		return NULL;
	mbuf->len = ADDR_OFFSET + tx_count * SPRD_PHYS_LEN;
	mbuf->buf = (unsigned char *)addr_buffer;
	print_hex_dump_debug("tx buf: ", DUMP_PREFIX_OFFSET,
			     16, 1, mbuf->buf, mbuf->len, 0);

	return addr_buffer;
}

/*cut data list from tx data list*/
static inline void
sipc_list_cut_position(struct list_head *tx_list_head,
		       struct list_head *tx_list,
		       struct list_head *tail_entry, int ac_index)
{
	spinlock_t *lock;
	struct sprd_msg *msg = NULL;

	if (!tail_entry)
		return;
	msg = list_first_entry(tx_list, struct sprd_msg, list);
	if (msg->msg_type != SPRD_TYPE_DATA) {
		lock = &msg->msglist->busylock;
	} else {
		if (ac_index != SPRD_AC_MAX)
			lock = &msg->data_list->p_lock;
		else
			lock = &msg->xmit_msg_list->send_lock;
	}
	spin_lock_bh(lock);
	list_cut_position(tx_list_head, tx_list, tail_entry);
	spin_unlock_bh(lock);
}

static inline void
sprdwl_list_cut_to_send_list(struct list_head *head_entry,
			     struct list_head *tail_entry,
			     int count)
{
	struct sprd_hif *hif = sc2355_get_hif();
	struct tx_mgmt *tx_msg = (struct tx_mgmt *)hif->tx_mgmt;
	struct list_head list_tmp;
	struct list_head *head;

	INIT_LIST_HEAD(&list_tmp);
	spin_lock(&tx_msg->xmit_msg_list.free_lock);
	head = head_entry->prev;
	list_cut_position(&list_tmp, head, tail_entry);
	atomic_sub(count, &tx_msg->xmit_msg_list.free_num);
	spin_unlock(&tx_msg->xmit_msg_list.free_lock);
	list_splice(&list_tmp, &tx_msg->xmit_msg_list.to_send_list);
}

/*cut data list from tx data list*/
static inline int
sprdwl_list_cut_to_free_list(struct list_head *tx_list_head,
		struct list_head *tx_list, struct list_head *tail_entry,
		int tx_count)
{
	struct sprd_hif *hif = sc2355_get_hif();
	struct tx_mgmt *tx_msg = (struct tx_mgmt *)hif->tx_mgmt;
	int ret = 0;
	struct list_head tx_list_tmp;

	if (tail_entry == NULL) {
		wl_err("%s, %d, error tail_entry\n", __func__, __LINE__);
		return -1;
	}

	INIT_LIST_HEAD(&tx_list_tmp);
	list_cut_position(&tx_list_tmp, tx_list, tail_entry);
	spin_lock(&tx_msg->xmit_msg_list.free_lock);
	list_splice_tail(&tx_list_tmp, &tx_msg->xmit_msg_list.to_free_list);
	spin_unlock(&tx_msg->xmit_msg_list.free_lock);
	atomic_add(tx_count, &tx_msg->xmit_msg_list.free_num);

	return ret;
}

static inline int
sipc_list_cut_to_free_list(struct list_head *tx_list_head,
			   struct list_head *tx_list,
			   struct list_head *tail_entry, int tx_count)
{
	struct sprd_hif *hif = sc2355_get_hif();
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	struct sprd_work *misc_work = NULL;
	int ret = 0;

	if (!tail_entry) {
		wl_err("%s, %d, error tail_entry\n", __func__, __LINE__);
		return -1;
	}

	/* TODO: How to do with misc_work = NULL? */
	misc_work = sprd_alloc_work(sizeof(struct list_head));
	if (misc_work) {
		misc_work->id = SPRD_PCIE_TX_MOVE_BUF;
		misc_work->len = tx_count;
		misc_work->hw_type = SPRD_HW_SC2355_SIPC;
		INIT_LIST_HEAD((struct list_head *)misc_work->data);
		list_cut_position((struct list_head *)misc_work->data,
				  tx_list, tail_entry);

		sprd_queue_work(hif->priv, misc_work);
		atomic_add(tx_count, &tx_mgmt->xmit_msg_list.free_num);
	} else {
		wl_err("%s: fail to alloc tx move misc work\n", __func__);
		ret = -1;
	}
	return ret;
}

static int sipc_rx_fill_mbuf(struct mbuf_t *head, struct mbuf_t *tail, int num,
			     int len)
{
	int ret = 0, count = 0;
	struct mbuf_t *pos = NULL;

	for (pos = head, count = 0; count < num; count++) {
		wl_all("%s: pos: %p\n", __func__, pos);
		pos->len = ALIGN(len, SMP_CACHE_BYTES);
		pos->buf = netdev_alloc_frag(pos->len);

		if (unlikely(!pos->buf)) {
			wl_err("%s: buffer error\n", __func__);
			ret = -ENOMEM;
			break;
		}

		pos->phy = virt_to_phys(pos->buf) | SPRD_MH_ADDRESS_BIT;
		if (unlikely(!pos->phy)) {
			wl_err("%s: buffer error\n", __func__);
			ret = -ENOMEM;
			break;
		}

		pos = pos->next;
	}

	if (ret) {
		pos = head;
		while (count--) {
			sc2355_free_data(pos->buf, SPRD_DEFRAG_MEM);
			pos = pos->next;
		}
	}

	return ret;
}

static int sipc_rx_common_push(int chn, struct mbuf_t **head,
			       struct mbuf_t **tail, int *num, int len)
{
	int ret = 0;

	if (0 == (*num))
		return ret;

	ret = sprdwcn_bus_list_alloc(chn, head, tail, num);
	if (ret || head == NULL || tail == NULL || *head == NULL || *tail == NULL) {
		wl_err("%s:%d sprdwcn_bus_list_alloc fail\n", __func__,
		       __LINE__);
		ret = -ENOMEM;
	} else {
		ret = sipc_rx_fill_mbuf(*head, *tail, *num, len);
		if (ret) {
			wl_err("%s: alloc buf fail\n", __func__);
			sprdwcn_bus_list_free(chn, *head, *tail, *num);
			*head = NULL;
			*tail = NULL;
			*num = 0;
		}
	}

	return ret;
}

static int sipc_rx_handle(int chn, struct mbuf_t *head,
			  struct mbuf_t *tail, int num)
{
	struct sprd_hif *hif = sc2355_get_hif();
	struct rx_mgmt *rx_mgmt = (struct rx_mgmt *)hif->rx_mgmt;
	struct sprd_msg *msg = NULL;
	int buf_num = 0, ret = 0;
	struct mbuf_t *pos = head;

	wl_all("%s: channel:%d head:%p tail:%p num:%d\n",
	       __func__, chn, head, tail, num);

	for (buf_num = num; buf_num > 0; buf_num--, pos = pos->next) {
		if (unlikely(!pos)) {
			wl_err("%s: NULL mbuf\n", __func__);
			break;
		}

		pos->phy = 0;

		msg = sprd_alloc_msg(&rx_mgmt->rx_list);
		if (!msg) {
			wl_err("%s: no more msg\n", __func__);
			continue;
		}

		sprd_fill_msg(msg, NULL, pos->buf, pos->len);

		msg->fifo_id = chn;
		msg->buffer_type = SPRD_NORMAL_MEM;
		msg->data = msg->tran_data + hif->hif_offset;

		msg->tran_data = sipc_fill_mbuf(pos->buf, pos->len);
		msg->data = msg->tran_data;

		if (msg->data == NULL) {
			wl_err("%s: no more mbuf\n", __func__);
			sprd_free_msg(msg, &rx_mgmt->rx_list);
			continue;
		}

		sprd_queue_msg(msg, &rx_mgmt->rx_list);
	}

	if (!ret)
		sprdwcn_bus_push_list(chn, head, tail, num);

	sc2355_rx_up(rx_mgmt);
	return 0;
}

#ifdef CONFIG_SPRD_WLAN_NAPI
static int sipc_data_rx_handle(int chn, struct mbuf_t *head,
			       struct mbuf_t *tail, int num)
{
	struct sprd_hif *hif = sc2355_get_hif();
	struct rx_mgmt *rx_mgmt = (struct rx_mgmt *)hif->rx_mgmt;
	struct sprd_msg *msg = NULL;

	wl_all("%s: channel:%d head:%p tail:%p num:%d\n",
		 __func__, chn, head, tail, num);

	/* FIXME: Should we use replace msg? */
	msg = sprd_alloc_msg(&rx_mgmt->rx_data_list);
	if (!msg) {
		wl_err("%s: no more msg\n", __func__);
		sprdwcn_bus_push_list(chn, head, tail, num);
		return 0;
	}

	sprd_fill_msg(msg, NULL, (void *)head, num);
	msg->fifo_id = chn;
	msg->buffer_type = SPRD_DEFRAG_MEM;
	msg->data = (void *)tail;

	sprd_queue_msg(msg, &rx_mgmt->rx_data_list);
	napi_schedule(&rx_mgmt->napi);

	return 0;
}
#endif

static int sipc_rx_cmd_push(int chn, struct mbuf_t **head, struct mbuf_t **tail,
			    int *num)
{
	return sipc_rx_common_push(chn, head, tail, num, SPRD_MAX_CMD_RXLEN);
}

static int sipc_rx_data_push(int chn, struct mbuf_t **head,
			     struct mbuf_t **tail, int *num)
{
	return sipc_rx_common_push(chn, head, tail, num, SPRD_MAX_DATA_RXLEN);
}

/*
 * mode:
 * 0 - suspend
 * 1 - resume
 */
static int sipc_suspend_resume_handle(int chn, int mode)
{
	struct sprd_hif *hif = sc2355_get_hif();
	struct sprd_priv *priv = hif->priv;
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	int ret;
	struct sprd_vif *vif = NULL, *tmp_vif;
	s64 time;

	spin_lock_bh(&priv->list_lock);
	list_for_each_entry(tmp_vif, &priv->vif_list, vif_node) {
		if (tmp_vif->state & VIF_STATE_OPEN) {
			vif = tmp_vif;
			break;
		}
	}
	spin_unlock_bh(&priv->list_lock);

	if (!vif || hif->cp_asserted) {
		wl_err("%s, %d, error! NULL vif or assert\n", __func__,
		       __LINE__);
		return -EBUSY;
	}

	if (mode == 0) {
		if (atomic_read(&tx_mgmt->tx_list_qos_pool.ref) > 0 ||
		    atomic_read(&tx_mgmt->tx_list_cmd.ref) > 0 ||
		    !list_empty(&tx_mgmt->xmit_msg_list.to_send_list) ||
		    !list_empty(&tx_mgmt->xmit_msg_list.to_free_list)) {
			wl_info("%s, %d,Q not empty suspend not allowed\n",
				__func__, __LINE__);
			return -EBUSY;
		}
		hif->suspend_mode = SPRD_PS_SUSPENDING;

		hif->sleep_time = sprd_get_ktime();

		priv->is_suspending = 1;
		ret = sprd_power_save(priv, vif, SPRD_SUSPEND_RESUME, 0);
		if (ret == 0)
			hif->suspend_mode = SPRD_PS_SUSPENDED;
		else
			hif->suspend_mode = SPRD_PS_RESUMED;
		return ret;
	} else if (mode == 1) {
		hif->suspend_mode = SPRD_PS_RESUMING;

		time = sprd_get_ktime();
		hif->sleep_time = time - hif->sleep_time;

		ret = sprd_power_save(priv, vif, SPRD_SUSPEND_RESUME, 1);
		wl_info("%s, %d,resume ret=%d, resume after %lld ms\n",
			__func__, __LINE__, ret, div_s64(hif->sleep_time, 1000000));
		return ret;
	}
	return -EBUSY;
}

struct mchn_ops_t sc2355_sipc_hif_ops[] = {

        /* RX channels */
        INIT_INTF_SC2355(SIPC_WIFI_CMD_RX, 2, 0, 0, SPRD_MAX_CMD_RXLEN,
                        64, 0, 0, 0, 1, 32, sipc_rx_handle,
                        sipc_rx_cmd_push, NULL, NULL),
        INIT_INTF_SC2355(SIPC_WIFI_DATA0_RX, 2, 0, 0, SPRD_MAX_CMD_RXLEN,
                        64, 0, 0, 0, 1, 32, sipc_rx_handle,
                        sipc_rx_data_push, NULL, NULL),

        INIT_INTF_SC2355(SIPC_WIFI_DATA1_RX, 2, 0, 0, SPRD_MAX_CMD_RXLEN,
                        64, 0, 0, 0, 1, 32, sipc_rx_handle,
                        sipc_rx_data_push, NULL, NULL),
        /* TX channels */
        INIT_INTF_SC2355(SIPC_WIFI_CMD_TX, 2, 1, 0, SPRD_MAX_CMD_TXLEN,
                        64, 0, 0, 0, 1, 32, sc2355_sipc_tx_cmd_pop_list,
                        NULL, NULL, sipc_suspend_resume_handle),
        INIT_INTF_SC2355(SIPC_WIFI_DATA0_TX, 1, 1, 0, SPRD_MAX_CMD_TXLEN,
                        64, 0, 0, 0, 1, 32, sc2355_sipc_tx_data_pop_list,
                        NULL, NULL, NULL),
        INIT_INTF_SC2355(SIPC_WIFI_DATA1_TX, 1, 1, 0, SPRD_MAX_CMD_TXLEN,
                        64, 0, 0, 0, 1, 4, sc2355_sipc_tx_data_pop_list,
                        NULL, NULL, NULL),
};

void sc2355_sipc_set_coex_bt_on_off(u8 action)
{
	struct sprd_hif *hif = sc2355_get_hif();

	hif->coex_bt_on = action;
}

inline int sc2355_sipc_tx_cmd(struct sprd_hif *hif, unsigned char *data, int len)
{
	return sipc_tx_one(hif, data, len, hif->tx_cmd_port);
}

inline int sc2355_tx_addr_trans_sipc(struct sprd_hif *hif,
				     unsigned char *data, int len,
				     bool send_now)
{
	struct rx_mgmt *rx_mgmt = (struct rx_mgmt *)hif->rx_mgmt;
	struct mbuf_t *head = NULL, *tail = NULL, *mbuf = NULL;
	int num = 1, ret = 0;

	if (data) {
		ret = sprdwcn_bus_list_alloc(hif->tx_data_port,
					     &head, &tail, &num);
		if (ret || !head || !tail) {
			wl_err("%s:%d sprdwcn_bus_list_alloc fail, chn: %d\n",
			       __func__, __LINE__, hif->tx_data_port);
		} else {
			mbuf = head;
			mbuf->buf = data;
			mbuf->len = len;
			mbuf->next = NULL;

			if (rx_mgmt->addr_trans_head) {
				((struct mbuf_t *)
				 rx_mgmt->addr_trans_tail)->next = head;
				rx_mgmt->addr_trans_tail = (void *)tail;
				rx_mgmt->addr_trans_num += num;
			} else {
				rx_mgmt->addr_trans_head = (void *)head;
				if (!head)
					wl_err
					    ("ERROR! %s, %d, addr_trans_head set to NULL\n",
					     __func__, __LINE__);
				rx_mgmt->addr_trans_tail = (void *)tail;
				rx_mgmt->addr_trans_num = num;
			}
		}
	}

	if (!ret && send_now && rx_mgmt->addr_trans_head) {
		ret = sc2355_sipc_push_link(hif, hif->tx_data_port,
				       (struct mbuf_t *)rx_mgmt->addr_trans_head,
				       (struct mbuf_t *)rx_mgmt->addr_trans_tail,
				       rx_mgmt->addr_trans_num,
				       sc2355_sipc_tx_data_pop_list);
		if (!ret) {
			rx_mgmt->addr_trans_head = NULL;
			rx_mgmt->addr_trans_tail = NULL;
			rx_mgmt->addr_trans_num = 0;
		} else if (ret < 0) {
			usleep_range(100, 200);
		}
	}
	wl_all("%s, trans rx buf, %d, cp2 buffer: %d\n", __func__, ret,
		skb_queue_len(&rx_mgmt->mm_entry.buffer_list));
	return ret;
}

inline void sc2355_sipc_tx_addr_trans_free(struct sprd_hif *hif)
{
	struct rx_mgmt *rx_mgmt = (struct rx_mgmt *)hif->rx_mgmt;

	sc2355_sipc_tx_data_pop_list(hif->tx_data_port,
			       (struct mbuf_t *)rx_mgmt->addr_trans_head,
			       (struct mbuf_t *)rx_mgmt->addr_trans_tail,
			       rx_mgmt->addr_trans_num);

	rx_mgmt->addr_trans_head = NULL;
	rx_mgmt->addr_trans_tail = NULL;
	rx_mgmt->addr_trans_num = 0;
}
static inline void sprd_mbuf_list_free(struct sprd_hif *hif,
					 struct mbuf_t *head,
					 struct mbuf_t *tail,
					 int count)
{
	int i;
	struct mbuf_t *mbuf_pos = head;

	for (i = 0; i < count && mbuf_pos; i++) {
		if (mbuf_pos->buf) {

			kfree(mbuf_pos->buf);
			mbuf_pos->phy = 0;
			mbuf_pos->buf = 0;
			mbuf_pos->len = 0;
		}
		mbuf_pos = mbuf_pos->next;
	}
	sprdwcn_bus_list_free(hif->tx_data_port, head, tail, count);
}

int sc2355_sipc_hif_tx_list(struct sprd_hif *hif,
		       struct list_head *tx_list,
		       struct list_head *tx_list_head,
		       int tx_count, int ac_index, u8 coex_bt_on,
		       enum sprd_mode mode)
{
	int ret = 0, i = 0, j = SIPC_TX_NUM;
	int sipc_count = 0, cnt = 0, num = 0, k = 0;
	struct sprd_msg *msg_pos;
	struct sipc_addr_buffer *addr_buffer = NULL;
	struct tx_mgmt *tx_mgmt;
	struct mbuf_t *head = NULL, *tail = NULL, *mbuf_pos;
	struct list_head *pos, *tx_list_tail, *tx_head = NULL;
	struct tx_msdu_dscr *dscr;
	int sipc_count_save = 0;
	//int print_len = 0;
#if defined(MORE_DEBUG)
	unsigned long tx_bytes = 0;
#endif

	wl_all("%s:%d tx_count is %d mode:%d\n", __func__, __LINE__, tx_count, mode);

	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;

	cnt = tx_count;
	while (cnt > SIPC_TX_NUM) {
		++num;
		cnt -= SIPC_TX_NUM;
	}
	sipc_count_save = sipc_count = num + 1;

	ret = sprdwcn_bus_list_alloc(hif->tx_data_port, &head, &tail,
					&sipc_count); //port is 6

	if (ret != 0 || head == NULL || tail == NULL || sipc_count == 0) {
		wl_err("%s:%d mbuf link alloc fail\n", __func__, __LINE__);
		return -1;
	}

	if (sipc_count_save != sipc_count) {
		wl_err("%s, %d error!mbuf not enough%d\n",
		       __func__, __LINE__, (sipc_count_save - sipc_count));
		sprdwcn_bus_list_free(hif->tx_data_port, head, tail, sipc_count);
		return -1;
	}

	mbuf_pos = head;
	for (i = 0; i < sipc_count && mbuf_pos; i++) {
		/* To prevent the mbuf_pos->buf not NULL case */
		mbuf_pos->buf = NULL;
		mbuf_pos = mbuf_pos->next;
	}
	mbuf_pos = head;

	if (sipc_count > 1) {
		addr_buffer =
			sipc_set_addr_to_mbuf(tx_mgmt,
					mbuf_pos, SIPC_TX_NUM);
	} else {
		addr_buffer =
			sipc_set_addr_to_mbuf(tx_mgmt,
					mbuf_pos, tx_count);
	}

	if (addr_buffer == NULL) {
		wl_err("%s:%d alloc sipc addr buf fail\n",
			       __func__, __LINE__);
		sprd_mbuf_list_free(hif, head, tail, sipc_count);
		return -1;
	}

	i = 0;
	list_for_each(pos, tx_list) {
		msg_pos = list_entry(pos, struct sprd_msg, list);
		sc2355_tcp_ack_move_msg(hif->priv, msg_pos);
		if (tx_head == NULL)
			tx_head = pos;

/*TODO*/
/*
		if (msg_pos->len > 200)
			print_len = 200;
		else
			print_len = msg_pos->len;
		print_hex_dump_debug("tx to cp2 data: ", DUMP_PREFIX_OFFSET,
				     16, 1, msg_pos->tran_data, print_len, 0);
*/
#if defined(MORE_DEBUG)
		tx_bytes += msg_pos->skb->len;
#endif
		if (sipc_count > 1 && num > 0 && i >= j) {
			if (--num == 0) {
				if (cnt > 0) {
					print_hex_dump_debug("tx to cp2 data: ",
							DUMP_PREFIX_OFFSET, 16, 1,
							mbuf_pos->buf, mbuf_pos->len, 0);
					mbuf_pos = mbuf_pos->next;
					addr_buffer =
						sipc_set_addr_to_mbuf(
								tx_mgmt, mbuf_pos, cnt);
				} else {
					wl_err("%s: cnt %d\n", __func__, cnt);
				}
			} else {
				/*if data num greater than SIPC_TX_NUM,
				*alloc another sipc addr buf
				*/
				j += SIPC_TX_NUM;
				print_hex_dump_debug("tx to addr trans: ",
						DUMP_PREFIX_OFFSET, 16, 1,
						mbuf_pos->buf, mbuf_pos->len, 0);
				mbuf_pos = mbuf_pos->next;
				addr_buffer =
					sipc_set_addr_to_mbuf(
					tx_mgmt, mbuf_pos, SIPC_TX_NUM);
			}

			if (addr_buffer == NULL) {
				wl_err("%s:%d alloc sipc addr buf fail\n",
					__func__, __LINE__);
				sprd_mbuf_list_free(hif, head, tail, sipc_count);
				return -1;
			}

			k = 0;
		}

		if (msg_pos->skb) {
			if (sipc_skb_to_tx_buf(hif, msg_pos)) {
				wl_err("%s skb to sipc node fail\n", __func__);
				sprd_mbuf_list_free(hif, head, tail, sipc_count);
				return -1;
			}
		}
		wl_all("debug sipc addr: 0x%lx\n", msg_pos->pcie_addr);
		memcpy(&addr_buffer->sipc_addr[k],
			&msg_pos->pcie_addr, SPRD_PHYS_LEN);
		dscr = (struct tx_msdu_dscr *)(msg_pos->tran_data + MSDU_DSCR_RSVD);
		addr_buffer->common.interface = dscr->common.interface;

		k++;

		if (++i == tx_count)
			break;
	}

	print_hex_dump_debug("tx to addr trans: ", DUMP_PREFIX_OFFSET,
			     16, 1, mbuf_pos->buf, mbuf_pos->len, 0);

	tx_list_tail = pos;

	sprdwl_list_cut_to_free_list(tx_list_head,
				tx_list, tx_list_tail,
				tx_count);
	hif->mbuf_head = (void *)head;
	hif->mbuf_tail = (void *)tail;
	hif->mbuf_num = sipc_count;

	if (hif->mbuf_head) {
		/*ret = mchn_push_link(9, head, tail, sipc_count);*/
		/*edma sync function*/
		ret = sc2355_sipc_push_link(hif, hif->tx_data_port,
					(struct mbuf_t *)hif->mbuf_head,
					(struct mbuf_t *)hif->mbuf_tail,
					hif->mbuf_num,
					sc2355_sipc_tx_data_pop_list);
		if (ret != 0) {
			/*wl_err("%s: push link fail\n", __func__); */
			hif->pushfail_count++;
			sprdwl_list_cut_to_send_list(tx_head,
							tx_list_tail,
							tx_count);
			if (tx_list_tail)
				sprd_mbuf_list_free(hif, head, tail, sipc_count);
		} else {
#if defined(MORE_DEBUG)
				UPDATE_TX_PACKETS(hif, tx_count, tx_bytes);
#endif
				INIT_LIST_HEAD(tx_list_head);
				hif->mbuf_head = NULL;
				hif->mbuf_tail = NULL;
				hif->mbuf_num = 0;
				hif->pushfail_count = 0;
				tx_mgmt->tx_num += tx_count;
		}
	}

	return ret;
}

#if 0
inline void *sc2355_get_rx_data(struct sprd_hif *hif,
				void *pos, void **data,
				void **tran_data, int *len, int offset)
{
	struct mbuf_t *mbuf = (struct mbuf_t *)pos;

	if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
		sc2355_mm_phys_to_virt(&hif->pdev->dev, mbuf->phy, mbuf->len,
				       DMA_FROM_DEVICE, false);
		mbuf->phy = 0;
	}

	*tran_data = mbuf->buf;
	*data = (*tran_data) + offset;
	*len = mbuf->len;
	mbuf->buf = NULL;

	return (void *)mbuf->next;
}

inline void sc2355_free_rx_data(struct sprd_hif *hif,
				int chn, void *head, void *tail, int num)
{
	int len = 0, ret = 0;

	/* We should refill mbuf in sipc mode */
	if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
		if (hif->rx_cmd_port == chn)
			len = SPRD_MAX_CMD_RXLEN;
		else
			len = SPRD_MAX_DATA_RXLEN;

		ret = sipc_rx_fill_mbuf(head, tail, num, len);
		if (ret) {
			wl_err("%s: alloc buf fail\n", __func__);
			sprdwcn_bus_list_free(chn, (struct mbuf_t *)head,
					      (struct mbuf_t *)tail, num);
			head = NULL;
			tail = NULL;
			num = 0;
		}
	}

	if (!ret)
		sprdwcn_bus_push_list(chn, (struct mbuf_t *)head,
				      (struct mbuf_t *)tail, num);
}
#endif

void sc2355_sipc_handle_pop_list(void *data)
{
	int i;
	struct sprd_msg *msg_pos;
	struct mbuf_t *mbuf_pos = NULL;
	struct pop_work *pop = (struct pop_work *)data;
	struct tx_mgmt *tx_mgmt;
	struct sprd_hif *hif = sc2355_get_hif();
	struct list_head tmp_list;
	struct sprd_msg *msg_head, *msg_tail;

	INIT_LIST_HEAD(&tmp_list);
	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	mbuf_pos = (struct mbuf_t *)pop->head;
	msg_pos = GET_MSG_BUF(mbuf_pos);

	msg_head = GET_MSG_BUF((struct mbuf_t *)pop->head);
	msg_tail = GET_MSG_BUF((struct mbuf_t *)pop->tail);

	spin_lock_bh(&tx_mgmt->xmit_msg_list.free_lock);
	list_cut_position(&tmp_list, msg_head->list.prev, &msg_tail->list);
	spin_unlock_bh(&tx_mgmt->xmit_msg_list.free_lock);

	for (i = 0; i < pop->num; i++) {
		msg_pos = GET_MSG_BUF(mbuf_pos);
		dev_kfree_skb(msg_pos->skb);
		mbuf_pos = mbuf_pos->next;
	}

	spin_lock_bh(&tx_mgmt->tx_list_qos_pool.freelock);
	list_splice_tail(&tmp_list, &msg_pos->msglist->freelist);
	spin_unlock_bh(&tx_mgmt->tx_list_qos_pool.freelock);
	sprdwcn_bus_list_free(pop->chn, pop->head, pop->tail, pop->num);
}

int sc2355_sipc_add_topop_list(int chn, struct mbuf_t *head,
			  struct mbuf_t *tail, int num)
{
	struct sprd_hif *hif = sc2355_get_hif();
	struct sprd_work *misc_work;
	struct pop_work pop_work;
	u8 *work_data = NULL;

	pop_work.chn = chn;
	pop_work.head = (void *)head;
	pop_work.tail = (void *)tail;
	pop_work.num = num;

	misc_work = sprd_alloc_work(sizeof(struct pop_work));
	if (!misc_work) {
		wl_err("%s out of memory\n", __func__);
		return -1;
	}
	misc_work->vif = NULL;
	misc_work->id = SPRD_POP_MBUF;
	misc_work->hw_type = SPRD_HW_SC2355_SIPC;
	work_data = misc_work->data;
	memcpy(work_data, &pop_work, sizeof(struct pop_work));

	sprd_queue_work(hif->priv, misc_work);
	return 0;
}

/*call back func for HIF pop_link*/
int sc2355_sipc_tx_data_pop_list(int channel, struct mbuf_t *head,
			    struct mbuf_t *tail, int num)
{
	struct mbuf_t *mbuf_pos = NULL;
	int tmp_num = num;

	wl_all("%s channel: %d, head: %p, tail: %p num: %d\n",
		__func__, channel, head, tail, num);

	/* FIXME: Temp solution, addr node pos hard to sync dma */
	for (mbuf_pos = head; mbuf_pos != NULL;
		     mbuf_pos = mbuf_pos->next) {
		mbuf_pos->phy = 0;
		mbuf_pos->len = 0;
		if (mbuf_pos->buf != NULL) {
			kfree(mbuf_pos->buf);
			mbuf_pos->buf = NULL;
		} else
			wl_info("%s error mbuf->pos is NULL!\n", __func__);
		if (--tmp_num == 0)
			break;
	}
	sprdwcn_bus_list_free(channel, head, tail, num);
	wl_all("%s:%d free : %d msg buf\n", __func__, __LINE__, num);
	return 0;
}

static inline int sprd_tx_free_txc_msg(struct tx_mgmt *tx_msg,
					 struct sprd_msg *msg_buf)
{
	struct sprd_msg *pos_msg = NULL;
	unsigned long lockflag_txc = 0;
	int found = 0;

	spin_lock_irqsave(&tx_msg->xmit_msg_list.free_lock, lockflag_txc);
	list_for_each_entry(pos_msg, &tx_msg->xmit_msg_list.to_free_list,
			    list) {
		if (pos_msg == msg_buf) {
			found = 1;
			break;
		}
	}

	if (found != 1) {
		wl_err("%s: msg_buf %p not in to free list\n",
			__func__, msg_buf);
		spin_unlock_irqrestore(&tx_msg->xmit_msg_list.free_lock,
				       lockflag_txc);
		return -1;
	}
	atomic_sub(1, &tx_msg->xmit_msg_list.free_num);
	list_del(&msg_buf->list);
	spin_unlock_irqrestore(&tx_msg->xmit_msg_list.free_lock, lockflag_txc);

	if (msg_buf->sipc_node)
		sipc_free_tx_buf(tx_msg->hif, msg_buf);
	if (msg_buf->skb)
		dev_kfree_skb(msg_buf->skb);

	msg_buf->sipc_node = NULL;
	msg_buf->skb = NULL;
	msg_buf->data = NULL;
	msg_buf->tran_data = NULL;
	msg_buf->len = 0;

	sprd_free_msg(msg_buf, msg_buf->msglist);
	return 0;
}

/*free PCIe data when receive txc event from cp*/
int sc2355_tx_free_sipc_data(unsigned char *data)
{
	int i;
	struct sprd_hif *hif = sc2355_get_hif();
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	void *data_addr_ptr;
	unsigned long sipc_addr;
	unsigned short data_num;
	struct txc_addr_buff *txc_addr;
	unsigned char (*pos)[5];
	struct sprd_msg *msg, *last_msg = NULL;
#if defined(MORE_DEBUG)
	unsigned long tx_start_time = 0;
#endif
	unsigned char *tmp;
	unsigned long phy_addr;
	struct sipc_buf_node *node = NULL;
	static unsigned long caller_jiffies;
	struct sprd_priv *priv = hif->priv;
	struct sprd_msg *pos_msg = NULL;
	unsigned long lockflag_txc = 0;
	bool found = false;

	wl_all("%s:=0x%p %p %p\n", __func__, data, tx_mgmt, hif);

	if (tx_mgmt->net_stopped == 1) {
		sprd_net_flowcontrl(priv, SPRD_MODE_NONE, true);
		tx_mgmt->net_stopped = 0;
	}

	txc_addr = (struct txc_addr_buff *)data;
	data_num = txc_addr->number;
	tx_mgmt->txc_num += data_num;
	tmp = (unsigned char *)txc_addr;
	wl_all("%s: seq_num=0x%x", __func__, *(tmp + 1));
	//wl_info("%s, total free num:%lu; total tx_num:%lu\n", __func__,
	//	 tx_mgmt->txc_num, tx_mgmt->tx_num);

	if (printk_timed_ratelimit(&caller_jiffies, 1000)) {
		wl_info("%s, free_num: %d, to_free_list num: %d\n",
			__func__, data_num,
			atomic_read(&tx_mgmt->xmit_msg_list.free_num));
	}

	pos = (unsigned char (*)[5])(txc_addr + 1);
	for (i = 0; i < data_num; i++, pos++) {
		sipc_addr = 0;
		memcpy(&sipc_addr, pos, SPRD_PHYS_LEN);
		sipc_addr -= 0x10;	//Workaround for HW issue

		found = false;
		spin_lock_irqsave(&tx_mgmt->xmit_msg_list.free_lock, lockflag_txc);
		list_for_each_entry(pos_msg, &tx_mgmt->xmit_msg_list.to_free_list,
					list) {
			if (pos_msg->pcie_addr == sipc_addr) {
				found = true;
				break;
			}
		}
		spin_unlock_irqrestore(&tx_mgmt->xmit_msg_list.free_lock, lockflag_txc);

		if (!found) {
			wl_err("%s: sipc_addr 0x%lx not in to free list\n",
						__func__, sipc_addr);
			continue;
		}

		wl_all("%s: sipc_addr=0x%lx", __func__, sipc_addr);
		phy_addr = sipc_addr & (~(SPRD_MH_ADDRESS_BIT) & SPRD_PHYS_MASK);
		phy_addr = phy_addr | SPRD_MH_SIPC_ADDRESS_BASE;
		data_addr_ptr = (void *)(phy_addr + hif->sipc_mm->tx_buf->offset);
		msg = NULL;

		RESTORE_ADDR(msg, data_addr_ptr, sizeof(msg));
		memcpy_fromio(&node, (char *)data_addr_ptr + SPRD_MAX_DATA_TXLEN, sizeof(node));
		if (node != NULL) {
			msg = node->priv;
		} else {
			wl_err("%s, node already is NULL\n", __func__);
			continue;
		}
		if (last_msg == msg) {
			wl_info("%s: same msg buf: %lx, %lx\n", __func__,
				(unsigned long)msg, (unsigned long)last_msg);
		}
		wl_all("data_addr_ptr: 0x%p, msg: 0x:%p\n",
					data_addr_ptr, msg);
#if defined(MORE_DEBUG)
		if (i == 0)
			tx_start_time = msg->tx_start_time;
#endif
		if (!sprd_tx_free_txc_msg(tx_mgmt, msg))
			last_msg = msg;
	}
	sc2355_tx_up(tx_mgmt);

#if defined(MORE_DEBUG)
	sipc_get_tx_avg_time(hif, tx_start_time);
#endif

	return 0;
}

int sc2355_sipc_tx_cmd_pop_list(int channel, struct mbuf_t *head,
			   struct mbuf_t *tail, int num)
{
	int count = 0;
	struct mbuf_t *pos = NULL;
	struct sprd_hif *hif = sc2355_get_hif();
	struct tx_mgmt *tx_mgmt;
	struct sprd_msg *pos_buf, *temp_buf;

	wl_all("%s channel: %d, head: %p, tail: %p num: %d\n",
		 __func__, channel, head, tail, num);

	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;

	wl_all("%s len: %d buf: %s\n", __func__, head->len, head->buf + 4);

	pos = head;

	list_for_each_entry_safe(pos_buf, temp_buf,
				 &tx_mgmt->tx_list_cmd.cmd_to_free, list) {
		if (pos_buf->tran_data == pos->buf) {
			wl_all("move CMD node from to_free to free list\n");
			/*list msg from to_free list  to free list*/
			sc2355_free_cmd_buf(pos_buf, &tx_mgmt->tx_list_cmd);
			/*free it*/
			kfree(pos->buf);
			pos->buf = NULL;
			pos = pos->next;
			count++;
		}
		if (count == num)
			break;
	}

	tx_mgmt->cmd_poped += num;
	wl_debug("tx_cmd_pop num: %d,cmd_poped=%d, cmd_send=%d\n",
		num, tx_mgmt->cmd_poped, tx_mgmt->cmd_send);
	sprdwcn_bus_list_free(channel, head, tail, num);

	return 0;
}

int sc2355_sipc_push_link(struct sprd_hif *hif, int chn,
		     struct mbuf_t *head, struct mbuf_t *tail, int num,
		     int (*pop)(int, struct mbuf_t *, struct mbuf_t *, int))
{
	int ret = 0;
	struct mbuf_t *pos = head;
	int i = 0;

	for (i = 0; i < num; i++) {
		if (i == num && pos != tail)
			wl_info("num of head to tail is not match\n");

		pos = pos->next;
	}

	ret = sprdwcn_bus_push_list(chn, head, tail, num);

	if (ret) {
		wl_err("%s: push link fail: %d, chn: %d!\n", __func__, ret,
		       chn);
	}
	return ret;
}

/* update lut-inidex if event_sta_lut received
 * at CP side, lut_index range 0-31
 * but 0-3 were used to send non-assoc frame(only used by CP)
 * so for Ap-CP interface, there is only 4-31
 */
void sc2355_sipc_event_sta_lut(struct sprd_vif *vif, u8 *data, u16 len)
{
	struct sprd_hif *hif;
	struct evt_sta_lut_ind *sta_lut = NULL;
	u8 i;

	if (len < sizeof(*sta_lut)) {
		wl_err("%s, len:%d too short!\n", __func__, len);
		return;
	}
	hif = &vif->priv->hif;
	sta_lut = (struct evt_sta_lut_ind *)data;
	if (hif != sc2355_get_hif()) {
		wl_err("%s, wrong hif!\n", __func__);
		return;
	}
	if (!sta_lut) {
		wl_err("%s, NULL input data!\n", __func__);
		return;
	}

	i = sta_lut->sta_lut_index;
	if (i >= MAX_LUT_NUM) {
		wl_err("%s, error sta_lut_index %d!\n", __func__, i);
		return;
	}

	wl_info("ctx_id:%d,action:%d,lut:%d\n", sta_lut->ctx_id,
		sta_lut->action, sta_lut->sta_lut_index);
	switch (sta_lut->action) {
	case DEL_LUT_INDEX:
		if (hif->peer_entry[i].ba_tx_done_map != 0) {
			hif->peer_entry[i].ht_enable = 0;
			hif->peer_entry[i].ip_acquired = 0;
			hif->peer_entry[i].ba_tx_done_map = 0;
			/*sc2355_tx_delba(hif, hif->peer_entry + i);*/
		}
		sc2355_defrag_recover(vif, i);
		sc2355_peer_entry_delba(hif, i);
		memset(&hif->peer_entry[i], 0x00,
		       sizeof(struct sprd_peer_entry));
		hif->peer_entry[i].ctx_id = 0xFF;
		hif->tx_num[i] = 0;
		sc2355_dis_flush_txlist(hif, i);
		break;
	case UPD_LUT_INDEX:
		sc2355_peer_entry_delba(hif, i);
		sc2355_dis_flush_txlist(hif, i);
		fallthrough;
	case ADD_LUT_INDEX:
		hif->peer_entry[i].lut_index = i;
		hif->peer_entry[i].ctx_id = sta_lut->ctx_id;
		hif->peer_entry[i].ht_enable = sta_lut->is_ht_enable;
		hif->peer_entry[i].vht_enable = sta_lut->is_vht_enable;
		hif->peer_entry[i].ba_tx_done_map = 0;
		hif->tx_num[i] = 0;

#ifdef CONFIG_SPRD_WLAN_DEBUG
		wl_info("ctx_id%d,action%d,lut%d,%x:%x:%x:%x:%x:%x\n",
			sta_lut->ctx_id, sta_lut->action,
			sta_lut->sta_lut_index,
			sta_lut->ra[0], sta_lut->ra[1], sta_lut->ra[2],
			sta_lut->ra[3], sta_lut->ra[4], sta_lut->ra[5]);
#else
		wl_info("ctx_id%d,action%d,lut%d,%x:%x:%x:%x:xx:xx\n",
			sta_lut->ctx_id, sta_lut->action,
			sta_lut->sta_lut_index, sta_lut->ra[0], sta_lut->ra[1],
			sta_lut->ra[2], sta_lut->ra[3]);
#endif
		ether_addr_copy(hif->peer_entry[i].tx.da, sta_lut->ra);
		break;
	default:
		break;
	}
}

unsigned short sc2355_sipc_get_data_csum(void *entry, void *data)
{
	return 0;
}

void sipc_mm_fill_all_buffer(struct sprd_hif *hif)
{
	struct rx_mgmt *rx_mgmt =
	    (struct rx_mgmt *)((struct sprd_hif *)hif)->rx_mgmt;
	struct mem_mgmt *mm_entry = &rx_mgmt->mm_entry;
	int num = SPRD_SIPC_MAX_MH_BUF - skb_queue_len(&mm_entry->buffer_list);
	u8 mode_opened = 0;
	struct sprd_priv *priv = hif->priv;
	struct sprd_vif *tmp_vif;

	/*TODO make sure driver send buf only once*/
	spin_lock_bh(&priv->list_lock);
	list_for_each_entry(tmp_vif, &priv->vif_list, vif_node) {
		if (tmp_vif->state & VIF_STATE_OPEN)
			mode_opened++;
	}
	spin_unlock_bh(&priv->list_lock);

	if (mode_opened > 1) {
		wl_info("%s, mm buffer already filled\n", __func__);
		return;
	}

	if (num >= 0) {
		atomic_add(num, &mm_entry->alloc_num);
		sc2355_mm_fill_buffer(hif);
	}
}

void sc2355_sipc_handle_tx_return(struct sprd_hif *hif,
				  struct sprd_msg_list *list,
				  int send_num, int ret)
{
	if (ret == -2) {
		atomic_sub(send_num, &list->ref);
		wl_debug("%s,%d,debug: %d\n", __func__, __LINE__, atomic_read(&list->ref));
		usleep_range(100, 200);
		return;
	} else if (ret < 0) {
		usleep_range(100, 200);
		return;
	} else {
		wl_debug("%s,%d,debug: %d\n", __func__, __LINE__, atomic_read(&list->ref));
	}
}

int sc2355_sipc_rx_work_queue(void *data)
{
	struct sprd_msg *msg;
	struct sprd_priv *priv;
	struct rx_mgmt *rx_mgmt;
	struct sprd_hif *hif;
	int print_len;

	rx_mgmt = (struct rx_mgmt *)data;
	hif = rx_mgmt->hif;
	priv = hif->priv;
	set_user_nice(current, -20);

	while (1) {
		if (hif->exit) {
			if (kthread_should_stop())
				return 0;
			usleep_range(50, 100);
			continue;
		} else
			sc2355_rx_down(rx_mgmt);


		sc2355_sipc_rx_process(rx_mgmt, NULL);

		while ((msg = sprd_peek_msg(&rx_mgmt->rx_list))) {
			if (hif->exit)
				goto next;
			wl_all("%s: rx type:%d\n",  __func__, SPRD_HEAD_GET_TYPE(msg->data));

			if (msg->len > 400)
				print_len = 400;
			else
				print_len = msg->len;

			print_hex_dump_debug("rx data: ", DUMP_PREFIX_OFFSET,
					16, 1, msg->data, print_len, 0);

			switch (SPRD_HEAD_GET_TYPE(msg->data)) {
			case SPRD_TYPE_DATA:
#if defined FPGA_LOOPBACK_TEST
				if (hif->loopback_n < 500) {
					unsigned char *r_buf;
					r_buf = (unsigned char *)msg->data;
					sprdwl_intf_tx_data_fpga_test(hif, r_buf, msg->len);
				}
#else
				if (msg->len > SPRD_MAX_DATA_RXLEN)
					wl_err("err rx data too long:%d > %d\n", msg->len,
					SPRD_MAX_DATA_RXLEN);
				rx_data_process(priv, msg->data);
#endif
				break;
			case SPRD_TYPE_CMD:
				if (msg->len > SPRD_MAX_CMD_RXLEN)
					wl_err("err rx cmd too long:%d > %d\n",
						msg->len, SPRD_MAX_CMD_RXLEN);
				sc2355_rx_rsp_process(priv, msg->data);
				break;
			case SPRD_TYPE_EVENT:
				if (msg->len > SPRD_MAX_CMD_RXLEN)
					wl_err("err rx event too long:%d > %d\n", msg->len,
						SPRD_MAX_CMD_RXLEN);
				sc2355_rx_evt_process(priv, msg->data);
				break;
			case SPRD_TYPE_DATA_SPECIAL:
				if (msg->len > SPRD_MAX_DATA_RXLEN)
					wl_err("err data trans too long:%d > %d\n", msg->len,
						SPRD_MAX_CMD_RXLEN);

				sc2355_mm_mh_data_process(&rx_mgmt->mm_entry, msg->tran_data,
								msg->len, msg->buffer_type);
				msg->tran_data = NULL;
				msg->data = NULL;
				break;
			case SPRD_TYPE_DATA_PCIE_ADDR:
				if (msg->len > SPRD_MAX_CMD_RXLEN)
					wl_err("err rx mh data too long:%d > %d\n", msg->len,
						SPRD_MAX_DATA_RXLEN);

				sc2355_rx_mh_addr_process(rx_mgmt, msg->tran_data, msg->len,
						msg->buffer_type);
				msg->tran_data = NULL;
				msg->data = NULL;
				break;
			default:
				wl_err("rx unknown type:%d\n", SPRD_HEAD_GET_TYPE(msg->data));
				break;
			}
next:
			if (msg->tran_data) {
				sc2355_free_data(msg->tran_data, msg->buffer_type);
				msg->tran_data = NULL;
				msg->data = NULL;
			}
			sprd_dequeue_msg(msg, &rx_mgmt->rx_list);
		}
	}
}

static inline int
sc2355_sipc_get_non_sta_mode_list_num(struct sprd_hif *hif)
{
	int i;
	struct sprd_vif *vif = NULL;
	struct qos_tx_t *tx_list =  NULL;
	int non_sta_mode_list_num = 0;
	struct tx_mgmt *tx_mgmt = hif->tx_mgmt;
	struct sprd_priv *priv = hif->priv;

	/* bug 2690357: sta find other mode list num to decide whether balance tx credit */
	for (i = SPRD_MODE_AP; i < SPRD_MODE_MAX; i++) {
		vif = sprd_mode_to_vif(priv, i);
		if (!vif)
			continue;

		tx_list = tx_mgmt->tx_list[i];
		non_sta_mode_list_num = atomic_read(&tx_list->mode_list_num);
		if (non_sta_mode_list_num > 0) {
			sprd_put_vif(vif);
			break;
		}

		sprd_put_vif(vif);
	}

	wl_debug("%s,%d  sta_tx_no_credit:%d non_sta_mode_list_num:%d",
		__func__, __LINE__, hif->sta_tx_no_credit, non_sta_mode_list_num);

	return non_sta_mode_list_num;

}

static inline int
sc2355_sipc_scc_balance(struct sprd_hif *hif, enum sprd_mode mode, int data_num, int tx_buf_max)
{
	int send_num = 0;
	int avr_credit, add_credit;
	int non_sta_mode_list_num = 0;

	avr_credit = tx_buf_max >> 1;
	if (mode == SPRD_MODE_STATION) {
		non_sta_mode_list_num = sc2355_sipc_get_non_sta_mode_list_num(hif);
		if (non_sta_mode_list_num > 0) {
			add_credit = avr_credit - non_sta_mode_list_num;

			if (add_credit > 0)
				send_num = avr_credit + add_credit;
			else
				send_num = avr_credit;
		} else {
			if (data_num >= tx_buf_max) {
				printk_ratelimited("%s, %d, tx_buf_max=%d, data_num=%d\n",
					__func__, __LINE__, tx_buf_max, data_num);
				send_num = tx_buf_max;
			} else {
				send_num = data_num;
			}
		}
	} else {
		if (data_num <= avr_credit)
			send_num = data_num;
		else
			send_num = avr_credit;
	}

	return send_num;
}

static inline int
sc2355_sipc_mcc_balance(struct sprd_hif *hif, enum sprd_mode mode, int data_num, int tx_buf_max)
{
	int send_num = 0;
	struct tx_mgmt *tx_mgmt = hif->tx_mgmt;
	int tx_cred = atomic_read(&tx_mgmt->sipc_tx_cred[mode]);

	if (tx_cred <= 0)
		return 0;
	if (tx_buf_max > tx_cred)
		tx_buf_max = tx_cred;

	send_num = data_num <= tx_buf_max ? data_num : tx_buf_max;

	return send_num;
}

static inline int sc2355_sipc_fc_get_normal_send_num(struct sprd_hif *hif,
			    enum sprd_mode mode, int data_num, unsigned int tx_buf_max)
{
	int free_num = 0;
	static unsigned long caller_jiffies_normal;
	struct tx_mgmt *tx_mgmt = hif->tx_mgmt;
	struct sipc_buf_mm *tx_buf = hif->sipc_mm->tx_buf;

	free_num = atomic_read(&tx_mgmt->xmit_msg_list.free_num);
	if (printk_timed_ratelimit(&caller_jiffies_normal, 1000)) {
		wl_all("%s, free_num=%d, data_num=%d， node_free=%d, node_busy=%d\n",
			__func__, free_num, data_num, atomic_read(&tx_buf->nlist.ref),
			atomic_read(&tx_buf->nlist.flow));
		if (list_empty(&tx_mgmt->xmit_msg_list.to_free_list))
			wl_all("%s: to free list empty\n", __func__);
	}

	if (data_num >= tx_buf_max) {
		printk_ratelimited("%s, %d, tx_buf_max=%d, data_num=%d\n", __func__,
			__LINE__, tx_buf_max, data_num);
		return tx_buf_max;
	} else {
		return data_num;
	}
}

static inline int sc2355_sipc_fc_get_sta_ap_send_num(struct sprd_hif *hif,
			    enum sprd_mode mode, int data_num, unsigned int tx_buf_max)
{
	struct tx_mgmt *tx_mgmt = hif->tx_mgmt;
	int send_num = 0;
	int free_num = 0;
	static unsigned long caller_jiffies_sta_ap;

	free_num = atomic_read(&tx_mgmt->xmit_msg_list.free_num);
	wl_all("%s,%d tx_buf_max:%d sta_tx_no_credit:%d mode:%d free_num:%d",
		__func__, __LINE__, tx_buf_max, hif->sta_tx_no_credit, mode, free_num);

	if (printk_timed_ratelimit(&caller_jiffies_sta_ap, 1000)) {
		wl_all("%s, free_num=%d, data_num=%d\n", __func__,
			free_num, data_num);
		if (list_empty(&tx_mgmt->xmit_msg_list.to_free_list))
			wl_all("%s: to free list empty\n", __func__);
	}

	if (tx_mgmt->tx_hold_flag != 1)
		send_num = sc2355_sipc_scc_balance(hif, mode, data_num, tx_buf_max);
	else
		send_num = sc2355_sipc_mcc_balance(hif, mode, data_num, tx_buf_max);

	if (send_num < data_num)
		wl_debug("%s,%d data_num:%d send_num:%d mode:%d tx_buf_max:%d margin:%d\n",
			__func__, __LINE__, data_num, send_num, mode, tx_buf_max,
			data_num - send_num);

	return send_num;
}

int sc2355_sipc_fc_get_send_num(struct sprd_hif *hif,
			    enum sprd_mode mode, int data_num)
{
	struct sprd_priv *priv = hif->priv;
	unsigned int tx_buf_max = get_max_fw_tx_dscr() >
				sipc_get_tx_buf_num(hif) ?
				sipc_get_tx_buf_num(hif) :
				get_max_fw_tx_dscr();

	if (unlikely(hif->cp_asserted)) {
		wl_err("%s cp2 assert!\n", __func__);
		return 0;
	}

	if (sprd_is_sta_ap_coexist(priv))
		return sc2355_sipc_fc_get_sta_ap_send_num(hif, mode, data_num, tx_buf_max);
	else
		return sc2355_sipc_fc_get_normal_send_num(hif, mode, data_num, tx_buf_max);
}

static inline int sc2355_sipc_fc_test_normal_send_num(struct sprd_hif *hif,
			    enum sprd_mode mode, int data_num, unsigned int tx_buf_max)
{
	int free_num = 0;
	static unsigned long caller_jiffies_normal;
	struct tx_mgmt *tx_mgmt = hif->tx_mgmt;
	struct sipc_buf_mm *tx_buf = hif->sipc_mm->tx_buf;

	free_num = atomic_read(&tx_mgmt->xmit_msg_list.free_num);
	if (printk_timed_ratelimit(&caller_jiffies_normal, 1000)) {
		wl_info("%s, free_num=%d, data_num=%d， node_free=%d, node_busy=%d\n",
			__func__, free_num, data_num, atomic_read(&tx_buf->nlist.ref),
			atomic_read(&tx_buf->nlist.flow));
		if (list_empty(&tx_mgmt->xmit_msg_list.to_free_list))
			wl_info("%s: to free list empty\n", __func__);
	}

	if (data_num >= tx_buf_max) {
		printk_ratelimited("%s, %d, tx_buf_max=%d, data_num=%d\n", __func__,
			__LINE__, tx_buf_max, data_num);
		return tx_buf_max;
	} else {
		return data_num;
	}
}

static inline int sc2355_sipc_fc_test_sta_ap_send_num(struct sprd_hif *hif,
			    enum sprd_mode mode, int data_num, unsigned int tx_buf_max)
{
	struct sprd_vif *vif = NULL;
	struct tx_mgmt *tx_mgmt = hif->tx_mgmt;
	struct sprd_priv *priv = hif->priv;
	int send_num = 0;
	int free_num = 0;
	static unsigned long caller_jiffies_sta_ap;

	vif = sprd_mode_to_vif(priv, mode);
	if (!vif)
		return 0;

	wl_all("%s,%d data_num:%d tx_buf_max:%d sta_tx_no_credit:%d mode:%d tx_hold_flag:%d tx_hold_ctxid:%d vif->ctx_id:%d",
		__func__, __LINE__, data_num, tx_buf_max, hif->sta_tx_no_credit, mode,
		tx_mgmt->tx_hold_flag, tx_mgmt->tx_hold_ctxid, vif->ctx_id);

	if (tx_mgmt->tx_hold_ctxid == vif->ctx_id && tx_mgmt->tx_hold_flag == 1) {
		wl_debug("%s,%d tx_hold_ctxid:%d\n", __func__, __LINE__, tx_mgmt->tx_hold_ctxid);
		sprd_put_vif(vif);
		return 0;
	}
	sprd_put_vif(vif);

	/* avoiding alloc all credit to another mode in one round of allocation. */
	if (hif->sta_tx_no_credit == 1 && mode != SPRD_MODE_STATION)
		return 0;

	if (unlikely(data_num <= 0))
		return 0;

	free_num = atomic_read(&tx_mgmt->xmit_msg_list.free_num);
	wl_all("%s,%d tx_buf_max:%d sta_tx_no_credit:%d mode:%d free_num:%d",
		__func__, __LINE__, tx_buf_max, hif->sta_tx_no_credit, mode, free_num);

	if (mode == SPRD_MODE_STATION && 0 == tx_buf_max)
		hif->sta_tx_no_credit = 1;
	else
		hif->sta_tx_no_credit = 0;

	if (printk_timed_ratelimit(&caller_jiffies_sta_ap, 1000)) {
		wl_all("%s, free_num=%d, data_num=%d\n", __func__,
			free_num, data_num);
		if (list_empty(&tx_mgmt->xmit_msg_list.to_free_list))
			wl_all("%s: to free list empty\n", __func__);
	}

	if (tx_mgmt->tx_hold_flag != 1)
		send_num = sc2355_sipc_scc_balance(hif, mode, data_num, tx_buf_max);
	else
		send_num = sc2355_sipc_mcc_balance(hif, mode, data_num, tx_buf_max);

	if (send_num < data_num)
		wl_debug("%s,%d data_num:%d send_num:%d mode:%d tx_buf_max:%d margin:%d\n",
			__func__, __LINE__, data_num, send_num, mode, tx_buf_max,
			data_num - send_num);

	return send_num;
}

int sc2355_sipc_fc_test_send_num(struct sprd_hif *hif,
			    enum sprd_mode mode, int data_num)
{
	struct sprd_priv *priv = hif->priv;
	unsigned int tx_buf_max = get_max_fw_tx_dscr() >
				sipc_get_tx_buf_num(hif) ?
				sipc_get_tx_buf_num(hif) :
				get_max_fw_tx_dscr();

	if (unlikely(hif->cp_asserted)) {
		wl_err("%s cp2 assert!\n", __func__);
		return 0;
	}

	if (sprd_is_sta_ap_coexist(priv))
		return sc2355_sipc_fc_test_sta_ap_send_num(hif, mode, data_num, tx_buf_max);
	else
		return sc2355_sipc_fc_test_normal_send_num(hif, mode, data_num, tx_buf_max);
}

int sc2355_sipc_init(struct sprd_hif *hif)
{
	u8 i;
	int ret = -EINVAL;

	hif->hw_type = SPRD_HW_SC2355_SIPC;
	ret = dma_coerce_mask_and_coherent(&hif->pdev->dev, DMA_BIT_MASK(39));

	if (ret) {
		wl_err("%s dma_coerce_mask_and_coherent fail.\n", __func__);
		return ret;
	}

	for (i = 0; i < MAX_LUT_NUM; i++)
		hif->peer_entry[i].ctx_id = 0xff;


	hif->hif_offset = 0;
	hif->dscr_rsvd = MSDU_DSCR_RSVD;
	hif->rx_cmd_port = SIPC_WIFI_CMD_RX;
	hif->rx_data_port = SIPC_WIFI_DATA0_RX;
	hif->tx_cmd_port = SIPC_WIFI_CMD_TX;
	hif->tx_data_port = SIPC_WIFI_DATA0_TX;

	if (sipc_txrx_buf_init(hif->pdev, hif)) {
		ret = -ENOMEM;
		wl_err("%s txrx buf init failed.\n", __func__);
		return ret;
	}
	adjust_max_fw_tx_dscr("max_fw_tx_dscr=1024", strlen("max_fw_tx_dscr="));

	ret = sc2355_sipc_rx_init(hif);
	if (ret) {
		wl_err("%s rx init failed: %d\n", __func__, ret);
		goto err_rx_init;
	}

	ret = sc2355_tx_init(hif);
	if (ret) {
		wl_err("%s tx_list init failed\n", __func__);
		goto err_tx_init;
	}

	sc2355_tp_static_init();
	sc2355_hif.mchn_ops = sc2355_sipc_hif_ops;
	sc2355_hif.max_num =
		    sizeof(sc2355_sipc_hif_ops) / sizeof(struct mchn_ops_t);
	hif->feature = NETIF_F_SG;
	return 0;

err_tx_init:
	sc2355_sipc_rx_deinit(hif);
err_rx_init:
	sipc_txrx_buf_deinit(hif);

	return ret;
}

int sipc_post_init(struct sprd_hif *hif)
{
	int ret = -EINVAL, chn = 0;
	sc2355_hif.hif = (void *)hif;
	sc2355_hif.max_num =
		sizeof(sc2355_sipc_hif_ops) / sizeof(struct mchn_ops_t);

	if (sc2355_hif.max_num < MAX_CHN_NUM) {
		wl_info("%s: register %d ops\n", __func__, sc2355_hif.max_num);

		for (chn = 0; chn < sc2355_hif.max_num; chn++) {
			ret = sprdwcn_bus_chn_init(&sc2355_hif.mchn_ops[chn]);
			if (ret < 0)
				goto err;
		}

		hif->fw_awake = 1;
		hif->fw_power_down = 0;
	}

	return 0;

err:
	wl_err("%s: unregister %d ops\n", __func__, sc2355_hif.max_num);

	for (; chn > 0; chn--)
		sprdwcn_bus_chn_deinit(&sc2355_hif.mchn_ops[chn]);
	sc2355_hif.mchn_ops = NULL;
	sc2355_hif.max_num = 0;

	return ret;
}

void sipc_post_deinit(struct sprd_hif *hif)
{
	int chn = 0;
	struct rx_mgmt *rx_mgmt = NULL;
	struct tx_mgmt *tx_mgmt = NULL;

	rx_mgmt = (struct rx_mgmt *)hif->rx_mgmt;
	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	if (rx_mgmt->addr_trans_head) {
		sc2355_sipc_tx_addr_trans_free(hif);
	}

	for (chn = 0; chn < sc2355_hif.max_num; chn++)
		sprdwcn_bus_chn_deinit(&sc2355_hif.mchn_ops[chn]);
	sc2355_hif.hif = NULL;
	sc2355_hif.max_num = 0;
	wl_info("reinit hang(%d) thermal(%d) suspend(%d) status to default\n",
		tx_mgmt->hang_recovery_status, tx_mgmt->thermal_status,
		hif->suspend_mode);
	tx_mgmt->hang_recovery_status = HANG_RECOVERY_END;
	tx_mgmt->thermal_status = THERMAL_TX_RESUME;
	hif->suspend_mode = SPRD_PS_RESUMED;
}

void sc2355_sipc_deinit(struct sprd_hif *hif)
{
	sc2355_tp_static_deinit();
	sc2355_tx_deinit(hif);
	sc2355_sipc_rx_deinit(hif);
	sipc_txrx_buf_deinit(hif);
}
static struct sprd_hif_ops sc2355_sipc_ops = {
	.init = sc2355_sipc_init,
	.deinit = sc2355_sipc_deinit,
	.post_init = sipc_post_init,
	.post_deinit = sipc_post_deinit,
	.sync_version = sc2355_sync_version,
	.download_hw_param = sc2355_sipc_download_hw_param,
	.reset = sc2355_reset,
	.fill_all_buffer = sipc_mm_fill_all_buffer,
	.tx_special_data = sc2355_tx_special_data,
	.free_msg_content = sipc_free_msg_content,
	.tx_addr_trans = sc2355_tx_addr_trans_sipc,
	.tx_flush = sc2355_tx_flush,
};

extern struct sprd_chip_ops sc2355_chip_ops;
int sc2355_sipc_probe(struct platform_device *pdev)
{
	return sprd_iface_probe(pdev, &sc2355_sipc_ops, &sc2355_chip_ops);
}
#if 0
static void sipc_remove(struct platform_device *pdev)
{
	sprd_iface_remove(pdev);
}

static const struct of_device_id sc2355_sipc_of_match[] = {
	{.compatible = "sprd,sc2355-sipc-wifi",},
	{}
};

MODULE_DEVICE_TABLE(of, sc2355_sipc_of_match);

static struct platform_driver sc2355_sipc_driver = {
	.probe = sipc_probe,
	.remove = sipc_remove,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "wlan",
		   .of_match_table = sc2355_sipc_of_match,
	}
};

module_platform_driver(sc2355_sipc_driver);

MODULE_DESCRIPTION("Spreadtrum SC2355 PCIE Initialization");
MODULE_AUTHOR("Spreadtrum WCN Division");
MODULE_LICENSE("GPL");
#endif
