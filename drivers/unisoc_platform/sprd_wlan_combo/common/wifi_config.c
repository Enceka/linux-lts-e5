// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 */

#include "common.h"
#include <linux/vmalloc.h>

#define CONF_PREFIX "config_"
#define MAX_NUM_LINES 1024

enum line_type {
	UNINITIAL_PARAM_LINE = 0,
	EMPTY_LINE_NO_END, // empty_line_without_line_end
	EMPTY_LINE_WITH_END, // empty_line_with_line_end
	COMMENT_LINE, // comment_line
	INVALID_CONF_LINE, // invalid conf_line
	TO_BE_PARSED_CONF_LINE, // to be parsed conf_line

	MAX_LINE_TYPE,
};

struct lineattr {
	const char *line_buf;
	u16 line_len;
	u16 valid_line_offset;
	u16 valid_line_len;
	enum line_type ltype;
};

static inline bool is_xdigit(const char *str)
{
	if (isxdigit(*str) || *str == 'x' || *str == 'X' ||
	    *str == '+' || *str == '-')
		return true;
	else
		return false;
}

static inline bool is_space_tab(const char *str)
{
	if (*str == ' ' || *str == '\t')
		return true;
	else
		return false;
}

static inline bool is_line_end(const char *str)
{
	if (*str == '\n' || *str == '\r')
		return true;
	else
		return false;
}

static const char *skip_strn_head_spaces(const char *str, int line)
{
	int icnt = 0;

	while (icnt < line) {
		if (is_space_tab(str)) {
			str++;
			icnt++;
		} else {
			break;
		}
	}

	return str;
}

struct global_parse_data {
	char *name;
	int (*parser)(const struct global_parse_data *data,
		      struct wifi_driver_config *config,
		      int line, const char *value);

	int param_offset, param_size;
	s64 range_min, range_max;
	bool range_check;
};

static int
wifi_config_set_xdigit(const struct global_parse_data *data,
		       struct wifi_driver_config *config, s64 input_val)
{
	s8 tmp_s8;
	s16 tmp_s16;
	s32 tmp_s32;
	s64 tmp_s64;

	switch (data->param_size) {
	case sizeof(s8):
		tmp_s8 = (s8)input_val;
		memcpy((u8 *)config + data->param_offset, &tmp_s8,
		       data->param_size);
		break;
	case sizeof(s16):
		tmp_s16 = (s16)input_val;
		memcpy((u8 *)config + data->param_offset, &tmp_s16,
		       data->param_size);
		break;
	case sizeof(s32):
		tmp_s32 = (s32)input_val;
		memcpy((u8 *)config + data->param_offset, &tmp_s32,
		       data->param_size);
		break;
	case sizeof(s64):
		tmp_s64 = (s64)input_val;
		memcpy((u8 *)config + data->param_offset, &tmp_s64,
		       data->param_size);
		break;
	default:
		wl_err("%s %s %d-%d err.\n", __func__, data->name,
		       data->param_offset, data->param_size);
		return -ERANGE;
	}

	return 0;
}

static int
wifi_config_parse_xdigit(const struct global_parse_data *data,
			 struct wifi_driver_config *config,
			 int line, const char *pos)
{
	s64 val = 0;
	int icnt = 0, ret, valid_len;
	char *buf;

	icnt = skip_strn_head_spaces(pos, line) - pos;
	if (icnt == line) {
		wl_err("all space or tab in line params.\n");
		return -EINVAL;
	}

	valid_len = line - icnt;
	buf = kzalloc(valid_len + 1, GFP_KERNEL);
	if (!buf) {
		wl_err("alloc %d failed for %s.\n", valid_len, pos + icnt);
		return -ENOMEM;
	}
	memcpy(buf, pos + icnt, valid_len);
	buf[valid_len] = '\0';

	icnt = 0;
	while (icnt < valid_len) {
		if (!is_xdigit(buf + icnt)) {
			buf[icnt] = '\0';
			break;
		}
		icnt++;
	}

	if (buf[0] == '-')
		ret = kstrtos64(buf, 0, &val);
	else
		ret = kstrtou64(buf, 0, &val);
	if (ret) {
		wl_err("Line %d ret %d \"%s\"", valid_len, ret, buf);
		goto err_buf;
	}

	wl_debug("%s=0x%llx for %d in %d %s.\n", data->name, val,
		 valid_len, line, pos);

	if (data->range_check && val < data->range_min) {
		wl_err("Line %d: too small %s (val=%lld min=%lld)",
		       line, data->name, val, data->range_min);
		ret = -ERANGE;
		goto err_buf;
	}

	if (data->range_check && val > data->range_max) {
		wl_err("Line %d: too large %s (val=%lld max=%lld)",
		       line, data->name, val, data->range_max);
		ret = -ERANGE;
		goto err_buf;
	}

	ret = wifi_config_set_xdigit(data, config, val);
	if (ret)
		goto err_buf;

err_buf:
	kfree(buf);
	return ret;
}

