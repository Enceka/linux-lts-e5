/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#include <net/ip.h>

#include "common/chip_ops.h"
#include "common/common.h"
#include "cmdevt.h"
#include "qos.h"
#include "rx.h"
#include "tx.h"
#include "txrx.h"
#include "cpu_performance.h"
#include "wcn_bus.h"

#define MAX_FW_TX_DSCR	(1024)

static void tx_dequeue_cmd_buf(struct sprd_msg *msg, struct sprd_msg_list *list)
{
	spin_lock_bh(&list->busylock);
	list_del(&msg->list);
	spin_unlock_bh(&list->busylock);

	spin_lock_bh(&list->complock);
	list_add_tail(&msg->list, &list->cmd_to_free);
	spin_unlock_bh(&list->complock);
}

static inline void tx_enqueue_data_msg(struct sprd_msg *msg, struct sprd_hif *hif)
{
	struct tx_msdu_dscr *dscr = (struct tx_msdu_dscr *)msg->tran_data;

	spin_lock_bh(&msg->data_list->p_lock);
	/*to make sure ARP/TDLS/preauth can be tx ASAP */
	if (hif->hw_type == SPRD_HW_SC2355_PCIE ||
		hif->hw_type == SPRD_HW_SC2355_SIPC) {
		list_add_tail(&msg->list, &msg->data_list->head_list);
	} else {
		if (dscr->tx_ctrl.sw_rate == 1)
			list_add(&msg->list, &msg->data_list->head_list);
		else
			list_add_tail(&msg->list, &msg->data_list->head_list);
	}
	atomic_inc(&msg->data_list->l_num);
	spin_unlock_bh(&msg->data_list->p_lock);
}

static inline void tx_dequeue_data_msg(struct sprd_hif *hif, struct sprd_msg *msg)
{
	dev_kfree_skb(msg->skb);
	msg->skb = NULL;
	if (hif->ops->free_msg_content)
		hif->ops->free_msg_content(msg);
	list_del(&msg->list);
	sprd_free_msg(msg, msg->msglist);
}

static void tx_flush_data_txlist(struct tx_mgmt *tx_mgmt)
{
	enum sprd_mode mode;
	struct list_head *data_list;
	int cnt = 0;
	struct sprd_priv *priv = tx_mgmt->hif->priv;

	for (mode = SPRD_MODE_STATION; mode < SPRD_MODE_MAX; mode++) {
		if (atomic_read(&tx_mgmt->tx_list[mode]->mode_list_num) == 0)
			continue;
		sc2355_flush_mode_txlist(tx_mgmt, mode);
	}

	sc2355_flush_tosendlist(tx_mgmt);
	data_list = &tx_mgmt->xmit_msg_list.to_free_list;
	/*wait until data list sent completely and freed by HIF */
	wl_err("%s check if data freed complete start\n", __func__);
	while (!list_empty(data_list) && (cnt < 1000)) {
		if (priv->hif.hw_type == SPRD_HW_SC2355_SIPC || (
		    priv->hif.hw_type == SPRD_HW_SC2355_PCIE &&
		    sprdwcn_bus_get_status() == WCN_BUS_DOWN)) {
			struct sprd_msg *pos_buf, *temp_buf;
			unsigned long lockflag_txfree = 0;

			spin_lock_irqsave(&tx_mgmt->xmit_msg_list.free_lock,
					  lockflag_txfree);
			list_for_each_entry_safe(pos_buf, temp_buf,
						 data_list, list) {
				tx_dequeue_data_msg(tx_mgmt->hif, pos_buf);
				atomic_dec(&tx_mgmt->xmit_msg_list.free_num);
			}
			spin_unlock_irqrestore(&tx_mgmt->xmit_msg_list.free_lock,
					       lockflag_txfree);
			goto out;
		}
		usleep_range(2500, 3000);
		cnt++;
	}
out:
	wl_err("%s check if data freed complete end\n", __func__);
}

static void tx_init_xmit_list(struct tx_mgmt *tx_mgmt)
{
	INIT_LIST_HEAD(&tx_mgmt->xmit_msg_list.to_send_list);
	INIT_LIST_HEAD(&tx_mgmt->xmit_msg_list.to_free_list);
	spin_lock_init(&tx_mgmt->xmit_msg_list.send_lock);
	spin_lock_init(&tx_mgmt->xmit_msg_list.free_lock);
}

static int
tx_add_xmit_list_tail(struct tx_mgmt *tx_mgmt,
		      struct sprd_qos_peer_list *p_list, int add_num)
{
	struct list_head *pos_list = NULL, *n_list;
	struct list_head temp_list;
	int num = 0;

	if (add_num == 0 || list_empty(&p_list->head_list))
		return -ENOMEM;
	spin_lock_bh(&p_list->p_lock);
	list_for_each_safe(pos_list, n_list, &p_list->head_list) {
		num++;
		if (num == add_num)
			break;
	}
	if (num != add_num)
		wl_err("%s, %d, error! add_num:%d, num:%d\n",
		       __func__, __LINE__, add_num, num);
	INIT_LIST_HEAD(&temp_list);
	list_cut_position(&temp_list, &p_list->head_list, pos_list);
	list_splice_tail(&temp_list, &tx_mgmt->xmit_msg_list.to_send_list);
	if (list_empty(&p_list->head_list))
		INIT_LIST_HEAD(&p_list->head_list);
	spin_unlock_bh(&p_list->p_lock);
	wl_all("%s,%d,q_num%d,tosend_num%d\n", __func__, __LINE__,
		 sc2355_qos_get_list_num(&p_list->head_list),
		 sc2355_qos_get_list_num(&tx_mgmt->xmit_msg_list.to_send_list));

	return 0;
}

static void tx_flush_cmd_txlist(struct sprd_msg_list *list)
{
	struct sprd_msg *msg = NULL, *pos_msg = NULL;
	int cnt = 0;

	/*wait until cmd list sent completely and freed by HIF */
	while (!list_empty(&list->cmd_to_free) && (cnt < 1000)) {
		wl_all("%s cmd not yet transmited", __func__);
		usleep_range(2500, 3000);
		cnt++;
	}

	if (!list_empty(&list->cmd_to_free)) {
		wl_err("%s flush cmd_to_free\n", __func__);
		list_for_each_entry_safe(pos_msg, msg,
				 &list->cmd_to_free, list) {
			kfree(pos_msg->tran_data);
			pos_msg->tran_data = NULL;
			sc2355_free_cmd_buf(pos_msg, list);
		}
	}

	while ((msg = sprd_peek_msg(list))) {
		if (msg->skb) {
			dev_kfree_skb(msg->skb);
			msg->skb = NULL;
		} else {
			kfree(msg->tran_data);
			msg->tran_data = NULL;
		}
		sprd_dequeue_msg(msg, list);
		continue;
	}
}

static int tx_cmd(struct sprd_hif *hif, struct sprd_msg_list *list)
{
	int ret = 0;
	struct sprd_msg *msg;
	struct tx_mgmt *tx_mgmt;
	struct sprd_cmd_hdr *hdr;
	u8 mode, i;
	u32 mstime;
	const char *cmd_str;

	struct sprd_priv *priv = hif->priv;
	struct sprd_cmd *cmd = &priv->cmd;

	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	while ((msg = sprd_peek_msg(list))) {
		if (unlikely(hif->exit)) {
			kfree(msg->tran_data);
			msg->tran_data = NULL;
			sprd_dequeue_msg(msg, list);
			continue;
		}

		hdr = (struct sprd_cmd_hdr *)(msg->tran_data + hif->hif_offset);
		cmd_str = sc2355_cmdevt_cmd2str(hdr->cmd_id);
		mstime = le32_to_cpu(hdr->mstime);

		if (time_after(jiffies, msg->timeout)) {
			tx_mgmt->drop_cmd_cnt++;
			wl_err("tx drop cmd msg,dropcnt:%lu, [%u]ctx_id %d send[%s]\n",
			       tx_mgmt->drop_cmd_cnt, mstime, hdr->common.mode, cmd_str);
			kfree(msg->tran_data);
			msg->tran_data = NULL;
			sprd_dequeue_msg(msg, list);
			continue;
		}
		tx_dequeue_cmd_buf(msg, list);
		tx_mgmt->cmd_send++;
		mode = hdr->common.mode;

		if (hif->hw_type == SPRD_HW_SC2355_PCIE)
			ret = sc2355_pcie_tx_cmd(hif, (unsigned char *)msg->tran_data,
						msg->len);
		else if (hif->hw_type == SPRD_HW_SC2355_SIPC)
			ret = sc2355_sipc_tx_cmd(hif, (unsigned char *)msg->tran_data,
						msg->len);
		else
			ret = sc2355_tx_cmd(hif, (unsigned char *)msg->tran_data,
						msg->len);
		if (ret && hif->hw_type == SPRD_HW_SC2355_SIPC) {
			wl_err("%s, %d. tx cmd err : %d firstly\n", __func__, __LINE__, ret);
			for (i = 0; i < 10; i++) {
				if (sprdwcn_bus_get_carddump_status() || wcn_is_assert()) {
					wl_err("%s, dump_status:%d, wcn_is_assert:%d\n", __func__,
						sprdwcn_bus_get_carddump_status(), wcn_is_assert());
					hif->cp_asserted = 1;
					complete(&cmd->completed);
					break;
				}
				msleep(50);
				ret = sc2355_sipc_tx_cmd(hif, (unsigned char *)msg->tran_data,
							msg->len);
				if (ret)
					wl_err("%s tx cmd retry %d\n", __func__, i);
				else
					break;
			}
		}
		if (ret) {
			wl_err("%s [%u]ctx_id %d send[%s] err:%d.\n", __func__,
				mstime, mode, cmd_str, ret);
			msg->tran_data = NULL;
			sc2355_free_cmd_buf(msg, list);
		}
	}

	return 0;
}

static int tx_handle_timeout(struct tx_mgmt *tx_mgmt,
			     struct sprd_msg_list *msg_list,
			     struct sprd_qos_peer_list *p_list, int ac_index)
{
	u8 mode;
	char *pinfo;
	spinlock_t *lock;
	int cnt, i, del_list_num;
	struct list_head *tx_list;
	struct sprd_msg *pos_buf, *temp_buf, *tailbuf;
	struct sprd_priv *priv = tx_mgmt->hif->priv;

	if (SPRD_AC_MAX == ac_index)
		return 0;

	tx_list = &p_list->head_list;
	lock = &p_list->p_lock;
	spin_lock_bh(lock);
	if (list_empty(tx_list)) {
		spin_unlock_bh(lock);
		return 0;
	}
	tailbuf = list_first_entry(tx_list, struct sprd_msg, list);
	spin_unlock_bh(lock);

	if (time_after(jiffies, tailbuf->timeout)) {
		mode = tailbuf->mode;
		sprd_net_flowcontrl(priv, mode, false);
		atomic_set(&msg_list->flow, 1);
		i = 0;
		spin_lock_bh(lock);
		del_list_num = TX_TIMEOUT_DROP_RATE *
		    atomic_read(&p_list->l_num) / 100;
		if (del_list_num >= atomic_read(&p_list->l_num))
			del_list_num = atomic_read(&p_list->l_num);
		wl_err("tx timeout drop num:%d, l_num:%d",
			del_list_num, atomic_read(&p_list->l_num));
		list_for_each_entry_safe(pos_buf, temp_buf, tx_list, list) {
			if (i >= del_list_num)
				break;
			wl_err("%s:%d buf->timeout\n", __func__, __LINE__);
			if (pos_buf->mode <= SPRD_MODE_AP) {
				pinfo = "STA/AP mode";
				cnt = tx_mgmt->drop_data1_cnt++;
			} else {
				pinfo = "P2P mode";
				cnt = tx_mgmt->drop_data2_cnt++;
			}
			wl_err("tx drop %s, dropcnt:%u\n", pinfo, cnt);
			tx_dequeue_data_msg(tx_mgmt->hif, pos_buf);
			atomic_dec(&tx_mgmt->tx_list[mode]->mode_list_num);
#if defined(MORE_DEBUG)
			tx_mgmt->hif->stats.tx_dropped++;
#endif
			i++;
		}
		atomic_sub(del_list_num, &p_list->l_num);
		spin_unlock_bh(lock);
		return -ENOMEM;
	}
	return 0;
}

