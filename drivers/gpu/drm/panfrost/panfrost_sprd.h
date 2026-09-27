/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Unisoc (Spreadtrum) platform glue for panfrost.
 *
 * Copyright (C) 2026 The e5-linux project
 */

#ifndef __PANFROST_SPRD_H__
#define __PANFROST_SPRD_H__

struct panfrost_device;

int panfrost_sprd_init(struct panfrost_device *pfdev);
void panfrost_sprd_fini(struct panfrost_device *pfdev);

#endif /* __PANFROST_SPRD_H__ */
