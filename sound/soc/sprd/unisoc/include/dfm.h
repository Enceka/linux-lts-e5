/*
 * audio/sprd/dfm.h
 *
 * SPRD SoC VBC -- SpreadTrum SOC for VBC DAI function.
 *
 * SPDX-FileCopyrightText: 2018 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */
#ifndef __SPRD_DFM_H
#define __SPRD_DFM_H

#define DFM_MAGIC_ID  0x7BC

struct sprd_dfm_priv {
	int hw_rate;
	int sample_rate;
};

struct sprd_dfm_priv dfm_priv_get(void);

#endif /* __SPRD_DFM_H */
