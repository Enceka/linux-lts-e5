#
# Makefile for trusty components
#

# (the vendor code declares most of what it exports nowhere, as in drivers/unisoc_platform)
ccflags-y += -Wno-missing-prototypes -Wno-missing-declarations

obj-$(CONFIG_TRUSTY) += trusty.o
trusty-y := trusty-core.o trusty-mem.o

obj-$(CONFIG_TRUSTY) += trusty-irq.o
obj-$(CONFIG_TRUSTY) += trusty-pm.o
obj-$(CONFIG_TRUSTY_LOG) += trusty-log.o
obj-$(CONFIG_TRUSTY_VIRTIO) += trusty-virtio.o
obj-$(CONFIG_TRUSTY_VIRTIO_IPC) += trusty-ipc.o
obj-$(CONFIG_SPRD_TSHM) += sprd_shm.o
obj-$(CONFIG_TRUSTY_TUI) += trusty-tui.o
