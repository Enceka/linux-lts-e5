/*
 * SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/vmalloc.h>
#include <linux/atomic.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_device.h>  
#endif
#include <linux/compat.h>
#include <linux/tty_flip.h>
#include <linux/tty_driver.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/version.h>

#include <linux/sipc.h>
#include <linux/io.h>
#include <linux/notifier.h>

#include "wcn_integrate_platform.h"
#include "marlin_platform.h"
#include "wcn_bus.h"


#include "alignment/sitm.h"
#include "unisoc_bt_log.h"
#include "lpm.h"
#include "tty.h"

#include "rfkill.h"
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>

static unsigned int log_level = MTTY_LOG_LEVEL_NONE;
#define BT_VER(fmt, ...)                        \
    do {                                        \
        if (log_level == MTTY_LOG_LEVEL_VER)    \
            pr_err(fmt, ##__VA_ARGS__);         \
    } while (0)

#define MTTY_DEV_MAX_NR     1
#define mTTY_MAX_DATA_LEN   4096
#define MTTY_STATE_OPEN     1
#define MTTY_STATE_CLOSE    0
#define COMMAND_HEAD        1
#define ISO_HEAD            5

#define DOWN_ACQUIRE_TIMEOUT_MS 20

#define SDIOM_WR_DIRECT_MOD
#ifdef SDIOM_WR_DIRECT_MOD
#define SDIOM_WR_DIRECT_MOD_ADDR 0x51004000
#endif

#define SET_BT_VERSION 1

static struct semaphore sem_id;
struct mchn_ops_t bt_pcie_rx_ops;
struct mchn_ops_t bt_pcie_tx_ops0;
struct mchn_ops_t bt_pcie_tx_ops1;

int PCIE = 0;
int SDIO = 0;
int SIPC = 0;
int SIPC2 = 0;

struct rx_data {
    unsigned int channel;
    struct mbuf_t *head;
    struct mbuf_t *tail;
    unsigned int num;
    struct list_head entry;
};

//to select pcie or other
struct mtty_device {
    struct mtty_init_data   *pdata;
    struct tty_port *port;
    /* pcie used */
    struct tty_port *port0;
    struct tty_port *port1;

    struct tty_struct   *tty;
    struct tty_driver   *driver;
    //add  platform to adapting sipc2
    struct platform_device *pdev;

/* mtty state */
  //uint32_t		mtty_state;
    struct mutex		stat_lock;
    /* mtty state */
    atomic_t state;
    /*spinlock_t    rw_lock;*/
    struct mutex    stat_mutex;
    struct mutex    rw_mutex;
    struct list_head rx_head;
    /*struct tasklet_struct rx_task;*/
    struct work_struct bt_rx_work;
    struct workqueue_struct *bt_rx_workqueue;
};



typedef struct {
    unsigned long vir;
    unsigned long phy;
    int size;
} dm_t;

//static struct mtty_pcie_device *mtty_pcie_dev;
static struct mtty_device *mtty_dev;

static struct tty_struct *m_tty;  //to adapt sipc and sipc2

extern int set_power_ret;
static unsigned int que_task = 1;
static int que_sche = 1;
static bool is_user_debug = false;
extern void sdiohal_dump_aon_reg(void);
struct device *ttyBT_dev = NULL;
static struct device *dm_rx_t = NULL;
unsigned long dm_rx_phy[BT_PCIE_RX_MAX_NUM];
unsigned char *(dm_rx_ptr[BT_PCIE_RX_MAX_NUM]);

struct dma_buf {
    unsigned long vir;
    unsigned long phy;
    int size;
};

static bool is_dumped = false;

int sprd_bt_read_soc_version(char *op_string){
    struct device_node *hwf;
    const char *value;

    hwf = of_find_node_by_path("/hwfeature/auto");
    if (IS_ERR_OR_NULL(hwf)) {
        pr_err("NO hwfeature/auto node found\n");
        return PTR_ERR(hwf);
    }

    value = of_get_property(hwf, "efuse", NULL);
    if (value != NULL){
        if (strcmp(value, "") == 0) {
            pr_err("phone soc version is null");
            return -EINVAL;
        }
    } else{
        pr_err("value is NULL");
        return -EINVAL;
    }

    strncpy(op_string, value, strlen(value)+1);
    pr_info("phone soc version: %s \n", op_string);

    return 0;
}

static ssize_t dumpmem_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    if (buf[0] == 2) {
        dev_unisoc_bt_info(ttyBT_dev,
                           "Set is_user_debug true!\n");
        is_user_debug = true;
        return 0;
    }

    if (is_dumped == false) {
        dev_unisoc_bt_info(ttyBT_dev,
                           "mtty BT start dump cp mem !\n");
        mdbg_assert_interface("BT command timeout assert !!!");
    } else {
        dev_unisoc_bt_info(ttyBT_dev,
                           "mtty BT has dumped cp mem, pls restart phone!\n");
    }
    is_dumped = true;
    return 0;
}
static ssize_t chipid_show(struct device *dev,
       struct device_attribute *attr, char *buf)
{
    int i = 0, id;
    const char *id_str = NULL;
	if (SIPC) {
		id = wcn_get_aon_chip_id();
		dev_unisoc_bt_info(ttyBT_dev, "sipc:%s: %d", __func__, id);
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", id);
	} else if (SIPC2) {
		id = wcn_get_aon_chip_id();
		id_str = wcn_get_chip_name();
		dev_unisoc_bt_info(ttyBT_dev,
							"sipc2 %s: chipid: %d, chipid_str: %s",
							__func__, id, id_str);
		i = scnprintf(buf, PAGE_SIZE, "%d/", id);
		dev_unisoc_bt_info(ttyBT_dev,
							"%s: buf: %s, i = %d",
							__func__, buf, i);
		strncat(buf, id_str, 32);
		i += scnprintf(buf + i, PAGE_SIZE - i, "%s", buf + i);
		dev_unisoc_bt_info(ttyBT_dev,
							"%s: buf: %s, i = %d",
							__func__, buf, i);
	} else {
		id = wcn_get_chip_type();
		id_str = wcn_get_chip_name();
		dev_unisoc_bt_info(ttyBT_dev,
						"%s: chipid: %d, chipid_str: %s",
						__func__, id, id_str);

		i = scnprintf(buf, PAGE_SIZE, "%d/", id);
		dev_unisoc_bt_info(ttyBT_dev,
						"%s: buf: %s, i = %d",
						__func__, buf, i);
		strcat(buf, id_str);
		i += scnprintf(buf + i, PAGE_SIZE - i, "%s", buf + i);
		dev_unisoc_bt_info(ttyBT_dev,
						"%s: buf: %s, i = %d",
						__func__, buf, i);
	}
	return i;
}

static DEVICE_ATTR_RO(chipid);
static DEVICE_ATTR_WO(dumpmem);

static struct attribute *bluetooth_attrs[] = {
    &dev_attr_chipid.attr,
    &dev_attr_dumpmem.attr,
    NULL,
};

static struct attribute_group bluetooth_group = {
    .name = NULL,
    .attrs = bluetooth_attrs,
};

static void hex_dump(unsigned char *bin, size_t binsz)
{
    char *str, hex_str[]= "0123456789ABCDEF";
    size_t i;

    str = (char *)vmalloc(binsz * 3);
    if (!str) {
    return;
    }

    for (i = 0; i < binsz; i++) {
        str[(i * 3) + 0] = hex_str[(bin[i] >> 4) & 0x0F];
        str[(i * 3) + 1] = hex_str[(bin[i]     ) & 0x0F];
        str[(i * 3) + 2] = ' ';
    }
    str[(binsz * 3) - 1] = 0x00;
    dev_unisoc_bt_info(ttyBT_dev,
                        "%s\n",
                        str);
    vfree(str);
}

static void hex_dump_block(unsigned char *bin, size_t binsz)
    {
    #define HEX_DUMP_BLOCK_SIZE 20
    int loop = binsz / HEX_DUMP_BLOCK_SIZE;
    int tail = binsz % HEX_DUMP_BLOCK_SIZE;
    int i;

    if (!loop) {
        hex_dump(bin, binsz);
        return;
    }

    for (i = 0; i < loop; i++) {
        hex_dump(bin + i * HEX_DUMP_BLOCK_SIZE, HEX_DUMP_BLOCK_SIZE);
    }

    if (tail)
        hex_dump(bin + i * HEX_DUMP_BLOCK_SIZE, tail);
    }

/******************dma alloc and free function****************/
int mtty_dmalloc(struct device *priv, struct dma_buf *dm, int size)
{
    struct device *dev = priv;

    if (!dev) {
        pr_err("%s(NULL)\n", __func__);
        return -1;
    }

    if (dma_set_mask(dev, DMA_BIT_MASK(64))) {
        pr_info("dma_set_mask err\n");
        if (dma_set_coherent_mask(dev, DMA_BIT_MASK(64))) {
            pr_err("dma_set_coherent_mask err\n");
            return -1;
        }
    }

    dm->vir =
        (unsigned long)dma_alloc_coherent(dev, size,
                            (dma_addr_t *)(&(dm->phy)),
                            GFP_DMA);
    if (dm->vir == 0) {
        pr_err("dma_alloc_coherent err\n");
        return -1;
    }
    dm->size = size;
    memset((unsigned char *)(dm->vir), 0x56, size);
    pr_info("dma_alloc_coherent(%d) 0x%lx 0x%lx\n",
            size, dm->vir, dm->phy);

    return 0;
}

int mtty_dma_buf_alloc(int chn, int size, int num)
{
    int ret, i;
    struct dma_buf temp = {0};
    struct mbuf_t *mbuf = NULL, *head = NULL, *tail = NULL;
    dm_rx_t = &mtty_dev->pdev->dev;

    if (!dm_rx_t) {
        pr_err("%s:PCIE device link error\n", __func__);
        return -1;
    }
    ret = sprdwcn_bus_list_alloc(chn, &head, &tail, &num);
    if (ret != 0)
        return -1;
    for (i = 0, mbuf = head; i < num; i++) {
        ret = mtty_dmalloc(dm_rx_t, &temp, size);
        if (ret != 0)
            return -1;
        mbuf->buf = (unsigned char *)(temp.vir);
        dm_rx_ptr[i] = mbuf->buf;
        mbuf->phy = (unsigned long)(temp.phy);
        dm_rx_phy[i] = mbuf->phy;
        mbuf->len = temp.size;
        memset(mbuf->buf, 0x0, mbuf->len);
        mbuf = mbuf->next;
    }

    ret = sprdwcn_bus_push_list(chn, head, tail, num);

    return ret;
}

int mtty_dma_buf_free(int num) {
    int loop_count = 0;
    for (; loop_count < num; loop_count++) {
        if(!dm_rx_t) {
            pr_err("%s: dm_rx_t or is dm_rx_ptr NULL \n", __func__);
        } else {
            dma_free_coherent(dm_rx_t, BT_PCIE_RX_DMA_SIZE , (void *)dm_rx_ptr[loop_count], dm_rx_phy[loop_count]);
            pr_err("%s: free  dm_rx_ptr[%d] success \n", __func__, loop_count);
            dm_rx_ptr[loop_count] = NULL;
        }
    }
    return 0;
}


/*********************rx_cb function*******************/

//sdio_rx_cb
/* static void mtty_rx_task(unsigned long data) */
static void mtty_pcie_rx_work_queue(struct work_struct *work)

