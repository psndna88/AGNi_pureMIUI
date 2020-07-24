/*
 * ALSA SoC Texas Instruments TAS2562 High Performance 4W Smart Amplifier
 *
 * Copyright (C) 2016 Texas Instruments, Inc.
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
#ifdef CONFIG_TAS2562_REGMAP

//#define DEBUG 5
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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pm.h>

#include "tas2562.h"
#include "tas2562-codec.h"
//#include "tas2562-misc.h"
#ifdef CONFIG_TAS25XX_ALGO
#include <dsp/tas_smart_amp_v2.h>
#endif /*CONFIG_TAS25XX_ALGO*/

static char p_icn[] = {0x00, 0x00, 0x2f, 0x2c};

static int tas2562_regmap_write(struct tas2562_priv *p_tas2562,
	unsigned int reg, unsigned int value)
{
	int nResult = 0;
	int retry_count = TAS2562_I2C_RETRY_COUNT;

	if(p_tas2562->i2c_suspend)
		return ERROR_I2C_SUSPEND;

	while(retry_count--)
	{
		nResult = regmap_write(p_tas2562->regmap, reg,
			value);
		if (nResult >= 0)
			break;
		msleep(20);
	}
	if(retry_count == -1)
		return ERROR_I2C_FAILED;
	else
		return 0;
}

static int tas2562_regmap_bulk_write(struct tas2562_priv *p_tas2562,
	unsigned int reg, unsigned char *pData, unsigned int nLength)
{
	int nResult = 0;
	int retry_count = TAS2562_I2C_RETRY_COUNT;

	if(p_tas2562->i2c_suspend)
		return ERROR_I2C_SUSPEND;

	while(retry_count --)
	{
		nResult = regmap_bulk_write(p_tas2562->regmap, reg,
			 pData, nLength);
		if (nResult >= 0)
			break;
		msleep(20);
	}
	if(retry_count == -1)
		return ERROR_I2C_FAILED;
	else
		return 0;
}

static int tas2562_regmap_read(struct tas2562_priv *p_tas2562,
	unsigned int reg, unsigned int *value)
{
	int nResult = 0;
	int retry_count = TAS2562_I2C_RETRY_COUNT;

	if(p_tas2562->i2c_suspend)
		return ERROR_I2C_SUSPEND;

	while(retry_count --)
	{
		nResult = regmap_read(p_tas2562->regmap, reg,
			value);
		if (nResult >= 0)
			break;
		msleep(20);
	}
	if(retry_count == -1)
		return ERROR_I2C_FAILED;
	else
		return 0;
}

static int tas2562_regmap_bulk_read(struct tas2562_priv *p_tas2562,
	unsigned int reg, unsigned char *pData, unsigned int nLength)
{
	int nResult = 0;
	int retry_count = TAS2562_I2C_RETRY_COUNT;

	if(p_tas2562->i2c_suspend)
		return ERROR_I2C_SUSPEND;

	while(retry_count --)
	{
		nResult = regmap_bulk_read(p_tas2562->regmap, reg,
			 pData, nLength);
		if (nResult >= 0)
			break;
		msleep(20);
	}
	if(retry_count == -1)
		return ERROR_I2C_FAILED;
	else
		return 0;
}

static int tas2562_regmap_update_bits(struct tas2562_priv *p_tas2562,
	unsigned int reg, unsigned int mask, unsigned int value)
{
	int nResult = 0;
	int retry_count = TAS2562_I2C_RETRY_COUNT;

	if(p_tas2562->i2c_suspend)
		return ERROR_I2C_SUSPEND;

	while(retry_count--)
	{
		nResult = regmap_update_bits(p_tas2562->regmap, reg,
			mask, value);
		if (nResult >= 0)
			break;
		msleep(20);
	}
	if(retry_count == -1)
		return ERROR_I2C_FAILED;
	else
		return 0;
}

static int tas2562_change_book_page(struct tas2562_priv *p_tas2562,
	enum channel chn,
	int book, int page)
{
	int n_result = 0;

	dev_dbg(p_tas2562->dev, "%s,  L=%d  chn:%d \n",
						__func__, __LINE__,chn);

	if ((chn&channel_left) || (p_tas2562->mn_channels == 1)) {
		p_tas2562->client->addr = p_tas2562->mn_l_addr;
		if (p_tas2562->mn_l_current_book != book) {
			dev_err(p_tas2562->dev, "%s,  L=%d\n",
						__func__, __LINE__);
			n_result = tas2562_regmap_write(p_tas2562,
				TAS2562_BOOKCTL_PAGE, 0);
			dev_err(p_tas2562->dev, "%s,  L=%d\n",
						__func__, __LINE__);
			if (n_result < 0) {
				dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
					__func__, __LINE__, n_result);
				goto end;
			}
			p_tas2562->mn_l_current_page = 0;
			n_result = tas2562_regmap_write(p_tas2562,
				TAS2562_BOOKCTL_REG, book);
			if (n_result < 0) {
				dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
					__func__, __LINE__, n_result);
				goto end;
			}
			p_tas2562->mn_l_current_book = book;
		}

		if (p_tas2562->mn_l_current_page != page) {
			n_result = tas2562_regmap_write(p_tas2562,
				TAS2562_BOOKCTL_PAGE, page);
			if (n_result < 0) {
				dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
					__func__, __LINE__, n_result);
				goto end;
			}
			p_tas2562->mn_l_current_page = page;
		}
	}

	if ((chn&channel_right) && (p_tas2562->mn_channels == 2)) {
		p_tas2562->client->addr = p_tas2562->mn_r_addr;
		if (p_tas2562->mn_r_current_book != book) {
			dev_err(p_tas2562->dev, "%s,  L=%d\n",
						__func__, __LINE__);
			n_result = tas2562_regmap_write(p_tas2562,
				TAS2562_BOOKCTL_PAGE, 0);
			dev_err(p_tas2562->dev, "%s,  L=%d\n",
						__func__, __LINE__);
			if (n_result < 0) {
				dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
					__func__, __LINE__, n_result);
				goto end;
			}
			p_tas2562->mn_r_current_page = 0;
			n_result = tas2562_regmap_write(p_tas2562,
				TAS2562_BOOKCTL_REG, book);
			if (n_result < 0) {
				dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
					__func__, __LINE__, n_result);
				goto end;
			}
			p_tas2562->mn_r_current_book = book;
		}

		if (p_tas2562->mn_r_current_page != page) {
			n_result = tas2562_regmap_write(p_tas2562,
				TAS2562_BOOKCTL_PAGE, page);
			if (n_result < 0) {
				dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
					__func__, __LINE__, n_result);
				goto end;
			}
			p_tas2562->mn_r_current_page = page;
		}
	}

