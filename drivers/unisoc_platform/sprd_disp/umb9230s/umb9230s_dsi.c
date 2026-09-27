// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

#include "../sprd_dsi.h"

#include "kernel_gpio_fun.h"
#include "umb9230s.h"

/* dsi rx registers */
#define REG_DSI_RX_BASE                     0x8000
#define REG_DSI_RX_IP_VERSION               (REG_DSI_RX_BASE + 0x0)
#define REG_DSI_RX_PHY_PD_N                 (REG_DSI_RX_BASE + 0x04)
#define REG_DSI_RX_RST_DPHY_N               (REG_DSI_RX_BASE + 0x08)
#define REG_DSI_RX_PHY_STATE                (REG_DSI_RX_BASE + 0x0C)
#define REG_DSI_RX_ERR_STATE                (REG_DSI_RX_BASE + 0x10)
#define REG_DSI_RX_ERR_STATE_MSK            (REG_DSI_RX_BASE + 0x14)
#define REG_DSI_RX_ERR_STATE_CLR            (REG_DSI_RX_BASE + 0x18)
#define REG_DSI_RX_CAL_DONE                 (REG_DSI_RX_BASE + 0x1C)
#define REG_DSI_RX_CAL_FAILED               (REG_DSI_RX_BASE + 0x20)
#define REG_DSI_RX_MSK_CAL_DONE             (REG_DSI_RX_BASE + 0x24)
#define REG_DSI_RX_MSK_CAL_FAILED           (REG_DSI_RX_BASE + 0x28)
#define REG_DSI_RX_PHY_RX_TRIGGERS          (REG_DSI_RX_BASE + 0x34)
#define REG_DSI_RX_LINE_START_DELAY         (REG_DSI_RX_BASE + 0x38)
#define REG_DSI_RX_VIDEO_VBLK_LINES         (REG_DSI_RX_BASE + 0x3C)
#define REG_DSI_RX_VIDEO_VFP_LINES          (REG_DSI_RX_BASE + 0x40)
#define REG_DSI_RX_VIDEO_VACTIVE_LINES      (REG_DSI_RX_BASE + 0x44)
#define REG_DSI_RX_VIDEO_LINE_TIME          (REG_DSI_RX_BASE + 0x48)
#define REG_DSI_RX_VIDEO_LINE_HBLK_TIME     (REG_DSI_RX_BASE + 0x4C)
#define REG_DSI_RX_VIDEO_LINE_HFP_TIME      (REG_DSI_RX_BASE + 0x50)
#define REG_DSI_RX_SOFT_RESET               (REG_DSI_RX_BASE + 0x54)
#define REG_DSI_RX_EOTP_EN                  (REG_DSI_RX_BASE + 0x58)
#define REG_DSI_RX_INT_STS                  (REG_DSI_RX_BASE + 0x5C)
#define REG_DSI_RX_INT_STS_MSK              (REG_DSI_RX_BASE + 0x60)
#define REG_DSI_RX_INT_STS_CLR              (REG_DSI_RX_BASE + 0x64)
#define REG_DSI_RX_CAL_DONE_INT_CLR         (REG_DSI_RX_BASE + 0x68)
#define REG_DSI_RX_CAL_FAILED_INT_CLR       (REG_DSI_RX_BASE + 0x6C)
#define REG_DSI_RX_DSI_ALL_IDLE             (REG_DSI_RX_BASE + 0xA0)

