// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/usb/typec.h>
#include <linux/extcon.h>
#include "fsa4480-i2c.h"

#define FSA4480_I2C_NAME	"fsa4480-driver"

#define FSA4480_SWITCH_SETTINGS 0x04
#define FSA4480_SWITCH_CONTROL  0x05
#define FSA4480_SWITCH_STATUS1  0x07
#define FSA4480_SLOW_L          0x08
#define FSA4480_SLOW_R          0x09
#define FSA4480_SLOW_MIC        0x0A
#define FSA4480_SLOW_SENSE      0x0B
#define FSA4480_SLOW_GND        0x0C
#define FSA4480_DELAY_L_R       0x0D
#define FSA4480_DELAY_L_MIC     0x0E
#define FSA4480_DELAY_L_SENSE   0x0F
#define FSA4480_DELAY_L_AGND    0x10
#define FSA4480_RESET           0x1E
#define FSA4480_CHIP_ID         0x00
#define FSA4480_VENDOR_NUM      0x09


#define AS6480_SWITCH_USB             0
#define AS6480_MODE_CTRL              1
#define AS6480_SWITCH_HEADSET         2
#define AS6480_SWITCH_GND_MIC_SWAP    3
#define AS6480_SWITCH_OFF             7

struct fsa4480_priv {
	struct regmap *regmap;
	struct extcon_dev *edev;
	struct device *dev;
	struct notifier_block nb;
	bool usbc_mode;
	struct work_struct usbc_analog_work;
	struct blocking_notifier_head fsa4480_notifier;
	struct mutex notification_lock;
	int switch_control;
	int chip_id;
};

struct fsa4480_priv *g_fsa_priv = NULL;

struct fsa4480_reg_val {
	u16 reg;
	u8 val;
};

static const struct regmap_config fsa4480_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = FSA4480_RESET,
};

static const struct fsa4480_reg_val fsa_reg_i2c_defaults[] = {
	{FSA4480_SLOW_L, 0x00},
	{FSA4480_SLOW_R, 0x00},
	{FSA4480_SLOW_MIC, 0x00},
	{FSA4480_SLOW_SENSE, 0x00},
	{FSA4480_SLOW_GND, 0x00},
	{FSA4480_DELAY_L_R, 0x00},
	{FSA4480_DELAY_L_MIC, 0x00},
	{FSA4480_DELAY_L_SENSE, 0x00},
	{FSA4480_DELAY_L_AGND, 0x09},
	{FSA4480_SWITCH_SETTINGS, 0x98},
	{FSA4480_SWITCH_CONTROL, 0x18},
};

static void fsa4480_usbc_update_settings(struct fsa4480_priv *fsa_priv,
		u32 switch_control, u32 switch_enable)
{
	u32 prev_control, prev_enable;

	if (!fsa_priv->regmap) {
		dev_err(fsa_priv->dev, "%s: regmap invalid\n", __func__);
		return;
	}

	regmap_read(fsa_priv->regmap, FSA4480_SWITCH_CONTROL, &prev_control);
	regmap_read(fsa_priv->regmap, FSA4480_SWITCH_SETTINGS, &prev_enable);

	if (prev_control == switch_control && prev_enable == switch_enable) {
		dev_dbg(fsa_priv->dev, "%s: settings unchanged\n", __func__);
		return;
	}

	regmap_write(fsa_priv->regmap, FSA4480_SWITCH_SETTINGS, 0x80);
	regmap_write(fsa_priv->regmap, FSA4480_SWITCH_CONTROL, switch_control);
	/* FSA4480 chip hardware requirement */
	usleep_range(50, 55);
	regmap_write(fsa_priv->regmap, FSA4480_SWITCH_SETTINGS, switch_enable);
}

static int fsa4480_usbc_event_changed_run(struct fsa4480_priv *fsa_priv,
				      unsigned long evt, void *ptr)
{
	struct device *dev;

	dev = fsa_priv->dev;
	dev_dbg(dev, "%s: USB change event received, usbc mode %d\n",
			__func__, fsa_priv->usbc_mode);

	fsa_priv->usbc_mode = !!evt;
	pm_stay_awake(fsa_priv->dev);
	queue_work(system_freezable_wq, &fsa_priv->usbc_analog_work);

	return 0;
}

