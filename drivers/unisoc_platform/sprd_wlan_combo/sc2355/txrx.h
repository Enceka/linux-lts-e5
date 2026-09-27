/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef __TXRX_H__
#define __TXRX_H__

#include "rx.h"
#include "tx.h"

void sc2355_tcp_ack_filter_rx(struct sprd_priv *priv, unsigned char *buf,
			      unsigned int plen);
/* return val: 0 for not fileter, 1 for fileter */
int sc2355_tcp_ack_filter_send(struct sprd_priv *priv, struct sprd_msg *msg,
			       unsigned char *buf, unsigned int plen);
void sc2355_tcp_ack_move_msg(struct sprd_priv *priv, struct sprd_msg *msg);

#endif
