/*
* SPDX-FileCopyrightText: 2021-2023 Unisoc (Shanghai) Technologies Co. Ltd
* SPDX-License-Identifier: GPL-2.0-only
*/

#include <linux/of.h>
#include <linux/ctype.h>
#include <linux/moduleparam.h>
#include "wcn_bus.h"
#include <linux/miscdevice.h>
#include <net/ip.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>

#include "cfg80211.h"
#include "chip_ops.h"
#include "cmd.h"
#include "common.h"
#include "delay_work.h"
#include "hif.h"
#include "iface.h"
#include "msg.h"
#include "npi.h"
#include "qos.h"
#include "report.h"
#include "tcp_ack.h"
#ifdef ENABLE_PAM_WIFI
#include "pamwifi/pamwifi.h"
#endif
#include "wapi.h"

static struct sprd_priv *sprd_prv;

const char *dhcp_str_info[] = {
	"INVALID DHCP",
	"DHCP DISCOVER",
	"DHCP OFFER",
	"DHCP REQUEST",
	"DHCP DECLINE",
	"DHCP ACK",
	"DHCP NACK"
};

int sprd_wlan_parse_dt(struct sprd_priv *priv)
{
	struct platform_device *pdev = priv->hif.pdev;
	struct sprd_wlan_dt_config *dt_configs = &priv->dt_configs;
	int ret = 0;

	if (!pdev) {
		pr_err("%s pdev NULL.\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	dt_configs->enable_n79 = of_property_read_bool(pdev->dev.of_node,
						       "sprd,enable-n79");
	dt_configs->enable_chr = of_property_read_bool(pdev->dev.of_node,
						       "sprd,enable-chr");

	wl_info("%s n79_en:%d chr_en:%d\n", __func__,
		dt_configs->enable_n79, dt_configs->enable_chr);

exit:
	return ret;
}

void sprd_put_vif(struct sprd_vif *vif)
{
	if (vif) {
		spin_lock_bh(&vif->priv->list_lock);
		vif->ref--;
		spin_unlock_bh(&vif->priv->list_lock);
	}
}

struct sprd_vif *sprd_mode_to_vif(struct sprd_priv *priv, u8 vif_mode)
{
	struct sprd_vif *vif, *found = NULL;

	spin_lock_bh(&priv->list_lock);
	list_for_each_entry(vif, &priv->vif_list, vif_node) {
		if (vif->mode == vif_mode) {
			vif->ref++;
			found = vif;
			break;
		}
	}
	spin_unlock_bh(&priv->list_lock);

	return found;
}

static void iface_set_priv(struct sprd_priv *priv)
{
	sprd_prv = priv;
}

static struct sprd_priv *iface_get_priv(void)
{
	return sprd_prv;
}

static int wlan_open(struct inode *inode, struct file *filep)
{
	return 0;
}

static ssize_t wlan_read_data(struct file *filp, char __user *buf,
			     size_t count, loff_t *pos)
{
	return 0;
}

static int wlan_release(struct inode *inode, struct file *filep)
{
	return 0;
}

static const struct file_operations wlan_misc_fops = {
	.owner = THIS_MODULE,
	.open = wlan_open,
	.read = wlan_read_data,
	.release = wlan_release,
};

static struct miscdevice wlan_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "wlan",
	.fops = &wlan_misc_fops,
};

#ifndef DRV_RESET_SELF
static int iface_host_reset(struct notifier_block *nb,
		       unsigned long data, void *ptr)
{
	struct sprd_priv *priv = iface_get_priv();
	struct sprd_hif *hif;
	struct sprd_cmd *cmd = NULL;

	if (!priv) {
		wl_err("%s sprd_prv is NULL\n", __func__);
		return NOTIFY_OK;
	}

	cmd = &priv->cmd;
	hif = &priv->hif;
	hif->cp_asserted = 1;
	complete(&cmd->completed);
	sprd_chip_force_exit((void *)&priv->chip);

	wl_info("%s start process\n", __func__);
	schedule_delayed_work(&priv->reset_delay_work, msecs_to_jiffies(0));

	return NOTIFY_OK;
}
#else
static int iface_host_reset(struct notifier_block *nb,
		       unsigned long data, void *ptr)
{
	struct sprd_priv *priv = iface_get_priv();
	struct sprd_hif *hif;
	struct sprd_cmd *cmd = &priv->cmd;

	if (!priv) {
		wl_err("%s sprd_prv is NULL\n", __func__);
		return NOTIFY_OK;
	}

	hif = &priv->hif;
	hif->cp_asserted = 1;
	complete(&cmd->completed);
	sprd_chip_force_exit((void *)&priv->chip);

	wl_debug("%s process wifi driver self reset work\n", __func__);
	if (!work_pending(&priv->reset_work))
		queue_work(priv->reset_workq, &priv->reset_work);

	return NOTIFY_OK;
}
#endif

static struct notifier_block iface_host_reset_cb = {
	.notifier_call = iface_host_reset,
};

static void iface_stop_net(struct sprd_vif *vif)
{
	struct sprd_vif *real_vif, *tmp_vif;
	struct sprd_priv *priv = vif->priv;

	spin_lock_bh(&priv->list_lock);
	list_for_each_entry_safe(real_vif, tmp_vif, &priv->vif_list, vif_node)
		if (real_vif->ndev)
			netif_stop_queue(real_vif->ndev);
	spin_unlock_bh(&priv->list_lock);
}

static void iface_set_mac_addr(struct sprd_vif *vif, u8 *pending_addr,
			       u8 *addr)
{
	enum nl80211_iftype type = vif->wdev.iftype;
	struct sprd_priv *priv = vif->priv;

	if (!addr)
		return;

	if (!priv) {
		netdev_err(vif->ndev, "%s get pirv failed\n", __func__);
		return;
	}

	if (is_valid_ether_addr(priv->default_mac)) {
		ether_addr_copy(addr, priv->default_mac);
	} else {
		eth_random_addr(addr);
		netdev_warn(vif->ndev, "%s Warning: use random MAC address\n",
				__func__);
		/* initialize MAC addr with specific OUI */
		addr[0] = 0x40;
		addr[1] = 0x45;
		addr[2] = 0xda;
	}

	switch (type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_AP:
		if (strncmp(vif->name, "wlan0", 5))
			addr[5] ^= 0x70;
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
		fallthrough;
	case NL80211_IFTYPE_P2P_GO:
		addr[4] ^= 0x80;
		fallthrough;
	case NL80211_IFTYPE_P2P_DEVICE:
		addr[0] ^= 0x02;
		break;
	default:
		break;
	}
}

int sprd_iface_set_power(struct sprd_hif *hif, int val)
{
	int ret = 0;
	struct sprd_wlan_dt_config *dt_configs = &hif->priv->dt_configs;

	if (val) {
		wl_info("%s Power on WCN (%d time)\n", __func__,
			atomic_read(&hif->power_cnt));
		sprd_wlan_power_status_sync(1, 1);
		ret = sprd_hif_power_on(hif);
		if (ret) {
			sprd_wlan_power_status_sync(1, 0);
			if (ret == -ENODEV)
				wl_err("failed to power on WCN!\n");
			else if (ret == -EIO)
				wl_err("SYNC cmd error!\n");
			if (dt_configs->enable_chr)
				sprd_chr_report_open_error(EVT_CHR_OPEN_ERR, OPEN_ERR_POWER_ON);
			return ret;
		}
		if (atomic_read(&hif->power_cnt) == 1)
			sprd_get_fw_info(hif->priv);
	} else {
		wl_info("%s Power off WCN (%d time)\n", __func__,
			atomic_read(&hif->power_cnt));
		sprd_hif_power_off(hif);
	}
	return ret;
}

