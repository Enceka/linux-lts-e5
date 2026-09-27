# SPDX-License-Identifier: GPL-2.0
#
# Unisoc vendor ASoC stack.
#
# Imported from android_kernel_modules_and_devicetree_oppo_ums9230
# (bsp/modules/kernel5.15/audio/sprd and vender/audio/fsa4480), converting
# each module's out-of-tree Kbuild (KO_MODULE_NAME + obj-m) to a normal
# obj-$(CONFIG_x) build.  Module names match what upstream builds, because the
# stock vendor_dlkm modules.dep refers to them by name.
#
# The per-module -D flags below are not the union of what upstream's Kbuilds
# can define: those are switched on BSP_KERNEL_BUILD_CONFIG, and only the
# build.config.gki.aarch64.ums9621_ branch applies here.
# (the vendor code declares most of what it shares between its modules nowhere, as in drivers/unisoc_platform)
subdir-ccflags-y += -Wno-missing-prototypes -Wno-missing-declarations

snd_unisoc := $(srctree)/sound/soc/sprd/unisoc
sprd_audio := $(srctree)/drivers/unisoc_platform/sprd_audio

subdir-ccflags-y += -I$(snd_unisoc)/include
subdir-ccflags-y += -I$(snd_unisoc)/platform/include
subdir-ccflags-y += -I$(sprd_audio)/agdsp_pd
subdir-ccflags-y += -I$(sprd_audio)/audiomem
subdir-ccflags-y += -I$(sprd_audio)/audiosipc
subdir-ccflags-y += -I$(sprd_audio)/mcdt

obj-$(CONFIG_SND_SOC_UNISOC_PCM_ROUTING)		+= platform/platform_routing/
obj-$(CONFIG_SND_SOC_UNISOC_DMAENGINE_PCM)		+= platform/dmaengine_pcm/
obj-$(CONFIG_SND_SOC_UNISOC_COMPR_2STAGE_DMA)		+= platform/compr_2stage_dma/
obj-$(CONFIG_SND_SOC_UNISOC_VBC_V4)			+= dai/vbc_v4/vbc_dai/
obj-$(CONFIG_SND_SOC_UNISOC_VBC_FE)			+= dai/vbc_v4/fe_dai/
obj-$(CONFIG_SND_SOC_UNISOC_TDM)			+= dai/tdm/
obj-$(CONFIG_SND_SOC_UNISOC_DUMMY_CODEC)		+= codec/dummy-codec/
obj-$(CONFIG_SND_SOC_UNISOC_HEADSET_FSA4480)		+= fsa4480/
obj-$(CONFIG_SND_SOC_UNISOC_PA_AW87XXX)			+= codec/aw87xxx/
obj-$(CONFIG_SND_SOC_UNISOC_CODEC_UMP9620)		+= codec/ump9620/codec/
obj-$(CONFIG_SND_SOC_UNISOC_CODEC_UMP9620_POWER)	+= codec/ump9620/power/
obj-$(CONFIG_SND_SOC_UNISOC_CODEC_UMP9620_POWER_DEV)	+= codec/ump9620/power_dev/
obj-$(CONFIG_SND_SOC_UNISOC_PDM_R2P0)			+= codec/pdm_r2p0/
obj-$(CONFIG_SND_SOC_UNISOC_CARD)			+= machine/
