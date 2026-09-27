/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef __SDIO_H__
#define __SDIO_H__

#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include "wcn_bus.h"

#include "common/hif.h"

#include "sc2355_intf.h"

#define SDIO_RX_CMD_PORT	22
#define SDIO_RX_PKT_LOG_PORT	23
/*use port 24 because fifo_len = 8*/
#define SDIO_RX_DATA_PORT	24
#define SDIO_TX_CMD_PORT	8
/*use port 10 because fifo_len = 8*/
#define SDIO_TX_DATA_PORT	10

struct sc2355_sdiohal_puh {
	unsigned int pad:6;
	unsigned int check_sum:1;
	unsigned int len:16;
	unsigned int eof:1;
	unsigned int subtype:4;
	unsigned int type:4;
};/* 32bits public header */
#endif /* __SDIO_H__ */