static int tx_handle_to_send_list(struct sprd_hif *hif, enum sprd_mode mode)
{
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	struct list_head *to_send_list, tx_list_head;
	spinlock_t *t_lock;	/*to protect sc2355_qos_get_list_num */
	int tosendnum = 0, credit = 0, ret = 0;
	struct sprd_msg_list *list = &tx_mgmt->tx_list_qos_pool;
	u8 coex_bt_on = hif->coex_bt_on;

	if (!list_empty(&tx_mgmt->xmit_msg_list.to_send_list)) {
		to_send_list = &tx_mgmt->xmit_msg_list.to_send_list;
		t_lock = &tx_mgmt->xmit_msg_list.send_lock;
		spin_lock_bh(t_lock);
		tosendnum = sc2355_qos_get_list_num(to_send_list);
		spin_unlock_bh(t_lock);
		if (hif->hw_type == SPRD_HW_SC2355_PCIE)
			credit = sc2355_pcie_fc_get_send_num(hif, mode, tosendnum);
		else if (hif->hw_type == SPRD_HW_SC2355_SIPC)
			credit = sc2355_sipc_fc_get_send_num(hif, mode, tosendnum);
		else
			credit = sc2355_fc_get_send_num(hif, mode, tosendnum);
		//if (credit < tosendnum)
			//wl_err("%s, %d,error! credit:%d,tosendnum:%d\n",
			//       __func__, __LINE__, credit, tosendnum);
		if (credit <= 0)
			return -ENOMEM;
		tx_mgmt->xmit_msg_list.mode = mode;

		if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
			ret = sc2355_pcie_hif_tx_list(hif,
						to_send_list,
						&tx_list_head,
						credit, SPRD_AC_MAX, coex_bt_on);
			sc2355_pcie_handle_tx_return(hif, list, credit, ret);
		} else if (hif->hw_type == SPRD_HW_SC2355_SIPC) {
			if (tosendnum < credit)
				credit = tosendnum;

			ret = sc2355_sipc_hif_tx_list(hif,
						to_send_list,
						&tx_list_head,
						credit, SPRD_AC_MAX, coex_bt_on, mode);
			sc2355_sipc_handle_tx_return(hif, list, credit, ret);
		} else {
			ret = sc2355_hif_tx_list(hif,
						to_send_list,
						&tx_list_head,
						credit, SPRD_AC_MAX, coex_bt_on);
			sc2355_handle_tx_return(hif, list, credit, ret);
		}
		if (ret) {
			wl_err("%s, %d: tx return err!\n", __func__, __LINE__);
			tx_mgmt->xmit_msg_list.failcount++;
			if (tx_mgmt->xmit_msg_list.failcount > 50)
				sc2355_flush_tosendlist(tx_mgmt);
			return -ENOMEM;
		}
		tx_mgmt->xmit_msg_list.failcount = 0;
	}
	return 0;
}

