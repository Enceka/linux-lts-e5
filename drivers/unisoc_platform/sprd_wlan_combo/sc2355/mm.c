/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#include "cmdevt.h"
#include "common/common.h"
#include "rx.h"
#include "sipc_buf.h"

#define GET_NEXT_ADDR_TRANS_VALUE(value, offset) \
	((struct addr_trans_value *)((unsigned char *)(value) + (offset)))

#define SKB_SHARED_INFO_OFFSET \
	SKB_DATA_ALIGN(SPRD_MAX_DATA_RXLEN + NET_SKB_PAD)

#define MAX_RETRY_NUM 8

#define ALIGN_8BYTE(a) (((a) + 7) & ~7)

static bool mm_check_mh_buffer(struct device *dev, void *buffer, dma_addr_t pa,
			       size_t size, enum dma_data_direction direction)
{
	int retry = 0;
	bool flag = true;

	if (direction == DMA_FROM_DEVICE) {
		struct rx_msdu_desc *desc = buffer + sizeof(struct rx_mh_desc);

		/* Check whether this buffer is ok to use */
		while ((desc->data_write_done != 1) &&
		       (retry < MAX_RETRY_NUM)) {
			wl_err("%s: hw still writing: 0x%lx, 0x%lx\n",
			       __func__, (unsigned long)buffer,
			       (unsigned long)pa);
			/* FIXME: Should we delay here? */
			dma_sync_single_for_cpu(dev, pa, size, direction);
			retry++;
		}
	}

	if (retry >= MAX_RETRY_NUM) {
		/* TODO: How to deal with this situation? */
		flag = false;
	}

	return flag;
}

static void mm_clear_mh_buffer(void *buffer)
{
	struct rx_msdu_desc *desc = buffer + sizeof(struct rx_mh_desc);

	desc->data_write_done = 0;
}

static inline bool mm_is_compound_data(struct mem_mgmt *mm_entry, void *data)
{
	struct rx_msdu_desc *msdu_desc =
	    (struct rx_msdu_desc *)(data + mm_entry->hif_offset);

	wl_all("%s: short_pkt_num: %d\n", __func__, msdu_desc->short_pkt_num);

	return (msdu_desc->short_pkt_num > 1);
}

bool sc2355_check_rx_pcie_addr(unsigned long pcie_addr, struct mem_mgmt *mm_entry)
{

	unsigned long check_pcie_addr = 0, flags = 0;
	bool flag = false;
	struct sk_buff_head *list = NULL;
	struct sk_buff *skb = NULL;

	list = &mm_entry->buffer_list;

	spin_lock_irqsave(&list->lock, flags);
	for (skb = (list)->next; skb != (struct sk_buff *)(list); skb = skb->next) {
		memcpy(&check_pcie_addr, (skb->data - (SPRD_PHYS_LEN + SKB_ADDR_LEN)),
					SPRD_PHYS_LEN);
		check_pcie_addr &= SPRD_PHYS_MASK;
		if (check_pcie_addr == pcie_addr) {
			flag = true;
			break;
		}
	}
	spin_unlock_irqrestore(&list->lock, flags);

	return flag;
}

static inline struct sk_buff *mm_build_skb(void *data, int len, int buffer_type)
{
	return build_skb(data, (buffer_type ? len : 0));
}

static struct sk_buff
*mm_data2skb_process(struct mem_mgmt *mm_entry, void *data, int len)
{
	struct sk_buff *skb = NULL;

	skb = dev_alloc_skb(len);
	if (likely(skb))
		memcpy(skb->data, data, len);

	return skb;
}

static inline void mm_free_addr_buf(struct mem_mgmt *mm_entry)
{
	void *p = (mm_entry->hdr - mm_entry->hif_offset);

	kfree(p);
	mm_entry->hdr = NULL;
	mm_entry->addr_trans = NULL;
}

