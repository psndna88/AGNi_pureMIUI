/*
 * =============================================================================
 * Copyright (c) 2016  Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.See the GNU General Public License for more details.
 *
 * File:
 *     tas2562-codec.c
 *
 * Description:
 *     ALSA SoC driver for Texas Instruments TAS2562 High Performance 4W Smart
 *     Amplifier
 *
 * =============================================================================
 */

#ifdef CONFIG_TAS2562_CODEC
//#define DEBUG 5
#include <linux/module.h>
#include <linux/moduleparam.h>
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
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "tas2562.h"
#ifdef CONFIG_TAS25XX_ALGO
#include <dsp/tas_smart_amp_v2.h>
#include "tas25xx-calib.h"
#endif /*CONFIG_TAS25XX_ALGO*/
#define TAS2562_MDELAY 0xFFFFFFFE
#define TAS2562_MSLEEP 0xFFFFFFFD
#define TAS2562_IVSENSER_ENABLE  1
#define TAS2562_IVSENSER_DISABLE 0
/* #define TAS2558_CODEC */

static char p_icn[] = {0x00, 0x00, 0x2f, 0x2c};//2019.12.06 longcheer lixiaona add for AUD test spk no signal
static char const *iv_enable_text[] = {"Off", "On"};
static int tas2562iv_enable;
static int mb_mute;

static const struct soc_enum tas2562_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(iv_enable_text), iv_enable_text),
};
static int tas2562_set_fmt(struct tas2562_priv *p_tas2562, unsigned int fmt);

static int tas2562_i2c_load_data(struct tas2562_priv *p_tas2562,
			enum channel chn, unsigned int *p_data);
static int tas2562_system_mute_ctrl_get(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue);
static int tas2562_system_mute_ctrl_put(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue);
static int tas2562_mute_ctrl_get(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue);
static int tas2562_mute_ctrl_put(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue);

static unsigned int p_tas2562_classh_d_data[] = {
		/* reg address			size	values */
	TAS2562_CLASSHHEADROOM, 0x4, 0x09, 0x99, 0x99, 0x9a,
	TAS2562_CLASSHHYSTERESIS, 0x4, 0x0, 0x0, 0x0, 0x0,
	TAS2562_CLASSHMTCT, 0x4, 0xb, 0x0, 0x0, 0x0,
	TAS2562_VBATFILTER, 0x1, 0x38,
	TAS2562_CLASSHRELEASETIMER, 0x1, 0x3c,
	TAS2562_BOOSTSLOPE, 0x1, 0x78,
	TAS2562_TESTPAGECONFIGURATION, 0x1, 0xd,
	TAS2562_CLASSDCONFIGURATION3, 0x1, 0x8e,
	TAS2562_CLASSDCONFIGURATION2, 0x1, 0x49,
	TAS2562_CLASSDCONFIGURATION4, 0x1, 0x21,
	TAS2562_CLASSDCONFIGURATION1, 0x1, 0x80,
	TAS2562_EFFICIENCYCONFIGURATION, 0x1, 0xc1,
	0xFFFFFFFF, 0xFFFFFFFF
};


static unsigned int tas2562_codec_read(struct snd_soc_codec *codec,
		unsigned int reg)
{
	unsigned int value = 0;
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);

	p_tas2562->read (p_tas2562, channel_left, reg, &value);
	dev_dbg(p_tas2562->dev, "%s, reg=%d, value=%d", __func__, reg, value);

	return value;
}

static int tas2562_iv_enable(struct tas2562_priv *p_tas2562, int enable)
{
	int n_result;

	if (enable) {
		pr_debug("%s:tas2562iv_enable\n", __func__);
		n_result = p_tas2562->update_bits(p_tas2562, channel_both,
		TAS2562_POWERCONTROL,
		TAS2562_POWERCONTROL_ISNSPOWER_MASK |
		TAS2562_POWERCONTROL_VSNSPOWER_MASK,
		TAS2562_POWERCONTROL_VSNSPOWER_ACTIVE |
		TAS2562_POWERCONTROL_ISNSPOWER_ACTIVE);
	} else {
		pr_debug("%s:tas2562iv_disable\n", __func__);
		n_result = p_tas2562->update_bits(p_tas2562, channel_both,
		TAS2562_POWERCONTROL,
		TAS2562_POWERCONTROL_ISNSPOWER_MASK |
		TAS2562_POWERCONTROL_VSNSPOWER_MASK,
		TAS2562_POWERCONTROL_VSNSPOWER_POWEREDDOWN |
		TAS2562_POWERCONTROL_ISNSPOWER_POWEREDDOWN);
	}
	tas2562iv_enable = enable;

	return n_result;
}

static int tas2562iv_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct tas2562_priv *p_tas2562 = NULL;
	int iv_enable = 0, n_result = 0;

	if (codec == NULL) {
		pr_err("%s:codec is NULL\n", __func__);
		return 0;
	}

	p_tas2562 = snd_soc_codec_get_drvdata(codec);
	if (p_tas2562 == NULL) {
		pr_err("%s:p_tas2562 is NULL\n", __func__);
		return 0;
	}

	iv_enable = ucontrol->value.integer.value[0];

	n_result = tas2562_iv_enable(p_tas2562, iv_enable);

	pr_debug("%s: tas2562iv_enable = %d\n", __func__, tas2562iv_enable);

	return n_result;
}

