BSP_MODULE_DISP_VERSION ?= ums9621

#SPDX-FileCopyrightText: 2020 Unisoc (Shanghai) Technologies Co.Ltd
#SPDX-License-Identifier: GPL-2.0-only
#
# Configurations
#
ifeq ($(BSP_MODULE_DISP_VERSION),sp7731e)
CONFIG_UNISOC_DISP := m
CONFIG_DRM_SPRD_DPU0 := y
CONFIG_DRM_SPRD_DSI := y
CONFIG_DRM_SPRD_DUMMY := n
CONFIG_DRM_SPRD_WB_DEBUG := n
CONFIG_UNISOC_GSP := m
endif

ifeq ($(BSP_MODULE_DISP_VERSION),sp9832e)
CONFIG_UNISOC_DISP := m
CONFIG_DRM_SPRD_DPU0 := y
CONFIG_DRM_SPRD_DSI := y
CONFIG_DRM_SPRD_DUMMY := n
CONFIG_DRM_SPRD_WB_DEBUG := n
CONFIG_UNISOC_GSP := m
endif

ifeq ($(BSP_MODULE_DISP_VERSION),s9863a)
CONFIG_UNISOC_DISP := m
CONFIG_DRM_SPRD_DPU0 := y
CONFIG_DRM_SPRD_DSI := y
CONFIG_DRM_SPRD_DUMMY := n
CONFIG_DRM_SPRD_WB_DEBUG := n
CONFIG_UNISOC_GSP := m
endif

ifeq ($(BSP_MODULE_DISP_VERSION),ums312)
CONFIG_UNISOC_DISP := m
CONFIG_DRM_SPRD_DPU0 := y
CONFIG_DRM_SPRD_DSI := y
CONFIG_DRM_SPRD_DUMMY := n
CONFIG_DRM_SPRD_WB_DEBUG := n
CONFIG_UNISOC_GSP := m
endif

ifeq ($(BSP_MODULE_DISP_VERSION),ums512)
CONFIG_UNISOC_DISP := m
CONFIG_DRM_SPRD_DPU0 := y
CONFIG_DRM_SPRD_DSI := y
CONFIG_DRM_SPRD_DUMMY := n
CONFIG_DRM_SPRD_WB_DEBUG := n
CONFIG_UNISOC_GSP := m
endif

ifeq ($(BSP_MODULE_DISP_VERSION),ums9230)
CONFIG_UNISOC_DISP := m
CONFIG_DRM_SPRD_DPU0 := y
CONFIG_DRM_SPRD_DSI := y
CONFIG_DRM_SPRD_DUMMY := n
CONFIG_DRM_SPRD_WB_DEBUG := n
CONFIG_UNISOC_GSP := m
CONFIG_DRM_SPRD_UMB9230S := y
endif

ifeq ($(BSP_MODULE_DISP_VERSION),ums9610)
CONFIG_UNISOC_DISP := m
CONFIG_DRM_SPRD_DPU0 := y
CONFIG_DRM_SPRD_DSI := y
CONFIG_DRM_SPRD_DUMMY := n
CONFIG_DRM_SPRD_WB_DEBUG := n
CONFIG_UNISOC_GSP := m
endif

ifeq ($(BSP_MODULE_DISP_VERSION),ums9621)
# In-tree build: leave CONFIG_UNISOC_DISP / CONFIG_UNISOC_GSP at their
# Kconfig values (=y) instead of forcing modules like the out-of-tree flow.
endif

ifeq ($(BSP_MODULE_DISP_VERSION),ums9620)
CONFIG_UNISOC_DISP := m
CONFIG_DRM_SPRD_DPU0 := y
CONFIG_DRM_SPRD_DP := y
CONFIG_DRM_SPRD_DPU1 := y
CONFIG_DRM_SPRD_DSI := y
CONFIG_DRM_SPRD_DUMMY := n
CONFIG_DRM_SPRD_WB_DEBUG := n
CONFIG_UNISOC_GSP := m
endif


#
# Source List
#
sprd-drm-y := \
	disp_lib.o \
	sprd_drm.o \
	sprd_dsc.o \
	sprd_gem.o \
	sprd_plane.o

INCLUDE_SUBDIR = \
	$(src)/pike2/Kbuild \
	$(src)/qogirl6/Kbuild \
	$(src)/qogirn6pro/Kbuild \
	$(src)/qogirn6lite/Kbuild \
	$(src)/sharkl3/Kbuild \
	$(src)/sharkl5/Kbuild \
	$(src)/sharkl5pro/Kbuild \
	$(src)/sharkle/Kbuild \