static inline void mm_alloc_addr_buf(struct mem_mgmt *mm_entry,  struct sprd_hif *hif)
{
	struct addr_trans_value *value = NULL;
	struct sprd_addr_hdr *hdr = NULL;
	void *p = NULL;
	unsigned int sprd_addr_buf_len;

	if (hif->hw_type == SPRD_HW_SC2355_PCIE)
		sprd_addr_buf_len =
			SPRD_PCIE_ADDR_BUF_LEN;
	else
		sprd_addr_buf_len =
			SPRD_SIPC_ADDR_BUF_LEN;

	p = kmalloc((mm_entry->hif_offset + sprd_addr_buf_len), GFP_KERNEL);
	if (likely(p)) {
		hdr = (struct sprd_addr_hdr *)(p + mm_entry->hif_offset);
		value = (struct addr_trans_value *)hdr->paydata;

		/* Tell CP that this CMD is used to add MH buffer */
		hdr->common.reserv = 1;
		/* NOTE: CP do not care ctx_id & rsp */
		hdr->common.mode = 0;
		hdr->common.rsp = 0;
		hdr->common.type = SPRD_TYPE_DATA_PCIE_ADDR;

		value->type = 0;
		value->num = 0;
	}

	mm_entry->hdr = (void *)hdr;
	mm_entry->addr_trans = (void *)value;
}

static inline int mm_do_addr_buf(struct mem_mgmt *mm_entry)
{
	struct rx_mgmt *rx_mgmt =
	    container_of(mm_entry, struct rx_mgmt, mm_entry);
	struct addr_trans_value *value =
	    (struct addr_trans_value *)mm_entry->addr_trans;
	struct sprd_hif *hif = rx_mgmt->hif;
	struct sprd_priv *priv = hif->priv;
	struct sprd_vif *vif = NULL, *tmp_vif;
	int ret = 0;
	int addr_trans_len = 0;
	unsigned char sprd_max_add_mh_buf_once;

	if (hif->hw_type == SPRD_HW_SC2355_PCIE)
		sprd_max_add_mh_buf_once =
			SPRD_PCIE_MAX_ADD_MH_BUF_ONCE;
	else
		sprd_max_add_mh_buf_once =
			SPRD_SIPC_MAX_ADD_MH_BUF_ONCE;

	/* NOTE: addr_buf should be allocating after being sent,
	 *       JUST avoid addr_buf allocating fail after being sent here
	 */
	if (!hif->fw_awake) {
		wl_info("%s, fw power save, need to wake up it!\n", __func__);
		spin_lock_bh(&priv->list_lock);
		list_for_each_entry(tmp_vif, &priv->vif_list, vif_node) {
			if (tmp_vif->state & VIF_STATE_OPEN) {
				vif = tmp_vif;
				break;
			}
		}
		spin_unlock_bh(&priv->list_lock);

		if (!vif)
			return -EIO;
		if (!sc2355_cmd_host_wakeup_fw(vif->priv, vif)) {
			hif->fw_power_down = 0;
		} else {
			wl_err("%s, wake up fw failed!!\n", __func__);
			return -EIO;
		}
	}

	if (unlikely(!value)) {
		wl_all("%s: addr buf is NULL, re-alloc here\n", __func__);
		mm_alloc_addr_buf(mm_entry, hif);
		if (unlikely(!mm_entry->addr_trans)) {
			wl_err("%s: alloc addr buf fail!\n", __func__);
			ret = -ENOMEM;
		}
	} else if (value->num >= sprd_max_add_mh_buf_once) {
		addr_trans_len = sizeof(struct sprd_addr_hdr) +
		    sizeof(struct addr_trans_value) +
		    (value->num * SPRD_PHYS_LEN);

		/* FIXME: temporary solution, would TX supply API for us? */
		/* TODO: How to do with tx fail? */
		if (hif->ops->tx_addr_trans == NULL)
			return -EIO;
		if ((hif->ops->tx_addr_trans(hif, mm_entry->hdr,
					  addr_trans_len, false) >= 0)) {
			mm_alloc_addr_buf(mm_entry, hif);
			if (unlikely(!mm_entry->addr_trans)) {
				wl_err("%s: alloc addr buf fail!\n", __func__);
				ret = -ENOMEM;
			}
		} else {
			wl_err("%s: send addr buf fail!\n", __func__);
			ret = -EIO;
		}
	}

	return ret;
}

