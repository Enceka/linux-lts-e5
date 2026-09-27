/*
 * SPDX-FileCopyrightText: 2015 Spreadtrum Communications (Shanghai) Co., Ltd
 * SPDX-FileCopyrightText: 2016 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef __AUDIO_SBLOCK_H
#define __AUDIO_SBLOCK_H

#include <linux/poll.h>
#include <linux/types.h>

struct sblock {
	void		*addr;
	u32	length;
};

int audio_sblock_poll_wait(uint8_t dst, uint8_t channel,
		struct file *filp, poll_table *wait);

int audio_sblock_create(uint8_t dst, uint8_t channel,
		u32 txblocknum, u32 txblocksize,
		u32 rxblocknum, u32 rxblocksize, int mem_type);

void audio_sblock_destroy(uint8_t dst, uint8_t channel);
int audio_sblock_get(uint8_t dst, uint8_t channel, struct sblock *blk,
		     int timeout);
int audio_sblock_send(uint8_t dst, uint8_t channel, struct sblock *blk);
int audio_sblock_receive(uint8_t dst, uint8_t channel, struct sblock *blk,
			 int timeout);
int audio_sblock_release(uint8_t dst, uint8_t channel, struct sblock *blk);
int audio_sblock_init(uint8_t dst, uint8_t channel,
		u32 txblocknum, u32 txblocksize,
		u32 rxblocknum, u32 rxblocksize, int mem_type);
void audio_sblock_info_print(void);
#endif
