// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/of.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

#include "../sprd_dsi.h"
#include "../sprd_dsi_panel.h"
#include "../sysfs/sysfs_display.h"

#include "kernel_gpio_fun.h"
#include "umb9230s.h"

#define SPRD_MIPI_DSI_FMT_DSC 0xff

/* iic2apb register */
#define REG_IIC2APB_BASE            0x0
#define REG_IIC2APB_INT_EN          (REG_IIC2APB_BASE + 0x08)
#define REG_IIC2APB_INT_RAW         (REG_IIC2APB_BASE + 0x0C)
#define REG_IIC2APB_INT_CLR         (REG_IIC2APB_BASE + 0x10)

/* clk register */
#define REG_CLK_CORE_BASE           0x4400
#define REG_CLOCK_SWITCH            (REG_CLK_CORE_BASE + 0x28)
#define REG_DSC_DIV                 (REG_CLK_CORE_BASE + 0x3C)

#define BIT_CLK_XBUF                0
#define BIT_CLK_RPLL                BIT(0)

/* top register */
#define REG_TOP_BASE                0x6000
#define REG_POWER_SWITCH            REG_TOP_BASE
#define REG_INTR_MASKED_STATUS      (REG_TOP_BASE + 0x50)
#define REG_INTR_RAW_STATUS         (REG_TOP_BASE + 0x54)
#define REG_INTR_MASK               (REG_TOP_BASE + 0x58)
#define REG_TE_DELAY_EN             (REG_TOP_BASE + 0x60)
#define REG_TE_DELAY                (REG_TOP_BASE + 0x64)
#define REG_CLK_CTRL                (REG_TOP_BASE + 0x70)

#define BIT_DEEP_SLEEP              BIT(0)
#define BIT_DPI_PAD_IN_SEL          BIT(7)

#define BIT_INTR_IIC2APB             BIT(0)
#define BIT_INTR_GPIO                BIT(1)
#define BIT_INTR_DSI_TX_INTERNAL_ERR BIT(2)
#define BIT_INTR_DSI_TX_PROTOCOL_ERR BIT(3)
#define BIT_INTR_DSI_TX_PLL_ERR      BIT(4)
#define BIT_INTR_DSI_RX_CAL_FAILED   BIT(5)
#define BIT_INTR_DSI_RX_CAL_DONE     BIT(6)
#define BIT_INTR_DSI_RX_INT_REQ              BIT(7)
#define BIT_INTR_VIDEO2CMD_FIFO_UNDERFLOW    BIT(8)

/* dsi rx registers */
#define REG_DSI_RX_BASE                     0x8000
#define REG_DSI_RX_PHY_STATE                (REG_DSI_RX_BASE + 0x0C)
#define REG_DSI_RX_ERR_STATE                (REG_DSI_RX_BASE + 0x10)
#define REG_DSI_RX_ERR_STATE_MSK            (REG_DSI_RX_BASE + 0x14)
#define REG_DSI_RX_ERR_STATE_CLR            (REG_DSI_RX_BASE + 0x18)
#define REG_DSI_RX_CAL_DONE                 (REG_DSI_RX_BASE + 0x1C)
#define REG_DSI_RX_CAL_FAILED               (REG_DSI_RX_BASE + 0x20)
#define REG_DSI_RX_MSK_CAL_DONE             (REG_DSI_RX_BASE + 0x24)
#define REG_DSI_RX_MSK_CAL_FAILED           (REG_DSI_RX_BASE + 0x28)
#define REG_DSI_RX_DSI_ALL_IDLE             (REG_DSI_RX_BASE + 0xA0)
#define REG_DSI_RX_INT_STS                  (REG_DSI_RX_BASE + 0x5C)
#define REG_DSI_RX_INT_STS_MSK              (REG_DSI_RX_BASE + 0x60)
#define REG_DSI_RX_INT_STS_CLR              (REG_DSI_RX_BASE + 0x64)
#define REG_DSI_RX_CAL_DONE_INT_CLR         (REG_DSI_RX_BASE + 0x68)
#define REG_DSI_RX_CAL_FAILED_INT_CLR       (REG_DSI_RX_BASE + 0x6C)

