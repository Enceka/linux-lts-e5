/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef __SCAN_H__
#define __SCAN_H__

#include <net/cfg80211.h>

#define SPRD_MAX_SCAN_REQ_IE_LEN	(255)
#define SPRD_MIN_RSSI_THOLD		(-127)

void sc2332_report_scan_result(struct sprd_vif *vif, u16 chan, s16 rssi,
			       u8 *frame, u16 len);

#endif
