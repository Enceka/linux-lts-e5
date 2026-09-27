// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#define pr_fmt(fmt) "sprd-backlight: " fmt

#include <linux/of.h>
#include <linux/backlight.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#include "sprd_bl.h"
#include "sprd_dpu.h"
#include "sysfs/sysfs_display.h"

void sprd_backlight_normalize_map(struct backlight_device *bd, u16 *level)
{
	struct sprd_backlight *bl = bl_get_data(bd);
	u64 mul;

	if (!bl->num) {
		if(!bd->props.brightness)
			*level = 0;
		else {
			mul = bl->max_level * bd->props.brightness;
			do_div(mul, bl->scale);
			*level = mul + bl->min_level;
		}
	} else
		*level = bl->levels[bd->props.brightness];
}

int sprd_cabc_backlight_update(struct backlight_device *bd)
{
	struct sprd_backlight *bl = bl_get_data(bd);
	struct pwm_state state;
	u64 duty_cycle;

	mutex_lock(&bd->update_lock);

	if (backlight_is_blank(bd)) {
		mutex_unlock(&bd->update_lock);
		return 0;
	}

	pr_debug("cabc brightness level: %u\n", bl->cabc_level);

	pwm_get_state(bl->pwm, &state);
	duty_cycle = bl->cabc_level * state.period;
	do_div(duty_cycle, bl->scale);
	state.duty_cycle = duty_cycle;
	pwm_apply_might_sleep(bl->pwm, &state);

	mutex_unlock(&bd->update_lock);

	return 0;
}

static int sprd_pwm_backlight_update(struct backlight_device *bd)
{
	struct sprd_backlight *bl = bl_get_data(bd);
	struct pwm_state state;
	u64 duty_cycle;
	u16 level;

	sprd_backlight_normalize_map(bd, &level);

	if (backlight_is_blank(bd))
		level = 0;

	pwm_get_state(bl->pwm, &state);
	if (level > 0) {
		if (bl->cabc_en) {
			if (bl->cabc_refer_level == 0)
				duty_cycle = level;
			else
				duty_cycle = DIV_ROUND_CLOSEST_ULL(bl->cabc_level *
					level, bl->cabc_refer_level);
		} else
			duty_cycle = level;

		pr_debug("pwm brightness level: %llu\n", duty_cycle);

		duty_cycle *= state.period;
		do_div(duty_cycle, bl->scale);
		state.duty_cycle = duty_cycle;
		state.enabled = true;
	} else {
		pr_debug("pwm brightness level: %u\n", level);

		state.duty_cycle = 0;
		state.enabled = false;
	}
	pwm_apply_might_sleep(bl->pwm, &state);

	return 0;
}

static const struct backlight_ops sprd_backlight_ops = {
	.update_status = sprd_pwm_backlight_update,
};

static int sprd_backlight_parse_dt(struct device *dev,
			struct sprd_backlight *bl)
{
	struct device_node *node = dev->of_node;
	struct property *prop;
	u32 value;
	int length;
	int ret;
	if (!node)
		return -ENODEV;

	/* determine the number of brightness levels */
	prop = of_find_property(node, "brightness-levels", &length);
	if (prop) {
		bl->num = length / sizeof(u32);

		/* read brightness levels from DT property */
		if (bl->num > 0) {
			size_t size = sizeof(*bl->levels) * bl->num;

			bl->levels = devm_kzalloc(dev, size, GFP_KERNEL);
			if (!bl->levels)
				return -ENOMEM;

			ret = of_property_read_u32_array(node,
							"brightness-levels",
							bl->levels, bl->num);
			if (ret < 0)
				return ret;
		}
	}

	ret = of_property_read_u32(node, "sprd,max-brightness-level", &value);
	if (!ret)
		bl->max_level = value;
	else
		bl->max_level = 255;

	ret = of_property_read_u32(node, "sprd,min-brightness-level", &value);
	if (!ret)
		bl->min_level = value;
	else
		bl->min_level = 0;

	ret = of_property_read_u32(node, "default-brightness-level", &value);
	if (!ret)
		bl->dft_level = value;
	else
		bl->dft_level = 25;

	ret = of_property_read_u32(node, "sprd,brightness-scale",
				   &value);
	if (!ret)
		bl->scale = value;
	else
		bl->scale = bl->max_level;

