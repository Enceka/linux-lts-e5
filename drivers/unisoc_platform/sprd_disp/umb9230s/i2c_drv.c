/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/clk.h>
#include <linux/types.h>
#include <linux/regmap.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <asm/io.h>
#include "i2c_api.h"

#define I2C_TIMEOUT					1000
#define I2C_ERROR_NACK					1
#define I2C_ERROR_BUSY					2

#define HS_MODE_ENABLE					0
#define HS_MODE_DISABLE					1

/* I2C Frequency Modes */
#define I2C_MAX_STANDARD_MODE_FREQ	100000
#define I2C_MAX_FAST_MODE_FREQ		400000
#define I2C_MAX_FAST_MODE_PLUS_FREQ	1000000
#define I2C_MAX_TURBO_MODE_FREQ		1400000
#define I2C_MAX_HIGH_SPEED_MODE_FREQ	3400000
#define I2C_MAX_ULTRA_FAST_MODE_FREQ	5000000

#define I2C_CTL			0x00
#define I2C_ADDR_CFG		0x04
#define I2C_COUNT		0x08
#define I2C_RX			0x0c
#define I2C_TX			0x10
#define I2C_STATUS		0x14
#define I2C_HSMODE_CFG		0x18
#define I2C_VERSION		0x1c
#define ADDR_DVD0		0x20
#define ADDR_DVD1		0x24
#define ADDR_STA0_DVD		0x28
#define ADDR_RST		0x2c

/* I2C_CTL */
#define I2C_NACK_EN		BIT(22)
#define I2C_TRANS_EN		BIT(21)
#define STP_EN			BIT(20)
#define FIFO_AF_LVL_MASK	GENMASK(19, 16)
#define FIFO_AF_LVL		16
#define FIFO_AE_LVL_MASK	GENMASK(15, 12)
#define FIFO_AE_LVL		12
#define I2C_DMA_EN		BIT(11)
#define FULL_INTEN		BIT(10)
#define EMPTY_INTEN		BIT(9)
#define I2C_DVD_OPT		BIT(8)
#define I2C_OUT_OPT		BIT(7)
#define I2C_TRIM_OPT		BIT(6)
#define I2C_HS_MODE		BIT(4)
#define I2C_MODE		BIT(3)
#define I2C_EN			BIT(2)
#define I2C_INT_EN		BIT(1)
#define I2C_START		BIT(0)

/* I2C_STATUS */
#define SDA_IN			BIT(21)
#define SCL_IN			BIT(20)
#define FIFO_FULL		BIT(4)
#define FIFO_EMPTY		BIT(3)
#define I2C_INT			BIT(2)
#define I2C_RX_ACK		BIT(1)
#define I2C_BUSY		BIT(0)

/* ADDR_RST */
#define I2C_RST			BIT(0)

#define I2C_FIFO_DEEP		12
#define I2C_FIFO_FULL_THLD	12
#define I2C_FIFO_EMPTY_THLD	4
#define I2C_DATA_STEP		12
#define I2C_ADDR_DVD0_CALC(high, low)	\
	((((high) & GENMASK(15, 0)) << 16) | ((low) & GENMASK(15, 0)))
#define I2C_ADDR_DVD1_CALC(high, low)	\
	(((high) & GENMASK(31, 16)) | (((low) & GENMASK(31, 16)) >> 16))

/* dynamic modify clk_freq flag  */
#define	I2C_3M4_FLAG		0x0100
#define	I2C_1M_FLAG		0x0080
#define	I2C_400K_FLAG		0x0040

/* SPRD i2c data structure */
struct sprd_i2c {
	struct device *dev;
	void __iomem *base;
	struct i2c_msg *msg;
	struct clk *clk;
	u32 src_clk;
	u32 bus_freq;
	u8 *buf;
	u32 count;
	u32 full_thld;
	int err;
	bool ack_flag;
	struct reset_control *rst;
};

struct sprd_i2c *i2c_dev;

static inline u16 bswap_16(u16 x)
{
	return (x<<8)|(x>>8);
}

static inline u32 bswap_32(u32 x)
{
	x = ((x<<8)&0xff00ff00) | ((x>>8)&0x00ff00ff);
	return (x<<16)|(x>>16);
}

