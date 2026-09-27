/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/of_device.h>

#include "marlin_platform.h"

#include "common/common.h"
#include "common/chip_ops.h"
#include "common/delay_work.h"
#include "pcie.h"
#include "rx.h"
#include "tx.h"
#include "qos.h"
#include "cmdevt.h"
#include "txrx.h"
#include "defrag.h"
#ifdef ENABLE_PAM_WIFI
#include "pamwifi/pamwifi.h"
#endif
#define SPRD_NORMAL_MEM	0
#define SPRD_DEFRAG_MEM	1

#define INIT_INTF_SC2355(num, type, out, interval, bsize, psize, max, \
		threshold, time, in_irq, pending, pop, push, complete, suspend) \
		{ .channel = num, .hif_type = type, .inout = out, .intr_interval = interval, \
		.buf_size = bsize, .pool_size = psize, .once_max_trans = max, \
		.rx_threshold = threshold, .timeout = time, .cb_in_irq = in_irq, \
		.max_pending = pending, .pop_link = pop, .push_link = push, \
		.tx_complete = complete, .power_notify = suspend }

struct list_head pcie_rx_mbuf_list;

#if defined(MORE_DEBUG)
static void pcie_dump_stats(struct sprd_hif *hif)
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

static void pcie_clear_stats(struct sprd_hif *hif)
{
	memset(&hif->stats, 0x0, sizeof(struct txrx_stats));
}

/*calculate packets  average sent time from received
 *from network stack to freed by HIF every STATS_COUNT packets
 */
static void pcie_get_tx_avg_time(struct sprd_hif *hif,
				 s64 tx_start_time)
{
	s64 tx_end;

	tx_end = sprd_get_ktime();
	hif->stats.tx_cost_time += tx_end - tx_start_time;

	if (hif->stats.gap_num >= STATS_COUNT) {
		hif->stats.tx_avg_time =
		    div_s64(hif->stats.tx_cost_time, hif->stats.gap_num);
		pcie_dump_stats(hif);
		hif->stats.gap_num = 0;
		hif->stats.tx_cost_time = 0;
		wl_info("%s:%d packets avg cost time: %lld\n",
			__func__, __LINE__, hif->stats.tx_avg_time);
	}
}
#endif

void sc2355_pcie_dump_addr(struct sprd_hif *hif)
{
	unsigned long lockflag_txc = 0;
	struct tx_mgmt *tx_mgmt = NULL;
	struct sprd_msg *pos_msg = NULL;

	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;

	spin_lock_irqsave(&tx_mgmt->xmit_msg_list.free_lock, lockflag_txc);
	list_for_each_entry(pos_msg, &tx_mgmt->xmit_msg_list.to_free_list, list) {
		wl_err("%s: pos_msg %p, pcie_addr=0x%lx\n",
				__func__, pos_msg, pos_msg->pcie_addr);
	}
	spin_unlock_irqrestore(&tx_mgmt->xmit_msg_list.free_lock, lockflag_txc);
}

unsigned long pcie_mbufalloc;
unsigned long pcie_mbufpop;
static int pcie_tx_one(struct sprd_hif *hif, unsigned char *data,
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

	pcie_mbufalloc += num;
	mbuf = head;
	mbuf->buf = data;
	mbuf->len = len;
	mbuf->next = NULL;
	print_hex_dump_debug("tx to cp2 cmd data dump: ", DUMP_PREFIX_OFFSET,
			     16, 1, data + 4, len, 0);
	if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
		mbuf->phy = sc2355_mm_virt_to_phys(&hif->pdev->dev, mbuf->buf,
						   mbuf->len, DMA_TO_DEVICE);
		ret = sc2355_pcie_push_link(hif, chn, head, tail, num,
				       sc2355_pcie_tx_cmd_pop_list);
	} else {
		ret = sprdwcn_bus_push_list(chn, head, tail, num);
	}

	if (ret) {
		mbuf = head;
		if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
			sc2355_mm_phys_to_virt(&hif->pdev->dev, mbuf->phy,
					       mbuf->len, DMA_TO_DEVICE, false);
			mbuf->phy = 0;
		}
		kfree(mbuf->buf);
		mbuf->buf = NULL;

		sprdwcn_bus_list_free(chn, head, tail, num);
		pcie_mbufalloc -= num;
	}

	return ret;
}