{
    int i, ret = 0;
    /*struct mtty_device *mtty = (struct mtty_device *)data;*/
    struct mtty_device *mtty;
    struct rx_data *rx = NULL;

    que_task = que_task + 1;
    if (que_task > 65530)
        que_task = 0;
    BT_VER("mtty que_task= %d\n", que_task);
    que_sche = que_sche - 1;

    mtty = container_of(work, struct mtty_device, bt_rx_work);
    if (unlikely(!mtty)) {
        pr_err("mtty_rx_task mtty is NULL\n");
        return;
    }

    if (atomic_read(&mtty->state) == MTTY_STATE_OPEN) {
        do {
            mutex_lock(&mtty->rw_mutex);
            if (list_empty_careful(&mtty->rx_head)) {
                BT_VER("mtty over load queue done\n");
                mutex_unlock(&mtty->rw_mutex);
                break;
            }
            rx = list_first_entry_or_null(&mtty->rx_head,
                        struct rx_data, entry);
            if (!rx) {
                pr_err("mtty over load queue abort\n");
                mutex_unlock(&mtty->rw_mutex);
                break;
            }
            list_del(&rx->entry);
            mutex_unlock(&mtty->rw_mutex);

            BT_VER("mtty over load working at channel: %d, len: %d\n",
                        rx->channel, rx->head->len);
            for (i = 0; i < rx->head->len; i++) {
                ret = tty_insert_flip_char(mtty->port0,
                            *(rx->head->buf+i), TTY_NORMAL);
                if (ret != 1) {
                    i--;
                    continue;
                } else {
                    tty_flip_buffer_push(mtty->port0);
                }
            }
            pr_err("mtty over load cut channel: %d\n", rx->channel);
            if (rx->head->buf != NULL) {
                kfree(rx->head->buf);
                rx->head->buf = NULL;
            }
            if (rx != NULL) {
                kfree(rx);
                rx = NULL;
            }

        } while (1);
    } else {
        pr_info("mtty status isn't open, status:%d\n", atomic_read(&mtty->state));
    }
}

static void mtty_rx_work_queue(struct work_struct *work)

{
    int i, ret = 0;
    /*struct mtty_device *mtty = (struct mtty_device *)data;*/
    struct mtty_device *mtty;
    struct rx_data *rx = NULL;

    que_task = que_task + 1;
    if (que_task > 65530)
        que_task = 0;
    dev_unisoc_bt_info(ttyBT_dev,
                       "mtty que_task= %d\n",
                       que_task);
    que_sche = que_sche - 1;

    mtty = container_of(work, struct mtty_device, bt_rx_work);
    if (unlikely(!mtty)) {
        dev_unisoc_bt_err(ttyBT_dev,
                          "mtty_rx_task mtty is NULL\n");
        return;
    }

    if (atomic_read(&mtty->state) == MTTY_STATE_OPEN) {
        do {
            mutex_lock(&mtty->rw_mutex);
            if (list_empty_careful(&mtty->rx_head)) {
                dev_unisoc_bt_err(ttyBT_dev,
                                  "mtty over load queue done\n");
                mutex_unlock(&mtty->rw_mutex);
                break;
            }
            rx = list_first_entry_or_null(&mtty->rx_head,
                        struct rx_data, entry);
            if (!rx) {
                dev_unisoc_bt_err(ttyBT_dev,
                                  "mtty over load queue abort\n");
                mutex_unlock(&mtty->rw_mutex);
                break;
            }
            list_del(&rx->entry);
            mutex_unlock(&mtty->rw_mutex);

            dev_unisoc_bt_err(ttyBT_dev,
                              "mtty over load working at channel: %d, len: %d\n",
                              rx->channel, rx->head->len);
            for (i = 0; i < rx->head->len; i++) {
                ret = tty_insert_flip_char(mtty->port,
                            *(rx->head->buf+i), TTY_NORMAL);
                if (ret != 1) {
                    i--;
                    continue;
                } else {
                    tty_flip_buffer_push(mtty->port);
                }
            }
            dev_unisoc_bt_err(ttyBT_dev,
                              "mtty over load cut channel: %d\n",
                              rx->channel);
		if (rx->head->buf != NULL) {
			kfree(rx->head->buf);
			rx->head->buf = NULL;
		}
		if (rx != NULL) {
			kfree(rx);
			rx = NULL;
		}

        } while (1);
    } else {
        dev_unisoc_bt_info(ttyBT_dev,
                           "mtty status isn't open, status:%d\n",
                           atomic_read(&mtty->state));
    }
}

//sipc rx_cb
static void mtty_handler (int event, void *data)
{
    struct mtty_device *mtty = data;
    int i, cnt = 0, ret = -1, retry_count = 10;
    unsigned char *buf;

    buf = kzalloc(mTTY_MAX_DATA_LEN, GFP_KERNEL);
    if (!buf) {
        return;
    }

    dev_unisoc_bt_dbg(ttyBT_dev,
                        "mtty handler event=%d\n", event);

    switch (event) {
    case SBUF_NOTIFY_WRITE:
        break;
    case SBUF_NOTIFY_READ:
        do {
            cnt = sbuf_read(mtty->pdata->dst,
                    mtty->pdata->channel,
                    mtty->pdata->rx_bufid,
                    (void *)buf,
                    mTTY_MAX_DATA_LEN,
                    0);
            dev_unisoc_bt_dbg(ttyBT_dev,
                                "%s read data len =%d\n",__func__, cnt);
            mutex_lock(&(mtty->stat_lock));
            if ((atomic_read(&mtty->state) == MTTY_STATE_OPEN) && (cnt > 0)) {
                for (i = 0; i < cnt; i++) {
                    ret = tty_insert_flip_char(mtty->port,
                                buf[i],
                                TTY_NORMAL);
                    while((ret != 1) && retry_count--) {
                        msleep(2);
                        dev_unisoc_bt_info(ttyBT_dev,
                                            "mtty insert data fail ret =%d, retry_count = %d\n",
                                            ret, 10 - retry_count);
                        ret = tty_insert_flip_char(mtty->port,
                                    buf[i],
                                    TTY_NORMAL);
                    }
                    if(retry_count != 10)
                        retry_count = 10;
                }
                tty_flip_buffer_push(mtty->port);
            }
            mutex_unlock(&(mtty->stat_lock));
        } while(cnt == mTTY_MAX_DATA_LEN);
        break;
    default:
        dev_unisoc_bt_info(ttyBT_dev,
                            "Received event is invalid(event=%d)\n", event);
    }

    kfree(buf);
    }

//sipc2 rx_cb
static int mtty_sipc2_rx_cb(int chn, struct mbuf_t *head, struct mbuf_t *tail, int num)
{
    int ret = 0, len_send, rx_num;
    struct rx_data *rx;
    struct mbuf_t *rx_head = head;
    len_send = head->len;

    bt_wakeup_host();
    if (atomic_read(&mtty_dev->state) == MTTY_STATE_CLOSE) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "%s mtty bt is closed abnormally\n",
                            __func__);
        sprdwcn_bus_push_list(chn, head, tail, num);
        return -1;
    }

    if (mtty_dev != NULL) {
        if (!work_pending(&mtty_dev->bt_rx_work)) {
            dev_unisoc_bt_dbg(ttyBT_dev,
                                "%s tty_insert_flip_string",
                                __func__);
            for (rx_num = num; rx_num > 0; rx_num--) {
                len_send = head->len;
                /*if (rx_head->buf[BT_SIPC_HEAD_LEN] == 0x04) {
                    if(len_send < 32)
                       hex_dump_block((unsigned char *)rx_head->buf + BT_SIPC_HEAD_LEN, len_send);
                    else
                       hex_dump_block((unsigned char *)rx_head->buf + BT_SIPC_HEAD_LEN, 16);
                }*/
                ret = tty_insert_flip_string(mtty_dev->port,
                            (unsigned char *)rx_head->buf+BT_SIPC_HEAD_LEN,
                            len_send);
                dev_unisoc_bt_dbg(ttyBT_dev,
                                  "%s ret: %d, len: %d\n",
                                  __func__, ret, len_send);
                if (ret)
                    tty_flip_buffer_push(mtty_dev->port);

                if (ret == (len_send)) {
                    if (rx_num > 1) {
                        rx_head = rx_head->next;
                        dev_unisoc_bt_info(ttyBT_dev, "%s point next",__func__);
                    } else {
                        dev_unisoc_bt_dbg(ttyBT_dev,
                                          "%s send success",
                                          __func__);
                        sprdwcn_bus_push_list(chn, head, tail, num);
                        return 0;
                    }
                } else {
                    dev_unisoc_bt_info(ttyBT_dev,
                                       "%s send error", __func__);
                    return -1;
                }
            }
        }

        rx = kmalloc(sizeof(struct rx_data), GFP_KERNEL);
        if (rx == NULL) {
            dev_unisoc_bt_err(ttyBT_dev,
                                "%s rx == NULL\n",
                                __func__);
            sprdwcn_bus_push_list(chn, head, tail, num);
            return -ENOMEM;
        }

	if (atomic_read(&mtty_dev->state) == MTTY_STATE_CLOSE) {
		dev_unisoc_bt_err(ttyBT_dev,
							"%s mtty bt is closed abnormally\n",
							__func__);
		sprdwcn_bus_push_list(chn, head, tail, num);
		if (rx != NULL) {
			kfree(rx);
			rx = NULL;
		}
		return -1;
	}

        rx->head = head;
        rx->tail = tail;
        rx->channel = chn;
        rx->num = num;
        rx->head->len = (len_send) - ret;
        rx->head->buf = kmalloc(rx->head->len, GFP_KERNEL);
        if (rx->head->buf == NULL) {
            dev_unisoc_bt_err(ttyBT_dev,
                                "mtty low memory!\n");
	if (rx != NULL) {
		kfree(rx);
		rx = NULL;
	}
            sprdwcn_bus_push_list(chn, head, tail, num);
            return -ENOMEM;
        }

        memcpy(rx->head->buf, (unsigned char *)head->buf + BT_SIPC_HEAD_LEN + ret, rx->head->len);
        sprdwcn_bus_push_list(chn, head, tail, num);
        mutex_lock(&mtty_dev->rw_mutex);
        dev_unisoc_bt_err(ttyBT_dev,
                            "mtty over load push %d -> %d, channel: %d len: %d\n",
                            len_send, ret, rx->channel, rx->head->len);
        list_add_tail(&rx->entry, &mtty_dev->rx_head);
        mutex_unlock(&mtty_dev->rw_mutex);
        if (!work_pending(&mtty_dev->bt_rx_work)) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "work_pending\n");
        queue_work(mtty_dev->bt_rx_workqueue,
                    &mtty_dev->bt_rx_work);
        }
        return 0;
    }
    dev_unisoc_bt_err(ttyBT_dev,
                        "mtty_rx_cb mtty_dev is NULL!!!\n");

    return -1;
}