static int tas2562iv_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct tas2562_priv *p_tas2562 = NULL;
	int ret, value;

	if (codec == NULL) {
		pr_err("%s:codec is NULL\n", __func__);
		return 0;
	}

	p_tas2562 = snd_soc_codec_get_drvdata(codec);
	if (p_tas2562 == NULL) {
		pr_err("%s:p_tas2562 is NULL\n", __func__);
		return 0;
	}

	ret = p_tas2562->read(p_tas2562, channel_left,
			TAS2562_POWERCONTROL, &value);
	if (ret < 0)
		dev_err(p_tas2562->dev, "can't get ivsensor state %s, L=%d\n",
			__func__, __LINE__);
	else if (((value & TAS2562_POWERCONTROL_ISNSPOWER_MASK)
			== TAS2562_POWERCONTROL_ISNSPOWER_ACTIVE)
			&& ((value & TAS2562_POWERCONTROL_VSNSPOWER_MASK)
			== TAS2562_POWERCONTROL_VSNSPOWER_ACTIVE)) {
		ucontrol->value.integer.value[0] = TAS2562_IVSENSER_ENABLE;
	} else {
		ucontrol->value.integer.value[0] = TAS2562_IVSENSER_DISABLE;
	}

	tas2562iv_enable = ucontrol->value.integer.value[0];
	dev_info(p_tas2562->dev, "value: 0x%x, tas2562iv_enable %d\n",
			value, tas2562iv_enable);

	return 0;
}

static const struct snd_kcontrol_new tas2562_controls[] = {
SOC_ENUM_EXT("TAS2562 IVSENSE ENABLE", tas2562_enum[0],
			tas2562iv_get, tas2562iv_put),
};

static int tas2562_codec_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(p_tas2562->dev, "%s: %d, %d", __func__, reg, value);
	p_tas2562->write (p_tas2562, channel_both, reg, value);

	return 0;
}

static int tas2562_i2c_load_data(struct tas2562_priv *p_tas2562,
				enum channel chn,
				unsigned int *p_data)
{
	unsigned int n_register;
	unsigned int *n_data;
	unsigned char buf[128];
	unsigned int n_length = 0;
	unsigned int i = 0;
	unsigned int n_size = 0;
	int n_result = 0;

	do {
		n_register = p_data[n_length];
		n_size = p_data[n_length + 1];
		n_data = &p_data[n_length + 2];
		if (n_register == TAS2562_MSLEEP) {
			msleep(n_data[0]);
			dev_dbg(p_tas2562->dev, "%s, msleep = %d\n",
				__func__, n_data[0]);
		} else if (n_register == TAS2562_MDELAY) {
			msleep(n_data[0]);
			dev_dbg(p_tas2562->dev, "%s, mdelay = %d\n",
				__func__, n_data[0]);
		} else {
			if (n_register != 0xFFFFFFFF) {
				if (n_size > 128) {
					dev_err(p_tas2562->dev,
					"%s, Line=%d, invalid size, maximum is 128 bytes!\n",
					__func__, __LINE__);
					break;
				}
			if (n_size > 1) {
				for (i = 0; i < n_size; i++)
					buf[i] = (unsigned char)n_data[i];
				n_result = p_tas2562->bulk_write(p_tas2562, chn,
					n_register, buf, n_size);
				if (n_result < 0)
					break;
				} else if (n_size == 1) {
					n_result = p_tas2562->write(p_tas2562,
						chn,
						n_register, n_data[0]);
					if (n_result < 0)
						break;
				} else {
					dev_err(p_tas2562->dev,
						"%s, Line=%d,invalid size, minimum is 1 bytes!\n",
						__func__, __LINE__);
				}
			}
		}
		n_length = n_length + 2 + p_data[n_length + 1];
	} while (n_register != 0xFFFFFFFF);
	return n_result;
}
#ifdef CODEC_PM
static int tas2562_codec_suspend(struct snd_soc_codec *codec)
{
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	mutex_lock(&p_tas2562->codec_lock);

	dev_dbg(p_tas2562->dev, "%s\n", __func__);
	p_tas2562->runtime_suspend(p_tas2562);

	mutex_unlock(&p_tas2562->codec_lock);
	return ret;
}

static int tas2562_codec_resume(struct snd_soc_codec *codec)
{
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	mutex_lock(&p_tas2562->codec_lock);

	dev_dbg(p_tas2562->dev, "%s\n", __func__);
	p_tas2562->runtime_resume(p_tas2562);

	mutex_unlock(&p_tas2562->codec_lock);
	return ret;
}
#endif

static int tas2562_set_power_state(struct tas2562_priv *p_tas2562,
			enum channel chn, int state)
{
	int n_result = 0;
	/*unsigned int n_value;*/

