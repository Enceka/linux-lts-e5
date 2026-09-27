/*
* PDX-FileCopyrightText: 2021-2022 Unisoc (Shanghai) Technologies Co., Ltd
* SPDX-License-Identifier: GPL-2.0
*
* Copyright 2021-2022 Unisoc (Shanghai) Technologies Co., Ltd
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of version 2 of the GNU General Public License
* as published by the Free Software Foundation.
*/

#ifndef __SIPC_BUF_H__
#define __SIPC_BUF_H__

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include "common/hif.h"
#include "common/msg.h"


enum {
	SIPC_LOC_BUFF_FREE,
	SIPC_LOC_TX_INTF,
	SIPC_LOC_RX_INTF,
};

enum {
	SIPC_MEMORY_FREE,
	SIPC_MEMORY_ALLOC = 0x5a,
};

struct sipc_buf_node {
	struct list_head list;
	u8 flag;
	u8 ctxt_id;
	u8 location;
	u8 resv;
	void *buf;
	void *priv;
} __packed;

struct sipc_mem_region {
	phys_addr_t phy_base;
	void  *virt_base;
	size_t size;
	unsigned int page_count;
};

#define SPRDWL_SIPC_MEM_TXRX_TOTAL				0x280000
#define SPRDWL_SIPC_MEM_RX_OFFSET				0x1B5800
#define SIPC_TXRX_BUF_BLOCK_TYPE               (1)
#define SIPC_TXRX_BUF_SINGLE_TYPE              (0)
#define SIPC_TXRX_TX_BUF_MAX_NUM               (300)
#define SIPC_TXRX_RX_BUF_MAX_NUM               (450)
struct sipc_buf_mm {
	struct sprd_msg_list nlist;
	void *virt_start;
	void *virt_end;
	unsigned long offset;
	u32 len;
	u32 type;
	u32 buf_count;
	u32 padding;
};

struct sipc_txrx_mm {
	struct sipc_buf_mm *tx_buf;
	struct sipc_buf_mm *rx_buf;
	struct sipc_mem_region smem;
};

void sipc_free_tx_buf(struct sprd_hif *hif, struct sprd_msg *msg_buf);
int sipc_txrx_buf_init(struct platform_device *pdev, struct sprd_hif *hif);
void sipc_mm_rx_buf_deinit(struct sprd_hif *hif);
void sipc_mm_rx_buf_flush(struct sprd_hif *hif);
int sipc_get_tx_buf_num(struct sprd_hif *hif);
void sipc_txrx_buf_deinit(struct sprd_hif *hif);
void sprdwl_sipc_txrx_buf_deinit(struct sprd_hif *hif);
struct sipc_buf_node *sipc_rx_alloc_node_buf(struct sprd_hif *hif);
void sipc_free_node_buf(struct sipc_buf_node *node,
			 struct sprd_msg_list *list);
int sipc_skb_to_tx_buf(struct sprd_hif *dev,
				struct sprd_msg *msg_pos);
int sipc_rx_mm_buf_to_skb(struct sprd_hif *hif,
				struct sk_buff *skb);
void sipc_queue_node_buf(struct sipc_buf_node *node,
			  struct sprd_msg_list *list);
void *sipc_fill_mbuf(void *data, unsigned int len);

#endif /*__SIPC_BUF_H__*/
