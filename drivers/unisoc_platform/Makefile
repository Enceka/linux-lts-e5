# SPDX-License-Identifier: GPL-2.0-only
# Unisoc's drivers export functions they declare nowhere (the vendor tree has no prototypes for most of them):
# 6.8's default -Wmissing-prototypes would report every one.
subdir-ccflags-y += -Wno-missing-prototypes -Wno-missing-declarations

obj-y				+= modem/
obj-y				+= net/
obj-y				+= sprd_time_sync/
obj-$(CONFIG_UNISOC_WCN_BSP)	+= sprdwcn/
obj-$(CONFIG_UNISOC_WLAN_COMBO)	+= sprd_wlan_combo/
obj-$(CONFIG_UNISOC_WCN_BT)	+= sprd_bt/
obj-$(CONFIG_UNISOC_DISP)	+= sprd_disp/
obj-$(CONFIG_UNISOC_IOMMU)	+= iommu/
obj-$(CONFIG_UNISOC_SOC_THERMAL)	+= thermal/
obj-$(CONFIG_SPRD_USBPINMUX)	+= usbpinmux/