static int mm_w_addr_buf(struct mem_mgmt *mm_entry, unsigned long pcie_addr)
{
	int ret = 0;
	struct addr_trans_value *value = NULL;

	ret = mm_do_addr_buf(mm_entry);
	if (!ret) {
		value = (struct addr_trans_value *)mm_entry->addr_trans;

		/* NOTE: MH is little endian */
		memcpy((void *)value->address[value->num],
		       &pcie_addr, SPRD_PHYS_LEN);
		value->num++;
		/* do not care the result here */
		mm_do_addr_buf(mm_entry);
	}

	return ret;
}

static int mm_single_buffer_alloc(struct mem_mgmt *mm_entry)
{
	struct rx_mgmt *rx_mgmt =
	    container_of(mm_entry, struct rx_mgmt, mm_entry);
	struct sk_buff *skb = NULL, *temp_skb = NULL;
	unsigned long pcie_addr = 0;
	int ret = -ENOMEM;
	void *buff = NULL, *pad = NULL;
	struct sipc_buf_node *node = NULL;
	struct sipc_buf_mm *rx_mm = NULL;

	skb = dev_alloc_skb(SPRD_MAX_DATA_RXLEN);
	if (skb) {
		if (skb_headroom(skb) < (SPRD_PHYS_LEN + SKB_ADDR_LEN)) {
			temp_skb = skb;
			skb = skb_realloc_headroom(skb, (SPRD_PHYS_LEN + SKB_ADDR_LEN));
			dev_kfree_skb(temp_skb);
			temp_skb = NULL;
			if (skb == NULL) {
				wl_err("%s: %d failed to unshare skbbuff: NULL\n",
						__func__, __LINE__);
				return -EPERM;
			}
		}
		/* hook skb address after skb end
		 * first 64 bits of skb_shared_info are
		 * nr_frags, tx_flags, gso_size, gso_segs, gso_type
		 * It could be re-used and MUST clean after using
		 */
		//memcpy((void *)skb_end_pointer(skb), &skb, sizeof(skb));
		buff = skb->data;

		if (SPRD_HW_SC2355_SIPC == rx_mgmt->hif->hw_type) {
			rx_mm = rx_mgmt->hif->sipc_mm->rx_buf;
			node = sipc_rx_alloc_node_buf(rx_mgmt->hif);
			if (node) {
				memcpy(skb->data, &node, sizeof(node));
				pad = node->buf + SPRD_MAX_DATA_RXLEN;
				memcpy_toio(pad, &node, sizeof(node));
				buff = node->buf;
				node->priv = (void *)skb;
			} else {
				wl_all("%s: Node list is NULL! \n", __func__);
				dev_kfree_skb(skb);
				return ret;
			}
		}
		SAVE_ADDR(skb->data, skb, sizeof(struct sk_buff *));
		/* transfer virt to phys */
		if (SPRD_HW_SC2355_PCIE == rx_mgmt->hif->hw_type)
			pcie_addr = sc2355_mm_virt_to_phys(&rx_mgmt->hif->pdev->dev,
							skb->data,
							SPRD_MAX_DATA_RXLEN,
							DMA_FROM_DEVICE);

		if (SPRD_HW_SC2355_SIPC == rx_mgmt->hif->hw_type) {
			pcie_addr = ((unsigned long)buff - rx_mm->offset) | SPRD_MH_ADDRESS_BIT;
			pcie_addr = pcie_addr & SPRD_MH_SIPC_ADDRESS_BIT;
			wl_all("%s: sipc_addr is 0x%lx. \n", __func__, pcie_addr);
		}
		memcpy((skb->data - (SPRD_PHYS_LEN + SKB_ADDR_LEN)), &pcie_addr, SPRD_PHYS_LEN);

		if (likely(pcie_addr)) {
			ret = mm_w_addr_buf(mm_entry, pcie_addr);
			if (ret) {
				wl_err("%s: write addr buf fail: %d\n",
				       __func__, ret);
				if (SPRD_HW_SC2355_SIPC == rx_mgmt->hif->hw_type) {
					node->priv = NULL;
					sipc_free_node_buf(node, &rx_mm->nlist);
				}
				dev_kfree_skb(skb);
			} else {
				/* queue skb */
				skb_queue_tail(&mm_entry->buffer_list, skb);
				if (SPRD_HW_SC2355_SIPC == rx_mgmt->hif->hw_type)
					sipc_queue_node_buf(node, &rx_mm->nlist);
			}
		}
	} else {
		wl_err("%s: alloc skb fail\n", __func__);
	}

	return ret;
}

