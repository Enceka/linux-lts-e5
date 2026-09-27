// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */
#ifndef _UMB9230S_H_
#define _UMB9230S_H_

#include <video/videomode.h>
#include "../dsi/core/dsi_ctrl_r1p1.h"
#include "../sprd_dsc.h"
#include "i2c_api.h"

struct dsi_tx_context {
    struct videomode vm;
    u8 lanes;
    u32 format;
    u8 work_mode;
    u8 burst_mode;

    u32 int0_mask;
    u32 int1_mask;

    /* byte clock [KHz] */
    u32 byte_clk;
    /* escape clock [KHz] */
    u32 esc_clk;

    /* maximum time (ns) for data lanes from HS to LP */
    u16 data_hs2lp;
    /* maximum time (ns) for data lanes from LP to HS */
    u16 data_lp2hs;
    /* maximum time (ns) for clk lanes from HS to LP */
    u16 clk_hs2lp;
    /* maximum time (ns) for clk lanes from LP to HS */
    u16 clk_lp2hs;
    /* maximum time (ns) for BTA operation - REQUIRED */
    u64 max_rd_time;

    /* enable receiving frame ack packets - for video mode */
    bool frame_ack_en;
    /* enable receiving tear effect ack packets - for cmd mode */
    bool te_ack_en;
    /* enable low power cmd transsmit in video mode */
    bool video_lp_cmd_en;
    /* disable hporch enter in low power mode */
    bool hporch_lp_disable;
};

struct dphy_tx_context {
    u32 freq;
    u8 lanes;
    bool ulps_enable;
    u8 i2c_addr;
};

struct dphy_tx_pll_ops {
    int (*pll_config)(struct dphy_tx_context *ctx);
    int (*timing_config)(struct dphy_tx_context *ctx);
    int (*hop_config)(struct dphy_tx_context *ctx, int delta, int period);
    int (*ssc_en)(struct dphy_tx_context *ctx, bool en);
    void (*force_pll)(struct dphy_tx_context *ctx, int force);
};

struct video2cmd_reg {
    u32 mode;
    u32 size_x;
    u32 size_y;
};

struct dsi_rx_reg {
    union _dsi_rx_0x00 {
        u32 val;
        struct _DSI_RX_IP_VERSION {
        u32 ip_version: 16;
        u32 reserved: 16;
        } bits;
    } IP_VERSION;

    union _dsi_rx_0x04 {
        u32 val;
        struct _DSI_RX_PHY_PD_N {
        /*
         * Power down output. This line is used to place the
         * complete macro in power down. All analog blocks are
         * in power down mode and digital logic is cleared. Active
         * Low.
         */
        u32 phy_pd_n: 1;

        u32 reserved: 31;
        } bits;
    } PHY_PD_N;

    union _dsi_rx_0x08 {
        u32 val;
        struct _DSI_RX_RST_DPHY_N {
        /* DPHY reset output. Active Low. */
        u32 rst_dphy_n: 1;

        u32 reserved: 31;
        } bits;
    } RST_DPHY_N;

    union _dsi_rx_0x0C {
        u32 val;
        struct _DSI_RX_PHY_STATE {
        /* Data lane 0 is in stop state */
        u32 phy_stopstatedata0: 1;

        /* Data lane 1 is in stop state */
        u32 phy_stopstatedata1: 1;

        /* Data lane 2 is in stop state */
        u32 phy_stopstatedata2: 1;

        /* Data lane 3 is in stop state */
        u32 phy_stopstatedata3: 1;

        /* Lane module 0 is in Ultra Low Power mode */
        u32 phy_rxulpsesc0: 1;

        /* Lane module 1 is in Ultra Low Power mode */
        u32 phy_rxulpsesc1: 1;

        /* Lane module 2 is in Ultra Low Power mode */
        u32 phy_rxulpsesc2: 1;

        /* Lane module 3 is in Ultra Low Power mode */
        u32 phy_rxulpsesc3: 1;

        /* Clock lane is in stop state */
        u32 phy_stopstateclk: 1;

        /*
         * Indicates that the clock lane module is in Ultra
         * Low Power state. Active Low.
         */
        u32 phy_rxulpsclknot: 1;