//sdio rx_cb
static int mtty_rx_cb(int chn, struct mbuf_t *head, struct mbuf_t *tail, int num)
{
    int ret = 0 , block_size = 0, rx_num = 0;
    struct rx_data *rx;
    struct mbuf_t *rx_head = head;

    bt_wakeup_host();

    if (atomic_read(&mtty_dev->state) == MTTY_STATE_CLOSE) {
        dev_unisoc_bt_err(ttyBT_dev,
                          "%s mtty bt is closed abnormally\n",
                          __func__);
        sprdwcn_bus_push_list(chn, head, tail, num);
        return -1;
    }

    if (mtty_dev != NULL) {
        if (!work_pending(&mtty_dev->bt_rx_work)) {
            for (rx_num = num; rx_num > 0; rx_num--) {
                block_size = ((rx_head->buf[2] & 0x7F) << 9) + (rx_head->buf[1] << 1) + (rx_head->buf[0] >> 7);

                dev_unisoc_bt_dbg(ttyBT_dev,
                                   "%s dump head: %d, channel: %d, num: %d, event size: %d\n",
                                   __func__, BT_SDIO_HEAD_LEN, chn, rx_num, block_size);
                /*if (rx_head->buf[BT_SDIO_HEAD_LEN] == 0x04) {
                    if(block_size < 32)
                       hex_dump_block((unsigned char *)rx_head->buf + BT_SDIO_HEAD_LEN, block_size);
                    else
                       hex_dump_block((unsigned char *)rx_head->buf + BT_SDIO_HEAD_LEN, 16);
                }*/

                dev_unisoc_bt_dbg(ttyBT_dev,
                                  "%s tty_insert_flip_string",
                                  __func__);
                ret = tty_insert_flip_string(mtty_dev->port,
                                            (unsigned char *)rx_head->buf + BT_SDIO_HEAD_LEN,
                                            block_size);   // -BT_SDIO_HEAD_LEN
                dev_unisoc_bt_dbg(ttyBT_dev,
                                  "%s ret: %d, len: %d\n",
                                  __func__, ret, block_size);

                if (ret)
                    tty_flip_buffer_push(mtty_dev->port);

                if (ret == (block_size)) {
                    if (rx_num > 1) {
                        rx_head = rx_head->next;
                        dev_unisoc_bt_info(ttyBT_dev, "%s point next",__func__);
                    } else {
                        dev_unisoc_bt_dbg(ttyBT_dev,
                                          "%s send success",
                                          __func__);
                        sprdwcn_bus_push_list(chn, head, tail, num);
                        return 0;
                    }
                } else {
                    dev_unisoc_bt_info(ttyBT_dev,
                                       "%s send error", __func__);
                    return -1;
                }
            }
        }

        rx = kmalloc(sizeof(struct rx_data), GFP_KERNEL);
        if (rx == NULL) {
            dev_unisoc_bt_err(ttyBT_dev,
                              "%s rx == NULL\n",
                              __func__);
            sprdwcn_bus_push_list(chn, head, tail, num);
            return -ENOMEM;
        }

        rx->head = head;
        rx->tail = tail;
        rx->channel = chn;
        rx->num = num;
        rx->head->len = (block_size) - ret;
        rx->head->buf = kmalloc(rx->head->len, GFP_KERNEL);
        if (rx->head->buf == NULL) {
            dev_unisoc_bt_err(ttyBT_dev,
                              "mtty low memory!\n");
            kfree(rx);
            sprdwcn_bus_push_list(chn, head, tail, num);
            return -ENOMEM;
        }

        memcpy(rx->head->buf, (unsigned char *)head->buf + BT_SDIO_HEAD_LEN + ret, rx->head->len);
        sprdwcn_bus_push_list(chn, head, tail, num);
        mutex_lock(&mtty_dev->rw_mutex);
        dev_unisoc_bt_err(ttyBT_dev,
                          "mtty over load push %d -> %d, channel: %d len: %d\n",
                          block_size, ret, rx->channel, rx->head->len);
        list_add_tail(&rx->entry, &mtty_dev->rx_head);
        mutex_unlock(&mtty_dev->rw_mutex);
        if (!work_pending(&mtty_dev->bt_rx_work)) {
        dev_unisoc_bt_err(ttyBT_dev,
                          "work_pending\n");
            queue_work(mtty_dev->bt_rx_workqueue,
                        &mtty_dev->bt_rx_work);
        }
        return 0;
    }
    dev_unisoc_bt_err(ttyBT_dev,
                        "mtty_rx_cb mtty_dev is NULL!!!\n");

    return -1;
}

//pcie rx_cb
static int mtty_pcie_rx_cb(int chn, struct mbuf_t *head, struct mbuf_t *tail, int num)
{
    int ret = 0, len_send, rx_num;
    struct rx_data *rx;
    unsigned char *sdio_buf = NULL;
    struct mbuf_t *rx_head = head;
    sdio_buf = (unsigned char *)head->buf;
    bt_wakeup_host();

    len_send = head->len;

    BT_VER("%s() channel: %d, num: %d\n", __func__, chn, num);
    BT_VER("%s() ---mtty receive channel= %d, len_send = %d\n", __func__, chn, len_send);

    if (atomic_read(&mtty_dev->state) != MTTY_STATE_OPEN) {
        pr_err("%s() mtty bt is closed abnormally\n", __func__);

        sprdwcn_bus_push_list(chn, head, tail, num);
        return -1;
    }

    if (mtty_dev != NULL) {
        if (!work_pending(&mtty_dev->bt_rx_work)) {
            BT_VER("%s() tty_insert_flip_string", __func__);
            for (rx_num = num; rx_num > 0; rx_num--) {
                len_send = head->len;
                /*if (rx_head->buf[BT_PCIE_SDIO_HEAD_LEN] == 0x04) {
                    if(len_send < 32)
                       hex_dump_block((unsigned char *)rx_head->buf + BT_PCIE_SDIO_HEAD_LEN, len_send);
                    else
                       hex_dump_block((unsigned char *)rx_head->buf + BT_PCIE_SDIO_HEAD_LEN, 16);
                }*/
                ret = tty_insert_flip_string(mtty_dev->port0,
                            (unsigned char *)rx_head->buf+BT_PCIE_SDIO_HEAD_LEN,
                            len_send);
                pr_info("%s ret: %d, len: %d\n", __func__, ret, len_send);

                if (ret)
                    tty_flip_buffer_push(mtty_dev->port0);

                if (ret == (len_send)) {
                    if (rx_num > 1) {
                        rx_head = rx_head->next;
                        dev_unisoc_bt_info(ttyBT_dev, "%s point next",__func__);
                    } else {
                        dev_unisoc_bt_dbg(ttyBT_dev,
                                          "%s send success",
                                          __func__);
                        sprdwcn_bus_push_list(chn, head, tail, num);
                        return 0;
                    }
                } else {
                    dev_unisoc_bt_info(ttyBT_dev,
                                       "%s send error", __func__);
                    return -1;
                }
            }
        }

        mutex_lock(&mtty_dev->rw_mutex);
        rx = kmalloc(sizeof(struct rx_data), GFP_KERNEL);
        if (rx == NULL) {
            pr_err("%s() rx == NULL\n", __func__);
            sprdwcn_bus_push_list(chn, head, tail, num);
            mutex_unlock(&mtty_dev->rw_mutex);
            return -ENOMEM;
        }

        rx->head = head;
        rx->tail = tail;
        rx->channel = chn;
        rx->num = num;
        rx->head->len = (len_send) - ret;
        rx->head->buf = kmalloc(rx->head->len, GFP_KERNEL);
        if (rx->head->buf == NULL) {
            pr_err("mtty low memory!\n");
            kfree(rx);
            sprdwcn_bus_push_list(chn, head, tail, num);
            mutex_unlock(&mtty_dev->rw_mutex);
            return -ENOMEM;
        }

        memcpy(rx->head->buf, head->buf, rx->head->len);
        sprdwcn_bus_push_list(chn, head, tail, num);
        //mutex_lock(&mtty_dev->rw_mutex);
        BT_VER("mtty over load push %d -> %d, channel: %d len: %d\n",
                len_send, ret, rx->channel, rx->head->len);
        list_add_tail(&rx->entry, &mtty_dev->rx_head);
        mutex_unlock(&mtty_dev->rw_mutex);
        if (!work_pending(&mtty_dev->bt_rx_work)) {
            BT_VER("work_pending\n");
            queue_work(mtty_dev->bt_rx_workqueue,
                        &mtty_dev->bt_rx_work);
        }
        return 0;
    }
    pr_err("mtty_rx_cb mtty_dev is NULL!!!\n");

    return -1;
}

//try to tx_cb  adapt
static int mtty_tx_cb(int chn, struct mbuf_t *head, struct mbuf_t *tail, int num)
{
    int i;
    struct mbuf_t *pos = NULL;
    dev_unisoc_bt_dbg(ttyBT_dev,
                      "%s channel: %d, head: %p, tail: %p num: %d\n",
                      __func__, chn, head, tail, num);
    pos = head;
    for (i = 0; i < num; i++, pos = pos->next) {
        kfree(pos->buf);
        pos->buf = NULL;
    }
    if ((sprdwcn_bus_list_free(chn, head, tail, num)) == 0)
    {
        dev_unisoc_bt_dbg(ttyBT_dev,
                          "%s sprdwcn_bus_list_free() success\n",
                          __func__);
        up(&sem_id);
    }
    else
        dev_unisoc_bt_err(ttyBT_dev,
                          "%s sprdwcn_bus_list_free() fail\n",
                          __func__);

    return 0;
}

static int mtty_pcie_tx_cb(int chn, struct mbuf_t *head, struct mbuf_t *tail, int num)
{
    int i;
    struct mbuf_t *pos = NULL;
    BT_VER("%s channel: %d, head: %p, tail: %p num: %d mtty_dev %p pdev %p\n",
             __func__, chn, head, tail, num, mtty_dev, mtty_dev->pdev);
    pos = head;
    BT_VER("mtty_close mtty_dev->state = %d !\n", atomic_read(&mtty_dev->state));
    for (i = 0; i < num; i++, pos = pos->next) {
        struct device *dm = &mtty_dev->pdev->dev;
        if ((atomic_read(&mtty_dev->state) == MTTY_STATE_CLOSE) || (pos == NULL)) {
            pr_err("mtty_tx_cb error, return\n");
            up(&sem_id);
            return -1;
        }
        dma_free_coherent(dm, pos->len, (void *)pos->buf, head->phy);
        pos->buf = NULL;
    }
    if ((sprdwcn_bus_list_free(chn, head, tail, num)) == 0)
    {
        BT_VER("%s sprdwcn_bus_list_free() success\n", __func__);
    }
    else
        pr_err("%s sprdwcn_bus_list_free() fail\n", __func__);
    up(&sem_id);
    return 0;
}

static int rx_push(int chn,struct mbuf_t **head, struct mbuf_t **tail, int *num) {
    pr_err("%s no buf, rx_push called \n", __func__);
    return 0;
}

/**************open function****************/
//sdio open
static int mtty_sdio_open(struct tty_struct *tty, struct file *filp)
{
    struct mtty_device *mtty = NULL;
    struct tty_driver *driver = NULL;
    int ret = -1;

    dev_unisoc_bt_info(ttyBT_dev,"mtty_open\n");
    if (tty == NULL) {
        dev_unisoc_bt_err(ttyBT_dev,
                          "mtty open input tty is NULL!\n");
        return -ENOMEM;
    }
    driver = tty->driver;
    mtty = (struct mtty_device *)driver->driver_state;

    if (mtty == NULL) {
        dev_unisoc_bt_err(ttyBT_dev,
                          "mtty open input mtty NULL!\n");
        return -ENOMEM;
    }
   
    mtty->tty = tty;
    tty->driver_data = (void *)mtty;
  
    atomic_set(&mtty->state, MTTY_STATE_OPEN);
    que_task = 0;
    que_sche = 0;
    sitm_ini();
    dev_unisoc_bt_info(ttyBT_dev,
                       "mtty_open device success!\n");
    ret = start_marlin(MARLIN_BLUETOOTH);
    dev_unisoc_bt_info(ttyBT_dev,
                       "mtty_open power on state ret = %d!\n",
                       ret);

    return 0;
}

//sipc open
static int mtty_sipc_open(struct tty_struct *tty, struct file *filp)
{
    struct mtty_device *mtty = NULL;
    struct tty_driver *driver = NULL;
    int ret = -1;

    dev_unisoc_bt_info(ttyBT_dev,"mtty_open\n");

    if (tty == NULL) {
        dev_unisoc_bt_err(ttyBT_dev,"mtty open input tty is NULL!\n");
        return -ENOMEM;
    }
    driver = tty->driver;
    mtty = (struct mtty_device *)driver->driver_state;

    if (mtty == NULL) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "mtty open input mtty NULL!\n");
        return -ENOMEM;
    }
    //dev_unisoc_bt_err(ttyBT_dev,"dst:%d, channel:%d\n",mtty->pdata->dst,mtty->pdata->channel);
    mtty->tty = tty;
    tty->driver_data = (void *)mtty;
    m_tty = tty;
    atomic_set(&mtty->state, MTTY_STATE_OPEN);

    #ifdef CONFIG_ARCH_SCX20
    rf2351_vddwpa_ctrl_power_enable(1);
    #endif

    dev_unisoc_bt_info(ttyBT_dev,"mtty_open device success!\n");
    sitm_ini();
    #if 0
    mtty_address_init();
    #endif
    ret = start_marlin(MARLIN_BLUETOOTH);
    dev_unisoc_bt_info(ttyBT_dev,
                        "mtty_open power on state ret = %d!\n",
                        ret);
    return 0;
}

