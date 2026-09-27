/*
 * audio/sprd/codec/dummy-codec/dummy-codec.c
 *
 * DUMMY-CODEC -- SpreadTrum just for codec code.
 *
 * SPDX-FileCopyrightText: 2015 Spreadtrum Communications (Shanghai) Co., Ltd
 * SPDX-FileCopyrightText: 2016 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "sprd-asoc-debug.h"
#define pr_fmt(fmt) pr_sprd_fmt("DMYCD")""fmt

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#include <sound/core.h>
#include <sound/soc.h>

#include "sprd-asoc-common.h"

#define STUB_RATES	SNDRV_PCM_RATE_8000_192000
#define STUB_FORMATS	(SNDRV_PCM_FMTBIT_S8 | \
			SNDRV_PCM_FMTBIT_U8 | \
			SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_U16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_U24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE | \
			SNDRV_PCM_FMTBIT_U32_LE | \
			SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE)

struct snd_soc_dai_driver dummy_playback_dai[] = {
	{
	 .name = "sprd-dummy-playback-dai",
	 .playback = {
		      .stream_name = "Playback",
		      .channels_min = 1,
		      .channels_max = 384,
		      .rates = STUB_RATES,
		      .formats = STUB_FORMATS,
		      },
	},
};

static struct snd_soc_component_driver dummy_playback_codec;

static int sprd_dummy_playback_codec_probe(struct platform_device *pdev)
{
	int ret = 0;

	ret = snd_soc_register_component(&pdev->dev, &dummy_playback_codec,
				     dummy_playback_dai, 1);
	if (ret < 0)
		return ret;

	return ret;
}

static void sprd_dummy_playback_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
}

static struct platform_driver sprd_dummy_driver = {
	.driver = {
		.name = "sprd-dummy-playback",
	},
	.probe = sprd_dummy_playback_codec_probe,
	.remove = sprd_dummy_playback_codec_remove,
};

static struct platform_device *sprd_dummy_dev;

int __init sprd_dummy_playback_codec_init(void)
{
	int ret;

	sprd_dummy_dev = platform_device_register_simple("sprd-dummy-playback",
							 -1, NULL, 0);
	if (IS_ERR(sprd_dummy_dev))
		return PTR_ERR(sprd_dummy_dev);

	ret = platform_driver_register(&sprd_dummy_driver);
	if (ret != 0)
		platform_device_unregister(sprd_dummy_dev);

	return ret;
}

void __exit sprd_dummy_playback_codec_exit(void)
{
	platform_device_unregister(sprd_dummy_dev);
	platform_driver_unregister(&sprd_dummy_driver);
}

module_init(sprd_dummy_playback_codec_init);
module_exit(sprd_dummy_playback_codec_exit);

MODULE_DESCRIPTION("Dummy Playback ALSA SoC Codec Driver");
MODULE_AUTHOR("Peng Lee <peng.lee@spreadtrum.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("codec:dummy-playback");