        /* Indicates the clock lane is active */
        u32 phy_rxclkactivehs: 1;

        /* Lane module is in Low-Power data receive mode */
        u32 phy_rxlpdtesc: 1;

        u32 reserved: 20;
        } bits;
    } PHY_STATE;

    union _dsi_rx_0x10 {
        u32 val;
        struct _DSI_RX_ERR_STATE {
        /* Start of transmission error on data lane 0, no
         * synchronization achieved
         */
        u32 phy_errsotsynchs0: 1;

        /* Start of transmission error on data lane 0, no
         * synchronization achieved
         */
        u32 phy_errsotsynchs1: 1;

        /* Start of transmission error on data lane 0, no
         * synchronization achieved
         */
        u32 phy_errsotsynchs2: 1;

        /* Start of transmission error on data lane 0, no
         * synchronization achieved
         */
        u32 phy_errsotsynchs3: 1;

        /* Escape entry error (ULPM) on data lane 0 */
        u32 phy_erresc0: 1;

        /* Escape entry error (ULPM) on data lane 1 */
        u32 phy_erresc1: 1;

        /* Escape entry error (ULPM) on data lane 2 */
        u32 phy_erresc2: 1;

        /* Escape entry error (ULPM) on data lane 3 */
        u32 phy_erresc3: 1;

        /* Start of transmission error on data lane 0,
         * synchronization can still be achieved
         */
        u32 phy_errsoths0: 1;

        /* Start of transmission error on data lane 1,
         * synchronization can still be achieved
         */
        u32 phy_errsoths1: 1;

        /* Start of transmission error on data lane 2,
         * synchronization can still be achieved
         */
        u32 phy_errsoths2: 1;

        /* Start of transmission error on data lane 3,
         * synchronization can still be achieved
         */
        u32 phy_errsoths3: 1;

        u32 phy_errsyncesc0: 1;
        u32 phy_errsyncesc1: 1;
        u32 phy_errsyncesc2: 1;
        u32 phy_errsyncesc3: 1;
        u32 phy_errcontrol0: 1;
        u32 phy_errcontrol1: 1;
        u32 phy_errcontrol2: 1;
        u32 phy_errcontrol3: 1;
        u32 reserved: 12;
        } bits;
    } ERR_STATE;

    union _dsi_rx_0x14 {
        u32 val;
        struct _DSI_RX_ERR_STATE_MSK {
        u32 mask_phy_errsotsynchs0: 1;
        u32 mask_phy_errsotsynchs1: 1;
        u32 mask_phy_errsotsynchs2: 1;
        u32 mask_phy_errsotsynchs3: 1;
        u32 mask_phy_erresc0: 1;
        u32 mask_phy_erresc1: 1;
        u32 mask_phy_erresc2: 1;
        u32 mask_phy_erresc3: 1;
        u32 mask_phy_errsoths0: 1;
        u32 mask_phy_errsoths1: 1;
        u32 mask_phy_errsoths2: 1;
        u32 mask_phy_errsoths3: 1;
        u32 mask_phy_errsyncesc0: 1;
        u32 mask_phy_errsyncesc1: 1;
        u32 mask_phy_errsyncesc2: 1;
        u32 mask_phy_errsyncesc3: 1;
        u32 mask_phy_errcontrol0: 1;
        u32 mask_phy_errcontrol1: 1;
        u32 mask_phy_errcontrol2: 1;
        u32 mask_phy_errcontrol3: 1;
        u32 reserved: 12;
        } bits;
    } ERR_STATE_MSK;

