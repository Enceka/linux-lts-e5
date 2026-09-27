// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc (Spreadtrum) Mali platform glue for panfrost.
 *
 * On UMS9620/UMS9621 ("qogirn6pro"/"qogirn6lite") the Mali GPU is not behind a
 * generic power domain, and nothing in the device tree describes how it is
 * switched on: the vendor kbase driver pokes the PMU and GPU APB syscons
 * directly from its platform code
 * (drivers/gpu/arm/midgard/platform/qogirn6l/mali_kbase_config_qogirn6l.c).
 * The sequence is:
 *
 *   - the GPU PLL ("GPLL") is forced on and the GPU DCDC is enabled -- the
 *     latter is a PMIC register write, not a regulator consumer, so treating
 *     it as a supply does not work,
 *   - the GPU is taken out of its forced-shutdown state and soft reset,
 *   - the core clock gate inside the GPU APB block is enabled, the power
 *     state is polled until the block reports itself up, and the hardware
 *     DVFS engine is given a frequency index.
 *
 * The device tree only carries those registers as opaque
 * (syscon, register, mask) triples, which is why this glue reads them back and
 * replays the sequence instead of describing it in a binding.
 *
 * Copyright (C) 2026 The e5-linux project
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "panfrost_device.h"
#include "panfrost_sprd.h"

/* Offsets inside the GPU DVFS APB block: kbase's gpu,qogirn6l-regs.h */
#define SPRD_DVFS_APB_DVFS_INDEX_CFG	0x0794
#define SPRD_DVFS_APB_DVFS_INDEX_MASK	0x7
#define SPRD_DVFS_APB_FREQ_UPD_CFG0	0x09ac
#define SPRD_DVFS_APB_FREQ_UPD_MASK	0x3	/* HDSK_EN | DELAY_EN */

/*
 * The hardware DVFS table entry kbase starts on.  On these parts index 3 of
 * the "operating-points" list is 384 MHz; kbase raises it from there with its
 * own governor, panfrost only has to get off the 26 MHz floor.
 */
#define SPRD_DVFS_INDEX_DEFAULT		3

#define SPRD_TOP_STATE_POLL_US		50
#define SPRD_TOP_STATE_POLL_MAX		200
#define SPRD_TOP_STATE_UP		0

struct sprd_reg {
	struct regmap *map;
	u32 reg;
	u32 mask;
};

struct panfrost_sprd {
	struct sprd_reg top_dvfs_cfg;
	struct sprd_reg gpu_sw_dvfs_ctrl;
	struct sprd_reg dcdc_gpu_pd;
	struct sprd_reg top_force_shutdown;
	struct sprd_reg gpll_frc_off;
	struct sprd_reg gpll_frc_on;
	struct sprd_reg clk_core_gpu_eb;
	struct sprd_reg gpu_top_state;

	struct regmap *gpu_dvfs_apb;
};

/*
 * Read a "<&syscon register mask>" property.  The register and the mask are
 * baked into the device tree by the vendor BSP; we never need to know either.
 */
static int sprd_reg_get(struct device *dev, const char *prop,
			struct sprd_reg *out)
{
	struct regmap *map;
	u32 args[2];

	map = syscon_regmap_lookup_by_phandle_args(dev->of_node, prop, 2, args);
	if (IS_ERR(map)) {
		dev_err(dev, "no %s property/syscon: %ld\n", prop,
			PTR_ERR(map));
		return PTR_ERR(map);
	}

	out->map = map;
	out->reg = args[0];
	out->mask = args[1];

	return 0;
}

/*
 * The GPU DCDC lives in the PMIC, and the vendor device tree points
 * dcdc_gpu_pd at the PMU syscon with a note that the address is fake and the
 * "real syscon config" happens at init: kbase looks the PMIC up by compatible
 * and swaps the regmap underneath the parsed register/mask pair.  Do the same.
 */