/* dsi tx registers */
#define REG_DSI_TX_BASE                         0x9400
#define REG_DSI_TX_DSI_VERSION                  (REG_DSI_TX_BASE + 0x00)
#define REG_DSI_TX_SOFT_RESET                   (REG_DSI_TX_BASE + 0x04)
#define REG_DSI_TX_PROTOCOL_INT_STS             (REG_DSI_TX_BASE + 0x08)
#define REG_DSI_TX_MASK_PROTOCOL_INT            (REG_DSI_TX_BASE + 0x0C)
#define REG_DSI_TX_INTERNAL_INT_STS             (REG_DSI_TX_BASE + 0x10)
#define REG_DSI_TX_MASK_INTERNAL_INT            (REG_DSI_TX_BASE + 0x14)
#define REG_DSI_TX_DSI_MODE_CFG                 (REG_DSI_TX_BASE + 0x18)
#define REG_DSI_TX_VIRTUAL_CHANNEL_ID           (REG_DSI_TX_BASE + 0x1C)
#define REG_DSI_TX_DPI_VIDEO_FORMAT             (REG_DSI_TX_BASE + 0x20)
#define REG_DSI_TX_VIDEO_PKT_CONFIG             (REG_DSI_TX_BASE + 0x24)
#define REG_DSI_TX_VIDEO_LINE_HBLK_TIME         (REG_DSI_TX_BASE + 0x28)
#define REG_DSI_TX_VIDEO_LINE_TIME              (REG_DSI_TX_BASE + 0x2C)
#define REG_DSI_TX_VIDEO_VBLK_LINES             (REG_DSI_TX_BASE + 0x30)
#define REG_DSI_TX_VIDEO_VACTIVE_LINES          (REG_DSI_TX_BASE + 0x34)
#define REG_DSI_TX_VID_MODE_CFG                 (REG_DSI_TX_BASE + 0x38)
#define REG_DSI_TX_SDF_MODE_CONFIG              (REG_DSI_TX_BASE + 0x3C)
#define REG_DSI_TX_TIMEOUT_CNT_CLK_CONFIG       (REG_DSI_TX_BASE + 0x40)
#define REG_DSI_TX_HTX_TO_CONFIG                (REG_DSI_TX_BASE + 0x44)
#define REG_DSI_TX_LRX_H_TO_CONFIG              (REG_DSI_TX_BASE + 0x48)
#define REG_DSI_TX_RD_PRESP_TO_CONFIG           (REG_DSI_TX_BASE + 0x4C)
#define REG_DSI_TX_HSWR_PRESP_TO_CONFIG         (REG_DSI_TX_BASE + 0x50)
#define REG_DSI_TX_LPWR_PRESP_TO_CONFIG         (REG_DSI_TX_BASE + 0x54)
#define REG_DSI_TX_BTA_PRESP_TO_CONFIG          (REG_DSI_TX_BASE + 0x58)
#define REG_DSI_TX_TX_ESC_CLK_CONFIG            (REG_DSI_TX_BASE + 0x5C)
#define REG_DSI_TX_VACT_CMD_TRANS_LIMIT         (REG_DSI_TX_BASE + 0x60)
#define REG_DSI_TX_VBLK_CMD_TRANS_LIMIT         (REG_DSI_TX_BASE + 0x64)
#define REG_DSI_TX_CMD_MODE_CFG                 (REG_DSI_TX_BASE + 0x68)
#define REG_DSI_TX_GEN_HDR                      (REG_DSI_TX_BASE + 0x6C)
#define REG_DSI_TX_GEN_PLD_DATA                 (REG_DSI_TX_BASE + 0x70)
#define REG_DSI_TX_PHY_CLK_LANE_LP_CTRL         (REG_DSI_TX_BASE + 0x74)
#define REG_DSI_TX_PHY_INTERFACE_CTRL           (REG_DSI_TX_BASE + 0x78)
#define REG_DSI_TX_PHY_TX_TRIGGERS              (REG_DSI_TX_BASE + 0x7C)
#define REG_DSI_TX_DESKEW_START                 (REG_DSI_TX_BASE + 0x80)
#define REG_DSI_TX_DESKEW_MODE                  (REG_DSI_TX_BASE + 0x84)
#define REG_DSI_TX_DESKEW_TIME                  (REG_DSI_TX_BASE + 0x88)
#define REG_DSI_TX_DESKEW_PERIOD                (REG_DSI_TX_BASE + 0x8C)
#define REG_DSI_TX_DESKEW_BUSY                  (REG_DSI_TX_BASE + 0x90)
#define REG_DSI_TX_DESKEW_LANE_MASK             (REG_DSI_TX_BASE + 0x94)
#define REG_DSI_TX_CMD_MODE_STATUS              (REG_DSI_TX_BASE + 0x98)
#define REG_DSI_TX_PHY_STATUS                   (REG_DSI_TX_BASE + 0x9C)
#define REG_DSI_TX_PHY_MIN_STOP_TIME            (REG_DSI_TX_BASE + 0xA0)
#define REG_DSI_TX_PHY_LANE_NUM_CONFIG          (REG_DSI_TX_BASE + 0xA4)
#define REG_DSI_TX_PHY_CLKLANE_TIME_CONFIG      (REG_DSI_TX_BASE + 0xA8)
#define REG_DSI_TX_PHY_DATALANE_TIME_CONFIG     (REG_DSI_TX_BASE + 0xAC)
#define REG_DSI_TX_MAX_READ_TIME                (REG_DSI_TX_BASE + 0xB0)
#define REG_DSI_TX_RX_PKT_CHECK_CONFIG          (REG_DSI_TX_BASE + 0xB4)
#define REG_DSI_TX_TA_EN                        (REG_DSI_TX_BASE + 0xB8)
#define REG_DSI_TX_EOTP_EN                      (REG_DSI_TX_BASE + 0xBC)
#define REG_DSI_TX_VIDEO_NULLPKT_SIZE           (REG_DSI_TX_BASE + 0xC0)
#define REG_DSI_TX_DCS_WM_PKT_SIZE              (REG_DSI_TX_BASE + 0xC4)
#define REG_DSI_TX_PROTOCOL_INT_CLR             (REG_DSI_TX_BASE + 0xC8)
#define REG_DSI_TX_INTERNAL_INT_CLR             (REG_DSI_TX_BASE + 0xCC)
#define REG_DSI_TX_VIDEO_SIG_DELAY_CONFIG       (REG_DSI_TX_BASE + 0xD0)
#define REG_DSI_TX_DPI_VFP                      (REG_DSI_TX_BASE + 0x120)
#define REG_DSI_TX_INT_PLL_STS                  (REG_DSI_TX_BASE + 0x200)
#define REG_DSI_TX_INT_PLL_MSK                  (REG_DSI_TX_BASE + 0x204)
#define REG_DSI_TX_INT_PLL_CLR                  (REG_DSI_TX_BASE + 0x208)

#define BYTE_PER_PIXEL_RGB888 3

void umb9230s_dsi_rx_vrr_timing(struct umb9230s_device *umb9230s)
{
    struct videomode *vm = &umb9230s->dsi_ctx.vm;
    struct dsi_rx_reg reg = {};
    u16 hline;
    u32 buf[8];

     /* line start delay(0x38) */
    hline = vm->hactive + vm->hsync_len + vm->hfront_porch + vm->hback_porch;
    reg.LINE_START_DELAY.bits.line_start_delay = 30 * hline / 100;
    reg.LINE_START_DELAY.bits.delay_en = 1;

    /* vbp vsync(0x3C) vfp(0x40) vactive(0x44) */
    reg.VIDEO_VBLK_LINES.bits.vbp_lines = vm->vback_porch;
    reg.VIDEO_VBLK_LINES.bits.vsa_lines = vm->vsync_len;
    reg.VIDEO_VFP_LINES.bits.vfp_lines = vm->vfront_porch;
    reg.VIDEO_VACTIVE_LINES.bits.vactive_lines = vm->vactive;

    /* hactive(0x48) hsync hbp(0x4C) hfp(0x50) */
    reg.VIDEO_LINE_TIME.bits.video_line_hact_time = vm->hactive;
    reg.VIDEO_LINE_HBLK_TIME.bits.video_line_hbp_time = vm->hback_porch;
    reg.VIDEO_LINE_HBLK_TIME.bits.video_line_hsa_time = vm->hsync_len;
    reg.VIDEO_LINE_HFP_TIME.bits.video_line_hfp_time = vm->hfront_porch;

    /* update reg 0x38-0x50 */
    buf[0] = REG_DSI_RX_LINE_START_DELAY;
    memcpy(&buf[1], &reg.LINE_START_DELAY.val, 7 * 4);
    iic2cmd_write(umb9230s->i2c_addr, buf, 8);
}