static struct sk_buff *mm_single_buffer_unlink(struct mem_mgmt *mm_entry,
					       unsigned long pcie_addr)
{
	struct rx_mgmt *rx_mgmt =
	    container_of(mm_entry, struct rx_mgmt, mm_entry);
	struct sk_buff *skb = NULL;
	void *buffer = NULL;
	struct sipc_buf_node *node = NULL;
	unsigned long phy_addr = 0;
	struct sipc_buf_mm *rx_buf = NULL;
	unsigned long flags = 0;
	struct sprd_msg_list *list;

	if (!sc2355_check_rx_pcie_addr(pcie_addr, mm_entry)) {
		wl_err("%s pcie_addr is wrong\n", __func__);
		return NULL;
	}

	if (rx_mgmt->hif->hw_type == SPRD_HW_SC2355_PCIE) {
		buffer = sc2355_mm_phys_to_virt(&rx_mgmt->hif->pdev->dev, pcie_addr,
						SPRD_MAX_DATA_RXLEN, DMA_FROM_DEVICE,
						true);
		if (buffer == NULL) {
			wl_err("%s buffer is null\n", __func__);
			return NULL;
		}

		RESTORE_ADDR(skb, buffer, sizeof(struct sk_buff *));
		if (IS_ERR(skb)) {
			wl_err("%s skb is not correct. please check!\n", __func__);
			return NULL;
		}

		skb_unlink(skb, &mm_entry->buffer_list);
		CLEAR_ADDR(skb->data, sizeof(struct sk_buff *));
		memset((skb->data - (SPRD_PHYS_LEN + SKB_ADDR_LEN)), 0x00, SPRD_PHYS_LEN);
	} else if (rx_mgmt->hif->hw_type == SPRD_HW_SC2355_SIPC) {
		rx_buf = rx_mgmt->hif->sipc_mm->rx_buf;
		phy_addr = pcie_addr & (~(SPRD_MH_ADDRESS_BIT) & SPRD_PHYS_MASK);
		phy_addr |= SPRD_MH_SIPC_ADDRESS_BASE;
		buffer = (void *)(phy_addr + rx_buf->offset);

		list = &(rx_buf->nlist);
		spin_lock_irqsave(&list->busylock, flags);
		memcpy_fromio(&node, buffer + SPRD_MAX_DATA_RXLEN, sizeof(node));
		if (node && node->priv) {
			skb = node->priv;
			skb_unlink(skb, &mm_entry->buffer_list);
			CLEAR_ADDR(skb->data, sizeof(skb));
			memset((skb->data - (SPRD_PHYS_LEN + SKB_ADDR_LEN)), 0x00, SPRD_PHYS_LEN);
		} else {
			wl_err("%s node or addr is null, phy 0x%lx,\
				sipc addr 0x%lx\n", __func__, phy_addr, pcie_addr);
			spin_unlock_irqrestore(&list->busylock, flags);
			return NULL;
		}
		spin_unlock_irqrestore(&list->busylock, flags);
	}

	return skb;
}

int mm_buffer_relink(struct mem_mgmt *mm_entry,
			    struct addr_trans_value *value, int total_len)
{
	int num = 0;
	unsigned long pcie_addr = 0;
	struct sk_buff *skb = NULL;
	int len = 0, ret = 0;