#define ADDR_OFFSET 7
static inline struct pcie_addr_buffer
*pcie_alloc_pcie_addr_buf(int tx_count)
{
	struct pcie_addr_buffer *addr_buffer;

	addr_buffer =
	    kzalloc(sizeof(struct pcie_addr_buffer) +
		    tx_count * SPRD_PHYS_LEN, GFP_ATOMIC);
	mb();
	if (!addr_buffer) {
		wl_err("%s:%d alloc pcie addr buf fail\n", __func__, __LINE__);
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

static inline struct pcie_addr_buffer
*pcie_set_addr_to_mbuf(struct tx_mgmt *tx_mgmt, struct mbuf_t *mbuf,
		       int tx_count)
{
	struct pcie_addr_buffer *addr_buffer;

	addr_buffer = pcie_alloc_pcie_addr_buf(tx_count);
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
pcie_list_cut_position(struct list_head *tx_list_head,
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
pcie_list_cut_to_free_list(struct list_head *tx_list_head,
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
		if (hif->hw_type == SPRD_HW_SC2355_PCIE)
			misc_work->id = SPRD_PCIE_TX_MOVE_BUF;
		misc_work->len = tx_count;
		misc_work->hw_type = SPRD_HW_SC2355_PCIE;
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

static int pcie_queue_mbuf(void *buf)
{
	struct pcie_rx_mbuf *rx_mbuf = NULL;
	int ret = 0;

	rx_mbuf = kzalloc(sizeof(*rx_mbuf), GFP_ATOMIC);
	if (rx_mbuf) {
		INIT_LIST_HEAD(&rx_mbuf->list);
		rx_mbuf->buf = buf;
		list_add_tail(&rx_mbuf->list, &pcie_rx_mbuf_list);
	} else {
		pr_err("%s failed to alloc rx_mbuf!\n", __func__);
		ret = -ENOMEM;
	}

	return ret;
}

static void pcie_dequeue_mbuf(void *buf)
{
	struct pcie_rx_mbuf *rx_mbuf = NULL, *rx_mbuf_pos = NULL;

	list_for_each_entry_safe(rx_mbuf, rx_mbuf_pos, &pcie_rx_mbuf_list, list) {
		if (rx_mbuf->buf == buf) {
			list_del(&rx_mbuf->list);
			kfree(rx_mbuf);
			rx_mbuf = NULL;
			break;
		}
	}
}

static int pcie_rx_fill_mbuf(struct mbuf_t *head, struct mbuf_t *tail, int num,
			     int len)
{
	struct sprd_hif *hif = sc2355_get_hif();
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

		pos->phy = sc2355_mm_virt_to_phys(&hif->pdev->dev, pos->buf,
						  pos->len, DMA_FROM_DEVICE);

		if (unlikely(!pos->phy)) {
			wl_err("%s: buffer error\n", __func__);
			ret = -ENOMEM;
			break;
		}

		ret = pcie_queue_mbuf(pos->buf);
		if (ret) {
			pr_err("%s failed to pcie_queue_mbuf!\n", __func__);
			break;
		}

		pos = pos->next;
	}

	if (ret) {
		pos = head;
		while (count--) {
			pcie_dequeue_mbuf(pos->buf);
			sc2355_free_data(pos->buf, SPRD_DEFRAG_MEM);
			pos = pos->next;
		}
	}

	return ret;
}

static int pcie_rx_common_push(int chn, struct mbuf_t **head,
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
		ret = pcie_rx_fill_mbuf(*head, *tail, *num, len);
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

static int pcie_rx_handle(int chn, struct mbuf_t *head,
			  struct mbuf_t *tail, int num)
{
	struct sprd_hif *hif = sc2355_get_hif();
	struct rx_mgmt *rx_mgmt = (struct rx_mgmt *)hif->rx_mgmt;
	struct sprd_msg *msg = NULL;
	int buf_num = 0, len = 0, ret = 0;
	struct mbuf_t *pos = head;

	wl_all("%s: channel:%d head:%p tail:%p num:%d\n",
	       __func__, chn, head, tail, num);

	for (buf_num = num; buf_num > 0; buf_num--, pos = pos->next) {
		if (unlikely(!pos)) {
			wl_err("%s: NULL mbuf\n", __func__);
			break;
		}

		pcie_dequeue_mbuf(pos->buf);
		sc2355_mm_phys_to_virt(&hif->pdev->dev, pos->phy,
				       pos->len, DMA_FROM_DEVICE,
				       false);
		pos->phy = 0;

		msg = sprd_alloc_msg(&rx_mgmt->rx_list);
		if (!msg) {
			wl_err("%s: no more msg\n", __func__);
			sc2355_free_data(pos->buf, SPRD_DEFRAG_MEM);
			pos->buf = NULL;
			continue;
		}

		sprd_fill_msg(msg, NULL, pos->buf, pos->len);

		msg->fifo_id = chn;
		msg->buffer_type = SPRD_DEFRAG_MEM;
		msg->data = msg->tran_data + hif->hif_offset;
		pos->buf = NULL;

		sprd_queue_msg(msg, &rx_mgmt->rx_list);
	}

	if (hif->rx_cmd_port == chn)
		len = SPRD_MAX_CMD_RXLEN;
	else
		len = SPRD_MAX_DATA_RXLEN;

	ret = pcie_rx_fill_mbuf(head, tail, num, len);
	if (ret) {
		wl_err("%s: alloc buf fail\n", __func__);
		sprdwcn_bus_list_free(chn, head, tail, num);
		head = NULL;
		tail = NULL;
		num = 0;
	}

	if (!ret)
		sprdwcn_bus_push_list(chn, head, tail, num);

	if (!work_pending(&rx_mgmt->rx_work))
		queue_work(rx_mgmt->rx_queue, &rx_mgmt->rx_work);

	return 0;
}

#ifdef CONFIG_SPRD_WLAN_NAPI
static int pcie_data_rx_handle(int chn, struct mbuf_t *head,
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

static int pcie_rx_cmd_push(int chn, struct mbuf_t **head, struct mbuf_t **tail,
			    int *num)
{
	return pcie_rx_common_push(chn, head, tail, num, SPRD_MAX_CMD_RXLEN);
}

static int pcie_rx_data_push(int chn, struct mbuf_t **head,
			     struct mbuf_t **tail, int *num)
{
	return pcie_rx_common_push(chn, head, tail, num, SPRD_MAX_DATA_RXLEN);
}

/*
 * mode:
 * 0 - suspend
 * 1 - resume
 */
static int pcie_suspend_resume_handle(int chn, int mode)
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
#ifdef ENABLE_PAM_WIFI
		if (vif->mode == SPRD_MODE_AP && sprd_pamwifi_using_ap()) {
			wl_err("ul resource not released, can not sleep!");
			return -EBUSY;
		}
#endif
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

struct mchn_ops_t sc2355_pcie_hif_ops[] = {
	/* ADD HIF ops config here */
	/* NOTE: Requested by SDIO team, pool_size MUST be 1 in RX */
	/* RX channels 1 3 5 7 9 11 13 15 */
#ifndef SC2355_RX_NO_LOOP
	INIT_INTF_SC2355(PCIE_RX_CMD_PORT, 1, 0, 0, SPRD_MAX_CMD_RXLEN,
			10, 0, 0, 0, 1, 32, pcie_rx_handle,
			pcie_rx_cmd_push, NULL, NULL),
	INIT_INTF_SC2355(PCIE_RX_DATA_PORT, 1, 0, 0, SPRD_MAX_CMD_RXLEN,
			100, 0, 0, 0, 1, 32, pcie_rx_handle,
			pcie_rx_data_push, NULL, NULL),
#ifdef PCIE_DEBUG
	INIT_INTF_SC2355(PCIE_RX_ADDR_DATA_PORT, 1, 0, 0, SPRDWL_MAX_CMD_RXLEN,
			100, 0, 0, 0, 1, 32, sprdwl_sc2355_rx_handle_for_debug,
			sprdwl_rx_data_push, NULL, NULL),
#else
	INIT_INTF_SC2355(PCIE_RX_ADDR_DATA_PORT, 1, 0, 0, SPRD_MAX_CMD_RXLEN,
			100, 0, 0, 0, 1, 32, pcie_rx_handle,
			pcie_rx_data_push, NULL, NULL),
#endif
#else
	INIT_INTF_SC2355(PCIE_RX_CMD_PORT, 1, 0, 0, SPRDWL_MAX_CMD_RXLEN,
			10, 0, 0, 0, 1, 32, sprdwl_sc2355_rx_handle_no_loop,
			sprdwl_rx_cmd_push, NULL, NULL),
	INIT_INTF_SC2355(PCIE_RX_DATA_PORT, 1, 0, 0, SPRDWL_MAX_CMD_RXLEN,
			100, 0, 0, 0, 1, 32, sprdwl_sc2355_rx_handle_no_loop,
			sprdwl_rx_data_push, NULL, NULL),
	INIT_INTF_SC2355(PCIE_RX_ADDR_DATA_PORT, 1, 0, 0, SPRDWL_MAX_CMD_RXLEN,
			100, 0, 0, 0, 1, 32, sprdwl_sc2355_rx_handle_no_loop,
			sprdwl_rx_data_push, NULL, NULL),
#endif
	/* TX channels 0 2 4 6 8 10 12 14 */
	INIT_INTF_SC2355(PCIE_TX_CMD_PORT, 1, 1, 0, SPRD_MAX_CMD_TXLEN,
			10, 0, 0, 0, 1, 32, sc2355_pcie_tx_cmd_pop_list,
			NULL, NULL, pcie_suspend_resume_handle),
	INIT_INTF_SC2355(PCIE_TX_DATA_PORT, 1, 1, 0, SPRD_MAX_CMD_TXLEN,
			300, 0, 0, 0, 1, 32, sc2355_pcie_tx_data_pop_list,
			NULL, NULL, NULL),
	INIT_INTF_SC2355(PCIE_TX_ADDR_DATA_PORT, 1, 1, 0, SPRD_MAX_CMD_TXLEN,
			300, 0, 0, 0, 1, 4, sc2355_pcie_tx_data_pop_list,
			NULL, NULL, NULL),
};

#define INTF_IS_PCIE \
	(sc2355_get_hif()->hw_type == SPRD_HW_SC2355_PCIE)

void sc2355_pcie_set_coex_bt_on_off(u8 action)
{
	struct sprd_hif *hif = sc2355_get_hif();

	hif->coex_bt_on = action;
}

inline int sc2355_pcie_tx_cmd(struct sprd_hif *hif, unsigned char *data, int len)
{
	return pcie_tx_one(hif, data, len, hif->tx_cmd_port);
}

inline int sc2355_tx_addr_trans_pcie(struct sprd_hif *hif,
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
			mbuf->phy =
			    sc2355_mm_virt_to_phys(&hif->pdev->dev, mbuf->buf,
						   mbuf->len, DMA_TO_DEVICE);

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
		ret = sc2355_pcie_push_link(hif, hif->tx_data_port,
				       (struct mbuf_t *)rx_mgmt->addr_trans_head,
				       (struct mbuf_t *)rx_mgmt->addr_trans_tail,
				       rx_mgmt->addr_trans_num,
				       sc2355_pcie_tx_cmd_pop_list);
		if (!ret) {
			rx_mgmt->addr_trans_head = NULL;
			rx_mgmt->addr_trans_tail = NULL;
			rx_mgmt->addr_trans_num = 0;
		}
	}
	wl_all("%s, trans rx buf, %d, cp2 buffer: %d\n", __func__, ret,
		skb_queue_len(&rx_mgmt->mm_entry.buffer_list));
	return ret;
}

inline void sc2355_pcie_tx_addr_trans_free(struct sprd_hif *hif)
{
	struct rx_mgmt *rx_mgmt = (struct rx_mgmt *)hif->rx_mgmt;

	sc2355_pcie_tx_cmd_pop_list(hif->tx_data_port,
			       (struct mbuf_t *)rx_mgmt->addr_trans_head,
			       (struct mbuf_t *)rx_mgmt->addr_trans_tail,
			       rx_mgmt->addr_trans_num);

	rx_mgmt->addr_trans_head = NULL;
	rx_mgmt->addr_trans_tail = NULL;
	rx_mgmt->addr_trans_num = 0;
}
static inline void sprdwl_mbuf_list_free(struct sprd_hif *hif,
					 struct mbuf_t *head,
					 struct mbuf_t *tail,
					 int count)
{
	int i;
	struct mbuf_t *mbuf_pos = head;

	for (i = 0; i < count && mbuf_pos; i++) {
		if (mbuf_pos->buf) {
			sc2355_mm_phys_to_virt(&hif->pdev->dev,
					mbuf_pos->phy, mbuf_pos->len,
					DMA_TO_DEVICE, false);
			kfree(mbuf_pos->buf);
			mbuf_pos->phy = 0;
			mbuf_pos->buf = 0;
			mbuf_pos->len = 0;
		}
		mbuf_pos = mbuf_pos->next;
	}
	sprdwcn_bus_list_free(hif->tx_data_port, head, tail, count);
}

int sc2355_pcie_hif_tx_list(struct sprd_hif *hif,
		       struct list_head *tx_list,
		       struct list_head *tx_list_head,
		       int tx_count, int ac_index, u8 coex_bt_on)
{
	int ret = 0, i = 0, j = PCIE_TX_NUM;
	int pcie_count = 0, cnt = 0, num = 0, k = 0;
	struct sprd_msg *msg_pos;
	struct pcie_addr_buffer *addr_buffer = NULL;
	struct tx_mgmt *tx_mgmt;
	struct mbuf_t *head = NULL, *tail = NULL, *mbuf_pos;
	struct list_head *pos, *tx_list_tail, *tx_head = NULL;
	struct tx_msdu_dscr *dscr;
	int print_len = 0, pcie_count_save = 0;
#if defined(MORE_DEBUG)
	unsigned long tx_bytes = 0;
#endif

	wl_all("%s:%d tx_count is %d\n", __func__, __LINE__, tx_count);

	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;

	if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
		cnt = tx_count;
		while (cnt > PCIE_TX_NUM) {
			++num;
			cnt -= PCIE_TX_NUM;
		}
		pcie_count_save = pcie_count = num + 1;
		ret = sprdwcn_bus_list_alloc(hif->tx_data_port, &head, &tail,
				&pcie_count); //port is 6
	} else {
		ret = sprdwcn_bus_list_alloc(hif->tx_data_port, &head, &tail,
					&tx_count); //port is 9
	}
	if (ret != 0 || head == NULL || tail == NULL || pcie_count == 0) {
		wl_err("%s:%d mbuf link alloc fail\n", __func__, __LINE__);
		return -1;
	}

	if (pcie_count_save != pcie_count) {
		wl_err("%s, %d error!mbuf not enough%d\n",
		       __func__, __LINE__, (pcie_count_save - pcie_count));
		sprdwl_mbuf_list_free(hif, head, tail, pcie_count);
		return -1;
	}

	mbuf_pos = head;
	if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
		for (i = 0; i < pcie_count && mbuf_pos; i++) {
			/* To prevent the mbuf_pos->buf not NULL case */
			mbuf_pos->buf = NULL;
			mbuf_pos = mbuf_pos->next;
		}
		mbuf_pos = head;
		if (pcie_count > 1) {
			addr_buffer =
			pcie_set_addr_to_mbuf(tx_mgmt,
				mbuf_pos, PCIE_TX_NUM);
		} else {
			addr_buffer =
			pcie_set_addr_to_mbuf(tx_mgmt,
				mbuf_pos, tx_count);
		}
		if (addr_buffer == NULL) {
			wl_err("%s:%d alloc pcie addr buf fail\n",
			       __func__, __LINE__);
			sprdwl_mbuf_list_free(hif, head, tail, pcie_count);
			return -1;
		}

	}

	i = 0;
	list_for_each(pos, tx_list) {
		msg_pos = list_entry(pos, struct sprd_msg, list);
		sc2355_tcp_ack_move_msg(hif->priv, msg_pos);
		if (tx_head == NULL)
			tx_head = pos;

/*TODO*/
		if (msg_pos->len > 200)
			print_len = 200;
		else
			print_len = msg_pos->len;
		print_hex_dump_debug("tx to cp2 data: ", DUMP_PREFIX_OFFSET,
				     16, 1, msg_pos->tran_data, print_len, 0);
#if defined(MORE_DEBUG)
		tx_bytes += msg_pos->skb->len;
#endif
		if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
			if (pcie_count > 1 && num > 0 && i >= j) {
				if (--num == 0) {
					if (cnt > 0) {
						mbuf_pos->phy =
							sc2355_mm_virt_to_phys(&hif->pdev->dev,
									       mbuf_pos->buf,
									       mbuf_pos->len,
									       DMA_TO_DEVICE);
						print_hex_dump_debug("tx to addr trans: ",
							DUMP_PREFIX_OFFSET, 16, 1,
							mbuf_pos->buf, mbuf_pos->len, 0);
						mbuf_pos = mbuf_pos->next;
						addr_buffer =
						pcie_set_addr_to_mbuf(
						tx_mgmt, mbuf_pos, cnt);
					} else {
						wl_err("%s: cnt %d\n", __func__, cnt);
					}
				} else {
					/*if data num greater than PCIE_TX_NUM,
					 *alloc another pcie addr buf
					 */
					j += PCIE_TX_NUM;
					mbuf_pos->phy =
						sc2355_mm_virt_to_phys(&hif->pdev->dev,
								       mbuf_pos->buf,
								       mbuf_pos->len,
								       DMA_TO_DEVICE);
						print_hex_dump_debug("tx to addr trans: ",
							DUMP_PREFIX_OFFSET, 16, 1,
							mbuf_pos->buf, mbuf_pos->len, 0);
					mbuf_pos = mbuf_pos->next;
					addr_buffer =
						pcie_set_addr_to_mbuf(
						tx_mgmt, mbuf_pos, PCIE_TX_NUM);
				}
				if (addr_buffer == NULL) {
					wl_err("%s:%d alloc pcie addr buf fail\n",
					       __func__, __LINE__);
					sprdwl_mbuf_list_free(hif, head, tail, pcie_count);
					return -1;
				}

				k = 0;
			}
			if (msg_pos->skb)
				pcie_skb_to_tx_buf(hif, msg_pos);
			wl_all("debug pcie addr: 0x%lx\n", msg_pos->pcie_addr);
			memcpy(&addr_buffer->pcie_addr[k],
			       &msg_pos->pcie_addr, SPRD_PHYS_LEN);
			dscr = (struct tx_msdu_dscr *)(msg_pos->tran_data + MSDU_DSCR_RSVD);
			addr_buffer->common.interface = dscr->common.interface;

			k++;
		} else {
			mbuf_pos->buf = msg_pos->tran_data - hif->hif_offset;
			mbuf_pos->len = msg_pos->len;
			mbuf_pos = mbuf_pos->next;
		}
		if (++i == tx_count)
			break;
	}

	mbuf_pos->phy =
		sc2355_mm_virt_to_phys(&hif->pdev->dev, mbuf_pos->buf,
				mbuf_pos->len, DMA_TO_DEVICE);
	print_hex_dump_debug("tx to addr trans: ", DUMP_PREFIX_OFFSET,
			     16, 1, mbuf_pos->buf, mbuf_pos->len, 0);

	tx_list_tail = pos;

	if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
		sprdwl_list_cut_to_free_list(tx_list_head,
			     tx_list, tx_list_tail,
			     tx_count);
		hif->mbuf_head = (void *)head;
		hif->mbuf_tail = (void *)tail;
		hif->mbuf_num = pcie_count;

		if (hif->mbuf_head) {
			/*ret = mchn_push_link(9, head, tail, pcie_count);*/
			/*edma sync function*/
			ret = sc2355_pcie_push_link(hif, hif->tx_data_port,
					       (struct mbuf_t *)hif->mbuf_head,
					       (struct mbuf_t *)hif->mbuf_tail,
					       hif->mbuf_num,
					       sc2355_pcie_tx_data_pop_list);
			if (ret != 0) {
				/*wl_err("%s: push link fail\n", __func__); */
				hif->pushfail_count++;
				sprdwl_list_cut_to_send_list(tx_head,
								 tx_list_tail,
								 tx_count);
				if (tx_list_tail)
					sprdwl_mbuf_list_free(hif, head, tail, pcie_count);
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

	/* We should refill mbuf in pcie mode */
	if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
		if (hif->rx_cmd_port == chn)
			len = SPRD_MAX_CMD_RXLEN;
		else
			len = SPRD_MAX_DATA_RXLEN;

		ret = pcie_rx_fill_mbuf(head, tail, num, len);
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

void sc2355_pcie_handle_pop_list(void *data)
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
		msg_pos->skb = NULL;
		mbuf_pos = mbuf_pos->next;
	}

	spin_lock_bh(&tx_mgmt->tx_list_qos_pool.freelock);
	list_splice_tail(&tmp_list, &msg_pos->msglist->freelist);
	spin_unlock_bh(&tx_mgmt->tx_list_qos_pool.freelock);
	sprdwcn_bus_list_free(pop->chn, pop->head, pop->tail, pop->num);
	pcie_mbufpop += pop->num;
}

int sc2355_pcie_add_topop_list(int chn, struct mbuf_t *head,
			  struct mbuf_t *tail, int num)
{
	struct sprd_hif *hif = sc2355_get_hif();
	struct sprd_work *misc_work;
	struct pop_work pop_work;
	u8 *data = NULL;

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
	misc_work->hw_type = SPRD_HW_SC2355_PCIE;
	data = misc_work->data;
	memcpy(data, &pop_work, sizeof(struct pop_work));

	sprd_queue_work(hif->priv, misc_work);
	return 0;
}

/*call back func for HIF pop_link*/
int sc2355_pcie_tx_data_pop_list(int channel, struct mbuf_t *head,
			    struct mbuf_t *tail, int num)
{
	struct mbuf_t *mbuf_pos = NULL;
#if defined(MORE_DEBUG)
	struct sprd_msg *msg_head;
#endif
	struct sprd_hif *hif = sc2355_get_hif();

	wl_all("%s channel: %d, head: %p, tail: %p num: %d\n",
		__func__, channel, head, tail, num);

	if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
		int tmp_num = num;

		/* FIXME: Temp solution, addr node pos hard to sync dma */
		for (mbuf_pos = head; mbuf_pos != NULL;
		     mbuf_pos = mbuf_pos->next) {
			sc2355_mm_phys_to_virt(&hif->pdev->dev, mbuf_pos->phy,
					       mbuf_pos->len, DMA_TO_DEVICE,
					       false);
			mbuf_pos->phy = 0;
			kfree(mbuf_pos->buf);
			mbuf_pos->buf = NULL;
			if (--tmp_num == 0)
				break;
		}
		sprdwcn_bus_list_free(channel, head, tail, num);
		wl_all("%s:%d free : %d msg buf\n", __func__, __LINE__, num);
		return 0;
	}
#if defined(MORE_DEBUG)
	msg_head = GET_MSG_BUF(head);
	/*show packet average sent time, unit: ns*/
	pcie_get_tx_avg_time(hif, msg_head->tx_start_time);
#endif

	sc2355_dequeue_data_list(head, num);
	wl_all("%s:%d free : %d msg buf\n", __func__, __LINE__, num);
	sprdwcn_bus_list_free(channel, head, tail, num);

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
		wl_err("%s: msg_buf %p not in to free list, pcie_addr=0x%lx\n",
			__func__, msg_buf, msg_buf->pcie_addr);
		spin_unlock_irqrestore(&tx_msg->xmit_msg_list.free_lock,
				       lockflag_txc);
		return -1;
	}
	atomic_sub(1, &tx_msg->xmit_msg_list.free_num);
	list_del(&msg_buf->list);
	spin_unlock_irqrestore(&tx_msg->xmit_msg_list.free_lock, lockflag_txc);
	if (msg_buf->node)
		pcie_free_tx_buf(msg_buf->node);
	if (msg_buf->skb)
		dev_kfree_skb(msg_buf->skb);

	msg_buf->node = NULL;
	msg_buf->skb = NULL;
	msg_buf->data = NULL;
	msg_buf->tran_data = NULL;
	msg_buf->len = 0;

	sprd_free_msg(msg_buf, msg_buf->msglist);
	return 0;
}

/*free PCIe data when receive txc event from cp*/
int sc2355_tx_free_pcie_data(unsigned char *data)
{
	int i;
	struct sprd_hif *hif = sc2355_get_hif();
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	void *data_addr_ptr;
	unsigned long pcie_addr;
	unsigned short data_num;
	struct txc_addr_buff *txc_addr;
	unsigned char (*pos)[5];
	struct sprd_msg *msg, *last_msg = NULL;
#if defined(MORE_DEBUG)
	unsigned long tx_start_time = 0;
#endif
	unsigned char *tmp;
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
	wl_info("%s, total free num:%lu; total tx_num:%lu\n", __func__,
		 tx_mgmt->txc_num, tx_mgmt->tx_num);


	if (printk_timed_ratelimit(&caller_jiffies, 1000)) {
		wl_info("%s, free_num: %d, to_free_list num: %d\n",
			__func__, data_num,
			atomic_read(&tx_mgmt->xmit_msg_list.free_num));
	}

	pos = (unsigned char (*)[5])(txc_addr + 1);
	for (i = 0; i < data_num; i++, pos++) {
		pcie_addr = 0;
		memcpy(&pcie_addr, pos, SPRD_PHYS_LEN);
		pcie_addr -= 0x10;	//Workaround for HW issue

		found = false;
		spin_lock_irqsave(&tx_mgmt->xmit_msg_list.free_lock, lockflag_txc);
		list_for_each_entry(pos_msg, &tx_mgmt->xmit_msg_list.to_free_list,
					list) {
			if (pos_msg->pcie_addr == pcie_addr) {
				found = true;
				break;
			}
		}
		spin_unlock_irqrestore(&tx_mgmt->xmit_msg_list.free_lock, lockflag_txc);

		if (!found) {
			wl_err("%s: pcie_addr 0x%lx not in to free list\n",
						__func__, pcie_addr);
			continue;
		}

		wl_all("%s: pcie_addr=0x%lx", __func__, pcie_addr);
		data_addr_ptr =
		    sc2355_mm_phys_to_virt(&hif->pdev->dev, pcie_addr,
					   SPRD_MAX_DATA_TXLEN, DMA_TO_DEVICE,
					   false);
		msg = NULL;
		RESTORE_ADDR(msg, data_addr_ptr, sizeof(msg));
		if (msg == NULL) {
			wl_info("%s: msg null!\n", __func__);
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
	pcie_get_tx_avg_time(hif, tx_start_time);
#endif

	return 0;
}

int sc2355_pcie_tx_cmd_pop_list(int channel, struct mbuf_t *head,
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

			if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
				sc2355_mm_phys_to_virt(&hif->pdev->dev,
						       pos->phy, pos->len,
						       DMA_TO_DEVICE, false);
				pos->phy = 0;
			}
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
	wl_info("tx_cmd_pop num: %d,cmd_poped=%d, cmd_send=%d\n",
		num, tx_mgmt->cmd_poped, tx_mgmt->cmd_send);
	sprdwcn_bus_list_free(channel, head, tail, num);

	return 0;
}

int sc2355_pcie_push_link(struct sprd_hif *hif, int chn,
		     struct mbuf_t *head, struct mbuf_t *tail, int num,
		     int (*pop)(int, struct mbuf_t *, struct mbuf_t *, int))
{
	int ret = 0;
	unsigned long time = 0;
	struct mbuf_t *pos = head;
	int i = 0;
	unsigned int low = 0xffffffff;
	unsigned int low1 = 0;

	for (i = 0; i < num; i++) {
		if ((memcmp(&pos->phy, &low, 4) == 0) ||
		    (memcmp(&pos->phy, &low1, 4) == 0)) {
			wl_err
			    ("err phy address: %lx\n, err virt address: %p\n, err port: %d\n",
			     pos->phy, pos->buf, chn);
			return -ENOMEM;
		}
		if (i == num && pos != tail)
			wl_info("num of head to tail is not match\n");

		pos = pos->next;
	}
	time = jiffies;
	ret = sprdwcn_bus_push_list(chn, head, tail, num);
	time = jiffies - time;

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
void sc2355_pcie_event_sta_lut(struct sprd_vif *vif, u8 *data, u16 len)
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
#ifdef ENABLE_PAM_WIFI
              if(sprd_pamwifi_supported(hif->pdev)){
		       hif->peer_entry[i].vif = NULL;
			memset(hif->peer_entry[i].tx.sa, 0x0, ETHER_ADDR_LEN);
			if(vif->mode == SPRD_MODE_AP && sta_lut->sta_lut_index > 5) {
				sprd_pamwifi_update_router_table(vif->priv, sta_lut, vif->mode, 0, 0);
			}
              }
#endif
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
#ifdef ENABLE_PAM_WIFI
		if(sprd_pamwifi_supported(hif->pdev)){
			hif->peer_entry[i].vif = vif;
			if(vif->mode == SPRD_MODE_AP && sta_lut->sta_lut_index > 5) {
				if (!vif->ndev)
					ether_addr_copy(hif->peer_entry[i].tx.sa, vif->wdev.address);
				else
					ether_addr_copy(hif->peer_entry[i].tx.sa, vif->ndev->dev_addr);
				sprd_pamwifi_update_router_table(vif->priv, sta_lut, vif->mode, 0, 1);
			}
		}
#endif
		break;
	default:
		break;
	}
}

unsigned short sc2355_pcie_get_data_csum(void *entry, void *data)
{
	unsigned short csum = 0;
#if 0
	struct rx_mh_desc *mh_desc = (struct rx_mh_desc *)data;
	struct sprd_hif *hif = (struct sprd_hif *)entry;

	if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
		if (mh_desc->tcp_checksum_en)
			csum = mh_desc->tcp_hw_checksum;
	}
#endif

	return csum;
}

