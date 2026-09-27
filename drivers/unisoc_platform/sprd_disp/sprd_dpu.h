/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _SPRD_DPU_H_
#define _SPRD_DPU_H_

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/timex.h>
#include <video/videomode.h>

#include <uapi/drm/drm_mode.h>
#include <drm/drm_crtc.h>

#include "sprd_crtc.h"
#include "sprd_plane.h"
#include "disp_lib.h"
#include "disp_trusty.h"
#include "sprd_dsi.h"
#include "sprd_dsi_panel.h"
#include "dsi/sprd_dsi_api.h"
#include "dsi/sprd_dsi_hal.h"
#include "sprd_dsc.h"

#define DPU_INT_MAX_CNT			300

#define WAIT_TE_MAX_TIME_120		8600000
#define WAIT_TE_MAX_TIME_90		11500000
#define WAIT_TE_MAX_TIME_60		17100000

#define WAIT_TE_MIN_TIME_120		7500000
#define WAIT_TE_MIN_TIME_90		9000000
#define WAIT_TE_MIN_TIME_60		14000000

#define DPI_VREFRESH_120	120
#define DPI_VREFRESH_90		90
#define DPI_VREFRESH_60		60

#define cabc_cfg0			268439552
#define cabc_cfg1			268439552
#define cabc_cfg2			16777215
#define cabc_cfg3			0
#define cabc_cfg4			0

/* enhance config bits */
#define ENHANCE_MODE_UI			BIT(2)
#define ENHANCE_MODE_GAME		BIT(3)
#define ENHANCE_MODE_VIDEO		BIT(4)
#define ENHANCE_MODE_IMAGE		BIT(5)
#define ENHANCE_MODE_CAMERA		BIT(6)
#define ENHANCE_MODE_FULL_FRAME		BIT(7)

#define CABC_HIST_SIZE		128
#define CABC_HIST_V2_SIZE	256
#define LUTS_HEAD_SIZE		sizeof(struct luts_typeindex)
#define LUTS_HSV_PARA_SIZE	sizeof(struct hsv_params)
#define LUTS_2K_SIZE		2048

enum {
	SPRD_DPU_IF_DBI = 0,
	SPRD_DPU_IF_DPI,
	SPRD_DPU_IF_EDPI,
	SPRD_DPU_IF_LIMIT
};

enum {
	ENHANCE_CFG_ID_ENABLE,
	ENHANCE_CFG_ID_DISABLE,
	ENHANCE_CFG_ID_SCL,
	ENHANCE_CFG_ID_EPF,
	ENHANCE_CFG_ID_HSV,
	ENHANCE_CFG_ID_CM,
	ENHANCE_CFG_ID_SLP,
	ENHANCE_CFG_ID_GAMMA,
	ENHANCE_CFG_ID_LTM,
	ENHANCE_CFG_ID_CABC,
	ENHANCE_CFG_ID_CABC_HIST,
	ENHANCE_CFG_ID_CABC_HIST_V2,
	ENHANCE_CFG_ID_VSYNC_COUNT,
	ENHANCE_CFG_ID_FRAME_NO,
	ENHANCE_CFG_ID_CABC_NO,
	ENHANCE_CFG_ID_CABC_CUR_BL,
	ENHANCE_CFG_ID_CABC_PARAM,
	ENHANCE_CFG_ID_CABC_RUN,
	ENHANCE_CFG_ID_CABC_STATE,
	ENHANCE_CFG_ID_SLP_LUT,
	ENHANCE_CFG_ID_LUT3D,
	ENHANCE_CFG_ID_UD,
	ENHANCE_CFG_ID_UPDATE_LUTS,
	ENHANCE_CFG_ID_SR_EPF,
	ENHANCE_CFG_ID_MODE,
	ENHANCE_CFG_ID_MAX
};

struct dpu_context;

struct dpu_core_ops {
	void (*version)(struct dpu_context *ctx);
	int (*init)(struct dpu_context *ctx);
	void (*fini)(struct dpu_context *ctx);
	void (*run)(struct dpu_context *ctx);
	void (*stop)(struct dpu_context *ctx);
	void (*disable_vsync)(struct dpu_context *ctx);
	void (*enable_vsync)(struct dpu_context *ctx);
	u32 (*isr)(struct dpu_context *ctx);
	void (*write_back)(struct dpu_context *ctx, u8 count, bool debug);
	void (*ifconfig)(struct dpu_context *ctx);
	void (*flip)(struct dpu_context *ctx,
		     struct sprd_plane planes[], u8 count);
	void (*capability)(struct dpu_context *ctx,
			 struct sprd_crtc_capability *cap);
	void (*bg_color)(struct dpu_context *ctx, u32 color);
	int (*context_init)(struct dpu_context *ctx, struct device *dev);
	void (*enhance_set)(struct dpu_context *ctx, u32 id, void *param, size_t count);
	void (*enhance_get)(struct dpu_context *ctx, u32 id, void *param, size_t count);
	bool (*check_raw_int)(struct dpu_context *ctx, u32 mask);
	int (*modeset)(struct dpu_context *ctx, struct drm_display_mode *mode);
	void (*dma_request)(struct dpu_context *ctx);
	void (*get_gsp_base)(struct dpu_context *ctx, struct device_node *np);
	void (*reg_dump)(struct dpu_context *ctx);
};