static void sprd_i2c_dump_reg(void __iomem *addr)
{
	dev_err(i2c_dev->dev,"I2C_CTL 	= 0x%x\n", readl(addr + I2C_CTL));
	dev_err(i2c_dev->dev,"I2C_ADDR_CFG 	= 0x%x\n", readl(addr + I2C_ADDR_CFG));
	dev_err(i2c_dev->dev,"I2C_COUNT = 0x%x\n", readl(addr + I2C_COUNT));
	dev_err(i2c_dev->dev,"I2C_STATUS = 0x%x\n", readl(addr + I2C_STATUS));
	dev_err(i2c_dev->dev,"ADDR_DVD0 = 0x%x\n", readl(addr + ADDR_DVD0));
	dev_err(i2c_dev->dev,"ADDR_DVD1 = 0x%x\n", readl(addr + ADDR_DVD1));
	dev_err(i2c_dev->dev,"ADDR_STA0_DVD = 0x%x\n", readl(addr + ADDR_STA0_DVD));
}

static void sprd_i2c_set_count(void __iomem *addr, u32 count)
{
	writel(count, addr + I2C_COUNT);
}

static void sprd_i2c_send_stop(void __iomem *addr, int stop)
{
	u32 tmp = readl(addr + I2C_CTL);

	if (stop)
		writel(tmp & ~STP_EN, addr + I2C_CTL);
	else
		writel(tmp | STP_EN, addr + I2C_CTL);
}

sprd_i2c_opt_mode(void __iomem *addr, int rw)
{
	int cmd = readl(addr + I2C_CTL) & (~I2C_MODE);

	writel((cmd | rw << 3), addr + I2C_CTL);

	return 0;
}

static void sprd_i2c_opt_start(void __iomem *addr)
{
	u32 tmp = readl(addr + I2C_CTL);

	writel(tmp | I2C_START, addr + I2C_CTL);
}


static void sprd_i2c_clear_start(void __iomem *addr)
{
	u32 tmp = readl(addr + I2C_CTL);

	writel(tmp & ~I2C_START, addr + I2C_CTL);
}

static void sprd_i2c_clear_ack(void __iomem *addr)
{
	u32 tmp = readl(addr + I2C_STATUS);

	writel(tmp & ~I2C_RX_ACK, addr + I2C_STATUS);
}

static void sprd_i2c_clear_irq(void __iomem *addr)
{
	u32 tmp = readl(addr + I2C_STATUS);

	writel((tmp & ~I2C_INT) | I2C_RX_ACK, addr + I2C_STATUS);
}

static void sprd_i2c_reset_fifo(void __iomem *addr)
{
	writel(I2C_RST, addr + ADDR_RST);
}

static void sprd_i2c_set_devaddr(void __iomem *addr, struct i2c_msg *m)
{
	writel(m->addr << 1, addr + I2C_ADDR_CFG);
}

static void sprd_i2c_set_full_thld(void __iomem *addr, u32 full_thld)
{
	u32 tmp = readl(addr + I2C_CTL);

	tmp &= ~FIFO_AF_LVL_MASK;
	tmp |= full_thld << FIFO_AF_LVL;
	writel(tmp, addr + I2C_CTL);
};

static void sprd_i2c_set_empty_thld(void __iomem *addr, u32 empty_thld)
{
	u32 tmp = readl(addr + I2C_CTL);

	tmp &= ~FIFO_AE_LVL_MASK;
	tmp |= empty_thld << FIFO_AE_LVL;
	writel(tmp, addr + I2C_CTL);
};