	if ((p_tas2562->mb_mute) && (state == TAS2562_POWER_ACTIVE))
		state = TAS2562_POWER_MUTE;
	dev_info(p_tas2562->dev, "set power state: %d\n", state);

	switch (state) {
	case TAS2562_POWER_ACTIVE:
		/* if set format was not called by asoc, then set it default */
               if (p_tas2562->mn_asi_format == 0) {
                       p_tas2562->mn_asi_format = SND_SOC_DAIFMT_CBS_CFS
                                | SND_SOC_DAIFMT_IB_NF
                                | SND_SOC_DAIFMT_I2S;
                        n_result = tas2562_set_fmt(p_tas2562, p_tas2562->mn_asi_format);
                        if (n_result < 0)
                                return n_result;
          }
		/* Clear latched IRQ before power on */
		p_tas2562->update_bits(p_tas2562, chn,
			TAS2562_INTERRUPTCONFIGURATION,
			TAS2562_INTERRUPTCONFIGURATION_LTCHINTCLEAR_MASK,
			TAS2562_INTERRUPTCONFIGURATION_LTCHINTCLEAR);
                /* Set power active */
                n_result = p_tas2562->update_bits(p_tas2562,
                        chn, TAS2562_POWERCONTROL,
                        TAS2562_POWERCONTROL_OPERATIONALMODE10_MASK,
                        TAS2562_POWERCONTROL_OPERATIONALMODE10_ACTIVE);
                if (n_result < 0)
                        return n_result;

                p_tas2562->mb_power_up = true;
                p_tas2562->mn_power_state = TAS2562_POWER_ACTIVE;

                dev_dbg(p_tas2562->dev, "set ICN to -80dB\n");
                n_result = p_tas2562->bulk_write(p_tas2562, chn,
                        TAS2562_ICN_REG, p_icn, 4);
		schedule_delayed_work(&p_tas2562->irq_work,
				msecs_to_jiffies(40));
		break;

	case TAS2562_POWER_MUTE:
		n_result = p_tas2562->update_bits(p_tas2562, chn,
			TAS2562_POWERCONTROL,
			TAS2562_POWERCONTROL_OPERATIONALMODE10_MASK |
			TAS2562_POWERCONTROL_ISNSPOWER_MASK |
			TAS2562_POWERCONTROL_VSNSPOWER_MASK,
			TAS2562_POWERCONTROL_OPERATIONALMODE10_MUTE |
			TAS2562_POWERCONTROL_VSNSPOWER_ACTIVE |
			TAS2562_POWERCONTROL_ISNSPOWER_ACTIVE);
			p_tas2562->mb_power_up = true;
			p_tas2562->mn_power_state = TAS2562_POWER_MUTE;
		break;

	case TAS2562_POWER_SHUTDOWN:

		n_result = p_tas2562->update_bits(p_tas2562, chn,
			TAS2562_POWERCONTROL,
			TAS2562_POWERCONTROL_OPERATIONALMODE10_MASK,
			TAS2562_POWERCONTROL_OPERATIONALMODE10_SHUTDOWN);
			p_tas2562->mb_power_up = false;
			p_tas2562->mn_power_state = TAS2562_POWER_SHUTDOWN;
		msleep(20);
		p_tas2562->enable_irq(p_tas2562, false);
		break;

	default:
		dev_err(p_tas2562->dev, "wrong power state setting %d\n",
				state);
	}

	return n_result;
}


static int tas2562_dac_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		dev_info(p_tas2562->dev, "SND_SOC_DAPM_POST_PMU\n");
		break;
	case SND_SOC_DAPM_PRE_PMD:
		dev_info(p_tas2562->dev, "SND_SOC_DAPM_PRE_PMD\n");
		break;

	}

	return 0;
}

static const struct snd_soc_dapm_widget tas2562_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI1", "ASI1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0, tas2562_dac_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("OUT")
};

static const struct snd_soc_dapm_route tas2562_audio_map[] = {
	{"DAC", NULL, "ASI1"},
	{"OUT", NULL, "DAC"}
};

static int tas2562_get_left_speaker_switch(struct snd_kcontrol *pKcontrol,
					struct snd_ctl_elem_value *p_u_control)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);

	dev_info(p_tas2562->dev, "%s, p_u_control = %ld\n",
			__func__, p_u_control->value.integer.value[0]);
	p_u_control->value.integer.value[0] = p_tas2562->spk_l_control;
	return 0;
}

static int tas2562_set_left_speaker_switch(struct snd_kcontrol *pKcontrol,
					struct snd_ctl_elem_value *p_u_control)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);

	dev_info(p_tas2562->dev, "%s, spk_l_control = %d\n",
			__func__, p_tas2562->spk_l_control);
	p_tas2562->spk_l_control = p_u_control->value.integer.value[0];
	return 0;
}

static int tas2562_get_right_speaker_switch(struct snd_kcontrol *pKcontrol,
					struct snd_ctl_elem_value *p_u_control)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);

	dev_info(p_tas2562->dev, "%s, p_u_control = %ld\n",
			__func__, p_u_control->value.integer.value[0]);
	p_u_control->value.integer.value[0] = p_tas2562->spk_r_control;
	return 0;
}