//mtty_pcie_open
static int mtty_pcie_open(struct tty_struct *tty, struct file *filp)
{
    struct mtty_device *mtty = NULL;
    struct tty_driver *driver = NULL;
    int ret = -1;
    if (set_power_ret != 0) {
        pr_err("mtty_open : set power failed , return!\n");
        return -1;
    }
    if (tty == NULL) {
        pr_err("mtty open input tty is NULL!\n");
        return -ENOMEM;
    }
    driver = tty->driver;
    mtty = (struct mtty_device *)driver->driver_state;

    if (mtty == NULL) {
        pr_err("mtty open input mtty NULL!\n");
        return -ENOMEM;
    }

    mtty->tty = tty;
    tty->driver_data = (void *)mtty;

    atomic_set(&mtty->state, MTTY_STATE_OPEN);
    que_task = 0;
    que_sche = 0;
    sitm_ini();
    ret = start_marlin(MARLIN_BLUETOOTH);
    pr_info("mtty_open power on state ret = %d!\n", ret);
    sprdwcn_bus_chn_init(&bt_pcie_rx_ops);
    sprdwcn_bus_chn_init(&bt_pcie_tx_ops0);
    mtty_dma_buf_alloc(BT_PCIE_RX_CHANNEL, BT_PCIE_RX_DMA_SIZE, BT_PCIE_RX_MAX_NUM);
    pr_info("mtty_open device success!\n");

    return 0;
}

/*
 * e5-linux: tell the BT core it is being switched off, as Android's vendor
 * HAL does right before it closes this tty: vendor command 0xfca1 00 00 00,
 * the "core disable" counterpart of the 00 00 01 that enables it.  Only then
 * does the WCN CP delete its BT thread when stop_marlin() asks (17 ms on
 * Android).  Without it stop_marlin() waits the full 30 s CP_TIMEROUT for
 * that thread-delete interrupt with the WCN power lock held, and Wi-Fi's
 * start_marlin/stop_marlin queue behind it: on a close (btattach exiting, as
 * at every shutdown) NetworkManager and wpa_supplicant taking wlan0 down sat
 * unkillable for 21 s; on the "bluetooth" rfkill block systemd-rfkill
 * restores at boot, wlan0 came up 30 s late.  The HAL has the Command
 * Complete ~20 ms later; wait a little longer than that.
 */
static void mtty_core_disable_tty(struct tty_struct *tty)
{
	static const unsigned char core_disable[] = {
		0x01, 0xa1, 0xfc, 0x03, 0x00, 0x00, 0x00 };

	if (tty->ops && tty->ops->write) {
		tty->ops->write(tty, core_disable, sizeof(core_disable));
		msleep(50);
	}
}

/* Called by the rfkill switch before it powers BT off under an open tty. */
void mtty_core_disable(void)
{
	if (!mtty_dev || !mtty_dev->tty ||
	    atomic_read(&mtty_dev->state) != MTTY_STATE_OPEN)
		return;
	if (!(marlin_get_power() & BIT(MARLIN_BLUETOOTH)))
		return;
	mtty_core_disable_tty(mtty_dev->tty);
}

//try to a close
static void mtty_close(struct tty_struct *tty, struct file *filp)
{
    struct mtty_device *mtty = NULL;
    int ret = -1;
    dev_unisoc_bt_info(ttyBT_dev,
                            "mtty_close\n");

    if (tty == NULL) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "mtty close input tty is NULL!\n");
        return;
    }
    mtty = (struct mtty_device *) tty->driver_data;
    if (mtty == NULL) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "mtty close s tty is NULL!\n");
        return;
    }

	if (atomic_read(&mtty->state) == MTTY_STATE_CLOSE) {
		dev_unisoc_bt_err(ttyBT_dev,
							"mtty status alredy, status:%d\n",
							atomic_read(&mtty->state));
		return;
	}

	mtty_core_disable_tty(tty);

	atomic_set(&mtty->state, MTTY_STATE_CLOSE);
	sitm_cleanup();
	ret = stop_marlin(MARLIN_BLUETOOTH);

	dev_unisoc_bt_info(ttyBT_dev,
						"close device success !\n");

	dev_unisoc_bt_info(ttyBT_dev,
						"power off state ret = %d!\n",
						ret);
}

//pcie_close
static void mtty_pcie_close(struct tty_struct *tty, struct file *filp)
{
    struct mtty_device *mtty = NULL;
	int ret = -1;
    mtty_dma_buf_free(BT_PCIE_RX_MAX_NUM);
    if (tty == NULL) {
        pr_err("mtty close input tty is NULL!\n");
        return;
    }
    mtty = (struct mtty_device *) tty->driver_data;
    if (mtty == NULL) {
        pr_err("mtty close s tty is NULL!\n");
        return;
    }

	if (atomic_read(&mtty->state) == MTTY_STATE_CLOSE) {
		pr_err("mtty status alredy, status:%d\n",
							atomic_read(&mtty->state));
		return;
	}

    atomic_set(&mtty->state, MTTY_STATE_CLOSE);
    sprdwcn_bus_chn_deinit(&bt_pcie_rx_ops);
    sprdwcn_bus_chn_deinit(&bt_pcie_tx_ops0);
    sitm_cleanup();
    ret = stop_marlin(MARLIN_BLUETOOTH);
	pr_info("power off state ret = %d!\n", ret);
}

/*******************write function***************/
//sipc_write
static int mtty_sipc_write(struct tty_struct *tty,
                      const unsigned char *buf,
                      int count)
{
    struct mtty_device *mtty = tty->driver_data;
    int write_length = 0, left_legnth = 0;
    if(COMMAND_HEAD == buf[0]){
        dev_unisoc_bt_info(ttyBT_dev,
                            "%s bufwrite_length = %d\n",
                            __func__, count);
        if(count <= 16){
            hex_dump_block((unsigned char *)buf, count);
        }
        else{
            hex_dump_block((unsigned char*)buf, 8);
            hex_dump_block((unsigned char*)(buf+count-8), 8);
        }
    }
    left_legnth = count;
    while(left_legnth > 0) {
        write_length = sbuf_write(mtty->pdata->dst,
        mtty->pdata->channel,
        mtty->pdata->tx_bufid,
        (void *)(buf + count - left_legnth), left_legnth, -1);
        left_legnth = left_legnth - write_length;
        if (left_legnth > 0) {
            dev_unisoc_bt_info(ttyBT_dev,
                "mtty write bufwrite_length = %d,date count is:%d, left_legnth = %d\n",
                write_length, count,  left_legnth);
        }
    }
    return left_legnth;
}

/*****************sdio write*************/
static int mtty_sdio_write(struct tty_struct *tty,
            const unsigned char *buf, int count)
{
    int num = 1, ret;
    struct mbuf_t *tx_head = NULL, *tx_tail = NULL;
    unsigned char *block = NULL;

    dev_unisoc_bt_dbg(ttyBT_dev,
                      "%s +++\n",
                      __func__);
    if (buf[0] == COMMAND_HEAD) {
        dev_unisoc_bt_info(ttyBT_dev,
                           "%s dump size: %d\n",
                           __func__, count);
        if (count < 16) {
            hex_dump_block((unsigned char *)buf, count);
        }
        else {
            hex_dump_block((unsigned char*)buf, 16);
        }
    }

    block = kmalloc(count + BT_SDIO_HEAD_LEN, GFP_KERNEL);

    if (!block) {
        dev_unisoc_bt_err(ttyBT_dev,
                          "%s kmalloc failed\n",
                          __func__);
        return -ENOMEM;
    }
    memset(block, 0, count + BT_SDIO_HEAD_LEN);
    memcpy(block + BT_SDIO_HEAD_LEN, buf, count);
    down(&sem_id);
    ret = sprdwcn_bus_list_alloc(BT_TX_CHANNEL, &tx_head, &tx_tail, &num);
    if (ret) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "%s sprdwcn_bus_list_alloc failed: %d\n",
                            __func__, ret);
        up(&sem_id);
        kfree(block);
        block = NULL;
        return -ENOMEM;
    }
    tx_head->buf = block;
    tx_head->len = count;
    tx_head->next = NULL;

    ret = sprdwcn_bus_push_list(BT_TX_CHANNEL, tx_head, tx_tail, num);
    if (ret) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "%s sprdwcn_bus_push_list failed: %d\n",
                            __func__, ret);
        kfree(tx_head->buf);
        tx_head->buf = NULL;
        sprdwcn_bus_list_free(BT_TX_CHANNEL, tx_head, tx_tail, num);
        up(&sem_id);
        return -EBUSY;
    }

    dev_unisoc_bt_dbg(ttyBT_dev,
                        "%s ---\n",
                        __func__);
    return count;
}

//sipc2 write
static int mtty_sipc2_write(struct tty_struct *tty,
            const unsigned char *buf, int count)
{
    int num = 1, ret;
    struct mbuf_t *tx_head = NULL, *tx_tail = NULL;
    unsigned char *block = NULL;
    char phone_info[15] = "";

    if (log_level == MTTY_LOG_LEVEL_VER) {
        if (buf[0] == COMMAND_HEAD) {
            dev_unisoc_bt_err(ttyBT_dev,
                                "%s dump cmd %02X %02X %02X %02X \n",
                                __func__, buf[0], buf[1], buf[2],buf[3]);
        }
    }

    block = kmalloc(count + BT_SIPC_HEAD_LEN, GFP_KERNEL);

    if (!block) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "%s kmalloc failed\n",
                            __func__);
        return -ENOMEM;
    }
    memset(block, 0, count + BT_SIPC_HEAD_LEN);
    memcpy(block + BT_SIPC_HEAD_LEN, buf, count);
    ret = down_timeout(&sem_id,msecs_to_jiffies(DOWN_ACQUIRE_TIMEOUT_MS));
    if (ret) {
        dev_unisoc_bt_err(ttyBT_dev,"%s acquire sem fail",__func__);
        kfree(block);
        block = NULL;
        return ret;
    }
    ret = sprdwcn_bus_list_alloc(BT_SIPC_TX_CHANNEL, &tx_head, &tx_tail, &num);
    if (ret) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "%s sprdwcn_bus_list_alloc failed: %d\n",
                            __func__, ret);
        up(&sem_id);
        kfree(block);
        block = NULL;
        return -ENOMEM;
    }

    if (block[BT_SIPC_HEAD_LEN] == ISO_HEAD && (block[BT_SIPC_HEAD_LEN + 4]&0xC0)) {
            block[BT_SIPC_HEAD_LEN + 4] &= 0x3f;
            dev_unisoc_bt_info(ttyBT_dev,
                                "%s dump ISO %02X %02X %02X %02X \n",
                                __func__, block[0], block[1], block[2],block[3]);
    }

    if (block[BT_SIPC_HEAD_LEN] == COMMAND_HEAD && (block[BT_SIPC_HEAD_LEN + 1]== 0xA1)
        && (block[BT_SIPC_HEAD_LEN + 2]== 0xFC) && (block[BT_SIPC_HEAD_LEN + 6]== 0x01)) {
        if (SET_BT_VERSION) {
            ret = sprd_bt_read_soc_version(phone_info);
            pr_info("read soc version:%s\n", phone_info);
            if (ret) {
                pr_err("can't read soc version, phone info:%s\n", phone_info);
            } else if (!strcmp(phone_info, "UMS9230") || !strcmp(phone_info, "UMS9230T")) {
                block[BT_SIPC_HEAD_LEN + 5] = BT_VERSION_5_0;
            } else if (!strcmp(phone_info, "UMS9230H") || !strcmp(phone_info, "UMS9230E")
                || !strcmp(phone_info, "UMS9230S") || !strcmp(phone_info, "UMS9230M")) {
                block[BT_SIPC_HEAD_LEN + 5] = BT_VERSION_5_2;
            }
            dev_unisoc_bt_info(ttyBT_dev,
                "%s dump enable cmd: %02X %02X %02X %02X %02X %02X %02X\n",
                __func__, block[0], block[1], block[2],block[3], block[4], block[5], block[6]);
        }
    }

    tx_head->buf = block;
    tx_head->len = count;
    tx_head->next = NULL;

    ret = sprdwcn_bus_push_list(BT_SIPC_TX_CHANNEL, tx_head, tx_tail, num);
    if (ret) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "%s sprdwcn_bus_push_list failed: %d\n",
                            __func__, ret);
        kfree(tx_head->buf);
        tx_head->buf = NULL;
        sprdwcn_bus_list_free(BT_SIPC_TX_CHANNEL, tx_head, tx_tail, num);
        up(&sem_id);
        return -EBUSY;
    }

    return count;
}