static void sprd_i2c_set_clk(void __iomem *addr, u32 freq)
{
	u32 apb_clk = i2c_dev->src_clk;
	/*
	 * From I2C databook, the prescale calculation formula:
	 * prescale = freq_i2c / (4 * freq_scl) - 4;
	 */
	u32 i2c_dvd = apb_clk / (4 * freq) - 4;
	/*
	 * From I2C databook, the high period of SCL clock is recommended as
	 * 40% (2/5), and the low period of SCL clock is recommended as 60%
	 * (3/5), then the formula should be:
	 * high = (prescale * 2 * 2) / 5
	 * low = (prescale * 2 * 3) / 5
	 */
	u32 high = ((i2c_dvd << 1) * 2) / 5;
	u32 low = ((i2c_dvd << 1) * 3) / 5;
	u32 div0 = I2C_ADDR_DVD0_CALC(high, low);
	u32 div1 = I2C_ADDR_DVD1_CALC(high, low);

	writel(div0, addr + ADDR_DVD0);
	writel(div1, addr + ADDR_DVD1);

	/* Start hold timing = hold time(us) * source clock */
	if (freq == I2C_MAX_FAST_MODE_FREQ)
		writel((14 * apb_clk) / 10000000, addr + ADDR_STA0_DVD);
	else if (freq == I2C_MAX_STANDARD_MODE_FREQ)
		writel((4 * apb_clk) / 1000000, addr + ADDR_STA0_DVD);
	else if (freq == I2C_MAX_FAST_MODE_PLUS_FREQ)
		writel((8 * apb_clk) / 10000000, addr + ADDR_STA0_DVD);
	else if (freq == I2C_MAX_HIGH_SPEED_MODE_FREQ)
		writel((8 * apb_clk) / 10000000, addr + ADDR_STA0_DVD);
}

void sprd_i2c_reset(void)
{
	int ret;

	ret = reset_control_reset(i2c_dev->rst);
	if (ret < 0)
		dev_err(i2c_dev->dev, "i2c soft reset failed, ret = %d\n", ret);
}

void sprd_i2c_enable()
{
	u32 tmp = I2C_OUT_OPT;
	void __iomem *addr;

	addr=i2c_dev->base;
	writel(tmp, addr + I2C_CTL);
	sprd_i2c_set_full_thld(addr, I2C_FIFO_FULL_THLD);
	sprd_i2c_set_empty_thld(addr, I2C_FIFO_EMPTY_THLD);
	sprd_i2c_set_clk(addr, i2c_dev->bus_freq);
	sprd_i2c_reset_fifo(addr);
	sprd_i2c_clear_irq(addr);

	tmp = readl(addr + I2C_CTL);
	writel(tmp | I2C_EN | I2C_INT_EN | I2C_NACK_EN | I2C_TRANS_EN, addr + I2C_CTL);
}

static int sprd_wait_for_fifo_empty(void __iomem *addr)
{
	int status;
	int timeout = I2C_TIMEOUT;

	status = readl(addr + I2C_STATUS);
	while (!(status &(FIFO_EMPTY)) && timeout--) {
		status = readl(addr + I2C_STATUS);
	}

	if (timeout <= 0) {
		dev_err(i2c_dev->dev,"Timed out in wait_for_fifo_empty: status=%04x\n", status);
		sprd_i2c_dump_reg(addr);
	}

	return status;
}

static int sprd_wait_for_data_ready(void __iomem *addr)
{
	int status;
	int timeout = I2C_TIMEOUT;
	status = readl(addr + I2C_STATUS);
	while (!((status & (I2C_INT)) || status & (FIFO_FULL)) && timeout--) {
		status = readl(addr + I2C_STATUS);
	}
	sprd_i2c_clear_start(addr);
	if (timeout <= 0) {
		dev_err(i2c_dev->dev,"Timed out in wait_for_data_ready: status=%04x\n", status);
		sprd_i2c_dump_reg(addr);
		return -1;
	}

	return status;
}

static int sprd_wait_for_int(void __iomem *addr)
{
	int status;
	int timeout = I2C_TIMEOUT;
	status = readl(addr + I2C_STATUS);
	//need to check
	while (!(status & (I2C_INT)) && timeout--) {
		status = readl(addr + I2C_STATUS);
	}

	if (timeout <= 0) {
		dev_err(i2c_dev->dev,"Timed out in wait_for_int: status=%04x\n", status);
		sprd_i2c_dump_reg(addr);
		sprd_i2c_clear_start(addr);
		return -1;
	}

	status = readl(addr + I2C_STATUS);
	sprd_i2c_clear_irq(addr);
	sprd_i2c_clear_ack(addr);
	sprd_i2c_clear_start(addr);

	return !!(status & I2C_RX_ACK) ? -EIO : 0;
}

