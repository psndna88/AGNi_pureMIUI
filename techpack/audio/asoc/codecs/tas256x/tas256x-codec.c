/*
 * =============================================================================
 * Copyright (c) 2016  Texas Instruments Inc.
 * Copyright (C) 2021 XiaoMi, Inc.
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
 *     tas256x-codec.c
 *
 * Description:
 *     ALSA SoC driver for Texas Instruments TAS256X High Performance 4W Smart
 *     Amplifier
 *
 * =============================================================================
 */

#ifdef CONFIG_TAS256X_CODEC
#define DEBUG 5
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
#include <linux/version.h>

#include "tas256x.h"
#include "tas256x-device.h"
#ifdef CONFIG_TAS25XX_ALGO
#ifdef CONFIG_PLATFORM_EXYNOS
#include <sound/smart_amp.h>
#else
#include <dsp/tas_smart_amp_v2.h>
#endif /*CONFIG_PLATFORM_EXYNOS*/
#include "tas25xx-calib.h"
#endif /*CONFIG_TAS25XX_ALGO*/

#define TAS256X_MDELAY 0xFFFFFFFE
#define TAS256X_MSLEEP 0xFFFFFFFD
#define TAS256X_IVSENSER_ENABLE  1
#define TAS256X_IVSENSER_DISABLE 0
/* #define TAS2558_CODEC */


static char const *iv_enable_text[] = {"Off", "On"};

static const struct soc_enum tas256x_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(iv_enable_text), iv_enable_text),
};
static int tas256x_set_fmt(struct tas256x_priv *p_tas256x,
	unsigned int fmt);
static int tas256x_system_mute_ctrl_get(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue);
static int tas256x_system_mute_ctrl_put(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue);
static int tas256x_mute_ctrl_get(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue);
static int tas256x_mute_ctrl_put(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue);

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
static unsigned int tas256x_codec_read(struct snd_soc_component *codec,
		unsigned int reg)
{
	unsigned int value = 0;
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
	int ret = 0;

	switch (reg) {
	case TAS256X_LEFT_SWITCH:
		value = p_tas256x->devs[0]->spk_control;
		break;
	case TAS256X_RIGHT_SWITCH:
		value = p_tas256x->devs[1]->spk_control;
		break;
	case RX_SCFG_LEFT:
		value = p_tas256x->devs[0]->rx_cfg;
		break;
	case RX_SCFG_RIGHT:
		value = p_tas256x->devs[1]->rx_cfg;
		break;
	default:
		ret = p_tas256x->read(p_tas256x, channel_left, reg,
			&value);
		break;
	}

	dev_dbg(p_tas256x->dev, "%s, reg=%d, value=%d", __func__, reg, value);

	if (ret == 0)
		return value;
	else
		return ret;
}
#else
static unsigned int tas256x_codec_read(struct snd_soc_codec *codec,
		unsigned int reg)
{
	unsigned int value = 0;
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);

	int ret = 0;

	switch (reg) {
	case TAS256X_LEFT_SWITCH:
		value = p_tas256x->devs[0]->spk_control;
		break;
	case TAS256X_RIGHT_SWITCH:
		value = p_tas256x->devs[1]->spk_control;
		break;
	case RX_SCFG_LEFT:
		value = p_tas256x->devs[0]->rx_cfg;
		break;
	case RX_SCFG_RIGHT:
		value = p_tas256x->devs[1]->rx_cfg;
		break;
	default:
		ret = p_tas256x->read(p_tas256x, channel_left, reg,
			&value);
		break;
	}

	dev_dbg(p_tas256x->dev, "%s, reg=%d, value=%d", __func__, reg, value);

	if (ret == 0)
		return value;
	else
		return ret;

	return value;
}
#endif

static int tas256xiv_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec
				= snd_soc_kcontrol_component(kcontrol);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
#endif
	struct tas256x_priv *p_tas256x = NULL;
	int iv_enable = 0, n_result = 0;

	if (codec == NULL) {
		pr_err("%s:codec is NULL\n", __func__);
		return 0;
	}

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	if (p_tas256x == NULL) {
		pr_err("%s:p_tas256x is NULL\n", __func__);
		return 0;
	}

	iv_enable = ucontrol->value.integer.value[0];

	n_result = tas256x_iv_sense_enable_set(p_tas256x, iv_enable,
		channel_both);

	pr_debug("%s: tas256x->iv_enable = %d\n", __func__,
		p_tas256x->iv_enable);

	return n_result;
}

static int tas256xiv_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
#endif
	struct tas256x_priv *p_tas256x = NULL;

	if (codec == NULL) {
		pr_err("%s:codec is NULL\n", __func__);
		return 0;
	}

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	if (p_tas256x == NULL) {
		pr_err("%s:p_tas256x is NULL\n", __func__);
		return 0;
	}

	ucontrol->value.integer.value[0] =
		tas256x_iv_sense_enable_get(p_tas256x, channel_left);
	p_tas256x->iv_enable = ucontrol->value.integer.value[0];

	dev_info(p_tas256x->dev, "p_tas256x->iv_enable %d\n",
		p_tas256x->iv_enable);

	return 0;
}

static const struct snd_kcontrol_new tas256x_controls[] = {
SOC_ENUM_EXT("TAS256X IVSENSE ENABLE", tas256x_enum[0],
			tas256xiv_get, tas256xiv_put),
};

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
static int tas256x_codec_write(struct snd_soc_component *codec,
				unsigned int reg, unsigned int value)
{
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
	int ret = 0;

	dev_dbg(p_tas256x->dev, "%s: %d, %d", __func__, reg, value);

	switch (reg) {
	case TAS256X_LEFT_SWITCH:
		p_tas256x->devs[0]->spk_control = value;
		break;
	case TAS256X_RIGHT_SWITCH:
		p_tas256x->devs[1]->spk_control = value;
		break;
	case RX_SCFG_LEFT:
		ret = tas256x_update_rx_cfg(p_tas256x, value,
			channel_left);
		break;
	case RX_SCFG_RIGHT:
		ret = tas256x_update_rx_cfg(p_tas256x, value,
			channel_right);
		break;
	default:
		ret = p_tas256x->write(p_tas256x, channel_both,
			reg, value);
		break;
	}

	return ret;
}
#else
static int tas256x_codec_write(struct snd_soc_codec *codec,
				unsigned int reg, unsigned int value)
{
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	dev_dbg(p_tas256x->dev, "%s: %d, %d", __func__, reg, value);

	switch (reg) {
	case TAS256X_LEFT_SWITCH:
		p_tas256x->devs[0]->spk_control = value;
		break;
	case TAS256X_RIGHT_SWITCH:
		p_tas256x->devs[1]->spk_control = value;
		break;
	case RX_SCFG_LEFT:
		ret = tas256x_update_rx_cfg(p_tas256x, value,
			channel_left);
		break;
	case RX_SCFG_RIGHT:
		ret = tas256x_update_rx_cfg(p_tas256x, value,
			channel_right);
		break;
	default:
		ret = p_tas256x->write(p_tas256x, channel_both,
			reg, value);
		break;
	}

	return ret;
}
#endif

#ifdef CODEC_PM
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
static int tas256x_codec_suspend(struct snd_soc_component *codec)
{
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
	int ret = 0;

	mutex_lock(&p_tas256x->codec_lock);

	dev_dbg(p_tas256x->dev, "%s\n", __func__);
	p_tas256x->runtime_suspend(p_tas256x);

	mutex_unlock(&p_tas256x->codec_lock);
	return ret;
}

static int tas256x_codec_resume(struct snd_soc_component *codec)
{
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
	int ret = 0;

	mutex_lock(&p_tas256x->codec_lock);

	dev_dbg(p_tas256x->dev, "%s\n", __func__);
	p_tas256x->runtime_resume(p_tas256x);

	mutex_unlock(&p_tas256x->codec_lock);
	return ret;
}
#else
static int tas256x_codec_suspend(struct snd_soc_codec *codec)
{
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	mutex_lock(&p_tas256x->codec_lock);

	dev_dbg(p_tas256x->dev, "%s\n", __func__);
	p_tas256x->runtime_suspend(p_tas256x);

	mutex_unlock(&p_tas256x->codec_lock);
	return ret;
}