end:
	return n_result;
}

static int tas2562_dev_read(struct tas2562_priv *p_tas2562,
	enum channel chn,
	unsigned int reg, unsigned int *pValue)
{
	int n_result = 0;

	mutex_lock(&p_tas2562->dev_lock);

	n_result = tas2562_change_book_page(p_tas2562, chn,
		TAS2562_BOOK_ID(reg), TAS2562_PAGE_ID(reg));
	if (n_result < 0)
		goto end;

	if ((chn == channel_left) || (p_tas2562->mn_channels == 1))
		p_tas2562->client->addr = p_tas2562->mn_l_addr;
	else if (chn == channel_right)
		p_tas2562->client->addr = p_tas2562->mn_r_addr;
	else
		dev_err(p_tas2562->dev, "%s, wrong channel number\n", __func__);

	n_result = tas2562_regmap_read(p_tas2562,
		TAS2562_PAGE_REG(reg), pValue);
	if (n_result < 0)
		dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
			__func__, __LINE__, n_result);
	else
		dev_dbg(p_tas2562->dev,
			"%s: chn:%x:BOOK:PAGE:REG %u:%u:%u,0x%x\n", __func__,
			p_tas2562->client->addr, TAS2562_BOOK_ID(reg),
			TAS2562_PAGE_ID(reg),
			TAS2562_PAGE_REG(reg), *pValue);

end:
	mutex_unlock(&p_tas2562->dev_lock);
	return n_result;
}

static int tas2562_dev_write(struct tas2562_priv *p_tas2562, enum channel chn,
	unsigned int reg, unsigned int value)
{
	int n_result = 0;
	dev_dbg(p_tas2562->dev, "%s,  L=%d ch:%d \n",
							__func__, __LINE__,chn);

	mutex_lock(&p_tas2562->dev_lock);

	n_result = tas2562_change_book_page(p_tas2562, chn,
		TAS2562_BOOK_ID(reg), TAS2562_PAGE_ID(reg));
	if (n_result < 0)
		goto end;

	if ((chn&channel_left) || (p_tas2562->mn_channels == 1)) {
		p_tas2562->client->addr = p_tas2562->mn_l_addr;

		n_result = tas2562_regmap_write(p_tas2562,
			TAS2562_PAGE_REG(reg), value);
		if (n_result < 0)
			dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
				__func__, __LINE__, n_result);
		else
			dev_dbg(p_tas2562->dev,
			"%s: chn%x:BOOK:PAGE:REG %u:%u:%u, VAL: 0x%02x\n",
				__func__, p_tas2562->client->addr,
				TAS2562_BOOK_ID(reg), TAS2562_PAGE_ID(reg),
				TAS2562_PAGE_REG(reg), value);
	}

	if ((chn&channel_right) && (p_tas2562->mn_channels == 2)) {
		p_tas2562->client->addr = p_tas2562->mn_r_addr;

		n_result = tas2562_regmap_write(p_tas2562,
		TAS2562_PAGE_REG(reg),value);
		if (n_result < 0)
			dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
				__func__, __LINE__, n_result);
		else
			dev_dbg(p_tas2562->dev, "%s: chn%x:BOOK:PAGE:REG %u:%u:%u, VAL: 0x%02x\n",
				__func__, p_tas2562->client->addr,
				TAS2562_BOOK_ID(reg),
				TAS2562_PAGE_ID(reg),
				TAS2562_PAGE_REG(reg), value);
	}

end:
	mutex_unlock(&p_tas2562->dev_lock);
	return n_result;
}

static int tas2562_dev_bulk_write(struct tas2562_priv *p_tas2562,
	enum channel chn,
	unsigned int reg, unsigned char *p_data, unsigned int n_length)
{
	int n_result = 0;

	mutex_lock(&p_tas2562->dev_lock);

	n_result = tas2562_change_book_page(p_tas2562, chn,
		TAS2562_BOOK_ID(reg), TAS2562_PAGE_ID(reg));
	if (n_result < 0)
		goto end;

	if ((chn&channel_left) || (p_tas2562->mn_channels == 1)) {
		p_tas2562->client->addr = p_tas2562->mn_l_addr;
		n_result = tas2562_regmap_bulk_write(p_tas2562,
			TAS2562_PAGE_REG(reg), p_data, n_length);
		if (n_result < 0)
			dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
				__func__, __LINE__, n_result);
		else
			dev_dbg(p_tas2562->dev, "%s: chn%x:BOOK:PAGE:REG %u:%u:%u, len: 0x%02x\n",
				__func__, p_tas2562->client->addr,
				TAS2562_BOOK_ID(reg), TAS2562_PAGE_ID(reg),
				TAS2562_PAGE_REG(reg), n_length);
	}

	if ((chn&channel_right) && (p_tas2562->mn_channels == 2)) {
		p_tas2562->client->addr = p_tas2562->mn_r_addr;
				n_result = tas2562_regmap_bulk_write(p_tas2562,
			TAS2562_PAGE_REG(reg), p_data, n_length);
		if (n_result < 0)
			dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
				__func__, __LINE__, n_result);
		else
			dev_dbg(p_tas2562->dev, "%s: %x:BOOK:PAGE:REG %u:%u:%u, len: 0x%02x\n",
				__func__, p_tas2562->client->addr,
				TAS2562_BOOK_ID(reg), TAS2562_PAGE_ID(reg),
				TAS2562_PAGE_REG(reg), n_length);
	}

