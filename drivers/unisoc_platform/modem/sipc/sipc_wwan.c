/*
 * A WWAN AT port for the Unisoc SIPC AT channel.
 *
 * The modem's AT interpreter sits on SIPC channel 6, the "stty_nr" sbuf:
 * ring 1 is the command channel (Android's RIL and unisoc-cpd send on it),
 * ring 0 carries the unsolicited result codes and is receive-only -- nothing
 * ever writes to it, and ModemManager probing it with "AT" left the CP's AT
 * server silent for every client until a reboot.  spipe exposes the rings as
 * character devices (/dev/stty_nrN), which ModemManager cannot use: it wants
 * ttys or WWAN ports.  This driver registers one WWAN AT port (/dev/wwanXat0),
 * as rpmsg_wwan_ctrl does for the Qualcomm SMD channels: writes go to the
 * command ring, reads return the command ring's replies with ring 0's URCs
 * merged in -- what a modem's primary AT port looks like.  URCs are merged by
 * whole lines and only between reply lines, so they never split a reply; the
 * command ring passes straight through (the SMS "> " prompt has no newline).
 *
 * The rings are drained from the moment the module loads, whether the port
 * is open or not; what arrives while it is closed is dropped.  A ring nobody
 * reads fills in seconds while the modem is sending reports, the CP's queue
 * backs up behind it and the CP asserts ("The queue was full") -- it did,
 * three seconds into a handover from unisoc-cpd, and ModemManager itself
 * closes and reopens the port while probing.  The rings are the same ones
 * spipe hands out as /dev/stty_nrN, so this module and a user of those
 * character devices (unisoc-cpd) are mutually exclusive: load it only when
 * ModemManager is to own the modem.
 *
 * The other card's rings count as well.  The CP runs one AT interpreter per
 * SIM card on the same sbuf: rings 0-2 for the first card (URCs on 0), 3-5
 * for the second (URCs on 3), as the vendor RIL opens them.  Whichever card
 * the port is on, the other one's URC ring is read and dropped (drain_rings):
 * with the port on the second card, ring 0 filled within a minute and the
 * CP's AT server stopped answering on every ring, without an assert, until a
 * reboot.
 *
 * The CP asserts ("Error 0xb, The queue was full") when AT commands arrive
 * back to back for minutes; its RIL and unisoc-cpd space them.  tx_gap_ms
 * enforces a minimum gap between writes, so no client has to know.
 *
 * Commands end at <CR>, as the vendor RIL sends them.  ModemManager ends
 * everything it writes to a non-tty port with <CR><LF>, and the CP would take
 * the <LF> as input for whatever comes next -- after AT+CMGS=<n><CR>, the
 * first byte of the PDU.  A <LF> right after a write's closing <CR> is
 * dropped; payloads (a PDU ends in Ctrl-Z) are left alone.
 *
 * The port exists only while the channel is up.  At boot the module loads
 * well before the CP has booted, and a port that refuses to open (the channel
 * is not READY yet) is a port ModemManager probes once, fails and forgets.
 * So the port is registered when the channel comes up and removed when it
 * goes down -- a CP reset takes it -- and udev and ModemManager see the modem
 * leave and come back instead of an AT server that stopped answering.  The
 * channel announces READY to the ring handlers but not its going down, so a
 * watch polls sbuf_status() once a second as well.
 */

#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sipc.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/wwan.h>

#define SIPC_WWAN_RX_CHUNK	2048
#define SIPC_WWAN_URC_LINE	1024
#define SIPC_WWAN_URC_HELD	32

static char *node_label = "stty_nr";
module_param(node_label, charp, 0444);
MODULE_PARM_DESC(node_label, "label of the sprd,spipe node that carries AT");

static int cmd_ring = 1;
module_param(cmd_ring, int, 0444);
MODULE_PARM_DESC(cmd_ring, "sbuf ring of the AT command channel");

static int urc_ring;
module_param(urc_ring, int, 0444);
MODULE_PARM_DESC(urc_ring, "receive-only sbuf ring with the URCs, merged into the port (-1: none)");

#define SIPC_WWAN_DRAIN_MAX	4
static int drain_rings[SIPC_WWAN_DRAIN_MAX];
static int n_drain_rings = -1;
module_param_array(drain_rings, int, &n_drain_rings, 0444);
MODULE_PARM_DESC(drain_rings, "receive-only rings to read and drop (default: the URC ring, 0 or 3, the port does not use)");

static unsigned int tx_gap_ms = 300;
module_param(tx_gap_ms, uint, 0644);
MODULE_PARM_DESC(tx_gap_ms, "minimum gap between two writes, in ms");