static int tx_eachmode_data(struct sprd_hif *hif, enum sprd_mode mode)
{
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	int ret, i, j;
	struct list_head tx_list_head;
	struct qos_list *q_list;
	struct sprd_qos_peer_list *p_list;
	struct sprd_msg_list *list = &tx_mgmt->tx_list_qos_pool;
	struct qos_tx_t *tx_list = tx_mgmt->tx_list[mode];
	int send_num = 0, total = 0, min_num = 0, round_num = 0;
	int q_list_num[SPRD_AC_MAX] = { 0, 0, 0, 0 };
	int p_list_num[SPRD_AC_MAX][MAX_LUT_NUM] = { {0}, {0}, {0}, {0} };

	INIT_LIST_HEAD(&tx_list_head);
	/* first, go through all list, handle timeout msg
	 * and count each TID's tx_num and total tx_num
	 */
	for (i = 0; i < SPRD_AC_MAX; i++) {
		for (j = 0; j < MAX_LUT_NUM; j++) {
			p_list = &tx_list->q_list[i].p_list[j];
			if (atomic_read(&p_list->l_num) > 0) {
				if (tx_handle_timeout(tx_mgmt, list, p_list, i))
					wl_err("TID=%s%s%s%s, timeout!\n",
					       (i == SPRD_AC_VO) ? "VO" : "",
					       (i == SPRD_AC_VI) ? "VI" : "",
					       (i == SPRD_AC_BE) ? "BE" : "",
					       (i == SPRD_AC_BK) ? "BK" : "");
				p_list_num[i][j] = atomic_read(&p_list->l_num);
				q_list_num[i] += p_list_num[i][j];
			}
		}
		total += q_list_num[i];
		if (q_list_num[i] != 0)
			wl_all("TID%s%s%s%snum=%d, total=%d\n",
				 (i == SPRD_AC_VO) ? "VO" : "",
				 (i == SPRD_AC_VI) ? "VI" : "",
				 (i == SPRD_AC_BE) ? "BE" : "",
				 (i == SPRD_AC_BK) ? "BK" : "",
				 q_list_num[i], total);
	}
	if (hif->hw_type == SPRD_HW_SC2355_PCIE)
		send_num = sc2355_pcie_fc_test_send_num(hif, mode, total);
	else if (hif->hw_type == SPRD_HW_SC2355_SIPC)
		send_num = sc2355_sipc_fc_test_send_num(hif, mode, total);
	else
		send_num = sc2355_fc_test_send_num(hif, mode, total);
	if (total != 0 && send_num <= 0) {
		wl_err("%s, %d: _fc_ no credit!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	/* merge qos queues to to_send_list
	 * to best use of HIF interrupt
	 */
	/* case1: send_num >= total
	 * remained _fc_ num is more than remained qos data,
	 * just add all remained qos list to xmit list
	 * and send all xmit to_send_list
	 */
	if (send_num >= total) {
		for (i = 0; i < SPRD_AC_MAX; i++) {
			q_list = &tx_list->q_list[i];
			if (q_list_num[i] <= 0)
				continue;
			for (j = 0; j < MAX_LUT_NUM; j++) {
				p_list = &q_list->p_list[j];
				if (p_list_num[i][j] <= 0 ||
				    list_empty(&p_list->head_list))
					continue;
				if (tx_add_xmit_list_tail
				    (tx_mgmt, p_list, p_list_num[i][j]))
					continue;
				spin_lock_bh(&p_list->p_lock);
				if (atomic_read(&p_list->l_num)) {
					atomic_sub(p_list_num[i][j], &p_list->l_num);
					atomic_sub(p_list_num[i][j],
						   &tx_list->mode_list_num);
				}
				spin_unlock_bh(&p_list->p_lock);
				wl_all
				    ("%s, %d, mode=%d, TID=%d, lut=%d, %d add to xmit_list,"
				     "then l_num=%d, mode_list_num=%d\n",
				     __func__, __LINE__, mode, i, j,
				     p_list_num[i][j],
				     atomic_read(&p_list->l_num),
				     atomic_read(&tx_mgmt->tx_list[mode]->mode_list_num));
			}
		}
		ret = tx_handle_to_send_list(hif, mode);
		return ret;
	}

	/* case2: send_num < total
	 * vo get 87%,vi get 90%,be get remain 81%
	 */
	for (i = 0; i < SPRD_AC_MAX; i++) {
		int fp_num = 0;	/*assigned _fc_ num to qoslist */

		q_list = &tx_list->q_list[i];
		if (q_list_num[i] <= 0)
			continue;
		if (send_num <= 0)
			break;

		if (i == SPRD_AC_VO && total > q_list_num[i]) {
			round_num = send_num * get_vo_ratio() / 100;
			fp_num = min(round_num, q_list_num[i]);
		} else if ((i == SPRD_AC_VI) && (total > q_list_num[i])) {
			round_num = send_num * get_vi_ratio() / 100;
			fp_num = min(round_num, q_list_num[i]);
		} else if ((i == SPRD_AC_BE) && (total > q_list_num[i])) {
			round_num = send_num * get_be_ratio() / 100;
			fp_num = min(round_num, q_list_num[i]);
		} else {
			fp_num = send_num * q_list_num[i] / total;
		}
		if (((total - q_list_num[i]) < (send_num - fp_num)) &&
		    ((total - q_list_num[i]) > 0))
			fp_num += (send_num - fp_num - (total - q_list_num[i]));

		total -= q_list_num[i];

		wl_all("TID%s%s%s%s, credit=%d, fp_num=%d, remain=%d\n",
			 (i == SPRD_AC_VO) ? "VO" : "",
			 (i == SPRD_AC_VI) ? "VI" : "",
			 (i == SPRD_AC_BE) ? "BE" : "",
			 (i == SPRD_AC_BK) ? "BK" : "",
			 send_num, fp_num, total);

		send_num -= fp_num;
		for (j = 0; j < MAX_LUT_NUM; j++) {
			if (p_list_num[i][j] == 0)
				continue;
			round_num = p_list_num[i][j] * fp_num / q_list_num[i];
			if (fp_num > 0 && round_num == 0)
				round_num = 1;	/*round_num = 0.1~0.9 */
			min_num = min(round_num, fp_num);
			wl_all("TID=%d,PEER=%d,%d,%d,%d,%d,%d\n",
				 i, j, p_list_num[i][j], q_list_num[i],
				 round_num, fp_num, min_num);
			if (min_num <= 0)
				break;
			q_list_num[i] -= p_list_num[i][j];
			fp_num -= min_num;
			if (tx_add_xmit_list_tail(tx_mgmt,
					      &q_list->p_list[j], min_num))
				continue;
			spin_lock_bh(&q_list->p_list[j].p_lock);
			if (atomic_read(&q_list->p_list[j].l_num)) {
				atomic_sub(min_num, &q_list->p_list[j].l_num);
				atomic_sub(min_num, &tx_list->mode_list_num);
			}
			spin_unlock_bh(&q_list->p_list[j].p_lock);
			wl_all
			    ("%s, %d, mode=%d, TID=%d, lut=%d, %d add to xmit_list,"
			     "then l_num=%d, mode_list_num=%d\n",
			     __func__, __LINE__, mode, i, j, min_num,
			     atomic_read(&q_list->p_list[j].l_num),
			     atomic_read(&tx_mgmt->tx_list[mode]->mode_list_num));
			if (fp_num <= 0)
				break;
		}
	}
	ret = tx_handle_to_send_list(hif, mode);
	return ret;
}

static void tx_flush_all_txlist(struct tx_mgmt *tx_dev)
{
	tx_flush_cmd_txlist(&tx_dev->tx_list_cmd);
	tx_flush_data_txlist(tx_dev);
}

void sc2355_tx_prepare_addba(struct sprd_hif *hif, unsigned char lut_index,
			     struct sprd_peer_entry *peer_entry,
			     unsigned char tid)
{
	if (hif->tx_num[lut_index] > 9 &&
	    peer_entry &&
	    peer_entry->ht_enable &&
	    peer_entry->vowifi_enabled != 1 &&
	    !test_bit(tid, &peer_entry->ba_tx_done_map)) {
		s64 time, time_diffms;
		struct sprd_vif *vif;

		vif = sc2355_ctxid_to_vif(hif->priv, peer_entry->ctx_id);
		if (!vif) {
			wl_err("can not get vif base peer_entry ctx id\n");
			return;
		}

		if (vif->mode == SPRD_MODE_STATION ||
		    vif->mode == SPRD_MODE_P2P_CLIENT) {
			if (!peer_entry->ip_acquired) {
				sprd_put_vif(vif);
				return;
			}
		}

		time = sprd_get_ktime();
		time_diffms = div_s64((time - peer_entry->time[tid]), 1000000);
		/*need to delay 3s if priv addba failed */
		if (time_diffms > 3000 ||
		    peer_entry->time[tid] == 0) {
			wl_info("%s, %d, tx_addba, tid=%d\n", __func__,
				__LINE__, tid);
			peer_entry->time[tid] = sprd_get_ktime();
			if (!test_and_set_bit(tid, &peer_entry->ba_tx_done_map)) {
				if (hif->fw_power_down == 1) {
					wl_info("%s wakeup fw before tx_addba\n", __func__);
					hif->fw_power_down = 0;
					sc2355_work_host_wakeup_fw(vif);
				}
				sc2355_tx_addba(hif, peer_entry, tid);
			}
		}
		sprd_put_vif(vif);
	}
}

static int tx_prepare_tx_msg(struct sprd_hif *hif, struct sprd_msg *msg)
{
	u16 len;
	unsigned char *info;
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;

	if (msg->msglist == &tx_mgmt->tx_list_cmd) {
		len = SPRD_MAX_CMD_TXLEN;
		info = "cmd";
		msg->timeout = jiffies + tx_mgmt->cmd_timeout;
	} else {
		len = SPRD_MAX_DATA_TXLEN;
		info = "data";
		msg->timeout = jiffies + tx_mgmt->data_timeout;
	}

	if (msg->len > len) {
		wl_err("%s err:%s too long:%d > %d,drop it\n",
		       __func__, info, msg->len, len);
#if defined(MORE_DEBUG)
		hif->stats.tx_dropped++;
#endif
		INIT_LIST_HEAD(&msg->list);
		/* skb == NULL in cmd msg */
		if (msg->skb) {
			dev_kfree_skb(msg->skb);
			msg->skb = NULL;
		} else {
			kfree(msg->tran_data);
			msg->tran_data = NULL;
		}

		sprd_free_msg(msg, msg->msglist);
		return -1;
	}

	return 0;
}

static void tx_get_pcie_dma_addr(struct sprd_hif *hif, struct sk_buff *skb)
{
	struct sk_buff *tmp_skb = NULL;
	dma_addr_t dma_addr = 0;

	dma_addr = PFN_PHYS(virt_to_pfn(skb->head)) + offset_in_page(skb->head);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
#ifdef CONFIG_64BIT
	if (!dma_capable(wiphy_dev(hif->priv->wiphy), dma_addr, skb->len, true)) {
#else
	{
		wl_err("FIXME: dma_capble can't used by 32bit-ARCH!\n");
#endif  //CONFIG_64BIT
#else
	if (!dma_capable(wiphy_dev(hif->priv->wiphy), dma_addr, skb->len)) {
#endif
		/* current pa is lagrer than device dma mask
		 * need to use dma buffer
		 */
		wl_err("skb copy from dma addr(%lx)\n",
		       (unsigned long)dma_addr);
		tmp_skb = skb_copy(skb, (GFP_DMA | GFP_ATOMIC));
		dev_kfree_skb(skb);
		skb = tmp_skb;
	}
}

static void tx_work_queue(struct tx_mgmt *tx_mgmt)
{
	unsigned long need_polling;
	struct sprd_hif *hif;
	enum sprd_mode mode = SPRD_MODE_NONE;
	unsigned int polling_times = 0;
	int send_num = 0;
	struct sprd_priv *priv;
	struct sprd_vif *vif = NULL, *tmp_vif;

	hif = tx_mgmt->hif;
	priv = hif->priv;

RETRY:
	if (unlikely(hif->exit)) {
		wl_err("%s no longer exsit, flush data, return!\n", __func__);
		tx_flush_all_txlist(tx_mgmt);
		return;
	}
	need_polling = 0;

	/*During hang recovery, send data is not allowed.
	 * but we still need to send cmd to cp2
	 */
	if (tx_mgmt->hang_recovery_status != HANG_RECOVERY_END) {
		printk_ratelimited("sc2355, %s, hang happened\n", __func__);
		if (sprd_msg_tx_pended(&tx_mgmt->tx_list_cmd))
			tx_cmd(hif, &tx_mgmt->tx_list_cmd);
		return;
	}

	if (tx_mgmt->thermal_status == THERMAL_WIFI_DOWN) {
		printk_ratelimited("sc2355, %s, THERMAL_WIFI_DOWN\n", __func__);
		if (sprd_msg_tx_pended(&tx_mgmt->tx_list_cmd))
			tx_cmd(hif, &tx_mgmt->tx_list_cmd);
		return;
	}
	if (tx_mgmt->thermal_status == THERMAL_TX_STOP) {
		printk_ratelimited("sc2355, %s, THERMAL_TX_STOP\n", __func__);
		if (sprd_msg_tx_pended(&tx_mgmt->tx_list_cmd))
			tx_cmd(hif, &tx_mgmt->tx_list_cmd);
		return;
	}

	if (sprd_msg_tx_pended(&tx_mgmt->tx_list_cmd))
		tx_cmd(hif, &tx_mgmt->tx_list_cmd);

	/* if tx list, send wakeup firstly */
	if (hif->fw_power_down == 1 &&
	    (atomic_read(&tx_mgmt->tx_list_qos_pool.ref) > 0 ||
	     !list_empty(&tx_mgmt->xmit_msg_list.to_send_list) ||
	     !list_empty(&tx_mgmt->xmit_msg_list.to_free_list))) {
		spin_lock_bh(&priv->list_lock);
		list_for_each_entry(tmp_vif, &priv->vif_list, vif_node) {
			if (tmp_vif->state & VIF_STATE_OPEN) {
				vif = tmp_vif;
				break;
			}
		}
		spin_unlock_bh(&priv->list_lock);

		if (!vif)
			return;
		hif->fw_power_down = 0;
		sc2355_work_host_wakeup_fw(vif);
		return;
	}

	if (hif->fw_awake == 0) {
		printk_ratelimited("sc2355, %s, fw_awake = 0\n", __func__);
		return;
	}

	if (hif->suspend_mode != SPRD_PS_RESUMED) {
		printk_ratelimited("sc2355, %s, not RESUMED\n", __func__);
		return;
	}
	if (hif->pushfail_count > 100 && (priv->hif.hw_type == SPRD_HW_SC2355_PCIE ||
		priv->hif.hw_type == SPRD_HW_SC2355_SIPC))
		usleep_range(5990, 6010);

	if (!list_empty(&tx_mgmt->xmit_msg_list.to_send_list)) {
		wl_warn("%s,%d need check the to_send_list", __func__, __LINE__);
		if (tx_handle_to_send_list(hif, tx_mgmt->xmit_msg_list.mode)) {
			usleep_range(10, 20);
			return;
		}
	}
	if (hif->pushfail_count > 100 && (priv->hif.hw_type == SPRD_HW_SC2355_PCIE ||
		priv->hif.hw_type == SPRD_HW_SC2355_SIPC))
		sc2355_flush_tosendlist(tx_mgmt);

	hif->sta_tx_no_credit = 0;

	for (mode = SPRD_MODE_NONE; mode < SPRD_MODE_MAX; mode++) {
		int num = atomic_read(&tx_mgmt->tx_list[mode]->mode_list_num);

		if (num <= 0)
			continue;
		vif = sprd_mode_to_vif(priv, mode);
		if (!vif)
			continue;
		if (num > 0 && (!(vif->state & VIF_STATE_OPEN) ||
				((mode == SPRD_MODE_STATION ||
				  mode == SPRD_MODE_STATION_SECOND ||
				  mode == SPRD_MODE_P2P_CLIENT) &&
				 vif->sm_state != SPRD_CONNECTED))) {
			sc2355_flush_mode_txlist(tx_mgmt, mode);
			sprd_put_vif(vif);
			continue;
		}
		sprd_put_vif(vif);
		if (hif->hw_type == SPRD_HW_SC2355_PCIE)
			send_num = sc2355_pcie_fc_test_send_num(hif, mode, num);
		else if (hif->hw_type == SPRD_HW_SC2355_SIPC)
			send_num = sc2355_sipc_fc_test_send_num(hif, mode, num);
		else
			send_num = sc2355_fc_test_send_num(hif, mode, num);
		if (send_num > 0)
			tx_eachmode_data(hif, mode);
		else
			need_polling |= (1 << (u8)mode);
	}
	/*sleep more time if screen off */
	if (priv->is_screen_off == 1 && (
	    priv->hif.hw_type == SPRD_HW_SC2355_PCIE ||
		priv->hif.hw_type == SPRD_HW_SC2355_SIPC)) {
		usleep_range(590, 610);
		return;
	}

	if (need_polling) {
		if (hif->hw_type == SPRD_HW_SC2355_PCIE || hif->hw_type == SPRD_HW_SC2355_SIPC) {
			usleep_range(10, 15);
		} else if (hif->hw_type == SPRD_HW_SC2355_SDIO &&
			   polling_times++ < TX_MAX_POLLING) {
			udelay(TX_POLLING_INTERVAL);
			goto RETRY;
		}
	}
}

static int sc2355_tx_thread(void *data)
{
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)data;

	set_user_nice(current, -20);
	while (!kthread_should_stop()) {
		sc2355_tx_down(tx_mgmt);
		if (unlikely(tx_mgmt->tx_thread_exit))
			goto exit;

		sprd_hif_tp_ctl_uclamp(tx_mgmt->hif);
#ifndef CONFIG_SPRD_WLAN_DEBUG
		if (tx_mgmt->hif->hw_type == SPRD_HW_SC2355_SIPC)
			sc2355_tp_modify_cpu_usage(tx_mgmt->tx_thread, "TX",
				sprd_is_sta_ap_coexist(tx_mgmt->hif->priv));
#endif
		tx_work_queue(tx_mgmt);
	}

exit:
	tx_mgmt->tx_thread_exit = 0;
	wl_debug("%s exit.\n", __func__);
	return 0;
}

static inline unsigned short tx_from32to16(unsigned int x)
{
	/* add up 16-bit and 16-bit for 16+c bit */
	x = (x & 0xffff) + (x >> 16);
	/* add up carry.. */
	x = (x & 0xffff) + (x >> 16);
	return x;
}

static unsigned int tx_do_csum(const unsigned char *buff, int len)
{
	int odd;
	unsigned int result = 0;

	if (len <= 0)
		goto out;
	odd = 1 & (unsigned long)buff;
	if (odd) {
#ifdef __LITTLE_ENDIAN
		result += (*buff << 8);
#else
		result = *buff;
#endif
		len--;
		buff++;
	}
	if (len >= 2) {
		if (2 & (unsigned long)buff) {
			result += *(unsigned short *)buff;
			len -= 2;
			buff += 2;
		}
		if (len >= 4) {
			const unsigned char *end =
			    buff + ((unsigned int)len & ~3);
			unsigned int carry = 0;

			do {
				unsigned int w = *(unsigned int *)buff;

				buff += 4;
				result += carry;
				result += w;
				carry = (w > result);
			} while (buff < end);
			result += carry;
			result = (result & 0xffff) + (result >> 16);
		}
		if (len & 2) {
			result += *(unsigned short *)buff;
			buff += 2;
		}
	}
	if (len & 1)
#ifdef __LITTLE_ENDIAN
		result += *buff;
#else
		result += (*buff << 8);
#endif
	result = tx_from32to16(result);
	if (odd)
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
out:
	return result;
}

int sc2355_tx_do_csum(const unsigned char *buff, int len){
	return tx_do_csum(buff, len);
}

static int tx_is_multicast_mac_addr(const u8 *addr)
{
	return ((addr[0] != 0xff) && (0x01 & addr[0]));
}

static int tx_mc_pkt_checksum(struct sk_buff *skb, struct net_device *ndev)
{
	struct udphdr *udphdr;
	struct tcphdr *tcphdr;
	struct ipv6hdr *ipv6hdr = NULL;
	struct iphdr *iphdr = NULL;
	__sum16 checksum = 0;
	unsigned char iphdrlen = 0;
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;

	if (ethhdr->h_proto == htons(ETH_P_IPV6)) {
		ipv6hdr = (struct ipv6hdr *)(skb->data + ETHER_HDR_LEN);
		iphdrlen = sizeof(*ipv6hdr);
	} else {
		iphdr = (struct iphdr *)(skb->data + ETHER_HDR_LEN);
		iphdrlen = ip_hdrlen(skb);
	}

	udphdr = (struct udphdr *)(skb->data + ETHER_HDR_LEN + iphdrlen);
	tcphdr = (struct tcphdr *)(skb->data + ETHER_HDR_LEN + iphdrlen);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		checksum =
		    (__force __sum16)tx_do_csum(skb->data + ETHER_HDR_LEN +
						iphdrlen,
						skb->len - ETHER_HDR_LEN -
						iphdrlen);
		if ((ipv6hdr && ipv6hdr->nexthdr == IPPROTO_UDP) ||
		    (iphdr && iphdr->protocol == IPPROTO_UDP)) {
			udphdr->check = ~checksum;
			wl_info("csum:%x,udp check:%x\n",
				checksum, udphdr->check);
		} else if ((ipv6hdr && ipv6hdr->nexthdr == IPPROTO_TCP) ||
			   (iphdr && iphdr->protocol == IPPROTO_TCP)) {
			tcphdr->check = ~checksum;
			wl_info("csum:%x,tcp check:%x\n",
				checksum, tcphdr->check);
		} else {
			return 1;
		}
		skb->ip_summed = CHECKSUM_NONE;
		return 0;
	}
	return 1;
}

static int tx_mc_pkt(struct sk_buff *skb, struct net_device *ndev)
{
	struct sprd_vif *vif;
	struct sprd_hif *hif;
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;

	vif = netdev_priv(ndev);
	hif = &vif->priv->hif;

	if (hif->hw_type == SPRD_HW_SC2355_SIPC &&
	    ethhdr->h_proto == htons(ETH_P_IP))
		return 1;

	if (tx_is_multicast_mac_addr(skb->data) && vif->mode == SPRD_MODE_AP) {
		wl_debug("%s,AP mode, multicast bssid: %pM\n",
			 __func__, skb->data);
		tx_mc_pkt_checksum(skb, ndev);
		sc2355_xmit_data2cmd_wq(skb, ndev);
		return NETDEV_TX_OK;
	}
	return 1;
}

static int tx_filter_ip_pkt(struct sk_buff *skb, struct net_device *ndev)
{
	bool is_data2cmd;
	bool is_ipv4_dhcp = false, is_ipv6_dhcp = false;
	bool is_vowifi2cmd = false;
	bool is_dns = false;
	bool is_alive_rtsp = false;
	unsigned char *dhcpdata = NULL;
	struct udphdr *udphdr;
	struct tcphdr *tcphdr;
	struct iphdr *iphdr;
	__sum16 checksum = 0;
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	unsigned char iphdrlen = 0;
	unsigned char lut_index;
	struct sprd_vif *vif;
	struct sprd_hif *hif;
	unsigned char *rtsp_get_params = "GET_PARAMETER";
	unsigned int total_hdr_len = 0;

	vif = netdev_priv(ndev);
	hif = &vif->priv->hif;

	if (ethhdr->h_proto == htons(ETH_P_IP)) {
		iphdr = (struct iphdr *)(skb->data + ETHER_HDR_LEN);
		iphdrlen = ip_hdrlen(skb);
		if (iphdr->protocol == IPPROTO_TCP) {
			tcphdr = (struct tcphdr *)(skb->data +
						   ETHER_HDR_LEN + iphdrlen);
			total_hdr_len = ETHER_HDR_LEN + iphdrlen + tcp_hdrlen(skb);
			if (tcphdr->source == htons(RTSP_SERVER_PORT) &&
			    !memcmp((skb->data + total_hdr_len), rtsp_get_params, 13)) {
				is_alive_rtsp = true;
				wl_info("tx rtsp keep-alive data packet\n");
				goto next;
			} else {
				return 1;
			}
		}
	}

	udphdr = sprd_get_udphdr(skb, &iphdrlen);
	if (!udphdr)
		return 1;

	if (IP_DNS(ethhdr, udphdr)) {
		is_dns = true;
		wl_info("dns,check:%x,skb->ip_summed:%d\n",
			udphdr->check, skb->ip_summed);
	}

	if (sc2355_is_vowifi_pkt(skb, &is_vowifi2cmd)) {
		if (!is_vowifi2cmd) {
			struct sprd_peer_entry *peer_entry = NULL;
			lut_index = sc2355_find_lut_index(hif, vif, skb->data);
			peer_entry = &hif->peer_entry[lut_index];
			if (peer_entry->vowifi_enabled == 1) {
				if (peer_entry->vowifi_pkt_cnt < 11)
					peer_entry->vowifi_pkt_cnt++;
				if (peer_entry->vowifi_pkt_cnt == 10)
					sc2355_vowifi_data_protection(vif);
			}
		} else if (ethhdr->h_proto == htons(ETH_P_IP)) {
			wl_info("vowifi, proto=0x%x, dest=%d\n",
				ntohs(ethhdr->h_proto), ntohs(udphdr->dest));
		}
	} else {
		is_vowifi2cmd = false;
	}

	if (IPV4_DHCP(ethhdr, udphdr)) {
		is_ipv4_dhcp = true;
		dhcpdata = skb->data + ETHER_HDR_LEN + iphdrlen + 250;

		if (*dhcpdata < 0x07)
			wl_info("TX: [%s],check:%x,skb->ip_summed:%d\n",
				dhcp_str_info[*dhcpdata], udphdr->check,
				skb->ip_summed);
		if (*dhcpdata == 0x03 || *dhcpdata == 0x05) {
			lut_index = sc2355_find_lut_index(hif, vif, skb->data);
			hif->peer_entry[lut_index].ip_acquired = 1;
			if (*dhcpdata == 0x03 && sc2355_is_group(skb->data))
				hif->peer_entry[lut_index].ba_tx_done_map = 0;
		}
	} else if (IPV6_DHCP(ethhdr, udphdr)) {
		is_ipv6_dhcp = true;
		wl_info("dhcp,check:%x,skb->ip_summed:%d\n",
			udphdr->check, skb->ip_summed);
	}

next:
	is_data2cmd = (is_ipv4_dhcp || is_ipv6_dhcp || is_vowifi2cmd ||
		       is_dns || is_alive_rtsp);
	/*as CP request, send data with CMD */
	if (is_data2cmd) {
		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			checksum =
			    (__force __sum16)tx_do_csum(skb->data +
							ETHER_HDR_LEN +
							iphdrlen,
							skb->len -
							ETHER_HDR_LEN -
							iphdrlen);
			if ((ethhdr->h_proto == htons(ETH_P_IP)) &&
			    (iphdr->protocol == IPPROTO_TCP)) {
				tcphdr->check = ~checksum;
				wl_debug("csum:%x,check:%x\n", checksum, tcphdr->check);
			} else {
				udphdr->check = ~checksum;
				wl_debug("csum:%x,check:%x\n", checksum, udphdr->check);
			}
			skb->ip_summed = CHECKSUM_NONE;
		}

		spin_lock_bh(&adap_info.adap_lock);
		wl_debug("%s special_data_flag: %d\n",
			__func__, adap_info.special_data_flag);
		if (is_dns && (adap_info.special_data_flag == SPRD_NPI_NORMAL_ALL ||
		    (adap_info.special_data_flag == SPRD_NPI_NORMAL_UNENCRYP &&
		    vif->prwise_crypto == SPRD_CIPHER_NONE))) {
				spin_unlock_bh(&adap_info.adap_lock);
				return 1;
			}
		spin_unlock_bh(&adap_info.adap_lock);

		sc2355_xmit_data2cmd_wq(skb, ndev);
		return NETDEV_TX_OK;
	}

	return 1;
}

void sc2355_free_cmd_buf(struct sprd_msg *msg, struct sprd_msg_list *list)
{
	spin_lock_bh(&list->complock);
	list_del(&msg->list);
	spin_unlock_bh(&list->complock);
	sprd_free_msg(msg, list);
}

void sc2355_flush_mode_tofreelist(struct sprd_hif *hif, struct sprd_vif *vif)
{
	struct tx_mgmt *tx_mgmt = NULL;
	struct sprd_msg *pos_buf = NULL, *temp_buf = NULL;
	unsigned long lockflag_txfree = 0;
	struct list_head *data_list = NULL;

	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	data_list = &tx_mgmt->xmit_msg_list.to_free_list;

	spin_lock_irqsave(&tx_mgmt->xmit_msg_list.free_lock, lockflag_txfree);
	list_for_each_entry_safe(pos_buf, temp_buf, data_list, list) {
		if (pos_buf->mode == vif->mode) {
			wl_info("%s: msg_buf %p, pcie_addr %lx\n",
					__func__, pos_buf, (unsigned long)pos_buf->pcie_addr);
			tx_dequeue_data_msg(tx_mgmt->hif, pos_buf);
			atomic_dec(&tx_mgmt->xmit_msg_list.free_num);
		}
	}
	spin_unlock_irqrestore(&tx_mgmt->xmit_msg_list.free_lock, lockflag_txfree);
}

void sc2355_flush_tx_qoslist(struct tx_mgmt *tx_mgmt, int mode,
			     int ac_index, int lut_index)
{
	/*peer list lock */
	spinlock_t *plock;
	struct sprd_msg *pos_buf, *temp_buf;
	struct list_head *data_list;

	data_list =
	    &tx_mgmt->tx_list[mode]->q_list[ac_index].p_list[lut_index].head_list;

	plock =
	    &tx_mgmt->tx_list[mode]->q_list[ac_index].p_list[lut_index].p_lock;

	spin_lock_bh(plock);
	if (!list_empty(data_list)) {
		list_for_each_entry_safe(pos_buf, temp_buf, data_list, list) {
			dev_kfree_skb(pos_buf->skb);
			pos_buf->skb = NULL;
			list_del(&pos_buf->list);
			sprd_free_msg(pos_buf, pos_buf->msglist);
		}

		atomic_sub(atomic_read
			   (&tx_mgmt->tx_list[mode]->q_list[ac_index].
			    p_list[lut_index].l_num),
			   &tx_mgmt->tx_list[mode]->mode_list_num);
		atomic_set(&tx_mgmt->tx_list[mode]->q_list[ac_index].
			   p_list[lut_index].l_num, 0);
	}
	spin_unlock_bh(plock);
}

void sc2355_flush_mode_txlist(struct tx_mgmt *tx_mgmt, enum sprd_mode mode)
{
	int i, j;
	/*peer list lock */
	spinlock_t *plock;
	struct sprd_msg *pos_buf, *temp_buf;
	struct qos_tx_t *tx_list = tx_mgmt->tx_list[mode];
	struct list_head *data_list;

	wl_debug("%s, mode=%d\n", __func__, mode);

	for (i = 0; i < SPRD_AC_MAX; i++) {
		for (j = 0; j < MAX_LUT_NUM; j++) {
			data_list = &tx_list->q_list[i].p_list[j].head_list;

			if (list_empty(data_list))
				continue;
			plock = &tx_list->q_list[i].p_list[j].p_lock;

			spin_lock_bh(plock);

			list_for_each_entry_safe(pos_buf, temp_buf,
						 data_list, list) {
				if (pos_buf->skb) {
					dev_kfree_skb(pos_buf->skb);
					pos_buf->skb = NULL;
				}
				list_del(&pos_buf->list);
				sprd_free_msg(pos_buf, pos_buf->msglist);
			}

			spin_unlock_bh(plock);

			atomic_set(&tx_list->q_list[i].p_list[j].l_num, 0);
		}
	}

	atomic_set(&tx_list->mode_list_num, 0);
}

void sc2355_flush_tosendlist(struct tx_mgmt *tx_mgmt)
{
	struct sprd_msg *pos_buf, *temp_buf;
	struct list_head *data_list;
	spinlock_t *lock;

	wl_err("%s, %d\n", __func__, __LINE__);
	data_list = &tx_mgmt->xmit_msg_list.to_send_list;
	lock = &tx_mgmt->xmit_msg_list.send_lock;
	spin_lock_bh(lock);
	if (!list_empty(data_list)) {
		list_for_each_entry_safe(pos_buf, temp_buf, data_list, list) {
			tx_dequeue_data_msg(tx_mgmt->hif, pos_buf);
		}
	}
	spin_unlock_bh(lock);
}

void sc2355_dequeue_data_buf(struct sprd_msg *msg)
{
	spin_lock_bh(&msg->xmit_msg_list->free_lock);
	list_del(&msg->list);
	spin_unlock_bh(&msg->xmit_msg_list->free_lock);
	sprd_free_msg(msg, msg->msglist);
}

void sc2355_dequeue_data_list(struct mbuf_t *head, int num)
{
	int i;
	struct sprd_msg *msg_pos;
	struct mbuf_t *mbuf_pos = NULL;

	mbuf_pos = head;
	for (i = 0; i < num; i++) {
		msg_pos = GET_MSG_BUF(mbuf_pos);
		if (!msg_pos ||
		    !virt_addr_valid(msg_pos) ||
		    !virt_addr_valid(msg_pos->skb)) {
			wl_err("%s,%d, error! wrong sprd_msg\n",
			       __func__, __LINE__);
			BUG_ON(1);
			return;
		}
		dev_kfree_skb(msg_pos->skb);
		msg_pos->skb = NULL;
		/*delete node from to_free_list */
		spin_lock_bh(&msg_pos->xmit_msg_list->free_lock);
		list_del(&msg_pos->list);
		spin_unlock_bh(&msg_pos->xmit_msg_list->free_lock);
		/*add it to free_list */
		spin_lock_bh(&msg_pos->msglist->freelock);
		list_add_tail(&msg_pos->list, &msg_pos->msglist->freelist);
		spin_unlock_bh(&msg_pos->msglist->freelock);
		mbuf_pos = mbuf_pos->next;
	}
}

/*To clear mode assigned in flow_ctrl
 *and to flush data lit of closed mode
 */
void sc2355_handle_tx_status_after_close(struct sprd_vif *vif)
{
	struct sprd_priv *priv = vif->priv;
	struct sprd_vif *tmp_vif;
	u8 i, allmode_closed = 1;
	struct sprd_hif *hif;
	struct tx_mgmt *tx_mgmt;

	spin_lock_bh(&priv->list_lock);
	list_for_each_entry(tmp_vif, &priv->vif_list, vif_node) {
		if (tmp_vif->state & VIF_STATE_OPEN) {
			allmode_closed = 0;
			break;
		}
	}
	spin_unlock_bh(&priv->list_lock);

	hif = &vif->priv->hif;
	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	if (allmode_closed == 1) {
		/*all modee closed,
		 *reset all credit
		 */
		wl_debug("%s, %d, _fc_, delete flow num after all closed\n",
			__func__, __LINE__);
		for (i = 0; i < MAX_COLOR_BIT; i++) {
			tx_mgmt->flow_ctrl[i].mode = SPRD_MODE_NONE;
			tx_mgmt->flow_ctrl[i].color_bit = i;
			tx_mgmt->ring_cp = 0;
			tx_mgmt->ring_ap = 0;
			atomic_set(&tx_mgmt->flow_ctrl[i].flow, 0);
		}

		if (priv->hif.hw_type == SPRD_HW_SC2355_PCIE ||
			priv->hif.hw_type == SPRD_HW_SC2355_SIPC)
			sc2355_rx_flush_buffer(&priv->hif);
	} else {
		/*a mode closed,
		 *remove it from flow control to
		 *make it shared by other still open mode
		 */
		for (i = 0; i < MAX_COLOR_BIT; i++) {
			if (tx_mgmt->flow_ctrl[i].mode == vif->mode) {
				wl_debug
				    (" %s, %d, _fc_, clear mode%d because closed\n",
				     __func__, __LINE__, vif->mode);
				tx_mgmt->flow_ctrl[i].mode = SPRD_MODE_NONE;
			}
		}
		/*if tx_list[mode] not empty,
		 *but mode is closed, should flush it
		 */
		if (!(vif->state & VIF_STATE_OPEN) &&
		    (atomic_read(&tx_mgmt->tx_list[vif->mode]->mode_list_num) !=
		     0))
			sc2355_flush_mode_txlist(tx_mgmt, vif->mode);
	}

	if (!(vif->state & VIF_STATE_OPEN) && ((priv->hif.hw_type == SPRD_HW_SC2355_PCIE)
		   || (priv->hif.hw_type == SPRD_HW_SC2355_SIPC)))
		sc2355_flush_mode_tofreelist(hif, vif);
}

unsigned int sc2355_queue_is_empty(struct tx_mgmt *tx_mgmt, enum sprd_mode mode)
{
	int i, j;
	struct qos_tx_t *tx_t_list = tx_mgmt->tx_list[mode];

	if (mode == SPRD_MODE_AP || mode == SPRD_MODE_P2P_GO) {
		for (i = 0; i < SPRD_AC_MAX; i++) {
			for (j = 0; j < MAX_LUT_NUM; j++) {
				struct list_head *list =
				    &tx_t_list->q_list[i].p_list[j].head_list;

				if (!list_empty(list))
					return 0;
			}
		}
		return 1;
	}
	/*other mode, STA/GC/... */
	j = tx_mgmt->tx_list[mode]->lut_id;
	for (i = 0; i < SPRD_AC_MAX; i++) {
		struct list_head *list =
		    &tx_t_list->q_list[i].p_list[j].head_list;

		if (!list_empty(list))
			return 0;
	}
	return 1;
}

void sc2355_wake_net_ifneed(struct sprd_hif *hif, struct sprd_msg_list *list,
			    enum sprd_mode mode)
{
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;

	if (atomic_read(&list->flow)) {
		if (atomic_read(&list->ref) <= SPRD_TX_DATA_START_NUM) {
			atomic_set(&list->flow, 0);
			tx_mgmt->net_start_cnt++;
			sprd_net_flowcontrl(hif->priv, mode, true);
		}
	}
}

void sc2355_fc_add_share_credit(struct sprd_vif *vif)
{
	struct sprd_hif *hif;
	struct tx_mgmt *tx_mgmt;
	u8 i;

	hif = &vif->priv->hif;
	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	for (i = 0; i < MAX_COLOR_BIT; i++) {
		if (tx_mgmt->flow_ctrl[i].mode == vif->mode) {
			wl_err("%s, %d, mode:%d closed, index:%d, share it\n",
			       __func__, __LINE__, vif->mode, i);
			tx_mgmt->flow_ctrl[i].mode = SPRD_MODE_NONE;
			break;
		}
	}
}

u8 sc2355_fc_set_clor_bit(struct tx_mgmt *tx_mgmt, int num)
{
	u8 i = 0;
	int count_num = 0;
	struct sprd_priv *priv = tx_mgmt->hif->priv;

	if (priv->credit_capa == TX_NO_CREDIT)
		return 0;

	for (i = 0; i < MAX_COLOR_BIT; i++) {
		count_num += tx_mgmt->color_num[i];
		if (num <= count_num)
			break;
	}
	wl_all("%s, %d, color bit =%d\n", __func__, __LINE__, i);
	return i;
}

int sc2355_sdio_process_credit(struct sprd_hif *hif, void *data)
{
	int ret = 0, i;
	unsigned char *flow;
	struct sprd_common_hdr *common;
	struct tx_mgmt *tx_mgmt;
	ktime_t kt;
	int in_count = 0;

	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	common = (struct sprd_common_hdr *)data;

	if (common->type == SPRD_TYPE_DATA_SPECIAL) {
		int offset = (size_t)&((struct rx_msdu_desc *)0)->rsvd5;

		flow = data + offset;
		goto out;
	}

	if (common->type == SPRD_TYPE_EVENT) {
		struct sprd_cmd_hdr *cmd;

		cmd = (struct sprd_cmd_hdr *)data;
		if (cmd->cmd_id == EVT_SDIO_FLOWCON) {
			flow = cmd->paydata;
			ret = -1;
			goto out;
		}
	}
	return 0;

out:
	if (flow[0])
		atomic_add(flow[0], &tx_mgmt->flow_ctrl[0].flow);
	if (flow[1])
		atomic_add(flow[1], &tx_mgmt->flow_ctrl[1].flow);
	if (flow[2])
		atomic_add(flow[2], &tx_mgmt->flow_ctrl[2].flow);
	if (flow[3])
		atomic_add(flow[3], &tx_mgmt->flow_ctrl[3].flow);
	if (flow[0] || flow[1] || flow[2] || flow[3]) {
		in_count = flow[0] + flow[1] + flow[2] + flow[3];
		tx_mgmt->ring_cp += in_count;
		if (hif->fw_awake == 1)
			sc2355_tx_up(tx_mgmt);
	}
	/* Firmware want to reset credit, will send us
	 * a credit event with all 4 parameters set to zero
	 */
	if (in_count == 0) {
		/*in_count==0: reset credit event or a data without credit
		 *ret == -1:reset credit event
		 *for a data without credit:just return,donot print log
		 */
		if (ret == -1) {
			wl_debug("%s, %d, _fc_ reset credit\n", __func__,
				__LINE__);
			for (i = 0; i < MAX_COLOR_BIT; i++) {
				if (tx_mgmt->ring_cp != 0)
					tx_mgmt->ring_cp -=
					    atomic_read(&tx_mgmt->flow_ctrl[i].flow);
				atomic_set(&tx_mgmt->flow_ctrl[i].flow, 0);
				tx_mgmt->color_num[i] = 0;
			}
		}
		goto exit;
	}
	kt = ktime_get();
	/*1.(tx_mgmt->kt.tv64 == 0) means 1st event
	 *2.add (in_count == 0) to avoid
	 *division by expression in_count which
	 *may be zero has undefined behavior
	 */
	if (tx_mgmt->kt == 0 || in_count == 0) {
		tx_mgmt->kt = kt;
	} else {
		/* (us/c) means time interval between two updates for each credit */
		wl_debug("update_credit, %s, %dadded, %lld us/c\n",
			(ret == -1) ? "event" : "data",
			in_count,
			div_u64(div_u64(kt - tx_mgmt->kt, NSEC_PER_USEC),
				in_count));

		sprd_debug_record_add(TX_CREDIT_ADD, in_count);
		sprd_debug_record_add(TX_CREDIT_PER_ADD,
				      div_u64(div_u64(kt - tx_mgmt->kt,
						      NSEC_PER_USEC),
					      in_count));
		sprd_debug_record_add(TX_CREDIT_RECORD,
				      jiffies_to_usecs(jiffies));
		sprd_debug_record_add(TX_CREDIT_TIME_DIFF,
				      div_u64(kt - tx_mgmt->kt, NSEC_PER_USEC));
	}
	tx_mgmt->kt = ktime_get();

	wl_debug("_fc_,R+%d=%d,G+%d=%d,B+%d=%d,W+%d=%d,cp=%lu,ap=%lu\n",
		flow[0], atomic_read(&tx_mgmt->flow_ctrl[0].flow),
		flow[1], atomic_read(&tx_mgmt->flow_ctrl[1].flow),
		flow[2], atomic_read(&tx_mgmt->flow_ctrl[2].flow),
		flow[3], atomic_read(&tx_mgmt->flow_ctrl[3].flow),
		tx_mgmt->ring_cp, tx_mgmt->ring_ap);
exit:
	return ret;
}

struct sprd_msg *sc2355_tx_get_msg(struct sprd_chip *chip,
				   enum sprd_head_type type,
				   enum sprd_mode mode)
{
	struct sprd_msg *msg = NULL;
	struct sprd_msg_list *list = NULL;
	struct sprd_priv *priv = chip->priv;
	struct sprd_hif *hif = &priv->hif;
	struct tx_mgmt *tx_dev = NULL;

	tx_dev = (struct tx_mgmt *)hif->tx_mgmt;
	tx_dev->mode = mode;

	if (unlikely(hif->exit)) {
		wl_err("%s can not get msg: hif->exit\n", __func__);
		return NULL;
	}

	if (type == SPRD_TYPE_DATA)
		list = &tx_dev->tx_list_qos_pool;
	else
		list = &tx_dev->tx_list_cmd;

	if (!list) {
		wl_err("%s: type %d could not get list\n", __func__, type);
		return NULL;
	}

	msg = sprd_alloc_msg(list);

	if (msg) {
#if defined(MORE_DEBUG)
		msg->tx_start_time = sprd_get_ktime();
#endif
		if (type == SPRD_TYPE_DATA)
			msg->msg_type = SPRD_TYPE_DATA;
		else
			msg->msg_type = SPRD_TYPE_CMD;
		msg->type = type;
		msg->msglist = list;
		msg->mode = mode;
		msg->xmit_msg_list = &tx_dev->xmit_msg_list;
		return msg;
	}

	if (type == SPRD_TYPE_DATA) {
		tx_dev->net_stop_cnt++;
		sprd_net_flowcontrl(priv, mode, false);
		atomic_set(&list->flow, 1);
	}
	printk_ratelimited("%s no more msg for %s\n",
			   __func__, type == SPRD_TYPE_DATA ? "data" : "cmd");

	return NULL;
}

void sc2355_tx_free_msg(struct sprd_chip *chip, struct sprd_msg *msg)
{
	sprd_free_msg(msg, msg->msglist);
}

int sc2355_tx_prepare(struct sprd_chip *chip, struct sk_buff *skb)
{
	struct sprd_priv *priv = chip->priv;
	struct sprd_hif *hif = &priv->hif;

	if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
		if (hif->suspend_mode != SPRD_PS_RESUMED ||
		    sprdwcn_bus_get_status() == WCN_BUS_DOWN) {
			wl_err("%s, suspend(%d) or bus down, drop skb!\n",
			       __func__, hif->suspend_mode);
			dev_kfree_skb(skb);
			return -1;
		}
		tx_get_pcie_dma_addr(hif, skb);
	}

	return 0;
}