end:
	mutex_unlock(&p_tas2562->dev_lock);
	return n_result;
}

static int tas2562_dev_bulk_read(struct tas2562_priv *p_tas2562,
	enum channel chn,
	unsigned int reg, unsigned char *p_data, unsigned int n_length)
{
	int n_result = 0;

	mutex_lock(&p_tas2562->dev_lock);

	if ((chn == channel_left) || (p_tas2562->mn_channels == 1))
		p_tas2562->client->addr = p_tas2562->mn_l_addr;
	else if (chn == channel_right)
		p_tas2562->client->addr = p_tas2562->mn_r_addr;
	else
		dev_err(p_tas2562->dev, "%s, wrong channel number\n", __func__);

	n_result = tas2562_change_book_page(p_tas2562, chn,
		TAS2562_BOOK_ID(reg), TAS2562_PAGE_ID(reg));
	if (n_result < 0)
		goto end;

	n_result = tas2562_regmap_bulk_read(p_tas2562,
	TAS2562_PAGE_REG(reg), p_data, n_length);
	if (n_result < 0)
		dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
			__func__, __LINE__, n_result);
	else
		dev_dbg(p_tas2562->dev, "%s: chn%x:BOOK:PAGE:REG %u:%u:%u, len: 0x%02x\n",
			__func__, p_tas2562->client->addr,
			TAS2562_BOOK_ID(reg), TAS2562_PAGE_ID(reg),
			TAS2562_PAGE_REG(reg), n_length);
end:
	mutex_unlock(&p_tas2562->dev_lock);
	return n_result;
}

static int tas2562_dev_update_bits(struct tas2562_priv *p_tas2562,
	enum channel chn,
	unsigned int reg, unsigned int mask, unsigned int value)
{
	int n_result = 0;

	mutex_lock(&p_tas2562->dev_lock);
	n_result = tas2562_change_book_page(p_tas2562, chn,
		TAS2562_BOOK_ID(reg), TAS2562_PAGE_ID(reg));
	if (n_result < 0)
		goto end;

	if ((chn&channel_left) || (p_tas2562->mn_channels == 1)) {
		p_tas2562->client->addr = p_tas2562->mn_l_addr;
		n_result = tas2562_regmap_update_bits(p_tas2562,
			TAS2562_PAGE_REG(reg), mask, value);
		if (n_result < 0)
			dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
				__func__, __LINE__, n_result);
		else
			dev_dbg(p_tas2562->dev,
			"%s: chn%x:BOOK:PAGE:REG %u:%u:%u, mask: 0x%x, val: 0x%x\n",
				__func__, p_tas2562->client->addr,
				TAS2562_BOOK_ID(reg), TAS2562_PAGE_ID(reg),
				TAS2562_PAGE_REG(reg), mask, value);
	}

	if ((chn&channel_right) && (p_tas2562->mn_channels == 2)) {
		p_tas2562->client->addr = p_tas2562->mn_r_addr;
		n_result = tas2562_regmap_update_bits(p_tas2562,
			TAS2562_PAGE_REG(reg), mask, value);
		if (n_result < 0)
			dev_err(p_tas2562->dev, "%s, ERROR, L=%d, E=%d\n",
				__func__, __LINE__, n_result);
		else
			dev_dbg(p_tas2562->dev,
				"%s:chn%x:BOOK:PAGE:REG %u:%u:%u,mask: 0x%x, val: 0x%x\n",
				__func__, p_tas2562->client->addr,
				TAS2562_BOOK_ID(reg), TAS2562_PAGE_ID(reg),
				TAS2562_PAGE_REG(reg), mask, value);
	}

end:
	mutex_unlock(&p_tas2562->dev_lock);
	return n_result;
}

static bool tas2562_volatile(struct device *dev, unsigned int reg)
{
	return true;
}

static bool tas2562_writeable(struct device *dev, unsigned int reg)
{
	return true;
}
static const struct regmap_config tas2562_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = tas2562_writeable,
	.volatile_reg = tas2562_volatile,
	.cache_type = REGCACHE_NONE,
	.max_register = 1 * 128,
};


static void tas2562_hw_reset(struct tas2562_priv *p_tas2562)
{
	if (gpio_is_valid(p_tas2562->mn_reset_gpio)) {
			gpio_direction_output(p_tas2562->mn_reset_gpio, 0);
		if(p_tas2562->mn_channels != 1) {
			dev_dbg(p_tas2562->dev, "Reset gpio: not mono case, resetting second gpio");
			if(gpio_is_valid(p_tas2562->mn_reset_gpio2))
				gpio_direction_output(p_tas2562->mn_reset_gpio2, 0);
		} else {
			dev_dbg(p_tas2562->dev, "Reset gpio: mono case, not resetting second gpio");
		}
		msleep(20);
		gpio_direction_output(p_tas2562->mn_reset_gpio, 1);
		if(p_tas2562->mn_channels != 1) {
			dev_dbg(p_tas2562->dev, "Reset gpio: not mono case, resetting second gpio");
			if(gpio_is_valid(p_tas2562->mn_reset_gpio2))
				gpio_direction_output(p_tas2562->mn_reset_gpio2, 1);
		} else {
			dev_dbg(p_tas2562->dev, "Reset gpio: mono case, not resetting second gpio");
		}
		msleep(20);
	}
	dev_info(p_tas2562->dev, "reset gpio up !!\n");

	p_tas2562->mn_l_current_book = -1;
	p_tas2562->mn_l_current_page = -1;
	p_tas2562->mn_r_current_book = -1;
	p_tas2562->mn_r_current_page = -1;
}