void umb9230s_dsi_rx_state_reset(struct umb9230s_device *umb9230s)
{
    u32 buf[2];

    if (!umb9230s)
        return;

    /* power_en */
    buf[0] = REG_DSI_RX_SOFT_RESET;
    buf[1] = 0;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    udelay(100);

    /* power_en */
    buf[0] = REG_DSI_RX_SOFT_RESET;
    buf[1] = 1;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);
}

void umb9230s_dsi_rx_init(struct umb9230s_device *umb9230s)
{
    struct videomode *vm = &umb9230s->dsi_ctx.vm;
    struct dsi_rx_reg reg = {};
    u16 hline;
    u32 buf[8];

    /* power_en */
    buf[0] = REG_DSI_RX_SOFT_RESET;
    buf[1] = 0;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* int_err_state mask */
    buf[0] = REG_DSI_RX_ERR_STATE_MSK;
    buf[1] = 0xffffffff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* int_cal_done(0x24) int_cal_failed mask(0x28) */
    buf[0] = REG_DSI_RX_MSK_CAL_DONE;
    buf[1] = 0xf;
    buf[2] = 0xf;
    iic2cmd_write(umb9230s->i2c_addr, buf, 3);

    buf[0] = REG_DSI_RX_INT_STS_MSK;
    buf[1] = 0xff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    buf[0] = REG_DSI_RX_EOTP_EN;
    /* eotp_rx_en(0x58) */
    buf[1] = 0;
    /* int_sts(RO) */
    buf[2] = 0;
    /* int_sts mask(0x60) */
    buf[3] = 0xffffffff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 4);

    /* line start delay(0x38) */
    hline = vm->hactive + vm->hsync_len + vm->hfront_porch + vm->hback_porch;
    reg.LINE_START_DELAY.bits.line_start_delay = 30 * hline / 100;
    reg.LINE_START_DELAY.bits.delay_en = 1;

    /* vsync vbp(0x3C) */
    reg.VIDEO_VBLK_LINES.bits.vbp_lines = vm->vback_porch;
    reg.VIDEO_VBLK_LINES.bits.vsa_lines = vm->vsync_len;
    /* vfp (0x40) */
    reg.VIDEO_VFP_LINES.bits.vfp_lines = vm->vfront_porch;
    /* vactive(0x44) */
    reg.VIDEO_VACTIVE_LINES.bits.vactive_lines = vm->vactive;

    /* hactive(0x48) */
    reg.VIDEO_LINE_TIME.bits.video_line_hact_time = vm->hactive;
    /* hsync hbp(0x4C) */
    reg.VIDEO_LINE_HBLK_TIME.bits.video_line_hbp_time = vm->hback_porch;
    reg.VIDEO_LINE_HBLK_TIME.bits.video_line_hsa_time = vm->hsync_len;
    /* hfp(0x50) */
    reg.VIDEO_LINE_HFP_TIME.bits.video_line_hfp_time = vm->hfront_porch;

    buf[0] = REG_DSI_RX_LINE_START_DELAY;
    memcpy(&buf[1], &reg.LINE_START_DELAY.val, 7 * 4);
    iic2cmd_write(umb9230s->i2c_addr, buf, 8);

    /* power_en */
    buf[0] = REG_DSI_RX_SOFT_RESET;
    buf[1] = 1;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);
}

static int umb9230s_dsi_tx_wait_tx_payload_fifo_empty(struct umb9230s_device *umb9230s)
{
    int timeout;
    union _0x98 cmd_mode_status;

    for (timeout = 0; timeout < 20000; timeout++) {
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_CMD_MODE_STATUS, &cmd_mode_status.val, 1);
        if (cmd_mode_status.bits.gen_cmd_wdata_fifo_empty)
            return 0;
        udelay(1);
    }

    pr_err("umb9230s tx payload fifo is not empty\n");
    return -ETIMEDOUT;
}

static int umb9230s_dsi_tx_wait_tx_cmd_fifo_empty(struct umb9230s_device *umb9230s)
{
    int timeout;
    union _0x98 cmd_mode_status;

    for (timeout = 0; timeout < 20000; timeout++) {
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_CMD_MODE_STATUS, &cmd_mode_status.val, 1);
        if (cmd_mode_status.bits.gen_cmd_cmd_fifo_empty)
            return 0;
        udelay(1);
    }

    pr_err("umb9230s tx cmd fifo is not empty\n");
    return -ETIMEDOUT;
}

static int umb9230s_dsi_tx_wait_rd_resp_completed(struct umb9230s_device *umb9230s)
{
    int timeout;
    union _0x98 cmd_mode_status;

    for (timeout = 0; timeout < 10000; timeout++) {
        udelay(10);
        /* is_bta_returned */
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_CMD_MODE_STATUS, &cmd_mode_status.val, 1);
        if (cmd_mode_status.bits.gen_cmd_rdcmd_done)
            return 0;
    }

    pr_err("umb9230s wait read response time out\n");
    return -ETIMEDOUT;
}

static u16 calc_bytes_per_pixel_x100(int coding)
{
    u16 Bpp_x100;

    switch (coding) {
    case COLOR_CODE_16BIT_CONFIG1:
    case COLOR_CODE_16BIT_CONFIG2:
    case COLOR_CODE_16BIT_CONFIG3:
        Bpp_x100 = 200;
        break;
    case COLOR_CODE_18BIT_CONFIG1:
    case COLOR_CODE_18BIT_CONFIG2:
        Bpp_x100 = 225;
        break;
    case COLOR_CODE_24BIT:
        Bpp_x100 = 300;
        break;
    case COLOR_CODE_COMPRESSTION:
        Bpp_x100 = 100;
        break;
    case COLOR_CODE_20BIT_YCC422_LOOSELY:
        Bpp_x100 = 250;
        break;
    case COLOR_CODE_24BIT_YCC422:
        Bpp_x100 = 300;
        break;
    case COLOR_CODE_16BIT_YCC422:
        Bpp_x100 = 200;
        break;
    case COLOR_CODE_30BIT:
        Bpp_x100 = 375;
        break;
    case COLOR_CODE_36BIT:
        Bpp_x100 = 450;
        break;
    case COLOR_CODE_12BIT_YCC420:
        Bpp_x100 = 150;
        break;
    default:
        pr_err("invalid color coding");
        Bpp_x100 = 0;
        break;
    }

    return Bpp_x100;
}