static int
sprd_i2c_write_byte(void __iomem *addr, unsigned char *byte, int c)
{
	int i =0;

	for(i = 0; i < c; i++) {
		writel(byte[i], addr + I2C_TX);
	}

	return 0;
}

static int
sprd_i2c_read_byte(void __iomem *addr, unsigned char *byte,  int c)
{
	int i =0;

	for(i = 0; i < c; i++) {
		byte[i] = (unsigned char)(readl(addr + I2C_RX));
	}

	return 0;
}

static int
sprd_i2c_writebytes(void __iomem *addr, struct i2c_msg *m)
{
	int rc = 0;
	int s_len = 0, msg_len = m->len;
	unsigned char *p_buf = m->buf;

	sprd_i2c_opt_mode(addr, 0);
	s_len = msg_len > I2C_FIFO_DEEP ?  I2C_FIFO_DEEP : msg_len;
	rc = sprd_i2c_write_byte(addr, p_buf, s_len);
	sprd_i2c_opt_start(addr);
	sprd_wait_for_fifo_empty(addr);
	msg_len = msg_len -s_len;
	p_buf +=s_len;

	while(msg_len) {
		s_len = msg_len < I2C_FIFO_DEEP ? msg_len : I2C_FIFO_DEEP;
		rc = sprd_i2c_write_byte(addr, p_buf, s_len);
		msg_len -= s_len;
		p_buf +=s_len;
		sprd_wait_for_fifo_empty(addr);
	}

	return sprd_wait_for_int(addr);
}

static int
sprd_i2c_readbytes(void __iomem *addr, struct i2c_msg *m)
{
	int rc = 0;
	int get_len = 0, msg_len = m->len;
	unsigned char *p_buf = m->buf;

	sprd_i2c_opt_mode(addr, 1);
	sprd_i2c_opt_start(addr);

	while (msg_len){
		sprd_wait_for_data_ready(addr);
		get_len = msg_len < I2C_FIFO_DEEP ? msg_len : I2C_FIFO_DEEP;
		rc = sprd_i2c_read_byte(addr,p_buf, get_len);
		msg_len -= get_len;
		p_buf +=get_len;
		sprd_i2c_clear_irq(addr);
	}

	return rc;
}


static int
sprd_i2c_handle_msg(void __iomem *addr, struct i2c_msg *m, int last)
{
	sprd_i2c_set_devaddr(addr,m);
	sprd_i2c_send_stop(addr, last);
	sprd_i2c_set_count(addr, m->len);

	if ((m->flags & I2C_M_RD))
		return sprd_i2c_readbytes(addr, m);
	 else
		return sprd_i2c_writebytes(addr, m);
}

int sprd_i2c_master_xfer(struct i2c_msg *m, int n)
{
	int im = 0, ret = 0;

	ret = clk_prepare_enable(i2c_dev->clk);
	if (ret)
	{
		dev_err(i2c_dev->dev,"i2c_clk_prepare_enable error \n");
	}

	sprd_i2c_enable();
	sprd_i2c_reset_fifo(i2c_dev->base);
	sprd_i2c_clear_irq(i2c_dev->base);

	for (im = 0; ret >= 0 && im != n; im++){
		ret = sprd_i2c_handle_msg(i2c_dev->base, &m[im], im == n - 1);
		if(ret < 0){
			sprd_i2c_reset();
			break;
		}
	}

	clk_disable_unprepare(i2c_dev->clk);

	return (ret >= 0) ? im : ret;
}

int iic2cmd_write(u8 dec_addr, u32* buf, u32 len)
{
	u8 * write_buf = (u8 *)buf;
	u32 i=0;
	int ret;

	/*
	 * iic2cmd support 16bit reg addr and 32bit data, buf = reg_addr + reg_data .
	 * to avoid unnecessary memory operations, we need to ignore the first 16 bits.
	 */

	struct i2c_msg msg = {
		.addr = dec_addr,
		.flags = 0,
		.len = 4*len-2,
		.buf = write_buf+2,
	};

	write_buf[2] = buf[0] >> 0x8;
	write_buf[3] = buf[0] & 0xFF;

	for (i=1;i<len;i++){
		buf[i] = bswap_32(buf[i]);
	}

	ret = sprd_i2c_master_xfer(&msg,1);

	for (i=1;i<len;i++){
		buf[i] = bswap_32(buf[i]);
	}

	write_buf[2] = buf[0] >> 0x8;
	write_buf[3] = buf[0] & 0xFF;

	return ret;
}