static int fsa4480_usbc_event_changed(struct notifier_block *nb_ptr,
				      unsigned long evt, void *ptr)
{
	struct fsa4480_priv *fsa_priv =
			container_of(nb_ptr, struct fsa4480_priv, nb);
	struct device *dev;

	if (!fsa_priv)
		return -EINVAL;
	dev = fsa_priv->dev;
	if (!dev)
		return -EINVAL;

    pr_info("%s: get into the function\n", __func__);
	return fsa4480_usbc_event_changed_run(fsa_priv, evt, ptr);
}

static int fsa4480_usbc_analog_setup_switches_ucsi(
						struct fsa4480_priv *fsa_priv)
{
	int rc = 0;
	struct device *dev;

	if (!fsa_priv)
		return -EINVAL;
	dev = fsa_priv->dev;
	if (!dev)
		return -EINVAL;

	mutex_lock(&fsa_priv->notification_lock);

	if (fsa_priv->usbc_mode) {
		pr_info("%s: set the audio attach\n", __func__);
		/* activate switches */
		if(fsa_priv->chip_id == FSA4480_VENDOR_NUM)
		{
			fsa4480_usbc_update_settings(fsa_priv, 0x00, 0x9F);
		}else
		{
			rc =  regmap_write(fsa_priv->regmap, AS6480_MODE_CTRL, AS6480_SWITCH_OFF);
			rc |= regmap_write(fsa_priv->regmap,AS6480_MODE_CTRL, AS6480_SWITCH_HEADSET);
		}
		/* notify call chain on event */
		blocking_notifier_call_chain(&fsa_priv->fsa4480_notifier,
					     TYPEC_ACCESSORY_AUDIO, NULL);
	} else {
		pr_info("%s: set the audio dettach\n", __func__);
		/* notify call chain on event */
		blocking_notifier_call_chain(&fsa_priv->fsa4480_notifier,
				TYPEC_ACCESSORY_NONE, NULL);

		/* deactivate switches */
		if(fsa_priv->chip_id == FSA4480_VENDOR_NUM)
		{
			fsa4480_usbc_update_settings(fsa_priv, 0x18, 0x98);
		}else
		{
			rc =  regmap_write(fsa_priv->regmap, AS6480_MODE_CTRL, AS6480_SWITCH_OFF);
			rc |= regmap_write(fsa_priv->regmap,AS6480_MODE_CTRL, AS6480_SWITCH_USB);
		}

	}
	mutex_unlock(&fsa_priv->notification_lock);

	return rc;
}

static int fsa4480_usbc_analog_setup_switches(struct fsa4480_priv *fsa_priv)
{
	return fsa4480_usbc_analog_setup_switches_ucsi(fsa_priv);
}

static int fsa4480_validate_display_port_settings(struct fsa4480_priv *fsa_priv)
{
	u32 switch_status = 0;

	regmap_read(fsa_priv->regmap, FSA4480_SWITCH_STATUS1, &switch_status);

	if ((switch_status != 0x23) && (switch_status != 0x1C)) {
		pr_err("AUX SBU1/2 switch status is invalid = %u\n",
				switch_status);
		return -EIO;
	}

	return 0;
}