    union _dsi_rx_0x18 {
        u32 val;
        struct _DSI_RX_ERR_STATE_CLR {
        u32 phy_errsotsynchs0_clr: 1;
        u32 phy_errsotsynchs1_clr: 1;
        u32 phy_errsotsynchs2_clr: 1;
        u32 phy_errsotsynchs3_clr: 1;
        u32 phy_erresc0_clr: 1;
        u32 phy_erresc1_clr: 1;
        u32 phy_erresc2_clr: 1;
        u32 phy_erresc3_clr: 1;
        u32 phy_errsoths0_clr: 1;
        u32 phy_errsoths1_clr: 1;
        u32 phy_errsoths2_clr: 1;
        u32 phy_errsoths3_clr: 1;
        u32 phy_errsyncesc0_clr: 1;
        u32 phy_errsyncesc1_clr: 1;
        u32 phy_errsyncesc2_clr: 1;
        u32 phy_errsyncesc3_clr: 1;
        u32 phy_errcontrol0_clr: 1;
        u32 phy_errcontrol1_clr: 1;
        u32 phy_errcontrol2_clr: 1;
        u32 phy_errcontrol3_clr: 1;
        u32 reserved: 12;
        } bits;
    } ERR_STATE_CLR;

    union _dsi_rx_0x1C {
        u32 val;
        struct _DSI_RX_CAL_DONE {
        /* Calibration done provided bt D-PHY lane0 */
        u32 phy_caldone0: 1;

        /* Calibration done provided bt D-PHY lane1 */
        u32 phy_caldone1: 1;

        /* Calibration done provided bt D-PHY lane2 */
        u32 phy_caldone2: 1;

        /* Calibration done provided bt D-PHY lane3 */
        u32 phy_caldone3: 1;

        u32 reserved: 28;
        } bits;
    } CAL_DONE;

    union _dsi_rx_0x20 {
        u32 val;
        struct _DSI_RX_CAL_FAILED {
        /* Calibration failed provided bt D-PHY lane0 */
        u32 phy_calfailed0: 1;

        /* Calibration failed provided bt D-PHY lane1 */
        u32 phy_calfailed1: 1;

        /* Calibration failed provided bt D-PHY lane2 */
        u32 phy_calfailed2: 1;

        /* Calibration failed provided bt D-PHY lane3 */
        u32 phy_calfailed3: 1;

        u32 reserved: 28;
        } bits;
    } CAL_FAILED;

    union _dsi_rx_0x24 {
        u32 val;
        struct _DSI_RX_MSK_CAL_DONE {
        /* Mask for calibration done provided bt D-PHY lane0 */
        u32 mask_phy_caldone0: 1;

        /* Mask for calibration done provided bt D-PHY lane1 */
        u32 mask_phy_caldone1: 1;

        /* Mask for calibration done provided bt D-PHY lane2 */
        u32 mask_phy_caldone2: 1;

        /* Mask for calibration done provided bt D-PHY lane3 */
        u32 mask_phy_caldone3: 1;

        u32 reserved: 28;
        } bits;
    } MSK_CAL_DONE;

    union _dsi_rx_0x28 {
        u32 val;
        struct _DSI_RX_MSK_CAL_FAILED {
        /* Mask for calibration failed provided bt D-PHY lane0 */
        u32 mask_phy_calfailed0: 1;

        /* Mask for calibration failed provided bt D-PHY lane1 */
        u32 mask_phy_calfailed1: 1;

        /* Mask for calibration failed provided bt D-PHY lane2 */
        u32 mask_phy_calfailed2: 1;

        /* Mask for calibration failed provided bt D-PHY lane3 */
        u32 mask_phy_calfailed3: 1;

        u32 reserved: 28;
        } bits;
    } MSK_CAL_FAILED;

    union _dsi_rx_0x2C {
        u32 val;
        struct _DSI_RX_PHY_TEST_CTRL0 {
        /* PHY test interface clear (active high) */
        u32 phy_testclr: 1;

        /* This bit is used to clock the TESTDIN bus into the D-PHY */
        u32 phy_testclk: 1;

        u32 reserved: 30;
        } bits;
    } PHY_TEST_CTRL0;

    union _dsi_rx_0x30 {
        u32 val;
        struct _DSI_RX_PHY_TEST_CTRL1 {
        /* PHY test interface input 8-bit data bus for internal
         * register programming and test functionalities access.
         */
        u32 phy_testdin: 8;

        /* PHY output 8-bit data bus for read-back and internal
         * probing functionalities.
         */
        u32 phy_testdout: 8;

        /*
         * PHY test interface operation selector:
         * 1: The address write operation is set on the falling edge
         *    of the testclk signal.
         * 0: The data write operation is set on the rising edge of
         *    the testclk signal.
         */
        u32 phy_testen: 1;

