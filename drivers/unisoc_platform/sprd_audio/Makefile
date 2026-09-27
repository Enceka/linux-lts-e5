# SPDX-License-Identifier: GPL-2.0
#
# Unisoc audio platform plumbing.
#
# Imported from android_kernel_modules_and_devicetree_oppo_ums9230
# (bsp/modules/kernel5.15/audio/sprd_audio), converting each module's
# out-of-tree Kbuild (KO_MODULE_NAME + obj-m) to a normal in-tree
# obj-$(CONFIG_x) build.  Module names are kept exactly as upstream builds
# them, because the stock vendor_dlkm modules.dep refers to them by name.

src_audio := $(srctree)/drivers/unisoc_platform/sprd_audio

subdir-ccflags-y += -I$(src_audio)/include
subdir-ccflags-y += -I$(src_audio)/audiomem
subdir-ccflags-y += -I$(src_audio)/audiosipc
subdir-ccflags-y += -I$(src_audio)/agdsp_pd

obj-$(CONFIG_UNISOC_AUDIO_MEM)			+= audiomem/
obj-$(CONFIG_UNISOC_AUDIO_AGDSP_PD)		+= agdsp_pd/
obj-$(CONFIG_UNISOC_AUDIO_SIPC)			+= audiosipc/
obj-$(CONFIG_UNISOC_AUDIO_PIPE)			+= audiopipe/
obj-$(CONFIG_UNISOC_AUDIO_APIPE)		+= audio_pipe/
obj-$(CONFIG_UNISOC_AUDIO_CP_BOOT)		+= audiocpboot/
obj-$(CONFIG_UNISOC_AUDIO_CP_DVFS)		+= audiodvfs/
obj-$(CONFIG_UNISOC_AUDIO_DSP_DUMP)		+= audiodspdump/
obj-$(CONFIG_UNISOC_AUDIO_VOICE_TRIGGER_IRQ)	+= voice_trigger_irq/
obj-$(CONFIG_UNISOC_AUDIO_MCDT_R2P0)		+= mcdt/mcdt_r2p0/