static u8 calc_video_size_step(int coding)
{
    u8 video_size_step;

    switch (coding) {
    case COLOR_CODE_16BIT_CONFIG1:
    case COLOR_CODE_16BIT_CONFIG2:
    case COLOR_CODE_16BIT_CONFIG3:
    case COLOR_CODE_18BIT_CONFIG1:
    case COLOR_CODE_18BIT_CONFIG2:
    case COLOR_CODE_24BIT:
    case COLOR_CODE_COMPRESSTION:
        return video_size_step = 1;
    case COLOR_CODE_20BIT_YCC422_LOOSELY:
    case COLOR_CODE_24BIT_YCC422:
    case COLOR_CODE_16BIT_YCC422:
    case COLOR_CODE_30BIT:
    case COLOR_CODE_36BIT:
    case COLOR_CODE_12BIT_YCC420:
        return video_size_step = 2;
    default:
        pr_err("invalid color coding");
        return 0;
    }
}

static u16 round_video_size(int coding, u16 video_size)
{
    switch (coding) {
    case COLOR_CODE_16BIT_YCC422:
    case COLOR_CODE_24BIT_YCC422:
    case COLOR_CODE_20BIT_YCC422_LOOSELY:
    case COLOR_CODE_12BIT_YCC420:
        /* round up active H pixels to a multiple of 2 */
        if ((video_size % 2) != 0)
            video_size += 1;
        break;
    default:
        break;
    }

    return video_size;
}

#define SPRD_MIPI_DSI_FMT_DSC 0xff
static u32 fmt_to_coding(u32 fmt)
{
    switch (fmt) {
    case MIPI_DSI_FMT_RGB565:
        return COLOR_CODE_16BIT_CONFIG1;
    case MIPI_DSI_FMT_RGB666_PACKED:
        return COLOR_CODE_18BIT_CONFIG1;
    case MIPI_DSI_FMT_RGB666:
    case MIPI_DSI_FMT_RGB888:
        return COLOR_CODE_24BIT;
    case SPRD_MIPI_DSI_FMT_DSC:
        return COLOR_CODE_COMPRESSTION;
    default:
        pr_err("Unsupported format (%d)\n", fmt);
        return COLOR_CODE_24BIT;
    }
}

#define ns_to_cycle(ns, byte_clk) \
    DIV_ROUND_UP((ns) * (byte_clk), 1000000)

static void umb9230s_dsi_tx_set_packet_header(struct umb9230s_device *umb9230s,
                   u8 vc,
                   u8 type,
                   u8 wc_lsb,
                   u8 wc_msb)
{
    union _0x6C gen_hdr;
    u32 buf[2];

    gen_hdr.bits.gen_dt = type;
    gen_hdr.bits.gen_vc = vc;
    gen_hdr.bits.gen_wc_lsbyte = wc_lsb;
    gen_hdr.bits.gen_wc_msbyte = wc_msb;

    buf[0] = REG_DSI_TX_GEN_HDR;
    buf[1] = gen_hdr.val;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);
}

int umb9230s_dsi_tx_wr_pkt(struct umb9230s_device *umb9230s, u8 vc, u8 type,
            const u8 *param, u16 len)
{
    u32 payload;
    u8 wc_lsbyte, wc_msbyte;
    int i, j, ret;
    u32 buf[2];

    if (vc > 3)
        return -EINVAL;

    /* 1st: for long packet, must config payload first */
    ret = umb9230s_dsi_tx_wait_tx_payload_fifo_empty(umb9230s);
    if (ret)
        return ret;

    if (len > 2) {
        for (i = 0; i < len; i += j) {
            payload = 0;
            for (j = 0; (j < 4) && ((j + i) < (len)); j++)
                payload |= param[i + j] << (j * 8);

            /* set_packet_payload */
            buf[0] = REG_DSI_TX_GEN_PLD_DATA;
            buf[1] = payload;
            iic2cmd_write(umb9230s->i2c_addr, buf, 2);
        }
        wc_lsbyte = len & 0xff;
        wc_msbyte = len >> 8;
    } else {
        wc_lsbyte = (len > 0) ? param[0] : 0;
        wc_msbyte = (len > 1) ? param[1] : 0;
    }

    /* 2nd: then set packet header */
    ret = umb9230s_dsi_tx_wait_tx_cmd_fifo_empty(umb9230s);
    if (ret)
        return ret;

    umb9230s_dsi_tx_set_packet_header(umb9230s, vc, type, wc_lsbyte, wc_msbyte);

    return 0;
}

int umb9230s_dsi_tx_rd_pkt(struct umb9230s_device *umb9230s, u8 vc, u8 type,
            u8 msb_byte, u8 lsb_byte,
            u8 *buffer, u8 bytes_to_read)
{
    int i, ret;
    int count = 0;
    u32 temp;
    union _0x98 cmd_mode_status;

    if (vc > 3)
        return -EINVAL;

    /* 1st: send read command to peripheral */
    ret = umb9230s_dsi_tx_wait_tx_cmd_fifo_empty(umb9230s);
    if (ret)
        return ret;

    umb9230s_dsi_tx_set_packet_header(umb9230s, vc, type, lsb_byte, msb_byte);

    /* 2nd: wait peripheral response completed */
    umb9230s_dsi_tx_wait_rd_resp_completed(umb9230s);

    /* 3rd: get data from rx payload fifo */
    /* is_rx_payload_fifo_empty */
    iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_CMD_MODE_STATUS, &cmd_mode_status.val, 1);
    if (cmd_mode_status.bits.gen_cmd_rdata_fifo_empty) {
        pr_err("umb9230s rx payload fifo empty\n");
        return -EINVAL;
    }

    for (i = 0; i < 100; i++) {
        /* get_rx_payload */
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_GEN_PLD_DATA, &temp, 1);

        if (count < bytes_to_read)
            buffer[count++] = temp & 0xff;
        if (count < bytes_to_read)
            buffer[count++] = (temp >> 8) & 0xff;
        if (count < bytes_to_read)
            buffer[count++] = (temp >> 16) & 0xff;
        if (count < bytes_to_read)
            buffer[count++] = (temp >> 24) & 0xff;

        /* is_rx_payload_fifo_empty */
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_CMD_MODE_STATUS, &cmd_mode_status.val, 1);
        if (cmd_mode_status.bits.gen_cmd_rdata_fifo_empty)
            return count;
    }

    pr_err("umb9230s read too many buffers\n");
    return -EINVAL;
}