static int tas256x_codec_resume(struct snd_soc_codec *codec)
{
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	mutex_lock(&p_tas256x->codec_lock);

	dev_dbg(p_tas256x->dev, "%s\n", __func__);
	p_tas256x->runtime_resume(p_tas256x);

	mutex_unlock(&p_tas256x->codec_lock);
	return ret;
}
#endif
#endif

static int tas256x_set_power_state(struct tas256x_priv *p_tas256x,
			enum channel chn, int state)
{
	int n_result = 0, i = 0;

	if ((p_tas256x->mb_mute) && (state == TAS256X_POWER_ACTIVE))
		state = TAS256X_POWER_MUTE;
	dev_info(p_tas256x->dev, "set power state: %d\n", state);

	switch (state) {
	case TAS256X_POWER_ACTIVE:
		/* if set format was not called by asoc, then set it default */
		if (p_tas256x->mn_asi_format == 0)
			p_tas256x->mn_asi_format = SND_SOC_DAIFMT_CBS_CFS
				| SND_SOC_DAIFMT_IB_NF
				| SND_SOC_DAIFMT_I2S;
		n_result = tas256x_set_fmt(p_tas256x, p_tas256x->mn_asi_format);
		if (n_result < 0)
			return n_result;
		tas256x_iv_sense_enable_set(p_tas256x, 1, chn);
#ifdef CONFIG_TAS25XX_ALGO
		//tas25xx_send_algo_calibration();

		/*Moved to probe*/
		/*tas25xx_set_iv_bit_fomat (p_tas256x->mn_iv_width,
		 *p_tas256x->mn_vbat, 1);
		 */
#endif
/* Clear latched IRQ before power on */

		tas256x_interrupt_clear(p_tas256x, chn);

		/*Mask interrupt for TDM*/
		n_result = tas256x_interrupt_enable(p_tas256x, 0/*Disable*/,
			channel_both);
#ifdef CONFIG_TAS256X_BIN_PARSER
//		tas256x_select_cfg_blk(p_tas256x, 0, TAS256X_BIN_BLK_PRE_POWER_UP);//Normal
		tas256x_select_cfg_blk(p_tas256x, 1, TAS256X_BIN_BLK_PRE_POWER_UP);//Swap
		dev_info(p_tas256x->dev, "IRQ reg is: %s tas256x_select_cfg_blk %d\n",
			__func__, __LINE__);
		
#endif
		n_result = tas256x_set_power_up(p_tas256x, chn);

		pr_info("%s: set ICN to -80dB\n", __func__);
		n_result = tas256x_icn_data(p_tas256x, chn);
		p_tas256x->mb_power_up = true;
		p_tas256x->mn_power_state = TAS256X_POWER_ACTIVE;
		schedule_delayed_work(&p_tas256x->init_work,
				msecs_to_jiffies(50));
		break;

	case TAS256X_POWER_MUTE:
		n_result = tas256x_set_power_mute(p_tas256x, chn);
			p_tas256x->mb_power_up = true;
			p_tas256x->mn_power_state = TAS256X_POWER_MUTE;

		/*Mask interrupt for TDM*/
		n_result = tas256x_interrupt_enable(p_tas256x, 0/*Disable*/,
			chn);
		break;

	case TAS256X_POWER_SHUTDOWN:
		for (i = 0; i < p_tas256x->mn_channels; i++) {
			if (p_tas256x->devs[i]->device_id == DEVICE_TAS2564) {
				if (chn & (i+1)) {
					/*Mask interrupt for TDM*/
					n_result = tas256x_interrupt_enable(p_tas256x, 0/*Disable*/,
							i+1);
					n_result = tas256x_set_power_mute(p_tas256x, i+1);
					n_result = tas256x_iv_sense_enable_set(p_tas256x, 0,
						i+1);
					p_tas256x->mb_power_up = false;
					p_tas256x->mn_power_state = TAS256X_POWER_SHUTDOWN;
				}
			} else {
				n_result = tas256x_set_power_shutdown(p_tas256x, i+1);
				n_result = tas256x_iv_sense_enable_set(p_tas256x, 0,
					i+1);
				p_tas256x->mb_power_up = false;
				p_tas256x->mn_power_state = TAS256X_POWER_SHUTDOWN;
				/*Mask interrupt for TDM*/
				n_result = tas256x_interrupt_enable(p_tas256x, 0/*Disable*/,
					i+1);
			}
		}
		msleep(20);
		p_tas256x->enable_irq(p_tas256x, false);
#ifdef CONFIG_TAS25XX_ALGO
		//tas25xx_update_big_data();
#endif
		break;

	default:
		dev_err(p_tas256x->dev, "wrong power state setting %d\n",
				state);
	}

	return n_result;
}

void failsafe(struct tas256x_priv  *p_tas256x)
{
	int n_result;

	dev_err(p_tas256x->dev, "%s\n", __func__);
	p_tas256x->mn_err_code |= ERROR_FAILSAFE;

	if (p_tas256x->mnRestart < RESTART_MAX) {
		p_tas256x->mnRestart++;
		msleep(100);
		dev_err(p_tas256x->dev, "I2C COMM error, restart SmartAmp.\n");
		tas256x_set_power_state(p_tas256x, channel_both, p_tas256x->mn_power_state);
		return;
	}

	n_result = tas256x_set_power_shutdown(p_tas256x, channel_both);
	p_tas256x->mb_power_up = false;
	p_tas256x->mn_power_state = TAS256X_POWER_SHUTDOWN;
	msleep(20);
	/*Mask interrupt for TDM*/
	n_result = tas256x_interrupt_enable(p_tas256x, 0/*Disable*/,
		channel_both);
	p_tas256x->enable_irq(p_tas256x, false);
	p_tas256x->hw_reset(p_tas256x);
	p_tas256x->write(p_tas256x, channel_both, TAS256X_SOFTWARERESET,
		TAS256X_SOFTWARERESET_SOFTWARERESET_RESET);
	udelay(1000);
	/*pTAS256x->write(pTAS256x, channel_both, TAS256X_SPK_CTRL_REG, 0x04);*/
}

static int tas256x_dac_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = snd_soc_dapm_to_component(w->dapm);
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		dev_info(p_tas256x->dev, "SND_SOC_DAPM_POST_PMU\n");
		break;
	case SND_SOC_DAPM_PRE_PMD:
		dev_info(p_tas256x->dev, "SND_SOC_DAPM_PRE_PMD\n");
		break;
	}

	return 0;
}

static const char * const tas256x_ASI1_src[] = {
	"I2C offset", "Left", "Right", "LeftRightDiv2",
};

static SOC_ENUM_SINGLE_DECL(tas2562_ASI1_src_left_enum, RX_SCFG_LEFT, 0,
			    tas256x_ASI1_src);
static SOC_ENUM_SINGLE_DECL(tas2562_ASI1_src_right_enum, RX_SCFG_RIGHT, 0,
			    tas256x_ASI1_src);

static const struct snd_kcontrol_new dapm_switch_left =
	SOC_DAPM_SINGLE("Switch", TAS256X_LEFT_SWITCH, 0, 1, 0);
static const struct snd_kcontrol_new dapm_switch_right =
	SOC_DAPM_SINGLE("Switch", TAS256X_RIGHT_SWITCH, 0, 1, 0);
static const struct snd_kcontrol_new tas256x_asi1_left_mux =
	SOC_DAPM_ENUM("Mux", tas2562_ASI1_src_left_enum);
static const struct snd_kcontrol_new tas256x_asi1_right_mux =
	SOC_DAPM_ENUM("Mux", tas2562_ASI1_src_right_enum);