/* dsc registers */
#define REG_DSC_BASE                    0x9000
#define REG_DSC_CTRL                    (REG_DSC_BASE + 0x00)
#define REG_DSC_PIC_SIZE                (REG_DSC_BASE + 0x04)
#define REG_DSC_GRP_SIZE                (REG_DSC_BASE + 0x08)
#define REG_DSC_SLICE_SIZE              (REG_DSC_BASE + 0x0c)
#define REG_DSC_H_TIMING                (REG_DSC_BASE + 0x10)
#define REG_DSC_V_TIMING                (REG_DSC_BASE + 0x14)
#define REG_DSC_CFG0                    (REG_DSC_BASE + 0x18)
#define REG_DSC_CFG1                    (REG_DSC_BASE + 0x1c)
#define REG_DSC_CFG2                    (REG_DSC_BASE + 0x20)
#define REG_DSC_CFG3                    (REG_DSC_BASE + 0x24)
#define REG_DSC_CFG4                    (REG_DSC_BASE + 0x28)
#define REG_DSC_CFG5                    (REG_DSC_BASE + 0x2c)
#define REG_DSC_CFG6                    (REG_DSC_BASE + 0x30)
#define REG_DSC_CFG7                    (REG_DSC_BASE + 0x34)
#define REG_DSC_CFG8                    (REG_DSC_BASE + 0x38)
#define REG_DSC_CFG9                    (REG_DSC_BASE + 0x3c)
#define REG_DSC_CFG10                   (REG_DSC_BASE + 0x40)
#define REG_DSC_CFG11                   (REG_DSC_BASE + 0x44)
#define REG_DSC_CFG12                   (REG_DSC_BASE + 0x48)
#define REG_DSC_CFG13                   (REG_DSC_BASE + 0x4c)
#define REG_DSC_CFG14                   (REG_DSC_BASE + 0x50)
#define REG_DSC_CFG15                   (REG_DSC_BASE + 0x54)
#define REG_DSC_CFG16                   (REG_DSC_BASE + 0x58)
#define REG_DSC_STS0                    (REG_DSC_BASE + 0x5c)
#define REG_DSC_STS1                    (REG_DSC_BASE + 0x60)
#define REG_DSC_VERSION                 (REG_DSC_BASE + 0x64)

/* dsi tx registers */
#define REG_DSI_TX_BASE                         0x9400
#define REG_DSI_TX_PROTOCOL_INT_STS             (REG_DSI_TX_BASE + 0x08)
#define REG_DSI_TX_MASK_PROTOCOL_INT            (REG_DSI_TX_BASE + 0x0C)
#define REG_DSI_TX_INTERNAL_INT_STS             (REG_DSI_TX_BASE + 0x10)
#define REG_DSI_TX_MASK_INTERNAL_INT            (REG_DSI_TX_BASE + 0x14)
#define REG_DSI_TX_PHY_STATUS                   (REG_DSI_TX_BASE + 0x9C)
#define REG_DSI_TX_PROTOCOL_INT_CLR             (REG_DSI_TX_BASE + 0xC8)
#define REG_DSI_TX_INTERNAL_INT_CLR             (REG_DSI_TX_BASE + 0xCC)
#define REG_DSI_TX_INT_PLL_STS                  (REG_DSI_TX_BASE + 0x200)
#define REG_DSI_TX_INT_PLL_MSK                  (REG_DSI_TX_BASE + 0x204)
#define REG_DSI_TX_INT_PLL_CLR                  (REG_DSI_TX_BASE + 0x208)

#define BIT_INTERNAL_INT_ECC_SINGLE_ERR     BIT(27)
#define BIT_PROTOCOL_INT_ACK_WITH_ERR_8     BIT(24)

/* video2cmd registers */
#define REG_VIDEO2CMD_BASE          0xA200
#define REG_VIDEO2CMD_MODE          REG_VIDEO2CMD_BASE
#define REG_VIDEO2CMD_SIZE_X        (REG_VIDEO2CMD_BASE + 0x4)
#define REG_VIDEO2CMD_SIZE_Y        (REG_VIDEO2CMD_BASE + 0x8)
#define REG_VIDEO2CMD_SIZE_IDLE     (REG_VIDEO2CMD_BASE + 0xC)