void pcie_mm_fill_all_buffer(struct sprd_hif *hif)
{
	struct rx_mgmt *rx_mgmt =
	    (struct rx_mgmt *)((struct sprd_hif *)hif)->rx_mgmt;
	struct mem_mgmt *mm_entry = &rx_mgmt->mm_entry;
	int num = SPRD_MAX_MH_BUF - skb_queue_len(&mm_entry->buffer_list);
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

void sc2355_pcie_handle_tx_return(struct sprd_hif *hif,
				  struct sprd_msg_list *list,
				  int send_num, int ret)
{
	if (ret == -2) {
		atomic_sub(send_num, &list->ref);
		wl_info("%s,%d,debug: %d\n", __func__, __LINE__, atomic_read(&list->ref));

		usleep_range(100, 200);
		return;
	} else if (ret < 0) {
		usleep_range(100, 200);
		return;
	} else {
		printk_ratelimited("%s,%d,debug: %d\n", __func__,
			__LINE__, atomic_read(&list->ref));
	}
}

void sc2355_pcie_rx_work_queue(struct work_struct *work)
{
	struct sprd_msg *msg;
	struct sprd_priv *priv;
	struct rx_mgmt *rx_mgmt;
	struct sprd_hif *hif;
	int print_len;

	rx_mgmt = container_of(work, struct rx_mgmt, rx_work);
	hif = rx_mgmt->hif;
	priv = hif->priv;

	if (!hif->exit && !sprd_peek_msg(&rx_mgmt->rx_list))
		sc2355_rx_process(rx_mgmt, NULL);

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
		if (msg->tran_data)
			sc2355_free_data(msg->tran_data, msg->buffer_type);
		sprd_dequeue_msg(msg, &rx_mgmt->rx_list);
	}
}

