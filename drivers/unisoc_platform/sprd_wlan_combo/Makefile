# SPDX-FileCopyrightText: 2016-2023 Unisoc (Shanghai) Technologies Co. Ltd
# SPDX-License-Identifier: GPL-2.0-only
#
# sprd_wlan_combo.ko - Unisoc WCN combo chip fullmac WLAN driver.
#
# Imported from android_kernel_modules_and_devicetree_oppo_ums9230
# (bsp/modules/kernel5.15/wcn/wlan/wlan_combo), adapted from its
# out-of-tree Kbuild to a normal in-tree obj-$(CONFIG_x) build. The
# object list below is the vendor Kbuild's unconditional default
# (common + sc2332 + sc2355, SDIO/SIPC/PCIE transports all built in) -
# this board only wires up sc2355 over SDIO (sprd,sc2355-sdio-wifi in
# e5-rongyue-overlay.dts), but stock ships one universal binary
# across chip generations rather than trimming per board, so this
# keeps that shape instead of guessing at what else is safe to drop.
#
# Left out: sc2355/pamwifi/ (gated on CONFIG_SPRD_PAMWIFI, which the
# vendor Kbuild only turns on for BSP_MODULE_ION_VERSION=ums9620 - a
# different SoC, not imported) and sc2355/dfs.o (gated on
# CONFIG_SPRD_DFS, off by default upstream too).

ccflags-y += -I$(srctree)/drivers/unisoc_platform/sprd_wlan_combo
ccflags-y += -I$(srctree)/drivers/unisoc_platform/sprd_wlan_combo/common
ccflags-y += -I$(srctree)/drivers/unisoc_platform/sprd_wlan_combo/sc2332
ccflags-y += -I$(srctree)/drivers/unisoc_platform/sprd_wlan_combo/sc2355
ccflags-y += -I$(srctree)/include/misc

obj-$(CONFIG_UNISOC_WLAN_COMBO) += sprd_wlan_combo.o

SPRD_WLAN_COMMON := \
	common/sprd_wlan.o \
	common/cfg80211.o \
	common/debug.o \
	common/delay_work.o \
	common/iface.o \
	common/msg.o \
	common/npi.o \
	common/report.o \
	common/apf.o \
	common/tdls.o \
	common/chr.o \
	common/wifi_config.o

SC2332_WLAN := \
	sc2332/acs.o \
	sc2332/init.o \
	sc2332/cmdevt.o \
	sc2332/qos.o \
	sc2332/scan.o \
	sc2332/tcp_ack.o \
	sc2332/txrx.o \
	sc2332/fcc.o \
	sc2332/vendor.o \
	sc2332/wapi.o

SC2332_SIPC_WLAN := sc2332/sipc.o

SC2355_WLAN := \
	sc2355/acs.o \
	sc2355/api_version.o \
	sc2355/cmdevt.o \
	sc2355/defrag.o \
	sc2355/hw_param.o \
	sc2355/init.o \
	sc2355/mm.o \
	sc2355/qos.o \
	sc2355/reorder.o \
	sc2355/rx.o \
	sc2355/scan.o \
	sc2355/tcp_ack.o \
	sc2355/tx.o \
	sc2355/fcc.o \
	sc2355/vendor.o \
	sc2355/rtt.o \
	sc2355/cpu_performance.o

SC2355_SDIO_WLAN := sc2355/sdio.o

SC2355_PCIE_WLAN := \
	sc2355/pcie.o \
	sc2355/pcie_buf.o

SC2355_SIPC_WLAN := \
	sc2355/hw_sipc_param.o \
	sc2355/sipc.o \
	sc2355/sipc_buf.o

SC2355_DFS_WLAN := sc2355/dfs.o

sprd_wlan_combo-y += \
	$(SPRD_WLAN_COMMON) \
	$(SC2332_WLAN) \
	$(SC2332_SIPC_WLAN) \
	$(SC2355_WLAN) \
	$(SC2355_SDIO_WLAN) \
	$(SC2355_SIPC_WLAN) \
	$(SC2355_PCIE_WLAN)

sprd_wlan_combo-$(CONFIG_SPRD_DFS) += $(SC2355_DFS_WLAN)