/* color pattern registers */
#define REG_COLOR_PATTERN_BASE                  0xA400
#define REG_COLOR_PATTERN_EB                    REG_COLOR_PATTERN_BASE
#define REG_COLOR_PATTERN_VIDEO_CMD             (REG_COLOR_PATTERN_BASE + 0x04)
#define REG_COLOR_PATTERN_MODE                  (REG_COLOR_PATTERN_BASE + 0x08)
#define REG_COLOR_PATTERN_VS                    (REG_COLOR_PATTERN_BASE + 0x0C)
#define REG_COLOR_PATTERN_VBP                   (REG_COLOR_PATTERN_BASE + 0x10)
#define REG_COLOR_PATTERN_VFP                   (REG_COLOR_PATTERN_BASE + 0x14)
#define REG_COLOR_PATTERN_HS                    (REG_COLOR_PATTERN_BASE + 0x18)
#define REG_COLOR_PATTERN_HBP                   (REG_COLOR_PATTERN_BASE + 0x1C)
#define REG_COLOR_PATTERN_HFP                   (REG_COLOR_PATTERN_BASE + 0x20)
#define REG_COLOR_PATTERN_SIZE_X                (REG_COLOR_PATTERN_BASE + 0x24)
#define REG_COLOR_PATTERN_SIZE_Y                (REG_COLOR_PATTERN_BASE + 0x28)
#define REG_COLOR_PATTERN_COCLOR_DATA0          (REG_COLOR_PATTERN_BASE + 0x2C)
#define REG_COLOR_PATTERN_COCLOR_DATA1          (REG_COLOR_PATTERN_BASE + 0x30)
#define REG_COLOR_PATTERN_COCLOR_DATA2          (REG_COLOR_PATTERN_BASE + 0x34)
#define REG_COLOR_PATTERN_COCLOR_DATA3          (REG_COLOR_PATTERN_BASE + 0x38)
#define REG_COLOR_PATTERN_COCLOR_DATA4          (REG_COLOR_PATTERN_BASE + 0x3C)
#define REG_COLOR_PATTERN_IDLE                  (REG_COLOR_PATTERN_BASE + 0x40)
#define REG_COLOR_PATTERN_IS_COLOR_PATTERN_T    (REG_COLOR_PATTERN_BASE + 0x44)

int umb9230s_power_enable(struct umb9230s_device *umb9230s, int enable)
{
    u32 buf[2];

    if (enable) {
        umb9230s_init();

        mdelay(1);

        /* iic clock switch: 0x4428 bit0 set 1 */
        buf[0] = REG_CLOCK_SWITCH;
        buf[1] = 0x1;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        /* enable power switch: 0x6000 bit0 set 0 */
        iic2cmd_read(umb9230s->i2c_addr, REG_POWER_SWITCH, &buf[1], 1);
        buf[0] = REG_POWER_SWITCH;
        buf[1] &= (~(1 << 0));
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        mdelay(5);

        pr_info("umb9230s power up completed!\n");
    } else {
        /* close power switch: 0x6000 bit0 set 1 */
        iic2cmd_read(umb9230s->i2c_addr, REG_POWER_SWITCH, &buf[1], 1);
        buf[0] = REG_POWER_SWITCH;
        buf[1] |= 0x1;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        mdelay(5);

        /* EXT_RST_B_AP pull down, RFCTL16 GPIO8 */
        umb9230s_gpio_reset(0);

        /* xbuf_pd pull high，U2TXD GPIO171 */
        umb9230s_set_gpio_power(1);

        pr_info("umb9230s power down completed!\n");
    }

    return 0;
}

void umb9230s_videomode_copy(struct umb9230s_device *umb9230s, struct videomode *vm)
{
    if (umb9230s)
        memcpy(&umb9230s->dsi_ctx.vm, vm, sizeof(struct videomode));
}