int sc2355_tx(struct sprd_chip *chip, struct sprd_msg *msg)
{
	struct sprd_priv *priv = chip->priv;
	struct sprd_hif *hif = &priv->hif;
	unsigned int qos_index = 0;
	struct sprd_peer_entry *peer_entry = NULL;
	unsigned char tid = 0, tos = 0;
	struct tx_msdu_dscr *dscr = NULL;
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;

	if (-1 == tx_prepare_tx_msg(hif, msg))
		return -EPERM;

	if (msg->msglist == &tx_mgmt->tx_list_qos_pool) {
		struct sprd_qos_peer_list *data_list;
		dscr = (struct tx_msdu_dscr *)(msg->tran_data + hif->dscr_rsvd);
		qos_index =
		    sc2355_qos_get_tid_index(msg->skb, DSCR_LEN + hif->dscr_rsvd,
					     &tid, &tos);

		qos_index =
		    sc2355_qos_change_priority_if(hif->priv, &tid, &tos,
						  msg->len);
		wl_all("%s qos_index: %d tid: %d, tos:%d\n", __func__,
			 qos_index, tid, tos);
		if (qos_index == SPRD_AC_MAX) {
			INIT_LIST_HEAD(&msg->list);
			if (msg->skb) {
				dev_kfree_skb(msg->skb);
				msg->skb = NULL;
			}
			sprd_free_msg(msg, msg->msglist);
			return -EPERM;
		}
		/*send group in BK to avoid FW hang */
		if ((msg->mode == SPRD_MODE_AP ||
		     msg->mode == SPRD_MODE_P2P_GO) &&
		    sc2355_is_valid_group_lut(dscr->sta_lut_index)) {
			qos_index = SPRD_AC_BK;
			tid = prio_1;
			wl_all("%s, %d, SOFTAP/GO group go as BK\n", __func__,
				 __LINE__);
		} else {
			hif->tx_num[dscr->sta_lut_index]++;
		}
		dscr->buffer_info.msdu_tid = tid;
		peer_entry = &hif->peer_entry[dscr->sta_lut_index];
		sc2355_tx_prepare_addba(hif, dscr->sta_lut_index, peer_entry, tid);
		data_list =
		    &tx_mgmt->tx_list[msg->mode]->q_list[qos_index].p_list[dscr->sta_lut_index];
		tx_mgmt->tx_list[msg->mode]->lut_id = dscr->sta_lut_index;
		msg->data_list = data_list;

		if (hif->hw_type == SPRD_HW_SC2355_PCIE)
			msg->pcie_addr = sc2355_mm_virt_to_phys(&hif->pdev->dev,
								msg->tran_data,
								msg->len,
								DMA_TO_DEVICE);
		SAVE_ADDR(msg->tran_data - hif->hif_offset, msg, MSG_PTR_LEN);

		tx_enqueue_data_msg(msg, hif);
		atomic_inc(&tx_mgmt->tx_list[msg->mode]->mode_list_num);
	}

	if (msg->msg_type != SPRD_TYPE_DATA)
		sprd_queue_msg(msg, msg->msglist);

	if (msg->msg_type == SPRD_TYPE_CMD)
		sc2355_tx_up(tx_mgmt);
	if (msg->msg_type == SPRD_TYPE_DATA &&
	    ((hif->fw_awake == 0 &&
	      hif->fw_power_down == 1) || hif->fw_awake == 1))
		sc2355_tx_up(tx_mgmt);

	return 0;
}

