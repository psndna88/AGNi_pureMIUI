/*
 * Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2013, LGE Inc. All rights reserved
 * Copyright (c) 2014 savoca <adeddo27@gmail.com>
 * Copyright (c) 2014 Paul Reioux <reioux@gmail.com>
 * Copyright (c) 2016 Pal Zoltan Illes <palilles@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * KCAL SCREEN MODES v1.2 by psndna88@xda (12-January-2018)
 *
 * all modes use individual parameters independent from tunables values
 *
 * echo "x" > /sys/devices/platform/kcal_ctrl.0/kcal_mode
 *
 * 0: User Mode (use the values set for the individual kcal tunables)
 * 1: Standard Mode
 * 2: Night Mode (uses backlight dimmer algorithm)
 * 3: Warm Mode
 * 4: Vivid Mode
 * 5: Reading Mode
 * 6: vivid-2 mode (reduce contrast from Vivid mode)
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/module.h>

#if defined(CONFIG_MMI_PANEL_NOTIFICATIONS) && defined(CONFIG_FB)
#include <mach/mmi_panel_notifier.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#elif defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif

#include "mdss_mdp.h"
#include "mdss_dsi.h"

#define DEF_PCC 0x100
#define DEF_PA 0xff
#define PCC_ADJ 0x80

struct kcal_lut_data {
#if defined(CONFIG_MMI_PANEL_NOTIFICATIONS) && defined(CONFIG_FB)
	struct mmi_notifier panel_nb;
#elif defined(CONFIG_FB)
	struct device dev;
	struct notifier_block panel_nb;
#endif
	bool queue_changes;
	int red;
	int green;
	int blue;
	int minimum;
	int enable;
	int invert;
	int sat;
	int hue;
	int val;
	int cont;
};

int kcal_custom_mode = 0;
int prev_kcal_r, prev_kcal_g, prev_kcal_b;
int prev_kcal_min, prev_kcal_sat, prev_kcal_val, prev_kcal_cont;
int user_kcal_r, user_kcal_g, user_kcal_b;
int user_kcal_min, user_kcal_sat, user_kcal_val, user_kcal_cont;
int mode_kcal_r, mode_kcal_g, mode_kcal_b;
int mode_kcal_min, mode_kcal_sat, mode_kcal_val, mode_kcal_cont;
bool prev_backlight_dimmer, mode_backlight_dimmer;
extern bool backlight_dimmer;

#ifdef CONFIG_KLAPSE
struct kcal_lut_data *lut_cpy;
#endif

struct mdss_mdp_ctl *fb0_ctl = 0;

static void kcal_mode_save_prev(struct device *dev) {

    struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

    prev_kcal_r = lut_data->red;
    prev_kcal_g = lut_data->green;
    prev_kcal_b = lut_data->blue;
    prev_kcal_min = lut_data->minimum;
    prev_kcal_sat = lut_data->sat;
    prev_kcal_val = lut_data->val;
    prev_kcal_cont = lut_data->cont;
    prev_backlight_dimmer = backlight_dimmer;

}

static void kcal_mode_save_mode(struct device *dev) {

    struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

    lut_data->red = mode_kcal_r;
    lut_data->green = mode_kcal_g;
    lut_data->blue = mode_kcal_b;
    lut_data->minimum = mode_kcal_min;
    lut_data->sat = mode_kcal_sat;
    lut_data->val = mode_kcal_val;
    lut_data->cont = mode_kcal_cont;
    backlight_dimmer = mode_backlight_dimmer;

}

static void kcal_apply_mode(struct device *dev) {

    kcal_mode_save_prev(dev);

	switch (kcal_custom_mode) {
	case 0:
    	/* USER MODE */
		mode_kcal_r = user_kcal_r;
        mode_kcal_g = user_kcal_g;
        mode_kcal_b = user_kcal_b;
        mode_kcal_min = user_kcal_min;
        mode_kcal_sat = user_kcal_sat;
        mode_kcal_val = user_kcal_val;
        mode_kcal_cont = user_kcal_cont;
        mode_backlight_dimmer = prev_backlight_dimmer;
		break;
	case 1:
        /* STANDARD MODE */
        mode_kcal_r = 256;
        mode_kcal_g = 256;
        mode_kcal_b = 256;
        mode_kcal_min = 35;
        mode_kcal_sat = 255;
        mode_kcal_val = 255;
        mode_kcal_cont = 255;
        mode_backlight_dimmer = prev_backlight_dimmer;
		break;
	case 2:
        /* NIGHT MODE */
        mode_kcal_r = 228;
        mode_kcal_g = 168;
        mode_kcal_b = 120;
        mode_kcal_min = 0;
        mode_kcal_sat = 265;
        mode_kcal_val = 255;
        mode_kcal_cont = 255;
        mode_backlight_dimmer = true;
		break;
	case 3:
        /* WARM MODE */
        mode_kcal_r = 256;
        mode_kcal_g = 240;
        mode_kcal_b = 208;
        mode_kcal_min = 35;
        mode_kcal_sat = 275;
        mode_kcal_val = 251;
        mode_kcal_cont = 258;
        mode_backlight_dimmer = prev_backlight_dimmer;
		break;
	case 4:
        /* VIVID MODE */
        mode_kcal_r = 256;
        mode_kcal_g = 256;
        mode_kcal_b = 256;
        mode_kcal_min = 35;
        mode_kcal_sat = 270;
        mode_kcal_val = 257;
        mode_kcal_cont = 265;
        mode_backlight_dimmer = prev_backlight_dimmer;
		break;
	case 5:
        /* READING MODE */
        mode_kcal_r = 256;
        mode_kcal_g = 256;
        mode_kcal_b = 180;
        mode_kcal_min = 35;
        mode_kcal_sat = 255;
        mode_kcal_val = 255;
        mode_kcal_cont = 255;
        mode_backlight_dimmer = prev_backlight_dimmer;
		break;
	case 6:
        /* VIVID 2 MODE */
        mode_kcal_r = 256;
        mode_kcal_g = 256;
        mode_kcal_b = 256;
        mode_kcal_min = 35;
        mode_kcal_sat = 265;
        mode_kcal_val = 257;
        mode_kcal_cont = 255;
        mode_backlight_dimmer = prev_backlight_dimmer;
 		break;
	default:
		break;
    }

    kcal_mode_save_mode(dev);

}

