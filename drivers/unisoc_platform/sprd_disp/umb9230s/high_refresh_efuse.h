// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef __HIGH_REFRESH_EFUSE_H__
#define __HIGH_REFRESH_EFUSE_H__

u32 high_refresh_efuse_prog(int start_index, int end_index, bool isdouble, u32 val);
u32 high_refresh_efuse_read(int start_index, int end_index, bool isdouble, u32 *val);
int high_refresh_efuse_power_on_read(int reg_offset, u32 *val);
void high_refresh_efuse_init(void);

#endif//__HIGH_REFRESH_EFUSE_H__
