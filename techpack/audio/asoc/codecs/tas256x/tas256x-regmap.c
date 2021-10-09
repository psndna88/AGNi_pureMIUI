/*
 * ALSA SoC Texas Instruments TAS256X High Performance 4W Smart Amplifier
 *
 * Copyright (C) 2016 Texas Instruments, Inc.
 * Copyright (C) 2021 XiaoMi, Inc.
 *
 * Author: saiprasad
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#ifdef CONFIG_TAS256X_REGMAP

#define DEBUG 5
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/firmware.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/version.h>

#include "tas256x.h"
#include "tas2562.h"
#include "tas2564.h"
#include "tas256x-device.h"
#include "tas256x-codec.h"

#ifdef CONFIG_TAS25XX_ALGO
#ifdef CONFIG_PLATFORM_EXYNOS
#include <sound/smart_amp.h>
#else
#include <dsp/tas_smart_amp_v2.h>
#endif
static dc_detection_data_t s_dc_detect;
#endif /*CONFIG_TAS25XX_ALGO*/

/*For mixer_control implementation*/
#define MAX_STRING	200

static const char *dts_tag[][3] = {
	{
		"ti,left-channel",
		"ti,reset-gpio",
		"ti,irq-gpio"
	},
	{
		"ti,right-channel",
		"ti,reset-gpio2",
		"ti,irq-gpio2"
	}
};

static const char *reset_gpio_label[2] = {
	"TAS256X_RESET", "TAS256X_RESET2"
};

static const char *irq_gpio_label[2] = {
	"TAS256X-IRQ", "TAS256X-IRQ2"
};

static int tas256x_regmap_write(struct tas256x_priv *p_tas256x,
	unsigned int reg, unsigned int value)
{
	int nResult = 0;
	int retry_count = TAS256X_I2C_RETRY_COUNT;

	if (p_tas256x->i2c_suspend)
		return ERROR_I2C_SUSPEND;

	while (retry_count--) {
		nResult = regmap_write(p_tas256x->regmap, reg,
			value);
		if (nResult >= 0)
			break;
		msleep(20);
	}
	if (retry_count == -1)
		return ERROR_I2C_FAILED;
	else
		return 0;
}

static int tas256x_regmap_bulk_write(struct tas256x_priv *p_tas256x,
	unsigned int reg, unsigned char *pData, unsigned int nLength)
{
	int nResult = 0;
	int retry_count = TAS256X_I2C_RETRY_COUNT;

	if (p_tas256x->i2c_suspend)
		return ERROR_I2C_SUSPEND;

	while (retry_count--) {
		nResult = regmap_bulk_write(p_tas256x->regmap, reg,
			 pData, nLength);
		if (nResult >= 0)
			break;
		msleep(20);
	}
	if (retry_count == -1)
		return ERROR_I2C_FAILED;
	else
		return 0;
}

static int tas256x_regmap_read(struct tas256x_priv *p_tas256x,
	unsigned int reg, unsigned int *value)
{
	int nResult = 0;
	int retry_count = TAS256X_I2C_RETRY_COUNT;

	if (p_tas256x->i2c_suspend)
		return ERROR_I2C_SUSPEND;

	while (retry_count--) {
		nResult = regmap_read(p_tas256x->regmap, reg,
			value);
		if (nResult >= 0)
			break;
		msleep(20);
	}
	if (retry_count == -1)
		return ERROR_I2C_FAILED;
	else
		return 0;
}

static int tas256x_regmap_bulk_read(struct tas256x_priv *p_tas256x,
	unsigned int reg, unsigned char *pData, unsigned int nLength)
{
	int nResult = 0;
	int retry_count = TAS256X_I2C_RETRY_COUNT;

	if (p_tas256x->i2c_suspend)
		return ERROR_I2C_SUSPEND;

	while (retry_count--) {
		nResult = regmap_bulk_read(p_tas256x->regmap, reg,
			 pData, nLength);
		if (nResult >= 0)
			break;
		msleep(20);
	}
	if (retry_count == -1)
		return ERROR_I2C_FAILED;
	else
		return 0;
}

static int tas256x_regmap_update_bits(struct tas256x_priv *p_tas256x,
	unsigned int reg, unsigned int mask, unsigned int value)
{
	int nResult = 0;
	int retry_count = TAS256X_I2C_RETRY_COUNT;

	if (p_tas256x->i2c_suspend)
		return ERROR_I2C_SUSPEND;

	while (retry_count--) {
		nResult = regmap_update_bits(p_tas256x->regmap, reg,
			mask, value);
		if (nResult >= 0)
			break;
		msleep(20);
	}
	if (retry_count == -1)
		return ERROR_I2C_FAILED;
	else
		return 0;
}

static int tas256x_change_book_page(struct tas256x_priv *p_tas256x,
	enum channel chn,
	int book, int page)
{
	int n_result = 0, rc = 0;
	int i = 0;

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (((chn&channel_left) && (i == 0))
			|| ((chn&channel_right) && (i == 1))) {
			p_tas256x->client->addr = p_tas256x->devs[i]->mn_addr;
			if (p_tas256x->devs[i]->mn_current_book != book) {
				n_result = tas256x_regmap_write(p_tas256x,
					TAS256X_BOOKCTL_PAGE, 0);
				if (n_result < 0) {
					dev_err(p_tas256x->dev,
						"%s, ERROR, L=%d, E=%d\n",
						__func__, __LINE__, n_result);
					rc |= n_result;
					continue;
				}
				p_tas256x->devs[i]->mn_current_page = 0;
				n_result = tas256x_regmap_write(p_tas256x,
					TAS256X_BOOKCTL_REG, book);
				if (n_result < 0) {
					dev_err(p_tas256x->dev,
						"%s, ERROR, L=%d, E=%d\n",
						__func__, __LINE__, n_result);
					rc |= n_result;
					continue;
				}
				p_tas256x->devs[i]->mn_current_book = book;
			}

			if (p_tas256x->devs[i]->mn_current_page != page) {
				n_result = tas256x_regmap_write(p_tas256x,
					TAS256X_BOOKCTL_PAGE, page);
				if (n_result < 0) {
					dev_err(p_tas256x->dev,
						"%s, ERROR, L=%d, E=%d\n",
						__func__, __LINE__, n_result);
					rc |= n_result;
					continue;
				}
				p_tas256x->devs[i]->mn_current_page = page;
			}
		}
	}
	if (rc < 0) {
		if (chn&channel_left)
			p_tas256x->mn_err_code |= ERROR_DEVA_I2C_COMM;
		if (chn&channel_right)
			p_tas256x->mn_err_code |= ERROR_DEVB_I2C_COMM;
	} else {
		if (chn&channel_left)
			p_tas256x->mn_err_code &= ~ERROR_DEVA_I2C_COMM;
		if (chn&channel_right)
			p_tas256x->mn_err_code &= ~ERROR_DEVB_I2C_COMM;
	}

	return rc;
}

static int tas256x_dev_read(struct tas256x_priv *p_tas256x,
	enum channel chn,
	unsigned int reg, unsigned int *pValue)
{
	int n_result = 0;

	mutex_lock(&p_tas256x->dev_lock);

	n_result = tas256x_change_book_page(p_tas256x, chn,
		TAS256X_BOOK_ID(reg), TAS256X_PAGE_ID(reg));
	if (n_result < 0)
		goto end;

	/*Force left incase of mono*/
	if ((chn == channel_right) && (p_tas256x->mn_channels == 1))
		chn = channel_left;

	if ((chn == channel_left) || (p_tas256x->mn_channels == 1))
		p_tas256x->client->addr = p_tas256x->devs[chn>>1]->mn_addr;
	else if (chn == channel_right)
		p_tas256x->client->addr = p_tas256x->devs[chn>>1]->mn_addr;
	else
		dev_err(p_tas256x->dev, "%s, wrong channel number\n",
			__func__);

	n_result = tas256x_regmap_read(p_tas256x,
		TAS256X_PAGE_REG(reg), pValue);
	if (n_result < 0) {
		dev_err(p_tas256x->dev, "%s, ERROR, L=%d, E=%d\n",
			__func__, __LINE__, n_result);
		if (chn&channel_left)
			p_tas256x->mn_err_code |= ERROR_DEVA_I2C_COMM;
		if (chn&channel_right)
			p_tas256x->mn_err_code |= ERROR_DEVB_I2C_COMM;
	} else {
		dev_dbg(p_tas256x->dev,
			"%s: chn:%x:BOOK:PAGE:REG 0x%02x:0x%02x:0x%02x,0x%02x\n",
			__func__,
			p_tas256x->client->addr, TAS256X_BOOK_ID(reg),
			TAS256X_PAGE_ID(reg),
			TAS256X_PAGE_REG(reg), *pValue);
		if (chn&channel_left)
			p_tas256x->mn_err_code &= ~ERROR_DEVA_I2C_COMM;
		if (chn&channel_right)
			p_tas256x->mn_err_code &= ~ERROR_DEVB_I2C_COMM;
	}
end:
	mutex_unlock(&p_tas256x->dev_lock);
	return n_result;
}

