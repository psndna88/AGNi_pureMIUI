/*
 * cs35l41.c -- CS35l41 ALSA SoC audio driver
 *
 * Copyright 2018 Cirrus Logic, Inc.
 *
 * Author:	David Rhodes	<david.rhodes@cirrus.com>
 *		Brian Austin	<brian.austin@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#define SSR_RESET

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/gpio.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/of_irq.h>
#include <linux/completion.h>
#include <linux/spi/spi.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include "wm_adsp.h"
#include "cs35l41.h"
#include "cs35l41_user.h"

static const char * const cs35l41_supplies[] = {
	"VA",
	"VP",
};

struct cs35l41_pll_sysclk_config {
	int freq;
	int clk_cfg;
};

static const struct cs35l41_pll_sysclk_config cs35l41_pll_sysclk[] = {
	{ 32768,	0x00 },
	{ 8000,		0x01 },
	{ 11025,	0x02 },
	{ 12000,	0x03 },
	{ 16000,	0x04 },
	{ 22050,	0x05 },
	{ 24000,	0x06 },
	{ 32000,	0x07 },
	{ 44100,	0x08 },
	{ 48000,	0x09 },
	{ 88200,	0x0A },
	{ 96000,	0x0B },
	{ 128000,	0x0C },
	{ 176400,	0x0D },
	{ 192000,	0x0E },
	{ 256000,	0x0F },
	{ 352800,	0x10 },
	{ 384000,	0x11 },
	{ 512000,	0x12 },
	{ 705600,	0x13 },
	{ 750000,	0x14 },
	{ 768000,	0x15 },
	{ 1000000,	0x16 },
	{ 1024000,	0x17 },
	{ 1200000,	0x18 },
	{ 1411200,	0x19 },
	{ 1500000,	0x1A },
	{ 1536000,	0x1B },
	{ 2000000,	0x1C },
	{ 2048000,	0x1D },
	{ 2400000,	0x1E },
	{ 2822400,	0x1F },
	{ 3000000,	0x20 },
	{ 3072000,	0x21 },
	{ 3200000,	0x22 },
	{ 4000000,	0x23 },
	{ 4096000,	0x24 },
	{ 4800000,	0x25 },
	{ 5644800,	0x26 },
	{ 6000000,	0x27 },
	{ 6144000,	0x28 },
	{ 6250000,	0x29 },
	{ 6400000,	0x2A },
	{ 6500000,	0x2B },
	{ 6750000,	0x2C },
	{ 7526400,	0x2D },
	{ 8000000,	0x2E },
	{ 8192000,	0x2F },
	{ 9600000,	0x30 },
	{ 11289600,	0x31 },
	{ 12000000,	0x32 },
	{ 12288000,	0x33 },
	{ 12500000,	0x34 },
	{ 12800000,	0x35 },
	{ 13000000,	0x36 },
	{ 13500000,	0x37 },
	{ 19200000,	0x38 },
	{ 22579200,	0x39 },
	{ 24000000,	0x3A },
	{ 24576000,	0x3B },
	{ 25000000,	0x3C },
	{ 25600000,	0x3D },
	{ 26000000,	0x3E },
	{ 27000000,	0x3F },
};

static const unsigned char cs35l41_bst_k1_table[4][5] = {
	{0x24, 0x32, 0x32, 0x4F, 0x57},
	{0x24, 0x32, 0x32, 0x4F, 0x57},
	{0x40, 0x32, 0x32, 0x4F, 0x57},
	{0x40, 0x32, 0x32, 0x4F, 0x57}
};

static const unsigned char cs35l41_bst_k2_table[4][5] = {
	{0x24, 0x49, 0x66, 0xA3, 0xEA},
	{0x24, 0x49, 0x66, 0xA3, 0xEA},
	{0x48, 0x49, 0x66, 0xA3, 0xEA},
	{0x48, 0x49, 0x66, 0xA3, 0xEA}
};

static const unsigned char cs35l41_bst_slope_table[4] = {
					0x75, 0x6B, 0x3B, 0x28};

static int cs35l41_component_set_sysclk(struct snd_soc_component *component,
				int clk_id, int source, unsigned int freq,
				int dir);

static int cs35l41_dsp_power_ev(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);

	dev_info(cs35l41->dev, "%s: event = %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (cs35l41->halo_booted == false)
			wm_halo_early_event(w, kcontrol, event);
		else
			cs35l41->dsp.booted = true;

		return 0;
	case SND_SOC_DAPM_PRE_PMD:
		if (cs35l41->halo_booted == false) {
			wm_halo_early_event(w, kcontrol, event);
			wm_halo_event(w, kcontrol, event);
		}
	default:
		return 0;
	}
}

static int cs35l41_dsp_load_ev(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);

	dev_info(cs35l41->dev, "%s: event = %d\n",__func__, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (cs35l41->halo_booted == false) {
			wm_halo_event(w, kcontrol, event);
			cs35l41->halo_booted = true;
		}
	default:
		return 0;
	}
}

static int cs35l41_halo_booted_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = cs35l41->halo_booted;

	return 0;
}

static int cs35l41_halo_booted_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);

	cs35l41->halo_booted = ucontrol->value.integer.value[0];

	return 0;
}

static int cs35l41_digital_mute_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct cs35l41_private *cs35l41 =
		snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = 0;

	dev_info(cs35l41->dev, "%s: %d\n", __func__, 0);

	return 0;
}

static int cs35l41_digital_mute_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	int			ret = 0;

	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct cs35l41_private *cs35l41 =
		snd_soc_component_get_drvdata(component);

	int mute = !!ucontrol->value.integer.value[0];

	dev_info(cs35l41->dev, "%s: %d\n", __func__, mute);
	switch (mute) {
	case 0:
	case 1:
		regmap_update_bits(cs35l41->regmap, CS35L41_AMP_DIG_VOL_CTRL,
				0x7ff << 3, 0x400 << 3);
		break;
	default:
		break;
	}

	return ret;
}

static const char *cs35l41_fast_switch_text[] = {
	"spk1_playback_delta.txt",
	"spk1_voice_delta.txt",
	"spk2_playback_delta.txt",
	"spk2_voice_delta.txt",
	"rcv_voice_delta.txt",
	"spk1_playback_delta-mars.txt",
	"spk1_voice_delta-mars.txt",
	"spk2_playback_delta-mars.txt",
	"spk2_voice_delta-mars.txt",
	"rcv_voice_delta-mars.txt",
};

static int cs35l41_fast_switch_en_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = cs35l41->fast_switch_en;

	return 0;
}

static int cs35l41_do_fast_switch(struct cs35l41_private *cs35l41)
{
	char			val_str[CS35L41_BUFSIZE];
	const char		*fw_name;
	const struct firmware	*fw;
	int			ret;
	unsigned int		i, j, k;
	s32			data_ctl_len, val;
	__be32			*data_ctl_buf, cmd_ctl, st_ctl;
	bool			fw_running	= false;

	data_ctl_buf	= NULL;

	fw_name	= cs35l41->fast_switch_names[cs35l41->fast_switch_file_idx];
	dev_info(cs35l41->dev, "fw_name:%s\n", fw_name);
	ret	= request_firmware(&fw, fw_name, cs35l41->dev);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Failed to request firmware:%s\n",
			fw_name);
		return -EIO;
	}

	/* Parse number of data in file */
	for (i = 0, j = 0; (char)fw->data[i] != ','; i++) {
		if ((char)fw->data[i] == ' ') {
			/* Skip white space */
		} else {
			/* fw->data[i] must be numerical digit */
			if (j < CS35L41_BUFSIZE - 1) {
				val_str[j]	= fw->data[i];
				j++;
			} else {
				dev_err(cs35l41->dev, "Invalid input\n");
				ret		= -EINVAL;
				goto exit;
			}
		}
	}
	i++;	/* points to beginning of next number */
	val_str[j]	= '\0';
	ret		= kstrtos32(val_str, 10, &data_ctl_len);
	if (ret < 0) {
		dev_err(cs35l41->dev, "kstrtos32 failed (%d) val_str:%s\n",
			ret, val_str);
		goto exit;
	}

	dev_dbg(cs35l41->dev, "data_ctl_len:%u\n", data_ctl_len);

	data_ctl_buf	= kcalloc(1, data_ctl_len * sizeof(s32), GFP_KERNEL);
	if (!data_ctl_buf) {
		ret	= -ENOMEM;
		goto exit;
	}

	data_ctl_buf[0]	= cpu_to_be32(data_ctl_len);

	/* i continues from end of previous loop */
	for (j = 0, k = 1; i <= fw->size; i++) {
		if (i == fw->size || (char)fw->data[i] == ',') {
			/*
			 * Reached end of parameter
			 * delimited either by ',' or end of file
			 * Parse number and write parameter
			 */
			val_str[j]	= '\0';
			ret		= kstrtos32(val_str, 10, &val);
			if (ret < 0) {
				dev_err(cs35l41->dev,
					"kstrtos32 failed (%d) val_str:%s\n",
					ret, val_str);
				goto exit;
			}
			data_ctl_buf[k] = cpu_to_be32(val);
			j		= 0;
			k++;
		} else if ((char)fw->data[i] == ' ') {
			/* Skip white space */
		} else {
			/* fw->data[i] must be numerical digit */
			if (j < CS35L41_BUFSIZE - 1) {
				val_str[j] = fw->data[i];
				j++;
			} else {
				dev_err(cs35l41->dev, "Invalid input\n");
				ret	= -EINVAL;
				goto exit;
			}
		}
	}

	/* Verify if there is no active CSPL commands */
	wm_adsp_read_ctl(&cs35l41->dsp, "CSPL_COMMAND", &cmd_ctl, sizeof(s32));
	if (be32_to_cpu(cmd_ctl) != CSPL_CMD_NONE) {
		dev_err(cs35l41->dev, "CSPL_COMMAND = %d)\n",
			be32_to_cpu(cmd_ctl));
		usleep_range(100, 110);
		cmd_ctl = cpu_to_be32(CSPL_CMD_NONE);
		wm_adsp_write_ctl(&cs35l41->dsp, "CSPL_COMMAND",
				   &cmd_ctl, sizeof(s32));
	}

	wm_adsp_write_ctl(&cs35l41->dsp, "CSPL_UPDATE_PARAMS_CONFIG",
			  data_ctl_buf, data_ctl_len * sizeof(s32));

	dev_dbg(cs35l41->dev,
		"Wrote %u reg for CSPL_UPDATE_PARAMS_CONFIG\n", data_ctl_len);

#ifdef DEBUG
	wm_adsp_read_ctl(&cs35l41->dsp, "CSPL_UPDATE_PARAMS_CONFIG",
			 data_ctl_buf, data_ctl_len * sizeof(s32));
	dev_dbg(cs35l41->dev, "read CSPL_UPDATE_PARAMS_CONFIG:\n");
	for (i = 0; i < data_ctl_len; i++)
		dev_dbg(cs35l41->dev, "%u\n", be32_to_cpu(data_ctl_buf[i]));
#endif
	cmd_ctl		= cpu_to_be32(CSPL_CMD_UPDATE_PARAM);
	wm_adsp_write_ctl(&cs35l41->dsp, "CSPL_COMMAND", &cmd_ctl, sizeof(s32));

	/* Verify CSPL COMMAND */
	for (i = 0; i < 5; i++) {
		wm_adsp_read_ctl(&cs35l41->dsp, "CSPL_STATE", &st_ctl,
				 sizeof(s32));
		if (be32_to_cpu(st_ctl) == CSPL_ST_RUNNING) {
			dev_dbg(cs35l41->dev,
				"CSPL STATE == RUNNING (%u attempt)\n", i);
			fw_running	= true;
			break;
		}

		usleep_range(100, 110);
	}

	if (!fw_running) {
		dev_err(cs35l41->dev, "CSPL_STATE (%d) is not running\n",
			st_ctl);
		ret	= -1;
		goto exit;
	}
exit:
	kfree(data_ctl_buf);
	release_firmware(fw);
	return ret;
}

static int cs35l41_fast_switch_en_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{

	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);


	cs35l41->fast_switch_en = ucontrol->value.integer.value[0];

	return 0;
}

static int cs35l41_fast_switch_file_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);
	struct soc_enum		*soc_enum;
	unsigned int		i = ucontrol->value.enumerated.item[0];
	int			ret = 0;

	soc_enum = (struct soc_enum *)kcontrol->private_value;

	if (i >= soc_enum->items) {
		dev_err(cs35l41->dev, "Invalid mixer input (%u)\n", i);
		return -EINVAL;
	}

	i = i % (soc_enum->items/2);
	if (soc_enum->shift_l == 1) {
		i = i + soc_enum->items/2;
	}

	if ((i != cs35l41->fast_switch_file_idx) && cs35l41->fast_switch_en) {
		cs35l41->fast_switch_file_idx = i;
		ret = cs35l41_do_fast_switch(cs35l41);
	} else {
		dev_info(cs35l41->dev, "do not need switch to delta (%u),origin delta %d, fast_switch_en %d\n",
			i, cs35l41->fast_switch_file_idx, cs35l41->fast_switch_en);
	}

	cs35l41->fast_switch_file_idx = i;

	return 0;
}

static int cs35l41_fast_switch_file_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = cs35l41->fast_switch_file_idx;

	return 0;
}

