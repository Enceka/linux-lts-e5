// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include "i2c_api.h"

#define SCI_ADDR(_b_, _o_) ( (_b_) + (_o_) )

#define UMB9230S_EFUSE_BASE		0x2000
#define UMB9230S_SLAVE_ADDR		0x12

#define EFUSE_ALL0_INDEX		SCI_ADDR(UMB9230S_EFUSE_BASE, 0x0008)
#define EFUSE_MODE_CTRL			SCI_ADDR(UMB9230S_EFUSE_BASE, 0x000c)
#define EFUSE_SEC_EN			SCI_ADDR(UMB9230S_EFUSE_BASE, 0x0040)
#define EFUSE_SEC_ERR_FLAG		SCI_ADDR(UMB9230S_EFUSE_BASE, 0x0044)
#define EFUSE_SEC_FLAG_CLR		SCI_ADDR(UMB9230S_EFUSE_BASE, 0x0048)
#define EFUSE_SEC_MAGIC_NUM		SCI_ADDR(UMB9230S_EFUSE_BASE, 0x004c)
#define EFUSE_PW_SWT			SCI_ADDR(UMB9230S_EFUSE_BASE, 0x0054)
#define EFUSE_PW_ON_RD_END_FLAG		SCI_ADDR(UMB9230S_EFUSE_BASE, 0x006c)
#define EFUSE_POR_READ_DATA(n)		SCI_ADDR(UMB9230S_EFUSE_BASE, n)

#define EFUSE_MEM(index)		SCI_ADDR(UMB9230S_EFUSE_BASE,(0x1000 + (index << 2)))

/* bits definitions for register EFUSE_SEC_EN */
#define BIT_VDD_EN			(BIT(0))
#define BIT_AUTO_CHECK_ENABLE		(BIT(1))
#define BIT_DOUBLE_BIT_EN		(BIT(2))
#define BIT_MARGIN_RD_ENABLE		(BIT(3))
#define BIT_LOCK_BIT_WR_EN		(BIT(4))

/* bits definitions for register EFUSE_SEC_ERR_FLAG */
#define BIT_WORD0_ERR_FLAG		(BIT(0))
#define BIT_WORD1_ERR_FLAG		(BIT(1))
#define BIT_WORD0_PROT_FLAG		(BIT(4))
#define BIT_WORD1_PROT_FLAG		(BIT(5))
#define BIT_PG_EN_WR_FLAG		(BIT(8))
#define BIT_VDD_ON_RD_FLAG		(BIT(9))
#define BIT_BLOCK0_RD_FLAG		(BIT(10))
#define BIT_MAGNUM_WR_FLAG		(BIT(11))
#define BIT_ENK_ERR_FLAG		(BIT(12))
#define BIT_ALL0_CHECK_FLAG		(BIT(13))
#define ERR_FLAG_MASK			0x3fff

/* bits definitions for register EFUSE_PW_SWT */
#define BIT_EFS_ENK1_ON			(BIT(0))
#define BIT_EFS_ENK2_ON			(BIT(1))
#define BIT_NS_S_PG_EN			(BIT(2))

#define EFUSE_MAGIC_NUMBER		0x8810
#define ERR_CLR_MASK			GENMASK(13, 0)

static struct mutex efuse_lock;

static int efuse_i2c_read(u16 reg, u32 *val)
{
	int ret = 0;

	ret = iic2cmd_read(UMB9230S_SLAVE_ADDR, reg, val, 1);
	if (ret < 0)
		pr_err("%s: efuse read reg:0x%x failed\n", __func__, reg);

	return ret;
}

static int efuse_i2c_write(u16 reg, u32 val)
{
	int ret = 0;
	u32 buf[2] = {0};

	buf[0] = reg;
	buf[1] = val;

	ret = iic2cmd_write(UMB9230S_SLAVE_ADDR, buf, 2);
	if (ret < 0)
		pr_err("%s: efuse write reg:0x%x failed\n", __func__, reg);

	return ret;
}

static void efuse_reg_conf(u16 reg, u32 bit, bool enable)
{
	u32 cfg0 = 0;
	int ret = 0;

	ret = efuse_i2c_read(reg, &cfg0);
	if (ret < 0) {
		pr_err("%s: efuse read reg:0x%x failed\n", __func__, reg);
	} else {
		if (enable)
			cfg0 |= bit;
		else
			cfg0 &= ~bit;

		efuse_i2c_write(reg, cfg0);
	}
}