static const struct snd_soc_dapm_widget tas256x_dapm_widgets_stereo[] = {
	SND_SOC_DAPM_AIF_IN("ASI1", "ASI1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SWITCH("TAS256X ASI Left", SND_SOC_NOPM, 0, 0,
		&dapm_switch_left),
	SND_SOC_DAPM_SWITCH("TAS256X ASI Right", SND_SOC_NOPM, 0, 0,
		&dapm_switch_right),
	SND_SOC_DAPM_MUX("TAS256X ASI1 SEL LEFT", SND_SOC_NOPM, 0, 0,
		&tas256x_asi1_left_mux),
	SND_SOC_DAPM_MUX("TAS256X ASI1 SEL RIGHT", SND_SOC_NOPM, 0, 0,
		&tas256x_asi1_right_mux),
	SND_SOC_DAPM_AIF_OUT("Voltage Sense", "ASI1 Capture",  0,
		SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("Current Sense", "ASI1 Capture",  0,
		SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC_E("DAC1", NULL, SND_SOC_NOPM, 0, 0, tas256x_dac_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC_E("DAC2", NULL, SND_SOC_NOPM, 0, 0, tas256x_dac_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("OUT1"),
	SND_SOC_DAPM_OUTPUT("OUT2"),
	SND_SOC_DAPM_SIGGEN("VMON"),
	SND_SOC_DAPM_SIGGEN("IMON")
};

static const struct snd_soc_dapm_route tas256x_audio_map_stereo[] = {
	{"TAS256X ASI1 SEL LEFT", "Left", "ASI1"},
	{"TAS256X ASI1 SEL LEFT", "Right", "ASI1"},
	{"TAS256X ASI1 SEL LEFT", "LeftRightDiv2", "ASI1"},
	{"TAS256X ASI1 SEL RIGHT", "Left", "ASI1"},
	{"TAS256X ASI1 SEL RIGHT", "Right", "ASI1"},
	{"TAS256X ASI1 SEL RIGHT", "LeftRightDiv2", "ASI1"},
	{"DAC1", NULL, "TAS256X ASI1 SEL LEFT"},
	{"DAC2", NULL, "TAS256X ASI1 SEL RIGHT"},
	{"TAS256X ASI Left", "Switch", "DAC1"},
	{"TAS256X ASI Right", "Switch", "DAC2"},
	{"OUT1", NULL, "TAS256X ASI Left"},
	{"OUT2", NULL, "TAS256X ASI Right"},
	{"Voltage Sense", NULL, "VMON"},
	{"Current Sense", NULL, "IMON"}
};

static const struct snd_soc_dapm_widget tas256x_dapm_widgets_mono[] = {
	SND_SOC_DAPM_AIF_IN("ASI1", "ASI1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SWITCH("TAS256X ASI", SND_SOC_NOPM, 0, 0,
		&dapm_switch_left),
	SND_SOC_DAPM_MUX("TAS256X ASI1 SEL", SND_SOC_NOPM, 0, 0,
		&tas256x_asi1_left_mux),
	SND_SOC_DAPM_AIF_OUT("Voltage Sense", "ASI1 Capture",  1,
		TAS256X_POWERCONTROL, 2, 1),
	SND_SOC_DAPM_AIF_OUT("Current Sense", "ASI1 Capture",  0,
		TAS256X_POWERCONTROL, 3, 1),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0, tas256x_dac_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("OUT"),
	SND_SOC_DAPM_SIGGEN("VMON"),
	SND_SOC_DAPM_SIGGEN("IMON")
};

static const struct snd_soc_dapm_route tas256x_audio_map_mono[] = {
	{"TAS256X ASI1 SEL", "Left", "ASI1"},
	{"TAS256X ASI1 SEL", "Right", "ASI1"},
	{"TAS256X ASI1 SEL", "LeftRightDiv2", "ASI1"},
	{"DAC", NULL, "TAS256X ASI1 SEL"},
	{"TAS256X ASI", "Switch", "DAC"},
	{"OUT", NULL, "TAS256X ASI"},
	{"Voltage Sense", NULL, "VMON"},
	{"Current Sense", NULL, "IMON"}
};

static int tas256x_set_bitwidth(struct tas256x_priv *p_tas256x,
	int bitwidth, int stream)
{
	int slot_width_tmp = 16;

	dev_info(p_tas256x->dev, "%s %d\n", __func__, bitwidth);
	switch (bitwidth) {
	case SNDRV_PCM_FORMAT_S16_LE:
		p_tas256x->mn_ch_size = 16;
		slot_width_tmp = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		p_tas256x->mn_ch_size = 24;
		slot_width_tmp = 32;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		p_tas256x->mn_ch_size = 32;
		slot_width_tmp = 32;
		break;
	}

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		tas256x_rx_set_bitwidth(p_tas256x, p_tas256x->mn_ch_size);
		tas256x_rx_set_slot(p_tas256x, slot_width_tmp);
	} else { /*stream == SNDRV_PCM_STREAM_CAPTURE*/
		tas256x_iv_slot_config(p_tas256x);
		tas256x_iv_bitwidth_config(p_tas256x);
	}

	dev_info(p_tas256x->dev, "mn_ch_size: %d\n", p_tas256x->mn_ch_size);
	p_tas256x->mn_pcm_format = bitwidth;

	return 0;
}

static int tas256x_system_mute_ctrl_get(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec
					= snd_soc_kcontrol_component(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	pValue->value.integer.value[0] = p_tas256x->mb_mute;
	dev_dbg(p_tas256x->dev, "%s = %d\n",
		__func__, p_tas256x->mb_mute);

	return 0;
}

static int tas256x_system_mute_ctrl_put(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec =
		snd_soc_kcontrol_component(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	int mb_mute = pValue->value.integer.value[0];

	dev_dbg(p_tas256x->dev, "%s = %d\n", __func__, mb_mute);

	p_tas256x->mb_mute = !!mb_mute;

	return 0;
}

static int tas256x_mute_ctrl_get(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec =
		snd_soc_kcontrol_component(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	pValue->value.integer.value[0] = p_tas256x->mb_mute;

	if ((p_tas256x->mb_power_up == true) &&
		(p_tas256x->mn_power_state == TAS256X_POWER_ACTIVE))
		pValue->value.integer.value[0] = 0;
	else
		pValue->value.integer.value[0] = 1;

	dev_dbg(p_tas256x->dev, "%s = %ld\n",
		__func__, pValue->value.integer.value[0]);

	return 0;
}

static int tas256x_mute_ctrl_put(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec =
		snd_soc_kcontrol_component(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	enum channel chn = channel_left;
	int mute = pValue->value.integer.value[0];
	int i = 0, chnTemp = 0;

	dev_dbg(p_tas256x->dev, "%s, %d\n", __func__, mute);
	mutex_lock(&p_tas256x->codec_lock);

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (p_tas256x->devs[i]->spk_control == 1)
			chnTemp |= 1<<i;
	}
	chn = (chnTemp == 0) ? chn:(enum channel)chnTemp;

	if (mute) {
		tas256x_set_power_state(p_tas256x,
			chn, TAS256X_POWER_MUTE);
	} else {
		tas256x_set_power_state(p_tas256x, chn, TAS256X_POWER_ACTIVE);
	}
	mutex_unlock(&p_tas256x->codec_lock);
	return 0;
}

static int tas256x_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = dai->component;
	struct tas256x_priv *p_tas256x
			= snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = dai->codec;
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	/* int blr_clk_ratio; */
	int n_result = 0;

	dev_dbg(p_tas256x->dev, "%s, stream %s format: %d\n", __func__,
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? ("Playback") : ("Capture"),
		params_format(params));

	mutex_lock(&p_tas256x->codec_lock);

	n_result = tas256x_set_bitwidth(p_tas256x,
			params_format(params), substream->stream);
	if (n_result < 0) {
		dev_info(p_tas256x->dev, "set bitwidth failed, %d\n",
			n_result);
		goto ret;
	}

	dev_info(p_tas256x->dev, "%s, stream %s sample rate: %d\n", __func__,
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? ("Playback") : ("Capture"),
		params_rate(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		n_result = tas256x_set_samplerate(p_tas256x,
		params_rate(params));

ret:
	if (n_result < 0) {
		if (p_tas256x->mn_err_code &
			(ERROR_DEVA_I2C_COMM | ERROR_DEVB_I2C_COMM))
			failsafe(p_tas256x);
	}

	mutex_unlock(&p_tas256x->codec_lock);
	return n_result;
}

static int tas256x_set_fmt(struct tas256x_priv *p_tas256x,
	unsigned int fmt)
{
	u8 tdm_rx_start_slot = 0, asi_cfg_1 = 0;
	int ret = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		asi_cfg_1 = 0x00;
		break;
	default:
		dev_err(p_tas256x->dev, "ASI format master is not found\n");
		ret = -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		dev_info(p_tas256x->dev, "INV format: NBNF\n");
		asi_cfg_1 = 1; /* Rising */
		break;
	case SND_SOC_DAIFMT_IB_NF:
		dev_info(p_tas256x->dev, "INV format: IBNF\n");
		asi_cfg_1 = 0; /* Faling */
		break;
	default:
		dev_err(p_tas256x->dev, "ASI format Inverse is not found\n");
		ret = -EINVAL;
	}

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
	dev_err(p_tas256x->dev, "DAI Format is not found, fmt=0x%x\n", fmt);
	ret = -EINVAL;
		break;
	}

	ret = tas256x_rx_set_fmt(p_tas256x,
		asi_cfg_1, tdm_rx_start_slot);

	p_tas256x->mn_asi_format = fmt;

	if (ret < 0) {
		if (p_tas256x->mn_err_code &
			(ERROR_DEVA_I2C_COMM | ERROR_DEVB_I2C_COMM))
			failsafe(p_tas256x);
	}

	return 0;
}

static int tas256x_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = dai->component;
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = dai->codec;
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	int ret = 0;

	dev_dbg(p_tas256x->dev, "%s, format=0x%x\n", __func__, fmt);

	return ret;
}

static int tas256x_set_dai_tdm_slot(struct snd_soc_dai *dai,
		unsigned int tx_mask, unsigned int rx_mask,
		int slots, int slot_width)
{
	int ret = 0;
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = dai->component;
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = dai->codec;
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	dev_dbg(p_tas256x->dev, "%s, tx_mask:%d, rx_mask:%d",
		__func__, tx_mask, rx_mask);
	dev_dbg(p_tas256x->dev, "%s, slots:%d,slot_width:%d",
		__func__, slots, slot_width);

	p_tas256x->mn_slot_width = slot_width;

	if (rx_mask)
		ret = tas256x_rx_set_slot(p_tas256x, slot_width);
	else
		ret = tas256x_iv_slot_config(p_tas256x);

	if (ret < 0) {
		if (p_tas256x->mn_err_code & (ERROR_DEVA_I2C_COMM | ERROR_DEVB_I2C_COMM))
			failsafe(p_tas256x);
	}

	return ret;
}

static int tas256x_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = dai->component;
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = dai->codec;
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	enum channel chn = channel_left;
	int i = 0, chnTemp = 0;
	int ret = 0;

	dev_info(p_tas256x->dev, "%s =>stream %s mute %d\n", __func__,
		(stream == SNDRV_PCM_STREAM_PLAYBACK) ? ("Playback") : ("Capture"),
		mute);
	mutex_lock(&p_tas256x->codec_lock);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (mute) {
			ret = tas256x_set_power_state(p_tas256x,
				channel_both, TAS256X_POWER_SHUTDOWN);
		} else {
			for (i = 0; i < p_tas256x->mn_channels; i++) {
				if (p_tas256x->devs[i]->spk_control == 1)
					chnTemp |= 1<<i;
			}
			chn = (chnTemp == 0)?chn:(enum channel)chnTemp;
			ret = tas256x_set_power_state(p_tas256x, chn,
				TAS256X_POWER_ACTIVE);
		}
	}

	mutex_unlock(&p_tas256x->codec_lock);

	if (ret < 0) {
		if (p_tas256x->mn_err_code &
			(ERROR_DEVA_I2C_COMM | ERROR_DEVB_I2C_COMM))
			failsafe(p_tas256x);
	}

	return 0;
}

static void tas256x_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec = dai->component;
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = dai->codec;
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	int n_result = 0;
	int i = 0, chnTemp = 0;
	enum channel chn = channel_left;

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (p_tas256x->devs[i]->spk_control == 1)
			chnTemp |= 1<<i;
	}
	chn = (chnTemp == 0) ? chn:(enum channel)chnTemp;

	dev_dbg(p_tas256x->dev, "%s, stream %s\n", __func__,
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? ("Playback") : ("Capture"));
	if (p_tas256x->mb_power_up == true && substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		n_result = tas256x_set_power_state(p_tas256x, chn,
				TAS256X_POWER_SHUTDOWN);
}

static struct snd_soc_dai_ops tas256x_dai_ops = {
	.hw_params  = tas256x_hw_params,
	.set_fmt    = tas256x_set_dai_fmt,
	.set_tdm_slot = tas256x_set_dai_tdm_slot,
	.mute_stream = tas256x_mute_stream,
	.shutdown = tas256x_shutdown,
};

#define TAS256X_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
						SNDRV_PCM_FMTBIT_S20_3LE |\
						SNDRV_PCM_FMTBIT_S24_LE |\
						SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver tas256x_dai_driver[] = {
	{
		.name = "tas256x ASI1",
		.id = 0,
		.playback = {
			.stream_name    = "ASI1 Playback",
			.channels_min   = 2,
			.channels_max   = 2,
			.rates      = SNDRV_PCM_RATE_8000_192000,
			.formats    = TAS256X_FORMATS,
		},
		.capture = {
			.stream_name    = "ASI1 Capture",
			.channels_min   = 1,
			.channels_max   = 2,
			.rates          = SNDRV_PCM_RATE_8000_192000,
			.formats    = TAS256X_FORMATS,
		},
		.ops = &tas256x_dai_ops,
		.symmetric_rates = 1,
	},
};

static int tas256x_load_init(struct tas256x_priv *p_tas256x)
{
	int ret = 0, i;

	ret = tas256x_set_misc_config(p_tas256x, 0/*Ignored*/, channel_both);
	if (ret  < 0)
		goto end;
	ret = tas256x_set_tx_config(p_tas256x, 0/*Ignored*/, channel_both);
	if (ret  < 0)
		goto end;
	ret = tas256x_set_clock_config(p_tas256x, 0/*Ignored*/, channel_both);
	if (ret  < 0)
		goto end;
	/*ICN Improve Performance*/
	ret = tas256x_icn_config(p_tas256x, 0/*Ignored*/, channel_both);
	if (ret  < 0)
		goto end;
	/*Disable the HPF in Forward Path*/
	ret = tas256x_HPF_FF_Bypass(p_tas256x, 0/*Ignored*/, channel_both);
	if (ret  < 0)
		goto end;
	/*Disable the HPF in Reverse Path*/
	ret = tas256x_HPF_FB_Bypass(p_tas256x, 0/*Ignored*/, channel_both);
	if (ret  < 0)
		goto end;
	ret = tas256x_set_classH_config(p_tas256x, 0/*Ignored*/, channel_both);
	if (ret  < 0)
		goto end;

	/* To Overwrite the defaults */
	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (p_tas256x->devs[i]->dev_ops.tas_init)
			ret |= (p_tas256x->devs[i]->dev_ops.tas_init)(p_tas256x, i+1);
	}
end:
	return ret;
}