static const DECLARE_TLV_DB_RANGE(dig_vol_tlv,
		0, 0, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 1),
		1, 913, TLV_DB_MINMAX_ITEM(-10200, 1200));
static DECLARE_TLV_DB_SCALE(amp_gain_tlv, 0, 1, 1);

static const struct snd_kcontrol_new dre_ctrl =
	SOC_DAPM_SINGLE("DRE Switch", CS35L41_PWR_CTRL3, 20, 1, 0);

static const char * const cs35l41_pcm_sftramp_text[] =  {
	"Off", ".5ms", "1ms", "2ms", "4ms", "8ms", "15ms", "30ms"};

static SOC_ENUM_SINGLE_DECL(pcm_sft_ramp,
			    CS35L41_AMP_DIG_VOL_CTRL, 0,
			    cs35l41_pcm_sftramp_text);

static int cs35l41_reload_tuning_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = cs35l41->reload_tuning;

	return 0;
}

static int cs35l41_reload_tuning_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);

	cs35l41->reload_tuning = ucontrol->value.integer.value[0];

	return 0;
}

static bool cs35l41_is_csplmboxsts_correct(enum cs35l41_cspl_mboxcmd cmd,
					   enum cs35l41_cspl_mboxstate sts)
{
	switch (cmd) {
	case CSPL_MBOX_CMD_NONE:
	case CSPL_MBOX_CMD_UNKNOWN_CMD:
		return true;
	case CSPL_MBOX_CMD_PAUSE:
		return (sts == CSPL_MBOX_STS_PAUSED);
	case CSPL_MBOX_CMD_RESUME:
		return (sts == CSPL_MBOX_STS_RUNNING);
	case CSPL_MBOX_CMD_REINIT:
		return (sts == CSPL_MBOX_STS_RUNNING);
	case CSPL_MBOX_CMD_STOP_PRE_REINIT:
		return (sts == CSPL_MBOX_STS_RDY_FOR_REINIT);
	default:
		return false;
	}
}

static int cs35l41_set_csplmboxcmd(struct cs35l41_private *cs35l41,
				   enum cs35l41_cspl_mboxcmd cmd)
{
	int		ret = 0;
	unsigned int	sts, i;
	bool		ack = false;

	/* Reset DSP sticky bit */
	regmap_write(cs35l41->regmap, CS35L41_IRQ2_STATUS2,
		     1 << CS35L41_CSPL_MBOX_CMD_DRV_SHIFT);

	/* Reset AP sticky bit */
	regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS2,
		     1 << CS35L41_CSPL_MBOX_CMD_FW_SHIFT);

	/*
	 * Set mailbox cmd
	 */
	/* Unmask DSP INT */
	regmap_update_bits(cs35l41->regmap, CS35L41_IRQ2_MASK2,
			   1 << CS35L41_CSPL_MBOX_CMD_DRV_SHIFT, 0);
	regmap_write(cs35l41->regmap, CS35L41_CSPL_MBOX_CMD_DRV, cmd);

	/* Poll for DSP ACK */
	for (i = 0; i < 5; i++) {
		usleep_range(1000, 1010);
		ret = regmap_read(cs35l41->regmap, CS35L41_IRQ1_STATUS2, &sts);
		if (ret < 0) {
			dev_err(cs35l41->dev, "regmap_read failed (%d)\n", ret);
			continue;
		}
		if (sts & (1 << CS35L41_CSPL_MBOX_CMD_FW_SHIFT)) {
			dev_dbg(cs35l41->dev,
				"%u: Received ACK in EINT for mbox cmd (%d)\n",
				i, cmd);
			regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS2,
			     1 << CS35L41_CSPL_MBOX_CMD_FW_SHIFT);
			ack = true;
			break;
		}
	}

	if (!ack) {
		dev_err(cs35l41->dev,
			"Timout waiting for DSP to set mbox cmd\n");
		ret = -ETIMEDOUT;
	}

	/* Mask DSP INT */
	regmap_update_bits(cs35l41->regmap, CS35L41_IRQ2_MASK2,
			   1 << CS35L41_CSPL_MBOX_CMD_DRV_SHIFT,
			   1 << CS35L41_CSPL_MBOX_CMD_DRV_SHIFT);

	if (regmap_read(cs35l41->regmap,
			CS35L41_CSPL_MBOX_STS, &sts) < 0) {
		dev_err(cs35l41->dev, "Failed to read %u\n",
			CS35L41_CSPL_MBOX_STS);
		ret = -EACCES;
	}

	if (!cs35l41_is_csplmboxsts_correct(cmd,
					    (enum cs35l41_cspl_mboxstate)sts)) {
		dev_err(cs35l41->dev,
			"Failed to set mailbox(cmd: %u, sts: %u)\n", cmd, sts);
		ret = -ENOMSG;
	}

	return ret;
}



static const char *virt_text[] = { "None", "Ref"};
static SOC_ENUM_SINGLE_DECL(virt_enum,
			    SND_SOC_NOPM, 2, virt_text);

static const struct snd_kcontrol_new virt_mux =
	SOC_DAPM_ENUM("Virt Connect", virt_enum);

static const char * const cs35l41_pcm_source_texts[] = {"None", "ASP", "DSP"};
static const unsigned int cs35l41_pcm_source_values[] = {0x00, 0x08, 0x32};
static SOC_VALUE_ENUM_SINGLE_DECL(cs35l41_pcm_source_enum,
				CS35L41_DAC_PCM1_SRC,
				0, CS35L41_ASP_SOURCE_MASK,
				cs35l41_pcm_source_texts,
				cs35l41_pcm_source_values);

static const struct snd_kcontrol_new pcm_source_mux =
	SOC_DAPM_ENUM("PCM Source", cs35l41_pcm_source_enum);

static const char * const cs35l41_tx_input_texts[] = {"Zero", "ASPRX1",
							"ASPRX2", "VMON",
							"IMON", "VPMON",
							"DSPTX1", "DSPTX2"};
static const unsigned int cs35l41_tx_input_values[] = {0x00,
						CS35L41_INPUT_SRC_ASPRX1,
						CS35L41_INPUT_SRC_ASPRX2,
						CS35L41_INPUT_SRC_VMON,
						CS35L41_INPUT_SRC_IMON,
						CS35L41_INPUT_SRC_VPMON,
						CS35L41_INPUT_DSP_TX1,
						CS35L41_INPUT_DSP_TX2};

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l41_asptx1_enum,
				CS35L41_ASP_TX1_SRC,
				0, CS35L41_ASP_SOURCE_MASK,
				cs35l41_tx_input_texts,
				cs35l41_tx_input_values);

static const struct snd_kcontrol_new asp_tx1_mux =
	SOC_DAPM_ENUM("ASPTX1 SRC", cs35l41_asptx1_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l41_asptx2_enum,
				CS35L41_ASP_TX2_SRC,
				0, CS35L41_ASP_SOURCE_MASK,
				cs35l41_tx_input_texts,
				cs35l41_tx_input_values);

static const struct snd_kcontrol_new asp_tx2_mux =
	SOC_DAPM_ENUM("ASPTX2 SRC", cs35l41_asptx2_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l41_asptx3_enum,
				CS35L41_ASP_TX3_SRC,
				0, CS35L41_ASP_SOURCE_MASK,
				cs35l41_tx_input_texts,
				cs35l41_tx_input_values);

static const struct snd_kcontrol_new asp_tx3_mux =
	SOC_DAPM_ENUM("ASPTX3 SRC", cs35l41_asptx3_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l41_asptx4_enum,
				CS35L41_ASP_TX4_SRC,
				0, CS35L41_ASP_SOURCE_MASK,
				cs35l41_tx_input_texts,
				cs35l41_tx_input_values);

static const struct snd_kcontrol_new asp_tx4_mux =
	SOC_DAPM_ENUM("ASPTX4 SRC", cs35l41_asptx4_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l41_dsprx1_enum,
				CS35L41_DSP1_RX1_SRC,
				0, CS35L41_ASP_SOURCE_MASK,
				cs35l41_tx_input_texts,
				cs35l41_tx_input_values);

static const struct snd_kcontrol_new dsp_rx1_mux =
	SOC_DAPM_ENUM("DSPRX1 SRC", cs35l41_dsprx1_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l41_dsprx2_enum,
				CS35L41_DSP1_RX2_SRC,
				0, CS35L41_ASP_SOURCE_MASK,
				cs35l41_tx_input_texts,
				cs35l41_tx_input_values);

static const struct snd_kcontrol_new dsp_rx2_mux =
	SOC_DAPM_ENUM("DSPRX2 SRC", cs35l41_dsprx2_enum);

static const struct snd_kcontrol_new cs35l41_aud_controls[] = {
	SOC_SINGLE_SX_TLV("Digital PCM Volume", CS35L41_AMP_DIG_VOL_CTRL,
		      3, 0x4CF, 0x391, dig_vol_tlv),
	SOC_SINGLE_TLV("AMP PCM Gain", CS35L41_AMP_GAIN_CTRL, 5, 0x14, 0,
			amp_gain_tlv),
	SOC_SINGLE_RANGE("ASPTX1 Slot Position", CS35L41_SP_FRAME_TX_SLOT, 0,
			 0, 7, 0),
	SOC_SINGLE_RANGE("ASPTX2 Slot Position", CS35L41_SP_FRAME_TX_SLOT, 8,
			 0, 7, 0),
	SOC_SINGLE_RANGE("ASPTX3 Slot Position", CS35L41_SP_FRAME_TX_SLOT, 16,
			 0, 7, 0),
	SOC_SINGLE_RANGE("ASPTX4 Slot Position", CS35L41_SP_FRAME_TX_SLOT, 24,
			 0, 7, 0),
	SOC_SINGLE_RANGE("ASPRX1 Slot Position", CS35L41_SP_FRAME_RX_SLOT, 0,
			 0, 7, 0),
	SOC_SINGLE_RANGE("ASPRX2 Slot Position", CS35L41_SP_FRAME_RX_SLOT, 8,
			 0, 7, 0),
	SOC_SINGLE("Boost Class-H Tracking Enable", CS35L41_BSTCVRT_VCTRL2, 0, 1, 0),
	SOC_SINGLE("Boost Target Voltage", CS35L41_BSTCVRT_VCTRL1, 0, 0xAA, 0),
	SOC_SINGLE("Noise Gate", CS35L41_NG_CFG, 0, 0x3FFF, 0),
	//Convert hex to int, eg. 0x2005309 --> 33575689, tinymix 'VPBR Config' 33575689
	SOC_SINGLE("VPBR Config", CS35L41_VPBR_CFG, 0, 0x7FFFFFF, 0),
	SOC_SINGLE("AMP Enable", CS35L41_PWR_CTRL2, 0, 1, 0),
	SOC_ENUM("PCM Soft Ramp", pcm_sft_ramp),
	SOC_SINGLE_EXT("DSP Booted", SND_SOC_NOPM, 0, 1, 0,
			cs35l41_halo_booted_get, cs35l41_halo_booted_put),
	SOC_SINGLE_EXT("Fast Use Case Switch Enable", SND_SOC_NOPM, 0, 1, 0,
		       cs35l41_fast_switch_en_get, cs35l41_fast_switch_en_put),
	SOC_SINGLE_EXT("Digital Mute", SND_SOC_NOPM, 0, 1, 0,
			cs35l41_digital_mute_get, cs35l41_digital_mute_put),
	SOC_SINGLE_EXT("Firmware Reload Tuning", SND_SOC_NOPM, 0, 1, 0,
			cs35l41_reload_tuning_get, cs35l41_reload_tuning_put),
	SOC_SINGLE("GLOBAL_EN from GPIO Control", CS35L41_PWR_CTRL1, 8, 1, 0),
	WM_ADSP2_PRELOAD_SWITCH("DSP1", 1),
};

static const struct cs35l41_otp_map_element_t *cs35l41_find_otp_map(u32 otp_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs35l41_otp_map_map); i++) {
		if (cs35l41_otp_map_map[i].id == otp_id)
			return &cs35l41_otp_map_map[i];
	}

	return NULL;
}