int sprd_iface_report_assert_evt(struct sprd_priv *priv)
{
	struct wiphy *wiphy = priv->wiphy;
	struct sprd_vif *vif = NULL, *tmp_vif;
	struct sk_buff *reply;
	int ret = 0;
	char *data = "FW_ERROR";

	spin_lock_bh(&priv->list_lock);
	list_for_each_entry(tmp_vif, &priv->vif_list, vif_node) {
		if (tmp_vif->ndev) {
			vif = tmp_vif;
			break;
		}
	}
	spin_unlock_bh(&priv->list_lock);


	if (!vif) {
		wl_err("%s can not get vif!\n", __func__);
		return -1;
	}

	reply = cfg80211_vendor_event_alloc(wiphy, &vif->wdev, strlen(data) + 1,
					    SPRD_VENDOR_EVENT_ASSERT_INDEX,
					    GFP_KERNEL);
	if (!reply) {
		wl_err("%s alloc event error\n", __func__);
		return -ENOMEM;
	}

	if (nla_put(reply, SPRD_ATTR_ASSERT, strlen(data) + 1, data)) {
		netdev_info(vif->ndev, "nla put failed");
		kfree_skb(reply);
		return -1;
	}

	cfg80211_vendor_event(reply, GFP_KERNEL);
	wl_info("%s success\n", __func__);

	return ret;
}

static int iface_open(struct net_device *ndev)
{
	struct sprd_vif *vif = netdev_priv(ndev);
	struct sprd_hif *hif = &vif->priv->hif;
	int ret;
	int count = 0;

	netdev_info(ndev, "%s connected(%u)\n", __func__, vif->wdev.connected);

	/*here we need to wait for 3s*/
	while ((!vif->priv->probe_done) && (count < 1000)) {
		printk_ratelimited("%s error! driver probe not done, wait\n",
				   __func__);
		usleep_range(2500, 3000);
		count++;
	}

	/* iface_open call start_marlin, the first CMD will be send.
	 * block_cmd_after_close = 1 indicate that the CMD cannot
	 * be sent between the last CMD_CLOSE and start_marlin.
	 */
	if (atomic_read(&hif->block_cmd_after_close) == 1)
		atomic_set(&hif->block_cmd_after_close, 0);

	ret = sprd_iface_set_power(hif, true);
	if (ret)
		return ret;

	ret = sprd_init_fw(vif);
	if (!ret && vif->wdev.iftype == NL80211_IFTYPE_AP) {
		netif_carrier_off(ndev);
		return 0;
	}
	netif_start_queue(ndev);

	return 0;
}

static int iface_close(struct net_device *ndev)
{
	struct sprd_vif *vif = netdev_priv(ndev);
	struct sprd_hif *hif = &vif->priv->hif;

	netdev_info(ndev, "%s connected(%u)\n", __func__, vif->wdev.connected);

	sprd_report_scan_done(vif, true);
	sprd_report_sched_scan_done(vif, true);
	netif_stop_queue(ndev);
	if (netif_carrier_ok(ndev))
		netif_carrier_off(ndev);

	/*
	 * CR 2343348, 2352896
	 * wdev->connected = true when iface_close comes,
	 * call cfg80211_disconnected to clear wdev->connected
	 */
	if (vif->wdev.iftype == NL80211_IFTYPE_STATION ||
		vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT) {
		if (vif->sm_state == SPRD_DISCONNECTING ||
			vif->sm_state == SPRD_CONNECTING ||
			vif->sm_state == SPRD_CONNECTED) {
			wl_info("%s mode %d sm_state %d for sta or p2p gc.\n",
			        __func__, vif->mode, vif->sm_state);
			cfg80211_disconnected(vif->ndev, 0, NULL, 0,
						  false, GFP_KERNEL);
			vif->sm_state = SPRD_DISCONNECTED;
		}
	}

	/* hif->power_cnt = 1 means there is only one mode and
	 * stop_marlin will be called after closed.but it should
	 * not send any command between close and start_marlin,
	 * block_cmd_after_close need set to 1 to block other cmd.
	 */
	if (atomic_read(&hif->power_cnt) == 1)
		atomic_set(&hif->block_cmd_after_close, 1);

	sprd_uninit_fw(vif);
	sprd_iface_set_power(hif, false);

	return 0;
}

static void iface_netflowcontrl_mode(struct sprd_priv *priv,
				     enum sprd_mode mode, bool state)
{
	struct sprd_vif *vif;

	vif = sprd_mode_to_vif(priv, mode);
	if (vif && vif->ndev) {
		if (state)
			netif_wake_queue(vif->ndev);
		else
			netif_stop_queue(vif->ndev);
		sprd_put_vif(vif);
	}
}

static void iface_netflowcontrl_all(struct sprd_priv *priv, bool state)
{
	struct sprd_vif *real_vif, *tmp_vif;

	spin_lock_bh(&priv->list_lock);
	list_for_each_entry_safe(real_vif, tmp_vif, &priv->vif_list, vif_node)
		if (real_vif->ndev) {
			if (state)
				netif_wake_queue(real_vif->ndev);
			else
				netif_stop_queue(real_vif->ndev);
		}
	spin_unlock_bh(&priv->list_lock);
}

/* @state: true for netif_start_queue, false for netif_stop_queue */
void sprd_net_flowcontrl(struct sprd_priv *priv, enum sprd_mode mode,
			 bool state)
{
	if (mode != SPRD_MODE_NONE)
		iface_netflowcontrl_mode(priv, mode, state);
	else
		iface_netflowcontrl_all(priv, state);
}

struct udphdr *sprd_get_udphdr(struct sk_buff *skb, unsigned char *iphdrlen)
{
	struct udphdr *udphdr = NULL;
	struct iphdr *iphdr;
	struct ipv6hdr *ipv6hdr;
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;

	if (ethhdr->h_proto == htons(ETH_P_IPV6)) {
		ipv6hdr = (struct ipv6hdr *)(skb->data + ETHER_HDR_LEN);
		/* check for udp header */
		if (ipv6hdr->nexthdr != IPPROTO_UDP)
			return udphdr;
		*iphdrlen = sizeof(*ipv6hdr);
	} else if (ethhdr->h_proto == htons(ETH_P_IP)) {
		iphdr = (struct iphdr *)(skb->data + ETHER_HDR_LEN);
		if (iphdr->protocol != IPPROTO_UDP)
			return udphdr;
		*iphdrlen = iphdr->ihl * 4;
	} else {
		return udphdr;
	}

	udphdr = (struct udphdr *)(skb->data + ETHER_HDR_LEN + *iphdrlen);
	return udphdr;
}

void sprd_filter_ip_pkt_debug(struct sk_buff *skb,
			      struct net_device *ndev, const char *direct)
{
	unsigned char *dhcpdata = NULL;
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	struct udphdr *udphdr;
	unsigned char iphdrlen = 0;

	udphdr = sprd_get_udphdr(skb, &iphdrlen);
	if (!udphdr)
		return;
	if (IPV4_DHCP(ethhdr, udphdr)) {
		dhcpdata = skb->data + ETHER_HDR_LEN + iphdrlen + 250;
		if (*dhcpdata < ARRAY_SIZE(dhcp_str_info))
			wl_info("[%s] [%s]\n", direct, dhcp_str_info[*dhcpdata]);
	} else if (IPV6_DHCP(ethhdr, udphdr)) {
		wl_info("[%s] special data: DHCP\n", direct);
	} else if (IP_DNS(ethhdr, udphdr)) {
		wl_info("[%s] special data: DNS\n", direct);
	}
}

/*Print special data info in the TX and RX directions for debugging*/
void sprd_filter_data_debug(struct sk_buff *skb, struct net_device *ndev, const char *direct)
{
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	if (ethhdr->h_proto == htons(ETH_P_ARP))
		wl_info("[%s] special data: ARP\n", direct);
	else if (ethhdr->h_proto == htons(ETH_P_TDLS))
		wl_info("[%s] special data: TDLS\n", direct);
	else if (ethhdr->h_proto == htons(ETH_P_PREAUTH))
		wl_info("[%s] special data: PREAUTH\n", direct);
	else if (ethhdr->h_proto == htons(ETH_P_IP) ||
			 ethhdr->h_proto == htons(ETH_P_IPV6))
		sprd_filter_ip_pkt_debug(skb, ndev, direct);
	else
		return;
}