int sc2355_pcie_fc_get_send_num(struct sprd_hif *hif,
			   enum sprd_mode mode, int data_num)
{
	int free_num = 0;
	struct tx_mgmt *tx_mgmt = hif->tx_mgmt;
	static unsigned long caller_jiffies;
	/*send all data in buff with PCIe interface*/
	unsigned int tx_buf_max = get_max_fw_tx_dscr() >
				  pcie_get_tx_buf_num() ?
				  pcie_get_tx_buf_num() :
				  get_max_fw_tx_dscr();

	tx_buf_max -= PCIE_TX_BUF_PROTECT_NUM; //workround cp2 invalid pcie address

	if (data_num <= 0 || mode == SPRD_MODE_NONE)
		return 0;

	free_num = atomic_read(&tx_mgmt->xmit_msg_list.free_num);
	if (printk_timed_ratelimit(&caller_jiffies, 1000)) {
		wl_info("%s, free_num=%d, data_num=%d\n", __func__,
			free_num, data_num);
		if (list_empty(&tx_mgmt->xmit_msg_list.to_free_list))
			wl_info("%s: to free list empty\n", __func__);
	}

	if ((free_num + data_num) >= tx_buf_max) {
		printk_ratelimited("%s, free_num=%d, data_num=%d\n", __func__,
				   free_num, data_num);
		return (tx_buf_max - free_num);
	} else {
		return data_num;
	}

}