int sc2355_tx_force_exit(struct sprd_chip *chip)
{
	struct sprd_priv *priv = chip->priv;
	struct sprd_hif *hif = &priv->hif;

	hif->exit = 1;
	return 0;
}

int sc2355_tx_is_exit(struct sprd_chip *chip)
{
	struct sprd_priv *priv = chip->priv;
	struct sprd_hif *hif = &priv->hif;

	return hif->exit;
}

int sc2355_reset(struct sprd_hif *hif)
{
	struct sprd_priv *priv = NULL;
	struct tx_mgmt *tx_mgmt = NULL;
	struct sprd_vif *vif, *tmp;
	int i;

	if (!hif) {
		wl_err("%s can not get hif!\n", __func__);
		return -1;
	}

	priv = hif->priv;
	if (!priv) {
		wl_err("%s can not get priv!\n", __func__);
		return -1;
	}

	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	if (!tx_mgmt) {
		wl_err("%s can not get tx mgmt!\n", __func__);
		return -1;
	}

	tx_mgmt->tx_hold_flag = 0;
	tx_mgmt->tx_hold_ctxid = MCC_DEFAULT_HOLD_CTXID;

	list_for_each_entry_safe(vif, tmp, &priv->vif_list, vif_node) {
		int ciphyr_type, key_index;
		int ciphyr_type_max = 2, key_index_max = 4;
		/* check connect state */
		if (vif->mode == SPRD_MODE_STATION ||
		    vif->mode == SPRD_MODE_P2P_CLIENT) {
			if (vif->sm_state == SPRD_DISCONNECTING ||
			    vif->sm_state == SPRD_CONNECTING ||
			    vif->sm_state == SPRD_CONNECTED) {
				wl_debug("%s check connection state for sta or p2p gc\n", __func__);
				wl_debug("vif->mode : %d, vif->sm_state : %d\n",
					vif->mode, vif->sm_state);
				cfg80211_disconnected(vif->ndev, 0, NULL, 0,
						      false, GFP_KERNEL);
				vif->sm_state = SPRD_DISCONNECTED;
			}
		}

		if (vif->mode == SPRD_MODE_AP) {
			wl_debug("softap mode, reset iftype to station, before reset:%d\n",
				vif->wdev.iftype);
			vif->wdev.iftype = NL80211_IFTYPE_STATION;
			wl_debug("after reset iftype:%d\n", vif->wdev.iftype);
		}
		if (vif->mode != SPRD_MODE_NONE) {
			wl_debug("need reset mode to none: %d\n", vif->mode);
			vif->state &= ~VIF_STATE_OPEN;
			sc2355_handle_tx_status_after_close(vif);
			vif->mode = SPRD_MODE_NONE;
			vif->ctx_id = 0;
		}

		/* reset ssid & bssid */
		memset(vif->bssid, 0, sizeof(vif->bssid));
		memset(vif->ssid, 0, sizeof(vif->ssid));
		vif->ssid_len = 0;
		vif->prwise_crypto = SPRD_CIPHER_NONE;
		vif->grp_crypto = SPRD_CIPHER_NONE;

		for (ciphyr_type = 0; ciphyr_type < ciphyr_type_max; ciphyr_type++) {
			vif->key_index[ciphyr_type] = 0;
			for (key_index = 0; key_index < key_index_max; key_index++) {
				memset(vif->key[ciphyr_type][key_index], 0x00,
				       WLAN_MAX_KEY_LEN);
				vif->key_len[ciphyr_type][key_index] = 0;
			}
		}
	}

	for (i = 0; i < 32; i++) {
		if (hif->peer_entry[i].ba_tx_done_map != 0) {
			hif->peer_entry[i].ht_enable = 0;
			hif->peer_entry[i].ip_acquired = 0;
			hif->peer_entry[i].ba_tx_done_map = 0;
		}
		sc2355_peer_entry_delba(hif, i);
		memset(&hif->peer_entry[i], 0x00,
		       sizeof(struct sprd_peer_entry));
		hif->peer_entry[i].ctx_id = 0xFF;
		hif->tx_num[i] = 0;
		sc2355_dis_flush_txlist(hif, i);
	}

	/* flush cmd and data buffer */
	wl_debug("%s flust all tx list\n", __func__);
	tx_flush_all_txlist(tx_mgmt);
	if (hif->hw_type == SPRD_HW_SC2355_PCIE ||
		hif->hw_type == SPRD_HW_SC2355_SIPC)
		sc2355_rx_flush_buffer((void *)hif);

	/* when cp2 hang and reset, clear hang_recovery_status */
	wl_debug("%s set hang recovery status to END, %d\n", __func__, __LINE__);
	tx_mgmt->hang_recovery_status = HANG_RECOVERY_END;
	tx_mgmt->thermal_status = THERMAL_TX_RESUME;

	/* bug 1985177, initial suspend mode, set to SPRD_PS_RESUMED */
	wl_debug("%s set suspend_mode to RESUMED, %d\n", __func__, __LINE__);
	hif->suspend_mode = SPRD_PS_RESUMED;

	/* need reset hif->exit flag, if wcn reset happened */
	if (unlikely(hif->exit)) {
		hif->exit = 0;
		wl_debug("%s reset hif->exit flag:%d!\n", __func__, hif->exit);
	}

	/* need reset hif->cp_assert flag */
	mutex_lock(&hif->reset_lock);
	if (unlikely(hif->cp_asserted)) {
		hif->cp_asserted = 0;
		hif->report_try = 0;
		wl_debug("%s %d cp_asserted:%d report_try:%d!\n",
			 __func__, __LINE__, hif->cp_asserted, hif->report_try);
	}
	mutex_unlock(&hif->reset_lock);

	hif->fw_awake = 1;
	hif->fw_power_down = 0;

	return 0;
}