void sprd_netif_rx(struct net_device *ndev, struct sk_buff *skb)
{
	struct sprd_vif *vif;
	struct sprd_hif *hif;
	int print_len;

	vif = netdev_priv(ndev);
	hif = &vif->priv->hif;
	print_len = skb->len > 64 ? 64 : skb->len;

	/* report sniffer monitor data packet */
	if (atomic_read(&vif->priv->monitor_mode)) {
		print_hex_dump_debug("RX sniffer data packet: ", DUMP_PREFIX_OFFSET,
				     16, 1, skb->data, print_len, 0);

		wl_debug("sniffer data cnt: %lu\n", vif->priv->monitor_data_cnt++);

		skb->dev = ndev;
		/* report data for sniffer mode */
		skb_set_mac_header(skb, 0);
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->pkt_type = PACKET_OTHERHOST;
		skb->protocol = htons(ETH_P_802_2);

		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += skb->len;

		local_bh_disable();
		netif_receive_skb(skb);
		local_bh_enable();

		return;
	}

	sprd_filter_data_debug(skb, ndev, "RX");
	print_hex_dump_debug("RX packet: ", DUMP_PREFIX_OFFSET,
			     16, 1, skb->data, print_len, 0);
	skb->dev = ndev;
	skb->protocol = eth_type_trans(skb, ndev);
	/* CHECKSUM_UNNECESSARY not supported by our hardware */
	/* skb->ip_summed = CHECKSUM_UNNECESSARY; */

	if (skb->protocol == cpu_to_be16(ETH_P_PAE))
		wl_info("RX special data: 802.1x\n");
	else if (skb->protocol == cpu_to_be16(WAPI_TYPE))
		wl_info("RX special data: WAPI\n");

	ndev->stats.rx_packets++;
	ndev->stats.rx_bytes += skb->len;
#if defined(MORE_DEBUG)
	hif->stats.rx_packets++;
	hif->stats.rx_bytes += skb->len;
	if (skb->pkt_type == PACKET_MULTICAST)
		hif->stats.rx_multicast++;
#endif

	/* to ensure data handled in netif in order */
	local_bh_disable();
	netif_receive_skb(skb);
	local_bh_enable();
}

/* report sniffer monitor mgmt frame */
void sprd_rx_monitor_process(struct sprd_vif *vif,
			     unsigned char *data, unsigned int len)
{
	struct sk_buff *skb;
	struct net_device *ndev;
	int print_len;

	skb = dev_alloc_skb(len + NET_IP_ALIGN);
	if (!skb)
		return;

	print_len = len > 64 ? 64 : len;
	print_hex_dump_debug("RX sniffer frame: ", DUMP_PREFIX_OFFSET,
			     16, 1, data, print_len, 0);

	wl_debug("sniffer mgmt frame cnt: %lu\n", vif->priv->monitor_mgmt_cnt++);

	ndev = vif->ndev;
	skb_reserve(skb, NET_IP_ALIGN);
	memcpy(skb->data, data, len);
	skb_put(skb, len);

	skb->dev = ndev;
	/* report data for monitor mode */
	skb_set_mac_header(skb, 0);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);

	ndev->stats.rx_packets++;
	ndev->stats.rx_bytes += skb->len;

	local_bh_disable();
	netif_receive_skb(skb);
	local_bh_enable();
}

static int iface_prepare_xmit(struct sprd_vif *vif, struct net_device *ndev,
			      struct sk_buff **pskb)
{
	struct sprd_hif *hif = &vif->priv->hif;
	struct sk_buff *skb = *pskb;
	struct sk_buff *tmp_skb = skb;
	int ret = 0;

	/* drop nonlinearize skb */
	if (skb_linearize(skb)) {
		wl_err("nonlinearize skb\n");
		dev_kfree_skb(skb);
		ndev->stats.tx_dropped++;
		return -1;
	}

	if (hif->cp_asserted == 1 || unlikely(hif->exit)) {
		dev_kfree_skb(skb);
		iface_stop_net(vif);
		return -1;
	}

	ret = sprd_chip_tx_prepare(&vif->priv->chip, skb);
	if (ret)
		return ret;

	if (skb_headroom(skb) < ndev->needed_headroom) {
		skb = skb_realloc_headroom(skb, ndev->needed_headroom);
		dev_kfree_skb(tmp_skb);
		if (!skb) {
			netdev_err(ndev,
				   "%s skb_realloc_headroom failed\n",
				   __func__);
			return -1;
		}
#if defined(MORE_DEBUG)
                hif->stats.tx_realloc++;
#endif
		*pskb = skb;
	}
	return ret;

}

static netdev_tx_t iface_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	int ret = 0;
	int offset;
	struct sprd_vif *vif = netdev_priv(ndev);
	struct sprd_hif *hif = &vif->priv->hif;
	struct sprd_msg *msg = NULL;
	unsigned int skb_len;
	int print_len;

	ret = iface_prepare_xmit(vif, ndev, &skb);
	if (-1 == ret)
		goto out;

	/* Hardware tx data queue prority is lower than management queue
	 * management frame will be send out early even that get into queue
	 * after data frame.
	 * Workaround way: Put eap failure frame to high queue
	 * by use tx mgmt cmd
	 */
	/* send 802.1x or WAPI frame from cmd channel */
	ret = sprd_hif_tx_special_data(hif, skb, ndev);
	if (ret == NETDEV_TX_OK || ret == NETDEV_TX_BUSY)
		return ret;

	/* do not send packet before connected */
	if (((vif->mode == SPRD_MODE_STATION || vif->mode == SPRD_MODE_STATION_SECOND) &&
	     vif->sm_state != SPRD_CONNECTED) ||
	    ((vif->mode != SPRD_MODE_STATION && vif->mode != SPRD_MODE_STATION_SECOND) &&
	     !(vif->state & VIF_STATE_OPEN))) {
		printk_ratelimited("%s, %d, error! should not send this data\n",
				   __func__, __LINE__);
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	/*to improve tx throughput at start */
	if(skb->sk)
		sk_pacing_shift_update(skb->sk, 7);
#ifdef ENABLE_PAM_WIFI
	if (vif->mode == SPRD_MODE_AP && sprd_pamwifi_supported(hif->pdev)) {
		ret = sprd_pamwifi_xmit_to_ipa(skb, ndev);
		if(ret != PAMWIFI_DISABLED)
			return ret;
	}
#endif
	msg = sprd_chip_get_msg(&vif->priv->chip, SPRD_TYPE_DATA, vif->mode);
	if (!msg) {
		ndev->stats.tx_fifo_errors++;
		return NETDEV_TX_BUSY;
	}

	offset = sprd_send_data_offset(vif->priv);

	skb_len = skb->len;
	print_len = skb->len > 64 ? 64 : skb->len;
	print_hex_dump_debug("TX packet: ", DUMP_PREFIX_OFFSET,
			     16, 1, skb->data, print_len, 0);
	ret = sprd_send_data(vif->priv, vif, msg, skb, SPRD_DATA_TYPE_NORMAL,
			     offset, true);
	if (ret) {
		netdev_err(ndev, "%s drop msg due to TX Err\n", __func__);
		/* FIXME
		 * as debug sdiom later, just drop the msg here
		 * wapi temp drop
		 */
		return NETDEV_TX_OK;
	}

	vif->ndev->stats.tx_bytes += skb_len;
	vif->ndev->stats.tx_packets++;
out:
	return NETDEV_TX_OK;
}

