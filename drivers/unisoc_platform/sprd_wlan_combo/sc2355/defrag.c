/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#include "common/debug.h"
#include "defrag.h"
#include "rx.h"
#include "cmdevt.h"

static struct rx_defrag_node
*defrag_find_defrag_node(struct rx_defrag_entry *defrag_entry,
			 struct rx_msdu_desc *msdu_desc)
{
	struct rx_defrag_node *node = NULL, *pos_node = NULL;

	list_for_each_entry(pos_node, &defrag_entry->list, list) {
		if (pos_node->desc.sta_lut_index ==
		    msdu_desc->sta_lut_index &&
		    pos_node->desc.tid == msdu_desc->tid) {
			if (pos_node->desc.seq_num == msdu_desc->seq_num &&
			    (pos_node->last_frag_num + 1) ==
			    msdu_desc->frag_num) {
				/* Node alive & fragment avail */
				pos_node->last_frag_num = msdu_desc->frag_num;
				wl_all("%s: last_frag_num: %d\n",
					 __func__, pos_node->last_frag_num);
				node = pos_node;
			}
			break;
		}
	}

	return node;
}

static inline void defrag_init_first_frag_node(struct rx_defrag_node *node,
					       struct rx_msdu_desc *msdu_desc)
{
	node->desc.sta_lut_index = msdu_desc->sta_lut_index;
	node->desc.tid = msdu_desc->tid;
	node->desc.frag_num = msdu_desc->frag_num;
	node->desc.seq_num = msdu_desc->seq_num;

	if (!skb_queue_empty(&node->skb_list))
		skb_queue_purge(&node->skb_list);

	if (likely(msdu_desc->snap_hdr_present))
		node->msdu_len = ETH_HLEN + msdu_desc->msdu_offset;
	else
		node->msdu_len = 2 * ETH_ALEN + msdu_desc->msdu_offset;

	node->last_frag_num = msdu_desc->frag_num;
}

static struct rx_defrag_node
*defrag_init_first_defrag_node(struct rx_defrag_entry *defrag_entry,
			struct rx_msdu_desc *msdu_desc)
{
	struct rx_defrag_node *node = NULL, *pos_node = NULL;
	bool ret = true;

	/* Check whether this entry alive or this fragment avail */
	list_for_each_entry(pos_node, &defrag_entry->list, list) {
		if (pos_node->desc.sta_lut_index ==
		    msdu_desc->sta_lut_index &&
		    pos_node->desc.tid == msdu_desc->tid) {
			if (!seqno_leq(msdu_desc->seq_num,
				       pos_node->desc.seq_num)) {
				/* Replace this entry */
				wl_err("%s: fragment replace: %d, %d\n",
				       __func__, msdu_desc->seq_num,
				       pos_node->desc.seq_num);
				node = pos_node;
			} else {
				/* fragment not avail */
				wl_err("%s: fragment not avail: %d, %d\n",
				       __func__, msdu_desc->seq_num,
				       pos_node->desc.seq_num);
				ret = false;
			}
			break;
		}
	}

	if (ret) {
		if (!node) {
			/* Get the empty or oldest entry
			 * HW just maintain three fragLUTs
			 * just kick out oldest entry (Should it happen?)
			 */
			node = list_entry(defrag_entry->list.prev,
					  struct rx_defrag_node, list);
		}
		defrag_init_first_frag_node(node, msdu_desc);

		/* Move this node to head */
		if (defrag_entry->list.next != &node->list)
			list_move(&node->list, &defrag_entry->list);
	}

	return node;
}

static struct rx_defrag_node
*defrag_get_defrag_node(struct rx_defrag_entry *defrag_entry,
		 struct rx_msdu_desc *msdu_desc)
{
	struct rx_defrag_node *node = NULL;

	wl_all("%s: frag_num: %d\n", __func__, msdu_desc->frag_num);

	/* HW do not record entry time when HW suspend
	 * So we need to judge whether this entry is alive
	 */
	if (msdu_desc->frag_num) {
		/* Check whether this entry alive or this fragment avail */
		node = defrag_find_defrag_node(defrag_entry, msdu_desc);
	} else {
		node = defrag_init_first_defrag_node(defrag_entry, msdu_desc);
	}

	if (!node)
		wl_err("%s, node is null!\n", __func__);
	return node;
}

