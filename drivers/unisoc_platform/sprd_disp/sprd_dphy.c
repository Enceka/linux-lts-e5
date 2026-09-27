// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "sprd_dphy.h"
#include "sprd_dsi.h"
#include "dphy/sprd_dphy_api.h"
#include "sysfs/sysfs_display.h"

static int regmap_tst_io_write(void *context, u32 reg, u32 val)
{
	struct sprd_dphy *dphy = context;

	if (val > 0xff || reg > 0xff)
		return -EINVAL;

	DRM_DEBUG("reg = 0x%02x, val = 0x%02x\n", reg, val);

	sprd_dphy_test_write(dphy, reg, val);

	return 0;
}

static int regmap_tst_io_read(void *context, u32 reg, u32 *val)
{
	struct sprd_dphy *dphy = context;
	struct device *dsi_dev = sprd_disp_pipe_get_input(dphy->dev.parent);
	struct sprd_dsi *dsi = dev_get_drvdata(dsi_dev);
	int ret;

	if (!dsi->ctx.enabled) {
		pr_err("dphy is suspended, stop access\n");
		return -EINVAL;
	}

	if (reg > 0xff)
		return -EINVAL;

	ret = sprd_dphy_test_read(dphy, reg);
	if (ret < 0)
		return ret;

	*val = ret;

	DRM_DEBUG("reg = 0x%02x, val = 0x%02x\n", reg, *val);
	return 0;
}

static struct regmap_bus regmap_tst_io = {
	.reg_write = regmap_tst_io_write,
	.reg_read = regmap_tst_io_read,
};

static const struct regmap_config byte_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct regmap_config word_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int sprd_dphy_regmap_init(struct sprd_dphy *dphy)
{
	struct dphy_context *ctx = &dphy->ctx;
	struct regmap *regmap;

	if (ctx->apbbase)
		regmap = devm_regmap_init_mmio(&dphy->dev,
			(void __iomem *)ctx->apbbase, &word_config);
	else
		regmap = devm_regmap_init(&dphy->dev, &regmap_tst_io,
					  dphy, &byte_config);

	if (IS_ERR(regmap)) {
		DRM_ERROR("dphy regmap init failed\n");
		return PTR_ERR(regmap);
	}

	ctx->regmap = regmap;

	return 0;
}

int sprd_dphy_enable(struct sprd_dphy *dphy)
{
	int ret;

	mutex_lock(&dphy->ctx.lock);
	if (dphy->glb->power)
		dphy->glb->power(&dphy->ctx, true);
	if (dphy->glb->enable)
		dphy->glb->enable(&dphy->ctx);

	ret = sprd_dphy_init(dphy);
	if (ret) {
		mutex_unlock(&dphy->ctx.lock);
		DRM_ERROR("sprd dphy init failed\n");
		return -EINVAL;
	}

	if (dphy->slave) {
		if (dphy->slave->glb && dphy->slave->glb->power)
			dphy->slave->glb->power(&dphy->slave->ctx, true);
		if (dphy->slave->glb && dphy->slave->glb->enable)
			dphy->slave->glb->enable(&dphy->slave->ctx);

		ret = sprd_dphy_init(dphy->slave);
		if (ret) {
			mutex_unlock(&dphy->ctx.lock);
			DRM_ERROR("sprd dphy slave init failed\n");
			return -EINVAL;
		}
	}

	dphy->ctx.enabled = true;
	mutex_unlock(&dphy->ctx.lock);

	return 0;
}

int sprd_dphy_disable(struct sprd_dphy *dphy)
{
	int ret;

	mutex_lock(&dphy->ctx.lock);
	if (dphy->glb->disable)
		dphy->glb->disable(&dphy->ctx);
	if (dphy->glb->power)
		dphy->glb->power(&dphy->ctx, false);

	if (dphy->slave) {
		ret = sprd_dphy_fini(dphy->slave);
		if (ret)
			DRM_ERROR("sprd dphy slave fini failed\n");

		if (dphy->slave->glb && dphy->slave->glb->disable)
			dphy->slave->glb->disable(&dphy->slave->ctx);
		if (dphy->slave->glb && dphy->slave->glb->power)
			dphy->slave->glb->power(&dphy->slave->ctx, false);

	}

	dphy->ctx.enabled = false;
	mutex_unlock(&dphy->ctx.lock);

	return 0;
}