void tas2562_enable_irq(struct tas2562_priv *p_tas2562, bool enable)
{
	static int irq1_enabled = 0;
	static int irq2_enabled = 0;
	struct irq_desc *desc = NULL;
	if (enable) {
		if (p_tas2562->mb_irq_eable)
			return;

		if (gpio_is_valid(p_tas2562->mn_irq_gpio) && irq1_enabled == 0) {
			desc = irq_to_desc(p_tas2562->mn_irq);
			if (desc && desc->depth > 0) {
			enable_irq(p_tas2562->mn_irq);
			} else {
				dev_info (p_tas2562->dev, "### irq already enabled");
			}
			irq1_enabled = 1;
		}
		if (gpio_is_valid(p_tas2562->mn_irq_gpio2) && irq2_enabled == 0) {
			enable_irq(p_tas2562->mn_irq2);
			irq2_enabled = 1;
		}

		p_tas2562->mb_irq_eable = true;
	} else {
		if (gpio_is_valid(p_tas2562->mn_irq_gpio) && irq1_enabled == 1) {
			disable_irq_nosync(p_tas2562->mn_irq);
			irq1_enabled = 0;
		}
		if (gpio_is_valid(p_tas2562->mn_irq_gpio2) && irq2_enabled == 1) {
			disable_irq_nosync(p_tas2562->mn_irq2);
			irq2_enabled = 0;
		}
		p_tas2562->mb_irq_eable = false;
	}
}