static struct net_device_stats *iface_get_stats(struct net_device *ndev)
{
	return &ndev->stats;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
static void iface_tx_timeout(struct net_device *ndev, unsigned int txqueue)
#else
static void iface_tx_timeout(struct net_device *ndev)
#endif
{
	netdev_info(ndev, "%s\n", __func__);
	netif_wake_queue(ndev);
}

static int iface_get_user_data(void __user *data,
			       struct android_wifi_priv_cmd *priv_cmd, char **cmd)
{
	char *command = NULL;

	if (!data)
		return -EINVAL;

	if (copy_from_user(priv_cmd, data, sizeof(*priv_cmd)))
		return -EFAULT;

	/* add length check to avoid invalid NULL ptr */
	if (priv_cmd->total_len <= 0 || priv_cmd->total_len > 4096) {
		wl_err("%s: priv cmd total len is invalid\n", __func__);
		return -EINVAL;
	}

	command = kzalloc(priv_cmd->total_len + 4, GFP_KERNEL);
	if (!command)
		return -ENOMEM;

	if (copy_from_user(command, priv_cmd->buf, priv_cmd->total_len)) {
		kfree(command);
		command = NULL;
		return -EFAULT;
	}
	*cmd = command;
	return 0;
}

static int iface_priv_cmd_ccn0(struct net_device *ndev,
			       struct android_wifi_priv_cmd priv_cmd, char *command)
{
	struct sprd_vif *vif = netdev_priv(ndev);
	struct sprd_priv *priv = vif->priv;
	u8 feat = 0, status = 0;
	int ret = 0, skip;
	u16 interval = 0;

	if (!strncasecmp(command, CMD_11V_GET_CFG,
				strlen(CMD_11V_GET_CFG))) {
		/* deflaut CP support all featrue */
		if (priv_cmd.total_len < (strlen(CMD_11V_GET_CFG) + 4))
			goto len_err;

		memset(command, 0, priv_cmd.total_len);
		if (priv->fw_std & SPRD_STD_11V)
			feat = priv->wnm_ft_support;

		snprintf(command, priv_cmd.total_len, "%s %d", CMD_11V_GET_CFG, feat);
		netdev_info(ndev, "%s: get 11v feat\n", __func__);
		if (copy_to_user(priv_cmd.buf, command, priv_cmd.total_len)) {
			netdev_err(ndev, "%s: get 11v copy failed\n", __func__);
			ret = -EFAULT;
		}
	} else if (!strncasecmp(command, CMD_11V_SET_CFG,
				strlen(CMD_11V_SET_CFG))) {
		skip = strlen(CMD_11V_SET_CFG) + 1;
		if (priv_cmd.total_len < skip + 1)
			goto len_err;

		status = command[skip];

		sprd_set_11v_feature_support(priv, vif, status);
	} else if (!strncasecmp(command, CMD_11V_WNM_SLEEP,
				strlen(CMD_11V_WNM_SLEEP))) {
		skip = strlen(CMD_11V_WNM_SLEEP) + 1;
		if (priv_cmd.total_len < skip + 1)
			goto len_err;

		status = command[skip];
		if (status) {
			if (priv_cmd.total_len < skip + 4)
				goto len_err;
			interval = command[skip + 1];
		}
		netdev_info(ndev, "%s: 11v sleep, status %d, interval %d\n",
			    __func__, status, interval);
		sprd_set_11v_sleep_mode(priv, vif, status, interval);
	} else {
		ret = 1;
	}
	return ret;

len_err:
	netdev_info(ndev, "%s: priv cmd total len is invalid: %d\n",
		    __func__, priv_cmd.total_len);
	return -EINVAL;
}

static int iface_priv_cmd_ccn1(struct net_device *ndev,
			       struct android_wifi_priv_cmd priv_cmd, char *command)
{
	struct sprd_vif *vif = netdev_priv(ndev);
	struct sprd_priv *priv = vif->priv;
	int ret = 0, skip, n_clients;
	char country[SPRD_COUNTRY_CODE_LEN + 1];