static int sprd_dphy_device_create(struct sprd_dphy *dphy,
				   struct device *parent)
{
	int ret;

	dphy->dev.class = display_class;
	dphy->dev.parent = parent;
	dphy->dev.of_node = parent->of_node;
	dev_set_name(&dphy->dev, "dphy%d", dphy->ctx.id);
	dev_set_drvdata(&dphy->dev, dphy);

	ret = device_register(&dphy->dev);
	if (ret)
		DRM_ERROR("dphy device register failed\n");

	return ret;
}

static int sprd_dphy_context_init(struct sprd_dphy *dphy,
				  struct device *dev)
{
	struct resource r;
	u32 tmp;
	struct device_node *np = dev->of_node;

	if (dphy->glb->parse_dt)
		dphy->glb->parse_dt(&dphy->ctx, np);

	if (!of_address_to_resource(np, 0, &r)) {
		dphy->ctx.ctrlbase = (unsigned long)
		    devm_ioremap(dev, r.start, resource_size(&r));
		if (dphy->ctx.ctrlbase == 0) {
			DRM_ERROR("dphy ctrlbase ioremap failed\n");
			return -EFAULT;
		}
	} else {
		DRM_ERROR("parse dphy ctrl reg base failed\n");
		return -EINVAL;
	}

	if (!of_address_to_resource(np, 1, &r)) {
		DRM_INFO("this dphy has apb reg base\n");
		dphy->ctx.apbbase = (unsigned long)
		    devm_ioremap(dev, r.start, resource_size(&r));
		if (dphy->ctx.apbbase == 0) {
			DRM_ERROR("dphy apbbase ioremap failed\n");
			return -EFAULT;
		}
	}

	if (!of_property_read_u32(np, "dev-id", &tmp))
		dphy->ctx.id = tmp;

	if (of_property_read_bool(np, "sprd,ulps-disabled"))
		dphy->ctx.ulps_enable = false;
	else
		dphy->ctx.ulps_enable = true;

	mutex_init(&dphy->ctx.lock);
	dphy->ctx.enabled = true;

	return 0;
}

static int sprd_dphy_dual_channel_init(struct sprd_dphy *dphy)
{
	struct device_node *np;
	struct platform_device *secondary;

	np = of_parse_phandle(dphy->dev.of_node, "sprd,dual-channel", 0);
	if (np) {
		DRM_INFO("find sprd,dual-channel\n");
		secondary = of_find_device_by_node(np);
		dphy->slave = dev_get_drvdata(&secondary->dev);
		of_node_put(np);

		if (!dphy->slave)
			return -EPROBE_DEFER;

		dphy->slave->master = dphy;
	}

	return 0;
}

static const struct sprd_dphy_ops sharkle_dphy = {
	.ppi = &dsi_ctrl_ppi_ops,
	.pll = &sharkle_dphy_pll_ops,
	.glb = &sharkle_dphy_glb_ops,
};

static const struct sprd_dphy_ops pike2_dphy = {
	.ppi = &dsi_ctrl_ppi_ops,
	.pll = &sharkle_dphy_pll_ops,
	.glb = &pike2_dphy_glb_ops,
};

static const struct sprd_dphy_ops sharkl3_dphy = {
	.ppi = &dsi_ctrl_ppi_ops,
	.pll = &sharkle_dphy_pll_ops,
	.glb = &sharkl3_dphy_glb_ops,
};

static const struct sprd_dphy_ops sharkl5_dphy = {
	.ppi = &dsi_ctrl_ppi_ops,
	.pll = &sharkl5_dphy_pll_ops,
	.glb = &sharkl5_dphy_glb_ops,
};

static const struct sprd_dphy_ops sharkl5pro_dphy = {
	.ppi = &dsi_ctrl_ppi_ops,
	.pll = &sharkl5_dphy_pll_ops,
	.glb = &sharkl5pro_dphy_glb_ops,
};

static const struct sprd_dphy_ops qogirl6_dphy = {
	.ppi = &dsi_ctrl_ppi_ops,
	.pll = &sharkl5_dphy_pll_ops,
	.glb = &qogirl6_dphy_glb_ops,
};