static void irq_work_routine(struct work_struct *work)
{
	struct tas2562_priv *p_tas2562 =
		container_of(work, struct tas2562_priv, irq_work.work);
	unsigned int nDevInt1Status = 0, nDevInt2Status = 0,
		nDevInt3Status = 0, nDevInt4Status = 0;
	int n_counter = 2;
	int n_result = 0;
	enum channel chn;

	dev_info(p_tas2562->dev, "%s\n", __func__);
#ifdef CONFIG_TAS2562_CODEC
	mutex_lock(&p_tas2562->codec_lock);
#endif
	tas2562_enable_irq(p_tas2562, false);

	if (p_tas2562->mb_runtime_suspend) {
		dev_info(p_tas2562->dev, "%s, Runtime Suspended\n", __func__);
		goto end;
	}

	if (p_tas2562->mn_power_state == TAS2562_POWER_SHUTDOWN) {
		dev_info(p_tas2562->dev, "%s, device not powered\n", __func__);
		goto end;
	}

	n_result = p_tas2562->write(p_tas2562, channel_both,
				TAS2562_INTERRUPTMASKREG0,
				TAS2562_INTERRUPTMASKREG0_DISABLE);
	n_result = p_tas2562->write(p_tas2562, channel_both,
				TAS2562_INTERRUPTMASKREG1,
				TAS2562_INTERRUPTMASKREG1_DISABLE);

	if (n_result < 0)
		goto reload;

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

	if (chn & channel_left)
		n_result = p_tas2562->read(p_tas2562, channel_left,
		TAS2562_LATCHEDINTERRUPTREG0, &nDevInt1Status);
	if (n_result >= 0)
		n_result = p_tas2562->read(p_tas2562, channel_left,
		TAS2562_LATCHEDINTERRUPTREG1, &nDevInt2Status);
	else
		goto reload;

	if (chn & channel_right)
		n_result = p_tas2562->read(p_tas2562, channel_right,
		TAS2562_LATCHEDINTERRUPTREG0, &nDevInt3Status);
	if (n_result >= 0)
		n_result = p_tas2562->read(p_tas2562, channel_right,
		TAS2562_LATCHEDINTERRUPTREG1, &nDevInt4Status);
	else
		goto reload;

	dev_info(p_tas2562->dev, "IRQ status : 0x%x, 0x%x, 0x%x, 0x%x\n",
			nDevInt1Status, nDevInt2Status,
			nDevInt3Status, nDevInt4Status);

	if (((nDevInt1Status & 0x7) != 0)
		|| ((nDevInt2Status & 0x0f) != 0) ||
		((nDevInt3Status & 0x7) != 0)
		|| ((nDevInt4Status & 0x0f) != 0)) {
/*		in case of INT_CLK, INT_OC, INT_OT,
 *		INT_OVLT, INT_UVLT, INT_BO
 */

		if ((nDevInt1Status &
	    TAS2562_LATCHEDINTERRUPTREG0_TDMCLOCKERRORSTICKY_INTERRUPT) ||
		(nDevInt3Status &
	    TAS2562_LATCHEDINTERRUPTREG0_TDMCLOCKERRORSTICKY_INTERRUPT)) {
			p_tas2562->mn_err_code |= ERROR_CLOCK;
			dev_err(p_tas2562->dev, "TDM clock error!\n");
	} else
		p_tas2562->mn_err_code &= ~ERROR_OVER_CURRENT;

	if ((nDevInt1Status &
		TAS2562_LATCHEDINTERRUPTREG0_OCEFLAGSTICKY_INTERRUPT) ||
		(nDevInt3Status &
		TAS2562_LATCHEDINTERRUPTREG0_OCEFLAGSTICKY_INTERRUPT)) {
		p_tas2562->mn_err_code |= ERROR_OVER_CURRENT;
		dev_err(p_tas2562->dev, "SPK over current!\n");
	} else
		p_tas2562->mn_err_code &= ~ERROR_OVER_CURRENT;

	if ((nDevInt1Status &
		TAS2562_LATCHEDINTERRUPTREG0_OTEFLAGSTICKY_INTERRUPT) ||
		(nDevInt3Status &
		TAS2562_LATCHEDINTERRUPTREG0_OTEFLAGSTICKY_INTERRUPT)) {
		p_tas2562->mn_err_code |= ERROR_DIE_OVERTEMP;
		dev_err(p_tas2562->dev, "die over temperature!\n");
	} else
		p_tas2562->mn_err_code &= ~ERROR_DIE_OVERTEMP;

	if ((nDevInt2Status &
	TAS2562_LATCHEDINTERRUPTREG1_VBATOVLOSTICKY_INTERRUPT) ||
		(nDevInt4Status &
	TAS2562_LATCHEDINTERRUPTREG1_VBATOVLOSTICKY_INTERRUPT)) {
		p_tas2562->mn_err_code |= ERROR_OVER_VOLTAGE;
		dev_err(p_tas2562->dev, "SPK over voltage!\n");
	} else
		p_tas2562->mn_err_code &= ~ERROR_UNDER_VOLTAGE;

	if ((nDevInt2Status &
	TAS2562_LATCHEDINTERRUPTREG1_VBATUVLOSTICKY_INTERRUPT) ||
		(nDevInt4Status &
	TAS2562_LATCHEDINTERRUPTREG1_VBATUVLOSTICKY_INTERRUPT)) {
		p_tas2562->mn_err_code |= ERROR_UNDER_VOLTAGE;
		dev_err(p_tas2562->dev, "SPK under voltage!\n");
	} else
		p_tas2562->mn_err_code &= ~ERROR_UNDER_VOLTAGE;

	if ((nDevInt2Status &
	TAS2562_LATCHEDINTERRUPTREG1_BROWNOUTFLAGSTICKY_INTERRUPT) ||
		(nDevInt4Status &
	TAS2562_LATCHEDINTERRUPTREG1_BROWNOUTFLAGSTICKY_INTERRUPT)) {
		p_tas2562->mn_err_code |= ERROR_BROWNOUT;
		dev_err(p_tas2562->dev, "brownout!\n");
	} else
		p_tas2562->mn_err_code &= ~ERROR_BROWNOUT;

		goto reload;
	} else {
		n_counter = 2;

	while (n_counter > 0) {
		if (chn & channel_left)
			n_result = p_tas2562->read(p_tas2562, channel_left,
				TAS2562_POWERCONTROL, &nDevInt1Status);
		if (n_result < 0)
			goto reload;
		if (chn & channel_right)
			n_result = p_tas2562->read(p_tas2562, channel_right,
			TAS2562_POWERCONTROL, &nDevInt3Status);
		if (n_result < 0)
			goto reload;

		if ((nDevInt1Status
			& TAS2562_POWERCONTROL_OPERATIONALMODE10_MASK)
			!= TAS2562_POWERCONTROL_OPERATIONALMODE10_SHUTDOWN) {
			/* If only left should be power on */
			if (chn == channel_left)
				break;
			/* If both should be power on */
			if ((nDevInt3Status
				& TAS2562_POWERCONTROL_OPERATIONALMODE10_MASK)
				!=
				TAS2562_POWERCONTROL_OPERATIONALMODE10_SHUTDOWN)
				break;
		} else if (chn == channel_right) {
			if ((nDevInt3Status
				& TAS2562_POWERCONTROL_OPERATIONALMODE10_MASK)
				!=
				TAS2562_POWERCONTROL_OPERATIONALMODE10_SHUTDOWN)
				break;
		}

			n_counter--;
			if (n_counter > 0) {
			/* in case check pow status just after power on TAS2562 */
				dev_dbg(p_tas2562->dev, "PowSts B: 0x%x, check again after 10ms\n",
					nDevInt1Status);
				msleep(20);
			}
		}

		if ((((nDevInt1Status
			& TAS2562_POWERCONTROL_OPERATIONALMODE10_MASK)
			== TAS2562_POWERCONTROL_OPERATIONALMODE10_SHUTDOWN)
			&& (chn & channel_left))
			|| (((nDevInt3Status
			& TAS2562_POWERCONTROL_OPERATIONALMODE10_MASK)
			== TAS2562_POWERCONTROL_OPERATIONALMODE10_SHUTDOWN)
			&& (chn & channel_right))) {
			dev_err(p_tas2562->dev, "%s, Critical ERROR REG[0x%x] = 0x%x\n",
				__func__,
				TAS2562_POWERCONTROL,
				nDevInt1Status);
			p_tas2562->mn_err_code |= ERROR_CLASSD_PWR;
			goto reload;
		}
		p_tas2562->mn_err_code &= ~ERROR_CLASSD_PWR;
	}

	n_result = p_tas2562->write(p_tas2562, channel_left,
		TAS2562_INTERRUPTMASKREG0, 0xf8);
	if (n_result < 0)
		goto reload;

	n_result = p_tas2562->write(p_tas2562, channel_left,
		TAS2562_INTERRUPTMASKREG1, 0xb1);
	if (n_result < 0)
		goto reload;

	goto end;

reload:
	/* hardware reset and reload */
	tas2562_load_config(p_tas2562);

end:
	tas2562_enable_irq(p_tas2562, true);
#ifdef CONFIG_TAS2562_CODEC
	mutex_unlock(&p_tas2562->codec_lock);
#endif
}

static void init_work_routine(struct work_struct *work)
{
	struct tas2562_priv *p_tas2562 =
		container_of(work, struct tas2562_priv, init_work.work);
	int nResult = 0;
	//int irqreg;
	//dev_info(p_tas2562->dev, "%s\n", __func__);
#ifdef CONFIG_TAS2562_CODEC
	mutex_lock(&p_tas2562->codec_lock);
#endif

	p_tas2562->update_bits(p_tas2562, channel_both, TAS2562_POWERCONTROL,
		TAS2562_POWERCONTROL_OPERATIONALMODE10_MASK,
		TAS2562_POWERCONTROL_OPERATIONALMODE10_ACTIVE);

	//dev_info(p_tas2562->dev, "set ICN to -80dB\n");
	nResult = p_tas2562->bulk_write(p_tas2562, channel_both, TAS2562_ICN_REG, p_icn, 4);

	nResult = gpio_get_value(p_tas2562->mn_irq_gpio);
	//dev_info(p_tas2562->dev, "%s, irq GPIO state: %d\n", __func__, nResult);

#ifdef CONFIG_TAS2562_CODEC
	mutex_unlock(&p_tas2562->codec_lock);
#endif
}

