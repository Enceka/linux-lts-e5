# SPDX-License-Identifier: GPL-2.0
#
# Awinic AW87xxx speaker PA.
#
# Module name follows the stock vendor_dlkm (snd-soc-sprd-pa-aw87xxx.ko), not
# upstream's snd-soc-aw87xxx - the same convention the vendor uses for the
# other external PAs (e.g. snd-soc-sprd-pa-fs1815n).
obj-$(CONFIG_SND_SOC_UNISOC_PA_AW87XXX) += snd-soc-sprd-pa-aw87xxx.o
snd-soc-sprd-pa-aw87xxx-y := aw87xxx.o aw87xxx_device.o aw87xxx_monitor.o \
			     aw87xxx_bin_parse.o aw87xxx_dsp.o aw87xxx_acf_bin.o
