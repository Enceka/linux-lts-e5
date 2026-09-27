/*
 * audio/sprd/sprd-asoc-card-utils.h
 *
 * SPDX-FileCopyrightText: 2015 Spreadtrum Communications (Shanghai) Co., Ltd
 * SPDX-FileCopyrightText: 2016 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef __SPRD_ASOC_CARD_UTILS_H
#define __SPRD_ASOC_CARD_UTILS_H

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/types.h>
#include <sound/simple_card.h>
#include <sound/soc.h>
#include <uapi/sound/asound.h>

#include "sprd-asoc-card-utils-legacy.h"

enum VBC_VERSION_TYPE {
	BOARD_T_VBC_R2P0 = 0,
	BOARD_T_VBC_R3P0,
	BOARD_T_VBC_R1P0V3,
	BOARD_T_VBC_V4,
};
enum CODEC_VERSION_TYPE {
	BOARD_T_CODEC_2723 = 0,
	BOARD_T_CODEC_2731,
	BOARD_T_CODEC_2721,
	BOARD_T_CODEC_2730,
};

struct sprd_asoc_data {
	enum VBC_VERSION_TYPE vbc_type;
	enum CODEC_VERSION_TYPE codec_type;
};

struct sprd_card_data {
	struct snd_soc_card snd_card;
	struct sprd_dai_props {
		struct simple_util_dai cpu_dai;
		struct simple_util_dai codec_dai;
		unsigned int mclk_fs;
	} *dai_props;
	unsigned int mclk_fs;
	int gpio_hp_det;
	int gpio_hp_det_invert;
	int gpio_mic_det;
	int gpio_mic_det_invert;
	int codec_type;		/* 0: internal; 1: external */
	const struct sprd_asoc_data *board_type;
	struct sprd_asoc_ext_hook ext_hook;
	struct device_node *platform_of_node;
	unsigned int is_fm_open_src;	/* 0: not open src, 1 open src */
	unsigned int fm_hw_rate;
	/* for codec 2731 2721, not suppoert 44100 adc rate,
	 * so we should replace it.
	 */
	unsigned int codec_replace_adc_rate;
	struct smartamp_boost_data boost_data;
	struct snd_soc_dai_link dai_link[];	/* dynamically allocated */
};

struct asoc_sprd_ptr_num {
	union {
		struct snd_soc_ops *ops;
		struct snd_soc_compr_ops *compr_ops;

		int (**init) (struct snd_soc_pcm_runtime *);
		int (**be_hw_params_fixup) (struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params);
		void *ptr;
	} p;
	size_t num;
};

#define ASOC_SPRD_PRT_NUM(array) { \
		.p.ptr = array, \
		.num = ARRAY_SIZE(array), \
	}

void asoc_sprd_card_set_ops(struct asoc_sprd_ptr_num *pn);
void asoc_sprd_card_set_compr_ops(struct asoc_sprd_ptr_num *pn);
void asoc_sprd_card_set_init(struct asoc_sprd_ptr_num *pn);
void asoc_sprd_card_set_bhpf(struct asoc_sprd_ptr_num *pn);
int asoc_sprd_register_card(struct device *dev, struct snd_soc_card *card);
int asoc_sprd_card_probe(struct platform_device *pdev,
			 struct snd_soc_card **card);
void asoc_sprd_card_remove(struct platform_device *pdev);

#endif /* __SPRD_ASOC_CARD_UTILS_H */