static int mdss_mdp_kcal_store_fb0_ctl(void)
{
	int i;
	struct mdss_mdp_ctl *ctl;
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();

	if (fb0_ctl) return 1;
	if (!mdata) {
		pr_err("%s mdata is NULL...",__func__);
		return 0;
	}

	for (i = 0; i < mdata->nctl; i++) {
		ctl = mdata->ctl_off + i;
		if (!ctl) {
			pr_err("%s ctl is NULL...\n",__func__);
			return 0;
		}
		if (!(ctl->mfd)) {
			pr_err("%s MFD is NULL...\n",__func__);
			return 0;
		}
		pr_err("%s panel name %s\n",__func__,ctl->mfd->panel_info->panel_name);
		if ( ctl->mfd->panel_info->fb_num  == 0 ) {
			pr_err("%s panel found...\n",__func__);
			fb0_ctl = ctl;
			return 1;
		}
	}
	return 0;
}

static bool mdss_mdp_kcal_is_panel_on(void)
{
	int i;
	struct mdss_mdp_ctl *ctl;
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();

	for (i = 0; i < mdata->nctl; i++) {
		ctl = mdata->ctl_off + i;
		if (mdss_mdp_ctl_is_power_on(ctl))
			return true;
	}

	return false;
}

static int mdss_mdp_kcal_display_commit(void)
{
	int i;
	int ret = 0;
	struct mdss_mdp_ctl *ctl;
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();

	for (i = 0; i < mdata->nctl; i++) {
		ctl = mdata->ctl_off + i;
		/* pp setup requires mfd */
		if (mdss_mdp_ctl_is_power_on(ctl) && ctl->mfd &&
				ctl->mfd->index == 0) {
			ret = mdss_mdp_pp_setup(ctl);
			if (ret)
				pr_err("%s: setup failed: %d\n", __func__, ret);
		}
	}

	return ret;
}

