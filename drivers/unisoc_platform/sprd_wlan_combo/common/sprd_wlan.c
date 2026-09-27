/*
* SPDX-FileCopyrightText: 2020-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/
// SPDX-License-Identifier: GPL-2.0
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <linux/sipc.h>
#include <linux/slab.h>
#include <linux/unistd.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/nvmem-consumer.h>
#include <linux/thermal.h>

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <linux/moduleparam.h>

#include "chip_ops.h"
#include "common.h"
#include "iface.h"
#include "cfg80211.h"
#include "cmd.h"
#include "delay_work.h"
#include "hif.h"
#include "msg.h"
#include "npi.h"
#include "qos.h"
#include "report.h"
#include "tcp_ack.h"

/* value can be passed to wfa_cap via "insmod sprd_wlan_combo.ko wfa_cap=1" */
unsigned int wfa_cap;
module_param(wfa_cap, uint, 0644);
MODULE_PARM_DESC(wfa_cap, "set capability for WFA test");

struct wlan_match_data {
	enum sprd_hif_type hw_type;
};

static const struct wlan_match_data g_sc2332_sipc_data = {
	.hw_type = SPRD_HW_SC2332_SIPC,
};

#if 0
static const struct wlan_match_data g_sc2332_sdio_data = {
	.hw_type = SPRD_HW_SC2332_SDIO,
};
#endif

static const struct wlan_match_data g_sc2355_pcie_data = {
	.hw_type = SPRD_HW_SC2355_PCIE,
};

static const struct wlan_match_data g_sc2355_sipc_data = {
	.hw_type = SPRD_HW_SC2355_SIPC,
};

static const struct wlan_match_data g_sc2355_sdio_data = {
	.hw_type = SPRD_HW_SC2355_SDIO,
};

static const struct of_device_id wlan_global_match_table[] = {
	{ .compatible = "sprd,sc2332-sipc-wifi", .data = &g_sc2332_sipc_data},
#if 0
	{ .compatible = "sprd,sc2332-sdio-wifi", .data = &g_sc2332_sdio_data},
#endif
	{ .compatible = "sprd,sc2355-pcie-wifi", .data = &g_sc2355_pcie_data},
	{ .compatible = "sprd,sc2355-sdio-wifi", .data = &g_sc2355_sdio_data},
	{ .compatible = "sprd,sc2355-sipc-wifi", .data = &g_sc2355_sipc_data},
	{ },
};

MODULE_DEVICE_TABLE(of, wlan_global_match_table);

extern int sc2332_sipc_probe(struct platform_device *pdev);
extern int sc2355_sdio_probe(struct platform_device *pdev);
extern int sc2355_pcie_probe(struct platform_device *pdev);
extern int sc2355_sipc_probe(struct platform_device *pdev);

int sprd_load_fw(struct sprd_priv *priv, const char *path, char **buffer, size_t *buf_size)
{
	const struct firmware *fw = NULL;
	int ret = 0, i;

	ret = request_firmware_direct(&fw, path, wiphy_dev(priv->wiphy));
	if (ret == -ENOENT) {
		/* file not exist,don't retry */
		wl_err("%s, file %s not exist", __func__, path);
		return ret;
	}

	if (ret) {
		wl_err("first open file %s error\n", path);
		/*failed to read ini file, try again*/
		for (i = 0; i < 5; i++) {
			msleep(50);
			ret = request_firmware_direct(&fw, path, wiphy_dev(priv->wiphy));
			if (ret)
				wl_err("failed to open file %s for the %d time!\n", path, i);
			else
				break;
		}
		if (ret) {
			wl_err("open file %s error\n", path);
			return -1;
		}
	}

	if (!fw || !fw->data || fw->size <= 0 || fw->size > FW_MAX_SIZE) {
		wl_err("%s invalid firmware file\n", __func__);
		release_firmware(fw);
		return -EINVAL;
	}
	*buffer = vmalloc(fw->size);
	if (*buffer) {
		memcpy(*buffer, fw->data, fw->size);
		*buf_size = fw->size;
		release_firmware(fw);
		wl_info("%s read %s data_len:%zu\n", __func__, path, *buf_size);
		return 0;
	}

	wl_err("%s no memory\n", __func__);
	release_firmware(fw);
	return -ENOMEM;
}

static int sprd_wlan_probe(struct platform_device *pdev)
{
	struct wlan_match_data *p_match_data;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id =
		of_match_node(wlan_global_match_table, np);

	if (!of_id) {
		wl_err("%s not find matched id!", __func__);
		return -EINVAL;
	}

	p_match_data = (struct wlan_match_data *)of_id->data;
	if (!p_match_data) {
		wl_err("%s not find matched data!", __func__);
		return -EINVAL;
	}

	wl_info("%s %s %d.\n", __func__, of_id->compatible, p_match_data->hw_type);

	if (p_match_data->hw_type == SPRD_HW_SC2332_SIPC) {
		return sc2332_sipc_probe(pdev);
	} else if (p_match_data->hw_type == SPRD_HW_SC2355_SDIO) {
		return sc2355_sdio_probe(pdev);
	} else if (p_match_data->hw_type == SPRD_HW_SC2355_SIPC) {
		return sc2355_sipc_probe(pdev);
	} else if (p_match_data->hw_type == SPRD_HW_SC2355_PCIE) {
		return sc2355_pcie_probe(pdev);
	} else {
		wl_err("%s error hw_type %d.\n", __func__, p_match_data->hw_type);
		dump_stack();
		return -EINVAL;
	}
}

static void sprd_wlan_remove(struct platform_device *pdev)
{
	struct wlan_match_data *p_match_data;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id =
		of_match_node(wlan_global_match_table, np);

	if (!of_id) {
		wl_err("%s not find matched id!", __func__);
		return;
	}

	p_match_data = (struct wlan_match_data *)of_id->data;
	if (!p_match_data) {
		wl_err("%s not find matched data!", __func__);
		return;
	}

	wl_info("%s %s %d.\n", __func__, of_id->compatible, p_match_data->hw_type);

	sprd_iface_remove(pdev);
}

static struct platform_driver sprd_wlan_driver = {
	.probe = sprd_wlan_probe,
	.remove = sprd_wlan_remove,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "wlan",
		   .of_match_table = wlan_global_match_table,
	}
};

module_platform_driver(sprd_wlan_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Spreadtrum WLAN Driver");
MODULE_AUTHOR("Carson Yang <carson.yang@unisoc.com>");