	for (num = 0; num < value->num; num++) {
		len += SPRD_PHYS_LEN;
		if (unlikely(len > total_len)) {
			wl_err("%s: total_len:%d < len:%d\n",
			       __func__, total_len, len);
			wl_err("%s: total %d pkts, relink %d pkts\n",
			       __func__, value->num, num);
			len = -EINVAL;
			break;
		}

		memcpy(&pcie_addr, value->address[num], SPRD_PHYS_LEN);
		pcie_addr &= SPRD_PHYS_MASK;

		ret = mm_w_addr_buf(mm_entry, pcie_addr);
		if (ret) {
			wl_err("%s: write addr buf fail: %d\n", __func__, ret);
			skb = mm_single_buffer_unlink(mm_entry, pcie_addr);
			if (likely(skb))
				dev_kfree_skb(skb);
			else
				wl_err("%s: unlink skb fail!\n", __func__);
		}

		skb = NULL;
		pcie_addr = 0;
	}

	return len;
}

static int mm_buffer_unlink(struct mem_mgmt *mm_entry,
			    struct addr_trans_value *value, int total_len)
{
	int num = 0;
	unsigned long pcie_addr = 0;
	struct sk_buff *skb = NULL;
	struct rx_msdu_desc *msdu_desc = NULL;
	int len = 0;
	unsigned short csum = 0;
	struct rx_mgmt *rx_mgmt =
	    container_of(mm_entry, struct rx_mgmt, mm_entry);
	struct sprd_hif *hif = rx_mgmt->hif;
	unsigned char sprd_max_add_mh_buf_once;

	if (hif->hw_type == SPRD_HW_SC2355_PCIE)
		sprd_max_add_mh_buf_once =
			SPRD_PCIE_MAX_ADD_MH_BUF_ONCE;
	else
		sprd_max_add_mh_buf_once =
			SPRD_SIPC_MAX_ADD_MH_BUF_ONCE;

	if (atomic_add_return(value->num, &mm_entry->alloc_num) >=
	    sprd_max_add_mh_buf_once) {
		sc2355_queue_rx_buff_work(rx_mgmt->hif->priv,
					  SPRD_PCIE_RX_ALLOC_BUF);
	}

	for (num = 0; num < value->num; num++) {
		len += SPRD_PHYS_LEN;
		if (unlikely(len > total_len)) {
			wl_err("%s: total_len:%d < len:%d\n",
			       __func__, total_len, len);
			wl_err("%s: total %d pkts, unlink %d pkts\n",
			       __func__, value->num, num);
			len = -EINVAL;
			break;
		}

		memcpy(&pcie_addr, value->address[num], SPRD_PHYS_LEN);
		pcie_addr &= SPRD_PHYS_MASK;
		wl_all("%s: pcie_addr=0x%lx", __func__, pcie_addr);

		skb = mm_single_buffer_unlink(mm_entry, pcie_addr);
		if (SPRD_HW_SC2355_SIPC == rx_mgmt->hif->hw_type) {
			if (!skb) {
				wl_err("%s: Rx address buffer is valid.\n", __func__);
				continue;
			}
			sipc_rx_mm_buf_to_skb(rx_mgmt->hif, skb);
		}
		if (likely(skb)) {
			print_hex_dump_debug("sc2355_rx_mh_desc rx : ", DUMP_PREFIX_OFFSET,
					     16, 1, skb->data, 500, 0);
			if (hif->hw_type == SPRD_HW_SC2355_PCIE) {
				csum = sc2355_pcie_get_data_csum((void *)rx_mgmt->hif,
								skb->data);
			} else if (hif->hw_type == SPRD_HW_SC2355_SIPC) {
				csum = sc2355_sipc_get_data_csum((void *)rx_mgmt->hif,
								skb->data);
			} else {
				csum = sc2355_get_data_csum((void *)rx_mgmt->hif,
								skb->data);
			}
			skb_reserve(skb, sizeof(struct rx_mh_desc));
			/* TODO: Would CP do this? */
			msdu_desc = (struct rx_msdu_desc *)skb->data;
			msdu_desc->msdu_offset -= sizeof(struct rx_mh_desc);
			/* TODO: Check whether prefetch work */
			prefetch(skb->data);

			if (likely(sc2355_fill_skb_csum(skb, csum) >= 0)) {
				if (hif->hw_type != SPRD_HW_SC2355_SIPC)
					sc2355_rx_process(rx_mgmt, skb);
				else
					sc2355_sipc_rx_process(rx_mgmt, skb);
			}  else	{/* checksum error, free skb */
				dev_kfree_skb(skb);
			}

		} else {
			wl_err("%s: unlink skb fail!\n", __func__);

		}

		skb = NULL;
		pcie_addr = 0;
	}