static int tas256x_dev_write(struct tas256x_priv *p_tas256x, enum channel chn,
	unsigned int reg, unsigned int value)
{
	int n_result = 0, rc = 0;
	int i = 0;

	mutex_lock(&p_tas256x->dev_lock);

	n_result = tas256x_change_book_page(p_tas256x, chn,
		TAS256X_BOOK_ID(reg), TAS256X_PAGE_ID(reg));
	if (n_result < 0)
		goto end;

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (((chn&channel_left) && (i == 0))
			|| ((chn&channel_right) && (i == 1))) {
			p_tas256x->client->addr = p_tas256x->devs[i]->mn_addr;

			n_result = tas256x_regmap_write(p_tas256x,
				TAS256X_PAGE_REG(reg), value);
			if (n_result < 0) {
				dev_err(p_tas256x->dev,
					"%s, ERROR, L=%u, chn=0x%02x, E=%d\n",
					__func__, __LINE__,
					p_tas256x->client->addr, n_result);
				rc |= n_result;
				if (chn&channel_left)
					p_tas256x->mn_err_code |= ERROR_DEVA_I2C_COMM;
				if (chn&channel_right)
					p_tas256x->mn_err_code |= ERROR_DEVB_I2C_COMM;
			} else {
				dev_dbg(p_tas256x->dev,
					"%s: %u: chn:0x%02x:BOOK:PAGE:REG 0x%02x:0x%02x:0x%02x, VAL: 0x%02x\n",
					__func__, __LINE__,
					p_tas256x->client->addr,
					TAS256X_BOOK_ID(reg),
					TAS256X_PAGE_ID(reg),
					TAS256X_PAGE_REG(reg), value);
				if (chn&channel_left)
					p_tas256x->mn_err_code &= ~ERROR_DEVA_I2C_COMM;
				if (chn&channel_right)
					p_tas256x->mn_err_code &= ~ERROR_DEVB_I2C_COMM;
			}
		}
	}
end:
	mutex_unlock(&p_tas256x->dev_lock);
	return rc;
}

static int tas256x_dev_bulk_write(struct tas256x_priv *p_tas256x,
	enum channel chn,
	unsigned int reg, unsigned char *p_data, unsigned int n_length)
{
	int n_result = 0, rc = 0;
	int i = 0;

	mutex_lock(&p_tas256x->dev_lock);

	n_result = tas256x_change_book_page(p_tas256x, chn,
		TAS256X_BOOK_ID(reg), TAS256X_PAGE_ID(reg));
	if (n_result < 0)
		goto end;

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (((chn&channel_left) && (i == 0))
			|| ((chn&channel_right) && (i == 1))) {
			p_tas256x->client->addr = p_tas256x->devs[i]->mn_addr;
			n_result = tas256x_regmap_bulk_write(p_tas256x,
				TAS256X_PAGE_REG(reg), p_data, n_length);
			if (n_result < 0) {
				dev_err(p_tas256x->dev,
					"%s, ERROR, L=%u, chn=0x%02x: E=%d\n",
					__func__, __LINE__,
					p_tas256x->client->addr, n_result);
				rc |= n_result;
				if (chn&channel_left)
					p_tas256x->mn_err_code |= ERROR_DEVA_I2C_COMM;
				if (chn&channel_right)
					p_tas256x->mn_err_code |= ERROR_DEVB_I2C_COMM;
			} else {
				dev_dbg(p_tas256x->dev,
					"%s: chn%x:BOOK:PAGE:REG 0x%02x:0x%02x:0x%02x, len: %u\n",
					__func__, p_tas256x->client->addr,
					TAS256X_BOOK_ID(reg), TAS256X_PAGE_ID(reg),
					TAS256X_PAGE_REG(reg), n_length);
				if (chn&channel_left)
					p_tas256x->mn_err_code &= ~ERROR_DEVA_I2C_COMM;
				if (chn&channel_right)
					p_tas256x->mn_err_code &= ~ERROR_DEVB_I2C_COMM;
			}
		}
	}

end:
	mutex_unlock(&p_tas256x->dev_lock);
	return rc;
}

static int tas256x_dev_bulk_read(struct tas256x_priv *p_tas256x,
	enum channel chn,
	unsigned int reg, unsigned char *p_data, unsigned int n_length)
{
	int n_result = 0;

	mutex_lock(&p_tas256x->dev_lock);

	if ((chn == channel_left) || (p_tas256x->mn_channels == 1))
		p_tas256x->client->addr = p_tas256x->devs[chn>>1]->mn_addr;
	else if (chn == channel_right)
		p_tas256x->client->addr = p_tas256x->devs[chn>>1]->mn_addr;
	else
		dev_err(p_tas256x->dev, "%s, wrong channel number\n", __func__);

	n_result = tas256x_change_book_page(p_tas256x, chn,
		TAS256X_BOOK_ID(reg), TAS256X_PAGE_ID(reg));
	if (n_result < 0)
		goto end;

	n_result = tas256x_regmap_bulk_read(p_tas256x,
	TAS256X_PAGE_REG(reg), p_data, n_length);
	if (n_result < 0) {
		dev_err(p_tas256x->dev, "%s, ERROR, L=%d, E=%d\n",
			__func__, __LINE__, n_result);
		if (chn&channel_left)
			p_tas256x->mn_err_code |= ERROR_DEVA_I2C_COMM;
		if (chn&channel_right)
			p_tas256x->mn_err_code |= ERROR_DEVB_I2C_COMM;
	} else {
		dev_dbg(p_tas256x->dev,
			"%s: chn%x:BOOK:PAGE:REG %u:%u:%u, len: 0x%02x\n",
			__func__, p_tas256x->client->addr,
			TAS256X_BOOK_ID(reg), TAS256X_PAGE_ID(reg),
			TAS256X_PAGE_REG(reg), n_length);
		if (chn&channel_left)
			p_tas256x->mn_err_code &= ~ERROR_DEVA_I2C_COMM;
		if (chn&channel_right)
			p_tas256x->mn_err_code &= ~ERROR_DEVB_I2C_COMM;
	}
end:
	mutex_unlock(&p_tas256x->dev_lock);
	return n_result;
}

static int tas256x_dev_update_bits(struct tas256x_priv *p_tas256x,
	enum channel chn,
	unsigned int reg, unsigned int mask, unsigned int value)
{
	int n_result = 0, rc = 0;
	int i = 0;

	mutex_lock(&p_tas256x->dev_lock);
	n_result = tas256x_change_book_page(p_tas256x, chn,
		TAS256X_BOOK_ID(reg), TAS256X_PAGE_ID(reg));
	if (n_result < 0) {
		rc = n_result;
		goto end;
	}

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (((chn&channel_left) && (i == 0))
			|| ((chn&channel_right) && (i == 1))) {
			p_tas256x->client->addr = p_tas256x->devs[i]->mn_addr;
			n_result = tas256x_regmap_update_bits(p_tas256x,
				TAS256X_PAGE_REG(reg), mask, value);
			if (n_result < 0) {
				dev_err(p_tas256x->dev,
					"%s, ERROR, L=%u, chn=0x%02x: E=%d\n",
					__func__, __LINE__,
					p_tas256x->client->addr, n_result);
				rc |= n_result;
				if (chn&channel_left)
					p_tas256x->mn_err_code |= ERROR_DEVA_I2C_COMM;
				if (chn&channel_right)
					p_tas256x->mn_err_code |= ERROR_DEVB_I2C_COMM;
			} else {
				dev_dbg(p_tas256x->dev,
					"%s: chn%x:BOOK:PAGE:REG 0x%02x:0x%02x:0x%02x, mask: 0x%02x, val: 0x%02x\n",
					__func__, p_tas256x->client->addr,
					TAS256X_BOOK_ID(reg),
					TAS256X_PAGE_ID(reg),
					TAS256X_PAGE_REG(reg), mask, value);
				if (chn&channel_left)
					p_tas256x->mn_err_code &= ~ERROR_DEVA_I2C_COMM;
				if (chn&channel_right)
					p_tas256x->mn_err_code &= ~ERROR_DEVB_I2C_COMM;
			}
		}
	}