struct dpu_clk_ops {
	int (*parse_dt)(struct dpu_context *ctx,
			struct device_node *np);
	int (*init)(struct dpu_context *ctx);
	int (*uinit)(struct dpu_context *ctx);
	int (*enable)(struct dpu_context *ctx);
	int (*disable)(struct dpu_context *ctx);
	int (*update)(struct dpu_context *ctx, int clk_id, int val);
	int (*vrr)(struct dpu_context *ctx, u32 dst_dpi_clk);
};

struct dpu_glb_ops {
	int (*parse_dt)(struct dpu_context *ctx,
			struct device_node *np);
	void (*enable)(struct dpu_context *ctx);
	void (*disable)(struct dpu_context *ctx);
	void (*reset)(struct dpu_context *ctx);
	void (*suspend_reset)(struct dpu_context *ctx);
	void (*power)(struct dpu_context *ctx, int enable);
};

struct scale_config_param {
	bool sr_mode_changed;
	bool need_scale;
	u8 skip_layer_index;
	u32 in_w;
	u32 in_h;
	u32 out_w;
	u32 out_h;
};

struct dpu_qos_cfg {
	u8 arqos_low;
	u8 arqos_high;
	u8 awqos_low;
	u8 awqos_high;
};

struct time_fifo {
	struct timespec64 ts[33];
	int head;
	u32 sum_num;
};

struct dpu_int_cnt {
	u16 int_cnt_all;
	u16 int_cnt_vsync;
	u16 int_cnt_te;
	u16 int_cnt_lay_reg_update_done;
	u16 int_cnt_dpu_reg_update_done;
	u16 int_cnt_dpu_all_update_done;
	u16 int_cnt_pq_reg_update_done;
	u16 int_cnt_pq_lut_update_done;
	u16 int_cnt_dpu_int_done;
	u16 int_cnt_dpu_int_err;
	u16 int_cnt_dpu_int_wb_done;
	u16 int_cnt_dpu_int_wb_err;
	u16 int_cnt_dpu_int_fbc_pld_err;
	u16 int_cnt_dpu_int_fbc_hdr_err;
	u16 int_cnt_dpu_int_mmu;
};

struct dpu_context {
	/* dpu common parameters */
	void __iomem *base;
	void __iomem *gsp_base;
	bool gsp_base_init;
	u32 base_offset[2];
	const char *version;
	int irq;
	u8 if_type;
	struct videomode vm;
	struct semaphore lock;
	struct mutex vrr_lock;
	bool enabled;
	bool stopped;
	bool flip_pending;
	wait_queue_head_t wait_queue;
	bool evt_update;
	bool evt_pq_update;
	bool evt_all_update;
	bool evt_all_regs_update;
	bool evt_pq_lut_update;
	bool evt_stop;
	bool evt_wb_done;
	irqreturn_t (*dpu_isr)(int irq, void *data);
	struct tasklet_struct dvfs_task;

	/* pq enhance parameters */
	void *enhance;
	int corner_radius;
	struct semaphore cabc_lock;
	struct work_struct cabc_work;
	struct work_struct cabc_bl_update;
	bool is_oled_bl;

	/* write back parameters */
	int wb_en;
	int wb_xfbc_en;
	int max_vsync_count;
	int vsync_count;
	struct sprd_layer_state wb_layer;
	struct work_struct wb_work;
	dma_addr_t wb_addr_p;
	void *wb_addr_v;
	size_t wb_buf_size;
	bool wb_configed;
	bool wb_pending;
	bool need_wb_work;

	/* te check parameters */
	wait_queue_head_t te_wq;
	wait_queue_head_t te_update_wq;
	bool te_check_en;
	bool evt_te_update;
	bool evt_te;

	/* corner config parameters */
	u32 corner_size;
	int sprd_corner_radius;
	bool sprd_corner_support;

	void *layer_top;
	void *layer_bottom;
	dma_addr_t layer_top_p;
	dma_addr_t layer_bottom_p;

	/* widevine config parameters */
	bool secure_debug;
	bool fastcall_en;
	int time;
	struct disp_message *tos_msg;

