/*
 * audio/sprd/dai/sprd-dmaengine-pcm.h
 *
 * SpreadTrum DMA for the pcm stream.
 *
 * SPDX-FileCopyrightText: 2015 Spreadtrum Communications (Shanghai) Co., Ltd
 * SPDX-FileCopyrightText: 2016 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */
#ifndef __SPRD_DMA_ENGINE_PCM_H
#define __SPRD_DMA_ENGINE_PCM_H

#include <linux/dmaengine.h>
#include <linux/dma/sprd-dma.h>
#include "sprd_audio_dma.h"

#define VBC_FIFO_FRAME_NUM		(80)

#define VBC_BUFFER_BYTES_MAX		(64 * 1024)

#define I2S_BUFFER_BYTES_MAX	(64 * 1024)

#define AUDIO_BUFFER_BYTES_MAX	(VBC_BUFFER_BYTES_MAX + I2S_BUFFER_BYTES_MAX)

#define SPRD_DMAENGINE_PCM_DRV_NAME "sprd-pcm-dma"

struct sprd_pcm_dma_params {
	char *name;		/* stream identifier */
	int channels[2];	/* channel id */
	int irq_type;		/* dma interrupt type */
	struct sprd_dma_cfg desc;	/* dma description struct */
	u32 dev_paddr[2];	/* device physical address for DMA */
	u32 use_mcdt; /* @@1 use mcdt, @@0 not use mcdt */
	char *used_dma_channel_name[2];
};

enum{
	DMA_TYPE_INVAL = -1,
	DMA_TYPE_AGCP,
	DMA_TYPE_MAX
};

struct audio_pm_dma {
	int no_pm_cnt;
	struct sprd_runtime_data *normal_rtd;
	struct notifier_block pm_nb;
	/* protect rtd->dma_chn */
	spinlock_t pm_splk_dma_prot;
	/* irq flags of the pm_splk_dma_prot holder; only valid under the lock */
	unsigned long pm_splk_flags;
	/* protect rtd->dma_chn */
	struct mutex pm_mtx_dma_prot;
	/* protect no_pm_cnt */
	struct mutex pm_mtx_cnt;
};
#endif /* __SPRD_DMA_ENGINE_PCM_H */