static int tas2562_set_right_speaker_switch(struct snd_kcontrol *pKcontrol,
					struct snd_ctl_elem_value *p_u_control)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);

	dev_info(p_tas2562->dev, "%s, spk_r_control = %d\n",
			__func__, p_tas2562->spk_r_control);
	p_tas2562->spk_r_control = p_u_control->value.integer.value[0];
	return 0;
}

static int tas2562_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);
	enum channel chn;

	dev_dbg(p_tas2562->dev, "%s,%d\n", __func__, mute);
	mutex_lock(&p_tas2562->codec_lock);

	if ((p_tas2562->spk_l_control == 1)
		&& (p_tas2562->spk_r_control == 1)
		&& (p_tas2562->mn_channels == 2))
		chn = channel_both;
	else if (p_tas2562->spk_l_control == 1)
		chn = channel_left;
	else if ((p_tas2562->spk_r_control == 1)
			&& (p_tas2562->mn_channels == 2))
		chn = channel_right;
	else
		chn = channel_left;

	if (mute) {
		tas2562_set_power_state(p_tas2562,
		channel_both, TAS2562_POWER_SHUTDOWN);
	} else {

		tas2562_set_power_state(p_tas2562, chn, TAS2562_POWER_ACTIVE);
	}
	mutex_unlock(&p_tas2562->codec_lock);
	return 0;
}

static int tas2562_iv_slot_config(struct tas2562_priv *p_tas2562)
{
	int ret = 0;

	dev_info(p_tas2562->dev, "%s, %d\n", __func__,
			p_tas2562->mn_slot_width);

	if (p_tas2562->mn_channels == 2) {
		if (p_tas2562->mn_slot_width == 16) {
			p_tas2562->update_bits(p_tas2562, channel_left,
				TAS2562_TDMCONFIGURATIONREG5, 0xff, 0x41);

			p_tas2562->update_bits(p_tas2562, channel_left,
				TAS2562_TDMCONFIGURATIONREG6, 0xff, 0x40);

			p_tas2562->update_bits(p_tas2562, channel_right,
				TAS2562_TDMCONFIGURATIONREG5, 0xff, 0x43);

			p_tas2562->update_bits(p_tas2562, channel_right,
				TAS2562_TDMCONFIGURATIONREG6, 0xff, 0x42);
		} else {

			p_tas2562->update_bits(p_tas2562, channel_left,
				TAS2562_TDMCONFIGURATIONREG5, 0xff, 0x42);

			p_tas2562->update_bits(p_tas2562, channel_left,
				TAS2562_TDMCONFIGURATIONREG6, 0xff, 0x40);

			p_tas2562->update_bits(p_tas2562, channel_right,
				TAS2562_TDMCONFIGURATIONREG5, 0xff, 0x46);

			p_tas2562->update_bits(p_tas2562, channel_right,
				TAS2562_TDMCONFIGURATIONREG6, 0xff, 0x44);
		}
	} else if ((p_tas2562->mn_channels == 1)
		&& (p_tas2562->mn_slot_width == 32)) {
		p_tas2562->update_bits(p_tas2562, channel_left,
			TAS2562_TDMCONFIGURATIONREG5, 0xff, 0x44);

		p_tas2562->update_bits(p_tas2562, channel_left,
			TAS2562_TDMCONFIGURATIONREG6, 0xff, 0x40);
	} else if ((p_tas2562->mn_channels == 1)
		&& (p_tas2562->mn_slot_width == 16)) {
		p_tas2562->update_bits(p_tas2562, channel_left,
			TAS2562_TDMCONFIGURATIONREG5, 0xff, 0x42);

		p_tas2562->update_bits(p_tas2562, channel_left,
			TAS2562_TDMCONFIGURATIONREG6, 0xff, 0x40);
	} else {
		dev_err(p_tas2562->dev, "%s, wrong params, %d\n",
		__func__, p_tas2562->mn_slot_width);
	}

	return ret;
}

static int tas2562_iv_bitwidth_config(struct tas2562_priv *p_tas2562)
{
	int ret = 0;

	if ((p_tas2562->mn_channels == 2) && (p_tas2562->mn_slot_width == 16)){
		ret = p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG2,
			TAS2562_TDMCONFIGURATIONREG2_IVMONLEN76_MASK,
			TAS2562_TDMCONFIGURATIONREG2_IVMONLEN76_8BITS);
	} else {
		ret = p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG2,
			TAS2562_TDMCONFIGURATIONREG2_IVMONLEN76_MASK,
			TAS2562_TDMCONFIGURATIONREG2_IVMONLEN76_16BITS);
	}

	return ret;
}