int fsa4480_switch_event_enable(enum fsa_function event)
{
	int old_mode, new_mode;

	if(g_fsa_priv == NULL) {
		pr_info("%s: g_fsa_priv is null\n", __func__);
		return -EINVAL;
	}

	switch (event) {
	case FSA_MIC_GND_SWAP:
		if(g_fsa_priv->chip_id == FSA4480_VENDOR_NUM) {
			regmap_read(g_fsa_priv->regmap,
                                    FSA4480_SWITCH_CONTROL, &g_fsa_priv->switch_control);
			if ((g_fsa_priv->switch_control & 0x07) == 0x07)
				g_fsa_priv->switch_control = 0x0;
			else
				g_fsa_priv->switch_control = 0x7;
			fsa4480_usbc_update_settings(g_fsa_priv, g_fsa_priv->switch_control, 0x9F);
		} else {
			regmap_read(g_fsa_priv->regmap,AS6480_MODE_CTRL,&old_mode);
			new_mode = (old_mode == AS6480_SWITCH_HEADSET) ? AS6480_SWITCH_GND_MIC_SWAP : AS6480_SWITCH_HEADSET;
			regmap_write(g_fsa_priv->regmap,AS6480_MODE_CTRL, AS6480_SWITCH_OFF);
			regmap_write(g_fsa_priv->regmap,AS6480_MODE_CTRL, new_mode);
		}
		break;
	case FSA_USBC_ORIENTATION_CC1:
		fsa4480_usbc_update_settings(g_fsa_priv, 0x18, 0xF8);
		return fsa4480_validate_display_port_settings(g_fsa_priv);
	case FSA_USBC_ORIENTATION_CC2:
		fsa4480_usbc_update_settings(g_fsa_priv, 0x78, 0xF8);
		return fsa4480_validate_display_port_settings(g_fsa_priv);
	case FSA_USBC_DISPLAYPORT_DISCONNECTED:
		fsa4480_usbc_update_settings(g_fsa_priv, 0x18, 0x98);
		break;
	default:
		break;
	}

	return 0;
}

int typec_i2c_switch_event_enable(enum fsa_function event, bool need_delay) {
	if (!need_delay) {
		fsa4480_switch_event_enable(event);
	} else {
		/* AS3UA6485CR chip headmicbisa power on has 130ms delay, hw why? */
		if (g_fsa_priv->chip_id == FSA4480_CHIP_ID)
			msleep(130);
	}
	return 0;
}
EXPORT_SYMBOL(typec_i2c_switch_event_enable);

void typec_i2c_switch_depop(void) {
	int old_mode, new_mode;

	regmap_read(g_fsa_priv->regmap,AS6480_MODE_CTRL,&old_mode);
	new_mode = AS6480_SWITCH_HEADSET;
	regmap_write(g_fsa_priv->regmap,AS6480_MODE_CTRL, AS6480_SWITCH_OFF);
	regmap_write(g_fsa_priv->regmap,AS6480_MODE_CTRL, new_mode);
}
EXPORT_SYMBOL(typec_i2c_switch_depop);

static void fsa4480_usbc_analog_work_fn(struct work_struct *work)
{
	struct fsa4480_priv *fsa_priv =
		container_of(work, struct fsa4480_priv, usbc_analog_work);

	if (!fsa_priv) {
		pr_err("%s: fsa container invalid\n", __func__);
		return;
	}
	pr_info("%s: get into the function\n", __func__);
	fsa4480_usbc_analog_setup_switches(fsa_priv);
	pm_relax(fsa_priv->dev);
}

static void fsa4480_update_reg_defaults(struct regmap *regmap)
{
	u8 i;

	for (i = 0; i < ARRAY_SIZE(fsa_reg_i2c_defaults); i++)
		regmap_write(regmap, fsa_reg_i2c_defaults[i].reg,
				   fsa_reg_i2c_defaults[i].val);
}