#ifdef CONFIG_TAS256X_BIN_PARSER
static char *fw_name = "tas256x_reg.bin";

const char* blocktype[5] = {
    "COEFF",
    "POST_POWER_UP",
    "PRE_SHUTDOWN",
    "PRE_POWER_UP",
    "POST_SHUTDOWN"
};

static int tas256x_process_block(void *pContext, unsigned char* data,
    unsigned char dev_idx, int sublocksize)
{
    struct tas256x_priv* pTAS256x = (struct tas256x_priv*)pContext;
    unsigned char subblk_typ = data[1];
    int subblk_offset = 2;
    enum channel chn = (0 == dev_idx) ? channel_both : (enum channel)dev_idx;

    switch (subblk_typ) {
    case TAS256X_CMD_SING_W: {
        int i = 0;
        unsigned short len = SMS_HTONS(data[2], data[3]);
        subblk_offset += 2;
        if (subblk_offset + 4 * len > sublocksize) {
            dev_err(pTAS256x->dev, "Out of memory %s: %u\n", __func__, __LINE__);
			break;
        } 
        
        for (i = 0; i < len; i++) {
            pTAS256x->write(pTAS256x, chn,
                TAS256X_REG(data[subblk_offset], data[subblk_offset + 1], data[subblk_offset + 2]), 
                data[subblk_offset + 3]);
            subblk_offset += 4;
        }
    }
                           break;
    case TAS256X_CMD_BURST: {
//        int i = 0;
        unsigned short len = SMS_HTONS(data[2], data[3]);
        subblk_offset += 2;
        if (subblk_offset + 4 + len > sublocksize) {
            dev_err(pTAS256x->dev, "Out of memory %s: %u\n", __func__, __LINE__);
			break;
        }
        if (len % 4) {
            dev_err(pTAS256x->dev, "Burst len is wrong\n");
            break;
        }

		pTAS256x->bulk_write(pTAS256x, chn,
            TAS256X_REG(data[subblk_offset], data[subblk_offset + 1],
            data[subblk_offset + 2]), &(data[subblk_offset + 4]), len);
        subblk_offset += (len + 4);
        
    }
                          break;
    case TAS256X_CMD_DELAY: {
        unsigned short delay_time = 0;
		if(subblk_offset + 2 > sublocksize) {
			dev_err(pTAS256x->dev, "Out of memory %s: %u\n", __func__, __LINE__);
			break;
		}
		delay_time = SMS_HTONS(data[2], data[3]);
		usleep_range(delay_time*1000, delay_time*1000);
        subblk_offset += 2;
    }
                          break;
    case TAS256X_CMD_FIELD_W:
        if (subblk_offset + 6 > sublocksize) {
            dev_err(pTAS256x->dev,"Out of memory %s: %u\n", __func__, __LINE__);
        }
        pTAS256x->update_bits(pTAS256x,chn,
            TAS256X_REG(data[subblk_offset + 2], data[subblk_offset + 3],data[subblk_offset + 4]), 
		data[subblk_offset + 1], data[subblk_offset + 5]);
        subblk_offset += 6;
        break;
    default:
        break;
    };

    return subblk_offset;

}