	if (!strncasecmp(command, CMD_SET_COUNTRY,
			 strlen(CMD_SET_COUNTRY))) {
		skip = strlen(CMD_SET_COUNTRY) + 1;
		if (priv_cmd.total_len < skip + 2)
			goto len_err;

		memcpy(country, command + skip, SPRD_COUNTRY_CODE_LEN);
		country[SPRD_COUNTRY_CODE_LEN] = '\0';
		ret = regulatory_hint(priv->wiphy, country);
	} else if (!strncasecmp(command, CMD_SET_MAX_CLIENTS,
				strlen(CMD_SET_MAX_CLIENTS))) {
		skip = strlen(CMD_SET_MAX_CLIENTS) + 1;
		if (priv_cmd.total_len <= skip)
			goto len_err;

		ret = kstrtou32(command + skip, 10, &n_clients);
		if (ret < 0) {
			ret = -EINVAL;
			goto out;
		}
		ret = sprd_set_max_clients_allowed(priv, vif, n_clients);
	} else if (!strncasecmp(command, CMD_BT_COEX_MODE,
				strlen(CMD_BT_COEX_MODE))) {
		netdev_info(ndev, "%s received command BTCOEXMODE", __func__);
		/* To pass vts test, for details, please see Bug 1881011*/
		ret = 0;
	} else if (!strncasecmp(command, CMD_BT_COEX_SCAN,
				strlen(CMD_BT_COEX_SCAN))) {
		netdev_info(ndev, "%s received command BTCOEXSCAN", __func__);
		/* To pass vts test, for details, please see Bug 1881011*/
		ret = 0;
	} else if (!strncasecmp(command, CMD_RX_FILTER, strlen(CMD_RX_FILTER))) {
		netdev_info(ndev, "%s received command RXFILTER", __func__);
		/* To pass vts test, for details, please see Bug 2910563*/
		ret = 0;
	} else {
		netdev_err(ndev, "%s command not support\n", __func__);
		ret = -ENOTSUPP;
	}

out:
	return ret;

len_err:
	netdev_info(ndev, "%s: priv cmd total len is invalid: %d\n",
		    __func__, priv_cmd.total_len);
	return -EINVAL;
}

static int iface_priv_cmd(struct net_device *ndev, void __user *data)
{
	struct android_wifi_priv_cmd priv_cmd;
	char *command = NULL;
	int ret = 0;

	ret = iface_get_user_data(data, &priv_cmd, &command);
	if (ret)
		return ret;

	ret = iface_priv_cmd_ccn0(ndev, priv_cmd, command);
	if (ret == 1)
		ret =  iface_priv_cmd_ccn1(ndev, priv_cmd, command);

	kfree(command);
	command = NULL;
	return ret;
}

static int iface_set_power_save(struct net_device *ndev, void __user *data)
{
	struct sprd_vif *vif = netdev_priv(ndev);
	struct sprd_priv *priv = vif->priv;
	struct android_wifi_priv_cmd priv_cmd;
	char *command = NULL;
	int ret = 0, skip, value;

	if (!data)
		return -EINVAL;
	if (copy_from_user(&priv_cmd, data, sizeof(priv_cmd)))
		return -EFAULT;

	/* add length check to avoid invalid NULL ptr */
	if (priv_cmd.total_len <= 0 || priv_cmd.total_len > 4096) {
		netdev_err(ndev, "%s: priv cmd total len is invalid\n",
			   __func__);
		return -EINVAL;
	}

	command = kzalloc(priv_cmd.total_len + 4, GFP_KERNEL);
	if (!command)
		return -ENOMEM;
	if (copy_from_user(command, priv_cmd.buf, priv_cmd.total_len)) {
		ret = -EFAULT;
		goto out;
	}

	if (!strncasecmp(command, CMD_SETSUSPENDMODE,
			 strlen(CMD_SETSUSPENDMODE))) {
		skip = strlen(CMD_SETSUSPENDMODE) + 1;
		if (priv_cmd.total_len <= skip)
			goto len_err;
		ret = kstrtoint(command + skip, 0, &value);
		if (ret)
			goto out;

		priv->is_screen_off = value;
		ret = sprd_power_save(priv, vif, SPRD_SCREEN_ON_OFF, value);
	} else if (!strncasecmp(command, CMD_SET_FCC_CHANNEL,
				strlen(CMD_SET_FCC_CHANNEL))) {
		skip = strlen(CMD_SET_FCC_CHANNEL) + 1;
		if (priv_cmd.total_len <= skip)
			goto len_err;

		ret = kstrtoint(command + skip, 0, &value);
		if (ret)
			goto out;

		ret = sprd_power_save(priv, vif, SPRD_SET_FCC_CHANNEL, value);
	} else if (!strncasecmp(command, CMD_SET_SAR,
				strlen(CMD_SET_SAR))) {
		skip = strlen(CMD_SET_SAR) + 1;
		if (priv_cmd.total_len <= skip)
			goto len_err;

		ret = kstrtoint(command + skip, 0, &value);
		if (ret)
			goto out;
		netdev_info(ndev, "%s: set sar,value : %d\n",
			    __func__, value);
		ret = sprd_set_sar(priv, vif, SPRD_SET_SAR_ABSOLUTE, value);
	} else if (!strncasecmp(command, CMD_REDUCE_TX_POWER,
				strlen(CMD_REDUCE_TX_POWER))) {
		skip = strlen(CMD_REDUCE_TX_POWER) + 1;
		if (priv_cmd.total_len <= skip)
			goto len_err;

		ret = kstrtoint(command + skip, 0, &value);
		if (ret)
			goto out;
		netdev_info(ndev, "%s: reduce tx power,value : %d\n",
			    __func__, value);
		ret = sprd_power_save(priv, vif, SPRD_SET_TX_POWER, value);
	} else {
		netdev_err(ndev, "%s command not support\n", __func__);
		ret = -ENOTSUPP;
	}

out:
	kfree(command);
	return ret;

len_err:
	netdev_info(ndev, "%s: priv cmd total len is invalid: %d\n",
		    __func__, priv_cmd.total_len);
	kfree(command);
	return -EINVAL;
}

static int iface_set_p2p_mac(struct net_device *ndev, void __user *data)
{
	struct sprd_vif *vif = netdev_priv(ndev);
	struct sprd_priv *priv = vif->priv;
	struct android_wifi_priv_cmd priv_cmd;
	char *command = NULL;
	int ret = 0;
	struct sprd_vif *tmp1, *tmp2;
	u8 addr[ETH_ALEN] = { 0 };
	bool is_found = false;
	#define P2P_MAC_SKIP_LEN 11

	if (!data)
		return -EINVAL;
	if (copy_from_user(&priv_cmd, data, sizeof(priv_cmd)))
		return -EFAULT;

	/* add length check to avoid invalid NULL ptr */
	if (priv_cmd.total_len < P2P_MAC_SKIP_LEN + ETH_ALEN ||
		priv_cmd.total_len > 4096) {
		netdev_err(ndev, "%s: priv cmd total len is invalid\n",
			   __func__);
		return -EINVAL;
	}

	command = kzalloc(priv_cmd.total_len + 4, GFP_KERNEL);
	if (!command)
		return -ENOMEM;
	if (copy_from_user(command, priv_cmd.buf, priv_cmd.total_len)) {
		ret = -EFAULT;
		goto out;
	}

	memcpy(addr, command + P2P_MAC_SKIP_LEN, ETH_ALEN);
#ifdef CONFIG_SPRD_WLAN_DEBUG
	netdev_info(ndev, "p2p dev random addr is %pM\n", addr);
#else
	netdev_info(ndev, "p2p dev random addr is %02x:%02x:%02x:%02x:xx:xx\n",
		    addr[0], addr[1], addr[2], addr[3]);
#endif
	if (is_multicast_ether_addr(addr)) {
		netdev_err(ndev, "%s invalid addr\n", __func__);
		ret = -EINVAL;
		goto out;
	} else if (is_zero_ether_addr(addr)) {
		netdev_info(ndev, "restore to vif addr if addr is zero\n");
		memcpy(addr, vif->mac, ETH_ALEN);
	}

	spin_lock_bh(&priv->list_lock);
	list_for_each_entry_safe_reverse(tmp1, tmp2, &priv->vif_list,
					 vif_node) {
		if (tmp1->mode == SPRD_MODE_P2P_DEVICE) {
			netdev_info(ndev,
				    "get p2p device, set addr for wdev\n");
			memcpy(tmp1->wdev.address, addr, ETH_ALEN);
			is_found = true;
			break;
		}
	}
	spin_unlock_bh(&priv->list_lock);

	if (!is_found) {
		netdev_err(ndev, "%s Can not find p2p device\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	ret = sprd_set_random_mac(tmp1->priv, tmp1,
				  SPRD_CONNECT_RANDOM_ADDR, addr);
	if (ret) {
		netdev_err(ndev, "%s set p2p mac cmd error\n", __func__);
		ret = -EFAULT;
		goto out;
	}

out:
	kfree(command);
	return ret;
}

static int iface_set_ndev_mac(struct net_device *ndev, void __user *data)
{
	struct sprd_vif *vif = netdev_priv(ndev);
	struct sprd_hif *hif = &vif->priv->hif;
	struct android_wifi_priv_cmd priv_cmd;
	char *command = NULL;
	int ret = 0;
	u8 addr[ETH_ALEN] = { 0 };

	if (!data)
		return -EINVAL;
	if (copy_from_user(&priv_cmd, data, sizeof(priv_cmd)))
		return -EFAULT;

	/* add length check to avoid invalid NULL ptr */
	if (priv_cmd.total_len < ETH_ALEN || priv_cmd.total_len > 4096) {
		netdev_info(ndev, "%s: priv cmd total len is invalid\n",
			    __func__);
		return -EINVAL;
	}

	command = kzalloc(priv_cmd.total_len + 4, GFP_KERNEL);
	if (!command)
		return -ENOMEM;
	if (copy_from_user(command, priv_cmd.buf, priv_cmd.total_len)) {
		ret = -EFAULT;
		goto out;
	}

	memcpy(addr, command, ETH_ALEN);
#ifdef CONFIG_SPRD_WLAN_DEBUG
	netdev_info(ndev, "Device addr of '%s' is %pM\n", ndev->name, addr);
#else
	netdev_info(ndev, "Device addr of '%s' is %02x:%02x:%02x:%02x:xx:xx\n",
		    ndev->name, addr[0], addr[1], addr[2], addr[3]);
#endif
	if (is_multicast_ether_addr(addr) || is_zero_ether_addr(addr)) {
		netdev_err(ndev, "%s invalid addr\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	dev_addr_set(ndev, addr);
	ether_addr_copy(ndev->perm_addr, ndev->dev_addr);
	ether_addr_copy(vif->wdev.address, addr);
	ether_addr_copy(vif->priv->default_mac, addr);
	ether_addr_copy(vif->mac, addr);

	/* iface_register_netdev has generated an invalid address, and sent to
	 * cp2 by CMD_OPEN command, so it is necessary to update a
	 * correct
	 * netdevice address to cp2
	 */
	if (atomic_read(&hif->power_cnt) != 0) {
		netdev_dbg(ndev, "set nedv mac to cp2: %pM\n", addr);
		ret = sprd_set_random_mac(vif->priv, vif,
					  SPRD_CONNECT_RANDOM_ADDR,
					  addr);
		if (ret) {
			netdev_err(ndev, "%s set ndev mac error\n", __func__);
			ret = -EFAULT;
			goto out;
		}
	}

out:
	kfree(command);
	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
static int iface_ioctl(struct net_device *ndev, struct ifreq *req, void __user *data,  int cmd)
#else
static int iface_ioctl(struct net_device *ndev, struct ifreq *req, int cmd)
#endif
{
	struct sprd_vif *vif = netdev_priv(ndev);
	struct sprd_priv *priv = vif->priv;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0))
	void __user *data = req->ifr_data;
#endif

	switch (cmd) {
	case SPRDWLIOCTL:
	case SPRDWLSETCOUNTRY:
		return iface_priv_cmd(ndev, data);
	case SPRDWLSETMIRACAST:
		return sprd_set_miracast(priv, ndev, data);
	case SPRDWLSETFCC:
	case SPRDWLSETSUSPEND:
		return iface_set_power_save(ndev, data);
	case SPRDWLVOWIFI:
		return sprd_set_vowifi(priv, ndev, data);
	case SPRDWLSETP2PMAC:
		return iface_set_p2p_mac(ndev, data);
	case SPRDWLSETNDEVMAC:
		return iface_set_ndev_mac(ndev, data);
	case SPRDWLSNIFFER:
		return sprd_set_sniffer(priv, ndev, data);
	default:
		netdev_err(ndev, "Unsupported IOCTL %d\n", cmd);
		return -ENOTSUPP;
	}
	return 0;
}

static int iface_set_mac(struct net_device *dev, void *addr)
{
	struct sprd_vif *vif = netdev_priv(dev);
	struct sockaddr *sa = (struct sockaddr *)addr;
	int ret;

	if (!dev) {
		netdev_err(dev, "Invalid net device\n");
		return -EINVAL;
	}

	/*2025.3.20 Modify print to avoid printing mac sensitive information*/
	netdev_info(dev, "%s() receive mac: %02x:**:**:**:**:%02x, vif-> mac : %02x:**:**:**:**:%02x\n",
		    __func__, sa->sa_data[0],sa->sa_data[5],  vif->mac[0], vif->mac[5]);
	if (is_multicast_ether_addr(sa->sa_data)) {
		netdev_err(dev, "invalid, it is multicast addr: %pM\n",
			   sa->sa_data);
		return -EINVAL;
	}

	if (!is_zero_ether_addr(sa->sa_data)) {
		if ((ether_addr_equal(vif->mac, sa->sa_data)) &&
		    (vif->wdev.iftype != NL80211_IFTYPE_STATION)) {
			netdev_info(dev,
				    "equal to vif mac, no need set to cp\n");
			memset(vif->random_mac, 0, ETH_ALEN);
			dev_addr_set(dev, sa->sa_data);
			vif->has_rand_mac = false;
			return 0;
		}
		vif->has_rand_mac = true;
		memcpy(vif->random_mac, sa->sa_data, ETH_ALEN);
		dev_addr_set(dev, sa->sa_data);
		if (vif->state & VIF_STATE_OPEN) {
#ifdef CONFIG_SPRD_WLAN_DEBUG
			netdev_info(dev, "set random mac(%pM) to cp2\n", sa->sa_data);
#else
			netdev_info(dev, "set random mac(%02x:%02x:%02x:%02x:xx:xx) to cp2\n",
				    sa->sa_data[0], sa->sa_data[1], sa->sa_data[2], sa->sa_data[3]);
#endif
			ret = sprd_set_random_mac(vif->priv, vif,
						  SPRD_CONNECT_RANDOM_ADDR,
						  vif->random_mac);
			if (ret) {
				netdev_err(dev, "%s set station/gc random mac error\n",
					   __func__);
				return -EFAULT;
			}
		}
	} else {
		vif->has_rand_mac = false;
		netdev_info(dev, "need clear random mac\n");
		memset(vif->random_mac, 0, ETH_ALEN);
		dev_addr_set(dev, vif->mac);
	}

	/* return success to pass vts test */
	return 0;
}

static bool iface_mac_addr_changed(struct net_device *ndev)
{
	struct sprd_vif *vif = netdev_priv(ndev);
	struct netdev_hw_addr *ha;
	u8 mc_count, index;
	u8 *mac_addr;
	bool found;

	mc_count = netdev_mc_count(ndev);

	if (mc_count != vif->mc_filter->mac_num)
		return true;

	mac_addr = vif->mc_filter->mac_addr;
	netdev_for_each_mc_addr(ha, ndev) {
		found = false;
		for (index = 0; index < vif->mc_filter->mac_num; index++) {
			if (!memcmp(ha->addr, mac_addr, ETH_ALEN)) {
				found = true;
				break;
			}
			mac_addr += ETH_ALEN;
		}

		if (!found)
			return true;
	}
	return false;
}

static void iface_set_multicast(struct net_device *ndev)
{
	struct sprd_vif *vif = netdev_priv(ndev);
	struct sprd_priv *priv = vif->priv;
	struct sprd_work *work;
	struct netdev_hw_addr *ha;
	u8 mc_count;
	u8 *mac_addr;

	mc_count = netdev_mc_count(ndev);
	netdev_info(ndev, "%s multicast address num: %d\n", __func__, mc_count);
	if (mc_count > priv->max_mc_mac_addrs)
		return;

	vif->mc_filter->mc_change = false;
	if ((ndev->flags & IFF_MULTICAST) && (iface_mac_addr_changed(ndev))) {
		mac_addr = vif->mc_filter->mac_addr;
		netdev_for_each_mc_addr(ha, ndev) {
			netdev_dbg(ndev, "%s set mac: %pM\n", __func__,
				    ha->addr);
			if ((ha->addr[0] != 0x33 || ha->addr[1] != 0x33) &&
			    (ha->addr[0] != 0x01 || ha->addr[1] != 0x00 ||
			     ha->addr[2] != 0x5e || ha->addr[3] > 0x7f)) {
				netdev_info(ndev, "%s invalid addr\n",
					    __func__);
				return;
			}
			ether_addr_copy(mac_addr, ha->addr);
			mac_addr += ETH_ALEN;
		}
		vif->mc_filter->mac_num = mc_count;
		vif->mc_filter->mc_change = true;
	} else if (!(ndev->flags & IFF_MULTICAST) && vif->mc_filter->mac_num) {
		vif->mc_filter->mac_num = 0;
		vif->mc_filter->mc_change = true;
	}

	work = sprd_alloc_work(0);
	if (!work) {
		netdev_err(ndev, "%s out of memory\n", __func__);
		return;
	}
	work->vif = vif;
	work->id = SPRD_WORK_MC_FILTER;
	vif->mc_filter->subtype = SPRD_RX_MODE_MULTICAST;
	sprd_queue_work(vif->priv, work);
}

static struct net_device_ops sprd_netdev_ops = {
	.ndo_open = iface_open,
	.ndo_stop = iface_close,
	.ndo_start_xmit = iface_start_xmit,
	.ndo_get_stats = iface_get_stats,
	.ndo_tx_timeout = iface_tx_timeout,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	.ndo_siocdevprivate = iface_ioctl,
#else
	.ndo_do_ioctl = iface_ioctl,
#endif
	.ndo_set_mac_address = iface_set_mac,
};

static int iface_inetaddr_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct net_device *ndev;
	struct sprd_vif *vif;
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;

	if (!ifa || !(ifa->ifa_dev->dev))
		return NOTIFY_DONE;

	if (ifa->ifa_dev->dev->netdev_ops != &sprd_netdev_ops)
		return NOTIFY_DONE;

	ndev = ifa->ifa_dev->dev;
	vif = netdev_priv(ndev);

	if (vif->wdev.iftype == NL80211_IFTYPE_STATION ||
	    vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT) {
		netdev_info(ndev, "inetaddr event %ld\n", event);
		if (event == NETDEV_UP)
			sprd_notify_ip(vif->priv, vif, SPRD_IPV4,
				       (u8 *)&ifa->ifa_address);

		if (event == NETDEV_DOWN) {
			if (vif->priv->hif.hw_type != SPRD_HW_SC2355_PCIE)
				sprd_fc_add_share_credit(vif->priv, vif);

			sprd_qos_reset_wmmac_parameters(vif->priv);
			sprd_qos_reset_wmmac_ts_info(vif->priv);
			sprd_qos_init_default_map(vif->priv);
		}
	}

	return NOTIFY_DONE;
}

static struct notifier_block iface_inetaddr_cb = {
	.notifier_call = iface_inetaddr_event,
};

static int iface_inetaddr6_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	struct net_device *ndev;
	struct sprd_vif *vif;
	struct inet6_ifaddr *inet6_ifa = (struct inet6_ifaddr *)ptr;
	struct sprd_work *work;
	u8 *ipv6_addr;

	if (!inet6_ifa || !(inet6_ifa->idev->dev))
		return NOTIFY_DONE;

	if (inet6_ifa->idev->dev->netdev_ops != &sprd_netdev_ops)
		return NOTIFY_DONE;

	ndev = inet6_ifa->idev->dev;
	vif = netdev_priv(ndev);

	if (vif->wdev.iftype == NL80211_IFTYPE_STATION ||
	    vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT) {
		if (event == NETDEV_UP) {
			work = sprd_alloc_work(SPRD_IPV6_ADDR_LEN);
			if (!work) {
				netdev_err(ndev, "%s out of memory\n",
					   __func__);
				return NOTIFY_DONE;
			}
			work->vif = vif;
			work->id = SPRD_WORK_NOTIFY_IP;
			ipv6_addr = (u8 *)work->data;
			memcpy(ipv6_addr, (u8 *)&inet6_ifa->addr,
			       SPRD_IPV6_ADDR_LEN);
			sprd_queue_work(vif->priv, work);
		}
	}
	return NOTIFY_DONE;
}

static struct notifier_block iface_inet6addr_cb = {
	.notifier_call = iface_inetaddr6_event,
};

static int iface_notify_init(struct sprd_priv *priv)
{
	int ret = 0;
	struct sprd_hif *hif;

	hif = &priv->hif;
	mutex_init(&hif->reset_lock);
	atomic_notifier_chain_register(&wcn_reset_notifier_list,
				       &iface_host_reset_cb);

	ret = misc_register(&wlan_misc_device);
	if (ret)
		wl_err("wlan misc dev register fail\n");

	ret = register_inetaddr_notifier(&iface_inetaddr_cb);
	if (ret) {
		wl_err("%s failed to register inetaddr notifier(%d)!\n",
		       __func__, ret);
		return ret;
	}

	if (priv->fw_capa & SPRD_CAPA_NS_OFFLOAD) {
		wl_debug("\tIPV6 NS Offload supported\n");
		ret = register_inet6addr_notifier(&iface_inet6addr_cb);
		if (ret) {
			wl_err
			    ("%s failed to register inet6addr notifier(%d)!\n",
			     __func__, ret);
			return ret;
		}
	}

	return ret;
}

static void iface_notify_deinit(struct sprd_priv *priv)
{
	struct sprd_hif *hif;

	hif = &priv->hif;
	misc_deregister(&wlan_misc_device);
	atomic_notifier_chain_unregister(&wcn_reset_notifier_list,
					 &iface_host_reset_cb);
	mutex_destroy(&hif->reset_lock);
	unregister_inetaddr_notifier(&iface_inetaddr_cb);
	if (priv->fw_capa & SPRD_CAPA_NS_OFFLOAD)
		unregister_inet6addr_notifier(&iface_inet6addr_cb);
}

static void iface_init_vif(struct sprd_priv *priv, struct sprd_vif *vif,
			   const char *name)
{
	WARN_ON(strlen(name) >= sizeof(vif->name));

	strcpy(vif->name, name);
	vif->priv = priv;
	vif->sm_state = SPRD_DISCONNECTED;
	mutex_init(&vif->survey_lock);
	INIT_LIST_HEAD(&vif->survey_info_list);
	INIT_LIST_HEAD(&vif->scan_head_ptr);
}

static void iface_deinit_vif(struct sprd_vif *vif)
{
	int cnt = 0;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	sprd_report_scan_done(vif, true);
	sprd_report_sched_scan_done(vif, true);
	/* clear all the work in vif which is going to be removed */
	sprd_cancel_work(vif->priv, vif);

	if (vif->ref > 0) {
		do {
			usleep_range(2000, 2500);
			cnt++;
			if (time_after(jiffies, timeout)) {
				netdev_err(vif->ndev, "%s timeout cnt %d\n",
					   __func__, cnt);
				break;
			}
		} while (vif->ref > 0);
		netdev_dbg(vif->ndev, "cnt %d\n", cnt);
	}
	mutex_destroy(&vif->survey_lock);
}

static struct sprd_vif *iface_register_wdev(struct sprd_priv *priv,
					    const char *name,
					    enum nl80211_iftype type, u8 *addr)
{
	struct sprd_vif *vif;
	struct wireless_dev *wdev;

