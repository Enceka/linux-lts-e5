/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include "kernel_gpio_fun.h"
#include "umb9230s.h"

#define INTR_IRQ_NAME                     "umb_intr"

static struct gpio_desc *ext_rst = NULL, *xbuf_pd = NULL, *intr = NULL;
static u32 irq_gpio_flags = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
static int intr_irq, intr_gpio;
static bool intr_irq_flags;

  /**
   * umb9230s_gpio_request() - request gpio for umb9230s
   * @dev: input device
   *
   * request and init gpio of umb9230s. umb9230s_init() will
   * be executed after this func.
   *
   * Returns:
   * On successful request and init the GPIO with return 0
   *
   * In case of error PTR_ERR is returned.
   */
int umb9230s_gpio_request(struct device *dev)
{
	xbuf_pd = devm_gpiod_get_index(dev, "umb", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(xbuf_pd)) {
		pr_err("Failed to request xbuf_pd.\n");
		return PTR_ERR(xbuf_pd);
	}

	ext_rst = devm_gpiod_get_index(dev, "umb", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ext_rst)) {
		pr_err("Failed to request ext_rst.\n");
		return PTR_ERR(ext_rst);
	}

	intr = devm_gpiod_get_index(dev, "umb", 2, GPIOD_IN);
	if (IS_ERR(intr)) {
		pr_err("Failed to request intr.\n");
		return PTR_ERR(intr);
	}

	return 0;
}

   /**
   * umb9230s_init() - gpio init for umb9230s
   *
   * xbuf_pd gpio except to power umb9230. ext_rst gpio except to
   * reset umb9230s. other modules will be init after this func.
   *
   */
void umb9230s_init(void)
{
	gpiod_set_raw_value(xbuf_pd, 0);
	udelay(100);
	gpiod_set_raw_value(ext_rst, 1);
}

  /**
   * umb9230s_gpio_reset() - ext_rst gpio of umb9230s
   * @flag:	GPIO output high level with 1. Output low level with 0
   *
   * ext_rst gpio connect gpio171 of L6. except to reset umb9230s and
   * high level be output when umb9230 work.
   */
void umb9230s_gpio_reset(int flag)
{
	gpiod_set_raw_value(ext_rst, flag);
}

  /**
   * umb9230s_set_gpio_power() - xbuf_pd gpio of umb9230s
   * @flag: GPIO output high level with 1. Output low level with 0
   *
   * xbuf_pd gpio connect gpio172 of L6. except to power umb9230 and
   * low level be output when umb9230s work.
   */
void umb9230s_set_gpio_power(int flag)
{
	gpiod_set_raw_value(xbuf_pd, flag);
}

  /**
   * irq_handler_func() - irq work func
   */
static irqreturn_t irq_handler_func(int irq, void *data)
{
	umb9230s_isr(data);

	return IRQ_HANDLED;
}

   /**
   * intr_irq_switch() - switch irq state
   * @flag:	switch irq to open with true. switch irq to close with false.
   */
void intr_irq_switch(bool flag)
{
	if (!intr_irq)
		return;

	if (flag) {
		if (!intr_irq_flags) {
			enable_irq(intr_irq);
			intr_irq_flags = true;
		}
	} else {
		if (intr_irq_flags) {
			disable_irq(intr_irq);
			intr_irq_flags = false;
		}
	}
}

   /**
   * intr_irq_registration() - register irq of umb9230
   *
   * Returns:
   * On successful register with return 0
   *
   * In case of error Non-0 is returned.
   */
int intr_irq_registration(void *data)
{
	int ret;

	intr_gpio = desc_to_gpio(intr);
	intr_irq = gpio_to_irq(intr_gpio);

	ret = request_threaded_irq(intr_irq, NULL, irq_handler_func,
					irq_gpio_flags, INTR_IRQ_NAME, data);
	if (ret != 0) {
		pr_err("Failed to registrate intr.\n");
		intr_irq = 0;
		return ret;
	} else {
		pr_err("succeed to registrate intr irq.\n");
		intr_irq_flags = true;
	}

	return ret;
}

void intr_irq_free(void *data)
{
	if (intr_irq)
		free_irq(intr_irq, data);
}
