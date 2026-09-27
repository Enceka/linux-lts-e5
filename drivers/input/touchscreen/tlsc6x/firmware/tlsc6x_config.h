
/************************************************************************
*
* File Name: tlsc6x_config.h
*
*  Abstract: global configurations - Rongyue E5 (ums9158_1h10)
*
*   Version: v1.0
*
* Derived from the upstream ZTE board configs (chestnut/dates/pitaya/plum,
* which are all byte-identical to each other).  Differences for the E5 are
* marked below; everything else is kept as upstream had it.
*
************************************************************************/
#ifndef _LINUX_TLSC6X_CONFIG_H_
#define _LINUX_TLSC6X_CONFIG_H_

/*
 * E5: no proximity sensor.  The board's tlsc6x@2e node carries only
 * TP_MAX_X/TP_MAX_Y/have-virtualkey and the two gpios - there is no
 * proximity property, and this is a 320x480 CMCC data-terminal, not a
 * handset held to a face.  Leaving TLSC_TPD_PROXIMITY on would register a
 * "tp_ps" class plus a psensor input device that nothing ever reads.
 */
/* #define TLSC_TPD_PROXIMITY */
#define TLSC_APK_DEBUG		/* apk debugger, close:undef */

/*
 * E5: auto firmware upgrade disabled.
 *
 * comp_upd_bin.h carries ZTE project firmware, not E5 firmware.  The
 * upgrade path is in fact guarded - tlsc6x_tpcfg_ver_comp() requires the
 * i2c address embedded in the config blob to match, the checksum to pass,
 * and the low 26 bits of the config version (the project id) to be equal,
 * and it bails out entirely when the running config version reads 0, which
 * is exactly what the stock driver reports on this device
 * ("Config version = 0" in tlsc_tp_info).  So this would almost certainly
 * no-op rather than misflash.  Still, flashing a touch controller is not
 * something to leave armed on a guess during bring-up: get input events
 * working first, then revisit if the panel actually needs a config update.
 */
/* #define TLSC_AUTO_UPGRADE */
#define TLSC_ESD_HELPER_EN	/* esd helper, close:undef */
/* #define TLSC_GESTRUE */
#define TLSC_TP_PROC_SELF_TEST

/* #define TLSC_BUILDIN_BOOT */
/* #define TLSC_CHIP_NAME "chsc6440" */
#define CONFIG_TLSC_POINT_REPORT_CHECK
/*
 * E5: report touches straight from the driver (input_mt_* / input_report_abs).
 * The alternative path handed coordinates to the vendor framework's algorithm
 * layer, which this build no longer carries - see tlsc6x_core.h.
 */
/* #define TLSC_REPORT_BY_ZTE_ALGO */
/* E5: paired with TLSC_TPD_PROXIMITY above - no proximity hardware. */
#define HUB_TP_PS_ENABLE 0
#endif /* _LINUX_TLSC6X_CONFIG_H_ */