static int cs35l41_otp_unpack(void *data)
{
	struct cs35l41_private *cs35l41 = data;
	u32 otp_mem[32];
	int i;
	int bit_offset, word_offset;
	unsigned int bit_sum = 8;
	u32 otp_val, otp_id_reg;
	const struct cs35l41_otp_map_element_t *otp_map_match;
	const struct cs35l41_otp_packed_element_t *otp_map;
	int ret;
	struct spi_device *spi = NULL;
	u32 orig_spi_freq = 0;

	ret = regmap_read(cs35l41->regmap, CS35L41_OTPID, &otp_id_reg);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Read OTP ID failed\n");
		return -EINVAL;
	}

	otp_map_match = cs35l41_find_otp_map(otp_id_reg);

	if (otp_map_match == NULL) {
		dev_err(cs35l41->dev, "OTP Map matching ID %d not found\n",
				otp_id_reg);
		return -EINVAL;
	}

	if (cs35l41->bus_spi) {
		spi = to_spi_device(cs35l41->dev);
		orig_spi_freq = spi->max_speed_hz;
		spi->max_speed_hz = CS35L41_SPI_MAX_FREQ_OTP;
		spi_setup(spi);
	}

	#define MAX_BULK_READ_SIZE 4
	for (i = 0; i < CS35L41_OTP_SIZE_WORDS / MAX_BULK_READ_SIZE; i++) {
		ret = regmap_bulk_read(cs35l41->regmap, CS35L41_OTP_MEM0 + i * 4 * MAX_BULK_READ_SIZE,
					&otp_mem[i * MAX_BULK_READ_SIZE], MAX_BULK_READ_SIZE);
	}

	if (ret < 0) {
		dev_err(cs35l41->dev, "Read OTP Mem failed\n");
		return -EINVAL;
	}

	if (cs35l41->bus_spi) {
		spi->max_speed_hz = orig_spi_freq;
		spi_setup(spi);
	}

	otp_map = otp_map_match->map;

	bit_offset = otp_map_match->bit_offset;
	word_offset = otp_map_match->word_offset;

	ret = regmap_write(cs35l41->regmap, CS35L41_TEST_KEY_CTL, 0x00000055);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Write Unlock key failed 1/2\n");
		return -EINVAL;
	}
	ret = regmap_write(cs35l41->regmap, CS35L41_TEST_KEY_CTL, 0x000000AA);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Write Unlock key failed 2/2\n");
		return -EINVAL;
	}

	for (i = 0; i < otp_map_match->num_elements; i++) {
		dev_dbg(cs35l41->dev, "bitoffset= %d, word_offset=%d, bit_sum mod 32=%d\n",
					bit_offset, word_offset, bit_sum % 32);
		if (bit_offset + otp_map[i].size - 1 >= 32) {
			otp_val = (otp_mem[word_offset] &
					GENMASK(31, bit_offset)) >>
					bit_offset;
			otp_val |= (otp_mem[++word_offset] &
					GENMASK(bit_offset +
						otp_map[i].size - 33, 0)) <<
					(32 - bit_offset);
			bit_offset += otp_map[i].size - 32;
		} else {

			otp_val = (otp_mem[word_offset] &
				GENMASK(bit_offset + otp_map[i].size - 1,
					bit_offset)) >>	bit_offset;
			bit_offset += otp_map[i].size;
		}
		bit_sum += otp_map[i].size;

		if (bit_offset == 32) {
			bit_offset = 0;
			word_offset++;
		}

		if (otp_map[i].reg != 0) {
			ret = regmap_update_bits(cs35l41->regmap,
						otp_map[i].reg,
						GENMASK(otp_map[i].shift +
							otp_map[i].size - 1,
						otp_map[i].shift),
						otp_val << otp_map[i].shift);
			if (ret < 0) {
				dev_err(cs35l41->dev, "Write OTP val failed\n");
				return -EINVAL;
			}
		}
	}

	ret = regmap_write(cs35l41->regmap, CS35L41_TEST_KEY_CTL, 0x000000CC);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Write Lock key failed 1/2\n");
		return -EINVAL;
	}
	ret = regmap_write(cs35l41->regmap, CS35L41_TEST_KEY_CTL, 0x00000033);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Write Lock key failed 2/2\n");
		return -EINVAL;
	}

	return 0;
}

static irqreturn_t cs35l41_irq(int irq, void *data)
{
	struct cs35l41_private *cs35l41 = data;
	unsigned int status[4];
	unsigned int masks[4];
	unsigned int i;
	dev_info(cs35l41->dev, "step into cs35l41 irq handler\n");

	for (i = 0; i < ARRAY_SIZE(status); i++) {
		regmap_read(cs35l41->regmap,
			    CS35L41_IRQ1_STATUS1 + (i * CS35L41_REGSTRIDE),
			    &status[i]);
		regmap_read(cs35l41->regmap,
			    CS35L41_IRQ1_MASK1 + (i * CS35L41_REGSTRIDE),
			    &masks[i]);
	}

	/* Check to see if unmasked bits are active */
	if (!(status[0] & ~masks[0]) && !(status[1] & ~masks[1]) &&
		!(status[2] & ~masks[2]) && !(status[3] & ~masks[3]))
		return IRQ_NONE;

	/*
	 * The following interrupts require a
	 * protection release cycle to get the
	 * speaker out of Safe-Mode.
	 */
	if (status[0] & CS35L41_AMP_SHORT_ERR) {
		dev_crit(cs35l41->dev, "Amp short error\n");
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					CS35L41_AMP_SHORT_ERR);
		regmap_write(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_AMP_SHORT_ERR_RLS,
					CS35L41_AMP_SHORT_ERR_RLS);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_AMP_SHORT_ERR_RLS, 0);
	}

	if (status[0] & CS35L41_TEMP_WARN) {
		dev_crit(cs35l41->dev, "Over temperature warning\n");
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					CS35L41_TEMP_WARN);
		regmap_write(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_TEMP_WARN_ERR_RLS,
					CS35L41_TEMP_WARN_ERR_RLS);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_TEMP_WARN_ERR_RLS, 0);
	}

	if (status[0] & CS35L41_TEMP_ERR) {
		dev_crit(cs35l41->dev, "Over temperature error\n");
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					CS35L41_TEMP_ERR);
		regmap_write(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_TEMP_ERR_RLS,
					CS35L41_TEMP_ERR_RLS);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_TEMP_ERR_RLS, 0);
	}

	if (status[0] & CS35L41_BST_OVP_ERR) {
		dev_crit(cs35l41->dev, "VBST Over Voltage error\n");
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					CS35L41_BST_EN_MASK, 0);
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					CS35L41_BST_OVP_ERR);
		regmap_write(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_BST_OVP_ERR_RLS,
					CS35L41_BST_OVP_ERR_RLS);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_BST_OVP_ERR_RLS, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					CS35L41_BST_EN_MASK,
					CS35L41_BST_EN_DEFAULT <<
					CS35L41_BST_EN_SHIFT);
	}

	if (status[0] & CS35L41_BST_DCM_UVP_ERR) {
		dev_crit(cs35l41->dev, "DCM VBST Under Voltage Error\n");
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					CS35L41_BST_EN_MASK, 0);
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					CS35L41_BST_DCM_UVP_ERR);
		regmap_write(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_BST_UVP_ERR_RLS,
					CS35L41_BST_UVP_ERR_RLS);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_BST_UVP_ERR_RLS, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					CS35L41_BST_EN_MASK,
					CS35L41_BST_EN_DEFAULT <<
					CS35L41_BST_EN_SHIFT);
	}

	if (status[0] & CS35L41_BST_SHORT_ERR) {
		dev_crit(cs35l41->dev, "LBST error: powering off!\n");
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					CS35L41_BST_EN_MASK, 0);
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					CS35L41_BST_SHORT_ERR);
		regmap_write(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_BST_SHORT_ERR_RLS,
					CS35L41_BST_SHORT_ERR_RLS);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_BST_SHORT_ERR_RLS, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					CS35L41_BST_EN_MASK,
					CS35L41_BST_EN_DEFAULT <<
					CS35L41_BST_EN_SHIFT);
	}

	if (status[3] & CS35L41_OTP_BOOT_DONE) {
		regmap_update_bits(cs35l41->regmap, CS35L41_IRQ1_MASK4,
				CS35L41_OTP_BOOT_DONE, CS35L41_OTP_BOOT_DONE);
	}

	if (status[1] & (1 << CS35L41_DSP_VIRT2_MBOX_SHIFT)) {
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_MASK2,
			     0xFFFFFFFF);
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS2,
			     1 << CS35L41_DSP_VIRT2_MBOX_SHIFT);
		//Analog mute PA if DC is detected
		//regmap_write(cs35l41->regmap, CS35L41_AMP_OUT_MUTE,
		//	     1 << CS35L41_AMP_MUTE_SHIFT);
		cs35l41->dc_current_cnt++;
		dev_crit(cs35l41->dev, "DC current detected");
	}

	return IRQ_HANDLED;
}

static const struct reg_sequence cs35l41_pup_patch[] = {
	{0x00000040, 0x00000055},
	{0x00000040, 0x000000AA},
	{0x00002084, 0x002F1AA0},
	{0x00000040, 0x000000CC},
	{0x00000040, 0x00000033},
};

static const struct reg_sequence cs35l41_pdn_patch[] = {
	{0x00000040, 0x00000055},
	{0x00000040, 0x000000AA},
	{0x00002084, 0x002F1AA3},
	{0x00000040, 0x000000CC},
	{0x00000040, 0x00000033},
};

static const struct reg_sequence cs35l41_dsp_recovery_patch[] = {
	{0x02800258, 0x00000000},
	{0x0280025c, 0x00000000},
};

static int cs35l41_main_amp_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);
	enum cs35l41_cspl_mboxcmd mboxcmd = CSPL_MBOX_CMD_NONE;
	int ret = 0;
	enum cs35l41_cspl_mboxstate fw_status = CSPL_MBOX_STS_RUNNING;
	int i;
	bool pdn;
	unsigned int val;
	pr_debug("++++>CSPL: %s, event = %d, DC counter = %d.\n", __func__, event, cs35l41->dc_current_cnt);
	dev_warn(cs35l41->dev, "%s: event = %d\n", __func__, event);
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_multi_reg_write_bypassed(cs35l41->regmap,
					cs35l41_pup_patch,
					ARRAY_SIZE(cs35l41_pup_patch));

		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL1,
				CS35L41_GLOBAL_EN_MASK,
				1 << CS35L41_GLOBAL_EN_SHIFT);

		usleep_range(1000, 1100);

		if (cs35l41->dsp.running) {
			regmap_read(cs35l41->regmap, CS35L41_DSP_MBOX_2,
				    (unsigned int *)&fw_status);
			switch(fw_status) {
			case CSPL_MBOX_STS_RDY_FOR_REINIT:
				mboxcmd = CSPL_MBOX_CMD_REINIT;
				break;
			case CSPL_MBOX_STS_PAUSED:
				mboxcmd = CSPL_MBOX_CMD_RESUME;
				break;
			case CSPL_MBOX_STS_RUNNING:
				/*
				 * First time playing audio
				 * means fw_status is running
				 */
				mboxcmd = CSPL_MBOX_CMD_RESUME;
				break;
			default:
				dev_err(cs35l41->dev,
					"Firmware status is invalid(%u), try to recorver\n",
					fw_status);
				regmap_multi_reg_write_bypassed(cs35l41->regmap,
						cs35l41_dsp_recovery_patch,
						ARRAY_SIZE(cs35l41_dsp_recovery_patch));
				mboxcmd = CSPL_MBOX_CMD_RESUME;
				break;
			}
			ret = cs35l41_set_csplmboxcmd(cs35l41, mboxcmd);
			regmap_write(cs35l41->regmap, CS35L41_IRQ1_MASK2,
				     ~(1 << CS35L41_DSP_VIRT2_MBOX_SHIFT));
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (cs35l41->dsp.running) {
			regmap_write(cs35l41->regmap, CS35L41_IRQ1_MASK2,
				     0xFFFFFFFF);
			//Unmute PA if DC are less than 3 times, or keep muted state
			//if (cs35l41->dc_current_cnt <
			//    CS35L41_DC_CURRENT_THRESHOLD)
			//	regmap_write(cs35l41->regmap,
			//		     CS35L41_AMP_OUT_MUTE, 0);

			if (cs35l41->reload_tuning) {
				mboxcmd = CSPL_MBOX_CMD_STOP_PRE_REINIT;
				/*
				 * Reset reload_tuning, so driver does not
				 * continuously reload tuning file
				 */
				cs35l41->reload_tuning = false;
			} else {
				mboxcmd = CSPL_MBOX_CMD_PAUSE;
			}
			ret = cs35l41_set_csplmboxcmd(cs35l41, mboxcmd);
		}
		regmap_update_bits(cs35l41->regmap,
				CS35L41_PLL_CLK_CTRL,
				CS35L41_PLL_FORCE_EN_MASK, 0);

		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL1,
				CS35L41_GLOBAL_EN_MASK, 0);

		pdn = false;
		for (i = 0; i < 100; i++) {
			regmap_read(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
				    &val);
			if (val & CS35L41_PDN_DONE_MASK) {
				pdn = true;
				break;
			}
			usleep_range(1000, 1010);
		}

		if (!pdn)
			dev_warn(cs35l41->dev, "PDN failed\n");

		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
			     CS35L41_PDN_DONE_MASK);

		regmap_multi_reg_write_bypassed(cs35l41->regmap,
					cs35l41_pdn_patch,
					ARRAY_SIZE(cs35l41_pdn_patch));
		cs35l41->extclk_freq = 0;
		break;
	default:
		dev_err(cs35l41->dev, "Invalid event = 0x%x\n", event);
		ret = -EINVAL;
	}
	pr_debug("----CSPL: %s.\n", __func__);
	return ret;
}