static irqreturn_t tas2562_irq_handler(int irq, void *dev_id)
{
	struct tas2562_priv *p_tas2562 = (struct tas2562_priv *)dev_id;

	/* get IRQ status after 100 ms */
	schedule_delayed_work(&p_tas2562->irq_work, msecs_to_jiffies(100));
	return IRQ_HANDLED;
}

static int tas2562_runtime_suspend(struct tas2562_priv *p_tas2562)
{
	dev_dbg(p_tas2562->dev, "%s\n", __func__);

	p_tas2562->mb_runtime_suspend = true;

	if (delayed_work_pending(&p_tas2562->irq_work)) {
		dev_dbg(p_tas2562->dev, "cancel IRQ work\n");
		cancel_delayed_work_sync(&p_tas2562->irq_work);
	}

	return 0;
}

static int tas2562_runtime_resume(struct tas2562_priv *p_tas2562)
{
	dev_dbg(p_tas2562->dev, "%s\n", __func__);

	p_tas2562->mb_runtime_suspend = false;

	return 0;
}

static int tas2562_pm_suspend(struct device *dev)
{
	struct tas2562_priv *p_tas2562 = dev_get_drvdata(dev);

	if(!p_tas2562){
		dev_err(p_tas2562->dev, "drvdata is NULL\n");
		return -EINVAL;
	}

	mutex_lock(&p_tas2562->codec_lock);
	tas2562_runtime_suspend(p_tas2562);
	mutex_unlock(&p_tas2562->codec_lock);
	return 0;
}

static int tas2562_pm_resume(struct device *dev)
{
	struct tas2562_priv *p_tas2562 = dev_get_drvdata(dev);

	if(!p_tas2562){
		dev_err(p_tas2562->dev, "drvdata is NULL\n");
		return -EINVAL;
	}
	mutex_lock(&p_tas2562->codec_lock);
	tas2562_runtime_resume(p_tas2562);
	mutex_unlock(&p_tas2562->codec_lock);
	return 0;
}

#ifdef CONFIG_TAS25XX_ALGO
/*TODO: FIX Use of private data causes crash*/
struct tas2562_priv *g_p_tas2562;
void tas2562_software_reset(void *prv_data)
{
	(void)prv_data;
	pr_err("[TI-SmartPA:%s]\n", __func__);
	schedule_delayed_work(&g_p_tas2562->dc_work, msecs_to_jiffies(10));
}

static void dc_work_routine(struct work_struct *work)
{
	(void)work;
	pr_err("[TI-SmartPA:%s]\n", __func__);
	tas2562_enable_irq(g_p_tas2562, false);
	g_p_tas2562->write(g_p_tas2562, channel_both, TAS2562_SOFTWARERESET,
		TAS2562_SOFTWARERESET_SOFTWARERESET_RESET);
	msleep(20);
}
#endif /*CONFIG_TAS25XX_ALGO*/

static int tas2562_parse_dt(struct device *dev, struct tas2562_priv *p_tas2562)
{
	struct device_node *np = dev->of_node;
	int rc = 0, ret = 0;

	rc = of_property_read_u32(np, "ti,channels", &p_tas2562->mn_channels);
	if (rc) {
		dev_err(p_tas2562->dev, "Looking up %s property in node %s failed %d\n",
			"ti,channels", np->full_name, rc);
	} else {
		dev_err(p_tas2562->dev, "ti,channels=%d",
			p_tas2562->mn_channels);
	}

	rc = of_property_read_u32(np, "ti,left-channel", &p_tas2562->mn_l_addr);
	if (rc) {
		dev_err(p_tas2562->dev, "Looking up %s property in node %s failed %d\n",
			"ti,left-channel", np->full_name, rc);
	} else {
		dev_err(p_tas2562->dev, "ti,left-channel=0x%x",
			p_tas2562->mn_l_addr);
	}
	if(p_tas2562->mn_channels != 1) {
		rc = of_property_read_u32(np, "ti,right-channel",
			&p_tas2562->mn_r_addr);
		if (rc) {
			dev_err(p_tas2562->dev, "Looking up %s property in node %s failed %d\n",
				"ti,right-channel", np->full_name, rc);
		} else {
			dev_err(p_tas2562->dev, "ti,right-channel=0x%x",
				p_tas2562->mn_r_addr);
		}
	}
	p_tas2562->mn_reset_gpio = of_get_named_gpio(np, "ti,reset-gpio", 0);
	if (!gpio_is_valid(p_tas2562->mn_reset_gpio)) {
		dev_err(p_tas2562->dev, "Looking up %s property in node %s failed %d\n",
			"ti,reset-gpio", np->full_name,
				p_tas2562->mn_reset_gpio);
	} else {
		dev_info(p_tas2562->dev, "ti,reset-gpio=%d",
			p_tas2562->mn_reset_gpio);
	}
	if(p_tas2562->mn_channels != 1) {
		p_tas2562->mn_reset_gpio2 = of_get_named_gpio(np, "ti,reset-gpio2", 0);
		if (!gpio_is_valid(p_tas2562->mn_reset_gpio2)) {
			dev_err(p_tas2562->dev, "Looking up %s property in node %s failed %d\n",
				"ti,reset-gpio2", np->full_name,
				p_tas2562->mn_reset_gpio2);
		} else {
			dev_err(p_tas2562->dev, "ti,reset-gpio2=%d",
				p_tas2562->mn_reset_gpio2);
		}
	}
	p_tas2562->mn_irq_gpio = of_get_named_gpio(np, "ti,irq-gpio", 0);
	if (!gpio_is_valid(p_tas2562->mn_irq_gpio)) {
		dev_err(p_tas2562->dev, "Looking up %s property in node %s failed %d\n",
			"ti,irq-gpio", np->full_name, p_tas2562->mn_irq_gpio);
	} else {
		dev_info(p_tas2562->dev, "ti,irq-gpio=%d",
			p_tas2562->mn_irq_gpio);
	}
	if(p_tas2562->mn_channels != 1) {
		p_tas2562->mn_irq_gpio2 = of_get_named_gpio(np, "ti,irq-gpio2", 0);
		if (!gpio_is_valid(p_tas2562->mn_irq_gpio2)) {
			dev_err(p_tas2562->dev, "Looking up %s property in node %s failed %d\n",
			"ti,irq-gpio2", np->full_name, p_tas2562->mn_irq_gpio2);
		} else {
			dev_err(p_tas2562->dev, "ti,irq-gpio2=%d",
				p_tas2562->mn_irq_gpio2);
		}
	}
	return ret;
}

