// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc (Spreadtrum) marlin3 Bluetooth controller setup.
 *
 * The BT core of the marlin3 WCN combo chip (reached through the vendor
 * SDIO tty, /dev/ttyBT0) runs on its ROM defaults until the host writes
 * its configuration: without it the controller reports a placeholder
 * BD address (27:93:31:14:22:11), manufacturer 0 and runs uncalibrated.
 * Android's BT HAL (libbt-vendor, marlin3_lite) sends three vendor
 * commands before the first HCI Reset, and so does this:
 *
 *   0xfca0  pskey: class, feature set, BD address, company id, sleep and
 *           audio parameters (176 bytes)
 *   0xfca2  RF: gain and power tables (252 bytes)
 *   0xfca1  00 00 01: enable the BT core with that configuration
 *
 * The two payloads are the device's own bt_configure_{pskey,rf}.ini
 * (/odm/firmware) serialised as the HAL does it, with the factory address
 * patched in; they are loaded as firmware so that nothing board-specific
 * lives in the kernel.  A missing file is not fatal: the controller then
 * comes up unconfigured, as it did before.
 */

#include <linux/firmware.h>
#include <linux/module.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btsprd.h"

#define SPRD_OP_PSKEY		0xfca0
#define SPRD_OP_CORE_ENABLE	0xfca1
#define SPRD_OP_RF		0xfca2

#define SPRD_PSKEY_FW		"sprd/marlin3lite_pskey.bin"
#define SPRD_RF_FW		"sprd/marlin3lite_rf.bin"

static int btsprd_send(struct hci_dev *hdev, u16 opcode, const void *data,
		       u32 len, const char *what)
{
	struct sk_buff *skb;
	int err = 0;

	skb = __hci_cmd_sync(hdev, opcode, len, data, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "sprd: %s (0x%04x) failed (%d)", what, opcode, err);
		return err;
	}
	if (skb->len < 1 || skb->data[0]) {
		bt_dev_err(hdev, "sprd: %s (0x%04x) rejected, status 0x%02x", what,
			   opcode, skb->len ? skb->data[0] : 0xff);
		err = -EIO;
	} else if (opcode == SPRD_OP_PSKEY && skb->len >= 7) {
		/* the answer carries the firmware build: node, then the date */
		bt_dev_info(hdev, "sprd: firmware node %02x%02x, built %02x%02x-%02x-%02x",
			    skb->data[2], skb->data[1], skb->data[6], skb->data[5],
			    skb->data[4], skb->data[3]);
	}
	kfree_skb(skb);
	return err;
}

static int btsprd_send_fw(struct hci_dev *hdev, u16 opcode, const char *name,
			  size_t expect, const char *what)
{
	const struct firmware *fw;
	int err;

	err = request_firmware(&fw, name, &hdev->dev);
	if (err) {
		bt_dev_warn(hdev, "sprd: no %s (%d); the controller stays on its defaults",
			    name, err);
		return err;
	}
	if (fw->size != expect) {
		bt_dev_err(hdev, "sprd: %s is %zu bytes, expected %zu", name,
			   fw->size, expect);
		err = -EINVAL;
	} else {
		err = btsprd_send(hdev, opcode, fw->data, fw->size, what);
	}
	release_firmware(fw);
	return err;
}

int btsprd_setup_marlin3(struct hci_dev *hdev)
{
	static const u8 core_enable[] = { 0x00, 0x00, 0x01 };

	if (btsprd_send_fw(hdev, SPRD_OP_PSKEY, SPRD_PSKEY_FW, 176, "pskey"))
		return 0;
	if (btsprd_send_fw(hdev, SPRD_OP_RF, SPRD_RF_FW, 252, "rf"))
		return 0;
	btsprd_send(hdev, SPRD_OP_CORE_ENABLE, core_enable, sizeof(core_enable),
		    "core enable");
	bt_dev_info(hdev, "sprd: marlin3 configured (pskey, rf, core enable)");
	return 0;
}

MODULE_FIRMWARE(SPRD_PSKEY_FW);
MODULE_FIRMWARE(SPRD_RF_FW);
