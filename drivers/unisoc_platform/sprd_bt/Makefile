# SPDX-License-Identifier: GPL-2.0-only
#
# sprdbt_tty.ko - Unisoc WCN combo chip Bluetooth TTY driver.
#
# Imported from android_kernel_modules_and_devicetree_oppo_ums9230
# (bsp/modules/kernel5.15/wcn/bluetooth/driver/tty-515), adapted from
# its out-of-tree Kbuild to a normal in-tree obj-$(CONFIG_x) build.

ccflags-y += -I$(srctree)/include/misc

obj-$(CONFIG_UNISOC_WCN_BT) += sprdbt_tty.o

sprdbt_tty-y := \
	tty-515/lpm.o \
	tty-515/rfkill.o \
	tty-515/tty.o \
	tty-515/alignment/sitm.o