static int tas2562_set_slot(struct tas2562_priv *p_tas2562, int slot_width)
{
	int ret = 0;

	switch (slot_width) {
	case 16:
	ret = p_tas2562->update_bits(p_tas2562, channel_both,
		TAS2562_TDMCONFIGURATIONREG2,
		TAS2562_TDMCONFIGURATIONREG2_RXSLEN10_MASK,
		TAS2562_TDMCONFIGURATIONREG2_RXSLEN10_16BITS);
	break;

	case 24:
	ret = p_tas2562->update_bits(p_tas2562, channel_both,
		TAS2562_TDMCONFIGURATIONREG2,
		TAS2562_TDMCONFIGURATIONREG2_RXSLEN10_MASK,
		TAS2562_TDMCONFIGURATIONREG2_RXSLEN10_24BITS);
	break;

	case 32:
	ret = p_tas2562->update_bits(p_tas2562, channel_both,
		TAS2562_TDMCONFIGURATIONREG2,
		TAS2562_TDMCONFIGURATIONREG2_RXSLEN10_MASK,
		TAS2562_TDMCONFIGURATIONREG2_RXSLEN10_32BITS);
	break;

	case 0:
	/* Do not change slot width */
	break;

	default:
		dev_err(p_tas2562->dev, "slot width not supported");
		ret = -EINVAL;
	}

	if (ret >= 0)
		p_tas2562->mn_slot_width = slot_width;

	return ret;
}

static int tas2562_set_bitwidth(struct tas2562_priv *p_tas2562, int bitwidth)
{
	int slot_width_tmp = 16;

	dev_info(p_tas2562->dev, "%s %d\n", __func__, bitwidth);
	switch (bitwidth) {
	case SNDRV_PCM_FORMAT_S16_LE:
			p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG2,
			TAS2562_TDMCONFIGURATIONREG2_RXWLEN32_MASK,
			TAS2562_TDMCONFIGURATIONREG2_RXWLEN32_16BITS);
			p_tas2562->mn_ch_size = 16;
			slot_width_tmp = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
			p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG2,
			TAS2562_TDMCONFIGURATIONREG2_RXWLEN32_MASK,
			TAS2562_TDMCONFIGURATIONREG2_RXWLEN32_24BITS);
			p_tas2562->mn_ch_size = 24;
			slot_width_tmp = 32;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
			p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG2,
			TAS2562_TDMCONFIGURATIONREG2_RXWLEN32_MASK,
			TAS2562_TDMCONFIGURATIONREG2_RXWLEN32_32BITS);
			p_tas2562->mn_ch_size = 32;
			slot_width_tmp = 32;
		break;

	default:
		dev_info(p_tas2562->dev, "Not supported params format\n");
	}

	tas2562_set_slot(p_tas2562, slot_width_tmp);

	p_tas2562->update_bits(p_tas2562, channel_left,
			TAS2562_TDMCONFIGURATIONREG2,
			TAS2562_TDMCONFIGURATIONREG2_RXSCFG54_MASK,
			TAS2562_TDMCONFIGURATIONREG2_RXSCFG54_MONO_LEFT);
#ifdef CHANNEL_RIGHT
	p_tas2562->update_bits(p_tas2562, channel_right,
			TAS2562_TDMCONFIGURATIONREG2,
			TAS2562_TDMCONFIGURATIONREG2_RXSCFG54_MASK,
			TAS2562_TDMCONFIGURATIONREG2_RXSCFG54_MONO_RIGHT);
#endif
	tas2562_iv_slot_config(p_tas2562);
	tas2562_iv_bitwidth_config(p_tas2562);

	dev_dbg(p_tas2562->dev, "mn_ch_size: %d\n", p_tas2562->mn_ch_size);
	p_tas2562->mn_pcm_format = bitwidth;

	return 0;
}

static int tas2562_set_samplerate(struct tas2562_priv *p_tas2562,
			int samplerate)
{
	switch (samplerate) {
	case 48000:
		p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG0,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATERAMP_MASK,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATERAMP_48KHZ);
		p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG0,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATE31_MASK,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATE31_44_1_48KHZ);
			break;
	case 44100:
		p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG0,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATERAMP_MASK,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATERAMP_44_1KHZ);
		p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG0,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATE31_MASK,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATE31_44_1_48KHZ);
			break;
	case 96000:
		p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG0,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATERAMP_MASK,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATERAMP_48KHZ);
		p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG0,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATE31_MASK,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATE31_88_2_96KHZ);
			break;
	case 88200:
		p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG0,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATERAMP_MASK,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATERAMP_44_1KHZ);
		p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG0,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATE31_MASK,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATE31_88_2_96KHZ);
			break;
	case 19200:
		p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG0,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATERAMP_MASK,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATERAMP_48KHZ);
		p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG0,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATE31_MASK,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATE31_176_4_192KHZ);
			break;
	case 17640:
		p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG0,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATERAMP_MASK,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATERAMP_44_1KHZ);
		p_tas2562->update_bits(p_tas2562, channel_both,
			TAS2562_TDMCONFIGURATIONREG0,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATE31_MASK,
			TAS2562_TDMCONFIGURATIONREG0_SAMPRATE31_176_4_192KHZ);
			break;
	default:
			dev_info(p_tas2562->dev, "%s, unsupported sample rate, %d\n",
			__func__, samplerate);
	}

	p_tas2562->mn_sampling_rate = samplerate;
	return 0;
}