void tas256x_select_cfg_blk(void* pContext, int conf_no, unsigned char block_type)
{
	struct tas256x_priv* pTAS256x = (struct tas256x_priv*) pContext;
	struct tas256x_config_info** cfg_info = pTAS256x->cfg_info;
	int i = 0, j = 0, k = 0;

	if (conf_no > pTAS256x->ncfgs || conf_no < 0 || NULL == cfg_info) {
		dev_err(pTAS256x->dev, "ERROR!!!conf_no shoud be in range from 0 to %u\n", pTAS256x->ncfgs - 1);
		goto EXIT;
	}
	for (i = 0; i < pTAS256x->ncfgs; i++) {
		if (conf_no == i) {
			for (j = 0; j < (int)cfg_info[i]->real_nblocks; j++) {
				unsigned int length = 0, rc = 0;
				if (block_type > 5 || block_type < 2) {
					dev_err(pTAS256x->dev, "ERROR!!!block_type shoud be in range from 2 to 5\n");
					goto EXIT;
				}
				if (block_type != cfg_info[i]->blk_data[j]->block_type) continue;
				dev_info(pTAS256x->dev, "conf %d\n", i);
				dev_info(pTAS256x->dev,"\tblock type:%s\t device idx = 0x%02x\n",
					blocktype[cfg_info[i]->blk_data[j]->block_type - 1],
					cfg_info[i]->blk_data[j]->dev_idx);
				for (k = 0; k < (int)cfg_info[i]->blk_data[j]->nSublocks; k++) {
					rc= tas256x_process_block(pTAS256x, cfg_info[i]->blk_data[j]->regdata + length,
						cfg_info[i]->blk_data[j]->dev_idx, cfg_info[i]->blk_data[j]->block_size - length);
					length += rc;
					if (cfg_info[i]->blk_data[j]->block_size < length) {
						dev_err(pTAS256x->dev, "%s:%u:ERROR:%u %u out of memory\n", __func__, __LINE__, 
							length, cfg_info[i]->blk_data[j]->block_size);
						break;
					}
				}
				if (length != cfg_info[i]->blk_data[j]->block_size) {
					dev_err(pTAS256x->dev, "%s:%u:ERROR: %u %u size is not same\n", __func__,
						__LINE__, length, cfg_info[i]->blk_data[j]->block_size);
				}
			}
		}
		else continue;
	}
EXIT:
	return;
}

static struct tas256x_config_info* tas256x_add_config(unsigned char* config_data, unsigned int config_size)
{
    struct tas256x_config_info* cfg_info = NULL;
    int config_offset = 0, i = 0;
    cfg_info = (struct tas256x_config_info*)kzalloc(sizeof(struct tas256x_config_info), GFP_KERNEL);
    if (!cfg_info) {
        pr_err("%s:%u:Memory alloc failed!\n", __func__, __LINE__);
        goto EXIT;
    }
    if (config_offset + 4 > (int)config_size) {
        pr_err("%s:%u:Out of memory\n", __func__, __LINE__);
        goto EXIT;
    }
    cfg_info->nblocks = SMS_HTONL(config_data[config_offset], config_data[config_offset+1], 
        config_data[config_offset+2], config_data[config_offset+3]);
    config_offset +=  4;
    pr_info("cfg_info->num_blocks = %u\n", cfg_info->nblocks);
    cfg_info->blk_data = (struct tas256x_block_data**)kzalloc(
		cfg_info->nblocks*sizeof(struct tas256x_block_data*), 
		GFP_KERNEL);
    if (!cfg_info->blk_data) {
        pr_err("%s:%u:Memory alloc failed!\n", __func__, __LINE__);
        goto EXIT;
    }
    cfg_info->real_nblocks = 0;
    for (i = 0; i < (int)cfg_info->nblocks; i++) {
        if (config_offset + 12 > config_size) {
            pr_err("%s:%u:Out of memory: i = %d nblocks = %u!\n", 
                __func__, __LINE__, i, cfg_info->nblocks);
            break;
        }
        cfg_info->blk_data[i] = (struct tas256x_block_data*)kzalloc(
            sizeof(struct tas256x_block_data), GFP_KERNEL);
        if(!cfg_info->blk_data[i]) {
            pr_err("%s:%u:Memory alloc failed!\n", __func__, __LINE__);
            break;
        }
        cfg_info->blk_data[i]->dev_idx = config_data[config_offset];
        config_offset++;
        pr_info("blk_data(%d).dev_idx = 0x%02x\n", i, 
            cfg_info->blk_data[i]->dev_idx);
        cfg_info->blk_data[i]->block_type = config_data[config_offset];
        config_offset++;
        pr_info("blk_data(%d).block_type = 0x%02x\n", i, 
            cfg_info->blk_data[i]->block_type);
        cfg_info->blk_data[i]->yram_checksum = SMS_HTONS(config_data[config_offset], 
            config_data[config_offset+1]);
        config_offset += 2;
        cfg_info->blk_data[i]->block_size = SMS_HTONL(config_data[config_offset], 
            config_data[config_offset + 1], config_data[config_offset + 2], 
            config_data[config_offset + 3]);
        config_offset += 4;
        pr_info("blk_data(%d).block_size = %u\n", i,
            cfg_info->blk_data[i]->block_size);
        cfg_info->blk_data[i]->nSublocks = SMS_HTONL(config_data[config_offset],
            config_data[config_offset + 1], config_data[config_offset + 2],
            config_data[config_offset + 3]);
        pr_info("blk_data(%d).num_subblocks = %u\n", i, 
            cfg_info->blk_data[i]->nSublocks);
        config_offset += 4;
        pr_info("config_offset = %d\n", config_offset);
        cfg_info->blk_data[i]->regdata = (unsigned char*)kzalloc( 
            cfg_info->blk_data[i]->block_size, GFP_KERNEL);
        if(!cfg_info->blk_data[i]->regdata) {
            pr_err("%s:%u:Memory alloc failed!\n", __func__, __LINE__);
            goto EXIT;
        }
        if (config_offset + cfg_info->blk_data[i]->block_size > config_size) {
            pr_err("%s:%u:Out of memory: i = %d nblocks = %u!\n",
                __func__, __LINE__, i, cfg_info->nblocks);
            break;
        }
        memcpy(cfg_info->blk_data[i]->regdata, &config_data[config_offset], 
            cfg_info->blk_data[i]->block_size);
        config_offset += cfg_info->blk_data[i]->block_size;
        cfg_info->real_nblocks += 1;
    }
    
EXIT:
    return cfg_info;
}

