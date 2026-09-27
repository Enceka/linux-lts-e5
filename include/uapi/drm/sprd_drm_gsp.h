/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _SPRD_DRM_GSP_H_
#define _SPRD_DRM_GSP_H_

#include <drm/drm.h>
#include "gsp_cfg.h"

#define DRM_SPRD_GSP_GET_CAPABILITY	0
#define DRM_SPRD_GSP_TRIGGER	1

/*
 * The layout of both structures below is an ABI contract with the prebuilt
 * vendor HAL (android.hardware.graphics.composer@2.4-service) and must match
 * the Unisoc SDK headers it was compiled against - in particular the
 * 'version' field, which the HAL fills with its board string ("R9P0" on this
 * device) and which the stock 5.15.119 driver reads/compares.
 *
 * The Motorola-derived trees dropped 'version' from both structs.  That leaves
 * gsp_id (0) and size (4) at the same offsets, so sizes and the gsp_id lookup
 * still look correct, but cap lands at offset 8 instead of 40 - and offset 8 is
 * where the HAL stores the version string ("R9P0" on this board), so the driver
 * ends up passing those string bytes to copy_to_user() as a pointer and every
 * capability request fails with -EFAULT.  Verified against the stock binary: its
 * sprd_gsp_get_capability_ioctl loads cap with "ldr x20, [x20, #0x28]".
 */
struct drm_gsp_cfg_user {
	__u8 gsp_id;
	bool async;
	__u32 size;
	__u32 num;
	bool split;
	char version[32];
	void *config;
};

struct drm_gsp_capability {
	__u8 gsp_id;
	__u32 size;
	char version[32];
	void *cap;
};

#define DRM_IOCTL_SPRD_GSP_GET_CAPABILITY \
	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_SPRD_GSP_GET_CAPABILITY, \
		struct drm_gsp_capability)

#define DRM_IOCTL_SPRD_GSP_TRIGGER	\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_SPRD_GSP_TRIGGER, \
		struct drm_gsp_cfg_user)

#endif	/* _SPRD_DRM_GSP_H_ */