static int tas2562_system_mute_ctrl_get(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);

	pValue->value.integer.value[0] = p_tas2562->mb_mute;
	dev_dbg(p_tas2562->dev, "%s = %d\n",
		__func__, p_tas2562->mb_mute);

	return 0;
}

static int tas2562_system_mute_ctrl_put(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);

	mb_mute = pValue->value.integer.value[0];

	dev_dbg(p_tas2562->dev, "%s = %d\n", __func__, mb_mute);

	p_tas2562->mb_mute = !!mb_mute;

	return 0;
}

static int tas2562_mute_ctrl_get(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);

	pValue->value.integer.value[0] = p_tas2562->mb_mute;

	if((p_tas2562->mb_power_up == true)&&
		(p_tas2562->mn_power_state == TAS2562_POWER_ACTIVE))
		pValue->value.integer.value[0] = 0;
	else
		pValue->value.integer.value[0] = 1;

	dev_dbg(p_tas2562->dev, "%s = %ld\n",
		__func__, pValue->value.integer.value[0]);

	return 0;
}

static int tas2562_mute_ctrl_put(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);
	enum channel chn;
	int mute = pValue->value.integer.value[0];

	dev_dbg(p_tas2562->dev, "%s, %d\n", __func__, mute);
	mutex_lock(&p_tas2562->codec_lock);

	if ((p_tas2562->spk_l_control == 1)
		&& (p_tas2562->spk_r_control == 1)
		&& (p_tas2562->mn_channels == 2))
		chn = channel_both;
	else if (p_tas2562->spk_l_control == 1)
		chn = channel_left;
	else if ((p_tas2562->spk_r_control == 1)
			&& (p_tas2562->mn_channels == 2))
		chn = channel_right;
	else
		chn = channel_left;

	if (mute) {
		tas2562_set_power_state(p_tas2562,
			chn, TAS2562_POWER_MUTE);
	} else {
		tas2562_set_power_state(p_tas2562, chn, TAS2562_POWER_ACTIVE);
	}
	mutex_unlock(&p_tas2562->codec_lock);
	return 0;
}

static int tas2562_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);
	/* int blr_clk_ratio; */
	int n_result = 0;

	dev_dbg(p_tas2562->dev, "%s, format: %d\n", __func__,
		params_format(params));

	mutex_lock(&p_tas2562->codec_lock);

	n_result = tas2562_set_bitwidth(p_tas2562,
			params_format(params));
	if (n_result < 0) {
		dev_info(p_tas2562->dev, "set bitwidth failed, %d\n", n_result);
		goto ret;
	}

	dev_dbg(p_tas2562->dev, "%s, sample rate: %d\n", __func__,
		params_rate(params));

	n_result = tas2562_set_samplerate(p_tas2562,
	params_rate(params));

ret:
	mutex_unlock(&p_tas2562->codec_lock);
	return n_result;
}

static int tas2562_set_fmt(struct tas2562_priv *p_tas2562, unsigned int fmt)
{
	u8 tdm_rx_start_slot = 0, asi_cfg_1 = 0;
	int ret = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		asi_cfg_1 = 0x00;
		break;
	default:
		dev_err(p_tas2562->dev, "ASI format master is not found\n");
		ret = -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		dev_info(p_tas2562->dev, "INV format: NBNF\n");
		asi_cfg_1 |= TAS2562_TDMCONFIGURATIONREG1_RXEDGE_RISING;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		dev_info(p_tas2562->dev, "INV format: IBNF\n");
		asi_cfg_1 |= TAS2562_TDMCONFIGURATIONREG1_RXEDGE_FALLING;
		break;
	default:
		dev_err(p_tas2562->dev, "ASI format Inverse is not found\n");
		ret = -EINVAL;
	}

	p_tas2562->update_bits(p_tas2562, channel_both,
		TAS2562_TDMCONFIGURATIONREG1,
		TAS2562_TDMCONFIGURATIONREG1_RXEDGE_MASK,
		asi_cfg_1);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case (SND_SOC_DAIFMT_I2S):
		tdm_rx_start_slot = 1;
		break;
	case (SND_SOC_DAIFMT_DSP_A):
	case (SND_SOC_DAIFMT_DSP_B):
		tdm_rx_start_slot = 1;
		break;
	case (SND_SOC_DAIFMT_LEFT_J):
		tdm_rx_start_slot = 0;
		break;
	default:
	dev_err(p_tas2562->dev, "DAI Format is not found, fmt=0x%x\n", fmt);
	ret = -EINVAL;
		break;
	}

	p_tas2562->update_bits(p_tas2562, channel_left,
		TAS2562_TDMCONFIGURATIONREG1,
		TAS2562_TDMCONFIGURATIONREG1_RXOFFSET51_MASK,
		(tdm_rx_start_slot <<
		TAS2562_TDMCONFIGURATIONREG1_RXOFFSET51_SHIFT));

	if (p_tas2562->mn_channels == 2) {
		p_tas2562->update_bits(p_tas2562, channel_right,
			TAS2562_TDMCONFIGURATIONREG1,
			TAS2562_TDMCONFIGURATIONREG1_RXOFFSET51_MASK,
			(tdm_rx_start_slot <<
			TAS2562_TDMCONFIGURATIONREG1_RXOFFSET51_SHIFT));
	}

	p_tas2562->mn_asi_format = fmt;

	return 0;
}

