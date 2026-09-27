# SPDX-License-Identifier: GPL-2.0
#
# Chipsemi tlsc6x touchscreen: chip driver + our minimal core.
# Produces tlsc6x.ko, the name the stock first-stage modules.load expects.
#

obj-$(CONFIG_TOUCHSCREEN_TLSC6X)	+= tlsc6x.o

tlsc6x-objs :=		tlsc6x_main.o \
			tlsc6x_comp.o \
			tlsc6x_common_interface.o \
			tlsc6x_core.o

ccflags-y += -I$(srctree)/drivers/input/touchscreen/tlsc6x
ccflags-y += -I$(srctree)/drivers/input/touchscreen/tlsc6x/firmware
ccflags-y += -I$(srctree)/drivers/input/touchscreen/tlsc6x/tlsc_chip3535
ccflags-y += -I$(srctree)/drivers/input/touchscreen/tlsc6x/tlsc_chip3536

# (the chip driver declares what it shares between its files nowhere)
ccflags-y += -Wno-missing-prototypes -Wno-missing-declarations