#ifdef DRV_RESET_SELF
int sc2355_reset_self(struct sprd_priv *priv)
{
	struct sprd_vif *vif, *tmp;
	struct sprd_hif *hif;
	struct tx_mgmt *tx_msg;
	int i;

	if (!priv) {
		wl_err("%s can not get priv!\n", __func__);
		return -EINVAL;
	}
	hif = (struct sprd_hif *)(&priv->hif);
	if (!hif) {
		wl_err("%s can not get intf!\n", __func__);
		return -EINVAL;
	}
	tx_msg = (struct tx_mgmt *)hif->tx_mgmt;
	if (!tx_msg) {
		wl_err("%s can not get tx_msg!\n", __func__);
		return -EINVAL;
	}

	hif->drv_resetting = 1;
	wl_debug("enter %s\n", __func__);

	tx_mgmt->tx_hold_flag = 0;
	tx_mgmt->tx_hold_ctxid = MCC_DEFAULT_HOLD_CTXID;

	list_for_each_entry_safe(vif, tmp, &priv->vif_list, vif_node) {
		wl_debug("%s handle vif : name %s, mode %d, sm_state %d\n", __func__,
			vif->name, vif->mode, vif->sm_state);
		if (vif->mode == SPRD_MODE_STATION ||
		    vif->mode == SPRD_MODE_P2P_CLIENT) {
			if (vif->sm_state == SPRD_DISCONNECTING ||
			    vif->sm_state == SPRD_CONNECTING ||
			    vif->sm_state == SPRD_CONNECTED) {
				wl_debug("%s check connection state for sta or p2p gc\n", __func__);
				wl_debug("vif->mode : %d, vif->sm_state : %d\n",
					vif->mode, vif->sm_state);
				cfg80211_disconnected(vif->ndev, 0, NULL, 0,
					false, GFP_KERNEL);
					vif->sm_state = SPRD_DISCONNECTED;
			}
		}

		if (vif->ndev) {
			rtnl_lock();
			dev_close(vif->ndev);
			rtnl_unlock();
			wl_debug("%s dev_close %s!\n", __func__, vif->name);
		}

		if (vif->mode == SPRD_MODE_AP) {
			wl_debug("softap mode, reset iftype to station, before reset:%d\n",
				vif->wdev.iftype);
			//vif->wdev.iftype = NL80211_IFTYPE_STATION;
			wl_debug("after reset iftype:%d\n", vif->wdev.iftype);
			hif->drv_resetting = 0;
			return 0;
		}

		if (vif->mode != SPRD_MODE_NONE) {
			wl_all("need reset mode to none: %d\n", vif->mode);
			vif->state &= ~VIF_STATE_OPEN;
			sc2355_handle_tx_status_after_close(vif);
			vif->mode = SPRD_MODE_NONE;
			vif->ctx_id = 0;
		}
		/* reset ssid & bssid */
		memset(vif->bssid, 0, sizeof(vif->bssid));
		memset(vif->ssid, 0, sizeof(vif->ssid));
		vif->ssid_len = 0;
		vif->prwise_crypto = SPRD_CIPHER_NONE;
		vif->grp_crypto = SPRD_CIPHER_NONE;
		memset(vif->key_index, 0, sizeof(vif->key_index));
		memset(vif->key_len, 0, sizeof(vif->key_len));
		memset(vif->key, 0, sizeof(vif->key));
	}

	/* delba and flush qoslist and flush txlist*/
	for (i = 0; i < MAX_LUT_NUM; i++) {
		if (hif->peer_entry[i].ba_tx_done_map != 0) {
			hif->peer_entry[i].ht_enable = 0;
			hif->peer_entry[i].ip_acquired = 0;
			hif->peer_entry[i].ba_tx_done_map = 0;
		}
		sc2355_peer_entry_delba(hif, i);
		memset(&hif->peer_entry[i], 0x00,
		       sizeof(struct sprd_peer_entry));
		hif->peer_entry[i].ctx_id = 0xFF;
		hif->tx_num[i] = 0;
		sc2355_dis_flush_txlist(hif, i);
	}

	wl_debug("%s flust all tx list\n", __func__);
	tx_flush_all_txlist(tx_msg);

	wl_debug("%s initial hang status!\n", __func__);
	tx_msg->hang_recovery_status = HANG_RECOVERY_END;
	tx_msg->thermal_status = THERMAL_TX_RESUME;
	hif->suspend_mode = SPRD_PS_RESUMED;

	/* reset exit and cp_asserted flag */
	if (unlikely(hif->exit)) {
		hif->exit = 0;
		wl_debug("%s reset hif->exit flag:%d!\n", __func__, hif->exit);
	}

	if (unlikely(hif->cp_asserted)) {
		hif->cp_asserted = 0;
		wl_debug("%s reset hif->cp_asserted flag:%d!\n", __func__,
			hif->cp_asserted);
	}

	hif->fw_awake = 1;
	hif->fw_power_down = 0;
	atomic_set(&hif->power_cnt, 0);

	list_for_each_entry_safe(vif, tmp, &priv->vif_list, vif_node) {
		if (vif->ndev) {
			rtnl_lock();
			dev_open(vif->ndev, NULL);
			rtnl_unlock();
			wl_debug("%s open netdevice %s!\n", __func__, vif->name);
		} else {
			if (!sprd_iface_set_power(hif, true))
				sprd_init_fw(vif);
		}
	}
	wl_debug("exit %s\n", __func__);
	hif->drv_resetting = 0;

	return 0;
}
#endif

