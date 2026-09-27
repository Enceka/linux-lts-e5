/* SPDX-License-Identifier: GPL-2.0 */
/*
 * tlsc6x_core.h - minimal core for the Chipsemi tlsc6x panel driver
 *
 * The driver (tlsc6x_main.c / tlsc6x_comp.c / tlsc6x_common_interface.c) was
 * written against a vendor touchscreen framework.  It only ever used a small
 * part of it: an input device it allocates itself, a little state, a handful
 * of helpers.  This header plus tlsc6x_core.c provide exactly that, so the
 * driver works standalone - which is how the stock image ships it (tlsc6x.ko
 * with no vendor framework symbols in it at all).
 *
 * The names the driver already uses are kept (tpd_cdev, struct ztp_device,
 * tpd_* helpers) through the aliases below; everything is implemented here.
 */
#ifndef __TLSC6X_CORE_H_
#define __TLSC6X_CORE_H_

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/* ------------------------------------------------------------------ log ---*/
#define LAST_LOG_BUFF_SIZE	16
#define LAST_LOG_BUFF_LEN	100
#define ZLOG_INFO_LEN		(2 * 1024)

/* The driver's tlsc_info()/tlsc_err()/tlsc_dbg() macros end up here. */
#define TPD_DMESG(fmt, arg...)	pr_info("[tlsc6x] " fmt, ##arg)
#define TPD_ZLOG(fmt, arg...)	pr_info("[tlsc6x][zlog] " fmt, ##arg)
#define TPD_DBG(fmt, arg...)						\
do {									\
	if (tpd_cdev->debug_log_enable)					\
		pr_info("[tlsc6x][dbg] " fmt, ##arg);			\
} while (0)
#define UFP_INFO(fmt, arg...)	pr_info("[tlsc6x] " fmt, ##arg)
#define UFP_ERR(fmt, arg...)	pr_err("[tlsc6x] " fmt, ##arg)

#define tpd_save_last_log(fmt, args...) do {				\
	mutex_lock(&tpd_cdev->zlog_mutex);				\
	memset(tpd_cdev->last_log_buffer[tpd_cdev->pos], 0,		\
	       LAST_LOG_BUFF_LEN);					\
	snprintf(tpd_cdev->last_log_buffer[tpd_cdev->pos],		\
		 LAST_LOG_BUFF_LEN, fmt, ##args);			\
	tpd_cdev->pos++;						\
	tpd_cdev->pos &= LAST_LOG_BUFF_SIZE - 1;			\
	mutex_unlock(&tpd_cdev->zlog_mutex);				\
} while (0)

/* -------------------------------------------------------------- constants -*/
#define MAX_VENDOR_NAME_LEN	40
#define MAX_LCD_NAME_LEN	128
#define MAX_LIMIT_NUM		4
#define KB			(1024)
#define MB			(KB * KB)
#define RT_DATA_LEN		(10 * KB)
#define TP_TEST_FILE_SIZE	(1 * MB)
#define BLANK			1
#define UNBLANK			0

#define TP_RAWDATA_TEST_FAIL		0x0001
#define TP_OPEN_TEST_FAIL		0x0200
#define TP_SHORT_TEST_FAIL		0x0400
#define TP_NOISE_TEST_FAIL		0x4000
#define TP_CB_TEST_FAIL			0x8000
#define TP_PANEL_DIFF_TEST_FAIL		0x10000
#define TP_COMP_CAP_TEST_FAIL		0x20000
#define TP_OTHER_TEST_FAIL		0x40000
#define TP_INT_BAAT_TEST_FAIL		0x01
#define TP_RST_BAAT_TEST_FAIL		0x02

/* ----------------------------------------------------------------- enums --*/
typedef enum tp_error_no {
	TP_I2C_R_ERROR_NO = 1,
	TP_I2C_W_ERROR_NO,
	TP_SPI_R_ERROR_NO,
	TP_SPI_W_ERROR_NO,
	TP_CRC_ERROR_NO,
	TP_FW_UPGRADE_ERROR_NO,
	TP_ESD_CHECK_ERROR_NO,
	TP_PROBE_ERROR_NO,
	TP_SUSPEND_GESTURE_OPEN_NO,
	TP_REQUEST_FIRMWARE_ERROR_NO,
	TP_GHOST_ERROR_NO,
	TP_SELF_TEST_ERROR_NO,
	TP_GET_NOISE_ERROR_NO,
	TP_ERROR_NO_MAX,
} tp_error_no;

struct tp_error_item {
	unsigned long count[TP_ERROR_NO_MAX];
	unsigned long timer[TP_ERROR_NO_MAX];
};

enum ts_chip {
	TS_CHIP_INDETER		= 0x00,
	TS_CHIP_SYNAPTICS	= 0x01,
	TS_CHIP_ATMEL		= 0x02,
	TS_CHIP_CYTTSP		= 0x03,
	TS_CHIP_FOCAL		= 0x04,
	TS_CHIP_GOODIX		= 0x05,
	TS_CHIP_MELFAS		= 0x06,
	TS_CHIP_MSTAR		= 0x07,
	TS_CHIP_HIMAX		= 0x08,
	TS_CHIP_NOVATEK		= 0x09,
	TS_CHIP_ILITEK		= 0x0A,
	TS_CHIP_TLSC		= 0x0B,
	TS_CHIP_CHIPONE		= 0x0C,
	TS_CHIP_HYNITRON	= 0x0D,
	TS_CHIP_GTX8		= 0x0E,
	TS_CHIP_GT9897		= 0x0E,
	TS_CHIP_GCORE		= 0x0F,
	TS_CHIP_SITRONIX	= 0x10,
	TS_CHIP_OMNIVISION	= 0x11,
	TS_CHIP_BTL		= 0x12,
	TS_CHIP_SEMI		= 0x13,
	TS_CHIP_ASX		= 0x14,
	TS_CHIP_MAX		= 0xFF,
};

enum tp_test_type {
	RAWDATA_TEST = 0,
	DELTA_TEST = 1,
	BASE_TEST = 2,
};

typedef enum lcdstate {
	SCREEN_ON = 0,
	SCREEN_OFF,
	DOZE,
} lcdstate;

typedef enum lcdchange {
	EXIT_LP = 0,
	ENTER_LP,
	LCD_ON,
	LCD_OFF,
} lcdchange;

typedef enum psensor_state {
	DISABLE_PSENSOR = 0,
	ENABLE_PSENSOR,
	PSENSOR_BEGIN_SUSPEND,
	PSENSOR_BEGIN_RESUME,
} psensor_state;

/* ----------------------------------------------------------------- types --*/
struct tpvendor_t {
	int vendor_id;
	char *vendor_name;
};

struct tpd_tpinfo_t {
	unsigned int chip_model_id;
	unsigned int chip_part_id;
	unsigned int chip_ver;
	unsigned int module_id;
	unsigned int firmware_ver;
	unsigned int config_ver;
	unsigned int display_ver;
	unsigned int i2c_addr;
	unsigned int i2c_type;
	unsigned int spi_num;
	char tp_name[MAX_VENDOR_NAME_LEN];
	char vendor_name[MAX_VENDOR_NAME_LEN];
	char chip_batch[MAX_VENDOR_NAME_LEN];
};

struct ts_firmware {
	u8 *data;
	int size;
};

/*
 * Everything the chip driver touches.  Function pointers are filled in by the
 * driver during probe; the helpers in tlsc6x_core.c call them.
 */
struct tlsc6x_core {
	u16 max_x;
	u16 max_y;
	u8 tp_chip_id;
	bool TP_have_registered;
	bool debug_log_enable;
	bool tp_suspend;
	bool need_tp_resume;
	bool is_psensor_enable;
	bool bbat_test_enter;
	bool bbat_int_test;
	u8 bbat_test_result;
	u8 probe_fail_chip_id;
	u32 fw_data_pos;

	struct input_dev *input;
	struct workqueue_struct *tpd_wq;
	struct workqueue_struct *tpd_report_wq;
	struct delayed_work psensor_report_check_work;
	struct delayed_work point_report_check_work;
	struct completion bbat_test_completion;
	struct completion pm_completion;
	struct tpd_tpinfo_t ic_tpinfo;
	u8 *fw_data;		/* firmware staging buffer, only used on upgrade */
	int fw_size;

	struct mutex zlog_mutex;
	struct mutex cmd_mutex;
	struct mutex report_mutex;
	struct mutex report_down_mutex;
	struct mutex tp_resume_mutex;
	u16 pos;
	char last_log_buffer[LAST_LOG_BUFF_SIZE][LAST_LOG_BUFF_LEN];

	void *tp_data;
	unsigned long tp_reset_timer;
	struct device *dev;

	/* filled in by the chip driver */
	int (*tp_resume_func)(void *data);
	int (*tp_suspend_func)(void *data);
	int (*tp_fw_upgrade)(struct tlsc6x_core *cdev, char *fwname, int len);
	int (*get_tpinfo)(struct tlsc6x_core *cdev);
	int (*tp_suspend_show)(struct tlsc6x_core *cdev);
	int (*set_tp_suspend)(struct tlsc6x_core *cdev, u8 node, int enable);
	int (*tpd_shutdown)(struct tlsc6x_core *cdev);
	int (*get_tp_self_test_result)(struct tlsc6x_core *cdev, char *buf);
	int (*tp_self_test)(struct tlsc6x_core *cdev);
	int (*tp_psensor_report_check)(struct tlsc6x_core *cdev);
	int (*tp_bbat_test)(struct tlsc6x_core *cdev);
};

/* Aliases: the chip driver spells these the framework's way. */
#define ztp_device	tlsc6x_core
extern struct tlsc6x_core *tlsc6x_cdev;
#define tpd_cdev	tlsc6x_cdev

/* ------------------------------------------------------------ helpers -----*/
int get_tp_chip_id(void);
void change_tp_state(lcdchange lcd_change);
void change_psensor_state(psensor_state state);
void tpd_zlog_record_notify(tp_error_no error_no);
void tpd_touch_press(struct input_dev *input, u16 x, u16 y, u16 id,
		     u8 touch_major, u8 pressure);
void tpd_touch_release(struct input_dev *input, u16 id);
void tpd_clean_all_event(void);
int tpd_copy_to_tp_firmware_data(char *buf);
int tp_alloc_tp_firmware_data(int buf_size);
void tp_free_tp_firmware_data(void);
void tpd_reset_fw_data_pos_and_size(void);
int get_tp_consum_time(unsigned long jiffies_time);
int set_gpio_mode(u8 mode);

#endif /* __TLSC6X_CORE_H_ */
