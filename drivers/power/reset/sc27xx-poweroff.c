// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 * Copyright (C) 2018 Linaro Ltd.
 */

#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/syscore_ops.h>

#define SC27XX_PWR_PD_HW	0xc2c
#define SC27XX_PWR_OFF_EN	BIT(0)
#define SC27XX_SLP_CTRL		0xdf0
#define SC27XX_LDO_XTL_EN	BIT(3)

/* UMP9620 (UMS9620/UMS9621's main PMIC), from the vendor's driver */
#define UMP9620_PWR_PD_HW	0x2020
#define UMP9620_SLP_CTRL	0x2248
#define UMP9620_LDO_XTL_EN	BIT(2)
#define UMP9620_SLP_LDO_PD_EN	BIT(0)

struct sc27xx_poweroff_data {
	u32 poweroff_reg;
	u32 slp_ctrl_reg;
	u32 slp_clear;		/* bits cleared in slp_ctrl_reg first; 0: write ldo_xtl_en */
	u32 ldo_xtl_en;
};

/* the SC2731, as the MFD cell "sc27xx-poweroff" has always powered off */
static const struct sc27xx_poweroff_data sc2731_data = {
	.poweroff_reg = SC27XX_PWR_PD_HW,
	.slp_ctrl_reg = SC27XX_SLP_CTRL,
	.ldo_xtl_en = SC27XX_LDO_XTL_EN,
};

static const struct sc27xx_poweroff_data ump9620_data = {
	.poweroff_reg = UMP9620_PWR_PD_HW,
	.slp_ctrl_reg = UMP9620_SLP_CTRL,
	.slp_clear = UMP9620_LDO_XTL_EN | UMP9620_SLP_LDO_PD_EN,
};

static struct regmap *regmap;
static const struct sc27xx_poweroff_data *pdata;

/*
 * On Spreadtrum platform, we need power off system through external SC27xx
 * series PMICs, and it is one similar SPI bus mapped by regmap to access PMIC,
 * which is not fast io access.
 *
 * So before stopping other cores, we need release other cores' resource by
 * taking cpus down to avoid racing regmap or spi mutex lock when poweroff
 * system through PMIC.
 */
static void sc27xx_poweroff_shutdown(void *data)
{
#ifdef CONFIG_HOTPLUG_CPU
	int cpu;

	for_each_online_cpu(cpu) {
		if (cpu != smp_processor_id())
			remove_cpu(cpu);
	}
#endif
}

static const struct syscore_ops poweroff_syscore_ops = {
	.shutdown = sc27xx_poweroff_shutdown,
};

static struct syscore poweroff_syscore = {
	.ops = &poweroff_syscore_ops,
};

static void sc27xx_poweroff_do_poweroff(void)
{
	/* Disable the external subsys connection's power firstly */
	if (pdata->slp_clear)
		regmap_update_bits(regmap, pdata->slp_ctrl_reg, pdata->slp_clear, 0);
	else
		regmap_write(regmap, pdata->slp_ctrl_reg, pdata->ldo_xtl_en);

	regmap_write(regmap, pdata->poweroff_reg, SC27XX_PWR_OFF_EN);
}

static int sc27xx_poweroff_probe(struct platform_device *pdev)
{
	if (regmap)
		return -EINVAL;

	pdata = of_device_get_match_data(&pdev->dev);
	if (!pdata)
		pdata = &sc2731_data;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap)
		return -ENODEV;

	pm_power_off = sc27xx_poweroff_do_poweroff;
	register_syscore(&poweroff_syscore);
	return 0;
}

/*
 * The PMICs whose power-off is a device tree node of their own (the UMS9621's
 * DT has "sprd,ump9620-poweroff" at 0x2020): without a driver for it, a power
 * off fell through to PSCI SYSTEM_OFF, which on that firmware resets.
 */
static const struct of_device_id sc27xx_poweroff_of_match[] = {
	{ .compatible = "sprd,ump9620-poweroff", .data = &ump9620_data },
	{ }
};
MODULE_DEVICE_TABLE(of, sc27xx_poweroff_of_match);

static struct platform_driver sc27xx_poweroff_driver = {
	.probe = sc27xx_poweroff_probe,
	.driver = {
		.name = "sc27xx-poweroff",
		.of_match_table = sc27xx_poweroff_of_match,
	},
};
module_platform_driver(sc27xx_poweroff_driver);

MODULE_DESCRIPTION("Power off driver for SC27XX PMIC Device");
MODULE_AUTHOR("Baolin Wang <baolin.wang@unisoc.com>");
MODULE_LICENSE("GPL v2");