	return len;
}

static void
mm_compound_data_process(struct mem_mgmt *mm_entry, void *compound_data,
			 int total_len, int buffer_type)
{
	void *pos_data = NULL;
	int num = 0, msdu_len = 0, len = 0;
	struct sk_buff *skb = NULL;
	struct rx_mgmt *rx_mgmt =
	    container_of(mm_entry, struct rx_mgmt, mm_entry);
	struct sprd_hif *hif = rx_mgmt->hif;

	wl_all("%s: num: %d, total_len: %d\n", __func__, num, total_len);

	pos_data = compound_data + mm_entry->hif_offset;
	total_len -= mm_entry->hif_offset;
	num = ((struct rx_msdu_desc *)pos_data)->short_pkt_num;

	while (num--) {
		msdu_len = msdu_total_len((struct rx_msdu_desc *)pos_data);
		len += ALIGN_8BYTE(msdu_len);
		if (unlikely(len > total_len)) {
			wl_err("%s: total_len:%d < len:%d, leave %d pkts\n",
			       __func__, total_len, len, (num + 1));
			break;
		}

		wl_all("%s: msdu_len: %d, len: %d\n",
			 __func__, msdu_len, len);

		skb = mm_data2skb_process(mm_entry, pos_data, msdu_len);
		if (unlikely(!skb)) {
			wl_err("%s: alloc skb fail, leave %d pkts\n",
			       __func__, (num + 1));
			break;
		}

		if (hif->hw_type != SPRD_HW_SC2355_SIPC)
			sc2355_rx_process(rx_mgmt, skb);
		else
			sc2355_sipc_rx_process(rx_mgmt, skb);

		pos_data = (unsigned char *)pos_data +
		    ALIGN_8BYTE(msdu_len + sizeof(struct rx_mh_desc));
		skb = NULL;
	}

	sc2355_free_data(compound_data, buffer_type);
}

static void mm_normal_data_process(struct mem_mgmt *mm_entry,
				   void *data, int len, int buffer_type)
{
	int skb_len = 0;
	unsigned short csum = 0;
	bool free_data = false;
	struct sk_buff *skb = NULL;
	struct rx_msdu_desc *msdu_desc =
	    (struct rx_msdu_desc *)(data + mm_entry->hif_offset);
	struct rx_mgmt *rx_mgmt =
	    container_of(mm_entry, struct rx_mgmt, mm_entry);
	struct sprd_hif *hif = rx_mgmt->hif;

	if (unlikely(len < sizeof(struct rx_msdu_desc))) {
		wl_err("%s: data len is %d, too short\n", __func__, len);
		free_data = true;
	} else if (unlikely((mm_entry->hif_offset + msdu_total_len(msdu_desc)) > len)) {
		wl_err("%s: data len is %d too short, msdu_offset %d, msdu_len %d\n",
			__func__, len, msdu_desc->msdu_offset, msdu_desc->msdu_len);
		print_hex_dump(KERN_ERR, "DATA: ", DUMP_PREFIX_OFFSET,
			     16, 1, data, len, 0);
		free_data = true;
	} else {
		if (hif->hw_type == SPRD_HW_SC2355_PCIE)
			csum = sc2355_pcie_get_data_csum((void *)rx_mgmt->hif, data);
		else if (hif->hw_type == SPRD_HW_SC2355_SIPC)
			csum = sc2355_sipc_get_data_csum((void *)rx_mgmt->hif, data);
		else
			csum = sc2355_get_data_csum((void *)rx_mgmt->hif, data);
		skb_len = SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) +
		    SKB_DATA_ALIGN(msdu_total_len(msdu_desc) +
				   mm_entry->hif_offset);
		if (likely(skb_len <= len))
			skb = mm_build_skb(data, skb_len, buffer_type);
		else {
			/* Should not happen */
			wl_err("%s: data len is %d, skb need %d\n",
			       __func__, len, skb_len);
			skb = mm_data2skb_process(mm_entry, data,
						  len);
			free_data = true;
		}

		if (unlikely(!skb)) {
			wl_err("%s: alloc skb fail\n", __func__);
			free_data = true;
		} else {
			skb_reserve(skb, mm_entry->hif_offset);

			if (likely(sc2355_fill_skb_csum(skb, csum) >= 0)) {

				if (hif->hw_type != SPRD_HW_SC2355_SIPC)
					sc2355_rx_process(rx_mgmt, skb);
				else
					sc2355_sipc_rx_process(rx_mgmt, skb);
			 } else	 {/* checksum error, free skb */
				dev_kfree_skb(skb);
			}
		}
	}

	if (free_data)
		sc2355_free_data(data, buffer_type);
}