static int tas2562_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	dev_dbg(p_tas2562->dev, "%s, format=0x%x\n", __func__, fmt);

	ret = tas2562_set_fmt(p_tas2562, fmt);
	return ret;
}

static int tas2562_set_dai_tdm_slot(struct snd_soc_dai *dai,
		unsigned int tx_mask, unsigned int rx_mask,
		int slots, int slot_width)
{
	int ret = 0;
	struct snd_soc_codec *codec = dai->codec;
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(p_tas2562->dev, "%s, tx_mask:%d, rx_mask:%d, slots:%d, slot_width:%d",
			__func__, tx_mask, rx_mask, slots, slot_width);

	ret = tas2562_set_slot(p_tas2562, slot_width);

	return ret;
}

static struct snd_soc_dai_ops tas2562_dai_ops = {
	.digital_mute = tas2562_mute,
	.hw_params  = tas2562_hw_params,
	.set_fmt    = tas2562_set_dai_fmt,
	.set_tdm_slot = tas2562_set_dai_tdm_slot,
};

#define TAS2562_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

#define TAS2562_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 \
						SNDRV_PCM_RATE_88200 |\
						SNDRV_PCM_RATE_96000 |\
						SNDRV_PCM_RATE_176400 |\
						SNDRV_PCM_RATE_192000\
						)

static struct snd_soc_dai_driver tas2562_dai_driver[] = {
	{
		.name = "tas2562 ASI1",
		.id = 0,
		.playback = {
			.stream_name    = "ASI1 Playback",
			.channels_min   = 2,
			.channels_max   = 2,
			.rates      = SNDRV_PCM_RATE_8000_192000,
			.formats    = TAS2562_FORMATS,
		},
		.capture = {
			.stream_name    = "ASI1 Capture",
			.channels_min   = 0,
			.channels_max   = 2,
			.rates          = SNDRV_PCM_RATE_8000_192000,
			.formats    = TAS2562_FORMATS,
		},
		.ops = &tas2562_dai_ops,
		.symmetric_rates = 1,
	},
};

static int tas2562_load_init(struct tas2562_priv *p_tas2562)
{
	int ret;

#ifdef TAS2558_CODEC
/* Max voltage to 9V */
	ret = p_tas2562->update_bits(p_tas2562, channel_both,
		TAS2562_BOOSTCONFIGURATION2,
		TAS2562_BOOSTCONFIGURATION2_BOOSTMAXVOLTAGE_MASK,
		0x7);
	ret = p_tas2562->update_bits(p_tas2562, channel_both,
		TAS2562_PLAYBACKCONFIGURATIONREG0,
		TAS2562_PLAYBACKCONFIGURATIONREG0_AMPLIFIERLEVEL51_MASK,
		0xd << 1);
	if (ret < 0)
		return ret;
#endif

	ret = p_tas2562->write(p_tas2562, channel_both,
		TAS2562_MISCCONFIGURATIONREG0, 0xcf);
	if (ret < 0)
		return ret;
#ifdef CONFIG_PLATFORM_EXYONS
	if(p_tas2562->mn_channels == 2) {
		ret = p_tas2562->write(p_tas2562, channel_both,
				TAS2562_TDMConfigurationReg4, 0x13);
		if (ret < 0)
			return ret;
	} else {
		ret = p_tas2562->write(p_tas2562, channel_both,
				TAS2562_TDMConfigurationReg4, 0x03);
		if (ret < 0)
			return ret;
	}
#else
	if (p_tas2562->mn_channels == 2) {
		ret = p_tas2562->write(p_tas2562, channel_both,
			TAS2562_TDMConfigurationReg4, 0x11);
		if (ret < 0)
			return ret;
        } else {
		ret = p_tas2562->write(p_tas2562, channel_both,
			TAS2562_TDMConfigurationReg4, 0x01);
		if (ret < 0)
			return ret;
	}
#endif
	ret = p_tas2562->write(p_tas2562, channel_both,
		TAS2562_CLOCKCONFIGURATION, 0x0c);
	if (ret < 0)
		return ret;
	ret = tas2562_i2c_load_data(p_tas2562, channel_both,
			p_tas2562_classh_d_data);

	return ret;
}

static int tas2562_codec_probe(struct snd_soc_codec *codec)
{
	int ret;
	struct tas2562_priv *p_tas2562 = snd_soc_codec_get_drvdata(codec);

	ret = snd_soc_add_codec_controls(codec, tas2562_controls,
					 ARRAY_SIZE(tas2562_controls));
	if (ret < 0) {
		pr_err("%s: add_codec_controls failed, err %d\n",
			__func__, ret);
		return ret;
	}
#ifdef CONFIG_TAS25XX_ALGO
	tas_smartamp_add_algo_controls(codec);
#endif /*CONFIG_TAS25XX_ALGO*/
	tas2562_load_init(p_tas2562);
	tas2562_iv_enable(p_tas2562, 1);
	dev_info(p_tas2562->dev, "%s\n", __func__);

	return 0;
}