        u32 reserved: 15;
        } bits;
    } PHY_TEST_CTRL1;

    union _dsi_rx_0x34 {
        u32 val;
        struct _DSI_RX_PHY_RX_TRIGGERS {
        /* This field controls the trigger received */
        u32 phy_rx_triggers: 4;

        u32 reserved: 28;
        } bits;
    } PHY_RX_TRIGGERS;

    union _dsi_rx_0x38 {
        u32 val;
        struct _DSI_RX_LINE_START_DELAY {
        /*
         * DPI interface signal delay to be used in dpi_clk
         * domain for control logic to read video data from pixel
         * memory, measured in dpi_clk cycles
         */
        u32 line_start_delay: 31;

        /* delay function enable */
        u32 delay_en: 1;
        } bits;
    } LINE_START_DELAY;

    union _dsi_rx_0x3C {
        u32 val;
        struct _DSI_RX_VIDEO_VBLK_LINES {
        /* This field configures the Vertical Synchronism Active
         * period measured in number of horizontal lines
         */
        u32 vsa_lines: 8;

        /* This field configures the Vertical Back Porch period
         * measured in number of horizontal lines
         */
        u32 vbp_lines: 12;

        u32 reserved: 12;
        } bits;
    } VIDEO_VBLK_LINES;

    union _dsi_rx_0x40 {
        u32 val;
        struct _DSI_RX_VIDEO_VFP_LINES {
        /* This field configures the Vertical Front Porch period
         * measured in number of horizontal lines
         */
        u32 vfp_lines: 14;

        u32 reserved: 18;
        } bits;
    } VIDEO_VFP_LINES;

    union _dsi_rx_0x44 {
        u32 val;
        struct _DSI_RX_VIDEO_VACTIVE_LINES {
        /* This field configures the Vertical Active period measured
         * in number of horizontal lines
         */
        u32 vactive_lines: 14;

        u32 reserved: 18;
        } bits;
    } VIDEO_VACTIVE_LINES;

    union _dsi_rx_0x48 {
        u32 val;
        struct _DSI_RX_VIDEO_LINE_TIME {
        /* This field configures the Horizontal Active image data
         * period counted in dpi clock cycles
         */
        u32 video_line_hact_time: 16;

        u32 reserved: 16;
        } bits;
    } VIDEO_LINE_TIME;

    union _dsi_rx_0x4C {
        u32 val;
        struct _DSI_RX_VIDEO_LINE_HBLK_TIME {
        /* This field configures the Horizontal Back Porch period
         * in dpi clock cycles
         */
        u32 video_line_hbp_time: 16;

        /* This field configures the Horizontal Synchronism Active
         * period in dpi clock cycles
         */
        u32 video_line_hsa_time: 16;
        } bits;
    } VIDEO_LINE_HBLK_TIME;

    union _dsi_rx_0x50 {
        u32 val;
        struct _DSI_RX_VIDEO_LINE_HFP_TIME {
        /* This field configures the Horizontal Front Porch period
         * in dpi clock cycles
         */
        u32 video_line_hfp_time: 14;

        u32 reserved: 18;
        } bits;
    } VIDEO_LINE_HFP_TIME;

    union _dsi_rx_0x54 {
        u32 val;
        struct _DSI_RX_SOFT_RESET {
        /*
         * This bit configures the core either to work normal or to
         * reset. It's default value is 0. After the core configur-
         * ation, to enable the dsi_ctrl_top, set this register to 1.
         * 1: power up     0: reset core
         */
        u32 dsi_soft_reset: 1;

        u32 reserved: 31;
        } bits;
    } SOFT_RESET;

    union _dsi_rx_0x58 {
        u32 val;
        struct _DSI_RX_EOTP_EN {
        /* When set to 1, this bit enables the EoTp reception. */
        u32 rx_eotp_en: 1;

        u32 reserved: 31;
        } bits;
    } EOTP_EN;

    union _dsi_rx_0x5C {
        u32 val;
        struct _DSI_RX_INT_STS {
        u32 reserved_0: 1;