/* NOTE: This function JUST work when mm_w_addr_buf() work abnormal */
static inline void mm_refill_buffer(struct mem_mgmt *mm_entry)
{
	int num = SPRD_MAX_MH_BUF - skb_queue_len(&mm_entry->buffer_list);

	wl_all("%s: need to refill %d buffer\n", __func__, num);

	if (num > 0) {
		sc2355_mm_buffer_alloc(mm_entry, num);
	} else if (num < 0) {
		/* Should never happen */
		wl_err("%s: %d > mx addr buf!\n", __func__, num);
	}
}

static int mm_single_event_process(struct mem_mgmt *mm_entry,
				   struct addr_trans_value *value, int len)
{
	int ret = 0;

	switch (value->type) {
	case SPRD_PROCESS_BUFFER:
		ret = mm_buffer_unlink(mm_entry, value, len);
		break;
	case SPRD_FREE_BUFFER:
		wl_err("%s: null for free buff\n", __func__);
		/* NOTE: todo relink*/
		break;
	case SPRD_REQUEST_BUFFER:
		/* NOTE: Not need to do anything here */
		break;
	case SPRD_FLUSH_BUFFER:
		sc2355_rx_flush_buffer(mm_entry);
		break;
	default:
		wl_err("%s: err type: %d\n", __func__, value->type);
		ret = -EINVAL;
	}

	return (ret < 0) ? ret : (ret + sizeof(*value));
}

unsigned long sc2355_mm_virt_to_phys(struct device *dev, void *buffer,
				     size_t size,
				     enum dma_data_direction direction)
{
	dma_addr_t pa = 0;
	unsigned long pcie_addr = 0;

again:
	if (direction == DMA_FROM_DEVICE)
		mm_clear_mh_buffer(buffer);

	mb();
	pa = dma_map_single(dev, buffer, size, direction);
	if (likely(!dma_mapping_error(dev, pa))) {
		pcie_addr = pa | SPRD_MH_ADDRESS_BIT;
	} else {
		wl_err("%s: dma_mapping_error\n", __func__);
		goto again;
	}

	return pcie_addr;
}

void *sc2355_mm_phys_to_virt(struct device *dev, unsigned long pcie_addr,
			     size_t size, enum dma_data_direction direction,
			     bool is_mh)
{
	dma_addr_t pa = 0;
	void *buffer = NULL;

	pa = pcie_addr & (~(SPRD_MH_ADDRESS_BIT) & SPRD_PHYS_MASK);
	buffer = phys_to_virt(pa);

	if (direction == DMA_FROM_DEVICE)
		dma_sync_single_for_cpu(dev, pa, size, direction);
	else
		dma_sync_single_for_device(dev, pa, size, direction);

	if (is_mh) {
		if (!mm_check_mh_buffer(dev, buffer, pa, size, direction))
			buffer = NULL;
	}

	dma_unmap_single(dev, pa, size, direction);

	return buffer;
}