//pcie_write
static int mtty_pcie_write(struct tty_struct *tty,
            const unsigned char *buf, int count)
{
    int num = 1;
    struct mbuf_t *tx_head = NULL;
    struct mbuf_t *tx_tail = NULL;

    down(&sem_id);
    if (!sprdwcn_bus_list_alloc(BT_PCIE_TX_CHANNEL0, &tx_head, &tx_tail, &num)) {
        int ret = 0;
        struct device *dm = &mtty_dev->pdev->dev;
        BT_VER("%s() sprdwcn_bus_list_alloc() success tx_head %p tx_tail %p num %d mtty_dev->tty->dev %p\n",
                __func__, tx_head, tx_tail, num, mtty_dev->tty->dev);
        if ((ret = dma_set_mask(dm, DMA_BIT_MASK(64)))) {
            printk(KERN_ERR "dma_set_mask err ret %d\n", ret);
            if ((ret = dma_set_coherent_mask(dm, DMA_BIT_MASK(64)))) {
                printk(KERN_ERR "dma_set_coherent_mask err ret %d\n", ret);
                return -ENOMEM;
            }
        }
        tx_head->buf = (unsigned char *)dma_alloc_coherent(dm, count, (dma_addr_t *)(&(tx_head->phy)), GFP_DMA);

        if(!tx_head->buf)
        {
            pr_err("%s:line:%d dma_alloc_coherent err dev %p count %d phy %p\n",
                    __func__, __LINE__, mtty_dev->tty->dev, count, &(tx_head->phy));
            return -ENOMEM;
        }
        memcpy(tx_head->buf, buf, count);
        tx_head->len = count;
        tx_head->next = NULL;
        /*packer type 0, subtype 0*/
        BT_VER("%s sprdwcn_bus_push_list num: %d ++\n", __func__, num);
        ret = sprdwcn_bus_push_list(BT_PCIE_TX_CHANNEL0, tx_head, tx_tail, num);
        BT_VER("%s sprdwcn_bus_push_list ret: %d --\n", __func__, ret);
        if (ret)
        {
            dma_free_coherent(dm, count, (void *)tx_head->buf, tx_head->phy);
            tx_head->buf = NULL;
            sprdwcn_bus_list_free(BT_PCIE_TX_CHANNEL0, tx_head, tx_tail, num);
            up(&sem_id);
            return -EBUSY;
        }
        else
        {
            BT_VER("%s() sprdwcn_bus_push_list() success\n", __func__);
            return count;
        }
    } else {
        pr_err("%s:%d sprdwcn_bus_list_alloc fail\n", __func__, __LINE__);
        up(&sem_id);
        return -ENOMEM;
    }
}

/****************write function*******************/

static  int sipc_data_transmit(uint8_t *data, size_t count)
{
    return mtty_sipc_write(m_tty, data, count);
}

static  int sdio_data_transmit(uint8_t *data, size_t count)
{
    return mtty_sdio_write(NULL, data, count);
}

static  int sipc2_data_transmit(uint8_t *data, size_t count)
{
    return mtty_sipc2_write(m_tty, data, count);
}

static  int pcie_data_transmit(uint8_t *data, size_t count)
{
    return mtty_pcie_write(NULL, data, count);
}

#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
static ssize_t mtty_sipc_write_plus(struct tty_struct *tty,
		const unsigned char *buf, size_t count)
#else
static int mtty_sipc_write_plus(struct tty_struct *tty,
		const unsigned char *buf, int count)
#endif
{
	int ret = -1;
	struct mtty_device *mtty = NULL;

	if (tty == NULL) {
		dev_unisoc_bt_err(ttyBT_dev,
							"stty closed, tty is NULL!\n");
		return count;
	}
	mtty = (struct mtty_device *) tty->driver_data;

	if (mtty == NULL) {
		dev_unisoc_bt_err(ttyBT_dev,
							"stty closed, stty is NULL!\n");
		return count;
	}

	if (atomic_read(&mtty->state) == MTTY_STATE_CLOSE) {
		dev_unisoc_bt_err(ttyBT_dev,
							"stty status isn't open, status:%d\n",
							atomic_read(&mtty->state));
		return count;
	}

	ret = sitm_write(buf, count, sipc_data_transmit);
	return ret;
}

#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
static ssize_t mtty_sdio_write_plus(struct tty_struct *tty,
		const unsigned char *buf, size_t count)
#else
static int mtty_sdio_write_plus(struct tty_struct *tty,
		const unsigned char *buf, int count)
#endif
{
	int ret = -1;
	struct mtty_device *mtty = NULL;

	if (tty == NULL) {
		dev_unisoc_bt_err(ttyBT_dev,
							"stty closed, tty is NULL!\n");
		return count;
	}
	mtty = (struct mtty_device *) tty->driver_data;

	if (mtty == NULL) {
		dev_unisoc_bt_err(ttyBT_dev,
							"stty closed, stty is NULL!\n");
		return count;
	}

	if (atomic_read(&mtty->state) == MTTY_STATE_CLOSE) {
		dev_unisoc_bt_err(ttyBT_dev,
							"stty status isn't open, status:%d\n",
							atomic_read(&mtty->state));
		return count;
	}

	/*
	 * e5-linux: nothing goes to a BT core that is powered off.  The
	 * "bluetooth" rfkill switch calls stop_marlin(MARLIN_BLUETOOTH) with
	 * this tty still open: systemd-rfkill restoring a saved "off" at boot,
	 * or BT switched off in the shell.  bluetoothd keeps driving hci0 all
	 * the same, and the first command it wrote -- an HCI Reset -- went
	 * down the SDIO bus Wi-Fi shares to a subsystem that was gone: the
	 * transfer never completed, sdiohal's tx thread sat on the bus's
	 * xmit_lock, and four seconds later the Wi-Fi firmware asserted
	 * (sc2355_assert_cmd reason 3) and wlan0 stayed dead until a reboot,
	 * hotspot and all.  Dropped here instead: hci0's command times out,
	 * as it should with BT off.
	 */
	if (!(marlin_get_power() & BIT(MARLIN_BLUETOOTH))) {
		pr_err_ratelimited("mtty: BT is powered off, dropping %zu bytes\n",
				   (size_t)count);
		return count;
	}

	ret = sitm_write(buf, count, sdio_data_transmit);
	return ret;
}

#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
static ssize_t mtty_pcie_write_plus(struct tty_struct *tty,
		const unsigned char *buf, size_t count)
#else
static int mtty_pcie_write_plus(struct tty_struct *tty,
		const unsigned char *buf, int count)
#endif
{
	int ret = 0;
	struct mtty_device *mtty = NULL;

	if (tty == NULL) {
		dev_unisoc_bt_err(ttyBT_dev,
							"mtty close input tty is NULL!\n");
		return count;
	}

	mtty = (struct mtty_device *) tty->driver_data;
	if (mtty == NULL) {
		dev_unisoc_bt_err(ttyBT_dev,
							"mtty close tty is NULL!\n");
		return count;
	}

	if (atomic_read(&mtty->state) == MTTY_STATE_CLOSE) {
		dev_unisoc_bt_err(ttyBT_dev,
							"mtty status isn't open, status:%d\n",
							atomic_read(&mtty->state));
		return count;
	}

	ret = sitm_write(buf, count, pcie_data_transmit);
	return ret;
}

#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
static ssize_t mtty_sipc2_write_plus(struct tty_struct *tty,
		const unsigned char *buf, size_t count)
#else
static int mtty_sipc2_write_plus(struct tty_struct *tty,
		const unsigned char *buf, int count)
#endif
{
	int ret = -1;
	struct mtty_device *mtty = NULL;

	if (tty == NULL) {
		dev_unisoc_bt_err(ttyBT_dev,
							"stty closed, tty is NULL!\n");
		return count;
	}
	mtty = (struct mtty_device *) tty->driver_data;

	if (mtty == NULL) {
		dev_unisoc_bt_err(ttyBT_dev,
							"stty closed, stty is NULL!\n");
		return count;
	}

	if (atomic_read(&mtty->state) == MTTY_STATE_CLOSE) {
		dev_unisoc_bt_err(ttyBT_dev,
							"stty status isn't open, status:%d\n",
							atomic_read(&mtty->state));
		return count;
	}

	ret = sitm_write(buf, count, sipc2_data_transmit);
	return ret;
}


static void mtty_flush_chars(struct tty_struct *tty)
{

}
#if(LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0))
static unsigned int mtty_write_room(struct tty_struct *tty)
{
    return INT_MAX;
}
#else
static int mtty_write_room(struct tty_struct *tty)
{
    return INT_MAX;
}
#endif

/*********************tty_operations**************/
static const struct tty_operations mtty_sipc_ops = {
    .open  = mtty_sipc_open,
    .close = mtty_close,
    .write = mtty_sipc_write_plus,
    .flush_chars = mtty_flush_chars,
    .write_room  = mtty_write_room,
};

static const struct tty_operations mtty_sdio_ops = {
    .open  = mtty_sdio_open,
    .close = mtty_close,
    .write = mtty_sdio_write_plus,
    .flush_chars = mtty_flush_chars,
    .write_room  = mtty_write_room,
};

static const struct tty_operations mtty_sipc2_ops = {
    .open  = mtty_sipc_open,
    .close = mtty_close,
    .write = mtty_sipc2_write_plus,
    .flush_chars = mtty_flush_chars,
    .write_room  = mtty_write_room,
};

static const struct tty_operations mtty_pcie_ops = {
    .open  = mtty_pcie_open,
    .close = mtty_pcie_close,
    .write = mtty_pcie_write_plus,
    .flush_chars = mtty_flush_chars,
    .write_room  = mtty_write_room,
};

static struct tty_port *mtty_port_init(void)
{
    struct tty_port *port = NULL;

    port = kzalloc(sizeof(struct tty_port), GFP_KERNEL);
    if (port == NULL)
        return NULL;
    tty_port_init(port);

    return port;
}

/*****************driver_init_function******************/


//sipc_driver_init
static int mtty_sipc_driver_init(struct mtty_device *device)
{
    struct tty_driver *driver;
    int ret = 0;

    mutex_init(&(device->stat_lock));

    device->port = mtty_port_init();
    if (!device->port)
        return -ENOMEM;
    #if(LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0))
    driver = tty_alloc_driver(MTTY_DEV_MAX_NR,0);
    #else
    driver = alloc_tty_driver(MTTY_DEV_MAX_NR);
    #endif
    if (!driver) {
        kfree(device->port);
        return -ENOMEM;
    }

    /*
        * Initialize the tty_driver structure
        * Entries in mtty_driver that are NOT initialized:
        * proc_entry, set_termios, flush_buffer, set_ldisc, write_proc
        */
    driver->owner = THIS_MODULE;
    driver->driver_name = device->pdata->name;
    driver->name = device->pdata->name;
    driver->major = 0;
    driver->type = TTY_DRIVER_TYPE_SYSTEM;
    driver->subtype = SYSTEM_TYPE_TTY;
    driver->init_termios = tty_std_termios;
    driver->driver_state = (void *)device;
    device->driver = driver;
        /* initialize the tty driver */
    tty_set_operations(driver, &mtty_sipc_ops);
    tty_port_link_device(device->port, driver, 0);
    ret = tty_register_driver(driver);
    if (ret) {
        #if(LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0))
        tty_driver_kref_put(driver);
        #else
        put_tty_driver(driver);
        #endif
        tty_port_destroy(device->port);
        kfree(device->port);
        return ret;
    }
    return ret;
}