int umb9230s_pattern_mode(struct umb9230s_device *umb9230s)
{
    struct videomode *vm = &umb9230s->dsi_ctx.vm;
    u32 buf[2] = {0};

    pr_info("enter case pattern mode test");

    /* 1: gen dpi timing; 0: stop gen timing */
    buf[0] = REG_COLOR_PATTERN_EB;
    buf[1] = 0x0;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* 0:video2cmd putput; 1:color pattern output */
    buf[0] = REG_COLOR_PATTERN_IS_COLOR_PATTERN_T;
    buf[1] = 0x1;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* 0: video mode; 1: cmd mode */
    buf[0] = REG_COLOR_PATTERN_VIDEO_CMD;
    buf[1] = 0x0;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_VS;
    buf[1] = vm->vsync_len & 0xff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_VBP;
    buf[1] = vm->vback_porch & 0xfff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_VFP;
    buf[1] = vm->vfront_porch & 0xfff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_HS;
    buf[1] = vm->hsync_len & 0xff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_HBP;
    buf[1] = vm->hback_porch & 0xfff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_HFP;
    buf[1] = vm->hfront_porch & 0xfff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_SIZE_X;
    buf[1] = vm->hactive & 0xfff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_SIZE_Y;
    buf[1] = vm->vactive & 0xfff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* 0: horizontal stripe; 1: vertical stripe; other: checkboard */
    buf[0] = REG_COLOR_PATTERN_MODE;
    buf[1] = 0x1;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_EB;
    buf[1] = 0x1;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_COCLOR_DATA0;
    buf[1] = 0x0;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_COCLOR_DATA1;
    buf[1] = 0xffffff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_COCLOR_DATA2;
    buf[1] = 0xff0000;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_COCLOR_DATA3;
    buf[1] = 0x00ff00;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_COLOR_PATTERN_COCLOR_DATA4;
    buf[1] = 0x0000ff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    return 0;
}

static void umb9230s_video2cmd_mode(struct umb9230s_device *umb9230s, bool enable)
{
    struct videomode *vm = &umb9230s->dsi_ctx.vm;
    u32 buf[4];

    pr_info("video2cmd enable\n");

    buf[0] = REG_VIDEO2CMD_MODE;
    buf[1] = enable;

    if ((vm->hactive % 3) && umb9230s->dsc_en)
        buf[2] = vm->hactive / 3 + 1;
    else
        buf[2] = vm->hactive / 3;

    buf[3] = vm->vactive;
    iic2cmd_write(umb9230s->i2c_addr, buf, 4);
}
#if 0
static void umb9230s_te_delay(struct umb9230s_device *umb9230s, uint32_t time)
{
    u32 buf[3] = {};

    pr_info("time:%d\n", time);

    buf[0] = REG_TE_DELAY_EN;
    if (time > 0) {
        buf[1] = 1;
        buf[2] = time;
    } else
        buf[1] = 0;

    iic2cmd_write(umb9230s->i2c_addr, buf, 3);
}
#endif
static void umb9230s_dsc_porch_set(struct umb9230s_device *umb9230s)
{
    struct videomode *vm = &umb9230s->dsi_ctx.vm;
    u32 buf[3];

    buf[0] = REG_DSC_H_TIMING;
    buf[1] = (vm->hsync_len << 0) | (vm->hback_porch  << 8) |
            (vm->hfront_porch << 20);
    buf[2] = (vm->vsync_len << 0) | (vm->vback_porch  << 8) |
            (vm->vfront_porch << 20);

    iic2cmd_write(umb9230s->i2c_addr, buf, 3);
}