static int fsa4480_probe(struct i2c_client *i2c)
{
	struct fsa4480_priv *fsa_priv;
	int rc = 0;
	int chip_id=0;

	pr_info("%s: get the function\n", __func__);
	fsa_priv = devm_kzalloc(&i2c->dev, sizeof(*fsa_priv),
				GFP_KERNEL);
	if (!fsa_priv)
		return -ENOMEM;

	memset(fsa_priv, 0, sizeof(struct fsa4480_priv));
	fsa_priv->dev = &i2c->dev;

	fsa_priv->regmap = devm_regmap_init_i2c(i2c, &fsa4480_regmap_config);
	if (IS_ERR_OR_NULL(fsa_priv->regmap)) {
		dev_err(fsa_priv->dev, "%s: Failed to initialize regmap: %d\n",
			__func__, rc);
		if (!fsa_priv->regmap) {
			rc = -EINVAL;
			goto err_data;
		}
		rc = PTR_ERR(fsa_priv->regmap);
		goto err_data;
	}
	regmap_read(fsa_priv->regmap, FSA4480_CHIP_ID ,&chip_id);
	pr_info("%s: chip_id is %d\n",__func__, chip_id);
	fsa_priv->chip_id = chip_id;
	if(chip_id == FSA4480_VENDOR_NUM) {
		pr_info("%s: chip is fsa4480\n",__func__);
	    fsa4480_update_reg_defaults(fsa_priv->regmap);
		fsa4480_usbc_update_settings(fsa_priv, 0x18, 0x98);
	}
	fsa_priv->edev = extcon_get_edev_by_phandle(fsa_priv->dev, 0);
	if (IS_ERR(fsa_priv->edev)) {
		dev_err(fsa_priv->dev,
			"typec headset failed to find gpio extcon device, ret %ld\n", PTR_ERR(fsa_priv->edev));
		return PTR_ERR(fsa_priv->edev);
	}
	pr_info("%s: get the extcon handle\n", __func__);
	fsa_priv->nb.notifier_call = fsa4480_usbc_event_changed;
	rc = extcon_register_notifier(fsa_priv->edev,
						EXTCON_JACK_HEADPHONE, &fsa_priv->nb);
	if (rc) {
		dev_err(fsa_priv->dev,
		  "%s: ucsi glink notifier registration failed: %d\n",
		  __func__, rc);
		goto err_data;
	}

	mutex_init(&fsa_priv->notification_lock);
	i2c_set_clientdata(i2c, fsa_priv);
	g_fsa_priv = fsa_priv;
	INIT_WORK(&fsa_priv->usbc_analog_work,
					fsa4480_usbc_analog_work_fn);

	if (extcon_get_state(fsa_priv->edev, EXTCON_JACK_HEADPHONE)) {
		fsa_priv->usbc_mode = true;
		queue_work(system_freezable_wq, &fsa_priv->usbc_analog_work);
	}

	fsa_priv->fsa4480_notifier.rwsem =
		(struct rw_semaphore)__RWSEM_INITIALIZER
		((fsa_priv->fsa4480_notifier).rwsem);
	fsa_priv->fsa4480_notifier.head = NULL;

	return 0;

err_data:
	devm_kfree(&i2c->dev, fsa_priv);
	return rc;
}

static void fsa4480_remove(struct i2c_client *i2c)
{
	struct fsa4480_priv *fsa_priv =
			(struct fsa4480_priv *)i2c_get_clientdata(i2c);

	if (!fsa_priv)
		return;

	extcon_unregister_notifier(fsa_priv->edev,
			EXTCON_JACK_HEADPHONE, &fsa_priv->nb);
	fsa4480_usbc_update_settings(fsa_priv, 0x18, 0x98);
	cancel_work_sync(&fsa_priv->usbc_analog_work);
	pm_relax(fsa_priv->dev);
	mutex_destroy(&fsa_priv->notification_lock);
	dev_set_drvdata(&i2c->dev, NULL);
}

static const struct of_device_id fsa4480_i2c_dt_match[] = {
	{
		.compatible = "sprd,fsa4480-i2c",
	},
	{}
};

static struct i2c_driver fsa4480_i2c_driver = {
	.driver = {
		.name = FSA4480_I2C_NAME,
		.of_match_table = fsa4480_i2c_dt_match,
	},
	.probe = fsa4480_probe,
	.remove = fsa4480_remove,
};

static int __init fsa4480_init(void)
{
	int rc;
    pr_info("%s: get the function\n", __func__);
	rc = i2c_add_driver(&fsa4480_i2c_driver);
	if (rc)
		pr_err("fsa4480: Failed to register I2C driver: %d\n", rc);

	return rc;
}
module_init(fsa4480_init);

static void __exit fsa4480_exit(void)
{
	i2c_del_driver(&fsa4480_i2c_driver);
}
module_exit(fsa4480_exit);

MODULE_DESCRIPTION("FSA4480 I2C driver");
MODULE_LICENSE("GPL v2");