end:
	mutex_unlock(&p_tas256x->dev_lock);
	return rc;
}

static int tas2558_specific(struct tas256x_priv *p_tas256x, int chn)
{
	int ret = 0;

	ret = tas256x_boost_volt_update(p_tas256x, DEVICE_TAS2558, chn);

	return ret;
}

static int tas2564_specific(struct tas256x_priv *p_tas256x, int chn)
{
	int ret = 0;

	ret = tas256x_boost_volt_update(p_tas256x, DEVICE_TAS2564, chn);
	ret |= tas2564_rx_mode_update(p_tas256x, 0, chn);

	return ret;
}

static char const *tas2564_rx_mode_text[] = {"Speaker", "Receiver"};

static const struct soc_enum tas2564_rx_mode_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tas2564_rx_mode_text),
		tas2564_rx_mode_text),
};

static int tas2564_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
#endif
	struct tas256x_priv *p_tas256x = NULL;
	int ret = -1;

	if (codec == NULL) {
		pr_err("%s:codec is NULL\n", __func__);
		return ret;
	}

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	if (p_tas256x == NULL) {
		pr_err("%s:p_tas256x is NULL\n", __func__);
		return ret;
	}

	if (strnstr(ucontrol->id.name, "LEFT", MAX_STRING))
		ret = tas2564_rx_mode_update(p_tas256x,
			ucontrol->value.integer.value[0], channel_left);
	else if (strnstr(ucontrol->id.name, "RIGHT", MAX_STRING))
		ret = tas2564_rx_mode_update(p_tas256x,
			ucontrol->value.integer.value[0], channel_right);
	else
		dev_err(p_tas256x->dev, "Invalid Channel %s\n",
			ucontrol->id.name);

	return ret;
}

static int tas2564_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	int ret = -1;
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
#endif
	struct tas256x_priv *p_tas256x = NULL;

	if (codec == NULL) {
		pr_err("%s:codec is NULL\n", __func__);
		return ret;
	}

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	if (p_tas256x == NULL) {
		pr_err("%s:p_tas256x is NULL\n", __func__);
		return ret;
	}

	if (strnstr(ucontrol->id.name, "LEFT", MAX_STRING))
			ucontrol->value.integer.value[0] =
				p_tas256x->devs[0]->rx_mode;
	else if (strnstr(ucontrol->id.name, "RIGHT", MAX_STRING))
			ucontrol->value.integer.value[0] =
				p_tas256x->devs[1]->rx_mode;
	else
		dev_err(p_tas256x->dev, "Invalid Channel %s\n",
			ucontrol->id.name);

	return 0;
}

