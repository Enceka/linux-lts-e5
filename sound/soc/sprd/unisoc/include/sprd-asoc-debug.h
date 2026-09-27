/*
 * audio/sprd/sprd-asoc-debug.h
 *
 * SPRD ASoC Debug include -- SpreadTrum ASOC Debug.
 *
 * SPDX-FileCopyrightText: 2015 Spreadtrum Communications (Shanghai) Co., Ltd
 * SPDX-FileCopyrightText: 2016 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */
#ifndef __SPRD_ASOC_DEBUG_H
#define __SPRD_ASOC_DEBUG_H

#define pr_color_terminal 0
#if pr_color_terminal
#define pr_col_s "\e[35m"
#define pr_col_e "\e[0m"
#else
#define pr_col_s
#define pr_col_e
#endif

#define pr_id "ASoC:"
#define pr_s pr_col_s"["pr_id
#define pr_e "] "pr_col_e

#define pr_sprd_fmt(id) pr_s""id""pr_e

#endif /* __SPRD_ASOC_DEBUG_H */