void umb9230s_dsi_tx_lp_cmd_enable(struct umb9230s_device *umb9230s, bool enable)
{
    union _0x18 dsi_mode_cfg;
    union _0x68 cmd_mode_cfg;
    union _0x38 video_mode_cfg;
    u32 buf[2];

    if (!umb9230s)
        return;

    /* is_cmd_mode */
    iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_DSI_MODE_CFG, &dsi_mode_cfg.val, 1);
    if (dsi_mode_cfg.bits.cmd_video_mode) {
        /* cmd_mode_lp_cmd_en */
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_CMD_MODE_CFG, &cmd_mode_cfg.val, 1);
        cmd_mode_cfg.bits.gen_sw_0p_tx = enable;
        cmd_mode_cfg.bits.gen_sw_1p_tx = enable;
        cmd_mode_cfg.bits.gen_sw_2p_tx = enable;
        cmd_mode_cfg.bits.gen_lw_tx = enable;
        cmd_mode_cfg.bits.dcs_sw_0p_tx = enable;
        cmd_mode_cfg.bits.dcs_sw_1p_tx = enable;
        cmd_mode_cfg.bits.dcs_lw_tx = enable;
        cmd_mode_cfg.bits.max_rd_pkt_size = enable;

        cmd_mode_cfg.bits.gen_sr_0p_tx = enable;
        cmd_mode_cfg.bits.gen_sr_1p_tx = enable;
        cmd_mode_cfg.bits.gen_sr_2p_tx = enable;
        cmd_mode_cfg.bits.dcs_sr_0p_tx = enable;

        buf[0] = REG_DSI_TX_CMD_MODE_CFG;
        buf[1] = cmd_mode_cfg.val;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);
    } else {
        /* video_mode_lp_cmd_en */
        iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_VID_MODE_CFG, &video_mode_cfg.val, 1);
        video_mode_cfg.bits.lp_cmd_en = enable;

        buf[0] = REG_DSI_TX_VID_MODE_CFG;
        buf[1] = video_mode_cfg.val;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);
    }
}

void umb9230s_dsi_tx_set_work_mode(struct umb9230s_device *umb9230s, u8 mode)
{
    u32 buf[2];

    if (!umb9230s)
        return;

    if (mode == DSI_MODE_CMD)
        /* cmd_mode */
        buf[1] = 1;
    else
        /* video_mode */
        buf[1] = 2;

    buf[0] = REG_DSI_TX_DSI_MODE_CFG;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);
}

void umb9230s_dsi_tx_state_reset(struct umb9230s_device *umb9230s)
{
    u32 buf[2];

    if (!umb9230s)
        return;

    /* power_en */
    buf[0] = REG_DSI_TX_SOFT_RESET;
    buf[1] = 0;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    udelay(100);

    /* power_en */
    buf[0] = REG_DSI_TX_SOFT_RESET;
    buf[1] = 1;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);
}

void umb9230s_dsi_tx_vrr_timing(struct umb9230s_device *umb9230s)
{
    u16 Bpp_x100;
    u32 ratio_x1000;
    u16 hline;
    u8 coding;
    struct dsi_tx_context *ctx = &umb9230s->dsi_ctx;
    struct videomode *vm = &umb9230s->dsi_ctx.vm;
    union _0xD0 video_sig_delay_config;
    u32 buf[5];
    struct dsi_reg reg = {};

    coding = fmt_to_coding(ctx->format);
    Bpp_x100 = calc_bytes_per_pixel_x100(coding);
    ratio_x1000 = ctx->byte_clk * 1000 / (vm->pixelclock / 1000);
    hline = vm->hactive + vm->hsync_len + vm->hfront_porch +
        vm->hback_porch;

    /* dpi_sig_delay */
    iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_VIDEO_SIG_DELAY_CONFIG, &video_sig_delay_config.val, 1);

    video_sig_delay_config.bits.video_sig_delay = 95 * hline * ratio_x1000 / 100000;
    buf[0] = REG_DSI_TX_VIDEO_SIG_DELAY_CONFIG;
    buf[1] = video_sig_delay_config.val;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* dpi_hline_time (0x2C) */
    reg.VIDEO_LINE_TIME.bits.video_line_time = hline * ratio_x1000 / 1000;

    /* dpi_hbp_time dpi_hsync_time(0x28) */
    reg.VIDEO_LINE_HBLK_TIME.bits.video_line_hbp_time =
                                        vm->hback_porch * ratio_x1000 / 1000;
    reg.VIDEO_LINE_HBLK_TIME.bits.video_line_hsa_time =
                                        vm->hsync_len * ratio_x1000 / 1000;

    /* dpi_vact (0x34) */
    reg.VIDEO_VACTIVE_LINES.bits.vactive_lines = vm->vactive;

    /* dpi_vsync dpi_vbp (0x30) */
    reg.VIDEO_VBLK_LINES.bits.vsa_lines = vm->vsync_len;
    reg.VIDEO_VBLK_LINES.bits.vbp_lines = vm->vback_porch;

    buf[0] = REG_DSI_TX_VIDEO_LINE_HBLK_TIME;
    memcpy(&buf[1], &reg.VIDEO_LINE_HBLK_TIME.val, 4 * 4);
    /* update reg: 0x28-0x34 */
    iic2cmd_write(umb9230s->i2c_addr, buf, 5);

     /* dpi_vfp (0x120) */
    buf[0] = REG_DSI_TX_DPI_VFP;
    buf[1] = vm->vfront_porch;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);
}