static int tas256x_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
#endif
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct tas256x_priv *p_tas256x = NULL;
	int ret = -1;

	if ((codec == NULL) || (mc == NULL)) {
		pr_err("%s:codec or control is NULL\n", __func__);
		return ret;
	}

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	if (p_tas256x == NULL) {
		pr_err("%s:p_tas256x is NULL\n", __func__);
		return ret;
	}

	if (ucontrol->value.integer.value[0] > mc->max)
		return ret;

	switch (mc->reg) {
	case DVC_PCM:
		ret = tas256x_update_playback_volume(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	case LIM_MAX_ATN:
		ret = tas256x_update_lim_max_attenuation(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	case LIMB_INF_PT:
		ret = tas256x_update_lim_inflection_point(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	case LIMB_SLOPE:
		ret = tas256x_update_lim_slope(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	case LIMB_ATK_RT:
		ret = tas256x_update_limiter_attack_rate(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	case LIMB_RLS_RT:
		ret = tas256x_update_limiter_release_rate(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	case LIMB_RLS_ST:
		ret = tas256x_update_limiter_release_step_size(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	case LIMB_ATK_ST:
		ret = tas256x_update_limiter_attack_step_size(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	case BOP_ATK_RT:
		ret = tas256x_update_bop_attack_rate(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	case BOP_ATK_ST:
		ret = tas256x_update_bop_attack_step_size(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	case BOP_HLD_TM:
		ret = tas256x_update_bop_hold_time(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	case BST_VREG:
		ret = tas256x_update_boost_voltage(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	case BST_ILIM:
		ret = tas256x_update_current_limit(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
	break;
	}
	return ret;
}

static int tas256x_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
#endif
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct tas256x_priv *p_tas256x = NULL;
	int ret = -1;

	if ((codec == NULL) || (mc == NULL)) {
		pr_err("%s:codec or control is NULL\n", __func__);
		return ret;
	}

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	if (p_tas256x == NULL) {
		pr_err("%s:p_tas256x is NULL\n", __func__);
		return ret;
	}

	switch (mc->reg) {
	case DVC_PCM:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->dvc_pcm;
	break;
	case LIM_MAX_ATN:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->lim_max_attn;
	break;
	case LIMB_INF_PT:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->lim_infl_pt;
	break;
	case LIMB_SLOPE:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->lim_trk_slp;
	break;
	case LIMB_ATK_RT:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->lim_att_rate;
	break;
	case LIMB_RLS_RT:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->lim_rel_rate;
	break;
	case LIMB_RLS_ST:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->lim_rel_stp_size;
	break;
	case LIMB_ATK_ST:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->lim_att_stp_size;
	break;
	case BOP_ATK_RT:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->bop_att_rate;
	break;
	case BOP_ATK_ST:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->bop_att_stp_size;
	break;
	case BOP_HLD_TM:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->bop_hld_time;
	break;
	case BST_VREG:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->bst_vltg;
	break;
	case BST_ILIM:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->bst_ilm;
	break;
	}
	return 0;
}

static int tas256x_multi_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
#endif
	struct soc_multi_mixer_control *mc =
		(struct soc_multi_mixer_control *)kcontrol->private_value;
	struct tas256x_priv *p_tas256x = NULL;
	int ret = -1;

	if ((codec == NULL) || (mc == NULL)) {
		pr_err("%s:codec or control is NULL\n", __func__);
		return ret;
	}

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	if (p_tas256x == NULL) {
		pr_err("%s:p_tas256x is NULL\n", __func__);
		return ret;
	}

	if ((ucontrol->value.integer.value[0] > mc->max) ||
		(ucontrol->value.integer.value[1] > mc->max))
		return ret;

	switch (mc->reg) {
	case LIMB_TH_MAX_MIN:
		ret = tas256x_update_lim_max_thr(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
		ret = tas256x_update_lim_min_thr(p_tas256x,
			ucontrol->value.integer.value[1], mc->shift);
	break;
	case BOP_BOSD_TH:
		ret = tas256x_update_bop_thr(p_tas256x,
			ucontrol->value.integer.value[0], mc->shift);
		ret = tas256x_update_bosd_thr(p_tas256x,
			ucontrol->value.integer.value[1], mc->shift);
	break;
	}
	return ret;
}

static int tas256x_multi_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
#endif
	struct soc_multi_mixer_control *mc =
		(struct soc_multi_mixer_control *)kcontrol->private_value;
	struct tas256x_priv *p_tas256x = NULL;
	int ret = -1;

	if ((codec == NULL) || (mc == NULL)) {
		pr_err("%s:codec or control is NULL\n", __func__);
		return ret;
	}

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	if (p_tas256x == NULL) {
		pr_err("%s:p_tas256x is NULL\n", __func__);
		return ret;
	}

	switch (mc->reg) {
	case LIMB_TH_MAX_MIN:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->lim_thr_max;
		ucontrol->value.integer.value[1] =
			p_tas256x->devs[mc->shift-1]->lim_thr_min;
	break;
	case BOP_BOSD_TH:
		ucontrol->value.integer.value[0] =
			p_tas256x->devs[mc->shift-1]->bop_thd;
		ucontrol->value.integer.value[1] =
			p_tas256x->devs[mc->shift-1]->bosd_thd;
	break;
	}
	return 0;
}

static int tas256x_enum_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	int ret = -1;
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
#endif
	struct tas256x_priv *p_tas256x = NULL;

	if (codec == NULL) {
		pr_err("%s:codec is NULL\n", __func__);
		return ret;
	}

	if (ucontrol == NULL) {
		pr_err("%s:ucontrol is NULL\n", __func__);
		return ret;
	}

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	if (p_tas256x == NULL) {
		pr_err("%s:p_tas256x is NULL\n", __func__);
		return ret;
	}

	if (strnstr(ucontrol->id.name, "LEFT", MAX_STRING)) {
		if (strnstr(ucontrol->id.name, "LIMITER SWITCH",
			MAX_STRING))
			ucontrol->value.integer.value[0] =
				p_tas256x->devs[0]->lim_switch;
		else if (strnstr(ucontrol->id.name, "BOP ENABLE",
			MAX_STRING))
			ucontrol->value.integer.value[0] =
				p_tas256x->devs[0]->bop_enable;
		else if (strnstr(ucontrol->id.name, "BOP MUTE",
			MAX_STRING))
			ucontrol->value.integer.value[0] =
				p_tas256x->devs[0]->bop_mute;
		else if (strnstr(ucontrol->id.name, "BROWNOUT SHUTDOWN",
			MAX_STRING))
			ucontrol->value.integer.value[0] =
				p_tas256x->devs[0]->bosd_enable;
		else if (strnstr(ucontrol->id.name, "VBAT LPF",
			MAX_STRING))
			ucontrol->value.integer.value[0] =
				p_tas256x->devs[0]->vbat_lpf;
		else
			dev_err(p_tas256x->dev, "Invalid controll %s\n",
				ucontrol->id.name);
	} else if (strnstr(ucontrol->id.name, "RIGHT", MAX_STRING)) {
		if (strnstr(ucontrol->id.name, "LIMITER SWITCH",
			MAX_STRING))
			ucontrol->value.integer.value[0] =
				p_tas256x->devs[1]->lim_switch;
		else if (strnstr(ucontrol->id.name, "BOP ENABLE",
			MAX_STRING))
			ucontrol->value.integer.value[0] =
				p_tas256x->devs[1]->bop_enable;
		else if (strnstr(ucontrol->id.name, "BOP MUTE",
			MAX_STRING))
			ucontrol->value.integer.value[0] =
				p_tas256x->devs[1]->bop_mute;
		else if (strnstr(ucontrol->id.name, "BROWNOUT SHUTDOWN",
			MAX_STRING))
			ucontrol->value.integer.value[0] =
				p_tas256x->devs[1]->bosd_enable;
		else if (strnstr(ucontrol->id.name, "VBAT LPF",
			MAX_STRING))
			ucontrol->value.integer.value[0] =
				p_tas256x->devs[1]->vbat_lpf;
		else
			dev_err(p_tas256x->dev, "Invalid controll %s\n",
				ucontrol->id.name);
	} else {
		dev_err(p_tas256x->dev, "Invalid Channel %s\n",
			ucontrol->id.name);
	}
	return 0;
}

static int tas256x_enum_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
#endif
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct tas256x_priv *p_tas256x = NULL;
	int ret = -1;

	if ((codec == NULL) || (mc == NULL)) {
		pr_err("%s:codec or control is NULL\n", __func__);
		return ret;
	}

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	if (p_tas256x == NULL) {
		pr_err("%s:p_tas256x is NULL\n", __func__);
		return ret;
	}

	if (strnstr(ucontrol->id.name, "LEFT", MAX_STRING)) {
		if (strnstr(ucontrol->id.name, "LIMITER SWITCH",
			MAX_STRING))
			ret = tas256x_update_limiter_enable(p_tas256x,
				ucontrol->value.integer.value[0],
				channel_left);
		else if (strnstr(ucontrol->id.name, "BOP ENABLE",
			MAX_STRING))
			ret = tas256x_update_bop_enable(p_tas256x,
				ucontrol->value.integer.value[0],
				channel_left);
		else if (strnstr(ucontrol->id.name, "BOP MUTE",
			MAX_STRING))
			ret = tas256x_update_bop_mute(p_tas256x,
				ucontrol->value.integer.value[0],
				channel_left);
		else if (strnstr(ucontrol->id.name, "BROWNOUT SHUTDOWN",
			MAX_STRING))
			ret = tas256x_update_bop_shutdown_enable(p_tas256x,
				ucontrol->value.integer.value[0],
				channel_left);
		else if (strnstr(ucontrol->id.name, "VBAT LPF",
			MAX_STRING))
			ret = tas256x_update_vbat_lpf(p_tas256x,
				ucontrol->value.integer.value[0],
				channel_left);
		else
			dev_err(p_tas256x->dev, "Invalid Control %s\n",
				ucontrol->id.name);
	} else if (strnstr(ucontrol->id.name, "RIGHT", MAX_STRING)) {
		if (strnstr(ucontrol->id.name, "LIMITER SWITCH",
			MAX_STRING))
			ret = tas256x_update_limiter_enable(p_tas256x,
				ucontrol->value.integer.value[0],
				channel_right);
		else if (strnstr(ucontrol->id.name, "BOP ENABLE",
				MAX_STRING))
			ret = tas256x_update_bop_enable(p_tas256x,
				ucontrol->value.integer.value[0],
				channel_right);
		else if (strnstr(ucontrol->id.name, "BOP MUTE",
				MAX_STRING))
			ret = tas256x_update_bop_mute(p_tas256x,
				ucontrol->value.integer.value[0],
				channel_right);
		else if (strnstr(ucontrol->id.name, "BROWNOUT SHUTDOWN",
				MAX_STRING))
			ret = tas256x_update_bop_shutdown_enable(p_tas256x,
				ucontrol->value.integer.value[0],
				channel_right);
		else if (strnstr(ucontrol->id.name, "VBAT LPF",
				MAX_STRING))
			ret = tas256x_update_vbat_lpf(p_tas256x,
					ucontrol->value.integer.value[0],
					channel_right);
		else
			dev_err(p_tas256x->dev, "Invalid control %s\n",
				ucontrol->id.name);
	} else {
		dev_err(p_tas256x->dev, "Invalid Channel %s\n",
			ucontrol->id.name);
	}
	return ret;
}

static char const *tas256x_rx_switch_text[] = {"DISABLE", "ENABLE"};
static const struct soc_enum tas256x_rx_switch_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tas256x_rx_switch_text),
		tas256x_rx_switch_text),
};

static const struct snd_kcontrol_new tas256x_left_controls[] = {
	SOC_SINGLE_EXT("TAS256X PLAYBACK VOLUME LEFT", DVC_PCM, 1, 56, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X LIM MAX ATTN LEFT", LIM_MAX_ATN, 1, 15, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_MULTI_EXT("TAS256X LIM THR MAX MIN LEFT", LIMB_TH_MAX_MIN,
		1, 26, 0, 2, tas256x_multi_get, tas256x_multi_put),
	SOC_SINGLE_EXT("TAS256X LIM INFLECTION POINT LEFT", LIMB_INF_PT,
		1, 40, 0, tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X LIM SLOPE LEFT", LIMB_SLOPE, 1, 6, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_MULTI_EXT("TAS256X BOP THR SHUTDOWN THR LEFT", BOP_BOSD_TH,
		1, 15, 0, 2, tas256x_multi_get, tas256x_multi_put),
	SOC_SINGLE_EXT("TAS256X LIM ATTACT RATE LEFT", LIMB_ATK_RT, 1, 7, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X LIM RELEASE RATE LEFT", LIMB_RLS_RT, 1, 7, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X LIM ATTACK STEP LEFT", LIMB_ATK_ST, 1, 3, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X LIM RELEASE STEP LEFT", LIMB_RLS_ST, 1, 3, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X BOP ATTACK RATE LEFT", BOP_ATK_RT, 1, 7, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X BOP ATTACK STEP LEFT", BOP_ATK_ST, 1, 3, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X BOP HOLD TIME LEFT", BOP_HLD_TM, 1, 7, 0,
		tas256x_get, tas256x_put),
	SOC_ENUM_EXT("TAS256X LIMITER SWITCH LEFT", tas256x_rx_switch_enum[0],
		tas256x_enum_get, tas256x_enum_put),
	SOC_ENUM_EXT("TAS256X BOP ENABLE LEFT", tas256x_rx_switch_enum[0],
		tas256x_enum_get, tas256x_enum_put),
	SOC_ENUM_EXT("TAS256X BOP MUTE LEFT", tas256x_rx_switch_enum[0],
		tas256x_enum_get, tas256x_enum_put),
	SOC_ENUM_EXT("TAS256X BROWNOUT SHUTDOWN LEFT",
		tas256x_rx_switch_enum[0],
		tas256x_enum_get, tas256x_enum_put),
};

static const struct snd_kcontrol_new tas256x_right_controls[] = {
	SOC_SINGLE_EXT("TAS256X PLAYBACK VOLUME RIGHT", DVC_PCM, 2, 56, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X LIM MAX ATTN RIGHT", LIM_MAX_ATN, 2, 15, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_MULTI_EXT("TAS256X LIM THR MAX MIN RIGHT", LIMB_TH_MAX_MIN,
		2, 26, 0, 2, tas256x_multi_get, tas256x_multi_put),
	SOC_SINGLE_EXT("TAS256X LIM INFLECTION POINT RIGHT", LIMB_INF_PT,
		2, 40, 0, tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X LIM SLOPE RIGHT", LIMB_SLOPE, 2, 6, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_MULTI_EXT("TAS256X BOP THR SHUTDOWN THR RIGHT", BOP_BOSD_TH,
		2, 15, 0, 2, tas256x_multi_get, tas256x_multi_put),
	SOC_SINGLE_EXT("TAS256X LIM ATTACT RATE RIGHT", LIMB_ATK_RT, 2, 7, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X LIM RELEASE RATE RIGHT", LIMB_RLS_RT, 2, 7, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X LIM ATTACK STEP RIGHT", LIMB_ATK_ST, 2, 3, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X LIM RELEASE STEP RIGHT", LIMB_RLS_ST, 2, 3, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X BOP ATTACK RATE RIGHT", BOP_ATK_RT, 2, 7, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X BOP ATTACK STEP RIGHT", BOP_ATK_ST, 2, 3, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X BOP HOLD TIME RIGHT", BOP_HLD_TM, 2, 7, 0,
		tas256x_get, tas256x_put),
	SOC_ENUM_EXT("TAS256X LIMITER SWITCH RIGHT", tas256x_rx_switch_enum[0],
		tas256x_enum_get, tas256x_enum_put),
	SOC_ENUM_EXT("TAS256X BOP ENABLE RIGHT", tas256x_rx_switch_enum[0],
		tas256x_enum_get, tas256x_enum_put),
	SOC_ENUM_EXT("TAS256X BOP MUTE RIGHT", tas256x_rx_switch_enum[0],
		tas256x_enum_get, tas256x_enum_put),
	SOC_ENUM_EXT("TAS256X BROWNOUT SHUTDOWN RIGHT",
		tas256x_rx_switch_enum[0], tas256x_enum_get, tas256x_enum_put),
};

static char const *tas2564_vbat_lpf_text[] = {
	"DISABLE", "HZ_10", "HZ_100", "KHZ_1"};
static const struct soc_enum tas2564_vbat_lpf_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tas2564_vbat_lpf_text),
		tas2564_vbat_lpf_text),
};

static char const *tas2562_vbat_lpf_text[] = {
	"DISABLE", "HZ_100", "KHZ_1", "KHZ_10"};
static const struct soc_enum tas2562_vbat_lpf_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tas2562_vbat_lpf_text),
		tas2562_vbat_lpf_text),
};

