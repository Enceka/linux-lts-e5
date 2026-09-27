/*PDX-FileCopyrightText: 2020-2022 Unisoc (Shanghai) Technologies Co., Ltd
* SPDX-License-Identifier: GPL-2.0
*
* Copyright 2020-2022 Unisoc (Shanghai) Technologies Co., Ltd
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of version 2 of the GNU General Public License
* as published by the Free Software Foundation.
*/

#ifndef __sipc_H__
#define __sipc_H__

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include "wcn_bus.h"
#include "common/hif.h"
#include "sc2355_intf.h"

#include "sipc_buf.h"

#define SIPC_TX_NUM 48

#define SIPC_WIFI_CMD_TX 16
#define SIPC_WIFI_CMD_RX 17
#define SIPC_WIFI_DATA0_TX 18
#define SIPC_WIFI_DATA0_RX 19
#define SIPC_WIFI_DATA1_TX 20
#define SIPC_WIFI_DATA1_RX 21

#define MAX_FW_TX_DSCR (1024)


struct sipc_addr_buffer {
	struct {
		unsigned char type:3;
		/*direction of address buffer of cmd/event,*/
		/*0:Tx, 1:Rx*/
		unsigned char direction_ind:1;
		unsigned char buffer_type:1;
		unsigned char interface:3;
	} common;
	unsigned char offset;
	struct {
		unsigned char rsvd:7;
		unsigned char buffer_inuse:1;
	} buffer_ctrl;
	unsigned short number;
	unsigned short rsvd;
	unsigned char sipc_addr[][5];
} __packed;

static inline void sipc_free_msg_content(struct sprd_msg *msg)
{
	struct sprd_hif *hif = sc2355_get_hif();
	if (msg->skb) {
		dev_kfree_skb(msg->skb);
		msg->skb = NULL;
	}
	if (msg->sipc_node) {
		sipc_free_tx_buf(hif, msg);
		msg->sipc_node = NULL;
	}

}
void sc2355_tx_free_sipc_data_num(struct sprd_hif *hif, unsigned char *data);
int sc2355_tx_free_sipc_data(unsigned char *data);
int sc2355_tx_addr_trans_sipc(struct sprd_hif *hif,
				unsigned char *data, int len, bool send_now);

#endif /* __SIPC_H__ */