static void umb9230s_dsi_tx_init(struct umb9230s_device *umb9230s)
{
    struct dsi_tx_context *ctx = &umb9230s->dsi_ctx;
    u64 max_rd_time;
    u16 data_hs2lp, data_lp2hs, clk_hs2lp, clk_lp2hs;
    u32 buf[7];
    struct dsi_reg reg = {};

    /* power_en */
    buf[0] = REG_DSI_TX_SOFT_RESET;
    buf[1] = 0;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* int0_mask(0x0c), int1_sts(RO), int1_mask(0x14) */
    reg.MASK_PROTOCOL_INT.val = 0xffffffff;
    reg.MASK_INTERNAL_INT.val = 0xffffffff;
    /* cmd_mode(0x18) */
    reg.DSI_MODE_CFG.bits.cmd_video_mode = 1;
    /* video_vcid, rx vcid(0x1c) */
    reg.VIRTUAL_CHANNEL_ID.bits.video_pkt_vcid = 0;
    reg.VIRTUAL_CHANNEL_ID.bits.gen_rx_vcid = 0;
    /* update reg 0x0c-0x1c */
    buf[0] = REG_DSI_TX_MASK_PROTOCOL_INT;
    memcpy(&buf[1], &reg.MASK_PROTOCOL_INT.val, 5 * 4);
    iic2cmd_write(umb9230s->i2c_addr, buf, 6);

    /* tx_escape_division */
    buf[0] = REG_DSI_TX_TX_ESC_CLK_CONFIG;
    buf[1] = DIV_ROUND_UP(ctx->byte_clk, ctx->esc_clk);
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);
    pr_info("umb9230s escape clock divider = %d\n", buf[1]);

    /* eotp_rx_en, eotp_tx_en(0xbc) */
    reg.EOTP_EN.bits.rx_eotp_en = 0;
    reg.EOTP_EN.bits.tx_eotp_en = 0;
    /* ecc_rx_en, crc_rx_en(0xb4) */
    reg.RX_PKT_CHECK_CONFIG.bits.rx_pkt_ecc_en = 1;
    reg.RX_PKT_CHECK_CONFIG.bits.rx_pkt_crc_en = 1;
    /* bta_en(0xb8) */
    reg.TA_EN.val = 1;

    /* max_read_time(0xB0) */
    max_rd_time = ctx->max_rd_time * ctx->byte_clk;
    do_div(max_rd_time, 1000000);
    reg.MAX_READ_TIME.val = max_rd_time;

    data_hs2lp = ns_to_cycle(ctx->data_hs2lp, ctx->byte_clk);
    data_lp2hs = ns_to_cycle(ctx->data_lp2hs, ctx->byte_clk);
    clk_hs2lp = ns_to_cycle(ctx->clk_hs2lp, ctx->byte_clk);
    clk_lp2hs = ns_to_cycle(ctx->clk_lp2hs, ctx->byte_clk);

    /*datalane_hs2lp_config datalane_lp2hs_config(0xAC) */
    reg.PHY_DATALANE_TIME_CONFIG.bits.phy_datalane_hs_to_lp_time = data_hs2lp;
    reg.PHY_DATALANE_TIME_CONFIG.bits.phy_datalane_lp_to_hs_time = data_lp2hs;

    /* clklane_hs2lp_config clklane_lp2hs_config(0xA8) */
    reg.PHY_CLKLANE_TIME_CONFIG.bits.phy_clklane_hs_to_lp_time = clk_hs2lp;
    reg.PHY_CLKLANE_TIME_CONFIG.bits.phy_clklane_lp_to_hs_time = clk_lp2hs;

    /* update reg 0xA8-0xBC */
    buf[0] = REG_DSI_TX_PHY_CLKLANE_TIME_CONFIG;
    memcpy(&buf[1], &reg.PHY_CLKLANE_TIME_CONFIG.val, 6 * 4);
    iic2cmd_write(umb9230s->i2c_addr, buf, 7);

    /* power_en */
    buf[0] = REG_DSI_TX_SOFT_RESET;
    buf[1] = 1;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);
}