static void umb9230s_dsc_config(struct umb9230s_device *umb9230s)
{
    struct videomode *vm = &umb9230s->dsi_ctx.vm;
    struct dsc_reg *param = &umb9230s->dsc_cfg.reg;
    u32 buf[24];

    pr_info("umb9230s dsc enable\n");

    calc_dsc_r4p0_params(&umb9230s->dsc_init, &umb9230s->dsc_cfg,
            umb9230s->output_bpc, umb9230s->slice_width, umb9230s->slice_height,
            vm->hactive, vm->vactive);

     /*
     * if 1 slice per line, clk_dsc = clk_dpi;
     * if 2 slice per line, clk_dsc = clk_dpi / 2;
     * if 4 slice per line, clk_dsc = clk_dpi / 4;
     */
    if (umb9230s->slice_width == vm->hactive / 2)
        buf[1] = 1;
    else
        buf[1] = 0;
    buf[0] = REG_DSC_DIV;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    param->dsc_pic_size = (vm->vactive << 16) | (vm->hactive << 0);
    param->dsc_h_timing = (vm->hsync_len << 0) | (vm->hback_porch << 8) |
            (vm->hfront_porch << 20);
    param->dsc_v_timing = (vm->vsync_len << 0) | (vm->vback_porch << 8) |
            (vm->vfront_porch << 20);

    if (umb9230s->dsi_ctx.work_mode == DSI_MODE_CMD)
        param->dsc_ctrl = 0x2000050b;
    else
        param->dsc_ctrl = 0x2000040b;

    buf[0] = REG_DSC_CTRL;
    memcpy(&buf[1], &param->dsc_ctrl, 23 * 4);
    iic2cmd_write(umb9230s->i2c_addr, buf, 24);
}

int umb9230s_vrr_timing(struct umb9230s_device *umb9230s)
{
    umb9230s_dsi_rx_state_reset(umb9230s);

    umb9230s_dsi_rx_vrr_timing(umb9230s);

    umb9230s_dsi_tx_vrr_timing(umb9230s);

    if (umb9230s->dsc_en)
        umb9230s_dsc_porch_set(umb9230s);

    return 0;
}

void umb9230s_enable_irq(struct umb9230s_device *umb9230s)
{
    u32 buf[2];

    buf[0] = REG_IIC2APB_INT_EN;
    buf[1] = 0xff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_IIC2APB_INT_CLR;
    buf[1] = 0xff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_INTR_MASK;
    buf[1] = 0xffffffff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    intr_irq_switch(true);
}

void umb9230s_disable_irq(struct umb9230s_device *umb9230s)
{
    u32 buf[2];

    intr_irq_switch(false);

    buf[0] = REG_IIC2APB_INT_EN;
    buf[1] = 0xff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_IIC2APB_INT_CLR;
    buf[1] = 0xff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_INTR_MASK;
    buf[1] = 0xffffffff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);
}

void umb9230s_enable(struct umb9230s_device *umb9230s)
{
    if (!umb9230s)
        return;

    pr_info("enable umb9230s\n");

    mutex_lock(&umb9230s->lock);

    umb9230s_power_enable(umb9230s, 1);

    umb9230s_phy_rx_init(umb9230s);

    umb9230s_dsi_rx_init(umb9230s);

    if (umb9230s->dsc_en)
        umb9230s_dsc_config(umb9230s);

    if (umb9230s->dsi_ctx.work_mode == DSI_MODE_CMD)
        umb9230s_video2cmd_mode(umb9230s, true);

    umb9230s_dsi_tx_enable(umb9230s);

    umb9230s_phy_tx_enable(umb9230s);

    umb9230s_dsi_tx_lp_cmd_enable(umb9230s, true);

    /* clk_hs_rqst */
    umb9230s_phy_tx_hs_clk_en(umb9230s, true);

    umb9230s_enable_irq(umb9230s);

    umb9230s->enabled = true;

    mutex_unlock(&umb9230s->lock);
}