void sc2355_mm_flush_buffer(struct mem_mgmt *mm_entry)
{
	/* TODO: Should we stop something here? */

	/* Free all skbs */
	skb_queue_purge(&mm_entry->buffer_list);
	mm_free_addr_buf(mm_entry);
	atomic_set(&mm_entry->alloc_num, 0);
	wl_err("%s, %d, alloc_num set to 0\n", __func__, __LINE__);
}

int sc2355_mm_buffer_alloc(struct mem_mgmt *mm_entry, int need_num)
{
	int num = 0, ret = 0;

	for (num = 0; num < need_num; num++) {
		ret = mm_single_buffer_alloc(mm_entry);
		if (ret) {
			wl_all("%s: alloc num: %d, need num: %d, ret: %d\n",
			       __func__, num, need_num, ret);
			break;
		}
	}
	num = need_num - num;

	return num;
}

/* TODO: Could we use netdev_alloc_frag instead of kmalloc?
 *       So we did not need to distinguish buffer type
 *       Maybe it could speed up alloc process, too
 */
void sc2355_free_data(void *data, int buffer_type)
{
	if (buffer_type) {	/* Fragment page buffer */
		put_page(virt_to_head_page(data));
	} else {		/* Normal buffer */
		if (data)
			kfree(data);
	}
}

/* PCIE DATA EVENT */
void sc2355_mm_mh_data_event_process(struct mem_mgmt *mm_entry, void *data,
				     int len, int buffer_type)
{
	int offset = 0;
	struct sprd_addr_hdr *hdr =
	    (struct sprd_addr_hdr *)(data + mm_entry->hif_offset);
	struct addr_trans *addr_trans = (struct addr_trans *)hdr->paydata;
	struct addr_trans_value *value = addr_trans->value;
	unsigned char tlv_num = addr_trans->tlv_num;
	int remain_len = len - mm_entry->hif_offset -
	    sizeof(*hdr) - sizeof(*addr_trans) - sizeof(*value);

	while (tlv_num--) {
		remain_len = remain_len - offset;
		if (remain_len < 0) {
			wl_err("%s: remain tlv num: %d\n", __func__, tlv_num);
			break;
		}

		value = GET_NEXT_ADDR_TRANS_VALUE(value, offset);
		offset = mm_single_event_process(mm_entry, value, remain_len);
		if (offset < 0) {
			wl_err("%s: do mh event fail: %d!\n", __func__, offset);
			break;
		}
	}

}

/* NORMAL DATA */
void sc2355_mm_mh_data_process(struct mem_mgmt *mm_entry, void *data, int len,
			       int buffer_type)
{
	if (mm_is_compound_data(mm_entry, data))
		mm_compound_data_process(mm_entry, data, len, buffer_type);
	else
		mm_normal_data_process(mm_entry, data, len, buffer_type);
}

int sc2355_mm_init(struct mem_mgmt *mm_entry, void *hif)
{
	int ret = 0;

	mm_entry->hif_offset = ((struct sprd_hif *)hif)->hif_offset;

	if (((struct sprd_hif *)hif)->hw_type == SPRD_HW_SC2355_PCIE ||
		((struct sprd_hif *)hif)->hw_type == SPRD_HW_SC2355_SIPC) {
		skb_queue_head_init(&mm_entry->buffer_list);
		atomic_set(&mm_entry->alloc_num, 0);

	}

	return ret;
}

int sc2355_mm_deinit(struct mem_mgmt *mm_entry, void *hif)
{
	if (((struct sprd_hif *)hif)->hw_type == SPRD_HW_SC2355_PCIE ||
		((struct sprd_hif *)hif)->hw_type == SPRD_HW_SC2355_SIPC) {
		/* NOTE: pclint says kfree(NULL) is safe */
		kfree(mm_entry->hdr);
		mm_entry->hdr = NULL;
		mm_entry->addr_trans = NULL;
		sc2355_mm_flush_buffer(mm_entry);
	}

	mm_entry->hif_offset = 0;
	return 0;
}