static int umb9230s_dsi_tx_dpi_video(struct umb9230s_device *umb9230s)
{
    u16 Bpp_x100;
    u16 video_size;
    u32 ratio_x1000;
    u16 null_pkt_size = 0;
    u8 video_size_step;
    u32 hs_to;
    u32 total_bytes;
    u32 bytes_per_chunk;
    u32 chunks = 0;
    u32 bytes_left = 0;
    u32 chunk_overhead;
    const u8 pkt_header = 6;
    u8 coding;
    int div;
    u16 hline;
    struct dsi_tx_context *ctx = &umb9230s->dsi_ctx;
    struct videomode *vm = &umb9230s->dsi_ctx.vm;
    struct dsi_reg reg = {};
    u32 buf[6];

    coding = fmt_to_coding(ctx->format);
    video_size = round_video_size(coding, vm->hactive);
    Bpp_x100 = calc_bytes_per_pixel_x100(coding);
    video_size_step = calc_video_size_step(coding);
    ratio_x1000 = ctx->byte_clk * 1000 / (vm->pixelclock / 1000);
    hline = vm->hactive + vm->hsync_len + vm->hfront_porch +
        vm->hback_porch;

    /* power_en */
    buf[0] = REG_DSI_TX_SOFT_RESET;
    buf[1] = 0;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* dpi_color_coding */
    iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_DPI_VIDEO_FORMAT, &reg.DPI_VIDEO_FORMAT.val, 1);
    reg.DPI_VIDEO_FORMAT.bits.dpi_video_mode_format = coding;
    buf[0] = REG_DSI_TX_DPI_VIDEO_FORMAT;
    buf[1] = reg.DPI_VIDEO_FORMAT.val;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* dpi_sig_delay(0xD0) */
    iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_VIDEO_SIG_DELAY_CONFIG, &reg.VIDEO_SIG_DELAY_CONFIG.val, 1);
    reg.VIDEO_SIG_DELAY_CONFIG.bits.video_sig_delay = 95 * hline * ratio_x1000 / 100000;
    buf[0] = REG_DSI_TX_VIDEO_SIG_DELAY_CONFIG;
    buf[1] = reg.VIDEO_SIG_DELAY_CONFIG.val;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* vblk_cmd_trans_limit(0x64) */
    buf[0] = REG_DSI_TX_VBLK_CMD_TRANS_LIMIT;
    buf[1] = 0x80;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_VID_MODE_CFG, &reg.VID_MODE_CFG.val, 1);
    /* dpi_frame_ack_en(0x38) */
    reg.VID_MODE_CFG.bits.frame_bta_ack_en = ctx->frame_ack_en;
    /* dpi_video_burst_mode(0x38) */
    reg.VID_MODE_CFG.bits.vid_mode_type = ctx->burst_mode;

    /* dpi_hline_time(0x2C) */
    reg.VIDEO_LINE_TIME.bits.video_line_time = hline * ratio_x1000 / 1000;

    /* dpi_hsync_time dpi_hbp_time(0x28) */
    reg.VIDEO_LINE_HBLK_TIME.bits.video_line_hbp_time =
                                        vm->hback_porch * ratio_x1000 / 1000;
    reg.VIDEO_LINE_HBLK_TIME.bits.video_line_hsa_time =
                                        vm->hsync_len * ratio_x1000 / 1000;

    /* dpi_vact(0x34) */
    reg.VIDEO_VACTIVE_LINES.bits.vactive_lines = vm->vactive;

    /*dpi_vbp dpi_vsync(0x30) */
    reg.VIDEO_VBLK_LINES.bits.vsa_lines = vm->vsync_len;
    reg.VIDEO_VBLK_LINES.bits.vbp_lines = vm->vback_porch;

    if (!ctx->hporch_lp_disable) {
        /* dpi_hporch_lp_en(0x38) */
        reg.VID_MODE_CFG.bits.lp_hfp_en = 1;
        reg.VID_MODE_CFG.bits.lp_hbp_en = 1;
    }

    /* dpi_vporch_lp_en(0x38) */
    reg.VID_MODE_CFG.bits.lp_vact_en = 1;
    reg.VID_MODE_CFG.bits.lp_vfp_en = 1;
    reg.VID_MODE_CFG.bits.lp_vbp_en = 1;
    reg.VID_MODE_CFG.bits.lp_vsa_en = 1;

    /* update reg 0x28-0x38 */
    buf[0] = REG_DSI_TX_VIDEO_LINE_HBLK_TIME;
    memcpy(&buf[1], &reg.VIDEO_LINE_HBLK_TIME.val, 5 * 4);
    iic2cmd_write(umb9230s->i2c_addr, buf, 6);

    /* dpi_vfp(0x120) */
    buf[0] = REG_DSI_TX_DPI_VFP;
    buf[1] = vm->vfront_porch;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    hs_to = (hline * vm->vactive) + (2 * Bpp_x100) / 100;
    for (div = 0x80; (div < hs_to) && (div > 2); div--) {
        if ((hs_to % div) == 0) {
            buf[0] = REG_DSI_TX_TIMEOUT_CNT_CLK_CONFIG;
            /* timeout_clock_division(0x40) */
            buf[1] = div;
            /* hs_tx_timeout(0x44) */
            buf[2] = hs_to / div;
            /* lp_rx_timeout(0x48) */
            buf[3] = hs_to / div;
            /* update reg 0x40-0x48 */
            iic2cmd_write(umb9230s->i2c_addr, buf, 4);
            break;
        }
    }

    if (ctx->burst_mode == VIDEO_BURST_WITH_SYNC_PULSES) {
        /* dpi_video_packet_size(0x24) */
        reg.VIDEO_PKT_CONFIG.bits.video_pkt_size = video_size;
        /* dpi_chunk_num(0x24) */
        reg.VIDEO_PKT_CONFIG.bits.video_line_chunk_num = 0;
        buf[0] = REG_DSI_TX_VIDEO_PKT_CONFIG;
        buf[1] = reg.VIDEO_PKT_CONFIG.val;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        /* dpi_null_packet_size(0xC0) */
        buf[0] = REG_DSI_TX_VIDEO_NULLPKT_SIZE;
        buf[1] = 0;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);
    } else {
        /* non burst transmission */
        null_pkt_size = 0;

        /* bytes to be sent - first as one chunk */
        bytes_per_chunk = vm->hactive * Bpp_x100 / 100 + pkt_header;

        /* hline total bytes from the DPI interface */
        total_bytes = (vm->hactive + vm->hfront_porch) *
                ratio_x1000 / ctx->lanes / 1000;

        /* check if the pixels actually fit on the DSI link */
        if (total_bytes < bytes_per_chunk) {
            pr_err("current resolution can not be set\n");
            return -EINVAL;
        }

        chunk_overhead = total_bytes - bytes_per_chunk;

        /* overhead higher than 1 -> enable multi packets */
        if (chunk_overhead > 1) {

            /* multi packets */
            for (video_size = video_size_step;
                 video_size < vm->hactive;
                 video_size += video_size_step) {

                if (vm->hactive * 1000 / video_size % 1000)
                    continue;

                chunks = vm->hactive / video_size;
                bytes_per_chunk = Bpp_x100 * video_size / 100
                          + pkt_header;
                if (total_bytes >= (bytes_per_chunk * chunks)) {
                    bytes_left = total_bytes -
                             bytes_per_chunk * chunks;
                    break;
                }
            }

            /* prevent overflow (unsigned - unsigned) */
            if (bytes_left > (pkt_header * chunks)) {
                null_pkt_size = (bytes_left -
                        pkt_header * chunks) / chunks;
                /* avoid register overflow */
                if (null_pkt_size > 1023)
                    null_pkt_size = 1023;
            }

        } else {

            /* single packet */
            chunks = 1;

            /* must be a multiple of 4 except 18 loosely */
            for (video_size = vm->hactive;
                (video_size % video_size_step) != 0;
                 video_size++)
                ;
        }
        /* dpi_video_packet_size(0x24) */
        reg.VIDEO_PKT_CONFIG.bits.video_pkt_size = video_size;
        /* dpi_chunk_num(0x24) */
        reg.VIDEO_PKT_CONFIG.bits.video_line_chunk_num = chunks;
        buf[0] = REG_DSI_TX_VIDEO_PKT_CONFIG;
        buf[1] = reg.VIDEO_PKT_CONFIG.val;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);

        /* dpi_null_packet_size(0xC0) */
        buf[0] = REG_DSI_TX_VIDEO_NULLPKT_SIZE;
        buf[1] = null_pkt_size;
        iic2cmd_write(umb9230s->i2c_addr, buf, 2);
    }

    /* int0_mask(0xC), int1_sts(0x10), int1_mask(0x14) */
    buf[0] = REG_DSI_TX_MASK_PROTOCOL_INT;
    buf[1] = ctx->int0_mask;
    buf[2] = 0x0;
    buf[3] = ctx->int1_mask;
    iic2cmd_write(umb9230s->i2c_addr, buf, 4);

    /* power_en */
    buf[0] = REG_DSI_TX_SOFT_RESET;
    buf[1] = 1;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    return 0;
}