static void tas256x_fw_ready(const struct firmware* pFW, void* pContext)
{
    struct tas256x_priv* pTAS256x = (struct tas256x_priv*) pContext;
    struct tas256x_fw_hdr* fw_hdr = &(pTAS256x->fw_hdr);
    struct tas256x_config_info** cfg_info = NULL;
    unsigned char* buf = NULL;
    int offset = 0, i = 0;
    unsigned int total_config_sz = 0;

    pTAS256x->fw_state = TAS256X_DSP_FW_FAIL;

    if (unlikely(!pFW)||unlikely(!pFW->data)) {
        dev_err(pTAS256x->dev,"Failed to read %s, no side-effect on driver running\n", fw_name);
        return;
    }
	buf = (unsigned char*)pFW->data;
#ifdef CONFIG_TAS256X_CODEC
	mutex_lock(&pTAS256x->codec_lock);
#endif
#ifdef CONFIG_TAS256X_MISC
	mutex_lock(&pTAS256x->file_lock);
#endif
	dev_info(pTAS256x->dev,"%s: start\n", __func__);
    fw_hdr->img_sz = SMS_HTONL(buf[offset], buf[offset + 1], buf[offset + 2], buf[offset + 3]);
    offset += 4;
    if (fw_hdr->img_sz != pFW->size) {
        dev_err(pTAS256x->dev,"File size not match, %d %d", (int)pFW->size, (int)fw_hdr->img_sz);
        goto EXIT;
    }

    fw_hdr->checksum = SMS_HTONL(buf[offset], buf[offset + 1], buf[offset + 2], buf[offset + 3]);
    offset += 4;
    fw_hdr->binnary_version_num = SMS_HTONL(buf[offset], buf[offset + 1], buf[offset + 2], buf[offset + 3]);
    offset += 4;
    fw_hdr->drv_fw_version = SMS_HTONL(buf[offset], buf[offset + 1], buf[offset + 2], buf[offset + 3]);
    offset += 4;
    fw_hdr->timestamp = SMS_HTONL(buf[offset], buf[offset + 1], buf[offset + 2], buf[offset + 3]);
    offset += 4;
    fw_hdr->plat_type = buf[offset];
    offset += 1;
    fw_hdr->dev_family = buf[offset];
    offset += 1;
    fw_hdr->reserve = buf[offset];
    offset += 1;
    fw_hdr->ndev = buf[offset];
    offset += 1;

    dev_info(pTAS256x->dev,"ndev = %u\n", fw_hdr->ndev);

    if (offset + TAS256X_DEVICE_SUM > fw_hdr->img_sz) {
        dev_err(pTAS256x->dev,"%s:%u:Out of Memory!\n", __func__, __LINE__);
        goto EXIT;
    }

    for (i = 0; i < TAS256X_DEVICE_SUM; i++) {
        fw_hdr->devs[i] = buf[offset];
        offset += 1;
        dev_info(pTAS256x->dev,"devs[%d] = %u\n", i, fw_hdr->devs[i]);
    }
    fw_hdr->nconfig = SMS_HTONL(buf[offset], buf[offset + 1], buf[offset + 2], buf[offset + 3]);
    offset += 4;
    dev_info(pTAS256x->dev,"nconfig = %u\n", fw_hdr->nconfig);
    for (i = 0; i < TAS256X_CONFIG_SIZE; i++) {
        fw_hdr->config_size[i] = SMS_HTONL(buf[offset], buf[offset + 1], buf[offset + 2], buf[offset + 3]);
        offset += 4;
        dev_info(pTAS256x->dev,"config_size[%d] = %u\n", i, fw_hdr->config_size[i]);
        total_config_sz += fw_hdr->config_size[i];
    }
    dev_info(pTAS256x->dev,"img_sz = %u total_config_sz = %u offset = %d\n",
        fw_hdr->img_sz, total_config_sz, offset);
    if (fw_hdr->img_sz - total_config_sz != (unsigned int)offset) {
        dev_err(pTAS256x->dev,"Bin file error!\n");
        goto EXIT;
    }
    cfg_info = (struct tas256x_config_info**)kzalloc(
    	fw_hdr->nconfig*sizeof(struct tas256x_config_info*),
    	GFP_KERNEL);
    if (!cfg_info) {
        dev_err(pTAS256x->dev,"%s:%u:Memory alloc failed!\n", __func__, __LINE__);
        goto EXIT;
    }
    pTAS256x->cfg_info = cfg_info;
    pTAS256x->ncfgs = 0;
    for (i = 0; i < (int)fw_hdr->nconfig; i++) {
        cfg_info[i] = tas256x_add_config(&buf[offset], fw_hdr->config_size[i]);
        if (!cfg_info[i]) {
            dev_err(pTAS256x->dev,"%s:%u:Memory alloc failed!\n", __func__, __LINE__);
            break;
        }
        offset += (int)fw_hdr->config_size[i];
        pTAS256x->ncfgs += 1;
    }

    pTAS256x->fw_state = TAS256X_DSP_FW_OK;

EXIT:
#ifdef CONFIG_TAS256X_MISC
	mutex_unlock(&pTAS256x->file_lock);
#endif
#ifdef CONFIG_TAS256X_CODEC
	mutex_unlock(&pTAS256x->codec_lock);
#endif
	release_firmware(pFW);
	dev_info(pTAS256x->dev,"%s: Firmware init complete\n", __func__);
    return;
}

static int tas256x_load_container(struct tas256x_priv* pTAS256x)
{
	pTAS256x->fw_state = TAS256X_DSP_FW_PENDING;
	return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
		fw_name, pTAS256x->dev, GFP_KERNEL, pTAS256x, tas256x_fw_ready);
}
static void tas256x_config_info_remove(void* pContext)
{
	struct tas256x_priv* pTAS256x = (struct tas256x_priv*) pContext;
	struct tas256x_config_info** cfg_info = pTAS256x->cfg_info;
	int i = 0, j = 0;

	if (cfg_info) {
		for (i = 0; i < pTAS256x->ncfgs; i++) {
			if (cfg_info[i]) {
				for (j = 0; j < (int)cfg_info[i]->real_nblocks; j++) {
					if (cfg_info[i]->blk_data[j]->regdata)
						kfree(cfg_info[i]->blk_data[j]->regdata);
					if (cfg_info[i]->blk_data[j]) kfree(cfg_info[i]->blk_data[j]);
				}
				if (cfg_info[i]->blk_data) kfree(cfg_info[i]->blk_data);
				kfree(cfg_info[i]);
			}
		}
		kfree(cfg_info);
	}
}
#endif

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
static int tas256x_codec_probe(struct snd_soc_component *codec)
{
	int ret, i;
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);

	ret = snd_soc_add_component_controls(codec, tas256x_controls,
					 ARRAY_SIZE(tas256x_controls));
	if (ret < 0) {
		pr_err("%s: add_codec_controls failed, err %d\n",
			__func__, ret);
		return ret;
	}

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (p_tas256x->devs[i]->dev_ops.tas_probe)
			ret |= (p_tas256x->devs[i]->dev_ops.tas_probe)(p_tas256x, codec, i+1);
	}

	ret = tas256x_load_init(p_tas256x);
	if (ret < 0)
		goto end;
	ret = tas256x_iv_sense_enable_set(p_tas256x, 1, channel_both);
	if (ret < 0)
		goto end;
#ifdef CONFIG_TAS25XX_ALGO
	tas_smartamp_add_algo_controls(codec);
	/*Send IV Vbat format but don't update to algo yet*/
	tas25xx_set_iv_bit_fomat(p_tas256x->mn_iv_width,
		p_tas256x->mn_vbat, 0);
#endif
#ifdef CONFIG_TAS256X_BIN_PARSER
	ret = tas256x_load_container(p_tas256x);
	dev_info(p_tas256x->dev,"%s Bin file loading requested: %d\n", __func__, ret);
#endif
	dev_dbg(p_tas256x->dev, "%s\n", __func__);

end:
	if (ret < 0) {
		if (p_tas256x->mn_err_code & (ERROR_DEVA_I2C_COMM | ERROR_DEVB_I2C_COMM))
			failsafe(p_tas256x);
	}
	return ret;
}
#else
static int tas256x_codec_probe(struct snd_soc_codec *codec)
{
	int ret, i;
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);

	dev_info(p_tas256x->dev, "Driver Tag: %s\n", TAS256X_DRIVER_TAG);
	ret = snd_soc_add_codec_controls(codec, tas256x_controls,
					 ARRAY_SIZE(tas256x_controls));
	if (ret < 0) {
		pr_err("%s: add_codec_controls failed, err %d\n",
			__func__, ret);
		return ret;
	}

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (p_tas256x->devs[i]->dev_ops.tas_probe)
			ret |= (p_tas256x->devs[i]->dev_ops.tas_probe)(p_tas256x, codec, i+1);
	}

	ret = tas256x_load_init(p_tas256x);
	if (ret < 0)
		goto end;
	ret = tas256x_iv_sense_enable_set(p_tas256x, 1, channel_both);
	if (ret < 0)
		goto end;
#ifdef CONFIG_TAS25XX_ALGO
	tas_smartamp_add_algo_controls(codec);
	/*Send IV Vbat format but don't update to algo yet*/
	tas25xx_set_iv_bit_fomat(p_tas256x->mn_iv_width,
		p_tas256x->mn_vbat, 0);
