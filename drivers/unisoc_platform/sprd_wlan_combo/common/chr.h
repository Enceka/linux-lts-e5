/*
* SPDX-FileCopyrightText: 2020-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef __CHR_H__
#define __CHR_H__

#define CHR_CP2_DATA_LEN		11

#define CHR_OPENERR_FLAGSET(A, B)	(*A = B)

/*
 * struct evt_chr- the chr_evt data format from CP2 uploading
 *
 * @loglevel: event print level
 * @evt_id: the chr_evt's id
 * @evt_id_subtype: reserve for future
 * @evt_content_len: the evt_content len
 * @evt_content: point to the chr event content
 *  CP2 define the evt_content size is 100bytes
 */
struct evt_chr {
	u8 loglevel;
	u32 evt_id;
	u32 evt_id_subtype;
	u8 evt_content_len;
	u8 *evt_content;
} __packed;


/*
 * the flag just used in sprd_iface_set_power to
 * determine whether open_err evt has occurred
 */
enum OPEN_ERR_LIST {
	OPEN_ERR_INIT = 0,
	OPEN_ERR_POWER_ON,
	OPEN_ERR_DOWNLOAD_INI
};

/* The following are the evt_id for each chr_evt */
enum REPORT_CHR_LIST {
	/* chr event From Driver */
	EVT_CHR_DRV_MIN = 0x00000,
	EVT_CHR_OPEN_ERR = 0x00001,
	EVT_CHR_MODE_OPEN,
	EVT_CHR_MODE_CLOSE = 0x00003,
	EVT_CHR_DRV_MAX = 0x0FFFF,

	/* chr event From CP2 */
	EVT_CHR_FW_MIN = 0X13001,
	EVT_CHR_DISC_LINK_LOSS = EVT_CHR_FW_MIN,
	EVT_CHR_DISC_SYS_ERR,
	EVT_CHR_FW_MAX = 0x1FFFF,
};

struct chr_mode_change {
	u8 mode;
	u8 mac_addr[ETH_ALEN];
};

void sprd_chr_report_mode_change(struct sprd_vif *vif, u32 evt_id, const u8 *mac);
void sprd_chr_report_disconnect(struct sprd_vif *vif, struct evt_chr *echr);
/* This function is used to report open error evt from driver */
void sprd_chr_report_open_error(u32 evt_id, u8 err_code);

#endif