#
# Build Options
#
ccflags-y := \
	-I$(srctree)/include/linux \
	-I$(srctree)/drivers/staging/android \
	-I$(src)

subdir-ccflags-y += -I$(srctree)/drivers/devfreq/apsys/

#
# Conditional Compilation
#
ifneq ($(filter y m,$(CONFIG_UNISOC_DISP)),)
    ccflags-y += -DCONFIG_UNISOC_DISP=$(CONFIG_UNISOC_DISP)
endif

ifeq ($(CONFIG_DRM_SPRD_DPU0),y)
	sprd-drm-y += \
		sprd_crtc.o \
		sprd_dpu.o \
		sprd_bl.o \
		sprd_corner.o
	INCLUDE_SUBDIR += \
		$(src)/dpu/Kbuild \
		$(src)/sysfs/Kbuild
    # (CONFIG_DRM_SPRD_DPU0 is a Kconfig bool: autoconf.h defines it in-tree)
endif

ifeq ($(CONFIG_DRM_SPRD_DP),y)
	sprd-drm-y += \
		sprd_dp.o \
		qogirn6pro/global_dp.o
	INCLUDE_SUBDIR += $(src)/dp/Kbuild
    ccflags-y += -DCONFIG_DRM_SPRD_DP=$(CONFIG_DRM_SPRD_DP)
endif

ifeq ($(CONFIG_DRM_SPRD_DPU1),y)
	sprd-drm-y += \
		sprd_dpu1.o \
		dpu/dpu_lite_r3p0.o \
		qogirn6pro/global_dpu1.o
    ccflags-y += -DCONFIG_DRM_SPRD_DPU1=$(CONFIG_DRM_SPRD_DPU1)
endif

ifeq ($(CONFIG_DRM_SPRD_DSI),y)
	sprd-drm-y += \
		sprd_dsi.o \
		sprd_dphy.o \
		sprd_dsi_panel.o
	INCLUDE_SUBDIR += \
		$(src)/dphy/Kbuild \
		$(src)/dsi/Kbuild
    # (CONFIG_DRM_SPRD_DSI is a Kconfig bool: autoconf.h defines it in-tree)
endif

ifeq ($(CONFIG_DRM_SPRD_UMB9230S),y)
    INCLUDE_SUBDIR += $(src)/umb9230s/Kbuild
    ccflags-y += -DCONFIG_DRM_SPRD_UMB9230S=$(CONFIG_DRM_SPRD_UMB9230S)
endif

ifeq ($(CONFIG_DRM_SPRD_DUMMY),y)
    INCLUDE_SUBDIR += $(src)/dummy/Kbuild
    # (CONFIG_DRM_SPRD_DUMMY is a Kconfig bool: autoconf.h defines it in-tree)
endif

ifeq ($(CONFIG_DRM_SPRD_WB_DEBUG),y)
ifeq ($(TARGET_BUILD_VARIANT),userdebug)
    # (CONFIG_DRM_SPRD_WB_DEBUG is a Kconfig bool: autoconf.h defines it in-tree)
endif
endif

ifneq ($(filter y m,$(CONFIG_UNISOC_GSP)),)
    ccflags-y += -DCONFIG_UNISOC_GSP=$(CONFIG_UNISOC_GSP)
endif

ifneq ($(filter y m,$(CONFIG_TRUSTY_VIRTIO_IPC)),)
    sprd-drm-y += disp_trusty.o
endif

ifeq ($(CONFIG_COMPAT),y)
    sprd-drm-y += sprd_ioc32.o
endif

include $(INCLUDE_SUBDIR)

#
# Final Objects
#
obj-$(CONFIG_UNISOC_DISP) := sprd-drm.o

# 5.15.211 backports the generic MIN/MAX/in_range macros; this driver
# predates them and defines its own versions of the same names.
ccflags-y += -Wno-macro-redefined -Wno-frame-larger-than
subdir-ccflags-y += -Wno-macro-redefined -Wno-frame-larger-than

# LCD bias chip (OCP2131), called from sprd_panel_prepare(). Built as its own
# module: it has its own module_init, so listing it in sprd-drm-y would
# duplicate init_module/cleanup_module.
obj-m += lcd_bias/ocp2131.o

# GSP: referenced by sprd_drm ioctls and the panel path. The out-of-tree flow
# builds sprd-gsp.ko on its own, but gsp/Kbuild ends with
# "obj-$(CONFIG_UNISOC_GSP) := sprd-gsp.o", so it builds in-tree once wired up.
obj-$(CONFIG_UNISOC_GSP) += gsp/