static struct sk_buff
*single_data_process(struct rx_defrag_entry *defrag_entry, struct sk_buff *pskb)
{
	struct rx_defrag_node *node = NULL;
	struct rx_msdu_desc *msdu_desc = (struct rx_msdu_desc *)pskb->data;
	unsigned short offset = 0, frag_len = 0, frag_offset = 0;
	struct sk_buff *skb = NULL, *pos_skb = NULL;
	u64 pkt_pn;

	node = defrag_get_defrag_node(defrag_entry, msdu_desc);
	if (node) {
		/* bug2522383:for FFD-4.5.1 */
		if (msdu_desc->cipher_type != SPRD_HW_NO_CIPHER) {
			pkt_pn = ((u64)msdu_desc->pn_h << 32) | msdu_desc->pn_l;
			if (msdu_desc->frag_num && (node->desc.pn + 1) != pkt_pn) {
				wl_err("%s, frag encrypted with non-consec PNs(%lld-%lld),drop!\n",
				       __func__, node->desc.pn, pkt_pn);
				node->last_frag_num = 0;
				if (!skb_queue_empty(&node->skb_list))
					skb_queue_purge(&node->skb_list);
				dev_kfree_skb(pskb);
				goto exit;
			}

			node->desc.pn = pkt_pn;
		}
		skb_queue_tail(&node->skb_list, pskb);
		if (msdu_desc->snap_hdr_present)
			frag_len = msdu_desc->msdu_len - ETH_HLEN;
		else
			frag_len = msdu_desc->msdu_len - 2 * ETH_ALEN;
		node->msdu_len += frag_len;

		wl_all("%s: more_frag_bit: %d, node msdu_len: %d\n",
			 __func__, msdu_desc->more_frag_bit, node->msdu_len);
		if (!msdu_desc->more_frag_bit) {
			skb = skb_dequeue(&node->skb_list);
			if (!skb) {
				wl_err("%s:get skb buffer failed\n", __func__);
				return NULL;
			}
			msdu_desc = (struct rx_msdu_desc *)skb->data;
			offset = msdu_total_len(msdu_desc);
			msdu_desc->msdu_len =
			    node->msdu_len - msdu_desc->msdu_offset;

			pos_skb = dev_alloc_skb(node->msdu_len);
			if (unlikely(!pos_skb)) {
				/* Free all skbs */
				wl_err("%s: expand skb fail\n", __func__);
				skb_queue_purge(&node->skb_list);
				dev_kfree_skb(skb);
				skb = NULL;
				goto exit;
			}

			memcpy(pos_skb->data, skb->data, offset);
			dev_kfree_skb(skb);
			skb = pos_skb;

			while ((pos_skb = skb_dequeue(&node->skb_list))) {
				msdu_desc =
				    (struct rx_msdu_desc *)pos_skb->data;
				if (msdu_desc->snap_hdr_present) {
					frag_len = msdu_desc->msdu_len -
					    ETH_HLEN;
					frag_offset = msdu_desc->msdu_offset +
					    ETH_HLEN;
				} else {
					frag_len = msdu_desc->msdu_len -
					    2 * ETH_ALEN;
					frag_offset = msdu_desc->msdu_offset +
					    2 * ETH_ALEN;
				}

				wl_all("%s: frag_len: %d, frag_offset: %d\n",
					 __func__, frag_len, frag_offset);
				memcpy((skb->data + offset),
				       (pos_skb->data + frag_offset), frag_len);
				offset += frag_len;

				dev_kfree_skb(pos_skb);
			}

			sc2355_fill_skb_csum(skb, 0);
			skb->next = NULL;
exit:
			/* Move this entry to tail */
			if (!list_is_last(&node->list, &defrag_entry->list))
				list_move_tail(&node->list,
					       &defrag_entry->list);
		}
	} else {
		dev_kfree_skb(pskb);
	}

	return skb;
}

struct sk_buff
*sc2355_defrag_data_process(struct rx_defrag_entry *defrag_entry,
			    struct sk_buff *pskb)
{
	struct rx_msdu_desc *msdu_desc = (struct rx_msdu_desc *)pskb->data;
	struct sk_buff *skb = NULL;

	if (!msdu_desc->frag_num && !msdu_desc->more_frag_bit)
		skb = pskb;
	else
		skb = single_data_process(defrag_entry, pskb);

	return skb;
}

int sc2355_defrag_init(struct rx_defrag_entry *defrag_entry)
{
	int i = 0;
	struct rx_defrag_node *node = NULL;
	int ret = 0;

	INIT_LIST_HEAD(&defrag_entry->list);

	for (i = 0; i < MAX_DEFRAG_NUM; i++) {
		node = kzalloc(sizeof(*node), GFP_KERNEL);
		if (likely(node)) {
			skb_queue_head_init(&node->skb_list);
			list_add(&node->list, &defrag_entry->list);
		} else {
			wl_err("%s: fail to alloc rx_defrag_node\n", __func__);
			ret = -ENOMEM;
			break;
		}
	}

	return ret;
}

void sc2355_defrag_deinit(struct rx_defrag_entry *defrag_entry)
{
	struct rx_defrag_node *node = NULL, *pos_node = NULL;

	list_for_each_entry_safe(node, pos_node, &defrag_entry->list, list) {
		list_del(&node->list);
		if (!skb_queue_empty(&node->skb_list))
			skb_queue_purge(&node->skb_list);
		kfree(node);
	}
}

void sc2355_defrag_recover(struct sprd_vif *vif, unsigned char lut_index)
{
	struct sprd_hif *hif;
	struct rx_mgmt *rx_mgmt;
	struct rx_defrag_entry *defrag_entry;
	struct rx_defrag_node *node = NULL, *pos_node = NULL;

	hif = &vif->priv->hif;
	rx_mgmt = (struct rx_mgmt *)hif->rx_mgmt;
	defrag_entry = &rx_mgmt->defrag_entry;

	list_for_each_entry_safe(node, pos_node, &defrag_entry->list, list) {
		if (lut_index == node->desc.sta_lut_index) {
			if (!skb_queue_empty(&node->skb_list)) {
				skb_queue_purge(&node->skb_list);
				wl_err("%s:defrag clear cache\n", __func__);
			}
			wl_err("%s:msdu len %d\n", __func__, node->msdu_len);
			memset(&node->desc, 0, sizeof(node->desc));
			node->msdu_len = 0;
			node->last_frag_num = 0;
		}
	}
}
