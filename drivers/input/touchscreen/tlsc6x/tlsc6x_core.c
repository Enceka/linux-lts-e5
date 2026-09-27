// SPDX-License-Identifier: GPL-2.0
/*
 * tlsc6x_core.c - minimal core for the Chipsemi tlsc6x panel driver
 *
 * See tlsc6x_core.h for why this exists.  In short: the chip driver used to be
 * glued onto a vendor framework that also carried ~25 other panel drivers, a
 * proc/sysfs forest, an algorithm layer and a platform device that this board's
 * device tree does not even have (which is why the driver never probed).
 *
 * What is left here is the part the driver actually calls: a small state
 * container, chip-id reporting, LCD on/off plumbing and the touch-report
 * fallback.  Touch coordinates are reported by the driver itself.
 */
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "tlsc6x_core.h"

/* Provided by tlsc6x_main.c */
extern int tlsc6x_init(void);
extern void tlsc6x_exit(void);

struct tlsc6x_core *tlsc6x_cdev;
EXPORT_SYMBOL_GPL(tlsc6x_cdev);

/* ------------------------------------------------------------------------ */
/* state / chip id							    */
/* ------------------------------------------------------------------------ */

int get_tp_chip_id(void)
{
	/*
	 * The framework derived this from the "lcd_name" kernel parameter
	 * against a table of panel/chip pairs.  On this board the panel is
	 * always the tlsc6x, so report it directly.
	 */
	if (!tlsc6x_cdev)
		return -EIO;

	tlsc6x_cdev->tp_chip_id = TS_CHIP_TLSC;
	return 0;
}
EXPORT_SYMBOL_GPL(get_tp_chip_id);

void change_tp_state(lcdchange lcd_change)
{
	struct tlsc6x_core *cdev = tlsc6x_cdev;

	if (!cdev)
		return;

	mutex_lock(&cdev->tp_resume_mutex);
	switch (lcd_change) {
	case ENTER_LP:
	case LCD_OFF:
		cdev->tp_suspend = true;
		if (cdev->tp_suspend_func)
			cdev->tp_suspend_func(cdev->tp_data);
		break;
	case EXIT_LP:
	case LCD_ON:
		if (cdev->tp_suspend) {
			cdev->tp_suspend = false;
			if (cdev->tp_resume_func)
				cdev->tp_resume_func(cdev->tp_data);
		}
		break;
	default:
		UFP_ERR("ignore invalid lcd change %d\n", lcd_change);
		break;
	}
	mutex_unlock(&cdev->tp_resume_mutex);
}
EXPORT_SYMBOL_GPL(change_tp_state);