void sc2355_tx_drop_tcp_msg(struct sprd_chip *chip, struct sprd_msg *msg)
{
	enum sprd_mode mode;
	struct sprd_msg_list *list;
	struct sprd_priv *priv = chip->priv;
	struct sprd_hif *hif = &priv->hif;

	if (msg->skb) {
		dev_kfree_skb(msg->skb);
		msg->skb = NULL;
	}
	mode = msg->mode;
	list = msg->msglist;
	sprd_free_msg(msg, list);
	sc2355_wake_net_ifneed(hif, list, mode);
}

void sc2355_tx_down(struct tx_mgmt *tx_mgmt)
{
	wait_for_completion_interruptible(&tx_mgmt->tx_completed);
}

void sc2355_tx_up(struct tx_mgmt *tx_mgmt)
{
	complete(&tx_mgmt->tx_completed);
}

static void tx_hold_timeout(struct timer_list *t)
{
	struct tx_mgmt *tx_mgmt = timer_container_of(tx_mgmt, t, tx_hold_timer);

	if (tx_mgmt->tx_hold_ctxid == 0 || tx_mgmt->tx_hold_ctxid == 1) {
		tx_mgmt->tx_hold_flag = 1;  //sta --> softap, softap start tx
		tx_mgmt->tx_hold_ctxid = tx_mgmt->tx_hold_ctxid ? 0 : 1;
		sc2355_tx_up(tx_mgmt);
	} else {
		tx_mgmt->tx_hold_flag = 3; //reset
		sc2355_tx_up(tx_mgmt);
	}

	wl_debug("%s flow tx_hold_flag[%d] tx_hold_ctxid[%d]\n",
		__func__, tx_mgmt->tx_hold_flag, tx_mgmt->tx_hold_ctxid);
}


int sc2355_tx_init(struct sprd_hif *hif)
{
	int ret = 0;
	u8 i, j;
	struct tx_mgmt *tx_mgmt = NULL;

	tx_mgmt = kzalloc(sizeof(*tx_mgmt), GFP_KERNEL);
	if (!tx_mgmt) {
		ret = -ENOMEM;
		wl_err("%s kzalloc failed!\n", __func__);
		goto exit;
	}

	tx_mgmt->cmd_timeout = msecs_to_jiffies(SPRD_TX_CMD_TIMEOUT);
	tx_mgmt->data_timeout = msecs_to_jiffies(SPRD_TX_DATA_TIMEOUT);

	ret = sprd_init_msg(SPRD_TX_MSG_CMD_NUM, &tx_mgmt->tx_list_cmd);
	if (ret) {
		wl_err("%s tx_list_cmd alloc failed\n", __func__);
		goto err_tx_work;
	}

	ret = sprd_init_msg(SPRD_TX_QOS_POOL_SIZE, &tx_mgmt->tx_list_qos_pool);
	if (ret) {
		wl_err("%s tx_list_qos_pool alloc failed\n", __func__);
		goto err_tx_list_cmd;
	}

	for (i = 0; i < SPRD_MODE_MAX; i++) {
		tx_mgmt->tx_list[i] =
		    kzalloc(sizeof(struct qos_tx_t), GFP_KERNEL);
		if (!tx_mgmt->tx_list[i])
			goto err_txlist;
		sc2355_qos_init(tx_mgmt->tx_list[i]);
		atomic_set(&tx_mgmt->sipc_tx_cred[i], SIPC_TXRX_TX_BUF_MAX_NUM / 2);
	}
	tx_init_xmit_list(tx_mgmt);

	tx_mgmt->tx_thread_exit = 0;
	tx_mgmt->tx_thread = kthread_create(sc2355_tx_thread,
			       (void *)tx_mgmt, "SC2355_TX_THREAD");
	if (!tx_mgmt->tx_thread) {
		wl_err("%s SC2355_TX_THREAD create failed", __func__);
		ret = -ENOMEM;
		goto err_txlist;
	}

	hif->tx_mgmt = (void *)tx_mgmt;
	tx_mgmt->hif = hif;

	sprd_qos_reset_wmmac_parameters(tx_mgmt->hif->priv);
	sprd_qos_reset_wmmac_ts_info(hif->priv);

	for (i = 0; i < MAX_COLOR_BIT; i++) {
		tx_mgmt->flow_ctrl[i].mode = SPRD_MODE_NONE;
		tx_mgmt->flow_ctrl[i].color_bit = i;
		atomic_set(&tx_mgmt->flow_ctrl[i].flow, 0);
	}

	tx_mgmt->hang_recovery_status = HANG_RECOVERY_END;
	hif->remove_flag = 0;

	tx_mgmt->tx_hold_flag = 3;
	timer_setup(&tx_mgmt->tx_hold_timer, tx_hold_timeout, 0);

	init_completion(&tx_mgmt->tx_completed);
	wake_up_process(tx_mgmt->tx_thread);

	return ret;

err_txlist:
	for (j = 0; j < i; j++)
		kfree(tx_mgmt->tx_list[j]);

	sprd_deinit_msg(&tx_mgmt->tx_list_qos_pool);
err_tx_list_cmd:
	sprd_deinit_msg(&tx_mgmt->tx_list_cmd);
err_tx_work:
	kfree(tx_mgmt);
exit:
	return ret;
}

void sc2355_tx_deinit(struct sprd_hif *hif)
{
	struct tx_mgmt *tx_mgmt = NULL;
	u8 i;

	tx_mgmt = (void *)hif->tx_mgmt;

	/*let tx work queue exit */
	hif->exit = 1;
	hif->remove_flag = 1;

	if (tx_mgmt->tx_thread) {
		/*let tx_thread exit */
		tx_mgmt->tx_thread_exit = 1;
		sc2355_tx_up(tx_mgmt);
		kthread_stop(tx_mgmt->tx_thread);
		tx_mgmt->tx_thread = NULL;
	}

	/*need to check if there is some data and cmdpending
	 *or sending by HIF, and wait until tx complete and freed
	 */
	if (!list_empty(&tx_mgmt->tx_list_cmd.cmd_to_free))
		wl_err("%s cmd not yet transmited, cmd_send:%d, cmd_poped:%d\n",
		       __func__, tx_mgmt->cmd_send, tx_mgmt->cmd_poped);

	tx_flush_all_txlist(tx_mgmt);

	sprd_deinit_msg(&tx_mgmt->tx_list_cmd);
	sprd_deinit_msg(&tx_mgmt->tx_list_qos_pool);
	for (i = 0; i < SPRD_MODE_MAX; i++)
		kfree(tx_mgmt->tx_list[i]);
	kfree(tx_mgmt);
	hif->tx_mgmt = NULL;
}

bool sc2355_is_vowifi_pkt(struct sk_buff *skb, bool *b_cmd_path)
{
	bool ret = false;
	u8 dscp = 0;
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	unsigned char iphdrlen = 0;
	struct iphdr *iphdr;
	struct udphdr *udphdr;
	u32 mark;

	mark = skb->mark & DUAL_VOWIFI_MASK_MARK;
	wl_all("%s Dual vowifi: mark bits 0x%x\n", __func__, mark);
	switch (mark) {
	case DUAL_VOWIFI_NOT_SUPPORT:
		break;
	case DUAL_VOWIFI_SIP_MARK:
	case DUAL_VOWIFI_IKE_MARK:
		ret = true;
		(*b_cmd_path) = true;
		return ret;
	case DUAL_VOWIFI_VOICE_MARK:
	case DUAL_VOWIFI_VIDEO_MARK:
		ret = true;
		(*b_cmd_path) = false;
		return ret;

	default:
		wl_err("Dual vowifi: unexpect mark bits 0x%x\n", skb->mark);
		break;
	}

	if (ethhdr->h_proto != htons(ETH_P_IP))
		return false;

	iphdr = (struct iphdr *)(skb->data + ETHER_HDR_LEN);

	if (iphdr->protocol != IPPROTO_UDP)
		return false;

	iphdrlen = ip_hdrlen(skb);
	udphdr = (struct udphdr *)(skb->data + ETHER_HDR_LEN + iphdrlen);
	dscp = (iphdr->tos >> 2);
	switch (dscp) {
	case VOWIFI_IKE_DSCP:
		if (udphdr->dest == htons(VOWIFI_IKE_SIP_PORT) ||
		    udphdr->dest == htons(VOWIFI_IKE_ONLY_PORT)) {
			ret = true;
			(*b_cmd_path) = true;
		}
		break;
	case VOWIFI_SIP_DSCP:
		if (udphdr->dest == htons(VOWIFI_IKE_SIP_PORT)) {
			ret = true;
			(*b_cmd_path) = true;
		}
		break;
	case VOWIFI_VIDEO_DSCP:
	case VOWIFI_AUDIO_DSCP:
		ret = true;
		(*b_cmd_path) = false;
		break;
	default:
		ret = false;
		(*b_cmd_path) = false;
		break;
	}

	return ret;
}