static const struct snd_soc_dapm_widget cs35l41_dapm_widgets[] = {

	SND_SOC_DAPM_SPK("DSP1 Preload", NULL),
	SND_SOC_DAPM_SUPPLY_S("DSP1 Preloader", 100,
				SND_SOC_NOPM, 0, 0, cs35l41_dsp_power_ev,
				SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUT_DRV_E("DSP1", SND_SOC_NOPM, 0, 0, NULL, 0,
				cs35l41_dsp_load_ev, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_OUTPUT("SPK"),

	SND_SOC_DAPM_AIF_IN("ASPRX1", NULL, 0, CS35L41_SP_ENABLES, 16, 0),
	SND_SOC_DAPM_AIF_IN("ASPRX2", NULL, 0, CS35L41_SP_ENABLES, 17, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX1", NULL, 0, CS35L41_SP_ENABLES, 0, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX2", NULL, 0, CS35L41_SP_ENABLES, 1, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX3", NULL, 0, CS35L41_SP_ENABLES, 2, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX4", NULL, 0, CS35L41_SP_ENABLES, 3, 0),

	SND_SOC_DAPM_ADC("VMON ADC", NULL, CS35L41_PWR_CTRL2, 12, 0),
	SND_SOC_DAPM_ADC("IMON ADC", NULL, CS35L41_PWR_CTRL2, 13, 0),
	SND_SOC_DAPM_ADC("VPMON ADC", NULL, CS35L41_PWR_CTRL2, 8, 0),
	SND_SOC_DAPM_ADC("VBSTMON ADC", NULL, CS35L41_PWR_CTRL2, 9, 0),
	SND_SOC_DAPM_ADC("TEMPMON ADC", NULL, CS35L41_PWR_CTRL2, 10, 0),
	SND_SOC_DAPM_ADC("CLASS H", NULL, CS35L41_PWR_CTRL3, 4, 0),

	SND_SOC_DAPM_OUT_DRV_E("Main AMP", CS35L41_PWR_CTRL2, 0, 0, NULL, 0,
				cs35l41_main_amp_event,
				SND_SOC_DAPM_POST_PMD |	SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_INPUT("VP"),
	SND_SOC_DAPM_INPUT("VBST"),
	SND_SOC_DAPM_INPUT("ISENSE"),
	SND_SOC_DAPM_INPUT("VSENSE"),
	SND_SOC_DAPM_INPUT("TEMP"),

	SND_SOC_DAPM_MUX("ASPTX Ref", SND_SOC_NOPM, 0, 0, &virt_mux),
	SND_SOC_DAPM_MUX("ASP TX1 Source", SND_SOC_NOPM, 0, 0, &asp_tx1_mux),
	SND_SOC_DAPM_MUX("ASP TX2 Source", SND_SOC_NOPM, 0, 0, &asp_tx2_mux),
	SND_SOC_DAPM_MUX("ASP TX3 Source", SND_SOC_NOPM, 0, 0, &asp_tx3_mux),
	SND_SOC_DAPM_MUX("ASP TX4 Source", SND_SOC_NOPM, 0, 0, &asp_tx4_mux),
	SND_SOC_DAPM_MUX("DSP RX1 Source", SND_SOC_NOPM, 0, 0, &dsp_rx1_mux),
	SND_SOC_DAPM_MUX("DSP RX2 Source", SND_SOC_NOPM, 0, 0, &dsp_rx2_mux),
	SND_SOC_DAPM_MUX("PCM Source", SND_SOC_NOPM, 0, 0, &pcm_source_mux),
	SND_SOC_DAPM_SWITCH("DRE", SND_SOC_NOPM, 0, 0, &dre_ctrl),
};

static const struct snd_soc_dapm_route cs35l41_audio_map[] = {

	{ "DSP1", NULL, "DSP1 Preloader" },
	{ "DSP1 Preload", NULL, "DSP1 Preloader" },

	{"DSP RX1 Source", "VMON", "VMON ADC"},
	{"DSP RX1 Source", "IMON", "IMON ADC"},
	{"DSP RX1 Source", "VPMON", "VPMON ADC"},
	{"DSP RX1 Source", "DSPTX1", "DSP1"},
	{"DSP RX1 Source", "DSPTX2", "DSP1"},
	{"DSP RX1 Source", "ASPRX1", "ASPRX1"},
	{"DSP RX1 Source", "ASPRX2", "ASPRX2"},
	{"DSP RX1 Source", "Zero", "ASPRX1"},
	{"DSP1", NULL, "DSP RX1 Source"},

	{"DSP RX2 Source", "VMON", "VMON ADC"},
	{"DSP RX2 Source", "IMON", "IMON ADC"},
	{"DSP RX2 Source", "VPMON", "VPMON ADC"},
	{"DSP RX2 Source", "DSPTX1", "DSP1"},
	{"DSP RX2 Source", "DSPTX2", "DSP1"},
	{"DSP RX2 Source", "ASPRX1", "ASPRX1"},
	{"DSP RX2 Source", "ASPRX2", "ASPRX2"},
	{"DSP RX2 Source", "Zero", "ASPRX1"},
	{"DSP1", NULL, "DSP RX2 Source"},

	{"ASP TX1 Source", "VMON", "VMON ADC"},
	{"ASP TX1 Source", "IMON", "IMON ADC"},
	{"ASP TX1 Source", "VPMON", "VPMON ADC"},
	{"ASP TX1 Source", "DSPTX1", "DSP1"},
	{"ASP TX1 Source", "DSPTX2", "DSP1"},
	{"ASP TX1 Source", "ASPRX1", "ASPRX1" },
	{"ASP TX1 Source", "ASPRX2", "ASPRX2" },
	{"ASP TX2 Source", "VMON", "VMON ADC"},
	{"ASP TX2 Source", "IMON", "IMON ADC"},
	{"ASP TX2 Source", "VPMON", "VPMON ADC"},
	{"ASP TX2 Source", "DSPTX1", "DSP1"},
	{"ASP TX2 Source", "DSPTX2", "DSP1"},
	{"ASP TX2 Source", "ASPRX1", "ASPRX1" },
	{"ASP TX2 Source", "ASPRX2", "ASPRX2" },
	{"ASP TX3 Source", "VMON", "VMON ADC"},
	{"ASP TX3 Source", "IMON", "IMON ADC"},
	{"ASP TX3 Source", "VPMON", "VPMON ADC"},
	{"ASP TX3 Source", "DSPTX1", "DSP1"},
	{"ASP TX3 Source", "DSPTX2", "DSP1"},
	{"ASP TX3 Source", "ASPRX1", "ASPRX1" },
	{"ASP TX3 Source", "ASPRX2", "ASPRX2" },
	{"ASP TX4 Source", "VMON", "VMON ADC"},
	{"ASP TX4 Source", "IMON", "IMON ADC"},
	{"ASP TX4 Source", "VPMON", "VPMON ADC"},
	{"ASP TX4 Source", "DSPTX1", "DSP1"},
	{"ASP TX4 Source", "DSPTX2", "DSP1"},
	{"ASP TX4 Source", "ASPRX1", "ASPRX1" },
	{"ASP TX4 Source", "ASPRX2", "ASPRX2" },
	{"ASPTX1", NULL, "ASP TX1 Source"},
	{"ASPTX2", NULL, "ASP TX2 Source"},
	{"ASPTX3", NULL, "ASP TX3 Source"},
	{"ASPTX4", NULL, "ASP TX4 Source"},
	{"AMP Capture", NULL, "ASPTX1"},
	{"AMP Capture", NULL, "ASPTX2"},
	{"AMP Capture", NULL, "ASPTX3"},
	{"AMP Capture", NULL, "ASPTX4"},

	{"VMON ADC", NULL, "ASPRX1"},
	{"IMON ADC", NULL, "ASPRX1"},
	{"VPMON ADC", NULL, "ASPRX1"},
	{"TEMPMON ADC", NULL, "ASPRX1"},
	{"VBSTMON ADC", NULL, "ASPRX1"},

	{"DSP1", NULL, "IMON ADC"},
	{"DSP1", NULL, "VMON ADC"},
	{"DSP1", NULL, "VBSTMON ADC"},
	{"DSP1", NULL, "VPMON ADC"},
	{"DSP1", NULL, "TEMPMON ADC"},

	{"ASPRX1", NULL, "AMP Playback"},
	{"ASPRX2", NULL, "AMP Playback"},
	{"DRE", "DRE Switch", "CLASS H"},
	{"Main AMP", NULL, "CLASS H"},
	{"Main AMP", NULL, "DRE"},
	{"SPK", NULL, "Main AMP"},
	{"Main AMP", NULL, "ASPTX Ref"},
	{"ASPTX Ref", "Ref", "ASPTX1"},
	{"ASPTX Ref", "Ref", "ASPTX2"},
	{"PCM Source", "ASP", "ASPRX1"},
	{"PCM Source", "DSP", "DSP1"},
	{"CLASS H", NULL, "PCM Source"},

};

static const struct wm_adsp_region cs35l41_dsp1_regions[] = {
	{ .type = WMFW_HALO_PM_PACKED,	.base = CS35L41_DSP1_PMEM_0 },
	{ .type = WMFW_HALO_XM_PACKED,	.base = CS35L41_DSP1_XMEM_PACK_0 },
	{ .type = WMFW_HALO_YM_PACKED,	.base = CS35L41_DSP1_YMEM_PACK_0 },
	{. type = WMFW_ADSP2_XM,	.base = CS35L41_DSP1_XMEM_UNPACK24_0},
	{. type = WMFW_ADSP2_YM,	.base = CS35L41_DSP1_YMEM_UNPACK24_0},
};

static int cs35l41_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct cs35l41_private *cs35l41 =
			snd_soc_component_get_drvdata(codec_dai->component);
	unsigned int asp_fmt, lrclk_fmt, sclk_fmt, slave_mode;

	dev_info(cs35l41->dev,"%s, fmt = %d.\n", __func__, fmt);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		slave_mode = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		slave_mode = 0;
		break;
	default:
		dev_warn(cs35l41->dev, "cs35l41_set_dai_fmt: Mixed master mode unsupported\n");
		return -EINVAL;
	}

	regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_SCLK_MSTR_MASK,
				slave_mode << CS35L41_SCLK_MSTR_SHIFT);
	regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_LRCLK_MSTR_MASK,
				slave_mode << CS35L41_LRCLK_MSTR_SHIFT);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		asp_fmt = 0;
		cs35l41->i2s_mode = false;
		cs35l41->dspa_mode = true;
		break;
	case SND_SOC_DAIFMT_I2S:
		asp_fmt = 2;
		cs35l41->i2s_mode = true;
		cs35l41->dspa_mode = false;
		break;
	default:
		dev_warn(cs35l41->dev, "cs35l41_set_dai_fmt: Invalid or unsupported DAI format\n");
		return -EINVAL;
	}

	regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
					CS35L41_ASP_FMT_MASK,
					asp_fmt << CS35L41_ASP_FMT_SHIFT);

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		lrclk_fmt = 1;
		sclk_fmt = 0;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		lrclk_fmt = 0;
		sclk_fmt = 1;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		lrclk_fmt = 1;
		sclk_fmt = 1;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		lrclk_fmt = 0;
		sclk_fmt = 0;
		break;
	default:
		dev_warn(cs35l41->dev, "cs35l41_set_dai_fmt: Invalid DAI clock INV\n");
		return -EINVAL;
	}

	regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_LRCLK_INV_MASK,
				lrclk_fmt << CS35L41_LRCLK_INV_SHIFT);
	regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_SCLK_INV_MASK,
				sclk_fmt << CS35L41_SCLK_INV_SHIFT);

	return 0;
}

struct cs35l41_global_fs_config {
	int rate;
	int fs_cfg;
};

static const struct cs35l41_global_fs_config cs35l41_fs_rates[] = {
	{ 12000,	0x01 },
	{ 24000,	0x02 },
	{ 48000,	0x03 },
	{ 96000,	0x04 },
	{ 192000,	0x05 },
	{ 11025,	0x09 },
	{ 22050,	0x0A },
	{ 44100,	0x0B },
	{ 88200,	0x0C },
	{ 176400,	0x0D },
	{ 8000,		0x11 },
	{ 16000,	0x12 },
	{ 32000,	0x13 },
};

static int cs35l41_is_speaker_in_handset(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *rcv_dai = NULL;//rtd->codec_dais[1];
	struct cs35l41_private *cs35l41 = NULL;
	const char *fw_name = "spk_voice_delta.txt";
	const char *SPK_DAI_NAME = "cs35l41.1-0041";
	const char *RCV_DAI_NAME = "cs35l41.1-0040";
	const char *HANDSET_TUNING = "rcv_voice_delta.txt";
	int i = 0;
        u32 is_dev_mars = 0;
        struct device_node *np = dai->dev->of_node;
        of_property_read_u32(np, "cirrus,is-mars-pa", &is_dev_mars);

#if defined(CONFIG_TARGET_PRODUCT_STAR)
        SPK_DAI_NAME = "cs35l41.2-0042";
        RCV_DAI_NAME = "cs35l41.2-0040";
        HANDSET_TUNING = "rcv_voice_delta.txt";
#endif
#if defined(CONFIG_TARGET_PRODUCT_HAYDN)
        SPK_DAI_NAME = "cs35l41.0-0041";
        RCV_DAI_NAME = "cs35l41.0-0040";
        HANDSET_TUNING = "rcv_voice_delta.txt";
#endif
#if defined(CONFIG_TARGET_PRODUCT_ODIN)
        SPK_DAI_NAME = "cs35l41.1-0042";
        RCV_DAI_NAME = "cs35l41.1-0040";
        HANDSET_TUNING = "rcv_voice_delta.txt";
#endif
#if defined(CONFIG_TARGET_PRODUCT_VILI)
        SPK_DAI_NAME = "cs35l41.1-0041";
        RCV_DAI_NAME = "cs35l41.1-0040";
        HANDSET_TUNING = "rcv_voice_delta.txt";
#endif
        /* modify dai name and handset tuning for mars */
        if (is_dev_mars != 0) {
		SPK_DAI_NAME = "cs35l41.1-0042";
		RCV_DAI_NAME = "cs35l41.1-0040";
		HANDSET_TUNING = "rcv_voice_delta-mars.txt";
        }

	/* Only care about speaker ops*/
	if (strcmp(dai->name, SPK_DAI_NAME))
		return 0;

	for (i = 0; i < rtd->num_codecs; i++) {
		if (!strcmp(RCV_DAI_NAME, rtd->codec_dais[i]->name))
			rcv_dai = rtd->codec_dais[i];
	}

	/* Check the tuning on RCV amp */
	cs35l41 = snd_soc_component_get_drvdata(rcv_dai->component);
	fw_name = cs35l41->fast_switch_names[cs35l41->fast_switch_file_idx];

        dev_info(cs35l41->dev,"cs35l41_is_speaker_in_handset() SPK_DAI_NAME %s RCV_DAI_NAME %s HANDSET_TUNING %s fw_name %s",
						SPK_DAI_NAME, RCV_DAI_NAME, HANDSET_TUNING, fw_name);

	if (!strcmp(fw_name, HANDSET_TUNING)) {
		dev_info(cs35l41->dev, "%s: '%s'[%d] = '%s'\n",
				__func__, rcv_dai->name,
				cs35l41->fast_switch_file_idx, fw_name);
		return 1;
	}

	return 0;
}