static int tas2562_i2c_probe(struct i2c_client *p_client,
			const struct i2c_device_id *id)
{
	struct tas2562_priv *p_tas2562 = NULL;
	int n_result;

	dev_err(&p_client->dev, "Driver ID: %s\n", TAS2562_DRIVER_ID);
	dev_info(&p_client->dev, "%s enter\n", __func__);

	p_tas2562 = devm_kzalloc(&p_client->dev,
		sizeof(struct tas2562_priv), GFP_KERNEL);
	if (p_tas2562 == NULL) {
		/* dev_err(&p_client->dev, "failed to get i2c device\n"); */
		n_result = -ENOMEM;
		goto err;
	}

	p_tas2562->client = p_client;
	p_tas2562->dev = &p_client->dev;
	i2c_set_clientdata(p_client, p_tas2562);
	dev_set_drvdata(&p_client->dev, p_tas2562);

	p_tas2562->regmap = devm_regmap_init_i2c(p_client, &tas2562_i2c_regmap);
	if (IS_ERR(p_tas2562->regmap)) {
		n_result = PTR_ERR(p_tas2562->regmap);
		dev_err(&p_client->dev, "Failed to allocate register map: %d\n",
					n_result);
		goto err;
	} else {
		dev_err(&p_client->dev, "Failed to allocate register p_tas2562->regmap: %x\n",p_tas2562->regmap);
	}

	if (p_client->dev.of_node) {
    dev_info(&p_client->dev, "Before tas2562_parse_dt\n");
		tas2562_parse_dt(&p_client->dev, p_tas2562);
		
        }
	if (gpio_is_valid(p_tas2562->mn_reset_gpio)) {
		n_result = gpio_request(p_tas2562->mn_reset_gpio,
			"TAS2562_RESET");
		if (n_result) {
			dev_err(p_tas2562->dev, "%s: Failed to request gpio %d\n",
				__func__, p_tas2562->mn_reset_gpio);
			n_result = -EINVAL;
			goto err_gpio;
		}
		//gpio_export(p_tas2562->mn_reset_gpio,1);
		tas2562_hw_reset(p_tas2562);
	}

	if (gpio_is_valid(p_tas2562->mn_reset_gpio2) &&
			(p_tas2562->mn_channels == 2)) {
		n_result = gpio_request(p_tas2562->mn_reset_gpio2,
			"TAS2562_RESET2");
		if (n_result) {
			dev_err(p_tas2562->dev, "%s: Failed to request gpio %d\n",
				__func__, p_tas2562->mn_reset_gpio2);
			n_result = -EINVAL;
			goto err;
		}
		tas2562_hw_reset(p_tas2562);
	}

	p_tas2562->read = tas2562_dev_read;
	p_tas2562->write = tas2562_dev_write;
	p_tas2562->bulk_read = tas2562_dev_bulk_read;
	p_tas2562->bulk_write = tas2562_dev_bulk_write;
	p_tas2562->update_bits = tas2562_dev_update_bits;
	p_tas2562->hw_reset = tas2562_hw_reset;
	p_tas2562->enable_irq = tas2562_enable_irq;
#ifdef CODEC_PM
	p_tas2562->runtime_suspend = tas2562_runtime_suspend;
	p_tas2562->runtime_resume = tas2562_runtime_resume;
	p_tas2562->mn_power_state = TAS2562_POWER_SHUTDOWN;
#endif
	p_tas2562->mn_power_state = TAS2562_POWER_SHUTDOWN;
	p_tas2562->spk_l_control = 1;

	mutex_init(&p_tas2562->dev_lock);

	dev_info(&p_client->dev, "Before SW reset\n");
	/* Reset the chip */
	n_result = tas2562_dev_write(p_tas2562, channel_both,
			TAS2562_SOFTWARERESET, 0x01);
	if (n_result < 0) {
		dev_err(&p_client->dev, "I2c fail, %d\n", n_result);
		goto err_i2c;
	}
	dev_info(&p_client->dev, "After SW reset\n");

	if (gpio_is_valid(p_tas2562->mn_irq_gpio)) {
		n_result = gpio_request(p_tas2562->mn_irq_gpio, "TAS2562-IRQ");
		if (n_result < 0) {
			dev_err(p_tas2562->dev, "%s: GPIO %d request error\n",
				__func__, p_tas2562->mn_irq_gpio);
			goto err;
		}
		gpio_direction_input(p_tas2562->mn_irq_gpio);
		tas2562_dev_write(p_tas2562, channel_both,
			TAS2562_MISCCONFIGURATIONREG0, 0xce);

		p_tas2562->mn_irq = gpio_to_irq(p_tas2562->mn_irq_gpio);
		dev_info(p_tas2562->dev, "irq = %d\n", p_tas2562->mn_irq);
		INIT_DELAYED_WORK(&p_tas2562->irq_work, irq_work_routine);
		n_result = request_threaded_irq(p_tas2562->mn_irq,
				tas2562_irq_handler,
				NULL, IRQF_TRIGGER_FALLING|IRQF_ONESHOT,
				p_client->name, p_tas2562);
		if (n_result < 0) {
			dev_err(p_tas2562->dev,
				"request_irq failed, %d\n", n_result);
			goto err;
		}
		disable_irq_nosync(p_tas2562->mn_irq);
	}
	if (gpio_is_valid(p_tas2562->mn_irq_gpio2) &&
			(p_tas2562->mn_channels == 2)) {
		n_result = gpio_request(p_tas2562->mn_irq_gpio2,
				"TAS2562-IRQ2");
		if (n_result < 0) {
			dev_err(p_tas2562->dev, "%s: GPIO %d request error\n",
				__func__, p_tas2562->mn_irq_gpio2);
			goto err;
		}
		gpio_direction_input(p_tas2562->mn_irq_gpio2);
		tas2562_dev_write(p_tas2562, channel_both,
				TAS2562_MISCCONFIGURATIONREG0, 0xce);

		p_tas2562->mn_irq2 = gpio_to_irq(p_tas2562->mn_irq_gpio2);
		dev_info(p_tas2562->dev, "irq = %d\n", p_tas2562->mn_irq2);
		INIT_DELAYED_WORK(&p_tas2562->irq_work, irq_work_routine);
		n_result = request_threaded_irq(p_tas2562->mn_irq2,
				tas2562_irq_handler,
				NULL, IRQF_TRIGGER_FALLING|IRQF_ONESHOT,
				p_client->name, p_tas2562);
		if (n_result < 0) {
			dev_err(p_tas2562->dev,
				"request_irq failed, %d\n", n_result);
			goto err;
		}
		disable_irq_nosync(p_tas2562->mn_irq2);
	}
	tas2562_enable_irq(p_tas2562, true);
	INIT_DELAYED_WORK(&p_tas2562->init_work, init_work_routine);

#ifdef CONFIG_TAS2562_CODEC
	mutex_init(&p_tas2562->codec_lock);
	n_result = tas2562_register_codec(p_tas2562);
	if (n_result < 0) {
		dev_err(p_tas2562->dev,
			"register codec failed, %d\n", n_result);
		goto err;
	}
#endif

#ifdef CONFIG_TAS2562_MISC
	mutex_init(&p_tas2562->file_lock);
	n_result = tas2562_register_misc(p_tas2562);
	if (n_result < 0) {
		dev_err(p_tas2562->dev,
			"register codec failed, %d\n", n_result);
		goto err;
	}
#endif

#ifdef CONFIG_TAS25XX_ALGO
	INIT_DELAYED_WORK(&p_tas2562->dc_work, dc_work_routine);
	g_p_tas2562 = p_tas2562;
	register_tas2562_reset_func(tas2562_software_reset, p_tas2562);
#endif /*CONFIG_TAS25XX_ALGO*/

err:
	return n_result;

err_i2c:
	gpio_free(p_tas2562->mn_reset_gpio);
	devm_kfree(&p_client->dev, p_tas2562);
	p_tas2562 = NULL;
	return n_result;
    
err_gpio:
	devm_kfree(&p_client->dev, p_tas2562);
	p_tas2562 = NULL;
	return n_result;
}