static int tas2562_codec_remove(struct snd_soc_codec *codec)
{
#ifdef CONFIG_TAS25XX_ALGO
	tas_smartamp_remove_algo_controls(codec);
#endif /*CONFIG_TAS25XX_ALGO*/
	return 0;
}

static DECLARE_TLV_DB_SCALE(tas2562_digital_tlv, 1100, 50, 0);

static const char * const speaker_switch_text[] = {"Off", "On"};
static const struct soc_enum spk_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(speaker_switch_text),
	speaker_switch_text),
};

static const struct snd_kcontrol_new tas2562_snd_controls[] = {
	SOC_SINGLE_TLV("Amp Output Level", TAS2562_PLAYBACKCONFIGURATIONREG0,
		1, 0x16, 0,
		tas2562_digital_tlv),
	SOC_SINGLE_EXT("SmartPA System Mute", SND_SOC_NOPM, 0, 0x0001, 0,
			tas2562_system_mute_ctrl_get, tas2562_system_mute_ctrl_put),
	SOC_SINGLE_EXT("SmartPA Mute", SND_SOC_NOPM, 0, 0x0001, 0,
			tas2562_mute_ctrl_get, tas2562_mute_ctrl_put),
	SOC_ENUM_EXT("TAS2562 Left Speaker Switch", spk_enum[0],
			tas2562_get_left_speaker_switch,
			tas2562_set_left_speaker_switch),
	SOC_ENUM_EXT("TAS2562 Right Speaker Switch", spk_enum[0],
			tas2562_get_right_speaker_switch,
			tas2562_set_right_speaker_switch),
};

static struct snd_soc_codec_driver soc_codec_driver_tas2562 = {
	.probe			= tas2562_codec_probe,
	.remove			= tas2562_codec_remove,
	.read			= tas2562_codec_read,
	.write			= tas2562_codec_write,
#ifdef CODEC_PM
	.suspend		= tas2562_codec_suspend,
	.resume			= tas2562_codec_resume,
#endif
	.component_driver = {
		.controls		= tas2562_snd_controls,
		.num_controls		= ARRAY_SIZE(tas2562_snd_controls),
		.dapm_widgets		= tas2562_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(tas2562_dapm_widgets),
		.dapm_routes		= tas2562_audio_map,
		.num_dapm_routes	= ARRAY_SIZE(tas2562_audio_map),
	},
};

int tas2562_register_codec(struct tas2562_priv *p_tas2562)
{
	int n_result = 0;

	dev_info(p_tas2562->dev, "%s, enter\n", __func__);
	dev_set_name(&(p_tas2562->client->dev), "%s", "tas2562");
	n_result = snd_soc_register_codec(p_tas2562->dev,
		&soc_codec_driver_tas2562,
		tas2562_dai_driver, ARRAY_SIZE(tas2562_dai_driver));

	return n_result;
}

int tas2562_deregister_codec(struct tas2562_priv *p_tas2562)
{
	snd_soc_unregister_codec(p_tas2562->dev);

	return 0;
}

void tas2562_load_config(struct tas2562_priv *p_tas2562)
{
	int ret = 0;

	p_tas2562->hw_reset(p_tas2562);
	msleep(20);
	p_tas2562->write(p_tas2562, channel_both, TAS2562_SOFTWARERESET,
			TAS2562_SOFTWARERESET_SOFTWARERESET_RESET);
	msleep(20);

	ret = tas2562_iv_slot_config(p_tas2562);
	if(ret < 0) {
		goto end;
	}

	tas2562_load_init(p_tas2562);
	tas2562_iv_enable(p_tas2562, tas2562iv_enable);

	ret = tas2562_set_slot(p_tas2562, p_tas2562->mn_slot_width);
	if (ret < 0)
		goto end;

	ret = tas2562_set_fmt(p_tas2562, p_tas2562->mn_asi_format);
	if (ret < 0)
		goto end;

	ret = tas2562_set_bitwidth(p_tas2562, p_tas2562->mn_pcm_format);
	if (ret < 0)
		goto end;

	ret = tas2562_set_samplerate(p_tas2562, p_tas2562->mn_sampling_rate);
	if (ret < 0)
		goto end;

	ret = tas2562_set_power_state(p_tas2562, channel_both,
			p_tas2562->mn_power_state);
	if (ret < 0)
		goto end;

end:
/* power up failed, restart later */
	if (ret < 0)
		schedule_delayed_work(&p_tas2562->irq_work,
				msecs_to_jiffies(1000));
}

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TAS2562 ALSA SOC Smart Amplifier driver");
MODULE_LICENSE("GPL v2");
#endif /* CONFIG_TAS2562_CODEC */
