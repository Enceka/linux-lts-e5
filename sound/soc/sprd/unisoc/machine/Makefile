# SPDX-License-Identifier: GPL-2.0
ccflags-y += -I$(srctree)/sound/soc/sprd/unisoc/codec/ump9620/include
ccflags-y += -DCONFIG_SND_SOC_UNISOC_CODEC_UMP9620
obj-$(CONFIG_SND_SOC_UNISOC_CARD) += snd-soc-sprd-card.o
# Non-extpa variant: the stock snd-soc-sprd-card.ko was built from
# sprd-asoc-card-utils-hook.c / -legacy.c (verified from its DWARF), i.e.
# upstream's BSP_BOARD_AUDIO_EXTPA=false branch.
snd-soc-sprd-card-y := vbc-rxpx-codec-sc27xx.o \
		       sprd-asoc-common.o \
		       sprd-asoc-card-utils.o \
		       sprd-asoc-card-utils-hook.o \
		       sprd-asoc-card-utils-legacy.o
