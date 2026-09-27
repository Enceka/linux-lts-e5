# SPDX-License-Identifier: GPL-2.0
# Upstream Kbuild defines this unconditionally; the driver's own headers key
# register layout off it.
ccflags-y += -DCONFIG_UNISOC_AUDIO_MCDT_R2P0
ccflags-y += -I$(srctree)/drivers/unisoc_platform/sprd_audio/mcdt
obj-$(CONFIG_UNISOC_AUDIO_MCDT_R2P0) += mcdt_hw_r2p0.o