struct sipc_wwan {
	/* registered while the channel is READY, by the watch; NULL otherwise */
	struct wwan_port *port;
	struct delayed_work watch;
	bool stopping;
	struct platform_device *pdev;
	u8 dst;
	u8 channel;
	unsigned long last_tx;
	struct mutex tx_lock;
	/* held while pushing to the port and while opening/closing it */
	struct mutex rx_lock;
	bool open;
	unsigned long dropped;
	/* both rings are served by the channel's one sbuf thread */
	bool cmd_midline;
	char urc_line[SIPC_WWAN_URC_LINE];
	unsigned int urc_len;
	struct sk_buff_head urc_held;
	unsigned long drained;
};

struct sipc_wwan_drain {
	struct sipc_wwan *sw;
	int ring;
};

static struct sipc_wwan_drain drains[SIPC_WWAN_DRAIN_MAX];
static int n_drains;

static struct sipc_wwan sipc_wwan;

static void sipc_wwan_flush_urcs(struct sipc_wwan *sw)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&sw->urc_held)))
		wwan_port_rx(sw->port, skb);
}

static void sipc_wwan_cmd_notify(int event, void *data)
{
	struct sipc_wwan *sw = data;
	struct sk_buff *skb;
	int n;

	if (event == SBUF_NOTIFY_READY) {
		mod_delayed_work(system_wq, &sw->watch, 0);
		return;
	}
	if (event != SBUF_NOTIFY_READ)
		return;

	for (;;) {
		skb = alloc_skb(SIPC_WWAN_RX_CHUNK, GFP_KERNEL);
		if (!skb)
			return;
		n = sbuf_read(sw->dst, sw->channel, cmd_ring, skb->data,
			      SIPC_WWAN_RX_CHUNK, 0);
		if (n <= 0) {
			kfree_skb(skb);
			return;
		}
		skb_put(skb, n);
		mutex_lock(&sw->rx_lock);
		if (sw->open) {
			sw->cmd_midline = skb->data[n - 1] != '\n';
			wwan_port_rx(sw->port, skb);
			if (!sw->cmd_midline)
				sipc_wwan_flush_urcs(sw);
		} else {
			sw->dropped += n;
			kfree_skb(skb);
		}
		mutex_unlock(&sw->rx_lock);
	}
}

static void sipc_wwan_urc_line(struct sipc_wwan *sw)
{
	struct sk_buff *skb;

	mutex_lock(&sw->rx_lock);
	skb = sw->open ? alloc_skb(sw->urc_len, GFP_KERNEL) : NULL;
	if (!sw->open)
		sw->dropped += sw->urc_len;
	if (skb) {
		skb_put_data(skb, sw->urc_line, sw->urc_len);
		if (sw->cmd_midline && skb_queue_len(&sw->urc_held) < SIPC_WWAN_URC_HELD)
			skb_queue_tail(&sw->urc_held, skb);
		else
			wwan_port_rx(sw->port, skb);
	}
	sw->urc_len = 0;
	mutex_unlock(&sw->rx_lock);
}

static void sipc_wwan_urc_notify(int event, void *data)
{
	struct sipc_wwan *sw = data;
	char buf[256];
	int n, i;

	if (event != SBUF_NOTIFY_READ)
		return;

	while ((n = sbuf_read(sw->dst, sw->channel, urc_ring, buf,
			      sizeof(buf), 0)) > 0) {
		for (i = 0; i < n; i++) {
			sw->urc_line[sw->urc_len++] = buf[i];
			if (buf[i] == '\n' || sw->urc_len == SIPC_WWAN_URC_LINE)
				sipc_wwan_urc_line(sw);
		}
	}
}

static void sipc_wwan_drain_notify(int event, void *data)
{
	struct sipc_wwan_drain *d = data;
	char buf[256];
	int n;

	if (event != SBUF_NOTIFY_READ)
		return;

	while ((n = sbuf_read(d->sw->dst, d->sw->channel, d->ring, buf,
			      sizeof(buf), 0)) > 0)
		d->sw->drained += n;
}

static int sipc_wwan_start(struct wwan_port *port)
{
	struct sipc_wwan *sw = wwan_port_get_drvdata(port);

	if (sbuf_status(sw->dst, sw->channel) != 0)
		return -ENODEV;

	mutex_lock(&sw->rx_lock);
	sw->last_tx = jiffies - msecs_to_jiffies(tx_gap_ms);
	sw->cmd_midline = false;
	sw->urc_len = 0;
	skb_queue_purge(&sw->urc_held);
	sw->open = true;
	mutex_unlock(&sw->rx_lock);
	return 0;
}