static void mdss_mdp_kcal_update_pcc(struct kcal_lut_data *lut_data)
{
	u32 copyback = 0;
	struct mdp_pcc_cfg_data pcc_config;

	struct mdp_pcc_data_v1_7 *payload;

	lut_data->red = lut_data->red < lut_data->minimum ?
		lut_data->minimum : lut_data->red;
	lut_data->green = lut_data->green < lut_data->minimum ?
		lut_data->minimum : lut_data->green;
	lut_data->blue = lut_data->blue < lut_data->minimum ?
		lut_data->minimum : lut_data->blue;

	memset(&pcc_config, 0, sizeof(struct mdp_pcc_cfg_data));

	pcc_config.version = mdp_pcc_v1_7;
	pcc_config.block = MDP_LOGICAL_BLOCK_DISP_0;
	pcc_config.ops = lut_data->enable ?
		MDP_PP_OPS_WRITE | MDP_PP_OPS_ENABLE :
			MDP_PP_OPS_WRITE | MDP_PP_OPS_DISABLE;
	pcc_config.r.r = lut_data->red * PCC_ADJ;
	pcc_config.g.g = lut_data->green * PCC_ADJ;
	pcc_config.b.b = lut_data->blue * PCC_ADJ;

	if (lut_data->invert) {
		pcc_config.r.c = pcc_config.g.c =
			pcc_config.b.c = 0x7ff8;
		pcc_config.r.r |= (0xffff << 16);
		pcc_config.g.g |= (0xffff << 16);
		pcc_config.b.b |= (0xffff << 16);
	}

	payload = kzalloc(sizeof(struct mdp_pcc_data_v1_7),GFP_USER);
	payload->r.r = pcc_config.r.r;
	payload->g.g = pcc_config.g.g;
	payload->b.b = pcc_config.b.b;
	pcc_config.cfg_payload = payload;

	if (!mdss_mdp_kcal_store_fb0_ctl()) return;
	mdss_mdp_pcc_config(fb0_ctl->mfd, &pcc_config, &copyback);
	kfree(payload);
}

static void mdss_mdp_kcal_read_pcc(struct kcal_lut_data *lut_data)
{
	u32 copyback = 0;
	struct mdp_pcc_cfg_data pcc_config;

	memset(&pcc_config, 0, sizeof(struct mdp_pcc_cfg_data));

	pcc_config.block = MDP_LOGICAL_BLOCK_DISP_0;
	pcc_config.ops = MDP_PP_OPS_READ;

	mdss_mdp_pcc_config(fb0_ctl->mfd, &pcc_config, &copyback);

	/* LiveDisplay disables pcc when using default values and regs
	 * are zeroed on pp resume, so throw these values out.
	 */
	if (!pcc_config.r.r && !pcc_config.g.g && !pcc_config.b.b)
		return;

	lut_data->red = (pcc_config.r.r & 0xffff) / PCC_ADJ;
	lut_data->green = (pcc_config.g.g & 0xffff) / PCC_ADJ;
	lut_data->blue = (pcc_config.b.b & 0xffff) / PCC_ADJ;
}