static int cs35l41_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cs35l41_private *cs35l41 =
			snd_soc_component_get_drvdata(dai->component);
	int i;
	unsigned int rate = params_rate(params);
	u8 asp_width, asp_wl;
	int val = 0;


	if (cs35l41_is_speaker_in_handset(substream, dai)) {
		dev_info(cs35l41->dev, "%s: speaker amp"
				" hw_parmas in handset mode\n", __func__);
		return 0;
	}

	regmap_read(cs35l41->regmap, CS35L41_PLL_CLK_CTRL, &val);

	for (i = 0; i < ARRAY_SIZE(cs35l41_fs_rates); i++) {
		if (rate == cs35l41_fs_rates[i].rate)
			break;
	}
	regmap_update_bits(cs35l41->regmap, CS35L41_GLOBAL_CLK_CTRL,
			CS35L41_GLOBAL_FS_MASK,
			cs35l41_fs_rates[i].fs_cfg << CS35L41_GLOBAL_FS_SHIFT);

	asp_wl = params_width(params);
	asp_width = params_physical_width(params);

	dev_info(cs35l41->dev,"%s: rate = %d, asp_wl = %d, asp_width = %d\n",
		__func__, rate, asp_wl, asp_width);

#if defined(CONFIG_TARGET_PRODUCT_CS35L41_TDM)
	cs35l41_component_set_sysclk(dai->component, 0, 0, 8 * rate * asp_width, 0);
#else
	cs35l41_component_set_sysclk(dai->component, 0, 0, 2 * rate * asp_width, 0);
#endif

	regmap_read(cs35l41->regmap, CS35L41_PLL_CLK_CTRL, &val);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_ASP_WIDTH_RX_MASK,
				asp_width << CS35L41_ASP_WIDTH_RX_SHIFT);
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_RX_WL,
				CS35L41_ASP_RX_WL_MASK,
				asp_wl << CS35L41_ASP_RX_WL_SHIFT);
		if (cs35l41->i2s_mode) {
			regmap_update_bits(cs35l41->regmap,
					CS35L41_SP_FRAME_RX_SLOT,
					CS35L41_ASP_RX1_SLOT_MASK,
					((cs35l41->pdata.right_channel) ? 1 : 0)
					 << CS35L41_ASP_RX1_SLOT_SHIFT);
			regmap_update_bits(cs35l41->regmap,
					CS35L41_SP_FRAME_RX_SLOT,
					CS35L41_ASP_RX2_SLOT_MASK,
					((cs35l41->pdata.right_channel) ? 0 : 1)
					 << CS35L41_ASP_RX2_SLOT_SHIFT);
		}
	} else {
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_ASP_WIDTH_TX_MASK,
				asp_width << CS35L41_ASP_WIDTH_TX_SHIFT);
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_TX_WL,
				CS35L41_ASP_TX_WL_MASK,
				asp_wl << CS35L41_ASP_TX_WL_SHIFT);
	}

	return 0;
}

static int cs35l41_get_clk_config(int freq)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs35l41_pll_sysclk); i++) {
		if (cs35l41_pll_sysclk[i].freq == freq)
			return cs35l41_pll_sysclk[i].clk_cfg;
	}

	return -EINVAL;
}

#if 0
static const unsigned int cs35l41_src_rates[] = {
	8000, 12000, 11025, 16000, 22050, 24000, 32000,
	44100, 48000, 88200, 96000, 176400, 192000
};

static const struct snd_pcm_hw_constraint_list cs35l41_constraints = {
	.count  = ARRAY_SIZE(cs35l41_src_rates),
	.list   = cs35l41_src_rates,
};
#endif


#ifdef SSR_RESET
void cs35l41_ssr_recovery(struct device *dev, void *data)
{
	struct cs35l41_private *cs35l41 = dev_get_drvdata(dev);
	enum cs35l41_cspl_mboxcmd mboxcmd = CSPL_MBOX_CMD_NONE;
	enum cs35l41_cspl_mboxstate fw_status = CSPL_MBOX_STS_RUNNING;
	int ret, val;
	bool pdn;
	unsigned int global_en = 0;
	int i;

	ret = regmap_read(cs35l41->regmap, CS35L41_PWR_CTRL1, &global_en);
	if ((global_en & CS35L41_GLOBAL_EN_MASK) == 0) {
		dev_err(cs35l41->dev, "CS35L41 in standby mode, no need reset\n");
	}
	//perform digital mute firstly
	regmap_update_bits(cs35l41->regmap,0x6000, 0x7ff << 3, 0x400 << 3 | 0x7);
	usleep_range(30000, 30010);
	//maibox pause
	mboxcmd = (unsigned int)CSPL_MBOX_CMD_PAUSE;
	if (cs35l41->dsp.running) {
		cs35l41->cspl_cmd = (unsigned int)CSPL_MBOX_CMD_PAUSE;
		ret = cs35l41_set_csplmboxcmd(cs35l41, mboxcmd);
	}
	//global_en = 0 and power down pa
	regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL1,
					CS35L41_GLOBAL_EN_MASK, 0);

	pdn = false;
	for (i = 0; i < 100; i++) {
		regmap_read(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
				&val);
		if (val & CS35L41_PDN_DONE_MASK) {
			pdn = true;
			break;
		}
		usleep_range(1000, 1010);
	}

	if (!pdn)
		dev_warn(cs35l41->dev, "PDN failed\n");

	regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
			 CS35L41_PDN_DONE_MASK);

	regmap_multi_reg_write_bypassed(cs35l41->regmap,
				cs35l41_pdn_patch,
				ARRAY_SIZE(cs35l41_pdn_patch));

	//power up PA
	regmap_multi_reg_write_bypassed(cs35l41->regmap,
				cs35l41_pup_patch,
				ARRAY_SIZE(cs35l41_pup_patch));

	regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL1,
			CS35L41_GLOBAL_EN_MASK,
			1 << CS35L41_GLOBAL_EN_SHIFT);

	usleep_range(1000, 1100);

	if (cs35l41->dsp.running) {
		regmap_read(cs35l41->regmap, CS35L41_DSP_MBOX_2,
			    (unsigned int *)&fw_status);
		switch (fw_status) {
		case CSPL_MBOX_STS_RDY_FOR_REINIT:
			mboxcmd = CSPL_MBOX_CMD_REINIT;
			break;
		case CSPL_MBOX_STS_PAUSED:
			mboxcmd = CSPL_MBOX_CMD_RESUME;
			break;
		case CSPL_MBOX_STS_RUNNING:
			/*
			 * First time playing audio
			 * means fw_status is running
			 */
			mboxcmd = CSPL_MBOX_CMD_RESUME;
			break;
		default:
			dev_err(cs35l41->dev,
				"Firmware status is invalid(%u)\n",
				fw_status);
			break;
		}
		ret = cs35l41_set_csplmboxcmd(cs35l41, mboxcmd);
	}
	//perform digital unmute
	regmap_update_bits(cs35l41->regmap,0x6000,
			0x7ff << 3, 0x0 << 3 | 0x4 );
}

#endif

static int cs35l41_pcm_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{

	//struct snd_soc_codec *codec = dai->codec;
	struct cs35l41_private *cs35l41 =
			snd_soc_component_get_drvdata(dai->component);

	dev_info(cs35l41->dev,"%s.\n", __func__);

#if defined(CONFIG_TARGET_PRODUCT_CS35L41_TDM)
	cs35l41_set_dai_fmt(dai, SND_SOC_DAIFMT_CBS_CFS|SND_SOC_DAIFMT_DSP_A);
#else
	cs35l41_set_dai_fmt(dai, SND_SOC_DAIFMT_CBS_CFS|SND_SOC_DAIFMT_I2S);
#endif
	//cs35l41_codec_set_sysclk(codec, 0, 0, 1536000, 0);
#if 0
	if (substream->runtime)
		return snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE, &cs35l41_constraints);
#endif
	return 0;
}

static int cs35l41_component_set_sysclk(struct snd_soc_component *component,
				int clk_id, int source, unsigned int freq,
				int dir)
{
	struct cs35l41_private *cs35l41 =
				       snd_soc_component_get_drvdata(component);

	if (cs35l41->extclk_freq) {
		dev_info(cs35l41->dev, "%s: clock has beed configured, clk_id=%d, src=%d, freq=%d\n",
			__func__, clk_id, source, freq);
		return 0;
	}

	dev_info(cs35l41->dev, "%s: clk_id=%d, src=%d, freq=%d\n", __func__, clk_id, source, freq);

	switch (clk_id) {
	case 0:
		cs35l41->clksrc = CS35L41_PLLSRC_SCLK;
		break;
	case 1:
		cs35l41->clksrc = CS35L41_PLLSRC_LRCLK;
		break;
	case 2:
		cs35l41->clksrc = CS35L41_PLLSRC_PDMCLK;
		break;
	case 3:
		cs35l41->clksrc = CS35L41_PLLSRC_SELF;
		break;
	case 4:
		cs35l41->clksrc = CS35L41_PLLSRC_MCLK;
		break;
	default:
		dev_err(cs35l41->dev, "Invalid CLK Config\n");
		return -EINVAL;
	}

	cs35l41->extclk_cfg = cs35l41_get_clk_config(freq);

	if (cs35l41->extclk_cfg < 0) {
		dev_err(cs35l41->dev, "Invalid CLK Config: %d, freq: %u\n",
			cs35l41->extclk_cfg, freq);
		return -EINVAL;
	}

	regmap_update_bits(cs35l41->regmap, CS35L41_PLL_CLK_CTRL,
			CS35L41_PLL_OPENLOOP_MASK,
			1 << CS35L41_PLL_OPENLOOP_SHIFT);
	regmap_update_bits(cs35l41->regmap, CS35L41_PLL_CLK_CTRL,
			CS35L41_REFCLK_FREQ_MASK,
			cs35l41->extclk_cfg << CS35L41_REFCLK_FREQ_SHIFT);
	regmap_update_bits(cs35l41->regmap, CS35L41_PLL_CLK_CTRL,
			CS35L41_PLL_CLK_EN_MASK,
			0 << CS35L41_PLL_CLK_EN_SHIFT);
	regmap_update_bits(cs35l41->regmap, CS35L41_PLL_CLK_CTRL,
			CS35L41_PLL_CLK_SEL_MASK, cs35l41->clksrc);
	regmap_update_bits(cs35l41->regmap, CS35L41_PLL_CLK_CTRL,
			CS35L41_PLL_OPENLOOP_MASK,
			0 << CS35L41_PLL_OPENLOOP_SHIFT);
	regmap_update_bits(cs35l41->regmap, CS35L41_PLL_CLK_CTRL,
			CS35L41_PLL_CLK_EN_MASK,
			1 << CS35L41_PLL_CLK_EN_SHIFT);
	regmap_update_bits(cs35l41->regmap,
			CS35L41_PLL_CLK_CTRL,
			CS35L41_PLL_FORCE_EN_MASK,
			1 << CS35L41_PLL_FORCE_EN_SHIFT);

	cs35l41->extclk_freq = freq;


	return 0;
}

static int cs35l41_dai_set_sysclk(struct snd_soc_dai *dai,
					int clk_id, unsigned int freq, int dir)
{
	struct cs35l41_private *cs35l41 =
				  snd_soc_component_get_drvdata(dai->component);
	unsigned int fs1_val = 0;
	unsigned int fs2_val = 0;
	unsigned int val;

	dev_info(cs35l41->dev, "%s: clk_id=%d, freq=%d, dir=%d\n", __func__, clk_id, freq, dir);
	if (cs35l41_get_clk_config(freq) < 0) {
		dev_err(cs35l41->dev, "Invalid CLK Config freq: %u\n", freq);
		return -EINVAL;
	}

	cs35l41->sclk = freq;
	dev_dbg(cs35l41->dev, "Set DAI sysclk %d\n", freq);
	if (cs35l41->sclk > 6000000) {
		fs1_val = 3 * 4 + 4;
		fs2_val = 8 * 4 + 4;
	}
	if (cs35l41->sclk <= 6000000) {
		fs1_val = 3 * ((24000000 + cs35l41->sclk - 1) / cs35l41->sclk) + 4;
		fs2_val = 5 * ((24000000 + cs35l41->sclk - 1) / cs35l41->sclk) + 4;
	}

	val = fs1_val;
	val |= (fs2_val << CS35L41_FS2_WINDOW_SHIFT) & CS35L41_FS2_WINDOW_MASK;
	regmap_write(cs35l41->regmap, CS35L41_TEST_KEY_CTL, 0x00000055);
	regmap_write(cs35l41->regmap, CS35L41_TEST_KEY_CTL, 0x000000AA);
	regmap_write(cs35l41->regmap, CS35L41_TST_FS_MON0, val);
	regmap_write(cs35l41->regmap, CS35L41_TEST_KEY_CTL, 0x000000CC);
	regmap_write(cs35l41->regmap, CS35L41_TEST_KEY_CTL, 0x00000033);

	return 0;
}