static void sipc_wwan_stop(struct wwan_port *port)
{
	struct sipc_wwan *sw = wwan_port_get_drvdata(port);

	mutex_lock(&sw->rx_lock);
	sw->open = false;
	skb_queue_purge(&sw->urc_held);
	mutex_unlock(&sw->rx_lock);
}

static int sipc_wwan_write(struct sipc_wwan *sw, struct sk_buff *skb,
			   int timeout)
{
	unsigned long due;
	u32 done = 0;
	int n = 0;

	mutex_lock(&sw->tx_lock);
	due = sw->last_tx + msecs_to_jiffies(tx_gap_ms);
	if (time_before(jiffies, due))
		msleep(jiffies_to_msecs(due - jiffies));

	while (done < skb->len) {
		n = sbuf_write(sw->dst, sw->channel, cmd_ring,
			       skb->data + done, skb->len - done, timeout);
		if (n <= 0)
			break;
		done += n;
	}
	sw->last_tx = jiffies;
	mutex_unlock(&sw->tx_lock);

	if (!done)
		return n < 0 ? n : -EAGAIN;
	/* a command cut short cannot be resent without duplicating it */
	if (done < skb->len)
		pr_warn_ratelimited("sipc_wwan: ring %d took %u of %u bytes\n",
				    cmd_ring, done, skb->len);
	consume_skb(skb);
	return 0;
}

static void sipc_wwan_trim_lf(struct sk_buff *skb)
{
	if (skb->len >= 2 && skb->data[skb->len - 1] == '\n' &&
	    skb->data[skb->len - 2] == '\r')
		skb_trim(skb, skb->len - 1);
}

static int sipc_wwan_tx(struct wwan_port *port, struct sk_buff *skb)
{
	sipc_wwan_trim_lf(skb);
	/* AT commands are short; a full 2 KiB ring drains within a second */
	return sipc_wwan_write(wwan_port_get_drvdata(port), skb, HZ);
}

static int sipc_wwan_tx_blocking(struct wwan_port *port, struct sk_buff *skb)
{
	sipc_wwan_trim_lf(skb);
	return sipc_wwan_write(wwan_port_get_drvdata(port), skb, -1);
}

static const struct wwan_port_ops sipc_wwan_ops = {
	.start = sipc_wwan_start,
	.stop = sipc_wwan_stop,
	.tx = sipc_wwan_tx,
	.tx_blocking = sipc_wwan_tx_blocking,
};

static void sipc_wwan_remove_port(struct sipc_wwan *sw)
{
	struct wwan_port *port = sw->port;

	/* removal stops an open port first: the rx paths see !open from then on */
	wwan_remove_port(port);
	mutex_lock(&sw->rx_lock);
	sw->port = NULL;
	mutex_unlock(&sw->rx_lock);
}

static void sipc_wwan_watch(struct work_struct *work)
{
	struct sipc_wwan *sw = container_of(to_delayed_work(work),
					    struct sipc_wwan, watch);
	bool up = sbuf_status(sw->dst, sw->channel) == 0;
	struct wwan_port *port;

	if (up && !sw->port) {
		/* no caps: the AT channel has no frame size to announce (6.x added the argument) */
		port = wwan_create_port(&sw->pdev->dev, WWAN_PORT_AT,
					&sipc_wwan_ops, NULL, sw);
		if (IS_ERR(port)) {
			pr_err_ratelimited("sipc_wwan: no AT port: %ld\n",
					   PTR_ERR(port));
		} else {
			mutex_lock(&sw->rx_lock);
			sw->port = port;
			mutex_unlock(&sw->rx_lock);
			pr_info("sipc_wwan: channel up, AT port registered\n");
		}
	} else if (!up && sw->port) {
		pr_info("sipc_wwan: channel down (CP reset?), AT port removed\n");
		sipc_wwan_remove_port(sw);
	}
	if (!READ_ONCE(sw->stopping))
		schedule_delayed_work(&sw->watch, HZ);
}

static struct device_node *sipc_wwan_find_node(void)
{
	struct device_node *np = NULL;
	const char *label;

	while ((np = of_find_compatible_node(np, NULL, "sprd,spipe"))) {
		if (!of_property_read_string(np, "label", &label) &&
		    !strcmp(label, node_label))
			return np;
	}
	return NULL;
}

