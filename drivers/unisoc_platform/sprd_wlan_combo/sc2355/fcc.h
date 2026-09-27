/*
* SPDX-FileCopyrightText: 2020-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef __FCC_H__
#define __FCC_H__

#define MAX_PHY_MODE	7
#define MAX_POWER_BACKOFF_RULE	100
#define MAX_FCC_COUNTRY_NUM	91
#define MAX_FCC_COUNTRY_NUM_HULK	81
#define MAX_FCC_COUNTRY_NUM_DHAKA	98
#define MAX_FCC_COUNTRY_NUM_NAIROBI	120
#define MAX_FCC_COUNTRY_NUM_LATTE_T	98
#define MAX_FCC_COUNTRY_NUM_DHAKA_A	98
#define MAX_FCC_COUNTRY_NUM_LATTE_M	98
#define MAX_FCC_COUNTRY_NUM_NAIROBI_A	120
#define MAX_FCC_COUNTRY_NUM_CRUISERB4	102

struct sprd_priv;

struct sprd_power_backoff {
	u8 subtype;
	u8 channel;
	u8 bw;
	u8 power_rule[MAX_PHY_MODE][2];
} __packed;

struct fcc_power_bo {
	char country[3];
	u8 num;
	struct sprd_power_backoff power_backoff[MAX_POWER_BACKOFF_RULE];
} __packed;

struct fresh_bo_info {
	u8 pw_channel;
	u8 pw_bw;
};

struct sprd_fcc_priv {
	struct mutex lock;/* protects the FCC data */
	struct fcc_power_bo *cur_power_bo;
	bool flag;
	u8 channel[2];
	u8 bw[2];
};

void sc2355_fcc_fresh_bo_work(struct sprd_vif *vif, void *data, u16 len);
void sc2355_fcc_match_country(struct sprd_priv *priv, const char *alpha2);
void sc2355_fcc_reset_bo(struct sprd_priv *priv, struct sprd_vif *vif);
void sc2355_fcc_init(void);
void sc2355_init_5g_sar_info(void);
void sc2355_reset_5g_sar_info(void);
void sc2355_set_5g_sar_info(struct sprd_priv *priv, struct sprd_vif *vif, u8 *data, u16 len);
u8 sc2355_pw_backoff_band2value(u8 channel);
int sc2355_set_reduce_power(struct sprd_vif *vif, unsigned char *data, unsigned short len);
int sc2355_get_reduce_power(struct sprd_vif *vif, unsigned char *data, unsigned short len,
			    unsigned char *rbuf, unsigned short *r_len);
void sc2355_fcc_fresh_scan_bo(struct sprd_priv *priv);
void sc2355_fcc_fresh_all_mode_bo(struct sprd_priv *priv, u8 *channel, u8 *bw);
#endif

