/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The part of Unisoc's sysdump interface (drivers/unisoc_platform/sysdump/sysdump.h of its 5.15 tree) that
 * the ported drivers use.  sysdump itself -- a RAM dump for the bootloader when the kernel panics, into which
 * SIPC hooks the CP's "die" message -- is not ported; without it the registration is a no-op.
 */
#ifndef __SPRD_PLATFORM_SYSDUMP_H
#define __SPRD_PLATFORM_SYSDUMP_H

#include <linux/types.h>

#if IS_ENABLED(CONFIG_UNISOC_SYSDUMP)
void sysdump_callback_register(int (*callback)(u8 dst));
void sysdump_callback_unregister(void);
#else
static inline void sysdump_callback_register(int (*callback)(u8 dst)) { }
static inline void sysdump_callback_unregister(void) { }
#endif

#endif