void change_psensor_state(psensor_state state)
{
	struct tlsc6x_core *cdev = tlsc6x_cdev;

	if (!cdev)
		return;

	switch (state) {
	case DISABLE_PSENSOR:
		cdev->is_psensor_enable = false;
		break;
	case ENABLE_PSENSOR:
		cdev->is_psensor_enable = true;
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(change_psensor_state);

/* ------------------------------------------------------------------------ */
/* logging / diagnostics: nothing to record, the driver logs by itself	    */
/* ------------------------------------------------------------------------ */

void tpd_zlog_record_notify(tp_error_no error_no)
{
	TPD_DMESG("tp error %d\n", error_no);
}
EXPORT_SYMBOL_GPL(tpd_zlog_record_notify);

int get_tp_consum_time(unsigned long jiffies_time)
{
	return jiffies_to_msecs(jiffies - jiffies_time);
}
EXPORT_SYMBOL_GPL(get_tp_consum_time);

int set_gpio_mode(u8 mode)
{
	return 0;
}
EXPORT_SYMBOL_GPL(set_gpio_mode);

/* ------------------------------------------------------------------------ */
/* touch report fallback							    */
/* ------------------------------------------------------------------------ */

/*
 * Only used when the driver is built with its "report through the framework's
 * algorithm" path enabled - and even then only if cdev->zte_tp_algo is set,
 * which we never do.  Kept as a plain multi-touch report so the driver works
 * either way.
 */
void tpd_touch_press(struct input_dev *input, u16 x, u16 y, u16 id,
		     u8 touch_major, u8 pressure)
{
	struct tlsc6x_core *cdev = tlsc6x_cdev;

	if (!input)
		return;

	if (cdev && !cdev->input)
		cdev->input = input;

	input_mt_slot(input, id);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
	input_report_abs(input, ABS_MT_POSITION_X, x);
	input_report_abs(input, ABS_MT_POSITION_Y, y);
	input_report_abs(input, ABS_MT_TOUCH_MAJOR, touch_major);
	if (pressure)
		input_report_abs(input, ABS_MT_PRESSURE, pressure);
	input_report_key(input, BTN_TOUCH, 1);
	input_sync(input);
}
EXPORT_SYMBOL_GPL(tpd_touch_press);

void tpd_touch_release(struct input_dev *input, u16 id)
{
	if (!input)
		return;

	input_mt_slot(input, id);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, false);
	input_report_key(input, BTN_TOUCH, 0);
	input_sync(input);
}
EXPORT_SYMBOL_GPL(tpd_touch_release);

void tpd_clean_all_event(void)
{
	struct tlsc6x_core *cdev = tlsc6x_cdev;
	int i;

	if (!cdev || !cdev->input)
		return;

	for (i = 0; i < 10; i++) {
		input_mt_slot(cdev->input, i);
		input_mt_report_slot_state(cdev->input, MT_TOOL_FINGER, false);
	}
	input_report_key(cdev->input, BTN_TOUCH, 0);
	input_sync(cdev->input);
}
EXPORT_SYMBOL_GPL(tpd_clean_all_event);

/* ------------------------------------------------------------------------ */
/* firmware buffer (only touched when the driver runs an upgrade)	    */
/* ------------------------------------------------------------------------ */

int tp_alloc_tp_firmware_data(int buf_size)
{
	struct tlsc6x_core *cdev = tlsc6x_cdev;

	if (!cdev || buf_size <= 0)
		return -EINVAL;

	tp_free_tp_firmware_data();

	cdev->fw_data = kzalloc(buf_size, GFP_KERNEL);
	if (!cdev->fw_data)
		return -ENOMEM;

	cdev->fw_size = buf_size;
	cdev->fw_data_pos = 0;
	return 0;
}
EXPORT_SYMBOL_GPL(tp_alloc_tp_firmware_data);

void tp_free_tp_firmware_data(void)
{
	struct tlsc6x_core *cdev = tlsc6x_cdev;

	if (!cdev)
		return;

	kfree(cdev->fw_data);
	cdev->fw_data = NULL;
	cdev->fw_size = 0;
	cdev->fw_data_pos = 0;
}
EXPORT_SYMBOL_GPL(tp_free_tp_firmware_data);

void tpd_reset_fw_data_pos_and_size(void)
{
	struct tlsc6x_core *cdev = tlsc6x_cdev;

	if (!cdev)
		return;

	cdev->fw_data_pos = 0;
	kfree(cdev->fw_data);
	cdev->fw_data = NULL;
	cdev->fw_size = 0;
}
EXPORT_SYMBOL_GPL(tpd_reset_fw_data_pos_and_size);

int tpd_copy_to_tp_firmware_data(char *buf)
{
	struct tlsc6x_core *cdev = tlsc6x_cdev;
	int len;

	if (!cdev || !cdev->fw_data)
		return -ENOMEM;

	if (cdev->fw_size == 0)
		return -EINVAL;

	if (cdev->fw_data_pos >= cdev->fw_size)
		return 0;

	len = strlen(buf);
	if (cdev->fw_data_pos + len > cdev->fw_size)
		len = cdev->fw_size - cdev->fw_data_pos;

	memcpy(&cdev->fw_data[cdev->fw_data_pos], buf, len);
	cdev->fw_data_pos += len;
	return len;
}
EXPORT_SYMBOL_GPL(tpd_copy_to_tp_firmware_data);

/* ------------------------------------------------------------------------ */
/* module plumbing							    */
/* ------------------------------------------------------------------------ */

static int __init tlsc6x_core_init(void)
{
	struct tlsc6x_core *cdev;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	mutex_init(&cdev->zlog_mutex);
	mutex_init(&cdev->cmd_mutex);
	mutex_init(&cdev->report_mutex);
	mutex_init(&cdev->report_down_mutex);
	mutex_init(&cdev->tp_resume_mutex);
	init_completion(&cdev->bbat_test_completion);
	init_completion(&cdev->pm_completion);

	cdev->tp_chip_id = TS_CHIP_TLSC;
	cdev->max_x = 320;		/* panel is 320x480, DT values win */
	cdev->max_y = 480;

	/* the chip driver queues its psensor report check on this one */
	cdev->tpd_report_wq = alloc_ordered_workqueue("tlsc6x_report", 0);
	if (!cdev->tpd_report_wq) {
		kfree(cdev);
		return -ENOMEM;
	}

	tlsc6x_cdev = cdev;

	/* registers the i2c driver: the panel node is i2c-3 @0x2e */
	return tlsc6x_init();
}

static void __exit tlsc6x_core_exit(void)
{
	tlsc6x_exit();

	if (tlsc6x_cdev) {
		if (tlsc6x_cdev->tpd_report_wq) {
			cancel_delayed_work_sync(
				&tlsc6x_cdev->psensor_report_check_work);
			destroy_workqueue(tlsc6x_cdev->tpd_report_wq);
		}
		tp_free_tp_firmware_data();
		kfree(tlsc6x_cdev);
		tlsc6x_cdev = NULL;
	}
}

module_init(tlsc6x_core_init);
module_exit(tlsc6x_core_exit);

MODULE_AUTHOR("Chipsemi");
MODULE_DESCRIPTION("Chipsemi tlsc6x touchscreen (Rongyue E5)");
MODULE_LICENSE("GPL");