void umb9230s_isr(void *data)
{
    struct umb9230s_device *umb9230s = data;
    u32 reg_val;
    u32 status;
    u32 buf[2];

    iic2cmd_read(umb9230s->i2c_addr, REG_INTR_MASKED_STATUS, &reg_val, 1);

    if (reg_val & BIT_INTR_IIC2APB) {
        iic2cmd_read(umb9230s->i2c_addr, REG_IIC2APB_INT_RAW, &status, 1);
        pr_err("IIC2APB_INT_RAW:0x%08x\n", status);

        buf[0] = REG_IIC2APB_INT_EN;
        buf[1] = ~status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        buf[0] = REG_IIC2APB_INT_CLR;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);
    }

    if (reg_val & BIT_INTR_DSI_TX_INTERNAL_ERR) {
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_INTERNAL_INT_STS, &status, 1);
        pr_err("DSI_TX_INTERNAL_INT_STS:0x%08x\n", status);

        buf[0] = REG_DSI_TX_MASK_INTERNAL_INT;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        buf[0] = REG_DSI_TX_INTERNAL_INT_CLR;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);
    }

    if (reg_val & BIT_INTR_DSI_TX_PROTOCOL_ERR) {
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_PROTOCOL_INT_STS, &status, 1);
        pr_err("DSI_TX_PROTOCOL_INT_STS:0x%08x\n", status);

        buf[0] = REG_DSI_TX_MASK_PROTOCOL_INT;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        buf[0] = REG_DSI_TX_PROTOCOL_INT_CLR;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);
    }

    if (reg_val & BIT_INTR_DSI_TX_PLL_ERR) {
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_INT_PLL_STS, &status, 1);
        pr_err("DSI_TX_INT_PLL_STS:0x%08x\n", status);

        buf[0] = REG_DSI_TX_INT_PLL_MSK;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        buf[0] = REG_DSI_TX_INT_PLL_CLR;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    }

    if (reg_val & BIT_INTR_DSI_RX_CAL_FAILED) {
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_RX_CAL_FAILED, &status, 1);
        pr_err("DSI_RX_CAL_FAILED:0x%08x\n", status);

        buf[0] = REG_DSI_RX_MSK_CAL_FAILED;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        buf[0] = REG_DSI_RX_CAL_FAILED_INT_CLR;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);
    }

    if (reg_val & REG_DSI_RX_ERR_STATE) {
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_RX_INT_STS, &status, 1);
        pr_err("DSI_RX_INT_STS:0x%08x\n", status);

        buf[0] = REG_DSI_RX_INT_STS_MSK;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        buf[0] = REG_DSI_RX_INT_STS_CLR;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_RX_ERR_STATE, &status, 1);
        pr_err("DSI_RX_ERR_STATE:0x%08x\n", status);

        buf[0] = REG_DSI_RX_ERR_STATE_MSK;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        buf[0] = REG_DSI_RX_ERR_STATE_CLR;
        buf[1] = status;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    }

     if (reg_val & BIT_INTR_VIDEO2CMD_FIFO_UNDERFLOW)
        pr_err("VIDEO2CMD_FIFO_UNDERFLOW\n");
}
#if 0
static int wait_umb9230s_idle(struct umb9230s_device *umb9230s)
{
    union _dsi_rx_0x0C phy_state;
    union _dsi_rx_0xA0 dsi_all_idle;
    union _0x9C phy_status;
    int i;

    for (i = 0; i < 5000; i++) {
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_RX_PHY_STATE, &phy_state.val, 1);
        if (!phy_state.bits.phy_stopstatedata0 || !phy_state.bits.phy_stopstatedata1 ||
            !phy_state.bits.phy_stopstatedata2 || !phy_state.bits.phy_stopstatedata3) {
            udelay(1);
            continue;
        }

        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_RX_DSI_ALL_IDLE, &dsi_all_idle.val, 1);
        if (!dsi_all_idle.bits.dsi_all_idle) {
            udelay(1);
            continue;
        }

        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_PHY_STATUS, &phy_status.val, 1);
        if (!phy_status.bits.phy_stopstate0lane || !phy_status.bits.phy_stopstate1lane ||
            !phy_status.bits.phy_stopstate2lane || !phy_status.bits.phy_stopstate3lane) {
            udelay(1);
            continue;
        }

        break;
    }

    if (i < 5000)
        return 0;

    return -1;
}
#endif
void umb9230s_disable(struct umb9230s_device *umb9230s)
{
    if (!umb9230s)
        return;

    pr_info("disable umb9230s\n");

    mutex_lock(&umb9230s->lock);

    umb9230s_disable_irq(umb9230s);

    //if (wait_umb9230s_idle(umb9230s))
    //    pr_err("wait umb9230s idle timeout\n");

    umb9230s_dsi_tx_fini(umb9230s);

    umb9230s_power_enable(umb9230s, 0);

    umb9230s->enabled = false;

    mutex_unlock(&umb9230s->lock);
}

