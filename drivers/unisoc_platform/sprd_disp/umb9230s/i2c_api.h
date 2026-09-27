/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef __API_I2C_H__
#define __API_I2C_H__

#include <linux/i2c.h>
#include <linux/types.h>


void sprd_i2c_enable(void);
void sprd_i2c_disable(void);
int sprd_i2c_master_xfer( struct i2c_msg *m, int n);

int iic2cmd_write(u8 dec_addr, u32* buf, u32 len);
int iic2cmd_read(u8 dec_addr, u16 reg_addr , u32* buf, u32 len);
#endif /* __API_I2C_H__ */