static void mdss_mdp_kcal_update_pa(struct kcal_lut_data *lut_data)
{
	u32 copyback = 0;
	struct mdp_pa_cfg_data pa_config;
	struct mdp_pa_v2_cfg_data pa_v2_config;
	struct mdp_pa_data_v1_7 *payload;

	struct mdss_data_type *mdata = mdss_mdp_get_mdata();

	if (!mdss_mdp_kcal_store_fb0_ctl()) return;

	if (mdata->mdp_rev < MDSS_MDP_HW_REV_103) {
		memset(&pa_config, 0, sizeof(struct mdp_pa_cfg_data));

		pa_config.block = MDP_LOGICAL_BLOCK_DISP_0;
		pa_config.pa_data.flags = lut_data->enable ?
			MDP_PP_OPS_WRITE | MDP_PP_OPS_ENABLE :
				MDP_PP_OPS_WRITE | MDP_PP_OPS_DISABLE;
		pa_config.pa_data.hue_adj = lut_data->hue;
		pa_config.pa_data.sat_adj = lut_data->sat;
		pa_config.pa_data.val_adj = lut_data->val;
		pa_config.pa_data.cont_adj = lut_data->cont;

		mdss_mdp_pa_config(fb0_ctl->mfd, &pa_config, &copyback);
	} else {
		memset(&pa_v2_config, 0, sizeof(struct mdp_pa_v2_cfg_data));

		pa_v2_config.version = mdp_pa_v1_7;
		pa_v2_config.block = MDP_LOGICAL_BLOCK_DISP_0;
		pa_v2_config.pa_v2_data.flags = lut_data->enable ?
			MDP_PP_OPS_WRITE | MDP_PP_OPS_ENABLE :
				MDP_PP_OPS_WRITE | MDP_PP_OPS_DISABLE;
		pa_v2_config.pa_v2_data.flags |= MDP_PP_PA_HUE_ENABLE;
		pa_v2_config.pa_v2_data.flags |= MDP_PP_PA_HUE_MASK;
		pa_v2_config.pa_v2_data.flags |= MDP_PP_PA_SAT_ENABLE;
		pa_v2_config.pa_v2_data.flags |= MDP_PP_PA_SAT_MASK;
		pa_v2_config.pa_v2_data.flags |= MDP_PP_PA_VAL_ENABLE;
		pa_v2_config.pa_v2_data.flags |= MDP_PP_PA_VAL_MASK;
		pa_v2_config.pa_v2_data.flags |= MDP_PP_PA_CONT_ENABLE;
		pa_v2_config.pa_v2_data.flags |= MDP_PP_PA_CONT_MASK;
		pa_v2_config.pa_v2_data.global_hue_adj = lut_data->hue;
		pa_v2_config.pa_v2_data.global_sat_adj = lut_data->sat;
		pa_v2_config.pa_v2_data.global_val_adj = lut_data->val;
		pa_v2_config.pa_v2_data.global_cont_adj = lut_data->cont;
		pa_v2_config.flags = pa_v2_config.pa_v2_data.flags;

		payload = kzalloc(sizeof(struct mdp_pa_data_v1_7),GFP_USER);
		payload->mode = pa_v2_config.flags;
		payload->global_hue_adj = lut_data->hue;
		payload->global_sat_adj = lut_data->sat;
		payload->global_val_adj = lut_data->val;
		payload->global_cont_adj = lut_data->cont;
		pa_v2_config.cfg_payload = payload;

		mdss_mdp_pa_v2_config(fb0_ctl->mfd, &pa_v2_config, &copyback);
		kfree(payload);
	}
}

static void mdss_mdp_kcal_check_pcc(struct kcal_lut_data *lut_data)
{
	lut_data->red = lut_data->red < lut_data->minimum ?
		lut_data->minimum : lut_data->red;
	lut_data->green = lut_data->green < lut_data->minimum ?
		lut_data->minimum : lut_data->green;
	lut_data->blue = lut_data->blue < lut_data->minimum ?
		lut_data->minimum : lut_data->blue;
}

static ssize_t kcal_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	int kcal_r, kcal_g, kcal_b, r;
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	r = sscanf(buf, "%d %d %d", &kcal_r, &kcal_g, &kcal_b);
	if ((r != 3) || (kcal_r < 0 || kcal_r > 256) ||
		(kcal_g < 0 || kcal_g > 256) || (kcal_b < 0 || kcal_b > 256))
		return -EINVAL;

	user_kcal_r = kcal_r;
	user_kcal_g = kcal_g;
	user_kcal_b = kcal_b;
	lut_data->red = kcal_r;
	lut_data->green = kcal_g;
	lut_data->blue = kcal_b;

	mdss_mdp_kcal_check_pcc(lut_data);

	if (mdss_mdp_kcal_is_panel_on()) {
		mdss_mdp_kcal_update_pcc(lut_data);
		mdss_mdp_kcal_display_commit();
	} else
		lut_data->queue_changes = true;

	return count;
}

static ssize_t kcal_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	if (mdss_mdp_kcal_is_panel_on() && lut_data->enable)
		mdss_mdp_kcal_read_pcc(lut_data);

	return scnprintf(buf, PAGE_SIZE, "%d %d %d\n",
		lut_data->red, lut_data->green, lut_data->blue);
}

static ssize_t kcal_min_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int kcal_min, r;
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	r = kstrtoint(buf, 10, &kcal_min);
	if ((r) || (kcal_min < 0 || kcal_min > 256))
		return -EINVAL;

	user_kcal_min = kcal_min;
	lut_data->minimum = kcal_min;

	if (mdss_mdp_kcal_is_panel_on()) {
		mdss_mdp_kcal_update_pcc(lut_data);
		mdss_mdp_kcal_display_commit();
	} else
		lut_data->queue_changes = true;

	return count;
}