static int tas2562_i2c_remove(struct i2c_client *p_client)
{
	struct tas2562_priv *p_tas2562 = i2c_get_clientdata(p_client);

	dev_info(p_tas2562->dev, "%s\n", __func__);

#ifdef CONFIG_TAS2562_CODEC
	tas2562_deregister_codec(p_tas2562);
	mutex_destroy(&p_tas2562->codec_lock);
#endif

#ifdef CONFIG_TAS2562_MISC
	tas2562_deregister_misc(p_tas2562);
	mutex_destroy(&p_tas2562->file_lock);
#endif

	if (gpio_is_valid(p_tas2562->mn_reset_gpio))
		gpio_free(p_tas2562->mn_reset_gpio);
	if (gpio_is_valid(p_tas2562->mn_irq_gpio))
		gpio_free(p_tas2562->mn_irq_gpio);
	if (gpio_is_valid(p_tas2562->mn_reset_gpio2))
		gpio_free(p_tas2562->mn_reset_gpio2);
	if (gpio_is_valid(p_tas2562->mn_irq_gpio2))
		gpio_free(p_tas2562->mn_irq_gpio2);

	return 0;
}


static const struct i2c_device_id tas2562_i2c_id[] = {
	{ "tas2562", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas2562_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id tas2562_of_match[] = {
	{ .compatible = "ti,tas2562" },
	{},
};
MODULE_DEVICE_TABLE(of, tas2562_of_match);
#endif

static const struct dev_pm_ops tas2562_pm_ops = {
	.suspend = tas2562_pm_suspend,
	.resume = tas2562_pm_resume
};

static struct i2c_driver tas2562_i2c_driver = {
	.driver = {
		.name   = "tas2562",
		.owner  = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(tas2562_of_match),
#endif
		.pm = &tas2562_pm_ops,
	},
	.probe      = tas2562_i2c_probe,
	.remove     = tas2562_i2c_remove,
	.id_table   = tas2562_i2c_id,
};

module_i2c_driver(tas2562_i2c_driver);
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TAS2562 I2C Smart Amplifier driver");
MODULE_LICENSE("GPL v2");
//MODULE_LICENSE("GPL");//MODULE_LICENSE("GPL v2");
#endif