	/* vrr config parameters */
	bool fps_mode_changed;
	bool wb_size_changed;

	/* dsc config parameters */
	struct dsc_cfg dsc_cfg;
	struct dsc_init_param dsc_init;
	bool dual_dsi_en;
	bool dsc_en;
	int  dsc_mode;

	/* other specific parameters */
	bool panel_ready;
	unsigned long logo_addr;
	unsigned long logo_size;
	u32 prev_y2r_coef;
	u64 frame_count;
	struct time_fifo tf;
	struct dpu_int_cnt int_cnt;
	struct timer_list int_cnt_timer;

	/* scaling config parameters */
	struct scale_config_param scale_cfg;

	/* qos config parameters */
	struct dpu_qos_cfg qos_cfg;

	/* blend size limit config parameters */
	uint32_t max_cap_layers;

	/* blend size limit config parameters */
	bool vrr_enabled;

	/* command panel vrr config parameters */
	spinlock_t irq_lock;
	uint32_t actual_dpi_clk;
	uint32_t dpi_clk_60;
	uint32_t dpi_clk_90;
	uint32_t dpi_clk_120;
	bool is_single_run;
	ktime_t te_int_time;
	int te_int_max_gap;
	int te_int_min_gap;
	bool dpu_run_flag;
	bool cmd_dpi_mode;
};

struct sprd_dpu_ops {
	const struct dpu_core_ops *core;
	const struct dpu_clk_ops *clk;
	const struct dpu_glb_ops *glb;
};

struct sprd_dpu {
	struct device dev;
	struct mutex dpu_gsp_lock;
	struct sprd_crtc *crtc;
	struct dpu_context ctx;
	const struct dpu_core_ops *core;
	const struct dpu_clk_ops *clk;
	const struct dpu_glb_ops *glb;
	struct drm_display_mode mode;
	struct drm_display_mode actual_mode;
	struct sprd_dsi *dsi;
};

int dpu_wait_te_flush(struct dpu_context *ctx);
void sprd_drm_mode_copy(struct drm_display_mode *dst, const struct drm_display_mode *src);
void sprd_dpu_enable(struct sprd_dpu *dpu);
void sprd_dpu_disable(struct sprd_dpu *dpu);
void sprd_dpu_run(struct sprd_dpu *dpu);
void sprd_dpu_stop(struct sprd_dpu *dpu);
void sprd_dpu_resume(struct sprd_dpu *dpu);
extern int dpu_r6p0_enable_div6_clk(struct dpu_context *ctx);
extern int dpu_r6p1_enable_div6_clk(struct dpu_context *ctx);
extern int dpu_r6p0_glb_enable(struct dpu_context *ctx);

#ifdef CONFIG_DRM_SPRD_DPU0
void sprd_dpu_atomic_disable_force(struct drm_crtc *crtc);
#else
static inline void sprd_dpu_atomic_disable_force(struct drm_crtc *crtc) {}
#endif

extern const struct dpu_clk_ops sharkle_dpu_clk_ops;
extern const struct dpu_glb_ops sharkle_dpu_glb_ops;

extern const struct dpu_core_ops dpu_lite_r1p0_core_ops;
extern const struct dpu_clk_ops pike2_dpu_clk_ops;
extern const struct dpu_glb_ops pike2_dpu_glb_ops;

extern const struct dpu_core_ops dpu_lite_r2p0_core_ops;
extern const struct dpu_clk_ops sharkl5_dpu_clk_ops;
extern const struct dpu_glb_ops sharkl5_dpu_glb_ops;

extern const struct dpu_core_ops dpu_r2p0_core_ops;
extern const struct dpu_clk_ops sharkl3_dpu_clk_ops;
extern const struct dpu_glb_ops sharkl3_dpu_glb_ops;

extern const struct dpu_core_ops dpu_r4p0_core_ops;
extern const struct dpu_clk_ops sharkl5pro_dpu_clk_ops;
extern const struct dpu_glb_ops sharkl5pro_dpu_glb_ops;

extern const struct dpu_core_ops dpu_r5p0_core_ops;
extern const struct dpu_clk_ops qogirl6_dpu_clk_ops;
extern const struct dpu_glb_ops qogirl6_dpu_glb_ops;

extern const struct dpu_core_ops dpu_r6p0_core_ops;
extern const struct dpu_clk_ops qogirn6pro_dpu_clk_ops;
extern const struct dpu_glb_ops qogirn6pro_dpu_glb_ops;

extern const struct dpu_core_ops dpu_r6p1_core_ops;
extern const struct dpu_clk_ops qogirn6lite_dpu_clk_ops;
extern const struct dpu_glb_ops qogirn6lite_dpu_glb_ops;

#endif /* _SPRD_DPU_H_ */
