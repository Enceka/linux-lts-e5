/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BTSPRD_H
#define __BTSPRD_H

struct hci_dev;

#if IS_ENABLED(CONFIG_BT_HCIUART_SPRD)
int btsprd_setup_marlin3(struct hci_dev *hdev);
#else
static inline int btsprd_setup_marlin3(struct hci_dev *hdev)
{
	return 0;
}
#endif

#endif