#endif
#ifdef CONFIG_TAS256X_BIN_PARSER
	ret = tas256x_load_container(p_tas256x);
	dev_info(p_tas256x->dev,"%s Bin file loading requested: %d\n", __func__, ret);
#endif
	dev_dbg(p_tas256x->dev, "%s\n", __func__);

end:
	if (ret < 0) {
		if (p_tas256x->mn_err_code & (ERROR_DEVA_I2C_COMM | ERROR_DEVB_I2C_COMM))
			failsafe(p_tas256x);
	}

	return ret;
}
#endif

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
static void tas256x_codec_remove(struct snd_soc_component *codec)
{
#ifdef CONFIG_TAS256X_BIN_PARSER
	struct tas2562_priv *pTAS256x = snd_soc_component_get_drvdata(codec);
	tas256x_config_info_remove(pTAS256x);
#endif
}
#else
static int tas256x_codec_remove(struct snd_soc_codec *codec)
{
#ifdef CONFIG_TAS256X_BIN_PARSER
	struct tas256x_priv *pTAS256x = snd_soc_codec_get_drvdata(codec);
	//struct tas2562_priv *pTAS256x = snd_soc_component_get_drvdata(codec);
	tas256x_config_info_remove(pTAS256x);
#endif
	return 0;
}
#endif

static DECLARE_TLV_DB_SCALE(tas256x_digital_tlv, 1100, 50, 0);