static int umb9230s_parse_dt(struct umb9230s_device *umb9230s, struct device_node *np)
{
    struct dsi_tx_context *ctx = &umb9230s->dsi_ctx;
    int ret = 0;
    u32 tmp;

   if (!of_property_read_u32(np, "sprd,data-hs2lp", &tmp))
        ctx->data_hs2lp = tmp;
    else
        ctx->data_hs2lp = 120;

    if (!of_property_read_u32(np, "sprd,data-lp2hs", &tmp))
        ctx->data_lp2hs = tmp;
    else
        ctx->data_lp2hs = 500;

    if (!of_property_read_u32(np, "sprd,clk-hs2lp", &tmp))
        ctx->clk_hs2lp = tmp;
    else
        ctx->clk_hs2lp = 4;

    if (!of_property_read_u32(np, "sprd,clk-lp2hs", &tmp))
        ctx->clk_lp2hs = tmp;
    else
        ctx->clk_lp2hs = 15;

    if (!of_property_read_u32(np, "sprd,max-read-time", &tmp))
        ctx->max_rd_time = tmp;
    else
        ctx->max_rd_time = 0x8000;

    if (!of_property_read_u32(np, "sprd,int0_mask", &tmp))
        ctx->int0_mask = tmp;
    else
        ctx->int0_mask = 0xffffffff;

    if (!of_property_read_u32(np, "sprd,int1_mask", &tmp))
        ctx->int1_mask = tmp;
    else
        ctx->int1_mask = 0xffffffff;

    if (!of_property_read_u32(np, "sprd,i2c-addr", &tmp))
        umb9230s->i2c_addr = tmp;
    else {
        pr_err("umb9230s no i2c-addr\n");
        umb9230s->i2c_addr = 0x12;
    }

    pr_info("umb9230 i2c_addr:0x%x\n", umb9230s->i2c_addr);
    return ret;
}

void umb9230s_parse_lcd_info(struct umb9230s_device *umb9230s, struct device_node *lcd_node)
{
    struct dsi_tx_context *ctx = &umb9230s->dsi_ctx;
    int ret = 0;
    u32 val;
    const char *str;

    ret = of_property_read_u32(lcd_node, "sprd,umb9230s-dsi-work-mode", &val);
    if (!ret) {
        if (val == SPRD_DSI_MODE_CMD)
            ctx->work_mode = DSI_MODE_CMD;
        else {
            ctx->work_mode = DSI_MODE_VIDEO;
            if (val == SPRD_DSI_MODE_VIDEO_BURST)
                ctx->burst_mode = VIDEO_BURST_WITH_SYNC_PULSES;
            else if (val == SPRD_DSI_MODE_VIDEO_SYNC_PULSE)
                ctx->burst_mode = VIDEO_NON_BURST_WITH_SYNC_PULSES;
            else if (val == SPRD_DSI_MODE_VIDEO_SYNC_EVENT)
                ctx->burst_mode = VIDEO_NON_BURST_WITH_SYNC_EVENTS;
        }
    } else {
        DRM_ERROR("umb9230s work mode is not found! use video mode\n");
        ctx->work_mode = DSI_MODE_VIDEO;
        ctx->burst_mode = VIDEO_BURST_WITH_SYNC_PULSES;
    }

    ret = of_property_read_string(lcd_node, "sprd,umb9230s-dsi-color-format", &str);
    if (ret)
        ctx->format = MIPI_DSI_FMT_RGB888;
    else if (!strcmp(str, "rgb888"))
        ctx->format = MIPI_DSI_FMT_RGB888;
    else if (!strcmp(str, "dsc")) {
        ctx->format = SPRD_MIPI_DSI_FMT_DSC;
    } else
        DRM_ERROR("dsi-color-format (%s) is not supported\n", str);

    ret = of_property_read_u32(lcd_node, "sprd,umb9230s-phy-bit-clock", &val);
    if (!ret)
        ctx->byte_clk = val / 8;
    else
        ctx->byte_clk = 500000 / 8;

    ret = of_property_read_u32(lcd_node, "sprd,umb9230s-dsc-enable", &val);
    if (!ret)
        umb9230s->dsc_en = val;
    else
        DRM_DEBUG("dsc-enable is not found!\n");

    ret = of_property_read_u32(lcd_node, "sprd,umb9230s-slice-width", &val);
    if (!ret)
        umb9230s->slice_width = val;
    else
        DRM_DEBUG("slice-width is not found!\n");

    ret = of_property_read_u32(lcd_node, "sprd,umb9230s-slice-height", &val);
    if (!ret)
        umb9230s->slice_height = val;
    else
        DRM_DEBUG("slice-height is not found!\n");

    ret = of_property_read_u32(lcd_node, "sprd,umb9230s-output-bpc", &val);
    if (!ret)
        umb9230s->output_bpc = val;
    else
        DRM_DEBUG("output-bpc is not found!\n");
}

