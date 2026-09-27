/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _SYSFS_DISPLAY_H_
#define _SYSFS_DISPLAY_H_

#include <linux/device.h>

extern struct class *display_class;

#ifdef CONFIG_DRM_SPRD_DPU0
int sprd_display_class_init(void);
#else
static inline int sprd_display_class_init(void)
{
    return 0;
}
#endif

int sprd_backlight_sysfs_init(struct device *dev);
int sprd_dpu_sysfs_init(struct device *dev);
int sprd_dsi_sysfs_init(struct device *dev);
int sprd_dphy_sysfs_init(struct device *dev);
int sprd_panel_sysfs_init(struct device *dev);

void sprd_dpu_sysfs_deinit(struct device *dev);
void sprd_dsi_sysfs_deinit(struct device *dev);
void sprd_dphy_sysfs_deinit(struct device *dev);

#endif