static int  cs35l41_digital_mute(struct snd_soc_dai *dai, int mute)
{

	struct cs35l41_private *cs35l41 =
				  snd_soc_component_get_drvdata(dai->component);

	dev_info(cs35l41->dev, "%s: %d\n", __func__, mute);

	if (mute) {
		regmap_update_bits(cs35l41->regmap,CS35L41_AMP_DIG_VOL_CTRL,
				0x7ff << 3, 0x400 << 3 | 0x7);
		mdelay(30);
	} else {
		regmap_update_bits(cs35l41->regmap,CS35L41_AMP_DIG_VOL_CTRL,
				0x7ff << 3, 0x0 << 3 | 0x4 );
	}

	return 0;
}

static int cs35l41_boost_config(struct cs35l41_private *cs35l41,
		int boost_ind, int boost_cap, int boost_ipk)
{
	int ret;
	unsigned char bst_lbst_val, bst_cbst_range, bst_ipk_scaled;
	struct regmap *regmap = cs35l41->regmap;
	struct device *dev = cs35l41->dev;

	switch (boost_ind) {
	case 1000:	/* 1.0 uH */
		bst_lbst_val = 0;
		break;
	case 1200:	/* 1.2 uH */
		bst_lbst_val = 1;
		break;
	case 1500:	/* 1.5 uH */
		bst_lbst_val = 2;
		break;
	case 2200:	/* 2.2 uH */
		bst_lbst_val = 3;
		break;
	default:
		dev_err(dev, "Invalid boost inductor value: %d nH\n",
				boost_ind);
		return -EINVAL;
	}

	switch (boost_cap) {
	case 0 ... 19:
		bst_cbst_range = 0;
		break;
	case 20 ... 50:
		bst_cbst_range = 1;
		break;
	case 51 ... 100:
		bst_cbst_range = 2;
		break;
	case 101 ... 200:
		bst_cbst_range = 3;
		break;
	default:	/* 201 uF and greater */
		bst_cbst_range = 4;
	}

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_COEFF,
			CS35L41_BST_K1_MASK,
			cs35l41_bst_k1_table[bst_lbst_val][bst_cbst_range]
				<< CS35L41_BST_K1_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost K1 coefficient\n");
		return ret;
	}

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_COEFF,
			CS35L41_BST_K2_MASK,
			cs35l41_bst_k2_table[bst_lbst_val][bst_cbst_range]
				<< CS35L41_BST_K2_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost K2 coefficient\n");
		return ret;
	}

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_SLOPE_LBST,
			CS35L41_BST_SLOPE_MASK,
			cs35l41_bst_slope_table[bst_lbst_val]
				<< CS35L41_BST_SLOPE_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost slope coefficient\n");
		return ret;
	}

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_SLOPE_LBST,
			CS35L41_BST_LBST_VAL_MASK,
			bst_lbst_val << CS35L41_BST_LBST_VAL_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost inductor value\n");
		return ret;
	}

	if ((boost_ipk < 1600) || (boost_ipk > 4500)) {
		dev_err(dev, "Invalid boost inductor peak current: %d mA\n",
				boost_ipk);
		return -EINVAL;
	}
	bst_ipk_scaled = ((boost_ipk - 1600) / 50) + 0x10;

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_PEAK_CUR,
			CS35L41_BST_IPK_MASK,
			bst_ipk_scaled << CS35L41_BST_IPK_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost inductor peak current\n");
		return ret;
	}

	return 0;
}

static int cs35l41_component_probe(struct snd_soc_component *component)
{
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct classh_cfg *classh = &cs35l41->pdata.classh_config;
	struct snd_kcontrol_new	*kcontrol;
	int ret;

	component->regmap = cs35l41->regmap;

	/* Set Platform Data */
	/* Required */
	if (cs35l41->pdata.bst_ipk &&
			cs35l41->pdata.bst_ind && cs35l41->pdata.bst_cap) {
		ret = cs35l41_boost_config(cs35l41, cs35l41->pdata.bst_ind,
					cs35l41->pdata.bst_cap,
					cs35l41->pdata.bst_ipk);
		if (ret) {
			dev_err(cs35l41->dev, "Error in Boost DT config\n");
			return ret;
		}
	} else {
		dev_err(cs35l41->dev, "Incomplete Boost component DT config\n");
		return -EINVAL;
	}

	/* Optional */
	if (cs35l41->pdata.sclk_frc)
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_SCLK_FRC_MASK,
				cs35l41->pdata.sclk_frc <<
				CS35L41_SCLK_FRC_SHIFT);

	if (cs35l41->pdata.lrclk_frc)
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_LRCLK_FRC_MASK,
				cs35l41->pdata.lrclk_frc <<
				CS35L41_LRCLK_FRC_SHIFT);

	if (cs35l41->pdata.amp_gain_zc)
		regmap_update_bits(cs35l41->regmap, CS35L41_AMP_GAIN_CTRL,
				CS35L41_AMP_GAIN_ZC_MASK,
				cs35l41->pdata.amp_gain_zc <<
				CS35L41_AMP_GAIN_ZC_SHIFT);

	if (cs35l41->pdata.bst_vctrl)
		regmap_update_bits(cs35l41->regmap, CS35L41_BSTCVRT_VCTRL1,
				CS35L41_BST_CTL_MASK, cs35l41->pdata.bst_vctrl);

	if (cs35l41->pdata.temp_warn_thld)
		regmap_update_bits(cs35l41->regmap, CS35L41_DTEMP_WARN_THLD,
				CS35L41_TEMP_THLD_MASK,
				cs35l41->pdata.temp_warn_thld);

	if (cs35l41->pdata.dout_hiz <= CS35L41_ASP_DOUT_HIZ_MASK &&
	    cs35l41->pdata.dout_hiz >= 0)
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_HIZ_CTRL,
				CS35L41_ASP_DOUT_HIZ_MASK,
				cs35l41->pdata.dout_hiz);

	if (cs35l41->pdata.ng_enable) {
		regmap_update_bits(cs35l41->regmap,
				CS35L41_MIXER_NGATE_CH1_CFG,
				CS35L41_NG_ENABLE_MASK,
				CS35L41_NG_ENABLE_MASK);
		regmap_update_bits(cs35l41->regmap,
				CS35L41_MIXER_NGATE_CH2_CFG,
				CS35L41_NG_ENABLE_MASK,
				CS35L41_NG_ENABLE_MASK);

		if (cs35l41->pdata.ng_pcm_thld) {
			regmap_update_bits(cs35l41->regmap,
				CS35L41_MIXER_NGATE_CH1_CFG,
				CS35L41_NG_THLD_MASK,
				cs35l41->pdata.ng_pcm_thld);
			regmap_update_bits(cs35l41->regmap,
				CS35L41_MIXER_NGATE_CH2_CFG,
				CS35L41_NG_THLD_MASK,
				cs35l41->pdata.ng_pcm_thld);
		}

		if (cs35l41->pdata.ng_delay) {
			regmap_update_bits(cs35l41->regmap,
				CS35L41_MIXER_NGATE_CH1_CFG,
				CS35L41_NG_DELAY_MASK,
				cs35l41->pdata.ng_delay <<
				CS35L41_NG_DELAY_SHIFT);
			regmap_update_bits(cs35l41->regmap,
				CS35L41_MIXER_NGATE_CH2_CFG,
				CS35L41_NG_DELAY_MASK,
				cs35l41->pdata.ng_delay <<
				CS35L41_NG_DELAY_SHIFT);
		}
	}

	if (classh->classh_algo_enable) {
		if (classh->classh_bst_override)
			regmap_update_bits(cs35l41->regmap,
					CS35L41_BSTCVRT_VCTRL2,
					CS35L41_BST_CTL_SEL_MASK,
					CS35L41_BST_CTL_SEL_REG);
		if (classh->classh_bst_max_limit)
			regmap_update_bits(cs35l41->regmap,
					CS35L41_BSTCVRT_VCTRL2,
					CS35L41_BST_LIM_MASK,
					classh->classh_bst_max_limit <<
					CS35L41_BST_LIM_SHIFT);
		if (classh->classh_mem_depth)
			regmap_update_bits(cs35l41->regmap,
					CS35L41_CLASSH_CFG,
					CS35L41_CH_MEM_DEPTH_MASK,
					classh->classh_mem_depth <<
					CS35L41_CH_MEM_DEPTH_SHIFT);
		if (classh->classh_headroom)
			regmap_update_bits(cs35l41->regmap,
					CS35L41_CLASSH_CFG,
					CS35L41_CH_HDRM_CTL_MASK,
					classh->classh_headroom <<
					CS35L41_CH_HDRM_CTL_SHIFT);
		if (classh->classh_release_rate)
			regmap_update_bits(cs35l41->regmap,
					CS35L41_CLASSH_CFG,
					CS35L41_CH_REL_RATE_MASK,
					classh->classh_release_rate <<
					CS35L41_CH_REL_RATE_SHIFT);
		if (classh->classh_wk_fet_delay)
			regmap_update_bits(cs35l41->regmap,
					CS35L41_WKFET_CFG,
					CS35L41_CH_WKFET_DLY_MASK,
					classh->classh_wk_fet_delay <<
					CS35L41_CH_WKFET_DLY_SHIFT);
		if (classh->classh_wk_fet_thld)
			regmap_update_bits(cs35l41->regmap,
					CS35L41_WKFET_CFG,
					CS35L41_CH_WKFET_THLD_MASK,
					classh->classh_wk_fet_thld <<
					CS35L41_CH_WKFET_THLD_SHIFT);

		regmap_update_bits(cs35l41->regmap,
				CS35L41_PLL_CLK_CTRL,
				CS35L41_PLL_FORCE_EN_MASK,
				1 << CS35L41_PLL_FORCE_EN_SHIFT);
	}

	wm_adsp2_component_probe(&cs35l41->dsp, component);
	if (cs35l41->pdata.right_channel) {
		snd_soc_dapm_ignore_suspend(dapm, "AMP Playback");
		snd_soc_dapm_ignore_suspend(dapm, "AMP Capture");
		snd_soc_dapm_ignore_suspend(dapm, "Main AMP");
		snd_soc_dapm_ignore_suspend(dapm, "SPK");
		snd_soc_dapm_ignore_suspend(dapm, "VP");
		snd_soc_dapm_ignore_suspend(dapm, "VBST");
		snd_soc_dapm_ignore_suspend(dapm, "ISENSE");
		snd_soc_dapm_ignore_suspend(dapm, "VSENSE");
		snd_soc_dapm_ignore_suspend(dapm, "TEMP");
		snd_soc_dapm_ignore_suspend(dapm, "DSP1 Preloader");
		snd_soc_dapm_ignore_suspend(dapm, "DSP1 Preload");
		#if defined(CONFIG_TARGET_PRODUCT_STAR)
		regmap_write(cs35l41->regmap, CS35L41_VPBR_CFG, 0x2005305);
		#endif
		#if defined(CONFIG_TARGET_PRODUCT_HAYDN)
		regmap_write(cs35l41->regmap, CS35L41_VPBR_CFG, 0x2005304);
		#endif
	} else {
		snd_soc_dapm_ignore_suspend(dapm, "RCV AMP Playback");
		snd_soc_dapm_ignore_suspend(dapm, "RCV AMP Capture");
		snd_soc_dapm_ignore_suspend(dapm, "RCV Main AMP");
		snd_soc_dapm_ignore_suspend(dapm, "RCV SPK");
		snd_soc_dapm_ignore_suspend(dapm, "RCV VP");
		snd_soc_dapm_ignore_suspend(dapm, "RCV VBST");
		snd_soc_dapm_ignore_suspend(dapm, "RCV ISENSE");
		snd_soc_dapm_ignore_suspend(dapm, "RCV VSENSE");
		snd_soc_dapm_ignore_suspend(dapm, "RCV TEMP");
		snd_soc_dapm_ignore_suspend(dapm, "RCV DSP1 Preloader");
		snd_soc_dapm_ignore_suspend(dapm, "RCV DSP1 Preload");
		#if defined(CONFIG_TARGET_PRODUCT_STAR)
		regmap_write(cs35l41->regmap, CS35L41_VPBR_CFG, 0x2005306);
		#endif
		#if defined(CONFIG_TARGET_PRODUCT_HAYDN)
		regmap_write(cs35l41->regmap, CS35L41_VPBR_CFG, 0x2005304);
		regmap_write(cs35l41->regmap, CS35L41_DAC_MSM_CFG, 0x00200000);
		#endif
	}