//sdio_driver_init

static int mtty_sdio_driver_init(struct mtty_device *device)
{
    struct tty_driver *driver;
    int ret = 0;

    device->port = mtty_port_init();
    if (!device->port)
        return -ENOMEM;

    #if(LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0))
    driver = tty_alloc_driver(MTTY_DEV_MAX_NR,0);
    #else
    driver = alloc_tty_driver(MTTY_DEV_MAX_NR);
    #endif
    if (!driver)
        return -ENOMEM;

    /*
    * Initialize the tty_driver structure
    * Entries in mtty_driver that are NOT initialized:
    * proc_entry, set_termios, flush_buffer, set_ldisc, write_proc
    */
    driver->owner = THIS_MODULE;
    driver->driver_name = device->pdata->name;
    driver->name = device->pdata->name;
    driver->major = 0;
    driver->type = TTY_DRIVER_TYPE_SYSTEM;
    driver->subtype = SYSTEM_TYPE_TTY;
    driver->init_termios = tty_std_termios;
    driver->driver_state = (void *)device;
    device->driver = driver;
    device->driver->flags = TTY_DRIVER_REAL_RAW;
    /* initialize the tty driver */
    tty_set_operations(driver, &mtty_sdio_ops);
    tty_port_link_device(device->port, driver, 0);
    ret = tty_register_driver(driver);
    if (ret) {
        #if(LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0))
        tty_driver_kref_put(driver);
        #else
        put_tty_driver(driver);
        #endif
        tty_port_destroy(device->port);
        return ret;
    }
    return ret;
}

//sipc2_driver_init
static int mtty_sipc2_driver_init(struct mtty_device *device)
{
    struct tty_driver *driver;
    int ret = 0;

    mutex_init(&(device->stat_lock));

    device->port = mtty_port_init();
    if (!device->port)
        return -ENOMEM;

    #if(LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0))
    driver = tty_alloc_driver(MTTY_DEV_MAX_NR,0);
    #else
    driver = alloc_tty_driver(MTTY_DEV_MAX_NR);
    #endif
    if (!driver) {
        kfree(device->port);
        return -ENOMEM;
    }

    /*
        * Initialize the tty_driver structure
        * Entries in mtty_driver that are NOT initialized:
        * proc_entry, set_termios, flush_buffer, set_ldisc, write_proc
        */
    driver->owner = THIS_MODULE;
    driver->driver_name = device->pdata->name;
    driver->name = device->pdata->name;
    driver->major = 0;
    driver->type = TTY_DRIVER_TYPE_SYSTEM;
    driver->subtype = SYSTEM_TYPE_TTY;
    driver->init_termios = tty_std_termios;
    driver->driver_state = (void *)device;
    device->driver = driver;
        /* initialize the tty driver */
    tty_set_operations(driver, &mtty_sipc2_ops);
    tty_port_link_device(device->port, driver, 0);
    ret = tty_register_driver(driver);
    if (ret) {
        #if(LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0))
        tty_driver_kref_put(driver);
        #else
        put_tty_driver(driver);
        #endif
        tty_port_destroy(device->port);
        kfree(device->port);
        return ret;
    }
    return ret;
}

//pcie_driver_init

static int mtty_pcie_driver_init(struct mtty_device *device)
{
    struct tty_driver *driver;
    int ret = 0;

    device->port0 = mtty_port_init();
    if (!device->port0)
        return -ENOMEM;

    device->port1 = mtty_port_init();
    if (!device->port1)
        return -ENOMEM;

    #if(LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0))
    driver = tty_alloc_driver(MTTY_DEV_MAX_NR,0);
    #else
    driver = alloc_tty_driver(MTTY_DEV_MAX_NR);
    #endif
    if (!driver)
        return -ENOMEM;

    /*
    * Initialize the tty_driver structure
    * Entries in mtty_driver that are NOT initialized:
    * proc_entry, set_termios, flush_buffer, set_ldisc, write_proc
    */
    driver->owner = THIS_MODULE;
    driver->driver_name = device->pdata->name;
    driver->name = device->pdata->name;
    driver->major = 0;
    driver->type = TTY_DRIVER_TYPE_SYSTEM;
    driver->subtype = SYSTEM_TYPE_TTY;
    driver->init_termios = tty_std_termios;
    driver->driver_state = (void *)device;
    device->driver = driver;
    device->driver->flags = TTY_DRIVER_REAL_RAW;
    /* initialize the tty driver */
    tty_set_operations(driver, &mtty_pcie_ops);
    tty_port_link_device(device->port0, driver, 0);
    tty_port_link_device(device->port1, driver, 1);
    ret = tty_register_driver(driver);
    if (ret) {
        #if(LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0))
        tty_driver_kref_put(driver);
        #else
        put_tty_driver(driver);
        #endif
        tty_port_destroy(device->port0);
        tty_port_destroy(device->port1);
        return ret;
    }
    return ret;
}

static void mtty_tty_driver_exit(struct mtty_device *device)
{
    struct tty_driver *driver = device->driver;

    tty_unregister_driver(driver);
    #if(LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0))
    tty_driver_kref_put(driver);
    #else
    put_tty_driver(driver);
    #endif
    if (PCIE) {
        tty_port_destroy(device->port0);
        tty_port_destroy(device->port1);
    } else {
        tty_port_destroy(device->port);
    }
}

/*****************parse dts fcuction*********************/
//sipc parse dts
static int mtty_sipc_parse_dt(struct mtty_init_data **init, struct device *dev)
{
    struct device_node *np = dev->of_node;
    struct mtty_init_data *pdata = NULL;
    int ret;

    pdata = devm_kzalloc(dev, sizeof(struct mtty_init_data), GFP_KERNEL);
    if (!pdata)
        return -ENOMEM;

    ret = of_property_read_string(np,
                        "sprd,name",
                        (const char **)&pdata->name);
    if (ret) {
        goto error;
    }
    /*sprd,dst*/
    pdata->dst = SPRD_BT_DST;
    /*sprd,channel*/
    pdata->channel = SPRD_BT_CHANNEL;
    /*sprd,tx_bufid*/
    pdata->tx_bufid = SPRD_BT_TX_BUFID;
    /*sprd,rx_bufid*/
    pdata->rx_bufid = SPRD_BT_RX_BUFID;
    /*Send the parsed data back*/
    *init = pdata;
    return 0;
error:
    devm_kfree(dev, pdata);
    *init = NULL;
    return ret;
}

//sdio and pcie parse dts
static int mtty_sdio_parse_dt(struct mtty_init_data **init, struct device *dev)
{
#ifdef CONFIG_OF
    struct device_node *np = dev->of_node;
    struct mtty_init_data *pdata = NULL;
    int ret;
    
    pdata = kzalloc(sizeof(struct mtty_init_data), GFP_KERNEL);
    if (!pdata)
        return -ENOMEM;

    ret = of_property_read_string(np,
                    "sprd,name",
                    (const char **)&pdata->name);
    if (ret)
        goto error;
    *init = pdata;

    return 0;
error:
    kfree(pdata);
    *init = NULL;
    return ret;
#else
    return -ENODEV;
#endif   
}

//sipc2 dts
static int mtty_sipc2_parse_dt(struct  mtty_init_data **init, struct device *dev)
{
    struct device_node *np = dev->of_node;
    struct  mtty_init_data *pdata = NULL;
    int ret;
    uint32_t data;

    pdata = devm_kzalloc(dev, sizeof(struct  mtty_init_data), GFP_KERNEL);
    if (!pdata){
        dev_unisoc_bt_err(ttyBT_dev,"dubugw pdata!\n");
        return -ENOMEM;
    }
    ret = of_property_read_string(np,
                                    "sprd,name",
                                    (const char **)&pdata->name);
    if (ret){
        dev_unisoc_bt_err(ttyBT_dev,"dubugw name!\n");
        goto error;
    }

    ret = of_property_read_u32(np, "sprd,dst", (uint32_t *)&data);
    if (ret){
        dev_unisoc_bt_err(ttyBT_dev,"dubugw dst!\n");
        goto error;
    }
    pdata->dst = (uint8_t)data;

    ret = of_property_read_u32(np, "sprd,channel", (uint32_t *)&data);
    if (ret){
        dev_unisoc_bt_err(ttyBT_dev,"dubugw channel!\n");
        goto error;
    }
    pdata->channel = (uint8_t)data;

    ret = of_property_read_u32(np, "sprd,tx_bufid", (uint32_t *)&pdata->tx_bufid);
    if (ret){
        dev_unisoc_bt_err(ttyBT_dev,"dubugw tx_bufid!\n");
        goto error;
    }

    ret = of_property_read_u32(np, "sprd,rx_bufid", (uint32_t *)&pdata->rx_bufid);
    if (ret){
        dev_unisoc_bt_err(ttyBT_dev,"dubugw rx_bufid!\n");
        goto error;
    }

    *init = pdata;
    return 0;
    error:
    devm_kfree(dev, pdata);
    *init = NULL;
    return ret;
}

static inline void mtty_destroy_pdata(struct mtty_init_data **init)
{
#ifdef CONFIG_OF  
    struct mtty_init_data *pdata = *init;

    kfree(pdata);

    *init = NULL;
#else
    return;
#endif
}

#define SPRDWL_MH_ADDRESS_BIT (1UL << 39)

/*****************rx_ops****************/
//sdio
struct mchn_ops_t bt_sdio_rx_ops = {
    .channel = BT_RX_CHANNEL,
    .hif_type = HW_TYPE_SDIO,
    .inout = BT_RX_INOUT,
    .pool_size = BT_RX_POOL_SIZE,
    .pop_link = mtty_rx_cb,
};

struct mchn_ops_t bt_sdio_tx_ops = {
    .channel = BT_TX_CHANNEL,
    .hif_type = HW_TYPE_SDIO,
    .inout = BT_TX_INOUT,
    .pool_size = BT_TX_POOL_SIZE,
    .pop_link = mtty_tx_cb,
};

//sipc2
struct mchn_ops_t bt_sipc2_rx_ops = {
    .channel = BT_SIPC_RX_CHANNEL,
    .hif_type = HW_TYPE_SIPC,
    .inout = BT_RX_INOUT,
    .pool_size = BT_RX_POOL_SIZE,
    .pop_link = mtty_sipc2_rx_cb,
};

struct mchn_ops_t bt_sipc2_tx_ops = {
    .channel = BT_SIPC_TX_CHANNEL,
    .hif_type = HW_TYPE_SIPC,
    .inout = BT_TX_INOUT,
    .pool_size = BT_TX_POOL_SIZE,
    .pop_link = mtty_tx_cb,
};

//pcie
struct mchn_ops_t bt_pcie_rx_ops = {
    .channel = BT_PCIE_RX_CHANNEL,
    .hif_type = HW_TYPE_PCIE,
    .inout = BT_RX_INOUT,
    .pool_size = 1,//BT_RX_POOL_SIZE,
    .pop_link = mtty_pcie_rx_cb,
    .push_link = rx_push
};

struct mchn_ops_t bt_pcie_tx_ops0 = {
    .channel = BT_PCIE_TX_CHANNEL0,
    .hif_type = HW_TYPE_PCIE,
    .inout = BT_TX_INOUT,
    .pool_size = BT_PCIE_TX_POOL_SIZE0,
    .cb_in_irq = 0,
    .max_pending = 16,
    .pop_link = mtty_pcie_tx_cb,
};

struct mchn_ops_t bt_pcie_tx_ops1 = {
    .channel = BT_PCIE_TX_CHANNEL1,
    .hif_type = HW_TYPE_PCIE,
    .inout = BT_TX_INOUT,
    .pool_size = BT_PCIE_TX_POOL_SIZE1,
    .pop_link = mtty_pcie_tx_cb,
};