static const struct snd_kcontrol_new tas2564_left_controls[] = {
	SOC_ENUM_EXT("TAS256X RX MODE LEFT", tas2564_rx_mode_enum[0],
		tas2564_get, tas2564_put),
	SOC_ENUM_EXT("TAS256X VBAT LPF LEFT", tas2564_vbat_lpf_enum[0],
		tas256x_enum_get, tas256x_enum_put),
	SOC_SINGLE_EXT("TAS256X BOOST VOLTAGE LEFT", BST_VREG, 1, 15, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X BOOST CURRENT LEFT", BST_ILIM, 1, 63, 0,
		tas256x_get, tas256x_put),
};

static const struct snd_kcontrol_new tas2564_right_controls[] = {
	SOC_ENUM_EXT("TAS256X RX MODE RIGHT", tas2564_rx_mode_enum[0],
		tas2564_get, tas2564_put),
	SOC_ENUM_EXT("TAS256X VBAT LPF RIGHT", tas2564_vbat_lpf_enum[0],
		tas256x_enum_get, tas256x_enum_put),
	SOC_SINGLE_EXT("TAS256X BOOST VOLTAGE RIGHT", BST_VREG, 2, 15, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X BOOST CURRENT RIGHT", BST_ILIM, 2, 63, 0,
		tas256x_get, tas256x_put),
};

static const struct snd_kcontrol_new tas2562_left_controls[] = {
	SOC_ENUM_EXT("TAS256X VBAT LPF LEFT", tas2562_vbat_lpf_enum[0],
		tas256x_enum_get, tas256x_enum_put),
	SOC_SINGLE_EXT("TAS256X BOOST VOLTAGE LEFT", BST_VREG, 1, 12, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X BOOST CURRENT LEFT", BST_ILIM, 1, 55, 0,
		tas256x_get, tas256x_put),
};

static const struct snd_kcontrol_new tas2562_right_controls[] = {
	SOC_ENUM_EXT("TAS256X VBAT LPF RIGHT", tas2562_vbat_lpf_enum[0],
		tas256x_enum_get, tas256x_enum_put),
	SOC_SINGLE_EXT("TAS256X BOOST VOLTAGE RIGHT", BST_VREG, 2, 12, 0,
		tas256x_get, tas256x_put),
	SOC_SINGLE_EXT("TAS256X BOOST CURRENT RIGHT", BST_ILIM, 2, 55, 0,
		tas256x_get, tas256x_put),
};

static int tas2564_probe(struct tas256x_priv *p_tas256x,
	struct snd_soc_codec *codec, int chn)
{
	int ret = -1;

	if ((!p_tas256x) || (!codec)) {
		pr_err("tas256x:%s p_tas256x or codec is Null\n", __func__);
		return ret;
	}
	dev_dbg(p_tas256x->dev, "%s channel %d", __func__, chn);

	tas256x_update_default_params(p_tas256x, chn);
	if (chn == channel_left) {
		ret = snd_soc_add_codec_controls(codec, tas256x_left_controls,
			ARRAY_SIZE(tas256x_left_controls));
		ret = snd_soc_add_codec_controls(codec, tas2564_left_controls,
			ARRAY_SIZE(tas2564_left_controls));
	} else if (chn == channel_right) {
		ret = snd_soc_add_codec_controls(codec, tas256x_right_controls,
			ARRAY_SIZE(tas256x_right_controls));
		ret = snd_soc_add_codec_controls(codec, tas2564_right_controls,
			ARRAY_SIZE(tas2564_right_controls));
	} else {
		dev_err(p_tas256x->dev, "Invalid Channel %d\n", chn);
	}

	return ret;
}

static int tas2562_probe(struct tas256x_priv *p_tas256x,
	struct snd_soc_codec *codec, int chn)
{
	int ret = -1;

	if ((!p_tas256x) || (!codec)) {
		pr_err("tas256x:%s p_tas256x or codec is Null\n", __func__);
		return ret;
	}
	dev_dbg(p_tas256x->dev, "%s channel %d", __func__, chn);

	tas256x_update_default_params(p_tas256x, chn);
	if (chn == channel_left) {
		ret = snd_soc_add_codec_controls(codec, tas256x_left_controls,
			ARRAY_SIZE(tas256x_left_controls));
		ret = snd_soc_add_codec_controls(codec, tas2562_left_controls,
			ARRAY_SIZE(tas2562_left_controls));
	} else if (chn == channel_right) {
		ret = snd_soc_add_codec_controls(codec, tas256x_right_controls,
			ARRAY_SIZE(tas256x_right_controls));
		ret = snd_soc_add_codec_controls(codec, tas2562_right_controls,
			ARRAY_SIZE(tas2562_right_controls));
	} else {
		dev_err(p_tas256x->dev, "Invalid Channel %d\n", chn);
	}

	return ret;
}

static bool tas256x_volatile(struct device *dev, unsigned int reg)
{
	return true;
}