        /* This bit indicates that the EoTp packet is not received at
         * the end of the incoming peripheral transmission
         */
        u32 eotp_not_receive_err: 1;

        /* This bit indicates that during a DPI pixel line storage,
         * the payload FIFO becomes full and the data stored is
         * corrupted.
         */
        u32 dpi_pix_fifo_wr_err: 1;

        /* This bit indicates that during a DPI pixel line storage,
         * the payload FIFO becomes empty and the data stored is
         * corrupted.
         */
        u32 dpi_pix_fifo_rd_err: 1;

        /* This bit indicates that the ECC single error is detected
         * and corrected in a received packet.
         */
        u32 ecc_single_err: 1;

        /* This bit indicates that the ECC multiple error is detected
         * in a received packet.
         */
        u32 ecc_multi_err: 1;

        /* This bit indicates that the CRC error is detected in the
         * received packet payload.
         */
        u32 crc_err: 1;

        u32 reserved_1: 25;
        } bits;
    } DSI_RX_INT_STS;

    union _dsi_rx_0x60 {
        u32 val;
        struct _DSI_RX_INT_STS_MSK {
        u32 reserved_0: 1;
        u32 eotp_not_receive_err_mask: 1;
        u32 dpi_pix_fifo_wr_err_mask: 1;
        u32 dpi_pix_fifo_rd_err_mask: 1;
        u32 ecc_single_err_mask: 1;
        u32 ecc_multi_err_mask: 1;
        u32 crc_err_mask: 1;
        u32 reserved_1: 25;
        } bits;
    } DSI_RX_INT_STS_MSK;

    union _dsi_rx_0x64 {
        u32 val;
        struct _DSI_RX_INT_STS_CLR {
        u32 reserved_0: 1;
        u32 eotp_not_receive_err_clr: 1;
        u32 dpi_pix_fifo_wr_err_clr: 1;
        u32 dpi_pix_fifo_rd_err_clr: 1;
        u32 ecc_single_err_clr: 1;
        u32 ecc_multi_err_clr: 1;
        u32 crc_err_clr: 1;
        u32 reserved_1: 25;
        } bits;
    } DSI_RX_INT_STS_CLR;

    union _dsi_rx_0x68 {
        u32 val;
        struct _DSI_RX_CAL_DONE_INT_CLR {
        u32 phy_caldone0_clr: 1;
        u32 phy_caldone1_clr: 1;
        u32 phy_caldone2_clr: 1;
        u32 phy_caldone3_clr: 1;
        u32 reserved: 28;
        } bits;
    } CAL_DONE_INT_CLR;

    union _dsi_rx_0x6C {
        u32 val;
        struct _DSI_RX_CAL_FAILED_INT_CLR {
        u32 phy_calfailed0_clr: 1;
        u32 phy_calfailed1_clr: 1;
        u32 phy_calfailed2_clr: 1;
        u32 phy_calfailed3_clr: 1;
        u32 reserved: 28;
        } bits;
    } CAL_FAILED_INT_CLR;

    union _dsi_rx_0xA0 {
        u32 val;
        struct _DSI_RX_DSI_ALL_IDLE {
        u32 dsi_all_idle: 1;
        u32 reserved: 31;
        } bits;
    } DSI_ALL_IDLE;
};

struct umb9230s_device {
    struct device dev;

    bool dsc_en;
    u32 slice_width;
    u32 slice_height;
    u32 output_bpc;
    struct dsc_cfg dsc_cfg;
    struct dsc_init_param dsc_init;

    struct dsi_tx_context dsi_ctx;

    struct dphy_tx_context phy_ctx;
    const struct dphy_tx_pll_ops *pll;

    struct mutex lock;
    bool enabled;

    u8 i2c_addr;
};

extern const struct dphy_tx_pll_ops umb9230s_dphy_tx_pll_ops;

#ifdef CONFIG_DRM_SPRD_UMB9230S
void umb9230s_dsi_tx_enable(struct umb9230s_device *umb9230s);
int umb9230s_dsi_tx_wr_pkt(struct umb9230s_device *umb9230s, u8 vc, u8 type,
            const u8 *param, u16 len);