/* Add run-time mixer control for fast use case switch */
	kcontrol = kzalloc(sizeof(*kcontrol), GFP_KERNEL);
	if (!kcontrol) {
		ret = -ENOMEM;
		goto exit;
	}

	kcontrol->name	= "Fast Use Case Delta File";
	kcontrol->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kcontrol->info	= snd_soc_info_enum_double;
	kcontrol->get	= cs35l41_fast_switch_file_get;
	kcontrol->put	= cs35l41_fast_switch_file_put;
	kcontrol->private_value	= (unsigned long)&cs35l41->fast_switch_enum;
	ret = snd_soc_add_component_controls(component, kcontrol, 1);
	if (ret < 0)
		dev_err(cs35l41->dev,
			"snd_soc_add_component_controls failed (%d)\n", ret);
	kfree(kcontrol);
exit:
	return ret;

}

static int cs35l41_irq_gpio_config(struct cs35l41_private *cs35l41)
{
	struct irq_cfg *irq_gpio_cfg1 = &cs35l41->pdata.irq_config1;
	struct irq_cfg *irq_gpio_cfg2 = &cs35l41->pdata.irq_config2;
	int irq_pol = IRQF_TRIGGER_NONE;

	if (irq_gpio_cfg1->is_present) {
		if (irq_gpio_cfg1->irq_pol_inv)
			regmap_update_bits(cs35l41->regmap,
						CS35L41_GPIO1_CTRL1,
						CS35L41_GPIO_POL_MASK,
						CS35L41_GPIO_POL_MASK);
		if (irq_gpio_cfg1->irq_out_en)
			regmap_update_bits(cs35l41->regmap,
						CS35L41_GPIO1_CTRL1,
						CS35L41_GPIO_DIR_MASK,
						0);
		if (irq_gpio_cfg1->irq_src_sel)
			regmap_update_bits(cs35l41->regmap,
						CS35L41_GPIO_PAD_CONTROL,
						CS35L41_GPIO1_CTRL_MASK,
						irq_gpio_cfg1->irq_src_sel <<
						CS35L41_GPIO1_CTRL_SHIFT);
	}

	if (irq_gpio_cfg2->is_present) {
		if (irq_gpio_cfg2->irq_pol_inv)
			regmap_update_bits(cs35l41->regmap,
						CS35L41_GPIO2_CTRL1,
						CS35L41_GPIO_POL_MASK,
						CS35L41_GPIO_POL_MASK);
		if (irq_gpio_cfg2->irq_out_en)
			regmap_update_bits(cs35l41->regmap,
						CS35L41_GPIO2_CTRL1,
						CS35L41_GPIO_DIR_MASK,
						0);
		if (irq_gpio_cfg2->irq_src_sel)
			regmap_update_bits(cs35l41->regmap,
						CS35L41_GPIO_PAD_CONTROL,
						CS35L41_GPIO2_CTRL_MASK,
						irq_gpio_cfg2->irq_src_sel <<
						CS35L41_GPIO2_CTRL_SHIFT);
	}

#if defined(CONFIG_TARGET_PRODUCT_ODIN)
	if (irq_gpio_cfg2->irq_src_sel ==
			(CS35L41_GPIO_CTRL_ACTV_LO | CS35L41_VALID_PDATA))
		irq_pol = IRQF_TRIGGER_FALLING;
	else if (irq_gpio_cfg2->irq_src_sel ==
			(CS35L41_GPIO_CTRL_ACTV_HI | CS35L41_VALID_PDATA))
		irq_pol = IRQF_TRIGGER_RISING;
#else
	if (irq_gpio_cfg2->irq_src_sel ==
			(CS35L41_GPIO_CTRL_ACTV_LO | CS35L41_VALID_PDATA))
		irq_pol = IRQF_TRIGGER_LOW;
	else if (irq_gpio_cfg2->irq_src_sel ==
			(CS35L41_GPIO_CTRL_ACTV_HI | CS35L41_VALID_PDATA))
		irq_pol = IRQF_TRIGGER_HIGH;
#endif

	return irq_pol;
}

static void cs35l41_component_remove(struct snd_soc_component *component)
{
	struct cs35l41_private *cs35l41 = snd_soc_component_get_drvdata(component);

	wm_adsp2_component_remove(&cs35l41->dsp, component);
}

static const struct snd_soc_dai_ops cs35l41_ops = {
	.startup = cs35l41_pcm_startup,
	.set_fmt = cs35l41_set_dai_fmt,
	.hw_params = cs35l41_pcm_hw_params,
	.set_sysclk = cs35l41_dai_set_sysclk,
	.digital_mute = cs35l41_digital_mute,
};

static struct snd_soc_dai_driver cs35l41_dai[] = {
	{
		.name = "cs35l41-pcm",
		.id = 0,
		.playback = {
			.stream_name = "AMP Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = CS35L41_RX_FORMATS,
		},
		.capture = {
			.stream_name = "AMP Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = CS35L41_TX_FORMATS,
		},
		.ops = &cs35l41_ops,
		.symmetric_rates = 1,
	},
};

static struct snd_soc_component_driver soc_component_dev_cs35l41 = {
	.probe = cs35l41_component_probe,
	.remove = cs35l41_component_remove,

	.dapm_widgets = cs35l41_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cs35l41_dapm_widgets),
	.dapm_routes = cs35l41_audio_map,
	.num_dapm_routes = ARRAY_SIZE(cs35l41_audio_map),

	.controls = cs35l41_aud_controls,
	.num_controls = ARRAY_SIZE(cs35l41_aud_controls),
	.set_sysclk = cs35l41_component_set_sysclk,
};


static int cs35l41_handle_of_data(struct device *dev,
				  struct cs35l41_platform_data *pdata,
				  struct cs35l41_private *cs35l41)
{
	struct device_node *np = dev->of_node;
	unsigned int val;
	int ret;
	size_t	num_fast_switch;
	struct device_node *sub_node;
	struct classh_cfg *classh_config = &pdata->classh_config;
	struct irq_cfg *irq_gpio1_config = &pdata->irq_config1;
	struct irq_cfg *irq_gpio2_config = &pdata->irq_config2;
	unsigned int i;
	u32 is_dev_mars = 0;

	if (!np)
		return 0;

	ret = of_property_count_strings(np, "cirrus,fast-switch");
	if (ret < 0) {
		/*
		 * device tree do not provide file name.
		 * Use default value
		 */

		num_fast_switch		= ARRAY_SIZE(cs35l41_fast_switch_text);
		cs35l41->fast_switch_enum.items	=
					ARRAY_SIZE(cs35l41_fast_switch_text);
		cs35l41->fast_switch_enum.texts	= cs35l41_fast_switch_text;
		cs35l41->fast_switch_names = cs35l41_fast_switch_text;
	} else {
		/* Device tree provides file name */
		num_fast_switch			= (size_t)ret;
		dev_info(dev, "num_fast_switch:%zu\n", num_fast_switch);
		cs35l41->fast_switch_names =
			devm_kmalloc(dev, num_fast_switch * sizeof(char *),
				     GFP_KERNEL);
		if (!cs35l41->fast_switch_names)
			return -ENOMEM;
		of_property_read_string_array(np, "cirrus,fast-switch",
					      cs35l41->fast_switch_names,
					      num_fast_switch);
		for (i = 0; i < num_fast_switch; i++) {
			dev_info(dev, "%d:%s\n", i,
				 cs35l41->fast_switch_names[i]);
		}
		cs35l41->fast_switch_enum.items	= num_fast_switch;
		cs35l41->fast_switch_enum.texts	= cs35l41->fast_switch_names;
	}

	of_property_read_u32(np, "cirrus,is-mars-pa", &is_dev_mars);

	cs35l41->fast_switch_enum.reg		= SND_SOC_NOPM;
	if (is_dev_mars != 0) {
		cs35l41->fast_switch_enum.shift_l	= 1;
		cs35l41->fast_switch_enum.shift_r	= 1;
	} else {
		cs35l41->fast_switch_enum.shift_l	= 0;
		cs35l41->fast_switch_enum.shift_r	= 0;
	}
	cs35l41->fast_switch_enum.mask		=
		roundup_pow_of_two(num_fast_switch) - 1;

	pdata->right_channel = of_property_read_bool(np,
					"cirrus,right-channel-amp");
	pdata->sclk_frc = of_property_read_bool(np,
					"cirrus,sclk-force-output");
	pdata->lrclk_frc = of_property_read_bool(np,
					"cirrus,lrclk-force-output");
	pdata->amp_gain_zc = of_property_read_bool(np,
					"cirrus,amp-gain-zc");

	if (of_property_read_u32(np, "cirrus,temp-warn_threshold", &val) >= 0)
		pdata->temp_warn_thld = val | CS35L41_VALID_PDATA;

	ret = of_property_read_u32(np, "cirrus,boost-ctl-millivolt", &val);
	if (ret >= 0) {
		if (val < 2550 || val > 11000) {
			dev_err(dev,
				"Invalid Boost Voltage %u mV\n", val);
			return -EINVAL;
		}
		pdata->bst_vctrl = ((val - 2550) / 100) + 1;
	}

	ret = of_property_read_u32(np, "cirrus,boost-peak-milliamp", &val);
	if (ret >= 0)
		pdata->bst_ipk = val;

	ret = of_property_read_u32(np, "cirrus,boost-ind-nanohenry", &val);
	if (ret >= 0)
		pdata->bst_ind = val;

	ret = of_property_read_u32(np, "cirrus,boost-cap-microfarad", &val);
	if (ret >= 0)
		pdata->bst_cap = val;

	ret = of_property_read_u32(np, "cirrus,asp-sdout-hiz", &val);
	if (ret >= 0)
		pdata->dout_hiz = val;
	else
		pdata->dout_hiz = -1;

	pdata->ng_enable = of_property_read_bool(np,
					"cirrus,noise-gate-enable");
	if (of_property_read_u32(np, "cirrus,noise-gate-threshold", &val) >= 0)
		pdata->ng_pcm_thld = val | CS35L41_VALID_PDATA;
	if (of_property_read_u32(np, "cirrus,noise-gate-delay", &val) >= 0)
		pdata->ng_delay = val | CS35L41_VALID_PDATA;


	sub_node = of_get_child_by_name(np, "cirrus,classh-internal-algo");
	classh_config->classh_algo_enable = sub_node ? true : false;

	if (classh_config->classh_algo_enable) {
		classh_config->classh_bst_override =
			of_property_read_bool(sub_node,
				"cirrus,classh-bst-overide");

		ret = of_property_read_u32(sub_node,
					   "cirrus,classh-bst-max-limit",
					   &val);
		if (ret >= 0) {
			val |= CS35L41_VALID_PDATA;
			classh_config->classh_bst_max_limit = val;
		}

		ret = of_property_read_u32(sub_node, "cirrus,classh-mem-depth",
					   &val);
		if (ret >= 0) {
			val |= CS35L41_VALID_PDATA;
			classh_config->classh_mem_depth = val;
		}

		ret = of_property_read_u32(sub_node,
					"cirrus,classh-release-rate", &val);
		if (ret >= 0)
			classh_config->classh_release_rate = val;

		ret = of_property_read_u32(sub_node, "cirrus,classh-headroom",
					   &val);
		if (ret >= 0) {
			val |= CS35L41_VALID_PDATA;
			classh_config->classh_headroom = val;
		}

		ret = of_property_read_u32(sub_node,
					"cirrus,classh-wk-fet-delay", &val);
		if (ret >= 0) {
			val |= CS35L41_VALID_PDATA;
			classh_config->classh_wk_fet_delay = val;
		}

		ret = of_property_read_u32(sub_node,
					"cirrus,classh-wk-fet-thld", &val);
		if (ret >= 0)
			classh_config->classh_wk_fet_thld = val;
	}
	of_node_put(sub_node);

	/* GPIO1 Pin Config */
	sub_node = of_get_child_by_name(np, "cirrus,gpio-config1");
	irq_gpio1_config->is_present = sub_node ? true : false;
	if (irq_gpio1_config->is_present) {
		irq_gpio1_config->irq_pol_inv = of_property_read_bool(sub_node,
						"cirrus,gpio-polarity-invert");
		irq_gpio1_config->irq_out_en = of_property_read_bool(sub_node,
						"cirrus,gpio-output-enable");
		ret = of_property_read_u32(sub_node, "cirrus,gpio-src-select",
					&val);
		if (ret >= 0) {
			val |= CS35L41_VALID_PDATA;
			irq_gpio1_config->irq_src_sel = val;
		}
	}
	of_node_put(sub_node);

	/* GPIO2 Pin Config */
	sub_node = of_get_child_by_name(np, "cirrus,gpio-config2");
	irq_gpio2_config->is_present = sub_node ? true : false;
	if (irq_gpio2_config->is_present) {
		irq_gpio2_config->irq_pol_inv = of_property_read_bool(sub_node,
						"cirrus,gpio-polarity-invert");
		irq_gpio2_config->irq_out_en = of_property_read_bool(sub_node,
						"cirrus,gpio-output-enable");
		ret = of_property_read_u32(sub_node, "cirrus,gpio-src-select",
					&val);
		if (ret >= 0) {
			val |= CS35L41_VALID_PDATA;
			irq_gpio2_config->irq_src_sel = val;
		}
	}
	of_node_put(sub_node);

	return 0;
}