static bool tas256x_writeable(struct device *dev, unsigned int reg)
{
	return true;
}
static const struct regmap_config tas256x_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = tas256x_writeable,
	.volatile_reg = tas256x_volatile,
	.cache_type = REGCACHE_NONE,
	.max_register = 1 * 128,
};

static void tas256x_hw_reset(struct tas256x_priv *p_tas256x)
{
	int i = 0;

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (gpio_is_valid(p_tas256x->devs[i]->mn_reset_gpio)) {
			gpio_direction_output(
				p_tas256x->devs[i]->mn_reset_gpio, 0);
		}
	}
	msleep(20);

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (gpio_is_valid(p_tas256x->devs[i]->mn_reset_gpio)) {
			gpio_direction_output(
				p_tas256x->devs[i]->mn_reset_gpio, 1);
		}
		p_tas256x->devs[i]->mn_current_book = -1;
		p_tas256x->devs[i]->mn_current_page = -1;
	}
	msleep(20);

	dev_info(p_tas256x->dev, "reset gpio up !!\n");
}

void tas256x_enable_irq(struct tas256x_priv *p_tas256x, bool enable)
{
	static int irq_enabled[2];
	struct irq_desc *desc = NULL;
	int i = 0;

	if (enable) {
		if (p_tas256x->mb_irq_eable)
			return;
		for (i = 0; i < p_tas256x->mn_channels; i++) {
			if (gpio_is_valid(p_tas256x->devs[i]->mn_irq_gpio) &&
				irq_enabled[i] == 0) {
				if (i == 0) {
					desc = irq_to_desc(
						p_tas256x->devs[i]->mn_irq);
					if (desc && desc->depth > 0)
						enable_irq(
							p_tas256x->devs[i]->mn_irq);
					else
						dev_info(p_tas256x->dev,
							"### irq already enabled");
				} else {
					enable_irq(p_tas256x->devs[i]->mn_irq);
				}
				irq_enabled[i] = 1;
			}
		}
		p_tas256x->mb_irq_eable = true;
	} else {
		for (i = 0; i < p_tas256x->mn_channels; i++) {
			if (gpio_is_valid(p_tas256x->devs[i]->mn_irq_gpio)
					&& irq_enabled[i] == 1) {
				disable_irq_nosync(p_tas256x->devs[i]->mn_irq);
				irq_enabled[i] = 0;
			}
		}
		p_tas256x->mb_irq_eable = false;
	}
}

static void irq_work_routine(struct work_struct *work)
{
	struct tas256x_priv *p_tas256x =
		container_of(work, struct tas256x_priv, irq_work.work);
	unsigned int nDevInt1Status = 0, nDevInt2Status = 0,
		nDevInt3Status = 0, nDevInt4Status = 0;
	int n_counter = 2;
	int n_result = 0;
	int irqreg, irqreg2, i, chnTemp = 0;
	enum channel chn = channel_left;

	dev_info(p_tas256x->dev, "%s\n", __func__);
#ifdef CONFIG_TAS256X_CODEC
	mutex_lock(&p_tas256x->codec_lock);
#endif
	tas256x_enable_irq(p_tas256x, false);

	if (p_tas256x->mn_err_code & ERROR_FAILSAFE)
		goto reload;

	if (p_tas256x->mb_runtime_suspend) {
		dev_info(p_tas256x->dev, "%s, Runtime Suspended\n", __func__);
		goto end;
	}

	if (p_tas256x->mn_power_state == TAS256X_POWER_SHUTDOWN) {
		dev_info(p_tas256x->dev, "%s, device not powered\n", __func__);
		goto end;
	}

	n_result = tas256x_interrupt_enable(p_tas256x, 0/*Disable*/,
			channel_both);
	if (n_result < 0)
		goto reload;

	/*Reset error codes*/
	p_tas256x->mn_err_code = 0;

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (p_tas256x->devs[i]->spk_control == 1)
			chnTemp |= 1<<i;
	}
	chn = (chnTemp == 0)?chn:(enum channel)chnTemp;

	if (chn & channel_left) {
		n_result = tas256x_interrupt_read(p_tas256x,
			&nDevInt1Status, &nDevInt2Status, channel_left);
		if (n_result < 0)
			goto reload;
		p_tas256x->mn_err_code =
			tas256x_interrupt_determine(p_tas256x, channel_left,
				nDevInt1Status, nDevInt2Status);
	}

	if (chn & channel_right) {
		n_result = tas256x_interrupt_read(p_tas256x,
			&nDevInt3Status, &nDevInt4Status, channel_right);
		if (n_result < 0)
			goto reload;
		p_tas256x->mn_err_code |=
			tas256x_interrupt_determine(p_tas256x, channel_right,
				nDevInt3Status, nDevInt4Status);
	}

	dev_dbg(p_tas256x->dev, "IRQ status : 0x%x, 0x%x, 0x%x, 0x%x mn_err_code %d\n",
		nDevInt1Status, nDevInt2Status,
		nDevInt3Status, nDevInt4Status,
		p_tas256x->mn_err_code);

	if (p_tas256x->mn_err_code)
		goto reload;
	else {
		n_counter = 2;

		while (n_counter > 0) {
			if (chn & channel_left)
				n_result = tas256x_power_check(p_tas256x,
						&nDevInt1Status,
						channel_left);
			if (n_result < 0)
				goto reload;
			if (chn & channel_right)
				n_result = tas256x_power_check(p_tas256x,
						&nDevInt3Status,
						channel_right);
			if (n_result < 0)
				goto reload;

			if (nDevInt1Status) {
				/* If only left should be power on */
				if (chn == channel_left)
					break;
				/* If both should be power on */
				if (nDevInt3Status)
					break;
			} else if (chn == channel_right) {
				/*If only right should be power on */
				if (nDevInt3Status)
					break;
			}

			tas256x_interrupt_read(p_tas256x,
				&irqreg, &irqreg2, channel_left);
			dev_info(p_tas256x->dev, "IRQ reg is: %s %d, %d\n",
				__func__, irqreg, __LINE__);
			tas256x_interrupt_read(p_tas256x,
				&irqreg, &irqreg2, channel_right);
			dev_info(p_tas256x->dev, "IRQ reg is: %s %d, %d\n",
				__func__, irqreg, __LINE__);



			n_counter--;
			if (n_counter > 0) {
			/* in case check pow status
			 *just after power on TAS256x
			 */
				dev_dbg(p_tas256x->dev,
					"PowSts B: 0x%x, check again after 10ms\n",
					nDevInt1Status);
					msleep(20);
			}
		}

		if (((!nDevInt1Status) && (chn & channel_left))
			|| ((!nDevInt3Status) && (chn & channel_right))) {
				dev_err(p_tas256x->dev,
					"%s, Critical ERROR REG[POWERCONTROL] = 0x%x\n",
				__func__, nDevInt1Status);
			goto reload;
		}
	}

	n_result = tas256x_interrupt_enable(p_tas256x, 1/*Enable*/,
		channel_both);
	if (n_result < 0)
		goto reload;

	goto end;

reload:
	/* hardware reset and reload */
	tas256x_load_config(p_tas256x);

end:
	tas256x_enable_irq(p_tas256x, true);
#ifdef CONFIG_TAS256X_CODEC
	mutex_unlock(&p_tas256x->codec_lock);
#endif
}

static void init_work_routine(struct work_struct *work)
{
	struct tas256x_priv *p_tas256x =
		container_of(work, struct tas256x_priv, init_work.work);
	int n_result = 0;
	int irqreg = 0, irqreg2 = 0;

	pr_info("%s:\n", __func__);

	tas256x_interrupt_read(p_tas256x,
		&irqreg, &irqreg2, channel_left);
	dev_info(p_tas256x->dev, "IRQ reg is: %s %d, %d\n",
		__func__, irqreg, __LINE__);
	tas256x_interrupt_read(p_tas256x,
		&irqreg, &irqreg2, channel_right);
	dev_info(p_tas256x->dev, "IRQ reg is: %s %d, %d\n",
		__func__, irqreg, __LINE__);

	/* Clear latched IRQ before power on */
	n_result = tas256x_interrupt_clear(p_tas256x, channel_both);

	/*Un-Mask interrupt for TDM*/
	n_result = tas256x_interrupt_enable(p_tas256x, 1/*Enable*/,
		channel_both);

	p_tas256x->enable_irq(p_tas256x, true);
}