int sc2355_pcie_fc_test_send_num(struct sprd_hif *hif,
			    enum sprd_mode mode, int data_num)
{
	int free_num = 0;
	struct tx_mgmt *tx_mgmt = hif->tx_mgmt;
	static unsigned long caller_jiffies;
	/*send all data in buff with PCIe interface, TODO*/
	unsigned int tx_buf_max = get_max_fw_tx_dscr() >
				  pcie_get_tx_buf_num() ?
				  pcie_get_tx_buf_num() :
				  get_max_fw_tx_dscr();

	tx_buf_max -= PCIE_TX_BUF_PROTECT_NUM; //workround cp2 invalid pcie address

	if (data_num <= 0 || mode == SPRD_MODE_NONE)
		return 0;

	free_num = atomic_read(&tx_mgmt->xmit_msg_list.free_num);
	if (printk_timed_ratelimit(&caller_jiffies, 1000)) {
		wl_info("%s,%d free_num=%d, data_num=%d\n", __func__,
			__LINE__, free_num, data_num);
		if (list_empty(&tx_mgmt->xmit_msg_list.to_free_list))
			wl_info("%s: to free list empty\n", __func__);
	}

	if ((free_num + data_num) >= tx_buf_max) {
		printk_ratelimited("%s,%d free_num=%d, data_num=%d\n",
				   __func__, __LINE__, free_num, data_num);
		return (tx_buf_max - free_num);
	} else {
		return data_num;
	}

}

