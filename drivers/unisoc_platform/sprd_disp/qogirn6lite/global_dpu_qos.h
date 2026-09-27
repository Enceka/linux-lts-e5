/*
*SPDX-FileCopyrightText: 2020 Unisoc (Shanghai) Technologies Co.Ltd
*SPDX-License-Identifier: GPL-2.0-only
*/
#ifndef __GLOBAL_DPU_QOS_H__
#define __GLOBAL_DPU_QOS_H__

#define writel_bits_relaxed(mask, val, addr) \
	writel_relaxed((readl_relaxed(addr) & ~(mask)) | (val), addr)

typedef struct _QOS_REG_STRUCT {
	unsigned int      offset;
	unsigned int      mask;
	unsigned int      value;

} QOS_REG_T;

static QOS_REG_T dpu_mtx_qos[] = {
	{ 0x0014, 0xffffffff, 0x00000000},
	{ 0x0018, 0xffffffff, 0x00000000},
	{ 0x0060, 0x80000003, 0x00000003},
	{ 0x0064, 0x3fff3fff, 0x06660dda},
	{ 0x0068, 0x00000701, 0x00000001},
	{ 0x0094, 0xffffffff, 0x00000000},
	{ 0x0098, 0xffffffff, 0x00000000},
	{ 0x00E0, 0x80000003, 0x00000003},
	{ 0x00E4, 0x3fff3fff, 0x06660dda},
	{ 0x00E8, 0x00000701, 0x00000001},
};
#endif
