/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The legacy GPIO calls Unisoc's 5.15 drivers make that 6.x no longer has, in terms of what it has: the
 * drivers keep their calls and get this header (upstream/tools/port-api.py adds it).
 */
#ifndef __LINUX_SOC_SPRD_UNISOC_GPIO_COMPAT_H
#define __LINUX_SOC_SPRD_UNISOC_GPIO_COMPAT_H

#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <dt-bindings/gpio/gpio.h>

/* devm_gpio_request(): gpio_request(), freed with the device; the direction is left alone, as it was */
static inline void unisoc_devm_gpio_free(void *gpio)
{
	gpio_free((unsigned long)gpio);
}

static inline int unisoc_devm_gpio_request(struct device *dev, unsigned int gpio, const char *label)
{
	int ret = gpio_request(gpio, label);

	if (ret)
		return ret;
	return devm_add_action_or_reset(dev, unisoc_devm_gpio_free, (void *)(unsigned long)gpio);
}
#define devm_gpio_request(dev, gpio, label)	unisoc_devm_gpio_request(dev, gpio, label)

/* of_get_named_gpio_flags(): the GPIO number, and OF_GPIO_ACTIVE_LOW when the specifier says so */
enum of_gpio_flags {
	OF_GPIO_ACTIVE_LOW = 0x1,
};

static inline int unisoc_of_get_named_gpio_flags(const struct device_node *np, const char *list_name,
						 int index, enum of_gpio_flags *flags)
{
	struct of_phandle_args args;
	int ret;

	if (flags)
		*flags = 0;
	ret = of_parse_phandle_with_args(np, list_name, "#gpio-cells", index, &args);
	if (ret)
		return ret;
	of_node_put(args.np);
	if (flags && args.args_count > 1 && (args.args[1] & GPIO_ACTIVE_LOW))
		*flags = OF_GPIO_ACTIVE_LOW;
	return of_get_named_gpio(np, list_name, index);
}
#define of_get_named_gpio_flags(np, name, index, flags) \
	unisoc_of_get_named_gpio_flags(np, name, index, flags)
#define of_get_gpio_flags(np, index, flags) \
	unisoc_of_get_named_gpio_flags(np, "gpios", index, flags)

#endif