static const struct reg_sequence cs35l41_reva0_errata_patch[] = {
	{0x00000040,			0x00005555},
	{0x00000040,			0x0000AAAA},
	{0x00003854,			0x05180240},
	{CS35L41_VIMON_SPKMON_RESYNC,	0x00000000},
	{0x00004310,			0x00000000},
	{CS35L41_VPVBST_FS_SEL,		0x00000000},
	{CS35L41_OTP_TRIM_30,		0x9091A1C8},
	{0x00003014,			0x0200EE0E},
	{CS35L41_BSTCVRT_DCM_CTRL,	0x00000051},
	{0x00000054,			0x00000004},
	{CS35L41_IRQ1_DB3,		0x00000000},
	{CS35L41_IRQ2_DB3,		0x00000000},
	{CS35L41_DSP1_YM_ACCEL_PL0_PRI,	0x00000000},
	{CS35L41_DSP1_XM_ACCEL_PL0_PRI,	0x00000000},
	{CS35L41_ASP_CONTROL4,		0x01010000},
	{0x00000040,			0x0000CCCC},
	{0x00000040,			0x00003333},
};

static const struct reg_sequence cs35l41_revb0_errata_patch[] = {
	{0x00000040,			0x00005555},
	{0x00000040,			0x0000AAAA},
	{CS35L41_VIMON_SPKMON_RESYNC,	0x00000000},
	{0x00004310,			0x00000000},
	{CS35L41_VPVBST_FS_SEL,		0x00000000},
	{CS35L41_BSTCVRT_DCM_CTRL,	0x00000051},
	{CS35L41_DSP1_YM_ACCEL_PL0_PRI,	0x00000000},
	{CS35L41_DSP1_XM_ACCEL_PL0_PRI,	0x00000000},
	{CS35L41_ASP_CONTROL4,		0x01010000},
	{0x00000040,			0x0000CCCC},
	{0x00000040,			0x00003333},
};

static const struct reg_sequence cs35l41_revb2_errata_patch[] = {
	{0x00000040,			0x00005555},
	{0x00000040,			0x0000AAAA},
	{CS35L41_VIMON_SPKMON_RESYNC,	0x00000000},
	{0x00004310,			0x00000000},
	{CS35L41_VPVBST_FS_SEL,		0x00000000},
	{CS35L41_BSTCVRT_DCM_CTRL,	0x00000051},
	{CS35L41_DSP1_YM_ACCEL_PL0_PRI,	0x00000000},
	{CS35L41_DSP1_XM_ACCEL_PL0_PRI,	0x00000000},
	{CS35L41_ASP_CONTROL4,		0x01010000},
	{0x00000040,			0x0000CCCC},
	{0x00000040,			0x00003333},
};

static int cs35l41_dsp_init(struct cs35l41_private *cs35l41)
{
	struct wm_adsp *dsp;
	int ret, i;
	u32 chip_revid;

	dsp = &cs35l41->dsp;
	dsp->part = "cs35l41";
	dsp->num = 1;
	dsp->type = WMFW_HALO;
	dsp->rev = 0;
	dsp->dev = cs35l41->dev;
	dsp->regmap = cs35l41->regmap;

	dsp->base = CS35L41_DSP1_CTRL_BASE;
	dsp->base_sysinfo = CS35L41_DSP1_SYS_ID;
	dsp->mem = cs35l41_dsp1_regions;
	dsp->num_mems = ARRAY_SIZE(cs35l41_dsp1_regions);

	dsp->n_rx_channels = CS35L41_DSP_N_RX_RATES;
	dsp->n_tx_channels = CS35L41_DSP_N_TX_RATES;
	ret = wm_halo_init(dsp, &cs35l41->rate_lock);
	cs35l41->halo_booted = false;

	for (i = 0; i < CS35L41_DSP_N_RX_RATES; i++)
		dsp->rx_rate_cache[i] = 0x1;
	for (i = 0; i < CS35L41_DSP_N_TX_RATES; i++)
		dsp->tx_rate_cache[i] = 0x1;

	regmap_write(cs35l41->regmap, CS35L41_DSP1_RX5_SRC,
					CS35L41_INPUT_SRC_VPMON);
	regmap_write(cs35l41->regmap, CS35L41_DSP1_RX6_SRC,
					CS35L41_INPUT_SRC_CLASSH);
	regmap_write(cs35l41->regmap, CS35L41_DSP1_RX7_SRC,
					CS35L41_INPUT_SRC_TEMPMON);
	regmap_write(cs35l41->regmap, CS35L41_DSP1_RX8_SRC,
					CS35L41_INPUT_SRC_RSVD);
	
	ret = regmap_read(cs35l41->regmap, CS35L41_REVID, &chip_revid);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Get Revision ID failed\n");
		return ret;
	} else {
		dsp->chip_revid = chip_revid;
	}

	return ret;
}

#if defined(CONFIG_TARGET_PRODUCT_CS35L41_TDM)
static int cs35l41_96k_sample_rate_init(struct cs35l41_private *cs35l41)
{
	int i;
	unsigned int rate = 96000;

	// Initialize clock, sample rate, ASP format etc. for ultrasonic
	for (i = 0; i < ARRAY_SIZE(cs35l41_fs_rates); i++) {
		if (rate == cs35l41_fs_rates[i].rate)
			break;
	}
	regmap_update_bits(cs35l41->regmap, CS35L41_GLOBAL_CLK_CTRL,
			CS35L41_GLOBAL_FS_MASK,
			cs35l41_fs_rates[i].fs_cfg << CS35L41_GLOBAL_FS_SHIFT);

	return 0;
}
#endif

int cs35l41_probe(struct cs35l41_private *cs35l41,
				struct cs35l41_platform_data *pdata)
{
	int ret;
	u32 regid, reg_revid, i, mtl_revid, int_status, chipid_match;
	int timeout = 100;
	int irq_pol = 0;
	cs35l41->fast_switch_en = false;
	cs35l41->fast_switch_file_idx = 0;
	cs35l41->reload_tuning = false;
	for (i = 0; i < ARRAY_SIZE(cs35l41_supplies); i++)
		cs35l41->supplies[i].supply = cs35l41_supplies[i];

	cs35l41->num_supplies = ARRAY_SIZE(cs35l41_supplies);

	ret = devm_regulator_bulk_get(cs35l41->dev, cs35l41->num_supplies,
					cs35l41->supplies);
	if (ret != 0) {
		dev_err(cs35l41->dev,
			"Failed to request core supplies: %d\n",
			ret);
		return ret;
	}

	if (pdata) {
		cs35l41->pdata = *pdata;
	} else if (cs35l41->dev->of_node) {
		ret = cs35l41_handle_of_data(cs35l41->dev, &cs35l41->pdata,
					     cs35l41);
		if (ret != 0)
			return ret;
	} else {
		ret = -EINVAL;
		goto err;
	}

	ret = regulator_bulk_enable(cs35l41->num_supplies, cs35l41->supplies);
	if (ret != 0) {
		dev_err(cs35l41->dev,
			"Failed to enable core supplies: %d\n", ret);
		return ret;
	}

/* returning NULL can be an option if in stereo mode */
	cs35l41->reset_gpio = devm_gpiod_get_optional(cs35l41->dev, "reset",
							GPIOD_OUT_LOW);
	if (IS_ERR(cs35l41->reset_gpio)) {
		ret = PTR_ERR(cs35l41->reset_gpio);
		cs35l41->reset_gpio = NULL;
		if (ret == -EBUSY) {
			dev_info(cs35l41->dev,
				 "Reset line busy, assuming shared reset\n");
		} else {
			dev_err(cs35l41->dev,
				"Failed to get reset GPIO: %d\n", ret);
			goto err;
		}
	}
	if (cs35l41->reset_gpio) {
		/* satisfy minimum reset pulse width spec */
		usleep_range(2000, 2100);
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 1);
	}

	cs35l41->spk_sw_gpio = devm_gpiod_get_optional(cs35l41->dev, "spksw",
							GPIOD_OUT_LOW);
	if (IS_ERR(cs35l41->spk_sw_gpio)) {
		ret = PTR_ERR(cs35l41->spk_sw_gpio);
		cs35l41->spk_sw_gpio = NULL;
		if (ret == -EBUSY) {
			dev_info(cs35l41->dev,
				 "spk_rev_sw line busy, assuming shared reset\n");
		} else {
			dev_err(cs35l41->dev,
				"Failed to get spk_rev_sw GPIO: %d\n", ret);
			goto err;
		}
	}
	if (cs35l41->spk_sw_gpio) {
		/* satisfy minimum reset pulse width spec */
		usleep_range(2000, 2100);
		dev_info(cs35l41->dev, "set the spk_rev_sw GPIO: 0\n");
		gpiod_set_value_cansleep(cs35l41->spk_sw_gpio, 0);
	}

	usleep_range(2000, 2100);

	do {
		if (timeout == 0) {
			dev_err(cs35l41->dev,
				"Timeout waiting for OTP_BOOT_DONE\n");
			ret = -EBUSY;
			goto err;
		}
		usleep_range(1000, 1100);
		regmap_read(cs35l41->regmap, CS35L41_IRQ1_STATUS4, &int_status);
		timeout--;
	} while (!(int_status & CS35L41_OTP_BOOT_DONE));

	regmap_read(cs35l41->regmap, CS35L41_IRQ1_STATUS3, &int_status);
	if (int_status & CS35L41_OTP_BOOT_ERR) {
		dev_err(cs35l41->dev, "OTP Boot error\n");
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_DEVID, &regid);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Get Device ID failed\n");
		goto err;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_REVID, &reg_revid);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Get Revision ID failed\n");
		goto err;
	}

	mtl_revid = reg_revid & CS35L41_MTLREVID_MASK;

	/* CS35L41 will have even MTLREVID
	*  CS35L41R will have odd MTLREVID
	*/
	chipid_match = (mtl_revid % 2) ? CS35L41R_CHIP_ID : CS35L41_CHIP_ID;
	if (regid != chipid_match) {
		dev_err(cs35l41->dev, "CS35L41 Device ID (%X). Expected ID %X\n",
			regid, chipid_match);
		ret = -ENODEV;
		goto err;
	}

	irq_pol = cs35l41_irq_gpio_config(cs35l41);

	cs35l41->dc_current_cnt = 0;

	ret = devm_request_threaded_irq(cs35l41->dev, cs35l41->irq, NULL,
				cs35l41_irq, irq_pol | IRQF_ONESHOT,
				"cs35l41", cs35l41);

	/* CS35L41 needs INT for PDN_DONE */
	if (ret != 0) {
		dev_err(cs35l41->dev, "Failed to request IRQ: %d\n", cs35l41->irq);
		//goto err;
	}

	/* Set interrupt masks for critical errors */
	regmap_write(cs35l41->regmap, CS35L41_IRQ1_MASK1,
			CS35L41_INT1_MASK_DEFAULT);

	switch (reg_revid) {
	case CS35L41_REVID_A0:
		ret = regmap_multi_reg_write(cs35l41->regmap,
				cs35l41_reva0_errata_patch,
				ARRAY_SIZE(cs35l41_reva0_errata_patch));
		if (ret < 0) {
			dev_err(cs35l41->dev,
				"Failed to apply A0 errata patch %d\n", ret);
			goto err;
		}
		break;
	case CS35L41_REVID_B0:
		ret = regmap_multi_reg_write(cs35l41->regmap,
				cs35l41_revb0_errata_patch,
				ARRAY_SIZE(cs35l41_revb0_errata_patch));
		if (ret < 0) {
			dev_err(cs35l41->dev,
				"Failed to apply B0 errata patch %d\n", ret);
			goto err;
		}
		break;
	case CS35L41_REVID_B2:
		ret = regmap_multi_reg_write(cs35l41->regmap,
				cs35l41_revb2_errata_patch,
				ARRAY_SIZE(cs35l41_revb2_errata_patch));
		if (ret < 0) {
			dev_err(cs35l41->dev,
				"Failed to apply B2 errata patch %d\n", ret);
			goto err;
		}
		break;
	}

	ret = cs35l41_otp_unpack(cs35l41);
	if (ret < 0) {
		dev_err(cs35l41->dev, "OTP Unpack failed\n");
		goto err;
	}

	regmap_write(cs35l41->regmap,
			CS35L41_DSP1_CCM_CORE_CTRL, 0);
	cs35l41_dsp_init(cs35l41);

	ret =  snd_soc_register_component(cs35l41->dev, &soc_component_dev_cs35l41,
					cs35l41_dai, ARRAY_SIZE(cs35l41_dai));
	if (ret < 0) {
		dev_err(cs35l41->dev, "%s: Register component failed\n", __func__);
		goto err;
	}
	//init brownout parameter
	ret = regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL3, 0x1000, 0x1000);
	ret = regmap_write(cs35l41->regmap, CS35L41_VPBR_CFG, 0x0200530E);

	#if defined(CONFIG_TARGET_PRODUCT_CS35L41_TDM)
	cs35l41_96k_sample_rate_init(cs35l41);
	#endif

	//external clock frequency initialize
	cs35l41->extclk_freq = 0;

	dev_info(cs35l41->dev, "Cirrus Logic CS35L41 (%x), Revision: %02X\n",
			regid, reg_revid);
	return 0;

err:
	regulator_bulk_disable(cs35l41->num_supplies, cs35l41->supplies);
	return ret;
}

MODULE_DESCRIPTION("ASoC CS35L41 driver");
MODULE_AUTHOR("David Rhodes, Cirrus Logic Inc, <david.rhodes@cirrus.com>");
MODULE_LICENSE("GPL");