int pcie_init(struct sprd_hif *hif)
{
	u8 i;
	int ret = -EINVAL;

	hif->hw_type = SPRD_HW_SC2355_PCIE;
	ret = dma_coerce_mask_and_coherent(&hif->pdev->dev, DMA_BIT_MASK(39));

	if (ret) {
                wl_err("%s dma_coerce_mask_and_coherent fail.\n", __func__);
                return ret;
        }

	for (i = 0; i < MAX_LUT_NUM; i++)
		hif->peer_entry[i].ctx_id = 0xff;


	hif->hif_offset = 0;
	hif->dscr_rsvd = MSDU_DSCR_RSVD;
	hif->rx_cmd_port = PCIE_RX_CMD_PORT;
	hif->rx_data_port = PCIE_RX_ADDR_DATA_PORT;
	hif->tx_cmd_port = PCIE_TX_CMD_PORT;
	hif->tx_data_port = PCIE_TX_ADDR_DATA_PORT;

	if (pcie_buf_init()) {
		ret = -ENOMEM;
		wl_err("%s txrx buf init failed.\n", __func__);
		return ret;
	}
	adjust_max_fw_tx_dscr("max_fw_tx_dscr=1024", strlen("max_fw_tx_dscr="));

	ret = sc2355_rx_init(hif);
	if (ret) {
		wl_err("%s rx init failed: %d\n", __func__, ret);
		goto err_rx_init;
	}

	ret = sc2355_tx_init(hif);
	if (ret) {
		wl_err("%s tx_list init failed\n", __func__);
		goto err_tx_init;
	}

	if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
		sc2355_hif.mchn_ops = sc2355_pcie_hif_ops;
		sc2355_hif.max_num =
		    sizeof(sc2355_pcie_hif_ops) / sizeof(struct mchn_ops_t);
	}
	if (hif->hw_type == SPRD_HW_SC2355_PCIE)
		hif->feature = NETIF_F_SG;
	else
		hif->feature = NETIF_F_CSUM_MASK | NETIF_F_SG;

	return 0;