static int __init sipc_wwan_init(void)
{
	struct sipc_wwan *sw = &sipc_wwan;
	struct device_node *np, *parent;
	u32 dst, channel, ringnr = 0;
	int ret, i = 0;

	np = sipc_wwan_find_node();
	if (!np) {
		pr_err("sipc_wwan: no sprd,spipe node labelled %s\n", node_label);
		return -ENODEV;
	}
	parent = of_get_parent(np);
	ret = of_property_read_u32(parent, "reg", &dst);
	of_node_put(parent);
	if (!ret)
		ret = of_property_read_u32(np, "reg", &channel);
	of_property_read_u32(np, "sprd,ringnr", &ringnr);
	sw->pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (ret || !sw->pdev) {
		pr_err("sipc_wwan: %s: no dst/channel or no platform device\n",
		       node_label);
		ret = ret ?: -ENODEV;
		goto err;
	}
	if (cmd_ring < 0 || cmd_ring >= ringnr || urc_ring >= (int)ringnr ||
	    urc_ring == cmd_ring) {
		pr_err("sipc_wwan: bad rings %d/%d (%u rings)\n", cmd_ring,
		       urc_ring, ringnr);
		ret = -EINVAL;
		goto err;
	}
	if (n_drain_rings < 0) {
		/* the two cards' URC rings, less the one merged into the port */
		n_drain_rings = 0;
		if (urc_ring != 0 && cmd_ring != 0)
			drain_rings[n_drain_rings++] = 0;
		if (urc_ring != 3 && cmd_ring != 3)
			drain_rings[n_drain_rings++] = 3;
	}
	for (n_drains = 0; n_drains < n_drain_rings; n_drains++) {
		int r = drain_rings[n_drains];

		if (r < 0 || r >= ringnr || r == cmd_ring || r == urc_ring) {
			pr_err("sipc_wwan: bad drain ring %d\n", r);
			ret = -EINVAL;
			goto err;
		}
		drains[n_drains].sw = sw;
		drains[n_drains].ring = r;
	}

	sw->dst = dst;
	sw->channel = channel;
	mutex_init(&sw->tx_lock);
	mutex_init(&sw->rx_lock);
	skb_queue_head_init(&sw->urc_held);
	INIT_DELAYED_WORK(&sw->watch, sipc_wwan_watch);
	/* drain from now on (registering also drains what is already queued) */
	ret = sbuf_register_notifier(dst, channel, cmd_ring,
				     sipc_wwan_cmd_notify, sw);
	if (!ret && urc_ring >= 0)
		ret = sbuf_register_notifier(dst, channel, urc_ring,
					     sipc_wwan_urc_notify, sw);
	for (i = 0; !ret && i < n_drains; i++) {
		ret = sbuf_register_notifier(dst, channel, drains[i].ring,
					     sipc_wwan_drain_notify, &drains[i]);
		if (ret) {
			pr_err("sipc_wwan: cannot drain ring %d: %d\n",
			       drains[i].ring, ret);
			break;
		}
	}
	if (ret) {
		/* drains[0..i-1] are registered */
		while (--i >= 0)
			sbuf_unregister_notifier(dst, channel, drains[i].ring);
		if (urc_ring >= 0)
			sbuf_unregister_notifier(dst, channel, urc_ring);
		sbuf_unregister_notifier(dst, channel, cmd_ring);
		goto err;
	}
	pr_info("sipc_wwan: AT channel %s (dst %u, channel %u): ring %d, URCs from ring %d, %d ring(s) drained\n",
		node_label, dst, channel, cmd_ring, urc_ring, n_drains);
	schedule_delayed_work(&sw->watch, 0);
	return 0;

err:
	if (sw->pdev)
		put_device(&sw->pdev->dev);
	return ret;
}

static void __exit sipc_wwan_exit(void)
{
	struct sipc_wwan *sw = &sipc_wwan;
	int i;

	/* notifiers first: a READY event would queue the watch again */
	for (i = 0; i < n_drains; i++)
		sbuf_unregister_notifier(sw->dst, sw->channel, drains[i].ring);
	if (urc_ring >= 0)
		sbuf_unregister_notifier(sw->dst, sw->channel, urc_ring);
	sbuf_unregister_notifier(sw->dst, sw->channel, cmd_ring);
	WRITE_ONCE(sw->stopping, true);
	cancel_delayed_work_sync(&sw->watch);
	if (sw->port)
		sipc_wwan_remove_port(sw);
	pr_info("sipc_wwan: %lu bytes dropped while the port was closed, %lu drained\n",
		sw->dropped, sw->drained);
	put_device(&sipc_wwan.pdev->dev);
}

module_init(sipc_wwan_init);
module_exit(sipc_wwan_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("WWAN AT port on the Unisoc SIPC AT channel");
MODULE_SOFTDEP("pre: spipe");