/*********************bluetooth_reset function*********************/

//sipc reset
static int mtty_sipc_bluetooth_reset(struct notifier_block *this, unsigned long ev, void *ptr)
{
#define RESET_BUFSIZE 5

    int ret = 0;
    unsigned char reset_buf[RESET_BUFSIZE]= {0x04, 0xff, 0x02, 0x57, 0xa5};
    int i = 0, retry_count = 10;

    dev_unisoc_bt_info(ttyBT_dev,
                                "%s:reset callback coming\n", __func__);
    if (mtty_dev != NULL) {
        dev_unisoc_bt_info(ttyBT_dev,
                                    "%s tty_insert_flip_string\n", __func__);
        mutex_lock(&(mtty_dev->stat_lock));
        if ((atomic_read(&mtty_dev->state) == MTTY_STATE_OPEN) && (RESET_BUFSIZE > 0)) {  //to adapt data struct
            for (i = 0; i < RESET_BUFSIZE; i++) {
                ret = tty_insert_flip_char(mtty_dev->port,
                        reset_buf[i],
                        TTY_NORMAL);
                while((ret != 1) && retry_count--) {
                    msleep(2);
                    dev_unisoc_bt_info(ttyBT_dev,
                                "mtty_dev insert data fail ret =%d, retry_count = %d\n",
                                ret, 10 - retry_count);
                    ret = tty_insert_flip_char(mtty_dev->port,
                                reset_buf[i],
                                TTY_NORMAL);
                }
                if(retry_count != 10)
                    retry_count = 10;
            }
            tty_flip_buffer_push(mtty_dev->port);
        }
        mutex_unlock(&(mtty_dev->stat_lock));
    }
    return NOTIFY_DONE;
}



//sdio and pcie reset
static int mtty_sdio_bluetooth_reset(struct notifier_block *this, unsigned long ev, void *ptr)
{
#define RESET_BUFSIZE 5

    int ret = 0;
    int block_size = RESET_BUFSIZE;
    unsigned char reset_buf[RESET_BUFSIZE]= {0x04, 0xff, 0x02, 0x57, 0xa5};

    dev_unisoc_bt_info(ttyBT_dev,"%s: reset callback coming\n", __func__);
    if (mtty_dev != NULL) {
        if (!work_pending(&mtty_dev->bt_rx_work)) {

            dev_unisoc_bt_info(ttyBT_dev,"%s tty_insert_flip_string", __func__);

            while(ret < block_size){
                dev_unisoc_bt_info(ttyBT_dev,"%s before tty_insert_flip_string ret: %d, len: %d\n",
                        __func__, ret, RESET_BUFSIZE);
                if (PCIE) {
                    ret = tty_insert_flip_string(mtty_dev->port0,
                                    (unsigned char *)reset_buf,
                                    RESET_BUFSIZE);   // -BT_SDIO_HEAD_LEN
                } else {
                ret = tty_insert_flip_string(mtty_dev->port,
                                    (unsigned char *)reset_buf,
                                    RESET_BUFSIZE);   // -BT_SDIO_HEAD_LEN
                }
                dev_unisoc_bt_info(ttyBT_dev,"%s ret: %d, len: %d\n", __func__, ret, RESET_BUFSIZE);
                if (ret) {
                    if (PCIE) {
                        tty_flip_buffer_push(mtty_dev->port0);
                    } else {
                        tty_flip_buffer_push(mtty_dev->port);
                    }
                }
                block_size = block_size - ret;
                ret = 0;
            }
        }
    }
    return NOTIFY_DONE;
}

static int n79_flag_notify(struct notifier_block *this, unsigned long ev, void *ptr)
{
#define N79_BUFSIZE 5

    int ret = 0;
    int block_size = N79_BUFSIZE;
    unsigned char reset_buf[N79_BUFSIZE]= {0x04, 0xff, 0x02, 0x79, 0x01};

    dev_unisoc_bt_info(ttyBT_dev,"%s: n79 flag callback coming\n", __func__);
    if (mtty_dev != NULL) {
        if (!work_pending(&mtty_dev->bt_rx_work)) {

            dev_unisoc_bt_info(ttyBT_dev,"%s tty_insert_flip_string", __func__);

            while(ret < block_size){
                dev_unisoc_bt_info(ttyBT_dev,"%s before tty_insert_flip_string ret: %d, len: %d\n",
                        __func__, ret, N79_BUFSIZE);
                ret = tty_insert_flip_string(mtty_dev->port,
                                    (unsigned char *)reset_buf,
                                    N79_BUFSIZE);   // -BT_SDIO_HEAD_LEN
                dev_unisoc_bt_info(ttyBT_dev,"%s ret: %d, len: %d\n", __func__, ret, N79_BUFSIZE);
                if (ret)
                    tty_flip_buffer_push(mtty_dev->port);
                block_size = block_size - ret;
                ret = 0;
            }
        }
    }
    return NOTIFY_DONE;
}

static struct notifier_block n79_flag_block = {
    .notifier_call = n79_flag_notify,
};

static struct notifier_block bluetooth_sipc_reset_block = {
    .notifier_call = mtty_sipc_bluetooth_reset,
};


static struct notifier_block bluetooth_sdio_reset_block = {
    .notifier_call = mtty_sdio_bluetooth_reset,
};
/******************probe function*****************/

//****************sipc probe
static int  mtty_sipc_probe(struct platform_device *pdev)
{
    
    struct mtty_init_data *pdata = (struct mtty_init_data *)pdev->
                    dev.platform_data;
    struct mtty_device *mtty;
    int rval = 0;
    SIPC = 1;
    if (pdev->dev.of_node && !pdata) {
        rval = mtty_sipc_parse_dt(&pdata, &pdev->dev);
        if (rval) {
            dev_unisoc_bt_err(ttyBT_dev,
                                "failed to parse styy device tree, ret=%d\n",
                                rval);
            return rval;
        }
    }
    dev_unisoc_bt_info(ttyBT_dev,
                        "mtty: after parse device tree, name=%s, dst=%u, channel=%u, tx_bufid=%u, rx_bufid=%u\n",
                        pdata->name, pdata->dst, pdata->channel, pdata->tx_bufid, pdata->rx_bufid);

    mtty = devm_kzalloc(&pdev->dev, sizeof(struct mtty_device), GFP_KERNEL);
    ttyBT_dev = &pdev->dev;
    if (mtty == NULL) {
        mtty_destroy_pdata(&pdata); 
        dev_unisoc_bt_err(ttyBT_dev,
                            "mtty Failed to allocate device!\n");
        return -ENOMEM;
    }

    mtty->pdata = pdata;
    rval = mtty_sipc_driver_init(mtty);
    if (rval) {
        devm_kfree(&pdev->dev, mtty);
        mtty_destroy_pdata(&pdata);
        dev_unisoc_bt_err(ttyBT_dev,
                            "mtty driver init error!\n");
        return -EINVAL;
    }

    rval = sbuf_register_notifier(pdata->dst, pdata->channel,
                    pdata->rx_bufid, mtty_handler, mtty);
    if (rval) {
        mtty_tty_driver_exit(mtty);
        kfree(mtty->port);
        devm_kfree(&pdev->dev, mtty);
        dev_unisoc_bt_err(ttyBT_dev,
                            "regitster notifier failed (%d)\n",
                            rval);
        return rval;
    }

    dev_unisoc_bt_info(ttyBT_dev,
                        "mtty_probe init device addr: 0x%p\n",
                        mtty);
    platform_set_drvdata(pdev, mtty);

    if (sysfs_create_group(&pdev->dev.kobj,
            &bluetooth_group)) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "%s failed to create bluetooth tty attributes.\n",
                            __func__);
    }

    rfkill_bluetooth_init(pdev);
    mtty_dev = mtty;
    atomic_notifier_chain_register(&wcn_reset_notifier_list, &bluetooth_sipc_reset_block);

    pr_info("%s  successful start!", __func__);
    return 0;
}

//sdio probe
static int  mtty_sdio_probe(struct platform_device *pdev) {
  
    struct mtty_init_data *pdata = (struct mtty_init_data *)
                                pdev->dev.platform_data;
    struct mtty_device *mtty;
    int rval = 0;
    SDIO = 1;
    if (pdev->dev.of_node && !pdata) {
        rval = mtty_sdio_parse_dt(&pdata, &pdev->dev);
        if (rval) {
            dev_unisoc_bt_err(ttyBT_dev,
                              "failed to parse mtty device tree, ret=%d\n",
                              rval);
            return rval;
        }
    }

    mtty = kzalloc(sizeof(struct mtty_device), GFP_KERNEL);
    ttyBT_dev = &pdev->dev;
    if (mtty == NULL) {
        mtty_destroy_pdata(&pdata);
        dev_unisoc_bt_err(ttyBT_dev,
                          "mtty Failed to allocate device!\n");
        return -ENOMEM;
    }

    mtty->pdata = pdata;
    rval = mtty_sdio_driver_init(mtty);
    if (rval) {
        kfree(mtty->port);
        kfree(mtty);
        mtty_destroy_pdata(&pdata);
        dev_unisoc_bt_err(ttyBT_dev,
                          "regitster notifier failed (%d)\n",
                          rval);
        return rval;
    }

    dev_unisoc_bt_info(ttyBT_dev,
                       "mtty_probe init device addr: 0x%p\n",
                       mtty);
    platform_set_drvdata(pdev, mtty);

    /*spin_lock_init(&mtty->rw_lock);*/
    atomic_set(&mtty->state, MTTY_STATE_CLOSE);
    mutex_init(&mtty->rw_mutex);
    INIT_LIST_HEAD(&mtty->rx_head);
    /*tasklet_init(&mtty->rx_task, mtty_rx_task, (unsigned long)mtty);*/
    mtty->bt_rx_workqueue =
        create_singlethread_workqueue("SPRDBT_RX_QUEUE");
    if (!mtty->bt_rx_workqueue) {
        mtty_tty_driver_exit(mtty);
        kfree(mtty->port);
        kfree(mtty);
        mtty_destroy_pdata(&pdata);
        dev_unisoc_bt_err(ttyBT_dev,
                          "%s SPRDBT_RX_QUEUE create failed",
                          __func__);
        return -ENOMEM;
    }
    INIT_WORK(&mtty->bt_rx_work, mtty_rx_work_queue);

    mtty_dev = mtty;

    if (sysfs_create_group(&pdev->dev.kobj,
            &bluetooth_group)) {
        dev_unisoc_bt_err(ttyBT_dev,
                          "%s failed to create bluetooth tty attributes.\n",
                          __func__);
    }

    rfkill_bluetooth_init(pdev);
    bluesleep_init();
    atomic_notifier_chain_register(&wcn_reset_notifier_list,&bluetooth_sdio_reset_block);
    atomic_notifier_chain_register(&modem_n79_notifier_list,&n79_flag_block);
    sprdwcn_bus_chn_init(&bt_sdio_rx_ops);
    sprdwcn_bus_chn_init(&bt_sdio_tx_ops);
    sema_init(&sem_id, BT_TX_POOL_SIZE - 1);
    pr_info("%s  successful start!", __func__);

    return 0;
}