err_tx_init:
	sc2355_rx_deinit(hif);
err_rx_init:
	pcie_buf_deinit();

	return ret;
}

int pcie_post_init(struct sprd_hif *hif)
{
	int ret = -EINVAL, chn = 0;

	sc2355_hif.hif = (void *)hif;
	sc2355_hif.max_num =
		sizeof(sc2355_pcie_hif_ops) / sizeof(struct mchn_ops_t);

	if (sc2355_hif.max_num < MAX_CHN_NUM) {
		wl_info("%s: register %d ops\n", __func__, sc2355_hif.max_num);

		for (chn = 0; chn < sc2355_hif.max_num; chn++) {
			ret = sprdwcn_bus_chn_init(&sc2355_hif.mchn_ops[chn]);
			if (ret < 0)
				goto err;
		}

		hif->fw_awake = 1;
		hif->fw_power_down = 0;
		INIT_LIST_HEAD(&pcie_rx_mbuf_list);
	}

	return 0;

err:
	wl_err("%s: unregister %d ops\n", __func__, sc2355_hif.max_num);

	for (; chn > 0; chn--)
		sprdwcn_bus_chn_deinit(&sc2355_hif.mchn_ops[chn]);
	sc2355_hif.max_num = 0;

	return ret;
}

void pcie_post_deinit(struct sprd_hif *hif)
{
	int chn = 0;
	struct pcie_rx_mbuf *rx_mbuf = NULL, *rx_mbuf_pos = NULL;
	struct tx_mgmt *tx_mgmt = NULL;

	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	for (chn = 0; chn < sc2355_hif.max_num; chn++)
		sprdwcn_bus_chn_deinit(&sc2355_hif.mchn_ops[chn]);

	list_for_each_entry_safe(rx_mbuf, rx_mbuf_pos, &pcie_rx_mbuf_list, list) {
		sc2355_free_data(rx_mbuf->buf, SPRD_DEFRAG_MEM);
		list_del(&rx_mbuf->list);
		kfree(rx_mbuf);
		rx_mbuf = NULL;
	}
	sc2355_hif.hif = NULL;
	sc2355_hif.max_num = 0;

	wl_info("reinit hang(%d) thermal(%d) suspend(%d) status to default\n",
		tx_mgmt->hang_recovery_status, tx_mgmt->thermal_status,
		hif->suspend_mode);
	tx_mgmt->hang_recovery_status = HANG_RECOVERY_END;
	tx_mgmt->thermal_status = THERMAL_TX_RESUME;
	hif->suspend_mode = SPRD_PS_RESUMED;

}
void pcie_deinit(struct sprd_hif *hif)
{
	sc2355_tx_deinit(hif);
	sc2355_rx_deinit(hif);
	pcie_buf_deinit();
}

