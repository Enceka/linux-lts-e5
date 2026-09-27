/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

int umb9230s_gpio_request(struct device *dev);
void umb9230s_init(void);
void umb9230s_gpio_reset(int flag);
void umb9230s_set_gpio_power(int flag);
void intr_irq_switch(bool flag);
int intr_irq_registration(void *data);
void intr_irq_free(void *data);