static const char * const icn_sw_text[] = {"Enable", "Disable"};
static const struct soc_enum icn_sw_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(icn_sw_text),
	icn_sw_text),
};
static int tas256x_get_icn_switch(struct snd_kcontrol *pKcontrol,
				struct snd_ctl_elem_value *p_u_control)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec =
		snd_soc_kcontrol_component(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	dev_info(p_tas256x->dev, "%s, icn_sw = %ld\n",
			__func__, p_u_control->value.integer.value[0]);
	p_u_control->value.integer.value[0] = p_tas256x->icn_sw;
	return 0;
}
static int tas256x_set_icn_switch(struct snd_kcontrol *pKcontrol,
				struct snd_ctl_elem_value *p_u_control)
{
	int ret  = 0;
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec =
		snd_soc_kcontrol_component(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	p_tas256x->icn_sw = p_u_control->value.integer.value[0];
	ret = tas256x_icn_enable(p_tas256x, p_tas256x->icn_sw, channel_both);

	return ret;
}


static int tas256x_dac_mute_ctrl_get(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	 struct snd_soc_component *codec =
		snd_soc_kcontrol_component(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
    #else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
    #endif
	pValue->value.integer.value[0] = p_tas256x->dac_mute;

	dev_dbg(p_tas256x->dev, "%s = %ld\n",
		__func__, pValue->value.integer.value[0]);

	return 0;
}

static int tas256x_dac_mute_ctrl_put(struct snd_kcontrol *pKcontrol,
	struct snd_ctl_elem_value *pValue)
{
	int n_result = 0;
	enum channel chn = channel_left;
	int mute = pValue->value.integer.value[0];
	int i = 0, chnTemp = 0;
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	struct snd_soc_component *codec =
		snd_soc_kcontrol_component(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_component_get_drvdata(codec);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
	struct tas256x_priv *p_tas256x = snd_soc_codec_get_drvdata(codec);
#endif
	dev_dbg(p_tas256x->dev, "%s, %d\n", __func__, mute);
	mutex_lock(&p_tas256x->codec_lock);

	for (i = 0; i < p_tas256x->mn_channels; i++) {
		if (p_tas256x->devs[i]->spk_control == 1)
			chnTemp |= 1<<i;
	}
	chn = (chnTemp == 0)?chn:(enum channel)chnTemp;

	if (mute)
		n_result = tas256x_set_power_mute(p_tas256x, chn);
	else
		n_result = tas256x_set_power_up(p_tas256x, chn);

	p_tas256x->dac_mute = mute;
	mutex_unlock(&p_tas256x->codec_lock);

	return n_result;
}

static const struct snd_kcontrol_new tas256x_snd_controls[] = {
	SOC_SINGLE_TLV("Amp Output Level", TAS256X_PLAYBACKCONFIGURATIONREG0,
		1, 0x16, 0,
		tas256x_digital_tlv),
	SOC_SINGLE_EXT("SmartPA System Mute", SND_SOC_NOPM, 0, 0x0001, 0,
		tas256x_system_mute_ctrl_get, tas256x_system_mute_ctrl_put),
	SOC_SINGLE_EXT("SmartPA Mute", SND_SOC_NOPM, 0, 0x0001, 0,
		tas256x_mute_ctrl_get, tas256x_mute_ctrl_put),
	SOC_SINGLE_EXT("TAS256X DAC Mute", SND_SOC_NOPM, 0, 0x0001, 0,
		tas256x_dac_mute_ctrl_get, tas256x_dac_mute_ctrl_put),
	SOC_ENUM_EXT("TAS256X ICN Switch", icn_sw_enum[0],
		tas256x_get_icn_switch,
		tas256x_set_icn_switch),
};

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
static const struct snd_soc_component_driver soc_codec_driver_tas256x = {
	.probe			= tas256x_codec_probe,
	.remove			= tas256x_codec_remove,
	.read			= tas256x_codec_read,
	.write			= tas256x_codec_write,
#ifdef CODEC_PM
	.suspend		= tas256x_codec_suspend,
	.resume			= tas256x_codec_resume,
#endif
	.controls		= tas256x_snd_controls,
	.num_controls		= ARRAY_SIZE(tas256x_snd_controls),
	.dapm_widgets		= tas256x_dapm_widgets_mono,
	.num_dapm_widgets	= ARRAY_SIZE(tas256x_dapm_widgets_mono),
	.dapm_routes		= tas256x_audio_map_stereo,
	.num_dapm_routes	= ARRAY_SIZE(tas256x_audio_map_stereo),
};
#else
static struct snd_soc_codec_driver soc_codec_driver_tas256x = {
	.probe			= tas256x_codec_probe,
	.remove			= tas256x_codec_remove,
	.read			= tas256x_codec_read,
	.write			= tas256x_codec_write,
#ifdef CODEC_PM
	.suspend		= tas256x_codec_suspend,
	.resume			= tas256x_codec_resume,
#endif
	.component_driver = {
		.controls		= tas256x_snd_controls,
		.num_controls		= ARRAY_SIZE(tas256x_snd_controls),
		.dapm_widgets		= tas256x_dapm_widgets_mono,
		.num_dapm_widgets	= ARRAY_SIZE(tas256x_dapm_widgets_mono),
		.dapm_routes		= tas256x_audio_map_mono,
		.num_dapm_routes	= ARRAY_SIZE(tas256x_audio_map_mono),
	},
};
#endif

int tas256x_register_codec(struct tas256x_priv *p_tas256x)
{
	int n_result = 0;

	dev_info(p_tas256x->dev, "%s, enter\n", __func__);

	if (p_tas256x->mn_channels == 2) {
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
		soc_codec_driver_tas256x.dapm_widgets =
			tas256x_dapm_widgets_stereo;
		soc_codec_driver_tas256x.num_dapm_widgets =
			ARRAY_SIZE(tas256x_dapm_widgets_stereo);
		soc_codec_driver_tas256x.dapm_routes =
			tas256x_audio_map_stereo;
		soc_codec_driver_tas256x.num_dapm_routes =
			ARRAY_SIZE(tas256x_audio_map_stereo);
#else
		soc_codec_driver_tas256x.component_driver.dapm_widgets =
			tas256x_dapm_widgets_stereo;
		soc_codec_driver_tas256x.component_driver.num_dapm_widgets =
			ARRAY_SIZE(tas256x_dapm_widgets_stereo);
		soc_codec_driver_tas256x.component_driver.dapm_routes =
			tas256x_audio_map_stereo;
		soc_codec_driver_tas256x.component_driver.num_dapm_routes =
			ARRAY_SIZE(tas256x_audio_map_stereo);
#endif
	}

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	n_result = devm_snd_soc_register_component(p_tas256x->dev,
		&soc_codec_driver_tas256x,
		tas256x_dai_driver, ARRAY_SIZE(tas256x_dai_driver));
#else
	n_result = snd_soc_register_codec(p_tas256x->dev,
		&soc_codec_driver_tas256x,
		tas256x_dai_driver, ARRAY_SIZE(tas256x_dai_driver));
#endif
	return n_result;
}

int tas256x_deregister_codec(struct tas256x_priv *p_tas256x)
{
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	snd_soc_unregister_component(p_tas256x->dev);
#else
	snd_soc_unregister_codec(p_tas256x->dev);
#endif
	return 0;
}

int tas256x_load_ctrl_values(struct tas256x_priv *p_tas256x, int ch)
{
	int n_result = 0;

	n_result |= tas256x_update_playback_volume(p_tas256x,
			p_tas256x->devs[ch-1]->dvc_pcm, ch);

	n_result |= tas256x_update_lim_max_attenuation(p_tas256x,
			p_tas256x->devs[ch-1]->lim_max_attn, ch);

	n_result |= tas256x_update_lim_max_thr(p_tas256x,
			p_tas256x->devs[ch-1]->lim_thr_max, ch);

	n_result |= tas256x_update_lim_min_thr(p_tas256x,
			p_tas256x->devs[ch-1]->lim_thr_min, ch);

	n_result |= tas256x_update_lim_inflection_point(p_tas256x,
			p_tas256x->devs[ch-1]->lim_infl_pt, ch);

	n_result |= tas256x_update_lim_slope(p_tas256x,
			p_tas256x->devs[ch-1]->lim_trk_slp, ch);

	n_result |= tas256x_update_bop_thr(p_tas256x,
			p_tas256x->devs[ch-1]->bop_thd, ch);

	n_result |= tas256x_update_bosd_thr(p_tas256x,
			p_tas256x->devs[ch-1]->bosd_thd, ch);

	n_result |= tas256x_update_boost_voltage(p_tas256x,
			p_tas256x->devs[ch-1]->bst_vltg, ch);

	n_result |= tas256x_update_current_limit(p_tas256x,
			p_tas256x->devs[ch-1]->bst_ilm, ch);

	n_result |= tas256x_update_limiter_enable(p_tas256x,
			p_tas256x->devs[ch-1]->lim_switch, ch);

	n_result |= tas256x_update_limiter_attack_rate(p_tas256x,
			p_tas256x->devs[ch-1]->lim_att_rate, ch);

	n_result |= tas256x_update_limiter_attack_step_size(p_tas256x,
			p_tas256x->devs[ch-1]->lim_att_stp_size, ch);

	n_result |= tas256x_update_limiter_release_rate(p_tas256x,
			p_tas256x->devs[ch-1]->lim_rel_rate, ch);

	n_result |= tas256x_update_limiter_release_step_size(p_tas256x,
			p_tas256x->devs[ch-1]->lim_rel_stp_size, ch);

	n_result |= tas256x_update_bop_enable(p_tas256x,
			p_tas256x->devs[ch-1]->bop_enable, ch);

	n_result |= tas256x_update_bop_mute(p_tas256x,
			p_tas256x->devs[ch-1]->bop_mute, ch);

	n_result |= tas256x_update_bop_shutdown_enable(p_tas256x,
			p_tas256x->devs[ch-1]->bosd_enable, ch);

	n_result |= tas256x_update_bop_attack_rate(p_tas256x,
			p_tas256x->devs[ch-1]->bop_att_rate, ch);

	n_result |= tas256x_update_bop_attack_step_size(p_tas256x,
			p_tas256x->devs[ch-1]->bop_att_stp_size, ch);

	n_result |= tas256x_update_bop_hold_time(p_tas256x,
			p_tas256x->devs[ch-1]->bop_hld_time, ch);

	n_result |= tas256x_update_vbat_lpf(p_tas256x,
			p_tas256x->devs[ch-1]->vbat_lpf, ch);

	n_result |= tas256x_update_rx_cfg(p_tas256x,
			p_tas256x->devs[ch-1]->rx_cfg, ch);

	/* TAS2564 Specific*/
	if (p_tas256x->devs[ch-1]->device_id == DEVICE_TAS2564)
		n_result |= tas2564_rx_mode_update(p_tas256x,
			p_tas256x->devs[ch-1]->rx_mode, ch);

	return n_result;
}

void tas256x_load_config(struct tas256x_priv *p_tas256x)
{
	int ret = 0;

	p_tas256x->hw_reset(p_tas256x);
	msleep(20);

	ret = tas56x_software_reset(p_tas256x, channel_both);
	if (ret < 0)
		goto end;
	ret = tas256x_iv_slot_config(p_tas256x);
	if (ret < 0)
		goto end;
	ret = tas256x_load_init(p_tas256x);
	if (ret < 0)
		goto end;	
	ret = tas256x_update_rx_cfg(p_tas256x, p_tas256x->devs[0]->rx_cfg,
			channel_left);
	if (ret < 0)
		goto end;
	ret |= tas256x_load_ctrl_values(p_tas256x, channel_left);
	if (ret < 0)
		goto end;
	if (p_tas256x->mn_channels == 2) {
		ret = tas256x_update_rx_cfg(p_tas256x, p_tas256x->devs[1]->rx_cfg,
			channel_right);
		if (ret < 0)
			goto end;
		ret |= tas256x_load_ctrl_values(p_tas256x, channel_right);
		if (ret < 0)
			goto end;
	}
	ret = tas256x_rx_set_slot(p_tas256x, p_tas256x->mn_slot_width);
	if (ret < 0)
		goto end;
	ret = tas256x_set_fmt(p_tas256x, p_tas256x->mn_asi_format);
	if (ret < 0)
		goto end;
	ret = tas256x_set_bitwidth(p_tas256x, p_tas256x->mn_pcm_format,
			SNDRV_PCM_STREAM_PLAYBACK);
	if (ret < 0)
		goto end;
	ret = tas256x_set_bitwidth(p_tas256x, p_tas256x->mn_pcm_format,
			SNDRV_PCM_STREAM_CAPTURE);
	if (ret < 0)
		goto end;
	ret = tas256x_set_samplerate(p_tas256x, p_tas256x->mn_sampling_rate);
	if (ret < 0)
		goto end;
	ret = tas256x_set_power_state(p_tas256x, channel_both,
			p_tas256x->mn_power_state);
	if (ret < 0)
		goto end;

end:
/* power up failed, restart later */
	if (ret < 0) {
		if (p_tas256x->mn_err_code &
			(ERROR_DEVA_I2C_COMM | ERROR_DEVB_I2C_COMM))
			failsafe(p_tas256x);
	}
}

void tas_reload(struct tas256x_priv *p_tas256x, int chn)
{
	int ret = 0;
	/*To be used later*/
	(void)chn;

	p_tas256x->enable_irq(p_tas256x, false);

	ret = tas56x_software_reset(p_tas256x, channel_both);
	if (ret < 0)
		goto end;
	ret = tas256x_load_init(p_tas256x);
	if (ret < 0)
		goto end;
	ret |= tas256x_load_ctrl_values(p_tas256x, chn);
	if (ret < 0)
		goto end;
	ret = tas256x_rx_set_slot(p_tas256x, p_tas256x->mn_slot_width);
	if (ret < 0)
		goto end;
	ret = tas256x_set_fmt(p_tas256x, p_tas256x->mn_asi_format);
	if (ret < 0)
		goto end;
	ret = tas256x_set_bitwidth(p_tas256x, p_tas256x->mn_pcm_format,
			SNDRV_PCM_STREAM_PLAYBACK);
	if (ret < 0)
		goto end;
	ret = tas256x_set_bitwidth(p_tas256x, p_tas256x->mn_pcm_format,
			SNDRV_PCM_STREAM_CAPTURE);
	if (ret < 0)
		goto end;
	ret = tas256x_set_samplerate(p_tas256x, p_tas256x->mn_sampling_rate);
	if (ret < 0)
		goto end;
	ret = tas256x_set_power_state(p_tas256x, channel_both,
		p_tas256x->mn_power_state);
	if (ret < 0)
		goto end;

end:
/* power up failed, restart later */
	if (ret < 0) {
		if (p_tas256x->mn_err_code &
			(ERROR_DEVA_I2C_COMM | ERROR_DEVB_I2C_COMM))
			failsafe(p_tas256x);
	}

	p_tas256x->enable_irq(p_tas256x, true);
}

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TAS256X ALSA SOC Smart Amplifier driver");
MODULE_LICENSE("GPL v2");
#endif /* CONFIG_TAS256X_CODEC */