static struct regmap *sprd_pmic_regmap(struct device *dev)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct regmap *map;

	np = of_find_compatible_node(NULL, NULL, "sprd,ump962x-syscon");
	if (!np) {
		dev_warn(dev, "no sprd,ump962x-syscon node: GPU DCDC untouched\n");
		return NULL;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		dev_warn(dev, "no platform device for the PMIC syscon\n");
		return NULL;
	}

	map = dev_get_regmap(pdev->dev.parent, NULL);
	put_device(&pdev->dev);

	if (!map)
		dev_warn(dev, "PMIC syscon has no regmap\n");

	return map;
}

static int sprd_gpu_top_state_wait(struct panfrost_sprd *sprd)
{
	unsigned int val;
	int i;

	for (i = 0; i < SPRD_TOP_STATE_POLL_MAX; i++) {
		if (regmap_read(sprd->gpu_top_state.map,
				sprd->gpu_top_state.reg, &val))
			return -EIO;

		if ((val & sprd->gpu_top_state.mask) == SPRD_TOP_STATE_UP)
			return 0;

		udelay(SPRD_TOP_STATE_POLL_US);
	}

	return -ETIMEDOUT;
}

static void sprd_gpu_power_on(struct panfrost_sprd *sprd,
			      struct reset_control *rstc)
{
	/* GPLL: 0 = not forced off, 1 = forced on */
	regmap_update_bits(sprd->gpll_frc_off.map, sprd->gpll_frc_off.reg,
			   sprd->gpll_frc_off.mask, ~sprd->gpll_frc_off.mask);
	udelay(100);
	regmap_update_bits(sprd->gpll_frc_on.map, sprd->gpll_frc_on.reg,
			   sprd->gpll_frc_on.mask, sprd->gpll_frc_on.mask);
	udelay(150);

	/* dcdc_gpu_pd bit: 0 = enable, 1 = power down */
	regmap_update_bits(sprd->dcdc_gpu_pd.map, sprd->dcdc_gpu_pd.reg,
			   sprd->dcdc_gpu_pd.mask,
			   ~sprd->dcdc_gpu_pd.mask);
	udelay(400);

	regmap_update_bits(sprd->top_force_shutdown.map,
			   sprd->top_force_shutdown.reg,
			   sprd->top_force_shutdown.mask,
			   ~sprd->top_force_shutdown.mask);

	reset_control_assert(rstc);
	udelay(10);
	reset_control_deassert(rstc);
	udelay(300);
}

static void sprd_gpu_power_off(struct panfrost_sprd *sprd)
{
	regmap_update_bits(sprd->top_force_shutdown.map,
			   sprd->top_force_shutdown.reg,
			   sprd->top_force_shutdown.mask,
			   sprd->top_force_shutdown.mask);

	regmap_update_bits(sprd->dcdc_gpu_pd.map, sprd->dcdc_gpu_pd.reg,
			   sprd->dcdc_gpu_pd.mask, sprd->dcdc_gpu_pd.mask);
	udelay(10);

	regmap_update_bits(sprd->gpll_frc_off.map, sprd->gpll_frc_off.reg,
			   sprd->gpll_frc_off.mask, sprd->gpll_frc_off.mask);
	regmap_update_bits(sprd->gpll_frc_on.map, sprd->gpll_frc_on.reg,
			   sprd->gpll_frc_on.mask, ~sprd->gpll_frc_on.mask);
}