static void umb9230s_dsi_tx_edpi_video(struct umb9230s_device *umb9230s)
{
    const u32 fifo_depth = 1096;
    const u32 word_length = 4;
    struct dsi_tx_context *ctx = &umb9230s->dsi_ctx;
    u32 hactive = ctx->vm.hactive;
    u32 Bpp_x100;
    u32 max_fifo_len;
    u32 cur_pkt_len, dcs_wm_pkt_size;
    u8 coding;
    int i, remainder;
    bool find_pkt_size = false;
    u32 buf[4];
    struct dsi_reg reg = {};

    coding = fmt_to_coding(ctx->format);
    Bpp_x100 = calc_bytes_per_pixel_x100(coding);
    max_fifo_len = word_length * fifo_depth * 100 / Bpp_x100;

    /* power_en */
    buf[0] = REG_DSI_TX_SOFT_RESET;
    buf[1] = 0;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* dpi_color_coding */
    iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_DPI_VIDEO_FORMAT, &reg.DPI_VIDEO_FORMAT.val, 1);
    reg.DPI_VIDEO_FORMAT.bits.dpi_video_mode_format = coding;
    buf[0] = REG_DSI_TX_DPI_VIDEO_FORMAT;
    buf[1] = reg.DPI_VIDEO_FORMAT.val;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* tear_effect_ack_en */
    iic2cmd_read(umb9230s->i2c_addr, REG_DSI_TX_CMD_MODE_CFG, &reg.CMD_MODE_CFG.val, 1);
    reg.CMD_MODE_CFG.bits.tear_fx_en = ctx->te_ack_en;
    buf[0] = REG_DSI_TX_CMD_MODE_CFG;
    buf[1] = reg.CMD_MODE_CFG.val;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    if (max_fifo_len > hactive)
        cur_pkt_len = hactive;
    else
        cur_pkt_len = max_fifo_len;

    for (i = 1; i <= cur_pkt_len; i++) {
        if (cur_pkt_len % i != 0)
            continue;

        dcs_wm_pkt_size = cur_pkt_len / i;
        remainder = (dcs_wm_pkt_size * BYTE_PER_PIXEL_RGB888 + 1) % 8;
        if (remainder == 0 || remainder > 4) {
            find_pkt_size = true;
            break;
        }
    }

     /* edpi_max_pkt_size */
    if ((i == cur_pkt_len && !find_pkt_size) || (ctx->format == SPRD_MIPI_DSI_FMT_DSC))
        buf[1] = cur_pkt_len;
    else
        buf[1] = dcs_wm_pkt_size;

    buf[0] = REG_DSI_TX_DCS_WM_PKT_SIZE;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);

    /* int0_mask(0x0C) int1_sts(0x10) int1_mask(0x14) */
    buf[0] = REG_DSI_TX_MASK_PROTOCOL_INT;
    buf[1] = ctx->int0_mask;
    buf[2] = 0x0;
    buf[3] = ctx->int1_mask;
    iic2cmd_write(umb9230s->i2c_addr, buf, 4);

    /* power_en */
    buf[0] = REG_DSI_TX_SOFT_RESET;
    buf[1] = 1;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);
}

void umb9230s_dsi_tx_configure(struct umb9230s_device *umb9230s)
{
    if (!umb9230s)
        return;

    umb9230s_dsi_tx_set_work_mode(umb9230s, umb9230s->dsi_ctx.work_mode);
    umb9230s_dsi_tx_state_reset(umb9230s);
    umb9230s_dsi_rx_state_reset(umb9230s);

    if ((umb9230s->dsi_ctx.work_mode == DSI_MODE_VIDEO) &&
            umb9230s->dsi_ctx.video_lp_cmd_en)
        umb9230s_dsi_tx_lp_cmd_enable(umb9230s, true);
}

void umb9230s_dsi_tx_enable(struct umb9230s_device *umb9230s)
{
    pr_info("work_mode:%d \n", umb9230s->dsi_ctx.work_mode);

    umb9230s_dsi_tx_init(umb9230s);

    if (umb9230s->dsi_ctx.work_mode == DSI_MODE_VIDEO)
        umb9230s_dsi_tx_dpi_video(umb9230s);
    else
        umb9230s_dsi_tx_edpi_video(umb9230s);
}

void umb9230s_dsi_tx_fini(struct umb9230s_device *umb9230s)
{
    u32 buf[4];

    /* int0_mask(0x0C) int1_sts(0x10) int1_mask(0x14) */
    buf[0] = REG_DSI_TX_MASK_PROTOCOL_INT;
    buf[1] = 0xffffffff;
    buf[2] = 0x0;
    buf[3] = 0xffffffff;
    iic2cmd_write(umb9230s->i2c_addr, buf, 4);

    /* power_en */
    buf[0] = REG_DSI_TX_SOFT_RESET;
    buf[1] = 0;
    iic2cmd_write(umb9230s->i2c_addr, buf, 2);
}
