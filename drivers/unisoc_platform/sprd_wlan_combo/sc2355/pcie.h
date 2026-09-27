/*
* SPDX-FileCopyrightText: 2020-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef __PCIE_H__
#define __PCIE_H__

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include "wcn_bus.h"
#include "common/hif.h"
#include "sc2355_intf.h"

#include "pcie_buf.h"

#define PCIE_TX_NUM 96

#define PCIE_RX_CMD_PORT	7
#define PCIE_RX_DATA_PORT	9
#define PCIE_RX_ADDR_DATA_PORT	11
#define PCIE_TX_CMD_PORT	4
#define PCIE_TX_DATA_PORT	5
#define PCIE_TX_ADDR_DATA_PORT  10

#define USB_RX_CMD_PORT	20
#define USB_RX_PKT_LOG_PORT	21
#define USB_RX_DATA_PORT	22
#define USB_TX_CMD_PORT	4
#define USB_TX_DATA_PORT	6

#define MAX_FW_TX_DSCR (1024)


struct pcie_addr_buffer {
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
	unsigned char pcie_addr[][5];
} __packed;

struct pcie_rx_mbuf {
	struct list_head list;
	void *buf;
};

static inline void pcie_free_msg_content(struct sprd_msg *msg)
{
	if (msg->node)
		pcie_free_tx_buf(msg->node);

}

void sc2355_tx_free_pcie_data_num(struct sprd_hif *hif, unsigned char *data);
int sc2355_tx_free_pcie_data(unsigned char *data);
int sc2355_tx_addr_trans_pcie(struct sprd_hif *hif,
			      unsigned char *data, int len, bool send_now);
void sc2355_pcie_dump_addr(struct sprd_hif *hif);

#endif /* __PCIE_H__ */