	vif = kzalloc(sizeof(*vif), GFP_KERNEL);
	if (!vif)
		return ERR_PTR(-ENOMEM);

	/* initialize vif stuff */
	iface_init_vif(priv, vif, name);

	/* initialize wdev stuff */
	wdev = &vif->wdev;
	wdev->wiphy = priv->wiphy;
	wdev->iftype = type;

	iface_set_mac_addr(vif, addr, wdev->address);
	wl_debug("iface '%s'(%pM) type %d added\n", name, wdev->address, type);

	return vif;
}

static void iface_unregister_wdev(struct sprd_vif *vif)
{
	wl_debug("iface '%s' deleted\n", vif->name);

	cfg80211_unregister_wdev(&vif->wdev);
	/* cfg80211_unregister_wdev use list_del_rcu to delete wdev,
	 * so we can not free vif immediately, must wait until an
	 * RCU grace period has elapsed.
	 */
	synchronize_rcu();
	iface_deinit_vif(vif);
	kfree(vif);
}

static struct sprd_vif *iface_register_netdev(struct sprd_priv *priv,
					      const char *name,
					      enum nl80211_iftype type,
					      u8 *addr)
{
	struct net_device *ndev;
	struct wireless_dev *wdev;
	struct sprd_vif *vif;
	int ret;

	ndev = alloc_netdev(sizeof(*vif), name, NET_NAME_UNKNOWN, ether_setup);
	if (!ndev) {
		wl_err("%s failed to alloc net_device!\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	/* initialize vif stuff */
	vif = netdev_priv(ndev);
	vif->ndev = ndev;
	iface_init_vif(priv, vif, name);

	/* initialize wdev stuff */
	wdev = &vif->wdev;
	wdev->netdev = ndev;
	wdev->wiphy = priv->wiphy;
	wdev->iftype = type;

	/* initialize ndev stuff */
	ndev->ieee80211_ptr = wdev;
	if (priv->fw_capa & SPRD_CAPA_MC_FILTER) {
		wl_debug("\tMulticast Filter supported\n");
		vif->mc_filter =
		    kzalloc(sizeof(struct sprd_mc_filter) +
			    priv->max_mc_mac_addrs * ETH_ALEN, GFP_KERNEL);
		if (!vif->mc_filter) {
			ret = -ENOMEM;
			goto err;
		}

		sprd_netdev_ops.ndo_set_rx_mode = iface_set_multicast;
	}
	ndev->netdev_ops = &sprd_netdev_ops;
	ndev->priv_destructor = free_netdev;
	ndev->needed_headroom = sprd_needed_headroom(priv);
	ndev->watchdog_timeo = 2 * HZ;
	ndev->features |= priv->hif.feature;
	SET_NETDEV_DEV(ndev, wlan_misc_device.this_device);

	iface_set_mac_addr(vif, addr, vif->mac);
	dev_addr_set(ndev, vif->mac);

	/* register new Ethernet interface */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	ret = cfg80211_register_netdevice(ndev);
#else
	ret = register_netdevice(ndev);
#endif
	if (ret) {
		netdev_err(ndev, "failed to regitster netdev(%d)!\n", ret);
		goto err;
	}
#ifdef CONFIG_SPRD_WLAN_DEBUG
	wl_info("iface '%s'(%pM) type %d added\n",
		ndev->name, ndev->dev_addr, type);
#else
	wl_info("iface '%s'(%02x:%02x:%02x:%02x:xx:xx) type %d added\n",
		ndev->name, ndev->dev_addr[0], ndev->dev_addr[1],
		ndev->dev_addr[2], ndev->dev_addr[3],type);
#endif
	return vif;
err:
	iface_deinit_vif(vif);
	free_netdev(ndev);
	return ERR_PTR(ret);
}

static void iface_unregister_netdev(struct sprd_vif *vif)
{
	wl_debug("iface '%s' deleted\n", vif->ndev->name);

	iface_deinit_vif(vif);

	if (vif->priv->fw_capa & SPRD_CAPA_MC_FILTER)
		kfree(vif->mc_filter);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	cfg80211_unregister_netdevice(vif->ndev);
#else
	unregister_netdevice(vif->ndev);
#endif
}

struct wireless_dev *sprd_add_iface(struct sprd_priv *priv, const char *name,
				    enum nl80211_iftype type, u8 *addr)
{
	struct sprd_vif *vif, *temp_vif;

	if (type == NL80211_IFTYPE_P2P_DEVICE)
		vif = iface_register_wdev(priv, name, type, addr);
	else
		vif = iface_register_netdev(priv, name, type, addr);

	if (IS_ERR(vif)) {
		wl_err("failed to add iface '%s'\n", name);
		return (void *)vif;
	}

	spin_lock_bh(&priv->list_lock);
	list_add_tail(&vif->vif_node, &priv->vif_list);
	spin_unlock_bh(&priv->list_lock);

	/* if scan commands of other modes exist when adding the interface
	 * of type 3, the scan commands of other modes must be abandoned because
	 * of the acs scan command in ap mode will be blocked.
	 */
	if (priv->scan_request && vif->wdev.iftype == NL80211_IFTYPE_AP) {
		if (priv->scan_vif) {
			temp_vif = priv->scan_vif;
			wl_err("passive abort iftype %d scan when added iftype AP.\n",
			       temp_vif->wdev.iftype);
			sprd_abort_scan(priv, priv->wiphy, &temp_vif->wdev);
			/* cp2 may take a while for scan_done to be returned, so scan_done
			 * is reported first here.
			 */
			sprd_report_scan_done(temp_vif, true);
		}
	}

#ifdef ENABLE_DFS
	sprd_init_dfs_master(vif->priv, vif);
#endif

	return &vif->wdev;
}

int sprd_del_iface(struct sprd_priv *priv, struct sprd_vif *vif)
{
	if (!vif->ndev)
		iface_unregister_wdev(vif);
	else
		iface_unregister_netdev(vif);

#ifdef ENABLE_DFS
	sprd_deinit_dfs_master(vif->priv, vif);
#endif
	return 0;
}

static void iface_del_all_ifaces(struct sprd_priv *priv)
{
	struct sprd_vif *vif, *tmp;

next_intf:
	spin_lock_bh(&priv->list_lock);
	list_for_each_entry_safe_reverse(vif, tmp, &priv->vif_list, vif_node) {
		list_del(&vif->vif_node);
		spin_unlock_bh(&priv->list_lock);
		rtnl_lock();
		sprd_del_iface(priv, vif);
		rtnl_unlock();
		goto next_intf;
	}

	spin_unlock_bh(&priv->list_lock);
}

static int iface_core_init(struct device *dev, struct sprd_priv *priv)
{
	struct wiphy *wiphy = priv->wiphy;
	struct wireless_dev *wdev;
	struct sprd_hif *hif;
	int ret;

	sprd_tcp_ack_init(priv);
	sprd_fcc_init(priv);
	sprd_setup_wiphy(wiphy, priv);
	sprd_vendor_init(priv, wiphy);
	set_wiphy_dev(wiphy, dev);
	ret = wiphy_register(wiphy);
	if (ret) {
		wiphy_err(wiphy, "failed to regitster wiphy(%d)!\n", ret);
		goto out;
	}

	rtnl_lock();
	wdev = sprd_add_iface(priv, "wlan%d", NL80211_IFTYPE_STATION, NULL);
	rtnl_unlock();
	if (IS_ERR(wdev)) {
		wiphy_unregister(wiphy);
		ret = -ENXIO;
		goto out;
	}

	sprd_init_npi();

	hif = &priv->hif;
	sprd_5g_sar_info_init(priv);

	sprd_qos_enable(priv, 1);

	sprd_debug_init(&priv->debug);
out:
	return ret;
}

static int iface_core_deinit(struct sprd_priv *priv)
{
	sprd_debug_deinit(&priv->debug);
	sprd_qos_enable(priv, 0);
	sprd_deinit_npi();
#ifdef DRV_RESET_SELF
	sprd_cancel_reset_work(priv);
#endif
	iface_del_all_ifaces(priv);
	sprd_vendor_deinit(priv, priv->wiphy);
	wiphy_unregister(priv->wiphy);
	sprd_tcp_ack_deinit(priv);

	return 0;
}

int sprd_iface_probe(struct platform_device *pdev,
		     struct sprd_hif_ops *hif_ops,
		     struct sprd_chip_ops *chip_ops)
{
	struct sprd_priv *priv;
	struct sprd_hif *hif;
	int ret = 0;

	wl_info("Spreadtrum WLAN Driver (Ver. %s, %s)\n",
		SPRD_DRIVER_VERSION, utsname()->release);

	priv = sprd_core_create(chip_ops);
	if (!priv) {
		wl_err("%s core create fail\n", __func__);
		return -ENXIO;
	}

	priv->probe_done = false;
	iface_set_priv(priv);
	platform_set_drvdata(pdev, priv);
	hif = &priv->hif;
	hif->priv = priv;
	hif->pdev = pdev;
	hif->ops = hif_ops;
	sprd_wlan_parse_dt(priv);

	ret = sprd_hif_init(hif);
	if (ret) {
		wl_err("%s hif init failed: %d\n", __func__, ret);
		goto err_hif_init;
	}

	ret = sprd_iface_set_power(hif, true);
	if (ret) {
		wl_err("%s iface_set_power failed : %d", __func__, ret);
		goto err_power_on;
	}

	ret = iface_core_init(&pdev->dev, priv);
	if (ret) {
		wl_err("%s core init failed: %d\n", __func__, ret);
		goto err_core_init;
	}

	ret = iface_notify_init(priv);
	if (ret) {
		wl_err("%s notify init failed: %d\n", __func__, ret);
		goto err_notify_init;
	}

	/* Power off chipset in order to save power */
	sprd_iface_set_power(hif, false);
	priv->probe_done = true;

	return ret;

err_notify_init:
	iface_core_deinit(priv);
err_core_init:
	sprd_iface_set_power(hif, false);
err_power_on:
	sprd_hif_deinit(hif);
err_hif_init:
	sprd_core_free(priv);
	return ret;
}

int sprd_iface_remove(struct platform_device *pdev)
{
	struct sprd_priv *priv = platform_get_drvdata(pdev);
	struct sprd_hif *hif = &priv->hif;
	int ret;

	ret = sprd_iface_set_power(hif, true);
	if (ret)
		return ret;

	iface_notify_deinit(priv);
	iface_core_deinit(priv);
	sprd_iface_set_power(hif, false);
	sprd_hif_deinit(hif);
	sprd_core_free(priv);
	iface_set_priv(NULL);

	return 0;
}

MODULE_DESCRIPTION("Spreadtrum Wireless LAN Common Code");
MODULE_AUTHOR("Spreadtrum WCN Division");
MODULE_LICENSE("GPL");
