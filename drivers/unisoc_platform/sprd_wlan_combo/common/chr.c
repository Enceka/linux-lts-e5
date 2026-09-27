/*
* SPDX-FileCopyrightText: 2020-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#include "common/common.h"
#include "chr.h"
#include "wcn_bus.h"

/* This function is used to report open error evt from driver */
void sprd_chr_report_open_error(u32 evt_id, u8 err_code)
{
	unsigned char *reason_code = &err_code;

	sprdwcn_bus_chr_report(WCN_SOURCE_BTWF, WCN_CHR_ERROR, evt_id,
				 reason_code, sizeof(*reason_code));

}

void sprd_chr_report_mode_change(struct sprd_vif *vif, u32 evt_id, const u8 *mac)
{
	struct chr_mode_change *mode_ch = NULL;

	mode_ch = kzalloc(sizeof(*mode_ch), GFP_KERNEL);
	if (!mode_ch)
		return;

	mode_ch->mode = vif->mode;
	ether_addr_copy(mode_ch->mac_addr, mac);
	sprdwcn_bus_chr_report(WCN_SOURCE_BTWF, WCN_CHR_DEBUG, evt_id,
				 mode_ch, sizeof(*mode_ch));
	kfree(mode_ch);

}

/* This function is used to report chr_disconnect evt from CP2 */
void sprd_chr_report_disconnect(struct sprd_vif *vif, struct evt_chr *echr)
{
	unsigned char *reason_code = echr->evt_content;

	sprdwcn_bus_chr_report(WCN_SOURCE_BTWF, echr->loglevel, echr->evt_id,
				 reason_code, sizeof(*reason_code));

}