static u32 efuse_check_status(void)
{
	int ret = 0;
	u32 val = ERR_FLAG_MASK;

	ret = efuse_i2c_read(EFUSE_SEC_ERR_FLAG, &val);
	if (ret < 0)
		pr_err("%s: efuse read status failed\n", __func__);

	efuse_i2c_write(EFUSE_SEC_FLAG_CLR, ERR_CLR_MASK);

	return val;
}

static void efuse_prog_power_on(void)
{
	efuse_reg_conf(EFUSE_PW_SWT, BIT_NS_S_PG_EN, TRUE);

	efuse_reg_conf(EFUSE_PW_SWT, BIT_EFS_ENK2_ON, FALSE);
	udelay(1000);

	efuse_reg_conf(EFUSE_PW_SWT, BIT_EFS_ENK1_ON, TRUE);
	udelay(1000);
}

static void efuse_prog_power_off(void)
{
	efuse_reg_conf(EFUSE_PW_SWT, BIT_EFS_ENK1_ON, FALSE);
	udelay(1000);

	efuse_reg_conf(EFUSE_PW_SWT, BIT_EFS_ENK2_ON, TRUE);
	udelay(1000);

	efuse_reg_conf(EFUSE_PW_SWT, BIT_NS_S_PG_EN, FALSE);
}

static void efuse_double(bool enable)
{
	efuse_reg_conf(EFUSE_SEC_EN, BIT_DOUBLE_BIT_EN, enable);
}

static void efuse_read_power_on(void)
{
	efuse_reg_conf(EFUSE_SEC_EN, BIT_VDD_EN, TRUE);
}

static void efuse_read_power_off(void)
{
	efuse_reg_conf(EFUSE_SEC_EN, BIT_VDD_EN, FALSE);
}

u32 high_refresh_efuse_prog(int start_index, int end_index, bool isdouble, u32 val)
{
	int blk_index;
	u32 err_flag = ERR_FLAG_MASK;

	mutex_lock(&efuse_lock);
	efuse_i2c_write(EFUSE_SEC_FLAG_CLR, ERR_CLR_MASK);
	efuse_i2c_write(EFUSE_SEC_MAGIC_NUM, EFUSE_MAGIC_NUMBER);
	efuse_prog_power_on();
	efuse_double(isdouble);

	for (blk_index = start_index; blk_index <= end_index; blk_index++) {
		efuse_i2c_write(EFUSE_MEM(blk_index), val);
		err_flag = efuse_check_status();
		if (err_flag != 0)
			pr_err("%s: efuse write failed, status:0x%08x\n", __func__, err_flag);

		pr_info("%s: efuse write blk%d, val:0x%08x\n", __func__, blk_index, val);
	}

	efuse_double(FALSE);
	efuse_prog_power_off();
	efuse_i2c_write(EFUSE_SEC_MAGIC_NUM, 0);
	mutex_unlock(&efuse_lock);

	return err_flag;
}

u32 high_refresh_efuse_read(int start_index, int end_index, bool isdouble, u32 *val)
{
	int blk_index, ret = 0;
	uint32_t err_flag = ERR_FLAG_MASK;

	mutex_lock(&efuse_lock);
	efuse_i2c_write(EFUSE_SEC_FLAG_CLR, ERR_CLR_MASK);
	efuse_read_power_on();
	efuse_double(isdouble);

	for (blk_index = start_index; blk_index <= end_index; blk_index++) {
		ret = efuse_i2c_read(EFUSE_MEM(blk_index), val);
		if (ret < 0) {
			pr_err("%s: efuse read block error\n", __func__);
		} else {
			err_flag = efuse_check_status();
			if (err_flag != 0)
				pr_err("%s: efuse read failed, status:0x%08x\n", __func__,
					err_flag);
			pr_info("%s: efuse read blk%d, val:0x%08x\n", __func__, blk_index, *val);
		}
	}

	efuse_double(FALSE);
	efuse_read_power_off();
	mutex_unlock(&efuse_lock);

	return err_flag;
}

int high_refresh_efuse_power_on_read(int reg_offset, u32 *val)
{
	int ret = 0;
	u32 flag = 0;

	ret = efuse_i2c_read(EFUSE_PW_ON_RD_END_FLAG, &flag);
	if (ret == 0 && flag == 0x1) {
		ret = efuse_i2c_read(EFUSE_POR_READ_DATA(reg_offset), val);
		if (ret == 0)
			pr_info("%s: reg %x, val: 0x%08x", __func__, reg_offset, *val);
	}
	return ret;
}

void high_refresh_efuse_init(void)
{
	mutex_init(&efuse_lock);
}