static const struct of_device_id umb9230s_match_table[] = {
    { .compatible = "sprd,umb9230s", },
    { },
};

static int umb9230s_device_create(struct umb9230s_device *umb9230s,
                struct device *parent)
{
    int ret;

    umb9230s->dev.class = display_class;
    umb9230s->dev.parent = parent;
    umb9230s->dev.of_node = parent->of_node;
    dev_set_name(&umb9230s->dev, "umb9230s");
    dev_set_drvdata(&umb9230s->dev, umb9230s);

    ret = device_register(&umb9230s->dev);
    if (ret)
        pr_err("umb9230s device register failed\n");

    return ret;
}

static int umb9230s_probe(struct platform_device *pdev)
{
    struct device_node *np = pdev->dev.of_node;
    struct umb9230s_device *umb9230s;
    int ret;

    umb9230s = devm_kzalloc(&pdev->dev, sizeof(*umb9230s), GFP_KERNEL);
    if (!umb9230s) {
        pr_err("failed to allocate umb9230s data.\n");
        return -ENOMEM;
    }

    ret = umb9230s_parse_dt(umb9230s, np);
    if (ret) {
        pr_err("parse umb9230s info failed\n");
        return ret;
    }

    umb9230s->phy_ctx.i2c_addr = umb9230s->i2c_addr;

    ret = umb9230s_device_create(umb9230s, &pdev->dev);
    if (ret) {
        pr_err("umb9230s device create failed\n");
        return ret;
    }

    ret = umb9230s_gpio_request(&pdev->dev);
    if (ret) {
        pr_err("umb9230s gpio request failed\n");
        return ret;
    }

    ret = umb9230s_sysfs_init(&umb9230s->dev);
    if (ret) {
        pr_err("umb9230s sysfs init failed\n");
        return ret;
    }

    platform_set_drvdata(pdev, umb9230s);

    umb9230s->pll = &umb9230s_dphy_tx_pll_ops;

    mutex_init(&umb9230s->lock);

    umb9230s->enabled = true;

    intr_irq_registration(umb9230s);

    return ret;
}

static void umb9230s_remove(struct platform_device *pdev)
{
    struct umb9230s_device *umb9230s;

    pr_info("%s()\n", __func__);

    umb9230s = platform_get_drvdata(pdev);

    if (umb9230s) {
        umb9230s_sysfs_deinit(&umb9230s->dev);
        intr_irq_free(umb9230s);
    }

    return;
}

struct platform_driver sprd_umb9230s_driver = {
    .probe = umb9230s_probe,
    .remove = umb9230s_remove,
    .driver = {
        .name = "umb9230s-drv",
        .of_match_table = umb9230s_match_table,
    },
};

MODULE_AUTHOR("Youyou Yu <youyou.yu@unisoc.com>");
MODULE_DESCRIPTION("Unisoc Display UMB9230S Driver");
MODULE_LICENSE("GPL v2");