static ssize_t kcal_min_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", lut_data->minimum);
}

static ssize_t kcal_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int kcal_enable, r;
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	r = kstrtoint(buf, 10, &kcal_enable);
	if ((r) || (kcal_enable != 0 && kcal_enable != 1) ||
		(lut_data->enable == kcal_enable))
		return -EINVAL;

	lut_data->enable = kcal_enable;

	if (mdss_mdp_kcal_is_panel_on()) {
		mdss_mdp_kcal_update_pcc(lut_data);
		mdss_mdp_kcal_update_pa(lut_data);
		//mdss_mdp_kcal_update_igc(lut_data);
	} else
		lut_data->queue_changes = true;

	return count;
}

static ssize_t kcal_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", lut_data->enable);
}

static ssize_t kcal_invert_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int kcal_invert, r;
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	r = kstrtoint(buf, 10, &kcal_invert);
	if ((r) || (kcal_invert != 0 && kcal_invert != 1) ||
		(lut_data->invert == kcal_invert))
		return -EINVAL;

	lut_data->invert = kcal_invert;

	if (mdss_mdp_kcal_is_panel_on()) {
		mdss_mdp_kcal_update_pcc(lut_data);
		mdss_mdp_kcal_display_commit();
	} else
		lut_data->queue_changes = true;

	return count;
}

static ssize_t kcal_invert_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", lut_data->invert);
}

static ssize_t kcal_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int kcal_modes, r;
    struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	r = kstrtoint(buf, 10, &kcal_modes);
	if ((r) || (kcal_modes < 0) || (kcal_modes > 6) || (kcal_custom_mode == kcal_modes))
		return -EINVAL;

	kcal_custom_mode = kcal_modes;

    kcal_apply_mode(dev);

	if (mdss_mdp_kcal_is_panel_on()) {
		mdss_mdp_kcal_update_pcc(lut_data);
		mdss_mdp_kcal_update_pa(lut_data);
		mdss_mdp_kcal_display_commit();
	} else
		lut_data->queue_changes = true;

	return count;
}

static ssize_t kcal_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	return scnprintf(buf, PAGE_SIZE, "%d\n", kcal_custom_mode);
}

static ssize_t kcal_sat_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int kcal_sat, r;
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	r = kstrtoint(buf, 10, &kcal_sat);
	if ((r) || ((kcal_sat < 224 || kcal_sat > 383) && kcal_sat != 128))
		return -EINVAL;

	user_kcal_sat = kcal_sat;
	lut_data->sat = kcal_sat;

	if (mdss_mdp_kcal_is_panel_on()) {
		mdss_mdp_kcal_update_pa(lut_data);
		mdss_mdp_kcal_display_commit();
	} else
		lut_data->queue_changes = true;

	return count;
}

static ssize_t kcal_sat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", lut_data->sat);
}

static ssize_t kcal_hue_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int kcal_hue, r;
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	r = kstrtoint(buf, 10, &kcal_hue);
	if ((r) || (kcal_hue < 0 || kcal_hue > 1536))
		return -EINVAL;

	lut_data->hue = kcal_hue;

	if (mdss_mdp_kcal_is_panel_on()) {
		mdss_mdp_kcal_update_pa(lut_data);
		mdss_mdp_kcal_display_commit();
	} else
		lut_data->queue_changes = true;

	return count;
}

static ssize_t kcal_hue_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", lut_data->hue);
}

static ssize_t kcal_val_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int kcal_val, r;
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	r = kstrtoint(buf, 10, &kcal_val);
	if ((r) || (kcal_val < 128 || kcal_val > 383))
		return -EINVAL;

	user_kcal_val = kcal_val;
	lut_data->val = kcal_val;

	if (mdss_mdp_kcal_is_panel_on()) {
		mdss_mdp_kcal_update_pa(lut_data);
		mdss_mdp_kcal_display_commit();
	} else
		lut_data->queue_changes = true;

	return count;
}

static ssize_t kcal_val_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", lut_data->val);
}