//sipc2 probe
static int  mtty_sipc2_probe(struct platform_device *pdev)
{
    struct mtty_init_data *pdata = (struct mtty_init_data *)pdev->
                    dev.platform_data;
    struct mtty_device *mtty;
    int rval = 0;
    SIPC2 = 1;
    if (pdev->dev.of_node && !pdata) {
        rval = mtty_sipc2_parse_dt(&pdata, &pdev->dev);
        if (rval) {
            dev_unisoc_bt_err(ttyBT_dev,
                                "failed to parse styy device tree, ret=%d\n",
                                rval);
            return rval;
        }
    }
    dev_unisoc_bt_info(ttyBT_dev,
                        "mtty: after parse device tree, name=%s, dst=%u, channel=%u, tx_bufid=%u, rx_bufid=%u\n",
                        pdata->name, pdata->dst, pdata->channel, pdata->tx_bufid, pdata->rx_bufid);

    mtty = kzalloc(sizeof(struct mtty_device), GFP_KERNEL);
    ttyBT_dev = &pdev->dev;
    if (mtty == NULL) {
        mtty_destroy_pdata(&pdata);
        pr_err("mtty Failed to allocate device!\n");
        return -ENOMEM;
    }
    memset(mtty, 0 ,sizeof(struct mtty_device));

    mtty->pdata = pdata;
    rval = mtty_sipc2_driver_init(mtty);
    if (rval) {
        devm_kfree(&pdev->dev, mtty);
        mtty_destroy_pdata(&pdata);
        dev_unisoc_bt_err(ttyBT_dev,
                            "mtty driver init error!\n");
        return -EINVAL;
    }

    dev_unisoc_bt_info(ttyBT_dev,
                        "mtty_probe init device addr: 0x%p\n",
                        mtty);
    platform_set_drvdata(pdev, mtty);

    mtty_dev = mtty;

    atomic_set(&mtty->state, MTTY_STATE_CLOSE);
    sema_init(&sem_id, BT_TX_POOL_SIZE - 1);
    mutex_init(&mtty->rw_mutex);
    INIT_LIST_HEAD(&mtty->rx_head);

    mtty->bt_rx_workqueue = create_singlethread_workqueue("SPRDBT_RX_QUEUE");
    if (!mtty->bt_rx_workqueue) {
        mtty_tty_driver_exit(mtty);
        kfree(mtty->port);
        kfree(mtty);
        mtty_destroy_pdata(&pdata);
        dev_unisoc_bt_err(ttyBT_dev,
                            "%s SPRDBT_RX_QUEUE create failed",
                            __func__);
        return -ENOMEM;
    }
    INIT_WORK(&mtty->bt_rx_work, mtty_rx_work_queue);

    if (sysfs_create_group(&pdev->dev.kobj,
            &bluetooth_group)) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "%s failed to create bluetooth tty attributes.\n",
                            __func__);
    }

    rfkill_bluetooth_init(pdev);
    bluesleep_init();
    atomic_notifier_chain_register(&wcn_reset_notifier_list, &bluetooth_sipc_reset_block);
    sprdwcn_bus_chn_init(&bt_sipc2_rx_ops);
    sprdwcn_bus_chn_init(&bt_sipc2_tx_ops);

    pr_info("%s  successful start!", __func__);

    return 0;
}

//pcie probe
static int mtty_pcie_probe(struct platform_device *pdev)
{
    struct mtty_init_data *pdata = (struct mtty_init_data *)
                                pdev->dev.platform_data;
    struct mtty_device *mtty;
    int rval = 0;
    PCIE = 1;
    pr_err("mtty start insert mod pdev %p\n", pdev);
    if (pdev->dev.of_node && !pdata) {
        rval = mtty_sdio_parse_dt(&pdata, &pdev->dev);
        if (rval) {
            pr_err("failed to parse mtty device tree, ret=%d\n",
                    rval);
            return rval;
        }
    }

    mtty = kzalloc(sizeof(struct mtty_device), GFP_KERNEL);
    if (mtty == NULL) {
        mtty_destroy_pdata(&pdata);
        pr_err("mtty Failed to allocate device!\n");
        return -ENOMEM;
    }
    memset(mtty, 0 ,sizeof(struct mtty_device));
    mtty->pdata = pdata;
    rval = mtty_pcie_driver_init(mtty);
    if (rval) {
        mtty_tty_driver_exit(mtty);
        kfree(mtty->port0);
        kfree(mtty->port1);
        kfree(mtty);
        mtty_destroy_pdata(&pdata);
        pr_err("regitster notifier failed (%d)\n", rval);
        return rval;
    }

    pr_info("mtty_probe init device addr: 0x%p\n", mtty);
    platform_set_drvdata(pdev, mtty);
    mtty->pdev = pdev;
    mutex_init(&mtty->stat_mutex);
    sema_init(&sem_id, BT_PCIE_TX_POOL_SIZE0 - 1);
    mutex_init(&mtty->rw_mutex);
    INIT_LIST_HEAD(&mtty->rx_head);
    mtty->bt_rx_workqueue =
        create_singlethread_workqueue("SPRDBT_RX_QUEUE");
    if (!mtty->bt_rx_workqueue) {
        pr_err("%s SPRDBT_RX_QUEUE create failed", __func__);
        return -ENOMEM;
    }
    INIT_WORK(&mtty->bt_rx_work, mtty_pcie_rx_work_queue);

    mtty_dev = mtty;

    rfkill_bluetooth_init(pdev);
    bluesleep_init();
    atomic_notifier_chain_register(&wcn_reset_notifier_list,&bluetooth_sdio_reset_block);

    return 0;
}



/***************remove function*************/

//sipc remove
static int  mtty_sipc_remove(struct platform_device *pdev)
{
    struct mtty_device *mtty = platform_get_drvdata(pdev);
    int rval;

    rval = sbuf_register_notifier(mtty->pdata->dst, mtty->pdata->channel,
                    mtty->pdata->rx_bufid, NULL, NULL);
    if (rval) {
        dev_unisoc_bt_err(ttyBT_dev,
                            "unregitster notifier failed (%d)\n",
                            rval);
        return rval;
    }

    mtty_tty_driver_exit(mtty);
    kfree(mtty->port);
    mtty_destroy_pdata(&mtty->pdata);
    devm_kfree(&pdev->dev, mtty);
    platform_set_drvdata(pdev, NULL);
    sysfs_remove_group(&pdev->dev.kobj, &bluetooth_group);
    return 0;
}

//sipc2 remove
static int mtty_sipc2_remove(struct platform_device *pdev)
{
    struct mtty_device *mtty = platform_get_drvdata(pdev);

    mtty_tty_driver_exit(mtty);
    sprdwcn_bus_chn_deinit(&bt_sipc2_rx_ops);
    sprdwcn_bus_chn_deinit(&bt_sipc2_tx_ops);
    kfree(mtty->port);
    mtty_destroy_pdata(&mtty->pdata);
    flush_workqueue(mtty_dev->bt_rx_workqueue);
    destroy_workqueue(mtty_dev->bt_rx_workqueue);
    devm_kfree(&pdev->dev, mtty);
    platform_set_drvdata(pdev, NULL);
    sysfs_remove_group(&pdev->dev.kobj, &bluetooth_group);
    return 0;
}


//sdio remove
static int  mtty_sdio_remove(struct platform_device *pdev)
{
    struct mtty_device *mtty = platform_get_drvdata(pdev);

    mtty_tty_driver_exit(mtty);
    sprdwcn_bus_chn_deinit(&bt_sdio_rx_ops);
    sprdwcn_bus_chn_deinit(&bt_sdio_tx_ops);
    kfree(mtty->port);
    mtty_destroy_pdata(&mtty->pdata);
    flush_workqueue(mtty->bt_rx_workqueue);
    destroy_workqueue(mtty->bt_rx_workqueue);
    /*tasklet_kill(&mtty->rx_task);*/
    kfree(mtty);
    platform_set_drvdata(pdev, NULL);
    sysfs_remove_group(&pdev->dev.kobj, &bluetooth_group);
    bluesleep_exit();

    return 0;
}

//pcie_remove

static int  mtty_pcie_remove(struct platform_device *pdev)
{
    struct mtty_device *mtty = platform_get_drvdata(pdev);
    pr_err("mtty remove mod start\n");
    mtty_tty_driver_exit(mtty);
    kfree(mtty->port0);
    kfree(mtty->port1);
    mtty_destroy_pdata(&mtty->pdata);
    flush_workqueue(mtty->bt_rx_workqueue);
    destroy_workqueue(mtty->bt_rx_workqueue);
    kfree(mtty);
    platform_set_drvdata(pdev, NULL);
    bluesleep_exit();

    return 0;
}

const struct mtty_match_data g_sc2332_sipc_data = {
    .hw_type = SPRD_HW_SC2332_SIPC,
};

struct mtty_match_data g_sc2355_pcie_data = {
    .hw_type = SPRD_HW_SC2355_PCIE,
};

struct mtty_match_data g_sc2355_sipc2_data = {
    .hw_type = SPRD_HW_SC2355_SIPC2,
};

struct mtty_match_data g_sc2355_sdio_data = {
    .hw_type = SPRD_HW_SC2355_SDIO,
};

struct of_device_id mtty_global_match_table[] = {
    { .compatible = "sprd,wcn_bt", .data = &g_sc2332_sipc_data},
    { .compatible = "sprd,mtty-pcie", .data = &g_sc2355_pcie_data},
    { .compatible = "sprd,wcn_internal_chip", .data = &g_sc2355_sipc2_data},
    { .compatible = "sprd,mtty", .data = &g_sc2355_sdio_data},
    { },
};
MODULE_DEVICE_TABLE(of, mtty_global_match_table);

static int sprd_mtty_probe(struct platform_device *pdev)
{
    struct mtty_match_data *p_match_data;
    struct device_node *np = pdev->dev.of_node;
    const struct of_device_id *of_id =
        of_match_node(mtty_global_match_table, np);

    if (!of_id) {
        pr_info("%s not find matched id!", __func__);
        return -EINVAL;
    }

    p_match_data = (struct mtty_match_data *)of_id->data;
    if (!p_match_data) {
        pr_info("%s not find matched data!", __func__);
        return -EINVAL;
    }

    pr_info("%s %s %d.\n", __func__, of_id->compatible, p_match_data->hw_type);

    if (p_match_data->hw_type == SPRD_HW_SC2332_SIPC) {
        pr_info("%s sipc transport data!", __func__);
        return mtty_sipc_probe(pdev);
    } else if (p_match_data->hw_type == SPRD_HW_SC2355_SDIO) {
        pr_info("%s sdio transport data!", __func__);
        return mtty_sdio_probe(pdev);
    } else if (p_match_data->hw_type == SPRD_HW_SC2355_SIPC2) {
        pr_info("%s sipc2 transport data!", __func__);
        return mtty_sipc2_probe(pdev);
    } else if (p_match_data->hw_type == SPRD_HW_SC2355_PCIE) {
        pr_info("%s pcie transport data!", __func__);
        return mtty_pcie_probe(pdev);
    } else {
        pr_err("%s error hw_type %d.\n", __func__, p_match_data->hw_type);
        dump_stack();
        return -EINVAL;
    }
}


static void sprd_mtty_remove(struct platform_device *pdev)
{
    struct mtty_match_data *p_match_data;
    struct device_node *np = pdev->dev.of_node;
    const struct of_device_id *of_id =
        of_match_node(mtty_global_match_table, np);

    if (!of_id) {
        pr_info("%s not find matched id!", __func__);
        return;
    }

    p_match_data = (struct mtty_match_data *)of_id->data;
    if (!p_match_data) {
        pr_info("%s not find matched data!", __func__);
        return;
    }

    pr_info("%s %s %d.\n", __func__, of_id->compatible, p_match_data->hw_type);

    if (p_match_data->hw_type == SPRD_HW_SC2332_SIPC) {
        mtty_sipc_remove(pdev);
        return;
    }
    if (p_match_data->hw_type == SPRD_HW_SC2355_SIPC2) {
        mtty_sipc2_remove(pdev);
        return;
    }
    if (p_match_data->hw_type == SPRD_HW_SC2355_SDIO) {
        mtty_sdio_remove(pdev);
        return;
    }
    if (p_match_data->hw_type == SPRD_HW_SC2355_PCIE) {
        mtty_pcie_remove(pdev);
        return;
    }
}

static struct platform_driver sprd_mtty_driver = {
    .probe = sprd_mtty_probe,
    .remove = sprd_mtty_remove,
    .driver = {
            .owner = THIS_MODULE,
            .name = "mtty",
            
            .of_match_table = mtty_global_match_table,
    }
};





module_platform_driver(sprd_mtty_driver);

MODULE_AUTHOR("Unisoc wcn bt");
MODULE_DESCRIPTION("Unisoc marlin tty driver");