int panfrost_sprd_init(struct panfrost_device *pfdev)
{
	struct device *dev = pfdev->dev;
	struct panfrost_sprd *sprd;
	struct regmap *pmic;
	int ret;

	if (!of_device_is_compatible(dev->of_node, "sprd,mali-natt"))
		return 0;

	sprd = devm_kzalloc(dev, sizeof(*sprd), GFP_KERNEL);
	if (!sprd)
		return -ENOMEM;

	ret = sprd_reg_get(dev, "top_dvfs_cfg", &sprd->top_dvfs_cfg);
	if (ret)
		return ret;
	ret = sprd_reg_get(dev, "gpu_sw_dvfs_ctrl", &sprd->gpu_sw_dvfs_ctrl);
	if (ret)
		return ret;
	ret = sprd_reg_get(dev, "dcdc_gpu_pd", &sprd->dcdc_gpu_pd);
	if (ret)
		return ret;
	ret = sprd_reg_get(dev, "top_force_shutdown", &sprd->top_force_shutdown);
	if (ret)
		return ret;
	ret = sprd_reg_get(dev, "gpll_cfg_frc_off", &sprd->gpll_frc_off);
	if (ret)
		return ret;
	ret = sprd_reg_get(dev, "gpll_cfg_frc_on", &sprd->gpll_frc_on);
	if (ret)
		return ret;
	ret = sprd_reg_get(dev, "clk_core_gpu_eb", &sprd->clk_core_gpu_eb);
	if (ret)
		return ret;
	ret = sprd_reg_get(dev, "gpu_top_state", &sprd->gpu_top_state);
	if (ret)
		return ret;

	pmic = sprd_pmic_regmap(dev);
	if (pmic)
		sprd->dcdc_gpu_pd.map = pmic;

	sprd->gpu_dvfs_apb = syscon_regmap_lookup_by_phandle(dev->of_node,
						"sprd,gpu-dvfs-apb-syscon");
	if (IS_ERR(sprd->gpu_dvfs_apb)) {
		dev_err(dev, "no sprd,gpu-dvfs-apb-syscon: %ld\n",
			PTR_ERR(sprd->gpu_dvfs_apb));
		return PTR_ERR(sprd->gpu_dvfs_apb);
	}

	/*
	 * Let the hardware DVFS engine drive the GPU frequency and make sure
	 * the software path is not also steering it, exactly as kbase does
	 * before it powers the GPU on.
	 */
	regmap_update_bits(sprd->top_dvfs_cfg.map, sprd->top_dvfs_cfg.reg,
			   sprd->top_dvfs_cfg.mask, ~sprd->top_dvfs_cfg.mask);
	regmap_update_bits(sprd->gpu_sw_dvfs_ctrl.map,
			   sprd->gpu_sw_dvfs_ctrl.reg,
			   sprd->gpu_sw_dvfs_ctrl.mask,
			   ~sprd->gpu_sw_dvfs_ctrl.mask);

	/*
	 * The GPU soft reset is the one panfrost_reset_init() already owns, so
	 * it is toggled through pfdev->rstc rather than claimed a second time.
	 */
	sprd_gpu_power_on(sprd, pfdev->rstc);

	/* Core clock gate inside the GPU APB block */
	regmap_update_bits(sprd->clk_core_gpu_eb.map,
			   sprd->clk_core_gpu_eb.reg,
			   sprd->clk_core_gpu_eb.mask,
			   sprd->clk_core_gpu_eb.mask);
	udelay(400);

	ret = sprd_gpu_top_state_wait(sprd);
	if (ret) {
		dev_err(dev, "GPU did not report itself powered up (%d)\n", ret);
		return ret;
	}

	regmap_update_bits(sprd->gpu_dvfs_apb,
			   SPRD_DVFS_APB_FREQ_UPD_CFG0,
			   SPRD_DVFS_APB_FREQ_UPD_MASK, 1);
	regmap_update_bits(sprd->gpu_dvfs_apb,
			   SPRD_DVFS_APB_DVFS_INDEX_CFG,
			   SPRD_DVFS_APB_DVFS_INDEX_MASK,
			   SPRD_DVFS_INDEX_DEFAULT);

	pfdev->sprd = sprd;

	dev_info(dev, "Unisoc GPU powered on (DVFS index %d)\n",
		 SPRD_DVFS_INDEX_DEFAULT);

	return 0;
}

void panfrost_sprd_fini(struct panfrost_device *pfdev)
{
	struct panfrost_sprd *sprd = pfdev->sprd;

	if (!sprd)
		return;

	sprd_gpu_power_off(sprd);
	pfdev->sprd = NULL;
}
