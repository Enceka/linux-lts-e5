/*
PDX-FileCopyrightText: 2021-2022 Unisoc (Shanghai) Technologies Co., Ltd
* SPDX-License-Identifier: GPL-2.0
*
* Copyright 2021-2022 Unisoc (Shanghai) Technologies Co., Ltd
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of version 2 of the GNU General Public License
* as published by the Free Software Foundation.
*/

#ifndef __HW_SIPC_PARAM_H__
#define __HW_SIPC_PARAM_H__
#include "common/common.h"

struct merl_nvm_cali_cmd {
	int8_t itm[64];
	int32_t par[512];
	int32_t num;
};

struct merl_nvm_name_table {
	int8_t *itm;
	uint32_t mem_offset;
	int32_t type;
};
/*[Section 1: Version] */
struct merl_version_t {
	uint16_t major;
	uint16_t minor;
};

/*[Section 2: Board Config]*/
struct merl_board_config_t {
	uint16_t calibration_bypass;
	uint8_t g2_chain_mask;
	uint8_t g5_chain_mask;
};

/*[Section 3: Board Config TPC]*/
struct merl_board_config_tpc_t {
	uint8_t dpd_lut_idx[8];
	uint16_t tpc_goal_chain0[8];
	uint16_t tpc_goal_chain1[8];
};

struct merl_tpc_element_lut_t {
	uint8_t rf_gain_idx;
	uint8_t pa_bias_idx;
	int8_t  dvga_offset;
	int8_t  residual_error;
};
/*[Section 4: TPC-LUT]*/
struct merl_tpc_lut_t {
	struct merl_tpc_element_lut_t g2_lut[8];
	struct merl_tpc_element_lut_t g5_lut[8];
};

/*[Section 5: Board Config Frequency Compensation]*/
struct merl_board_conf_freq_comp_t {
	int8_t channel_2g_chain0[14];
	int8_t channel_2g_chain1[14];
	int8_t channel_5g_chain0[25];
	int8_t channel_5g_chain1[25];
	int8_t reserved[2];
};

/*[Section 6: Rate To Power with BW 20M]*/
struct merl_power_20m_t {
	int8_t power_11b[4];
	int8_t power_11g[8];
	int8_t power_11a[8];
	int8_t power_2g_11n[17];
	int8_t power_5g_11n[17];
	int8_t power_11ac[20];
	int8_t reserved[3];
};

/*[Section 7: Power Backoff]*/
struct merl_power_backoff_t {
	int8_t green_wifi_offset;
	int8_t ht40_2g_power_offset;
	int8_t ht40_5g_power_offset;
	int8_t vht40_power_offset;
	int8_t vht80_power_offset;
	int8_t sar_power_offset;
	int8_t mean_power_offset;
	int8_t apc_mode;
	int8_t magic_word;
	int8_t reserved[2];
};

/*[Section 8: Reg Domain]*/
struct merl_reg_domain_t {
	uint32_t reg_domain1;
	uint32_t reg_domain2;
};

/*[Section 9: Band Edge Power offset (MKK, FCC, ETSI)]*/
struct merl_band_edge_power_offset_t {
	uint8_t bw20m[39];
	uint8_t bw40m[21];
	uint8_t bw80m[6];
	uint8_t reserved[2];
};

/*[Section 10: TX Scale]*/
struct merl_tx_scale_t {
	int8_t chain0[39][16];
	int8_t chain1[39][16];
};

/*[Section 11: misc]*/
struct merl_misc_t {
	int8_t dfs_switch;
	int8_t power_save_switch;
	int8_t rssi_report_diff;
	int8_t ex_fem_en;
	int8_t tx_en_ctrl;
	int8_t lna_en_ctrl;
	int8_t sw_en_ctrl;
	int8_t lna_gain;
	int8_t lna_bypass_gain;
	int8_t ex_fem_pdet;
};

/*[Section 12: debug reg]*/
struct merl_debug_reg_t {
	uint32_t address[16];
	uint32_t value[16];
};

/*[Section 13:coex_config] */
struct merl_coex_config_t {
	uint32_t bt_performance_cfg0;
	uint32_t bt_performance_cfg1;
	uint32_t wifi_performance_cfg0;
	uint32_t wifi_performance_cfg2;
	uint32_t strategy_cfg0;
	uint32_t strategy_cfg1;
	uint32_t strategy_cfg2;
	uint32_t compatibility_cfg0;
	uint32_t compatibility_cfg1;
	uint32_t ant_cfg0;
	uint32_t ant_cfg1;
	uint32_t isolation_cfg0;
	uint32_t isolation_cfg1;
	uint32_t reserved_cfg0;
	uint32_t reserved_cfg1;
	uint32_t reserved_cfg2;
	uint32_t reserved_cfg3;
	uint32_t reserved_cfg4;
	uint32_t reserved_cfg5;
	uint32_t reserved_cfg6;
	uint32_t reserved_cfg7;
};

struct merl_rf_config_t {
	int rf_data_len;
	uint8_t rf_data[1500];
};

struct merl_ap_config_t {
	int ap_data_len;
	uint8_t ap_data[1500];
};

struct merl_ap_oui_config_t {
	int ap_oui_num;
	uint32_t oui_data[50];
};
/*wifi config section1 struct*/
struct merl_wifi_conf_sec1_t {
	struct merl_version_t version;
	struct merl_board_config_t board_config;
	struct merl_board_config_tpc_t board_config_tpc;
	struct merl_tpc_lut_t tpc_lut;
	struct merl_board_conf_freq_comp_t board_conf_freq_comp;
	struct merl_power_20m_t power_20m;
	struct merl_power_backoff_t power_backoff;
	struct merl_reg_domain_t reg_domain;
	struct merl_band_edge_power_offset_t band_edge_power_offset;
};

/*wifi config section2 struct*/
struct merl_wifi_conf_sec2_t {
	struct merl_tx_scale_t tx_scale;
	struct merl_misc_t misc;
	struct merl_debug_reg_t debug_reg;
	struct merl_coex_config_t coex_config;
};

struct merl_roaming_param_t {
	uint8_t trigger;
	uint8_t delta;
	uint8_t band_5g_prefer;
};

struct merl_wifi_config_param_t {
	struct merl_roaming_param_t roaming_param;
};

/*wifi config struct*/
struct merl_wifi_conf_t {
	struct merl_version_t version;
	struct merl_board_config_t board_config;
	struct merl_board_config_tpc_t board_config_tpc;
	struct merl_tpc_lut_t tpc_lut;
	struct merl_board_conf_freq_comp_t board_conf_freq_comp;
	struct merl_power_20m_t power_20m;
	struct merl_power_backoff_t power_backoff;
	struct merl_reg_domain_t reg_domain;
	struct merl_band_edge_power_offset_t band_edge_power_offset;
	struct merl_tx_scale_t tx_scale;
	struct merl_misc_t misc;
	struct merl_debug_reg_t debug_reg;
	struct merl_coex_config_t coex_config;
	struct merl_rf_config_t rf_config;
	struct merl_wifi_config_param_t wifi_param;
	struct merl_ap_oui_config_t oui_config;
	struct merl_ap_config_t ap_config;
};

int get_wifi_config_param(struct sprd_priv *priv, struct merl_wifi_conf_t *p);
#endif