	DRM_INFO("bl num: %d, brightness max:%d, min:%d, dft:%d, scale:%d", bl->num,
		bl->max_level, bl->min_level, bl->dft_level, bl->scale);
	return 0;
}

static int sprd_backlight_device_create(struct sprd_backlight *bl,
				struct device *parent, struct backlight_device *bd)
{
	int ret;

	bl->dev.class = display_class;
	bl->dev.parent = parent;
	bl->dev.of_node = parent->of_node;
	dev_set_name(&bl->dev, "backlight");
	dev_set_drvdata(&bl->dev, bd);

	ret = device_register(&bl->dev);
	if (ret) {
		DRM_ERROR("backlight device register failed\n");
		return ret;
	}

	return 0;
}

static int sprd_backlight_probe(struct platform_device *pdev)
{
	struct backlight_device *bd;
	struct pwm_state state;
	struct sprd_backlight *bl;
	int div, ret;
	struct device_node *oled_bl_node, *lcd_node;

	lcd_node = sprd_get_panel_node_by_name();
	oled_bl_node = of_get_child_by_name(lcd_node, "oled-backlight");
	if (oled_bl_node) {
		DRM_INFO("use panel cabc backlight, do not register pwm backlight\n");
		return -ENODEV;
	}

	bl = devm_kzalloc(&pdev->dev,
			sizeof(struct sprd_backlight), GFP_KERNEL);
	if (!bl)
		return -ENOMEM;
	ret = sprd_backlight_parse_dt(&pdev->dev, bl);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to parse sprd backlight\n");
		return ret;
	}
	bl->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(bl->pwm)) {
		ret = PTR_ERR(bl->pwm);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "unable to request PWM\n");
		return ret;
	}
	pwm_init_state(bl->pwm, &state);

	ret = pwm_apply_might_sleep(bl->pwm, &state);
	if (ret) {
		dev_err(&pdev->dev, "failed to apply initial PWM state: %d\n",
			ret);
		return ret;
	}
	bd = devm_backlight_device_register(&pdev->dev,
			"sprd_backlight", &pdev->dev, bl,
			&sprd_backlight_ops, NULL);
	if (IS_ERR(bd)) {
		dev_err(&pdev->dev, "failed to register sprd backlight ops\n");
		return PTR_ERR(bd);
	}

	if(!bl->num) {
		bd->props.max_brightness = bl->scale;
		bd->props.brightness = bl->dft_level;
		bd->props.scale = BACKLIGHT_SCALE_LINEAR;
	} else {
		bd->props.max_brightness = bl->num - 1;
		div = ((bl->max_level - bl->min_level) << 8) / 255;
		if (div > 0) {
			bd->props.brightness = (bl->dft_level << 8) / div;
		} else {
			dev_err(&pdev->dev, "failed to calc default brightness level\n");
			return -EINVAL;
		}
	}

	bd->props.state &= ~BL_CORE_FBBLANK;
	bd->props.power = FB_BLANK_UNBLANK;

	DRM_INFO("Current Brightness:%d\n",bd->props.brightness);
	backlight_update_status(bd);

	platform_set_drvdata(pdev, bd);

	ret = sprd_backlight_device_create(bl, &pdev->dev, bd);
	if (ret)
		return ret;

	ret = sprd_backlight_sysfs_init(&bl->dev);
	if (ret)
		return ret;

	DRM_WARN("sprd_backlight_probe\n");
	return 0;
}

static const struct of_device_id sprd_backlight_of_match[] = {
	{ .compatible = "sprd,sharkl3-backlight" },
	{ .compatible = "sprd,sharkl5-backlight" },
	{ .compatible = "sprd,sharkl5pro-backlight" },
	{ .compatible = "sprd,sharkl6-backlight" },
	{ .compatible = "sprd,qogirn6pro-backlight"},
	{ .compatible = "sprd,qogirn6lite-backlight"},
	{ }
};

struct platform_driver sprd_backlight_driver = {
	.driver		= {
		.name		= "sprd-backlight",
		.of_match_table	= sprd_backlight_of_match,
	},
	.probe		= sprd_backlight_probe,
};

MODULE_AUTHOR("Kevin Tang <kevin.tang@unisoc.com>");
MODULE_DESCRIPTION("SPRD Base Backlight Driver");
MODULE_LICENSE("GPL v2");