static irqreturn_t tas256x_irq_handler(int irq, void *dev_id)
{
	struct tas256x_priv *p_tas256x = (struct tas256x_priv *)dev_id;

	/* get IRQ status after 100 ms */
	schedule_delayed_work(&p_tas256x->irq_work, msecs_to_jiffies(100));
	return IRQ_HANDLED;
}

static int tas256x_runtime_suspend(struct tas256x_priv *p_tas256x)
{
	dev_dbg(p_tas256x->dev, "%s\n", __func__);

	p_tas256x->mb_runtime_suspend = true;

	if (delayed_work_pending(&p_tas256x->irq_work)) {
		dev_dbg(p_tas256x->dev, "cancel IRQ work\n");
		cancel_delayed_work_sync(&p_tas256x->irq_work);
	}

	return 0;
}

static int tas256x_runtime_resume(struct tas256x_priv *p_tas256x)
{
	dev_dbg(p_tas256x->dev, "%s\n", __func__);

	p_tas256x->mb_runtime_suspend = false;

	return 0;
}

static int tas256x_pm_suspend(struct device *dev)
{
	struct tas256x_priv *p_tas256x = dev_get_drvdata(dev);

	if (!p_tas256x) {
		dev_err(p_tas256x->dev, "drvdata is NULL\n");
		return -EINVAL;
	}

	mutex_lock(&p_tas256x->codec_lock);
	p_tas256x->i2c_suspend = true;
	tas256x_runtime_suspend(p_tas256x);
	mutex_unlock(&p_tas256x->codec_lock);
	return 0;
}

static int tas256x_pm_resume(struct device *dev)
{
	struct tas256x_priv *p_tas256x = dev_get_drvdata(dev);

	if (!p_tas256x) {
		dev_err(p_tas256x->dev, "drvdata is NULL\n");
		return -EINVAL;
	}
	mutex_lock(&p_tas256x->codec_lock);
	p_tas256x->i2c_suspend = false;
	tas256x_runtime_resume(p_tas256x);
	mutex_unlock(&p_tas256x->codec_lock);
	return 0;
}

#ifdef CONFIG_TAS25XX_ALGO
/*TODO: FIX Use of private data causes crash*/
struct tas256x_priv *g_p_tas256x;

void tas256x_software_reset(void *prv_data)
{
	pr_err("[TI-SmartPA:%s]\n", __func__);
	schedule_delayed_work(&g_p_tas256x->dc_work, msecs_to_jiffies(10));
}

static void dc_work_routine(struct work_struct *work)
{
	struct tas256x_priv *p_tas256x =
		container_of(work, struct tas256x_priv, dc_work.work);

	pr_err("[TI-SmartPA:%s] DC in channel = %d\n", __func__,
		s_dc_detect.channel);
#ifdef CONFIG_TAS2562_CODEC
	mutex_lock(&p_tas256x->codec_lock);
#endif
	tas_reload(p_tas256x, s_dc_detect.channel);

#ifdef CONFIG_TAS2562_CODEC
	mutex_unlock(&p_tas256x->codec_lock);
#endif

}
#endif /*CONFIG_TAS25XX_ALGO*/

static int tas256x_parse_dt(struct device *dev,
					struct tas256x_priv *p_tas256x)
{
	struct device_node *np = dev->of_node;
	int rc = 0, i = 0;

	rc = of_property_read_u32(np, "ti,channels", &p_tas256x->mn_channels);
	if (rc) {
		dev_err(p_tas256x->dev,
			"Looking up %s property in node %s failed %d\n",
			"ti,channels", np->full_name, rc);
		goto EXIT;
	} else {
		dev_dbg(p_tas256x->dev, "ti,channels=%d",
			p_tas256x->mn_channels);
	}

	/*the device structures array*/
	p_tas256x->devs =
		kmalloc(p_tas256x->mn_channels * sizeof(struct tas_device *),
			GFP_KERNEL);
	for (i = 0; i < p_tas256x->mn_channels; i++) {
		p_tas256x->devs[i] = kmalloc(sizeof(struct tas_device),
					GFP_KERNEL);
		if (p_tas256x->devs[i] == NULL) {
			dev_err(p_tas256x->dev,
			"%s:%u:kmalloc failed!\n", __func__, __LINE__);
			rc = -1;
			break;
		}
		p_tas256x->devs[i]->i2c = p_tas256x->client;
		p_tas256x->devs[i]->regmap = p_tas256x->regmap;

		rc = of_property_read_u32(np, dts_tag[i][0],
			&p_tas256x->devs[i]->mn_addr);
		if (rc) {
			dev_err(p_tas256x->dev,
				"Looking up %s property in node %s failed %d\n",
				dts_tag[i][0], np->full_name, rc);
			break;
		} else {
			dev_dbg(p_tas256x->dev, "%s = 0x%02x",
				dts_tag[i][0], p_tas256x->devs[i]->mn_addr);
		}

		p_tas256x->devs[i]->mn_reset_gpio =
			of_get_named_gpio(np, dts_tag[i][1], 0);
		if (!gpio_is_valid(p_tas256x->devs[i]->mn_reset_gpio))
			dev_err(p_tas256x->dev,
				"Looking up %s property in node %s failed %d\n",
				dts_tag[i][1], np->full_name,
				p_tas256x->devs[i]->mn_reset_gpio);
		else
			dev_dbg(p_tas256x->dev, "%s = %d",
				dts_tag[i][1],
				p_tas256x->devs[i]->mn_reset_gpio);

		p_tas256x->devs[i]->mn_irq_gpio =
			of_get_named_gpio(np, dts_tag[i][2], 0);
		if (!gpio_is_valid(p_tas256x->devs[i]->mn_irq_gpio)) {
			dev_err(p_tas256x->dev,
				"Looking up %s property in node %s failed %d\n",
				dts_tag[i][2], np->full_name,
				p_tas256x->devs[i]->mn_irq_gpio);
		} else {
			dev_dbg(p_tas256x->dev, "%s = %d",
				dts_tag[i][2],
				p_tas256x->devs[i]->mn_irq_gpio);
		}
		p_tas256x->devs[i]->spk_control = 1;
	}

	if (rc)
		goto EXIT;

	rc = of_property_read_u32(np, "ti,iv-width", &p_tas256x->mn_iv_width);
	if (rc) {
		dev_err(p_tas256x->dev,
			"Looking up %s property in node %s failed %d\n",
			"ti,iv-width", np->full_name, rc);
	} else {
		dev_dbg(p_tas256x->dev, "ti,iv-width=0x%x",
			p_tas256x->mn_iv_width);
	}

	rc = of_property_read_u32(np, "ti,vbat-mon", &p_tas256x->mn_vbat);
	if (rc) {
		dev_err(p_tas256x->dev,
				"Looking up %s property in node %s failed %d\n",
			"ti,vbat-mon", np->full_name, rc);
	} else {
		dev_dbg(p_tas256x->dev, "ti,vbat-mon=0x%x",
			p_tas256x->mn_vbat);
	}
#ifdef CONFIG_TAS25XX_ALGO
	/*TODO: Enable if feature is needed*/
	/*tas25xx_parse_algo_dt(np);*/
#endif /*CONFIG_TAS25XX_ALGO*/
EXIT:
	return rc;
}