int umb9230s_dsi_tx_rd_pkt(struct umb9230s_device *umb9230s, u8 vc, u8 type,
            u8 msb_byte, u8 lsb_byte,
            u8 *buffer, u8 bytes_to_read);
void umb9230s_dsi_tx_set_work_mode(struct umb9230s_device *umb9230s, u8 mode);
void umb9230s_dsi_tx_state_reset(struct umb9230s_device *umb9230s);
void umb9230s_dsi_tx_lp_cmd_enable(struct umb9230s_device *umb9230s, bool enable);
void umb9230s_dsi_tx_vrr_timing(struct umb9230s_device *umb9230s);
void umb9230s_dsi_tx_fini(struct umb9230s_device *umb9230s);
void umb9230s_dsi_tx_configure(struct umb9230s_device *umb9230s);

void umb9230s_dsi_rx_init(struct umb9230s_device *umb9230s);
void umb9230s_dsi_rx_state_reset(struct umb9230s_device *umb9230s);
void umb9230s_dsi_rx_vrr_timing(struct umb9230s_device *umb9230s);

void umb9230s_enable(struct umb9230s_device *umb9230s);
void umb9230s_disable(struct umb9230s_device *umb9230s);
int umb9230s_vrr_timing(struct umb9230s_device *umb9230s);
void umb9230s_isr(void *data);
void umb9230s_parse_lcd_info(struct umb9230s_device *umb9230s, struct device_node *lcd_node);
void umb9230s_videomode_copy(struct umb9230s_device *umb9230s, struct videomode *vm);

void umb9230s_phy_rx_init(struct umb9230s_device *umb9230s);
int umb9230s_phy_rx_wait_clklane_stop_state(struct umb9230s_device *umb9230s);
int umb9230s_phy_tx_enable(struct umb9230s_device *umb9230s);
void umb9230s_phy_tx_hs_clk_en(struct umb9230s_device *umb9230s, bool enable);
void umb9230s_phy_tx_ulps_enter(struct umb9230s_device *umb9230s);
int umb9230s_sysfs_init(struct device *dev);
void umb9230s_sysfs_deinit(struct device *dev);
#else
static inline int umb9230s_dsi_tx_wr_pkt(struct umb9230s_device *umb9230s, u8 vc, u8 type,
            const u8 *param, u16 len)
{
    return 0;
}

static inline int umb9230s_dsi_tx_rd_pkt(struct umb9230s_device *umb9230s, u8 vc, u8 type,
            u8 msb_byte, u8 lsb_byte,
            u8 *buffer, u8 bytes_to_read)
{
    return 0;
}

static inline void umb9230s_dsi_tx_set_work_mode(struct umb9230s_device *umb9230s, u8 mode)
{
    return;
}

static inline void umb9230s_dsi_tx_state_reset(struct umb9230s_device *umb9230s)
{
    return;
}

static inline void umb9230s_dsi_tx_lp_cmd_enable(struct umb9230s_device *umb9230s, bool enable)
{
    return;
}

static inline void umb9230s_dsi_rx_state_reset(struct umb9230s_device *umb9230s)
{
    return;
}

static inline int umb9230s_phy_rx_wait_clklane_stop_state(struct umb9230s_device *umb9230s)
{
    return 0;
}

static inline void umb9230s_enable(struct umb9230s_device *umb9230s)
{
    return;
}

static inline void umb9230s_disable(struct umb9230s_device *umb9230s)
{
    return;
}

static inline int umb9230s_vrr_timing(struct umb9230s_device *umb9230s)
{
    return 0;
}

static inline void umb9230s_phy_tx_ulps_enter(struct umb9230s_device *umb9230s)
{
    return;
}

static inline void umb9230s_parse_lcd_info(struct umb9230s_device *umb9230s,
                                struct device_node *lcd_node)
{
    return;
}

static inline void umb9230s_videomode_copy(struct umb9230s_device *umb9230s, struct videomode *vm)
{
    return;
}

static inline void umb9230s_dsi_tx_configure(struct umb9230s_device *umb9230s)
{
    return;
}

#endif

#endif /* _UMB9230S_H_ */