static struct sprd_hif_ops sc2355_pcie_ops = {
	.init = pcie_init,
	.deinit = pcie_deinit,
	.post_init = pcie_post_init,
	.post_deinit = pcie_post_deinit,
	.sync_version = sc2355_sync_version,
	.download_hw_param = sc2355_download_hw_param,
	.reset = sc2355_reset,
	.fill_all_buffer = pcie_mm_fill_all_buffer,
	.tx_special_data = sc2355_tx_special_data,
	.free_msg_content = pcie_free_msg_content,
	.tx_addr_trans = sc2355_tx_addr_trans_pcie,
	.tx_flush = sc2355_tx_flush,
};

extern struct sprd_chip_ops sc2355_chip_ops;
int sc2355_pcie_probe(struct platform_device *pdev)
{
	return sprd_iface_probe(pdev, &sc2355_pcie_ops, &sc2355_chip_ops);
}
#if 0
static void pcie_remove(struct platform_device *pdev)
{
	sprd_iface_remove(pdev);
}

static const struct of_device_id sc2355_pcie_of_match[] = {
	{.compatible = "sprd,sc2355-pcie-wifi",},
	{}
};

MODULE_DEVICE_TABLE(of, sc2355_pcie_of_match);

static struct platform_driver sc2355_pcie_driver = {
	.probe = pcie_probe,
	.remove = pcie_remove,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "wlan",
		   .of_match_table = sc2355_pcie_of_match,
	}
};

module_platform_driver(sc2355_pcie_driver);

MODULE_DESCRIPTION("Spreadtrum SC2355 PCIE Initialization");
MODULE_AUTHOR("Spreadtrum WCN Division");
MODULE_LICENSE("GPL");
#endif