static ssize_t kcal_cont_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int kcal_cont, r;
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	r = kstrtoint(buf, 10, &kcal_cont);
	if ((r) || (kcal_cont < 128 || kcal_cont > 383))
		return -EINVAL;

	user_kcal_cont = kcal_cont;
	lut_data->cont = kcal_cont;

	if (mdss_mdp_kcal_is_panel_on()) {
		mdss_mdp_kcal_update_pa(lut_data);
		mdss_mdp_kcal_display_commit();
	} else
		lut_data->queue_changes = true;

	return count;
}

static ssize_t kcal_cont_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", lut_data->cont);
}

static DEVICE_ATTR(kcal, S_IWUSR | S_IRUGO, kcal_show, kcal_store);
static DEVICE_ATTR(kcal_min, S_IWUSR | S_IRUGO, kcal_min_show, kcal_min_store);
static DEVICE_ATTR(kcal_enable, S_IWUSR | S_IRUGO, kcal_enable_show,
	kcal_enable_store);
static DEVICE_ATTR(kcal_invert, S_IWUSR | S_IRUGO, kcal_invert_show,
	kcal_invert_store);
static DEVICE_ATTR(kcal_mode, S_IWUSR | S_IRUGO, kcal_mode_show, kcal_mode_store);
static DEVICE_ATTR(kcal_sat, S_IWUSR | S_IRUGO, kcal_sat_show, kcal_sat_store);
static DEVICE_ATTR(kcal_hue, S_IWUSR | S_IRUGO, kcal_hue_show, kcal_hue_store);
static DEVICE_ATTR(kcal_val, S_IWUSR | S_IRUGO, kcal_val_show, kcal_val_store);
static DEVICE_ATTR(kcal_cont, S_IWUSR | S_IRUGO, kcal_cont_show,
	kcal_cont_store);

static int mdss_mdp_kcal_update_queue(struct device *dev)
{
	struct kcal_lut_data *lut_data = dev_get_drvdata(dev);

	if (lut_data->queue_changes) {
		mdss_mdp_kcal_update_pcc(lut_data);
		mdss_mdp_kcal_update_pa(lut_data);
		lut_data->queue_changes = false;
	}

	return 0;
}

#ifdef CONFIG_KLAPSE
void klapse_kcal_push(int r, int g, int b)
{
	lut_cpy->red = r;
	lut_cpy->green = g;
	lut_cpy->blue = b;

	mdss_mdp_kcal_update_pcc(lut_cpy);
}

/* kcal_get_color() :
 * @param : 0 = red; 1 = green; 2 = blue;
 * @return : Value of color corresponding to @param, or 0 if not found
 */
unsigned short kcal_get_color(unsigned short int code)
{
  if (code == 0)
    return lut_cpy->red;
  else if (code == 1)
    return lut_cpy->green;
  else if (code == 2)
    return lut_cpy->blue;

  return 0;
}
#endif

#if defined(CONFIG_FB) && !defined(CONFIG_MMI_PANEL_NOTIFICATIONS)
static int fb_notifier_callback(struct notifier_block *nb,
	unsigned long event, void *data)
{
	int *blank;
	struct fb_event *evdata = data;
	struct kcal_lut_data *lut_data =
		container_of(nb, struct kcal_lut_data, panel_nb);

	if (evdata && evdata->data && event == FB_EVENT_BLANK) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK)
			mdss_mdp_kcal_update_queue(&lut_data->dev);
	}

	return 0;
}
#endif

