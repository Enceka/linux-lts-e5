/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef __SC2355_INTF_H__
#define __SC2355_INTF_H__

#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include "wcn_bus.h"

#include "common/hif.h"

#define MSDU_DSCR_RSVD		5

#define DEL_LUT_INDEX		0
#define ADD_LUT_INDEX		1
#define UPD_LUT_INDEX		2
#define SPRD_TX_MSG_CMD_NUM	128
#define SPRD_TX_QOS_POOL_SIZE	20000
#define SPRD_TX_DATA_START_NUM	(SPRD_TX_QOS_POOL_SIZE - 3)
#define SPRD_RX_MSG_NUM		20000

/* tx len less than cp len 4 byte as sdiom 4 bytes align */
/* set MAX CMD length to 1600 on firmware side*/
#define SPRD_MAX_CMD_TXLEN	1596
#define SPRD_MAX_CMD_RXLEN	1092
#define SPRD_MAX_DATA_TXLEN	1672
#define SPRD_MAX_DATA_RXLEN	1676

#define SAVE_ADDR(data, buf, len)	memcpy((data - len), &buf, len)
#define RESTORE_ADDR(buf, data, len)	memcpy(&buf, (data - len), len)
#define CLEAR_ADDR(data, len)		memset((data - len), 0x0, len)
#define HIGHER_DDR_PRIORITY	0xAA

struct sc2355_hif {
	unsigned int max_num;
	void *hif;
	struct mchn_ops_t *mchn_ops;
};

extern struct sc2355_hif sc2355_hif;

static inline struct sprd_hif *sc2355_get_hif(void)
{
	return (struct sprd_hif *)sc2355_hif.hif;
}

unsigned short sc2355_get_data_csum(void *entry, void *data);
unsigned short sc2355_pcie_get_data_csum(void *entry, void *data);
unsigned short sc2355_sipc_get_data_csum(void *entry, void *data);
int sc2355_tx_cmd_pop_list(int channel, struct mbuf_t *head,
			   struct mbuf_t *tail, int num);
int sc2355_pcie_tx_cmd_pop_list(int channel, struct mbuf_t *head,
			   struct mbuf_t *tail, int num);
int sc2355_sipc_tx_cmd_pop_list(int channel, struct mbuf_t *head,
			   struct mbuf_t *tail, int num);
int sc2355_tx_data_pop_list(int channel, struct mbuf_t *head,
			    struct mbuf_t *tail, int num);
int sc2355_pcie_tx_data_pop_list(int channel, struct mbuf_t *head,
			    struct mbuf_t *tail, int num);
int sc2355_sipc_tx_data_pop_list(int channel, struct mbuf_t *head,
			    struct mbuf_t *tail, int num);
int sc2355_tx_cmd(struct sprd_hif *hif, unsigned char *data, int len);
int sc2355_pcie_tx_cmd(struct sprd_hif *hif, unsigned char *data, int len);
int sc2355_sipc_tx_cmd(struct sprd_hif *hif, unsigned char *data, int len);
int sc2355_tx_addr_trans(struct sprd_hif *hif, unsigned char *data, int len);
int sc2355_hif_tx_list(struct sprd_hif *hif,
		       struct list_head *tx_list,
		       struct list_head *tx_list_head,
		       int tx_count, int ac_index, u8 coex_bt_on);
int sc2355_pcie_hif_tx_list(struct sprd_hif *hif,
		       struct list_head *tx_list,
		       struct list_head *tx_list_head,
		       int tx_count, int ac_index, u8 coex_bt_on);
int sc2355_sipc_hif_tx_list(struct sprd_hif *hif,
		       struct list_head *tx_list,
		       struct list_head *tx_list_head,
		       int tx_count, int ac_index, u8 coex_bt_on,
		       enum sprd_mode mode);
void *sc2355_get_rx_data(struct sprd_hif *hif, void *pos, void **data,
			 void **tran_data, int *len, int offset);
void sc2355_free_rx_data(struct sprd_hif *hif,
			 int chn, void *head, void *tail, int num);
void sc2355_event_sta_lut(struct sprd_vif *vif, u8 *data, u16 len);
void sc2355_pcie_event_sta_lut(struct sprd_vif *vif, u8 *data, u16 len);
void sc2355_sipc_event_sta_lut(struct sprd_vif *vif, u8 *data, u16 len);
void sc2355_handle_pop_list(void *data);
void sc2355_pcie_handle_pop_list(void *data);
void sc2355_sipc_handle_pop_list(void *data);
int sc2355_add_topop_list(int chn, struct mbuf_t *head,
			  struct mbuf_t *tail, int num);
int sc2355_pcie_add_topop_list(int chn, struct mbuf_t *head,
			  struct mbuf_t *tail, int num);
int sc2355_sipc_add_topop_list(int chn, struct mbuf_t *head,
			  struct mbuf_t *tail, int num);
void sc2355_set_coex_bt_on_off(u8 action);
void sc2355_pcie_set_coex_bt_on_off(u8 action);
void sc2355_sipc_set_coex_bt_on_off(u8 action);
int sc2355_push_link(struct sprd_hif *hif, int chn,
		     struct mbuf_t *head, struct mbuf_t *tail, int num,
		     int (*pop)(int, struct mbuf_t *, struct mbuf_t *, int));
int sc2355_pcie_push_link(struct sprd_hif *hif, int chn,
		     struct mbuf_t *head, struct mbuf_t *tail, int num,
		     int (*pop)(int, struct mbuf_t *, struct mbuf_t *, int));
int sc2355_sipc_push_link(struct sprd_hif *hif, int chn,
		     struct mbuf_t *head, struct mbuf_t *tail, int num,
		     int (*pop)(int, struct mbuf_t *, struct mbuf_t *, int));
enum sprd_hif_type get_hwintf_type(void);
void sc2355_tx_addr_trans_free(struct sprd_hif *hif);
void sc2355_pcie_tx_addr_trans_free(struct sprd_hif *hif);
void sc2355_sipc_tx_addr_trans_free(struct sprd_hif *hif);
void sc2355_rx_work_queue(struct work_struct *work);
void sc2355_pcie_rx_work_queue(struct work_struct *work);
int sc2355_sipc_rx_work_queue(void *data);
void sc2355_handle_tx_return(struct sprd_hif *hif,
			     struct sprd_msg_list *list, int send_num, int ret);
void sc2355_pcie_handle_tx_return(struct sprd_hif *hif,
			     struct sprd_msg_list *list, int send_num, int ret);
void sc2355_sipc_handle_tx_return(struct sprd_hif *hif,
			     struct sprd_msg_list *list, int send_num, int ret);
int sc2355_fc_get_send_num(struct sprd_hif *hif,
			   enum sprd_mode mode, int data_num);
int sc2355_pcie_fc_get_send_num(struct sprd_hif *hif,
			   enum sprd_mode mode, int data_num);
int sc2355_sipc_fc_get_send_num(struct sprd_hif *hif,
			   enum sprd_mode mode, int data_num);
int sc2355_fc_test_send_num(struct sprd_hif *hif,
			    enum sprd_mode mode, int data_num);
int sc2355_pcie_fc_test_send_num(struct sprd_hif *hif,
			    enum sprd_mode mode, int data_num);
int sc2355_sipc_fc_test_send_num(struct sprd_hif *hif,
			    enum sprd_mode mode, int data_num);

#endif /* __SC2355_INTF_H__ */