#define OFFSET(v) (offsetof(struct wifi_driver_config, v))
#define SIZE(v) (sizeof_field(struct wifi_driver_config, v))

#define XDIGIT_TYPE(f, min, max, check)		\
{#f, wifi_config_parse_xdigit, OFFSET(f), SIZE(f), min, max, check}
#define XDIGIT(f) XDIGIT_TYPE(f, 0, 0, false)
#define XDIGIT_RANGE(f, min, max) XDIGIT_TYPE(f, min, max, true)

static const struct global_parse_data global_fields[] = {
	XDIGIT(config_static_p2p),
	XDIGIT(config_enable_n79),
	XDIGIT(config_enable_chr),
	XDIGIT(config_disable_assert),
	XDIGIT(config_sta_ap_coex),
};

#define NUM_GLOBAL_FIELDS ARRAY_SIZE(global_fields)

static int get_line_length(const char *l, size_t *remaining_len)
{
	const char *lp = l;
	int icnt;

	if (!remaining_len)
		return -EINVAL;

	icnt = (int)*remaining_len;
	while (icnt > 0) {
		icnt--;
		if (is_line_end(lp++))
			break;
	}
	while (icnt > 0) {
		if (!is_line_end(lp))
			break;
		icnt--;
		lp++;
	}
	*remaining_len = (size_t)icnt;

	return lp - l;
}

static enum line_type
get_line_type(const char *line_buf, int line_len, int *valid_line_off)
{
	enum line_type ltype;
	int icnt = 0;
	const char *pos = line_buf;

	while ((is_space_tab(pos) || *pos == '\0') &&
	       icnt < line_len) {
		pos++;
		icnt++;
	}

	if (icnt < line_len) {
		if (*pos == '#')
			ltype = COMMENT_LINE;
		else if (is_line_end(pos))
			ltype = EMPTY_LINE_WITH_END;
		else if (icnt + strlen(CONF_PREFIX) < line_len &&
			 !strncmp(line_buf + icnt, CONF_PREFIX,
				  strlen(CONF_PREFIX)))
			ltype = TO_BE_PARSED_CONF_LINE;
		else
			ltype = INVALID_CONF_LINE;
	} else {
		ltype = EMPTY_LINE_NO_END;
	}
	*valid_line_off = icnt;

	return ltype;
}

static int
get_wifi_conf_lines(const char *fbuffer, size_t fsize,
		    struct lineattr *lines)
{
	enum line_type ltype;
	int icnt = 0, line_fpos = 0, line_len, num_lines = 0, valid_line_off;
	int line_type_cnt[MAX_LINE_TYPE] = {0};
	const char *line_buf;
	size_t file_size = fsize;

	while (file_size > 0) {
		if (++num_lines > MAX_NUM_LINES) {
			wl_err("%s too many lines in conf file, pls check.\n",
			       __func__);
			return -EINVAL;
		}

		line_buf = fbuffer + line_fpos;
		line_len = get_line_length(line_buf, &file_size);
		if (line_len > PAGE_SIZE) {
			wl_err("%s single line > %lu in conf, pls check.\n",
			       __func__, PAGE_SIZE);
			return -EINVAL;
		}

		ltype = get_line_type(line_buf, line_len, &valid_line_off);
		if (ltype == TO_BE_PARSED_CONF_LINE) {
			lines[icnt].ltype = ltype;
			lines[icnt].line_buf = line_buf;
			lines[icnt].line_len = line_len;
			lines[icnt].valid_line_offset = valid_line_off;
			lines[icnt].valid_line_len =
				line_len - valid_line_off;
			icnt++;
		}
		line_type_cnt[ltype]++;
		line_fpos += line_len;
	}

	wl_info("%s %d %d-%d-%d-%d-%d-%d.\n", __func__, num_lines,
		line_type_cnt[UNINITIAL_PARAM_LINE],
		line_type_cnt[EMPTY_LINE_NO_END],
		line_type_cnt[EMPTY_LINE_WITH_END],
		line_type_cnt[COMMENT_LINE], line_type_cnt[INVALID_CONF_LINE],
		line_type_cnt[TO_BE_PARSED_CONF_LINE]);

	return icnt;
}

static int
wifi_conf_process_global(struct lineattr *line,
			 struct wifi_driver_config *config)
{
	size_t i;
	int ret = 0, flen, line_len;
	const char *pos;
	char *valid_buf = NULL;

	pos = line->line_buf + line->valid_line_offset;
	line_len = line->valid_line_len;

	valid_buf = kzalloc(line_len + 1, GFP_KERNEL);
	if (!valid_buf)
		return -ENOMEM;

	memcpy(valid_buf, pos, line_len);
	valid_buf[line_len] = '\0';

	for (i = 0; i < NUM_GLOBAL_FIELDS; i++) {
		const struct global_parse_data *field = &global_fields[i];

		flen = strlen(field->name);
		if (line_len <= flen + 1 ||
		    strncmp(pos, field->name, flen) != 0 ||
		    pos[flen] != '=')
			continue;

		ret = field->parser(field, config, line_len - flen - 1,
				    valid_buf + flen + 1);
		if (ret)
			wl_err("ret %d to parse %d in %d %s.\n",
			       ret, flen, line_len, valid_buf);
		break;
	}

	if (i == NUM_GLOBAL_FIELDS) {
		wl_debug("unknown %s for %lu global_field.\n",
			 valid_buf, NUM_GLOBAL_FIELDS);
	}

	kfree(valid_buf);
	return ret;
}

static void wifi_config_dump(struct sprd_priv *priv)
{
	struct wifi_driver_config *conf = &priv->wifi_drv_conf;

	wl_info("static_p2p=%u, enable_n79=%u, enable_chr=%u, disable_assert=%u\n",
		conf->config_static_p2p, conf->config_enable_n79,
		conf->config_enable_chr, conf->config_disable_assert);
	wl_info("sta_ap_coex=%u\n", conf->config_sta_ap_coex);
}

int wifi_config_read(const char *fbuffer, size_t fsize, struct sprd_priv *priv)
{
	int ret = 0, errors = 0, line_cnt, icnt = 0;
	struct wifi_driver_config *config = NULL;
	struct lineattr *lines = NULL;

	lines = kzalloc(sizeof(struct lineattr) * MAX_NUM_LINES, GFP_KERNEL);
	if (!lines)
		return -ENOMEM;

	line_cnt = get_wifi_conf_lines(fbuffer, fsize, lines);
	wl_debug("%s line_num %d.\n", __func__, line_cnt);
	if (line_cnt <= 0) {
		ret = line_cnt;
		goto err_lines;
	}

	config = kzalloc(sizeof(struct wifi_driver_config), GFP_KERNEL);
	if (!config) {
		ret = -ENOMEM;
		goto err_lines;
	}

	for (icnt = 0; icnt < line_cnt; icnt++) {
		ret = wifi_conf_process_global(lines + icnt, config);
		if (ret)
			errors++;
	}

	if (!errors) {
		memcpy(&priv->wifi_drv_conf, config,
		       sizeof(struct wifi_driver_config));
		wifi_config_dump(priv);
	} else {
		wl_info("%s %d err in %d lines, pls check.\n",
			__func__, errors, line_cnt);
		ret = -EINVAL;
		goto err_config;
	}

err_config:
	kfree(config);
err_lines:
	kfree(lines);
	return ret;
}

void sprd_parse_wifi_driver_config(struct sprd_priv *priv)
{
	size_t buf_size = 0;
	char *buffer = NULL;

	if (sprd_load_fw(priv, WIFI_DRIVER_CONFIG_FILE, &buffer, &buf_size))
		return;

	wifi_config_read(buffer, buf_size, priv);
	vfree(buffer);
}