static int kcal_ctrl_probe(struct platform_device *pdev)
{
	int ret;
	struct kcal_lut_data *lut_data;

	lut_data = devm_kzalloc(&pdev->dev, sizeof(*lut_data), GFP_KERNEL);
	if (!lut_data) {
		pr_err("%s: failed to allocate memory for lut_data\n",
			__func__);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, lut_data);

	lut_data->enable = 0x1;
	lut_data->red = DEF_PCC;
	lut_data->green = DEF_PCC;
	lut_data->blue = DEF_PCC;
	lut_data->minimum = 0x23;
	lut_data->invert = 0x0;
	lut_data->hue = 0x0;
	lut_data->sat = DEF_PA;
	lut_data->val = DEF_PA;
	lut_data->cont = DEF_PA;

	lut_data->queue_changes = false;

	mdss_mdp_kcal_update_pcc(lut_data);
	mdss_mdp_kcal_update_pa(lut_data);
	mdss_mdp_kcal_display_commit();

#ifdef CONFIG_KLAPSE
	lut_cpy = lut_data;
#endif

#if defined(CONFIG_MMI_PANEL_NOTIFICATIONS)
	lut_data->panel_nb.display_on = mdss_mdp_kcal_update_queue;
	lut_data->panel_nb.dev = &pdev->dev;
	ret = mmi_panel_register_notifier(&lut_data->panel_nb);
	if (ret) {
		pr_err("%s: unable to register MMI notifier\n", __func__);
		goto out_free_mem;
	}
#elif defined(CONFIG_FB)
	lut_data->dev = pdev->dev;
	lut_data->panel_nb.notifier_call = fb_notifier_callback;
	ret = fb_register_client(&lut_data->panel_nb);
	if (ret) {
		pr_err("%s: unable to register fb notifier\n", __func__);
		goto out_free_mem;
	}
#endif

	ret = device_create_file(&pdev->dev, &dev_attr_kcal);
	ret |= device_create_file(&pdev->dev, &dev_attr_kcal_min);
	ret |= device_create_file(&pdev->dev, &dev_attr_kcal_enable);
	ret |= device_create_file(&pdev->dev, &dev_attr_kcal_invert);
	ret |= device_create_file(&pdev->dev, &dev_attr_kcal_mode);
	ret |= device_create_file(&pdev->dev, &dev_attr_kcal_sat);
	ret |= device_create_file(&pdev->dev, &dev_attr_kcal_hue);
	ret |= device_create_file(&pdev->dev, &dev_attr_kcal_val);
	ret |= device_create_file(&pdev->dev, &dev_attr_kcal_cont);
	if (ret) {
		pr_err("%s: unable to create sysfs entries\n", __func__);
		goto out_notifier;
	}

	return 0;

out_notifier:
#if defined(CONFIG_MMI_PANEL_NOTIFICATIONS)
	mmi_panel_unregister_notifier(&lut_data->panel_nb);
#elif defined(CONFIG_FB)
	fb_unregister_client(&lut_data->panel_nb);
#endif
	kfree(lut_data);
out_free_mem:
	kfree(lut_data);
	return ret;
}

static int kcal_ctrl_remove(struct platform_device *pdev)
{
	struct kcal_lut_data *lut_data = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_kcal);
	device_remove_file(&pdev->dev, &dev_attr_kcal_min);
	device_remove_file(&pdev->dev, &dev_attr_kcal_enable);
	device_remove_file(&pdev->dev, &dev_attr_kcal_invert);
	device_remove_file(&pdev->dev, &dev_attr_kcal_mode);
	device_remove_file(&pdev->dev, &dev_attr_kcal_sat);
	device_remove_file(&pdev->dev, &dev_attr_kcal_hue);
	device_remove_file(&pdev->dev, &dev_attr_kcal_val);
	device_remove_file(&pdev->dev, &dev_attr_kcal_cont);

#if defined(CONFIG_MMI_PANEL_NOTIFICATIONS)
	mmi_panel_unregister_notifier(&lut_data->panel_nb);
#elif defined(CONFIG_FB)
	fb_unregister_client(&lut_data->panel_nb);
#endif
	kfree(lut_data);
	return 0;
}

static struct platform_driver kcal_ctrl_driver = {
	.probe = kcal_ctrl_probe,
	.remove = kcal_ctrl_remove,
	.driver = {
		.name = "kcal_ctrl",
	},
};

static struct platform_device kcal_ctrl_device = {
	.name = "kcal_ctrl",
};

static int __init kcal_ctrl_init(void)
{
	if (platform_driver_register(&kcal_ctrl_driver))
		return -ENODEV;

	if (platform_device_register(&kcal_ctrl_device))
		return -ENODEV;

	pr_info("%s: registered\n", __func__);

	return 0;
}

static void __exit kcal_ctrl_exit(void)
{
	platform_device_unregister(&kcal_ctrl_device);
	platform_driver_unregister(&kcal_ctrl_driver);
}

late_initcall(kcal_ctrl_init);
module_exit(kcal_ctrl_exit);