static const struct sprd_dphy_ops qogirn6pro_dphy = {
	.ppi = &dsi_ctrl_ppi_ops,
	.pll = &sharkl5_dphy_pll_ops,
	.glb = &qogirn6pro_dphy_glb_ops,
};

static const struct sprd_dphy_ops qogirn6pro_dphy1 = {
	.ppi = &dsi_ctrl_ppi_ops,
	.pll = &sharkl5_dphy_pll_ops,
	.glb = &qogirn6pro_dphy_s_glb_ops,
};

static const struct sprd_dphy_ops qogirn6lite_dphy = {
	.ppi = &dsi_ctrl_ppi1_ops,
	.pll = &sharkl5_dphy_pll_ops,
	.glb = &qogirn6lite_dphy_glb_ops,
};

static const struct of_device_id dphy_match_table[] = {
	{ .compatible = "sprd,sharkle-dsi-phy",
	  .data = &sharkle_dphy },
	{ .compatible = "sprd,pike2-dsi-phy",
	  .data = &pike2_dphy },
	{ .compatible = "sprd,sharkl3-dsi-phy",
	  .data = &sharkl3_dphy },
	{ .compatible = "sprd,sharkl5-dsi-phy",
	  .data = &sharkl5_dphy },
	{ .compatible = "sprd,sharkl5pro-dsi-phy",
	  .data = &sharkl5pro_dphy },
	{ .compatible = "sprd,qogirl6-dsi-phy",
	  .data = &qogirl6_dphy },
	{ .compatible = "sprd,qogirn6pro-dsi-phy",
	  .data = &qogirn6pro_dphy },
	{ .compatible = "sprd,qogirn6lite-dsi-phy",
	  .data = &qogirn6lite_dphy },
	{ .compatible = "sprd,qogirn6pro-dsi1-phy",
	.data = &qogirn6pro_dphy1 },
	{ /* sentinel */ },
};

static int sprd_dphy_probe(struct platform_device *pdev)
{
	const struct sprd_dphy_ops *pdata;
	struct sprd_dphy *dphy;
	struct device *dsi_dev;
	struct sprd_dsi *dsi;
	int ret;

	dphy = devm_kzalloc(&pdev->dev, sizeof(*dphy), GFP_KERNEL);
	if (!dphy)
		return -ENOMEM;

	dsi_dev = sprd_disp_pipe_get_input(&pdev->dev);
	if (!dsi_dev)
		return -ENODEV;

	pdata = of_device_get_match_data(&pdev->dev);
	if (pdata) {
		dphy->ppi = pdata->ppi;
		dphy->pll = pdata->pll;
		dphy->glb = pdata->glb;
	} else {
		DRM_ERROR("No matching driver data found\n");
		return -EINVAL;
	}

	ret = sprd_dphy_context_init(dphy, &pdev->dev);
	if (ret)
		return ret;

	DRM_INFO("dphy driver probe (dphy->ctx.id=%d)\n", dphy->ctx.id);

	ret = sprd_dphy_device_create(dphy, &pdev->dev);
	if (ret)
		return ret;

	ret = sprd_dphy_regmap_init(dphy);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, dphy);

	dsi = dev_get_drvdata(dsi_dev);
	if (dsi->dual_dsi_en) {
		ret = sprd_dphy_dual_channel_init(dphy);
		if (ret)
			return ret;
	}

        ret = sprd_dphy_sysfs_init(&dphy->dev);
        if (ret)
                return ret;

	DRM_INFO("dphy driver probe success\n");

	return 0;
}

static void sprd_dphy_remove(struct platform_device *pdev)
{
	struct sprd_dphy *dphy = platform_get_drvdata(pdev);

	if (dphy)
		sprd_dphy_sysfs_deinit(&dphy->dev);
}

struct platform_driver sprd_dphy_driver = {
	.probe	= sprd_dphy_probe,
	.remove = sprd_dphy_remove,
	.driver = {
		.name  = "sprd-dphy-drv",
		.of_match_table	= dphy_match_table,
	}
};

MODULE_AUTHOR("Leon He <leon.he@unisoc.com>");
MODULE_AUTHOR("Kevin Tang <kevin.tang@unisoc.com>");
MODULE_DESCRIPTION("Unisoc SoC MIPI DSI PHY driver");
MODULE_LICENSE("GPL v2");