void sc2355_tx_flush(struct sprd_hif *hif, struct sprd_vif *vif)
{
	u8 count = 0;
	struct tx_mgmt *tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;

	/*flush data belong to this mode */
	if (atomic_read(&tx_mgmt->tx_list[vif->mode]->mode_list_num) > 0)
		sc2355_flush_mode_txlist(tx_mgmt, vif->mode);

	/*here we need to wait for 3s to avoid there
	 *is still data of this modeattached to sdio not poped
	 */
	while ((!list_empty(&tx_mgmt->xmit_msg_list.to_send_list) ||
		!list_empty(&tx_mgmt->xmit_msg_list.to_free_list)) &&
	       count < 100) {
		printk_ratelimited("error! %s data q not empty, wait\n",
				   __func__);
		usleep_range(2500, 3000);
		count++;
	}
}

int sprd_tx_filter_packet(struct sk_buff *skb, struct net_device *ndev)
{
	struct sprd_vif *vif;
	struct sprd_hif *hif;
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;

	vif = netdev_priv(ndev);
	hif = &vif->priv->hif;

#if defined(MORE_DEBUG)
	if (ethhdr->h_proto == htons(ETH_P_ARP))
		hif->stats.tx_arp_num++;
	if (sc2355_is_group(skb->data))
		hif->stats.tx_multicast++;
#endif

	if (ethhdr->h_proto == htons(ETH_P_ARP)) {
		wl_info("incoming ARP packet\n");

		spin_lock_bh(&adap_info.adap_lock);
		wl_debug("%s special_data_flag: %d\n",
			 __func__, adap_info.special_data_flag);
		if ((adap_info.special_data_flag == SPRD_NPI_NORMAL_ALL) ||
		    ((adap_info.special_data_flag == SPRD_NPI_NORMAL_UNENCRYP) &&
		    (vif->prwise_crypto == SPRD_CIPHER_NONE))) {
				spin_unlock_bh(&adap_info.adap_lock);
				return 1;
			}
		spin_unlock_bh(&adap_info.adap_lock);

		sc2355_xmit_data2cmd_wq(skb, ndev);
		return NETDEV_TX_OK;
	}
	if (ethhdr->h_proto == htons(ETH_P_TDLS))
		wl_info("incoming TDLS packet\n");
	if (ethhdr->h_proto == htons(ETH_P_PREAUTH))
		wl_info("incoming PREAUTH packet\n");

	if (ethhdr->h_proto == htons(ETH_P_IP) ||
	    ethhdr->h_proto == htons(ETH_P_IPV6)) {
		if (!tx_mc_pkt(skb, ndev))
			return NETDEV_TX_OK;
		return tx_filter_ip_pkt(skb, ndev);
	}
	return 1;
}

int sc2355_tx_special_data(struct sk_buff *skb, struct net_device *ndev)
{
	int ret = -1;

	if (skb->protocol == cpu_to_be16(ETH_P_PAE) ||
		skb->protocol == cpu_to_be16(WAPI_TYPE)) {
		wl_err("send %s frame by CMD_TX_DATA\n",
		skb->protocol == cpu_to_be16(ETH_P_PAE) ? "802.1X" : "WAI");
		if (sc2355_xmit_data2cmd_wq(skb, ndev) == -EAGAIN)
			return NETDEV_TX_BUSY;
		return NETDEV_TX_OK;
	} else {
		ret = sprd_tx_filter_packet(skb, ndev);
		if (!ret)
			return NETDEV_TX_OK;
	}

	return ret;
}
/* if err, the caller judge the skb if need free,
 * here just free the msg buf to the freelist
 */
int sc2355_send_data(struct sprd_vif *vif, struct sprd_msg *msg,
		     struct sk_buff *skb, u8 type, u8 offset, bool flag)
{
	int ret;
	unsigned char *buf = NULL;
	struct sprd_hif *hif;
	unsigned int plen = cpu_to_le16(skb->len);

	hif = &vif->priv->hif;

	buf = skb->data;
	sc2355_tx_tp_statistic(skb->len);
	sprd_hif_tp_ctl_pd(hif);

	if (sc2355_hif_fill_msdu_dscr(vif, skb, SPRD_TYPE_DATA, offset)) {
		sprd_free_msg(msg, msg->msglist);
		return -EPERM;
	}

	/* msg->tran_data --> skb->data --> dscr_rsvd */
	sprd_fill_msg(msg, skb, skb->data, skb->len);

	if (sc2355_tcp_ack_filter_send(vif->priv, msg, buf, plen))
		return 0;

	ret = sprd_chip_tx(&vif->priv->chip, msg);
	if (ret)
		wl_err("%s TX data Err: %d\n", __func__, ret);

	if (hif->tdls_flow_count_enable == 1 &&
	    vif->sm_state == SPRD_CONNECTED) {
		sc2355_tdls_count_flow(vif, buf, plen);
	}

	return ret;
}

int sc2355_needed_headroom(struct sprd_priv *priv)
{
/*
 * data path min headroom : sdio = 28, pcie/sipc = 24
 * tmp_flag : only used for data2cmd
 * | 8       | 4/0        | 0/5       | 11        | eth_data |
 * | msg_ptr | hif_offset | dscr_rsvd | msdu_dscr | eth_data |
 */
	struct sprd_hif *hif = &priv->hif;

	return (MSG_PTR_LEN + hif->hif_offset +
		hif->dscr_rsvd + DSCR_LEN);
}

void sc2355_tx_addba(struct sprd_hif *hif,
		     struct sprd_peer_entry *peer_entry, unsigned char tid)
{
#define WIN_SIZE 64
	struct host_addba_param addba;
	struct sprd_work *misc_work;
	struct sprd_vif *vif;
	u8 *work_data = NULL;

	vif = sc2355_ctxid_to_vif(hif->priv, peer_entry->ctx_id);
	if (!vif)
		return;
	memset(&addba, 0x0, sizeof(struct host_addba_param));

	addba.lut_index = peer_entry->lut_index;
	ether_addr_copy(addba.perr_mac_addr, peer_entry->tx.da);
	wl_info("%s, lut=%d, tid=%d\n", __func__, peer_entry->lut_index, tid);
	addba.dialog_token = 1;
	addba.addba_param.amsdu_permit = 0;
	addba.addba_param.ba_policy = DOT11_ADDBA_POLICY_IMMEDIATE;
	addba.addba_param.tid = tid;
	addba.addba_param.buffer_size = WIN_SIZE;
	misc_work = sprd_alloc_work(sizeof(struct host_addba_param));
	if (!misc_work) {
		wl_err("%s out of memory\n", __func__);
		sprd_put_vif(vif);
		return;
	}
	misc_work->vif = vif;
	misc_work->id = SPRD_WORK_ADDBA;
	misc_work->hw_type = hif->hw_type;
	work_data = misc_work->data;
	memcpy(work_data, &addba, sizeof(struct host_addba_param));

	sprd_queue_work(vif->priv, misc_work);
	sprd_put_vif(vif);
}


void sc2355_tx_delba(struct sprd_hif *hif,
		     struct sprd_peer_entry *peer_entry, unsigned int ac_index)
{
	struct host_delba_param delba;
	struct sprd_work *misc_work;
	struct sprd_vif *vif;
	u8 *work_data = NULL;

	vif = sc2355_ctxid_to_vif(hif->priv, peer_entry->ctx_id);
	if (!vif)
		return;
	memset(&delba, 0x0, sizeof(delba));

	wl_info("enter--at %s\n", __func__);
	ether_addr_copy(delba.perr_mac_addr, peer_entry->tx.da);
	delba.lut_index = peer_entry->lut_index;
	delba.delba_param.initiator = 1;
	delba.delba_param.tid = qos_index_2_tid(ac_index);
	delba.reason_code = 0;

	misc_work = sprd_alloc_work(sizeof(struct host_delba_param));
	if (!misc_work) {
		wl_err("%s out of memory\n", __func__);
		sprd_put_vif(vif);
		return;
	}
	misc_work->vif = vif;
	misc_work->id = SPRD_WORK_DELBA;
	misc_work->hw_type = hif->hw_type;
	work_data = misc_work->data;
	memcpy(work_data, &delba, sizeof(struct host_delba_param));

	clear_bit(qos_index_2_tid(ac_index), &peer_entry->ba_tx_done_map);

	sprd_queue_work(vif->priv, misc_work);
	sprd_put_vif(vif);
}

void sc2355_tx_ba_mgmt(struct sprd_priv *priv, struct sprd_vif *vif,
			    void *data, int len, unsigned char cmd_id)
{
	struct sprd_msg *msg;
	unsigned char *data_ptr;
	u8 *rbuf;
	u16 rlen = (1 + sizeof(struct host_addba_param));
	int ret = 0;

	msg = get_cmdbuf(priv, vif, len, cmd_id);
	if (!msg) {
		wl_err("%s, %d, get msg err\n", __func__, __LINE__);
		return;
	}

	rbuf = kzalloc(rlen, GFP_KERNEL);
	if (!rbuf)
		return;

	memcpy(msg->data, data, len);
	data_ptr = (unsigned char *)data;
	ret = send_cmd_recv_rsp(priv, msg, rbuf, &rlen);
	if (ret || rlen == 0)
		goto out;
	/*if tx ba req failed, need to clear txba map*/
	if (cmd_id == CMD_ADDBA_REQ && rbuf[0] != ADDBA_REQ_RESULT_SUCCESS) {
		struct host_addba_param *addba;
		struct sprd_peer_entry *peer_entry = NULL;
		struct sprd_hif *hif = &priv->hif;
		u16 tid = 0;

		addba = (struct host_addba_param *)(rbuf + 1);
		if (addba->lut_index >= MAX_LUT_NUM || addba->addba_param.tid >= NUM_TIDS) {
			wl_err("TID/lut_index is too large, lut_index: %d, tid: %d\n",
				addba->lut_index, addba->addba_param.tid);
			goto out;
		}
		peer_entry = &hif->peer_entry[addba->lut_index];
		tid = addba->addba_param.tid;
		if (!test_and_clear_bit(tid, &peer_entry->ba_tx_done_map))
			goto out;
		wl_err
		    ("%s, %d, tx_addba failed, reason=%d, lut_index=%d, tid=%d, map=%lu\n",
		     __func__, __LINE__, rbuf[0], addba->lut_index, tid,
		     peer_entry->ba_tx_done_map);
	}
out:
	kfree(rbuf);
}

void sc2355_tx_send_addba(struct sprd_vif *vif, void *data, int len)
{
  	sc2355_tx_ba_mgmt(vif->priv, vif, data, len, CMD_ADDBA_REQ);
}

void sc2355_tx_send_delba(struct sprd_vif *vif, void *data, int len)
{
	struct host_delba_param *delba;

	delba = (struct host_delba_param *)data;
	sc2355_tx_ba_mgmt(vif->priv, vif, delba,
			sizeof(struct host_delba_param), CMD_DELBA_REQ);
}

int sc2355_dis_flush_txlist(struct sprd_hif *hif, u8 lut_index)
{
	struct tx_mgmt *tx_mgmt;
	int i, j;

	if (sc2355_is_valid_group_lut(lut_index)) {
		wl_info("lut_index:%d, %s, %d\n",
		       lut_index, __func__, __LINE__);
		return -1;
	}
	wl_debug("disconnect, flush qoslist, %s, %d\n", __func__, __LINE__);
	tx_mgmt = (struct tx_mgmt *)hif->tx_mgmt;
	for (i = 0; i < SPRD_MODE_MAX; i++)
		for (j = 0; j < SPRD_AC_MAX; j++)
			sc2355_flush_tx_qoslist(tx_mgmt, i, j, lut_index);
	return 0;
}

void sc2355_tx_send_action(struct sprd_vif *vif, void *data, int len)
{
	u8 channel = *(u8 *)data;
	u8 *buf = (u8 *)data + 1;
	u32 wait = 0;
	u8 dont_wait_for_ack = 1;
	static u64 action_index;
	int ret = 0;
	u64 cookie;

	cookie = ++action_index;
	wl_info("%s cookie %lld, wait %d, ack %d\n", __func__, cookie, wait, dont_wait_for_ack);

	/* send tx mgmt */
	if (len > 0) {
		ret = sc2355_tx_mgmt(vif->priv, vif, channel, dont_wait_for_ack, wait, &cookie,
				     buf, len - 1);
		if (ret || vif->priv->tx_mgmt_status) {
			wl_info("%s ret %d, status %d\n", __func__, ret, vif->priv->tx_mgmt_status);
			vif->priv->tx_mgmt_status = 0;
		}
	}
}