static int tas256x_i2c_probe(struct i2c_client *p_client,
			const struct i2c_device_id *id)
{
	struct tas256x_priv *p_tas256x;
	int n_result = 0;
	int i = 0;

	dev_info(&p_client->dev, "Driver Tag: %s\n", TAS256X_DRIVER_TAG);
	dev_info(&p_client->dev, "%s enter\n", __func__);

	p_tas256x = devm_kzalloc(&p_client->dev,
		sizeof(struct tas256x_priv), GFP_KERNEL);
	if (p_tas256x == NULL) {
		/* dev_err(&p_client->dev, "failed to get i2c device\n"); */
		n_result = -ENOMEM;
		goto err;
	}

	p_tas256x->client = p_client;
	p_tas256x->dev = &p_client->dev;
	i2c_set_clientdata(p_client, p_tas256x);
	dev_set_drvdata(&p_client->dev, p_tas256x);

	p_tas256x->regmap = devm_regmap_init_i2c(p_client,
				&tas256x_i2c_regmap);
	if (IS_ERR(p_tas256x->regmap)) {
		n_result = PTR_ERR(p_tas256x->regmap);
		dev_err(&p_client->dev,
			"Failed to allocate register map: %d\n",
			n_result);
		goto err;
	}

	mutex_init(&p_tas256x->dev_lock);

	p_tas256x->read = tas256x_dev_read;
	p_tas256x->write = tas256x_dev_write;
	p_tas256x->bulk_read = tas256x_dev_bulk_read;
	p_tas256x->bulk_write = tas256x_dev_bulk_write;
	p_tas256x->update_bits = tas256x_dev_update_bits;
	p_tas256x->hw_reset = tas256x_hw_reset;
	p_tas256x->enable_irq = tas256x_enable_irq;
#ifdef CODEC_PM
	p_tas256x->runtime_suspend = tas256x_runtime_suspend;
	p_tas256x->runtime_resume = tas256x_runtime_resume;
	p_tas256x->mn_power_state = TAS256X_POWER_SHUTDOWN;
#endif
	p_tas256x->mn_power_state = TAS256X_POWER_SHUTDOWN;

	if (p_client->dev.of_node)
		tas256x_parse_dt(&p_client->dev, p_tas256x);

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (gpio_is_valid(p_tas256x->devs[i]->mn_reset_gpio)) {
			n_result = gpio_request(
					p_tas256x->devs[i]->mn_reset_gpio,
					reset_gpio_label[i]);
			if (n_result) {
				dev_err(p_tas256x->dev,
					"%s: Failed to request gpio %d\n",
					__func__,
					p_tas256x->devs[i]->mn_reset_gpio);
				n_result = -EINVAL;
				goto err;
			}
		}
	}

	tas256x_hw_reset(p_tas256x);

	dev_info(&p_client->dev, "Before SW reset\n");
	/* Reset the chip */
	n_result = tas56x_software_reset(p_tas256x, channel_both);
	if (n_result < 0) {
		dev_err(&p_client->dev, "I2c fail, %d\n", n_result);
		goto err;
	}
	dev_info(&p_client->dev, "After SW reset\n");

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		n_result = tas56x_get_chipid(p_tas256x,
			&(p_tas256x->devs[i]->mChipID),
			(i == 0) ? channel_left : channel_right);
		switch (p_tas256x->devs[i]->mChipID) {
		case 0x10:
		case 0x20:
			dev_dbg(p_tas256x->dev, "TAS2562 chip");
			p_tas256x->devs[i]->device_id = DEVICE_TAS2562;
			p_tas256x->devs[i]->dev_ops.tas_init = NULL;
			p_tas256x->devs[i]->dev_ops.tas_probe = tas2562_probe;
			break;
		case 0x00:
			dev_dbg(p_tas256x->dev, "TAS2564 chip");
			p_tas256x->devs[i]->device_id = DEVICE_TAS2564;
			p_tas256x->devs[i]->dev_ops.tas_init =
				tas2564_specific;
			p_tas256x->devs[i]->dev_ops.tas_probe = tas2564_probe;
			break;
		default:
			dev_dbg(p_tas256x->dev, "TAS2558 chip");
			p_tas256x->devs[i]->device_id = DEVICE_TAS2558;
			p_tas256x->devs[i]->dev_ops.tas_init =
				tas2558_specific;
			p_tas256x->devs[i]->dev_ops.tas_probe = NULL;
			break;
		}
	}

	INIT_DELAYED_WORK(&p_tas256x->irq_work, irq_work_routine);

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (gpio_is_valid(p_tas256x->devs[i]->mn_irq_gpio)) {
			n_result =
				gpio_request(
					p_tas256x->devs[i]->mn_irq_gpio,
					irq_gpio_label[i]);
			if (n_result < 0) {
				dev_err(p_tas256x->dev,
					"%s:%u: ch 0x%02x: GPIO %d request error\n",
					__func__, __LINE__,
					p_tas256x->devs[i]->mn_addr,
					p_tas256x->devs[i]->mn_irq_gpio);
				goto err;
			}
			gpio_direction_input(p_tas256x->devs[i]->mn_irq_gpio);
			/*tas256x_dev_write(p_tas256x,
			 *	(i == 0)? channel_left : channel_right,
			 *	TAS256X_MISCCONFIGURATIONREG0, 0xce);
			 */
			tas256x_set_misc_config(p_tas256x, 0,
				(i == 0) ? channel_left : channel_right);

			p_tas256x->devs[i]->mn_irq =
				gpio_to_irq(p_tas256x->devs[i]->mn_irq_gpio);
			dev_info(p_tas256x->dev, "irq = %d\n",
				p_tas256x->devs[i]->mn_irq);

			n_result = request_threaded_irq(
					p_tas256x->devs[i]->mn_irq,
					tas256x_irq_handler,
					NULL,
					IRQF_TRIGGER_FALLING|IRQF_ONESHOT,
					p_client->name, p_tas256x);
			if (n_result < 0) {
				dev_err(p_tas256x->dev,
					"request_irq failed, %d\n", n_result);
				goto err;
			}
			disable_irq_nosync(p_tas256x->devs[i]->mn_irq);
		}
	}

	tas256x_enable_irq(p_tas256x, true);
	INIT_DELAYED_WORK(&p_tas256x->init_work, init_work_routine);

#ifdef CONFIG_TAS256X_CODEC
	mutex_init(&p_tas256x->codec_lock);
	n_result = tas256x_register_codec(p_tas256x);
	if (n_result < 0) {
		dev_err(p_tas256x->dev,
			"register codec failed, %d\n", n_result);
		goto err;
	}
#endif

#ifdef CONFIG_TAS256X_MISC
	mutex_init(&p_tas256x->file_lock);
	n_result = tas256x_register_misc(p_tas256x);
	if (n_result < 0) {
		dev_err(p_tas256x->dev, "register codec failed %d\n",
			n_result);
		goto err;
	}
#endif


#ifdef CONFIG_TAS25XX_ALGO
	INIT_DELAYED_WORK(&p_tas256x->dc_work, dc_work_routine);
	g_p_tas256x = p_tas256x;
	register_tas256x_reset_func(tas256x_software_reset, &s_dc_detect);
#endif /*CONFIG_TAS25XX_ALGO*/


err:
	return n_result;
}

static int tas256x_i2c_remove(struct i2c_client *p_client)
{
	int i = 0;
	struct tas256x_priv *p_tas256x = i2c_get_clientdata(p_client);

	dev_info(p_tas256x->dev, "%s\n", __func__);

#ifdef CONFIG_TAS256X_CODEC
	tas256x_deregister_codec(p_tas256x);
	mutex_destroy(&p_tas256x->codec_lock);
#endif

#ifdef CONFIG_TAS256X_MISC
	tas256x_deregister_misc(p_tas256x);
	mutex_destroy(&p_tas256x->file_lock);
#endif

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (gpio_is_valid(p_tas256x->devs[i]->mn_reset_gpio))
			gpio_free(p_tas256x->devs[i]->mn_reset_gpio);
		if (gpio_is_valid(p_tas256x->devs[i]->mn_irq_gpio))
			gpio_free(p_tas256x->devs[i]->mn_irq_gpio);
		if (p_tas256x->devs[i])
			kfree(p_tas256x->devs[i]);
	}

	if (p_tas256x->devs)
		kfree(p_tas256x->devs);

	return 0;
}


static const struct i2c_device_id tas256x_i2c_id[] = {
	{ "tas256x", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas256x_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id tas256x_of_match[] = {
	{ .compatible = "ti, tas256x" },
	{ .compatible = "ti, tas2562" },
	{ .compatible = "ti, tas2564" },
	{},
};
MODULE_DEVICE_TABLE(of, tas256x_of_match);
#endif

static const struct dev_pm_ops tas256x_pm_ops = {
	.suspend = tas256x_pm_suspend,
	.resume = tas256x_pm_resume
};

static struct i2c_driver tas256x_i2c_driver = {
	.driver = {
		.name   = "tas256x",
		.owner  = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(tas256x_of_match),
#endif
		.pm = &tas256x_pm_ops,
	},
	.probe      = tas256x_i2c_probe,
	.remove     = tas256x_i2c_remove,
	.id_table   = tas256x_i2c_id,
};

module_i2c_driver(tas256x_i2c_driver);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TAS256X I2C Smart Amplifier driver");
MODULE_LICENSE("GPL v2");
#endif