int iic2cmd_read(u8 dec_addr, u16 reg_addr , u32* buf, u32 len)
{
	u8 * reg = (u8 *)&reg_addr;
	u8 * read_buf = (u8 *)buf;
	u32 i=0;
	int ret;
	struct i2c_msg msg[2];

	msg[0].addr = dec_addr;
	msg[0].flags = 0;
	msg[0].buf = reg;
	msg[0].len = 2;

	msg[1].addr = dec_addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = read_buf;
	msg[1].len = 4*len;

	reg_addr = bswap_16(reg_addr);

	ret = sprd_i2c_master_xfer(msg,2);

	for (i=0;i<len;i++){
		buf[i] = bswap_32(buf[i]);
	}

	return ret;
}

static int sprd_i2c_clk_init(struct sprd_i2c *i2c_dev)
{
	struct clk *clk_i2c, *clk_parent;

	clk_i2c = devm_clk_get(i2c_dev->dev, "i2c");
	if (IS_ERR(clk_i2c)) {
		dev_warn(i2c_dev->dev,"umb9230s_i2c can't get the i2c clock\n");
		clk_i2c = NULL;
	}

	clk_parent = devm_clk_get(i2c_dev->dev, "source");
	if (IS_ERR(clk_parent)) {
		dev_warn(i2c_dev->dev,"umb9230s_i2c can't get the i2c source\n");
		clk_parent = NULL;
	}

	if (!!clk_i2c && !!clk_parent && !clk_set_parent(clk_i2c, clk_parent))
		i2c_dev->src_clk = clk_get_rate(clk_i2c);
	else
		i2c_dev->src_clk = 26000000;

	dev_dbg(i2c_dev->dev,"umb9230s_i2c the i2c clock is %d\n" ,i2c_dev->src_clk);

	i2c_dev->clk = devm_clk_get(i2c_dev->dev, "enable");
	if (IS_ERR(i2c_dev->clk)) {
		dev_err(i2c_dev->dev,"umb9230s_i2c can't get the i2c source\n");
		return PTR_ERR(i2c_dev->clk);
	}

	return 0;
}


static int umb9230s_i2c_probe(struct platform_device *pdev)
{
	struct device_node *np ;
	struct resource *res;
	struct device *dev = &pdev->dev;
	u32 frq;
	int ret;

	np = pdev->dev.of_node;
	i2c_dev = devm_kzalloc(dev, sizeof(struct sprd_i2c), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOMEM;
	i2c_dev->base = devm_platform_ioremap_resource(pdev, 0);

	if (IS_ERR(i2c_dev->base))
		return PTR_ERR(i2c_dev->base);

	i2c_dev->dev = dev;
	platform_set_drvdata(pdev, i2c_dev);

	if (!of_property_read_u32(np, "clock-frequency", &frq))
	{
		dev_dbg(i2c_dev->dev,"sprd,clock-frequency %d \n", frq);
		i2c_dev->bus_freq = frq;
	}

	ret = sprd_i2c_clk_init(i2c_dev);
	if(ret)
		return ret;

	i2c_dev->rst = devm_reset_control_get(i2c_dev->dev, "i2c_rst");
	if (IS_ERR(i2c_dev->rst)) {
		dev_err(i2c_dev->dev,"can't get i2c reset node\n");
		return PTR_ERR(i2c_dev->rst);
	}

	return 0;
}


static void umb9230s_i2c_remove(struct platform_device *pdev)
{
	clk_disable_unprepare(i2c_dev->clk);
}


static const struct of_device_id umb9230s_i2c_matchs[] = {
	{ .compatible = "sprd,umb9230s-i2c", },
	{ }
};

struct platform_driver umb9230s_i2c_driver = {
	.probe      = umb9230s_i2c_probe,
	.remove     = umb9230s_i2c_remove,
	.driver = {
		.name   = "umb9230s_i2c_drv",
		.of_match_table = umb9230s_i2c_matchs,
	},
};

MODULE_DESCRIPTION("Spreadtrum I2C master controller driver");
MODULE_LICENSE("GPL v2");
