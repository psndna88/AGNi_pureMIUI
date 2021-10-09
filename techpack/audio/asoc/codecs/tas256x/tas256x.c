#include "tas256x.h"
#include "tas2562.h"
#include "tas2564.h"
#include "tas256x-device.h"

#define LOG_TAG "[tas256x]"

#define TAS256X_MDELAY 0xFFFFFFFE
#define TAS256X_MSLEEP 0xFFFFFFFD
#define TAS256X_IVSENSER_ENABLE  1
#define TAS256X_IVSENSER_DISABLE 0
static char p_icn_threshold[] = {0x00, 0x01, 0x2f, 0x2c};
static char p_icn_hysteresis[] = {0x00, 0x01, 0x5d, 0xc0};

static unsigned int p_tas256x_classh_d_data[] = {
		/* reg address			size	values */
	TAS256X_CLASSHHEADROOM, 0x4, 0x09, 0x99, 0x99, 0x9a,
	TAS256X_CLASSHHYSTERESIS, 0x4, 0x0, 0x0, 0x0, 0x0,
	TAS256X_CLASSHMTCT, 0x4, 0xb, 0x0, 0x0, 0x0,
	TAS256X_VBATFILTER, 0x1, 0x38,
	TAS256X_CLASSHRELEASETIMER, 0x1, 0x3c,
	TAS256X_BOOSTSLOPE, 0x1, 0x78,
	TAS256X_TESTPAGECONFIGURATION, 0x1, 0xd,
	TAS256X_CLASSDCONFIGURATION3, 0x1, 0x8e,
	TAS256X_CLASSDCONFIGURATION2, 0x1, 0x49,
	TAS256X_CLASSDCONFIGURATION4, 0x1, 0x21,
	TAS256X_CLASSDCONFIGURATION1, 0x1, 0x80,
	TAS256X_EFFICIENCYCONFIGURATION, 0x1, 0xc1,
	0xFFFFFFFF, 0xFFFFFFFF
};

static char HPF_reverse_path[] = {
	0x7F, 0xFF,  0xFF,  0xFF,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00};

static char dvc_pcm[57][4] = {
	{0x00, 0x00, 0x0D, 0x43}, {0x00, 0x00, 0x10, 0xB3}, {0x00, 0x00, 0x15, 0x05},
	{0x00, 0x00, 0x1A, 0x77}, {0x00, 0x00, 0x21, 0x51}, {0x00, 0x00, 0x29, 0xF1},
	{0x00, 0x00, 0x34, 0xCE}, {0x00, 0x00, 0x42, 0x7A}, {0x00, 0x00, 0x53, 0xB0},
	{0x00, 0x00, 0x69, 0x5B}, {0x00, 0x00, 0x84, 0xA3}, {0x00, 0x00, 0xA6, 0xFA},
	{0x00, 0x00, 0xD2, 0x37}, {0x00, 0x01, 0x08, 0xA5}, {0x00, 0x01, 0x4D, 0x2A},
	{0x00, 0x01, 0xA3, 0x6E}, {0x00, 0x02, 0x10, 0x08}, {0x00, 0x02, 0x98, 0xC1},
	{0x00, 0x03, 0x44, 0xE0}, {0x00, 0x04, 0x1D, 0x90}, {0x00, 0x05, 0x2E, 0x5B},
	{0x00, 0x06, 0x85, 0xC8}, {0x00, 0x08, 0x36, 0x22}, {0x00, 0x0A, 0x56, 0x6D},
	{0x00, 0x0D, 0x03, 0xA7}, {0x00, 0x10, 0x62, 0x4E}, {0x00, 0x14, 0xA0, 0x51},
	{0x00, 0x19, 0xF7, 0x86}, {0x00, 0x20, 0xB0, 0xBD}, {0x00, 0x29, 0x27, 0x9E},
	{0x00, 0x33, 0xCF, 0x8E}, {0x00, 0x41, 0x39, 0xD3}, {0x00, 0x52, 0x1D, 0x51},
	{0x00, 0x67, 0x60, 0x45}, {0x00, 0x82, 0x24, 0x8A}, {0x00, 0xA3, 0xD7, 0x0A},
	{0x00, 0xCE, 0x43, 0x29}, {0x01, 0x03, 0xAB, 0x3D}, {0x01, 0x46, 0xE7, 0x5E},
	{0x01, 0x9B, 0x8C, 0x27}, {0x02, 0x06, 0x1B, 0x8A}, {0x02, 0x8C, 0x42, 0x40},
	{0x03, 0x35, 0x25, 0x29}, {0x04, 0x09, 0xC2, 0xB1}, {0x05, 0x15, 0x6D, 0x69},
	{0x06, 0x66, 0x66, 0x66}, {0x08, 0x0E, 0x9F, 0x97}, {0x0A, 0x24, 0xB0, 0x63},
	{0x0C, 0xC5, 0x09, 0xAC}, {0x10, 0x13, 0x79, 0x88}, {0x14, 0x3D, 0x13, 0x62},
	{0x19, 0x7A, 0x96, 0x7F}, {0x20, 0x13, 0x73, 0x9E}, {0x28, 0x61, 0x9A, 0xEA},
	{0x32, 0xD6, 0x46, 0x18}, {0x40, 0x00, 0x00, 0x00}, {0x50, 0x92, 0x3B, 0xE4}
};

static char lim_max_attn[16][4] = {
	{0x14, 0x49, 0x60, 0xC5}, {0x16, 0xC3, 0x10, 0xE3}, {0x19, 0x8A, 0x13, 0x57},
	{0x1C, 0xA7, 0xD7, 0x68}, {0x20, 0x26, 0xF3, 0x10}, {0x24, 0x13, 0x46, 0xF6},
	{0x28, 0x7A, 0x26, 0xC5}, {0x2D, 0x6A, 0x86, 0x6F}, {0x32, 0xF5, 0x2C, 0xFF},
	{0x39, 0x2C, 0xED, 0x8E}, {0x40, 0x26, 0xE7, 0x3D}, {0x47, 0xFA, 0xCC, 0xF0},
	{0x50, 0xC3, 0x35, 0xD4}, {0x5A, 0x9D, 0xF7, 0xAC}, {0x65, 0xAC, 0x8C, 0x2F},
	{0x72, 0x14, 0x82, 0xC0}
};

static char vbat_lim_max_thd[27][4] = {
	{0x14, 0x00, 0x00, 0x00}, {0x18, 0x00, 0x00, 0x00}, {0x1C, 0x00, 0x00, 0x00},
	{0x20, 0x00, 0x00, 0x00}, {0x24, 0x00, 0x00, 0x00}, {0x28, 0x00, 0x00, 0x00},
	{0x2C, 0x00, 0x00, 0x00}, {0x30, 0x00, 0x00, 0x00}, {0x34, 0x00, 0x00, 0x00},
	{0x38, 0x00, 0x00, 0x00}, {0x3C, 0x00, 0x00, 0x00}, {0x40, 0x00, 0x00, 0x00},
	{0x44, 0x00, 0x00, 0x00}, {0x48, 0x00, 0x00, 0x00}, {0x4C, 0x00, 0x00, 0x00},
	{0x50, 0x00, 0x00, 0x00}, {0x54, 0x00, 0x00, 0x00}, {0x58, 0x00, 0x00, 0x00},
	{0x5C, 0x00, 0x00, 0x00}, {0x60, 0x00, 0x00, 0x00}, {0x64, 0x00, 0x00, 0x00},
	{0x68, 0x00, 0x00, 0x00}, {0x6C, 0x00, 0x00, 0x00}, {0x70, 0x00, 0x00, 0x00},
	{0x74, 0x00, 0x00, 0x00}, {0x78, 0x00, 0x00, 0x00}, {0x7C, 0x00, 0x00, 0x00}
};

static char vbat_lim_min_thd[27][4] = {
	{0x14, 0x00, 0x00, 0x00}, {0x18, 0x00, 0x00, 0x00}, {0x1C, 0x00, 0x00, 0x00},
	{0x20, 0x00, 0x00, 0x00}, {0x24, 0x00, 0x00, 0x00}, {0x28, 0x00, 0x00, 0x00},
	{0x2C, 0x00, 0x00, 0x00}, {0x30, 0x00, 0x00, 0x00}, {0x34, 0x00, 0x00, 0x00},
	{0x38, 0x00, 0x00, 0x00}, {0x3C, 0x00, 0x00, 0x00}, {0x40, 0x00, 0x00, 0x00},
	{0x44, 0x00, 0x00, 0x00}, {0x48, 0x00, 0x00, 0x00}, {0x4C, 0x00, 0x00, 0x00},
	{0x50, 0x00, 0x00, 0x00}, {0x54, 0x00, 0x00, 0x00}, {0x58, 0x00, 0x00, 0x00},
	{0x5C, 0x00, 0x00, 0x00}, {0x60, 0x00, 0x00, 0x00}, {0x64, 0x00, 0x00, 0x00},
	{0x68, 0x00, 0x00, 0x00}, {0x6C, 0x00, 0x00, 0x00}, {0x70, 0x00, 0x00, 0x00},
	{0x74, 0x00, 0x00, 0x00}, {0x78, 0x00, 0x00, 0x00}, {0x7C, 0x00, 0x00, 0x00}
};

static char vbat_lim_infl_pt[41][4] = {
	{0x20, 0x00, 0x00, 0x00}, {0x21, 0x99, 0x99, 0x99}, {0x23, 0x33, 0x33, 0x33},
	{0x24, 0xCC, 0xCC, 0xCC}, {0x26, 0x66, 0x66, 0x66}, {0x28, 0x00, 0x00, 0x00},
	{0x29, 0x99, 0x99, 0x99}, {0x2B, 0x33, 0x33, 0x33}, {0x2C, 0xCC, 0xCC, 0xCC},
	{0x2E, 0x66, 0x66, 0x66}, {0x30, 0x00, 0x00, 0x00}, {0x31, 0x99, 0x99, 0x99},
	{0x33, 0x33, 0x33, 0x33}, {0x34, 0xCC, 0xCC, 0xCC}, {0x36, 0x66, 0x66, 0x66},
	{0x38, 0x00, 0x00, 0x00}, {0x39, 0x99, 0x99, 0x99}, {0x3B, 0x33, 0x33, 0x33},
	{0x3C, 0xCC, 0xCC, 0xCC}, {0x3E, 0x66, 0x66, 0x66}, {0x40, 0x00, 0x00, 0x00},
	{0x41, 0x99, 0x99, 0x99}, {0x43, 0x33, 0x33, 0x33}, {0x44, 0xCC, 0xCC, 0xCC},
	{0x46, 0x66, 0x66, 0x66}, {0x48, 0x00, 0x00, 0x00}, {0x49, 0x99, 0x99, 0x99},
	{0x4B, 0x33, 0x33, 0x33}, {0x4C, 0xCC, 0xCC, 0xCC}, {0x4E, 0x66, 0x66, 0x66},
	{0x4F, 0xFF, 0xFF, 0xFF}, {0x51, 0x99, 0x99, 0x99}, {0x53, 0x33, 0x33, 0x33},
	{0x54, 0xCC, 0xCC, 0xCC}, {0x56, 0x66, 0x66, 0x66}, {0x57, 0xFF, 0xFF, 0xFF},
	{0x59, 0x99, 0x99, 0x99}, {0x5B, 0x33, 0x33, 0x33}, {0x5C, 0xCC, 0xCC, 0xCC},
	{0x5E, 0x66, 0x66, 0x66}, {0x5F, 0xFF, 0xFF, 0xFF}
};

static char vbat_lim_track_slope[7][4] = {
	{0x10, 0x00, 0x00, 0x00}, {0x18, 0x00, 0x00, 0x00}, {0x20, 0x00, 0x00, 0x00},
	{0x28, 0x00, 0x00, 0x00}, {0x30, 0x00, 0x00, 0x00}, {0x38, 0x00, 0x00, 0x00},
	{0x40, 0x00, 0x00, 0x00}
};

static char bop_thd[16][4] = {
	{0x28, 0x00, 0x00, 0x00}, {0x29, 0x99, 0x99, 0x99}, {0x2B, 0x33, 0x33, 0x33},
	{0x2C, 0xCC, 0xCC, 0xCC}, {0x2E, 0x66, 0x66, 0x66}, {0x30, 0x00, 0x00, 0x00},
	{0x31, 0x99, 0x99, 0x99}, {0x33, 0x33, 0x33, 0x33}, {0x34, 0xCC, 0xCC, 0xCC},
	{0x36, 0x66, 0x66, 0x66}, {0x38, 0x00, 0x00, 0x00}, {0x39, 0x99, 0x99, 0x99},
	{0x3B, 0x33, 0x33, 0x33}, {0x3C, 0xCC, 0xCC, 0xCC}, {0x3E, 0x66, 0x66, 0x66},
	{0x40, 0x00, 0x00, 0x00}
};

static char bsd_thd[16][4] = {
	{0x28, 0x00, 0x00, 0x00}, {0x29, 0x99, 0x99, 0x99}, {0x2B, 0x33, 0x33, 0x33},
	{0x2C, 0xCC, 0xCC, 0xCC}, {0x2E, 0x66, 0x66, 0x66}, {0x30, 0x00, 0x00, 0x00},
	{0x31, 0x99, 0x99, 0x99}, {0x33, 0x33, 0x33, 0x33}, {0x34, 0xCC, 0xCC, 0xCC},
	{0x36, 0x66, 0x66, 0x66}, {0x38, 0x00, 0x00, 0x00}, {0x39, 0x99, 0x99, 0x99},
	{0x3B, 0x33, 0x33, 0x33}, {0x3C, 0xCC, 0xCC, 0xCC}, {0x3E, 0x66, 0x66, 0x66},
	{0x40, 0x00, 0x00, 0x00}
};

static int tas256x_i2c_load_data(struct tas256x_priv *p_tas256x,
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
		if (n_register == TAS256X_MSLEEP) {
			msleep(n_data[0]);
			pr_debug("%s %s, msleep = %d\n", LOG_TAG,
				__func__, n_data[0]);
		} else if (n_register == TAS256X_MDELAY) {
			msleep(n_data[0]);
			pr_debug("%s %s, mdelay = %d\n", LOG_TAG,
				__func__, n_data[0]);
		} else {
			if (n_register != 0xFFFFFFFF) {
				if (n_size > 128) {
					pr_err("%s %s, Line=%d, invalid size, maximum is 128 bytes!\n",
					 LOG_TAG, __func__, __LINE__);
					break;
				}
				if (n_size > 1) {
					for (i = 0; i < n_size; i++)
						buf[i] = (unsigned char)n_data[i];
					n_result = p_tas256x->bulk_write(
						p_tas256x, chn,
						n_register, buf, n_size);
					if (n_result < 0)
						break;
				} else if (n_size == 1) {
					n_result = p_tas256x->write(p_tas256x,
					chn,
					n_register, n_data[0]);
					if (n_result < 0)
						break;
				} else {
					pr_err("%s %s, Line=%d,invalid size, minimum is 1 bytes!\n",
						LOG_TAG, __func__, __LINE__);
				}
			}
		}
		n_length = n_length + 2 + p_data[n_length + 1];
	} while (n_register != 0xFFFFFFFF);
	return n_result;
}

int tas256x_update_default_params(struct tas256x_priv *p_tas256x, int ch)
{
	int n_result = 0;

	/*Initialize to default values*/
	if (p_tas256x->devs[ch-1]->device_id == DEVICE_TAS2562) {
		p_tas256x->devs[ch-1]->dvc_pcm = 55; /*0dB*/
		p_tas256x->devs[ch-1]->lim_max_attn = 7; /*-9dB*/
		p_tas256x->devs[ch-1]->lim_thr_max = 13; /*9V*/
		p_tas256x->devs[ch-1]->lim_thr_min = 3; /*4V*/
		p_tas256x->devs[ch-1]->lim_infl_pt = 13; /*3.3V*/
		p_tas256x->devs[ch-1]->lim_trk_slp = 0; /*1 V/V*/
		p_tas256x->devs[ch-1]->bop_thd = 4; /*2.9 V*/
		p_tas256x->devs[ch-1]->bosd_thd = 2; /*2.7 V*/
		p_tas256x->devs[ch-1]->bst_vltg = 0xA; /*Mode = speaker*/
		p_tas256x->devs[ch-1]->bst_ilm = 0x36; /*0dB*/
		p_tas256x->devs[ch-1]->lim_switch = 0; /*-9dB*/
		p_tas256x->devs[ch-1]->lim_att_rate = 1; /*9V*/
		p_tas256x->devs[ch-1]->lim_att_stp_size = 1; /*4V*/
		p_tas256x->devs[ch-1]->lim_rel_rate = 6; /*3.3V*/
		p_tas256x->devs[ch-1]->lim_rel_stp_size = 1; /*1 V/V*/
		p_tas256x->devs[ch-1]->bop_enable = 1; /*2.9 V*/
		p_tas256x->devs[ch-1]->bop_mute = 0; /*2.7 V*/
		p_tas256x->devs[ch-1]->bosd_enable = 0; /*3.3V*/
		p_tas256x->devs[ch-1]->bop_att_rate = 1; /*1 V/V*/
		p_tas256x->devs[ch-1]->bop_att_stp_size = 1; /*2.9 V*/
		p_tas256x->devs[ch-1]->bop_hld_time = 6; /*2.7 V*/
		p_tas256x->devs[ch-1]->vbat_lpf = 2; /*2.7 V*/
		p_tas256x->devs[ch-1]->rx_cfg = 0;
	} else if (p_tas256x->devs[ch-1]->device_id == DEVICE_TAS2564) {
		p_tas256x->devs[ch-1]->rx_mode = 0; /*Mode = speaker*/
		p_tas256x->devs[ch-1]->dvc_pcm = 55; /*0dB*/
		p_tas256x->devs[ch-1]->lim_max_attn = 7; /*-9dB*/
		p_tas256x->devs[ch-1]->lim_thr_max = 13; /*9V*/
		p_tas256x->devs[ch-1]->lim_thr_min = 3; /*4V*/
		p_tas256x->devs[ch-1]->lim_infl_pt = 13; /*3.3V*/
		p_tas256x->devs[ch-1]->lim_trk_slp = 0; /*1 V/V*/
		p_tas256x->devs[ch-1]->bop_thd = 4; /*2.9 V*/
		p_tas256x->devs[ch-1]->bosd_thd = 2; /*2.7 V*/
		p_tas256x->devs[ch-1]->bst_vltg = 13; /*Mode = speaker*/
		p_tas256x->devs[ch-1]->bst_ilm = 0x39; /*0dB*/
		p_tas256x->devs[ch-1]->lim_switch = 0; /*-9dB*/
		p_tas256x->devs[ch-1]->lim_att_rate = 1; /*9V*/
		p_tas256x->devs[ch-1]->lim_att_stp_size = 1; /*4V*/
		p_tas256x->devs[ch-1]->lim_rel_rate = 6; /*3.3V*/
		p_tas256x->devs[ch-1]->lim_rel_stp_size = 1; /*1 V/V*/
		p_tas256x->devs[ch-1]->bop_enable = 1; /*2.9 V*/
		p_tas256x->devs[ch-1]->bop_mute = 0; /*2.7 V*/
		p_tas256x->devs[ch-1]->bosd_enable = 0; /*3.3V*/
		p_tas256x->devs[ch-1]->bop_att_rate = 1; /*1 V/V*/
		p_tas256x->devs[ch-1]->bop_att_stp_size = 1; /*2.9 V*/
		p_tas256x->devs[ch-1]->bop_hld_time = 6; /*2.7 V*/
		p_tas256x->devs[ch-1]->vbat_lpf = 2; /*2.7 V*/
		p_tas256x->devs[ch-1]->rx_cfg = 0;
	}
	return n_result;
}

int tas256x_set_power_up(struct tas256x_priv *p_tas256x,
	enum channel chn)
{
	int n_result = 0;

	n_result = p_tas256x->update_bits(p_tas256x,
		chn, TAS256X_POWERCONTROL,
		TAS256X_POWERCONTROL_OPERATIONALMODE10_MASK,
		TAS256X_POWERCONTROL_OPERATIONALMODE10_ACTIVE);

	/* Let Power on the device */
	msleep(20);

	/* Enable Comparator Hysteresis */
	n_result = p_tas256x->update_bits(p_tas256x, chn,
		TAS256X_MISC_CLASSD,
		TAS256X_CMP_HYST_MASK,
		(0x01 << TAS256X_CMP_HYST_SHIFT));
	return n_result;
}

int tas256x_set_power_mute(struct tas256x_priv *p_tas256x,
	enum channel chn)
{
	int n_result = 0;

	/*Disable Comparator Hysteresis before Power Mute */
	n_result = p_tas256x->update_bits(p_tas256x, chn,
		TAS256X_MISC_CLASSD,
		TAS256X_CMP_HYST_MASK,
		(0x00 << TAS256X_CMP_HYST_SHIFT));

	n_result = p_tas256x->update_bits(p_tas256x, chn,
		TAS256X_POWERCONTROL,
		TAS256X_POWERCONTROL_OPERATIONALMODE10_MASK,
		TAS256X_POWERCONTROL_OPERATIONALMODE10_MUTE);

	return n_result;
}

int tas256x_set_power_shutdown(struct tas256x_priv *p_tas256x,
	enum channel chn)
{
	int n_result = 0;
	/*Disable Comparator Hysteresis before Power Down */
	n_result = p_tas256x->update_bits(p_tas256x, chn,
		TAS256X_MISC_CLASSD,
		TAS256X_CMP_HYST_MASK,
		(0x00 << TAS256X_CMP_HYST_SHIFT));

	n_result = p_tas256x->update_bits(p_tas256x, chn,
		TAS256X_POWERCONTROL,
		TAS256X_POWERCONTROL_OPERATIONALMODE10_MASK,
		TAS256X_POWERCONTROL_OPERATIONALMODE10_SHUTDOWN);

	/*Device Shutdown need 20ms after shutdown writes are made*/
	msleep(20);

	return n_result;
}

int tas256x_power_check(struct tas256x_priv *p_tas256x, int *state, int ch)
{
	int n_result = 0;
	int status;

	n_result = p_tas256x->read(p_tas256x, ch,
		TAS256X_POWERCONTROL, &status);

	status &= TAS256X_POWERCONTROL_OPERATIONALMODE10_MASK;

	if ((status != TAS256X_POWERCONTROL_OPERATIONALMODE10_SHUTDOWN)
		&& (status != TAS256X_POWERCONTROL_OPERATIONALMODE10_MUTE))
		*state = 1;
	else
		*state = 0;

	return n_result;
}

/*
 *bool enable = 1; IV Sense Power UP = 0;
 *	IV Sense Power Down
 */
int tas256x_iv_sense_enable_set(struct tas256x_priv *p_tas256x, bool enable, int ch)
{
	int n_result = 0;

	if (enable) /*IV Sense Power Up*/
		n_result = p_tas256x->update_bits(p_tas256x, ch,
			TAS256X_POWERCONTROL,
			TAS256X_POWERCONTROL_ISNSPOWER_MASK |
			TAS256X_POWERCONTROL_VSNSPOWER_MASK,
			TAS256X_POWERCONTROL_VSNSPOWER_ACTIVE |
			TAS256X_POWERCONTROL_ISNSPOWER_ACTIVE);
	else /*IV Sense Power Down*/
		n_result = p_tas256x->update_bits(p_tas256x, ch,
			TAS256X_POWERCONTROL,
			TAS256X_POWERCONTROL_ISNSPOWER_MASK |
			TAS256X_POWERCONTROL_VSNSPOWER_MASK,
			TAS256X_POWERCONTROL_VSNSPOWER_POWEREDDOWN |
			TAS256X_POWERCONTROL_ISNSPOWER_POWEREDDOWN);

	p_tas256x->iv_enable = enable;
	return n_result;
}

bool tas256x_iv_sense_enable_get(struct tas256x_priv *p_tas256x, int ch)
{
	int n_result = 0;
	bool enable = 0;
	int value = 0;

	n_result = p_tas256x->read(p_tas256x, ch,
			TAS256X_POWERCONTROL, &value);
	if (n_result < 0)
		pr_err("%s can't get ivsensor state %s, L=%d\n",
			 LOG_TAG, __func__, __LINE__);
	else if (((value & TAS256X_POWERCONTROL_ISNSPOWER_MASK)
			== TAS256X_POWERCONTROL_ISNSPOWER_ACTIVE)
			&& ((value & TAS256X_POWERCONTROL_VSNSPOWER_MASK)
			== TAS256X_POWERCONTROL_VSNSPOWER_ACTIVE)) {
		enable = TAS256X_IVSENSER_ENABLE;
	} else {
		enable = TAS256X_IVSENSER_DISABLE;
	}
	return enable;
}

/*No need channel argument*/
int tas256x_iv_slot_config(struct tas256x_priv *p_tas256x)
{
	int n_result = 0;

	if (p_tas256x->mn_channels == 2) {
		if (p_tas256x->mn_slot_width == 16) {
			p_tas256x->update_bits(p_tas256x, channel_left,
				TAS256X_TDMCONFIGURATIONREG5, 0xff, 0x41);

			p_tas256x->update_bits(p_tas256x, channel_left,
				TAS256X_TDMCONFIGURATIONREG6, 0xff, 0x40);

			p_tas256x->update_bits(p_tas256x, channel_right,
				TAS256X_TDMCONFIGURATIONREG5, 0xff, 0x43);

			p_tas256x->update_bits(p_tas256x, channel_right,
				TAS256X_TDMCONFIGURATIONREG6, 0xff, 0x42);
		} else {
			if (p_tas256x->mn_iv_width == 8) {
				p_tas256x->update_bits(p_tas256x, channel_left,
					TAS256X_TDMCONFIGURATIONREG5,
					0xff, 0x41);

				p_tas256x->update_bits(p_tas256x, channel_left,
					TAS256X_TDMCONFIGURATIONREG6,
					0xff, 0x40);

				p_tas256x->update_bits(p_tas256x, channel_right,
					TAS256X_TDMCONFIGURATIONREG5,
					0xff, 0x45);

				p_tas256x->update_bits(p_tas256x, channel_right,
					TAS256X_TDMCONFIGURATIONREG6,
					0xff, 0x44);

				if (p_tas256x->mn_vbat == 1) {
					p_tas256x->update_bits(p_tas256x,
						channel_left,
						TAS256X_TDMCONFIGURATIONREG7,
						0xff, 0x42);

					p_tas256x->update_bits(p_tas256x,
						channel_right,
						TAS256X_TDMCONFIGURATIONREG7,
						0xff, 0x46);
				}
			} else {
				p_tas256x->update_bits(p_tas256x, channel_left,
					TAS256X_TDMCONFIGURATIONREG5,
					0xff, 0x42);

				p_tas256x->update_bits(p_tas256x, channel_left,
					TAS256X_TDMCONFIGURATIONREG6,
					0xff, 0x40);

				p_tas256x->update_bits(p_tas256x, channel_right,
					TAS256X_TDMCONFIGURATIONREG5,
					0xff, 0x46);

				p_tas256x->update_bits(p_tas256x, channel_right,
					TAS256X_TDMCONFIGURATIONREG6,
					0xff, 0x44);
			}
		}
	} else if ((p_tas256x->mn_channels == 1)
		&& (p_tas256x->mn_slot_width == 32)) {
		if (p_tas256x->mn_iv_width == 16) {
		p_tas256x->update_bits(p_tas256x, channel_left,
				TAS256X_TDMCONFIGURATIONREG2,
				TAS256X_TDMCONFIGURATIONREG2_IVLEN76_MASK,
				TAS256X_TDMCONFIGURATIONREG2_IVLENCFG76_16BITS);
			p_tas256x->update_bits(p_tas256x, channel_left,
			TAS256X_TDMCONFIGURATIONREG5, 0xff, 0x44);

		p_tas256x->update_bits(p_tas256x, channel_left,
			TAS256X_TDMCONFIGURATIONREG6, 0xff, 0x40);
		} else if (p_tas256x->mn_iv_width == 12) {
			p_tas256x->update_bits(p_tas256x, channel_left,
				TAS256X_TDMCONFIGURATIONREG2,
				TAS256X_TDMCONFIGURATIONREG2_IVLEN76_MASK,
				TAS256X_TDMCONFIGURATIONREG2_IVLENCFG76_12BITS);
			p_tas256x->update_bits(p_tas256x, channel_left,
				TAS256X_TDMCONFIGURATIONREG5, 0xff, 0x41);
			p_tas256x->update_bits(p_tas256x, channel_left,
				TAS256X_TDMCONFIGURATIONREG6, 0xff, 0x40);
			if (p_tas256x->mn_vbat == 1)
				p_tas256x->update_bits(p_tas256x, channel_left,
					TAS256X_TDMCONFIGURATIONREG7,
					0xff, 0x44);
		}
	} else if ((p_tas256x->mn_channels == 1)
		&& (p_tas256x->mn_slot_width == 16)) {
		if (p_tas256x->mn_iv_width == 16) {
			p_tas256x->update_bits(p_tas256x, channel_left,
				TAS256X_TDMCONFIGURATIONREG2,
				TAS256X_TDMCONFIGURATIONREG2_IVLEN76_MASK,
				TAS256X_TDMCONFIGURATIONREG2_IVLENCFG76_16BITS);
			p_tas256x->update_bits(p_tas256x, channel_left,
				TAS256X_TDMCONFIGURATIONREG5, 0xff, 0x42);

			p_tas256x->update_bits(p_tas256x, channel_left,
				TAS256X_TDMCONFIGURATIONREG6, 0xff, 0x40);
		} else if (p_tas256x->mn_iv_width == 12) {
			p_tas256x->update_bits(p_tas256x, channel_left,
				TAS256X_TDMCONFIGURATIONREG2,
				TAS256X_TDMCONFIGURATIONREG2_IVLEN76_MASK,
				TAS256X_TDMCONFIGURATIONREG2_IVLENCFG76_12BITS);
			p_tas256x->update_bits(p_tas256x, channel_left,
				TAS256X_TDMCONFIGURATIONREG5, 0xff, 0x41);
			p_tas256x->update_bits(p_tas256x, channel_left,
				TAS256X_TDMCONFIGURATIONREG6, 0xff, 0x40);
			if (p_tas256x->mn_vbat == 1)
				p_tas256x->update_bits(p_tas256x, channel_left,
					TAS256X_TDMCONFIGURATIONREG7,
					0xff, 0x43);
		}
	} else {
		pr_err("%s %s, wrong params, slot %d\n",
			LOG_TAG, __func__, p_tas256x->mn_slot_width);
	}

	return n_result;
}

/*No need channel argument*/
int tas256x_iv_bitwidth_config(struct tas256x_priv *p_tas256x)
{
	int n_result = 0;

	if (p_tas256x->mn_iv_width == 8)
		n_result = p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG2,
			TAS256X_TDMCONFIGURATIONREG2_IVMONLEN76_MASK,
			TAS256X_TDMCONFIGURATIONREG2_IVMONLEN76_8BITS);
	else if (p_tas256x->mn_iv_width == 12)
		n_result = p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG2,
			TAS256X_TDMCONFIGURATIONREG2_IVLEN76_MASK,
			TAS256X_TDMCONFIGURATIONREG2_IVLENCFG76_12BITS);
	else /*mn_iv_width == 16*/
		n_result = p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG2,
			TAS256X_TDMCONFIGURATIONREG2_IVLEN76_MASK,
			TAS256X_TDMCONFIGURATIONREG2_IVLENCFG76_16BITS);

	return n_result;
}

/**/
int tas256x_set_samplerate(struct tas256x_priv *p_tas256x,
			int samplerate)
{
	int n_result = 0;

	switch (samplerate) {
	case 48000:
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG0,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATERAMP_MASK,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATERAMP_48KHZ);
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG0,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATE31_MASK,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATE31_44_1_48KHZ);
		break;
	case 44100:
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG0,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATERAMP_MASK,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATERAMP_44_1KHZ);
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG0,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATE31_MASK,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATE31_44_1_48KHZ);
		break;
	case 96000:
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG0,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATERAMP_MASK,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATERAMP_48KHZ);
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG0,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATE31_MASK,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATE31_88_2_96KHZ);
		break;
	case 88200:
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG0,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATERAMP_MASK,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATERAMP_44_1KHZ);
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG0,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATE31_MASK,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATE31_88_2_96KHZ);
		break;
	case 19200:
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG0,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATERAMP_MASK,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATERAMP_48KHZ);
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG0,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATE31_MASK,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATE31_176_4_192KHZ);
		break;
	case 17640:
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG0,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATERAMP_MASK,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATERAMP_44_1KHZ);
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG0,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATE31_MASK,
			TAS256X_TDMCONFIGURATIONREG0_SAMPRATE31_176_4_192KHZ);
		break;
	default:
		pr_info("%s %s, unsupported sample rate, %d\n",
			 LOG_TAG, __func__, samplerate);
	}

	p_tas256x->mn_sampling_rate = samplerate;

	return n_result;
}

/*
 *rx_edge = 0; Falling
 *= 1; Rising
 */
int tas256x_rx_set_fmt(struct tas256x_priv *p_tas256x,
	unsigned int rx_edge, unsigned int rx_start_slot)
{
	int n_result = 0;

	if (rx_edge)
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG1,
			TAS256X_TDMCONFIGURATIONREG1_RXEDGE_MASK,
			TAS256X_TDMCONFIGURATIONREG1_RXEDGE_RISING);
	else
		p_tas256x->update_bits(p_tas256x, channel_both,
			TAS256X_TDMCONFIGURATIONREG1,
			TAS256X_TDMCONFIGURATIONREG1_RXEDGE_MASK,
			TAS256X_TDMCONFIGURATIONREG1_RXEDGE_FALLING);

	p_tas256x->update_bits(p_tas256x, channel_left,
		TAS256X_TDMCONFIGURATIONREG1,
		TAS256X_TDMCONFIGURATIONREG1_RXOFFSET51_MASK,
		(rx_start_slot <<
		TAS256X_TDMCONFIGURATIONREG1_RXOFFSET51_SHIFT));

	if (p_tas256x->mn_channels == 2)
		p_tas256x->update_bits(p_tas256x, channel_right,
			TAS256X_TDMCONFIGURATIONREG1,
			TAS256X_TDMCONFIGURATIONREG1_RXOFFSET51_MASK,
			(rx_start_slot <<
			TAS256X_TDMCONFIGURATIONREG1_RXOFFSET51_SHIFT));

	return n_result;
}

int tas256x_rx_set_slot(struct tas256x_priv *p_tas256x,
	int slot_width)
{
	int n_result = -1;

	switch (slot_width) {
	case 16:
	n_result = p_tas256x->update_bits(p_tas256x, channel_both,
		TAS256X_TDMCONFIGURATIONREG2,
		TAS256X_TDMCONFIGURATIONREG2_RXSLEN10_MASK,
		TAS256X_TDMCONFIGURATIONREG2_RXSLEN10_16BITS);
	break;
	case 24:
	n_result = p_tas256x->update_bits(p_tas256x, channel_both,
		TAS256X_TDMCONFIGURATIONREG2,
		TAS256X_TDMCONFIGURATIONREG2_RXSLEN10_MASK,
		TAS256X_TDMCONFIGURATIONREG2_RXSLEN10_24BITS);
	break;
	case 32:
	n_result = p_tas256x->update_bits(p_tas256x, channel_both,
		TAS256X_TDMCONFIGURATIONREG2,
		TAS256X_TDMCONFIGURATIONREG2_RXSLEN10_MASK,
		TAS256X_TDMCONFIGURATIONREG2_RXSLEN10_32BITS);
	break;
	case 0:
	/* Do not change slot width */
	break;
	}

	if (n_result == 0)
		p_tas256x->mn_slot_width = slot_width;

	return n_result;
}

int tas256x_rx_set_bitwidth(struct tas256x_priv *p_tas256x,
	int bitwidth)
{
	int n_result = 0;

	switch (bitwidth) {
	case 16:
		p_tas256x->update_bits(p_tas256x, channel_both,
		TAS256X_TDMCONFIGURATIONREG2,
		TAS256X_TDMCONFIGURATIONREG2_RXWLEN32_MASK,
		TAS256X_TDMCONFIGURATIONREG2_RXWLEN32_16BITS);
		break;
	case 24:
		p_tas256x->update_bits(p_tas256x, channel_both,
		TAS256X_TDMCONFIGURATIONREG2,
		TAS256X_TDMCONFIGURATIONREG2_RXWLEN32_MASK,
		TAS256X_TDMCONFIGURATIONREG2_RXWLEN32_24BITS);
		break;
	case 32:
		p_tas256x->update_bits(p_tas256x, channel_both,
		TAS256X_TDMCONFIGURATIONREG2,
		TAS256X_TDMCONFIGURATIONREG2_RXWLEN32_MASK,
		TAS256X_TDMCONFIGURATIONREG2_RXWLEN32_32BITS);
		break;

	default:
		pr_info("%s Not supported params format\n",  LOG_TAG);
		break;
	}

	return n_result;
}

int tas256x_interrupt_clear(struct tas256x_priv *p_tas256x, int ch)
{
	int n_result = 0;

	p_tas256x->update_bits(p_tas256x, ch,
		TAS256X_INTERRUPTCONFIGURATION,
		TAS256X_INTERRUPTCONFIGURATION_LTCHINTCLEAR_MASK,
		TAS256X_INTERRUPTCONFIGURATION_LTCHINTCLEAR);

	return n_result;
}

int tas256x_interrupt_enable(struct tas256x_priv *p_tas256x,
	int val, int ch)
{
	int n_result = 0;

	if (val) {
		/*Enable Interrupts*/
		n_result = p_tas256x->write(p_tas256x, ch,
			TAS256X_INTERRUPTMASKREG0, 0xf8);
		n_result = p_tas256x->write(p_tas256x, ch,
			TAS256X_INTERRUPTMASKREG1, 0xb1);
	} else {
		/*Disable Interrupts*/
		n_result = p_tas256x->write(p_tas256x, ch,
			TAS256X_INTERRUPTMASKREG0,
			TAS256X_INTERRUPTMASKREG0_DISABLE);
		n_result = p_tas256x->write(p_tas256x, ch,
			TAS256X_INTERRUPTMASKREG1,
			TAS256X_INTERRUPTMASKREG1_DISABLE);
	}

	return n_result;
}

int tas256x_interrupt_read(struct tas256x_priv *p_tas256x,
	int *intr1, int *intr2, int ch)
{
	int n_result = 0;

	n_result = p_tas256x->read(p_tas256x, ch,
		TAS256X_LATCHEDINTERRUPTREG0, intr1);
	n_result = p_tas256x->read(p_tas256x, ch,
		TAS256X_LATCHEDINTERRUPTREG1, intr2);

	return n_result;
}

int tas256x_icn_enable(struct tas256x_priv *p_tas256x, int enable, int ch)
{
	int n_result = 0;

	if (enable) { /*Enable*/
		p_tas256x->update_bits(p_tas256x, ch,
			TAS256X_ICN_SW_REG,
			TAS256X_ICN_SW_MASK,
			TAS256X_ICN_SW_ENABLE);
		pr_info("%s %s: ICN Enable!\n",  LOG_TAG, __func__);
	} else { /*Disable*/
		p_tas256x->update_bits(p_tas256x, ch,
			TAS256X_ICN_SW_REG,
			TAS256X_ICN_SW_MASK,
			TAS256X_ICN_SW_DISABLE);
		pr_info("%s %s: ICN Disable!\n",  LOG_TAG, __func__);
	}

	return n_result;
}

/* icn_hysteresis & p_icn_thresholds*/
int tas256x_icn_data(struct tas256x_priv *p_tas256x, int ch)
{
	int n_result = 0;

	pr_info("%s set ICN to -80dB\n", LOG_TAG);
	n_result = p_tas256x->bulk_write(p_tas256x, ch,
		TAS256X_ICN_THRESHOLD_REG,
		p_icn_threshold,
		sizeof(p_icn_threshold));
	n_result = p_tas256x->bulk_write(p_tas256x, ch,
		TAS256X_ICN_HYSTERESIS_REG,
		p_icn_hysteresis,
		sizeof(p_icn_hysteresis));

	return n_result;
}

int tas256x_icn_config(struct tas256x_priv *p_tas256x, int value, int ch)
{
	int n_result = 0;

	n_result = p_tas256x->write(p_tas256x, ch,
		TAS256X_REG(0, 253, 13), 0x0d);
	if (n_result < 0)
		return n_result;

	n_result = p_tas256x->write(p_tas256x, ch,
		TAS256X_REG(0, 253, 25), 0x80);

	return n_result;
}

int tas256x_boost_volt_update(struct tas256x_priv *p_tas256x, int value,
	int ch)
{
	int n_result = 0;

	if (value == DEVICE_TAS2558) {
		/* Max voltage to 9V */
		/*TODO: Need to be fixed*/
		n_result = p_tas256x->update_bits(p_tas256x, ch,
			TAS2562_BOOSTCONFIGURATION2,
			TAS2562_BOOSTCONFIGURATION2_BOOSTMAXVOLTAGE_MASK,
			0x7);
		if (n_result < 0)
			return n_result;
		n_result = p_tas256x->update_bits(p_tas256x, ch,
			TAS2562_PLAYBACKCONFIGURATIONREG0,
			TAS2562_PLAYBACKCONFIGURATIONREG0_AMPLIFIERLEVEL51_MASK,
			0xd << 1);
	} else if (value == DEVICE_TAS2562) {
		/*TODO: ??*/
	} else if (value == DEVICE_TAS2564) {
		/*Update Channel Gain to 16dBV*/
		n_result = p_tas256x->update_bits(p_tas256x, ch,
			TAS256X_PLAYBACKCONFIGURATIONREG0,
			TAS2564_PLAYBACKCONFIGURATION_AMP_LEVEL_MASK,
			TAS2564_AMP_LEVEL_16dBV);
	}
	return n_result;
}

int tas256x_set_misc_config(struct tas256x_priv *p_tas256x, int value, int ch)
{
	int n_result = 0;

	n_result = p_tas256x->write(p_tas256x, ch,
		TAS256X_MISCCONFIGURATIONREG0, 0xcf);

	return n_result;

}

int tas256x_set_tx_config(struct tas256x_priv *p_tas256x, int value, int ch)
{
	int n_result = 0;

#ifdef CONFIG_PLATFORM_EXYNOS
	if (p_tas256x->mn_channels == 2) {
		n_result = p_tas256x->write(p_tas256x, channel_left,
				TAS256X_TDMConfigurationReg4, 0xf3);
		if (n_result < 0)
			return n_result;
		n_result = p_tas256x->write(p_tas256x, channel_right,
				TAS256X_TDMConfigurationReg4, 0x13);
		if (n_result < 0)
			return n_result;
	} else {
		n_result = p_tas256x->write(p_tas256x, channel_both,
				TAS256X_TDMConfigurationReg4, 0x03);
		if (n_result < 0)
			return n_result;
	}
#else
	if (p_tas256x->mn_channels == 2) {
		n_result = p_tas256x->write(p_tas256x, channel_left,
			TAS256X_TDMConfigurationReg4, 0xf1);
		if (n_result < 0)
			return n_result;
		n_result = p_tas256x->write(p_tas256x, channel_right,
				TAS256X_TDMConfigurationReg4, 0x11);
		if (n_result < 0)
			return n_result;
	} else {
		n_result = p_tas256x->write(p_tas256x, channel_both,
				TAS256X_TDMConfigurationReg4, 0x01);
		if (n_result < 0)
			return n_result;
	}
#endif

	return n_result;
}

int tas256x_set_clock_config(struct tas256x_priv *p_tas256x, int value, int ch)
{
	int n_result = 0;

	n_result = p_tas256x->write(p_tas256x, ch,
		TAS256X_CLOCKCONFIGURATION, 0x0c);

	/* Increase the clock halt timer to 838ms to avoid
	 * TDM Clock errors during playback start/stop
	 */
	n_result |= p_tas256x->update_bits(p_tas256x, ch,
		TAS256X_INTERRUPTCONFIGURATION,
		TAS256X_CLOCK_HALT_TIMER_MASK,
		TAS256X_CLOCK_HALT_838MS);
	return n_result;
}

int tas256x_set_classH_config(struct tas256x_priv *p_tas256x, int value,
	int ch)
{
	int n_result = 0;

	n_result = tas256x_i2c_load_data(p_tas256x, ch,
			p_tas256x_classh_d_data);

	return n_result;
}

int tas256x_HPF_FF_Bypass(struct tas256x_priv *p_tas256x, int value, int ch)
{
	int n_result = 0;

	n_result = p_tas256x->update_bits(p_tas256x, ch,
			TAS256X_PLAYBACKCONFIGURATIONREG0,
			TAS256X_PLAYBACKCONFIGURATIONREG0_DCBLOCKER_MASK,
			TAS256X_PLAYBACKCONFIGURATIONREG0_DCBLOCKER_DISABLED);

	return n_result;
}

int tas256x_HPF_FB_Bypass(struct tas256x_priv *p_tas256x, int value, int ch)
{
	int n_result = 0;

	n_result = p_tas256x->bulk_write(p_tas256x, ch,
		TAS256X_HPF, HPF_reverse_path,
		sizeof(HPF_reverse_path)/sizeof(HPF_reverse_path[0]));

	return n_result;

}

int tas56x_software_reset(struct tas256x_priv *p_tas256x, int ch)
{
	int n_result = 0;

	n_result = p_tas256x->write(p_tas256x, ch, TAS256X_SOFTWARERESET,
		TAS256X_SOFTWARERESET_SOFTWARERESET_RESET);

	msleep(20);

	return n_result;
}

int tas256x_interrupt_determine(struct tas256x_priv *p_tas256x, int ch,
	int int1status, int int2status)
{
	int mn_err_code = 0;

	if (((int1status & 0x7) != 0)
		|| ((int2status & 0x0f) != 0)) {

		if (int1status &
			TAS256X_LATCHEDINTERRUPTREG0_TDMCLOCKERRORSTICKY_INTERRUPT) {
			mn_err_code |= ERROR_CLOCK;
			pr_err("%s TDM clock error!\n", LOG_TAG);
		} else {
			mn_err_code &= ~ERROR_CLOCK;
		}

		if (int1status &
			TAS256X_LATCHEDINTERRUPTREG0_OCEFLAGSTICKY_INTERRUPT) {
			mn_err_code |= ERROR_OVER_CURRENT;
			pr_err("%s SPK over current!\n", LOG_TAG);
		} else {
			mn_err_code &= ~ERROR_OVER_CURRENT;
		}

		if (int1status &
			TAS256X_LATCHEDINTERRUPTREG0_OTEFLAGSTICKY_INTERRUPT) {
			mn_err_code |= ERROR_DIE_OVERTEMP;
			pr_err("%s die over temperature!\n", LOG_TAG);
		} else {
			mn_err_code &= ~ERROR_DIE_OVERTEMP;
		}

		if (int2status &
			TAS256X_LATCHEDINTERRUPTREG1_VBATOVLOSTICKY_INTERRUPT) {
			mn_err_code |= ERROR_OVER_VOLTAGE;
			pr_err("%s SPK over voltage!\n", LOG_TAG);
		} else {
			mn_err_code &= ~ERROR_OVER_VOLTAGE;
		}

		if (int2status &
			TAS256X_LATCHEDINTERRUPTREG1_VBATUVLOSTICKY_INTERRUPT) {
			mn_err_code |= ERROR_UNDER_VOLTAGE;
			pr_err("%s SPK under voltage!\n", LOG_TAG);
		} else {
			mn_err_code &= ~ERROR_UNDER_VOLTAGE;
		}

		if (int2status &
			TAS256X_LATCHEDINTERRUPTREG1_BROWNOUTFLAGSTICKY_INTERRUPT) {
			mn_err_code |= ERROR_BROWNOUT;
			pr_err("%s brownout!\n", LOG_TAG);
		} else {
			mn_err_code &= ~ERROR_BROWNOUT;
		}
	} else {
		return 0;
	}

	return mn_err_code;
}

int tas56x_get_chipid(struct tas256x_priv *p_tas256x, int *chipid, int ch)
{
	int n_result = 0;

	n_result = p_tas256x->read(p_tas256x, ch,
		TAS256X_CHIPID, chipid);

	return n_result;
}

int tas2564_rx_mode_update(struct tas256x_priv *p_tas256x, int rx_mode, int ch)
{
	int n_result = 0;

	if (rx_mode) {
		/* Default code */
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS2564_PLAYBACKCONFIGURATIONREG0,
			TAS2564_PLAYBACKCONFIGURATIONREG_RX_SPKR_MODE_MASK,
			TAS2564_PLAYBACKCONFIGURATIONREG_RX_MODE);

		/*Unlock test page*/
		n_result =
			p_tas256x->write(p_tas256x, ch, TAS256X_TEST_PAGE_LOCK,
				0xd);
		/*Keep DAC modulator dither to minimum*/
		n_result |=
			p_tas256x->write(p_tas256x, ch, TAS256X_DAC_MODULATOR,
				0xc0);
		/* ICN improvement*/
		n_result |=
			p_tas256x->write(p_tas256x, ch, TAS256X_ICN_IMPROVE,
				0x1f);
		/*  LSFB strength bias current 9uA -> 3uA*/
		n_result |=
			p_tas256x->write(p_tas256x, ch, TAS256X_CLASSDCONFIGURATION2,
				0x08);
		/*Enable IRQ pull-up, disable SSM*/
		n_result |=
			p_tas256x->write(p_tas256x, ch,
				TAS256X_MISCCONFIGURATIONREG0,
				0xca);
		/*Mask reaction of idle channel detect on IV sense*/
		n_result |=
			p_tas256x->write(p_tas256x, ch, TAS256X_ICN_SW_REG,
				0x00);
	} else {
		/* Default code */
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS2564_PLAYBACKCONFIGURATIONREG0,
			TAS2564_PLAYBACKCONFIGURATIONREG_RX_SPKR_MODE_MASK,
			TAS2564_PLAYBACKCONFIGURATIONREG_SPKR_MODE);

		/*Unlock test page*/
		n_result =
			p_tas256x->write(p_tas256x, ch, TAS256X_TEST_PAGE_LOCK,
				0xd);
		/*Keep DAC modulator dither to minimum*/
		n_result |=
			p_tas256x->write(p_tas256x, ch, TAS256X_DAC_MODULATOR,
				0x00);
		/* ICN improvement*/
		n_result |=
			p_tas256x->write(p_tas256x, ch, TAS256X_ICN_IMPROVE,
				0x1f);
		/*  Default value = 0x28; LSFB strength = 9uA max.*/
		n_result |=
			p_tas256x->write(p_tas256x, ch, TAS256X_CLASSDCONFIGURATION2,
				0x28);
		/*Enable IRQ pull-up, disable SSM*/
		n_result |=
			p_tas256x->write(p_tas256x, ch,
				TAS256X_MISCCONFIGURATIONREG0,
				0xc6);
		/*Mask reaction of idle channel detect on IV sense*/
		n_result |=
			p_tas256x->write(p_tas256x, ch, TAS256X_ICN_SW_REG,
				0x10);
	}
	if (n_result == 0)
		p_tas256x->devs[ch-1]->rx_mode = rx_mode;

	return n_result;
}

int tas256x_update_playback_volume(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 57)) {
		n_result = p_tas256x->bulk_write(p_tas256x, ch,
			TAS256X_DVC_PCM,
			(char *)&(dvc_pcm[value][0]), sizeof(int));
		if (n_result == 0)
			p_tas256x->devs[ch-1]->dvc_pcm = value;
	}

	return n_result;
}

int tas256x_update_lim_max_attenuation(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 16)) {
		n_result = p_tas256x->bulk_write(p_tas256x, ch,
			TAS256X_LIM_MAX_ATN,
			(char *)&(lim_max_attn[value][0]), sizeof(int));
		if (n_result == 0)
			p_tas256x->devs[ch-1]->lim_max_attn = value;
	}

	return n_result;
}

int tas256x_update_lim_max_thr(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 27)) {
		n_result = p_tas256x->bulk_write(p_tas256x, ch,
			TAS256X_LIMB_TH_MAX,
			(char *)&(vbat_lim_max_thd[value][0]), sizeof(int));
		if (n_result == 0)
			p_tas256x->devs[ch-1]->lim_thr_max = value;
	}

	return n_result;
}

int tas256x_update_lim_min_thr(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 27)) {
		n_result = p_tas256x->bulk_write(p_tas256x, ch,
			TAS256X_LIMB_TH_MIN,
			(char *)&(vbat_lim_min_thd[value][0]), sizeof(int));
		if (n_result == 0)
			p_tas256x->devs[ch-1]->lim_thr_min = value;
	}

	return n_result;
}

int tas256x_update_lim_inflection_point(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 41)) {
		n_result = p_tas256x->bulk_write(p_tas256x, ch,
			TAS256X_LIMB_INF_PT,
			(char *)&(vbat_lim_infl_pt[value][0]), sizeof(int));
		if (n_result == 0)
			p_tas256x->devs[ch-1]->lim_infl_pt = value;
	}

	return n_result;
}

int tas256x_update_lim_slope(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 7)) {
		n_result = p_tas256x->bulk_write(p_tas256x, ch,
			TAS256X_LIMB_SLOPE,
			(char *)&(vbat_lim_track_slope[value][0]), sizeof(int));
		if (n_result == 0)
			p_tas256x->devs[ch-1]->lim_trk_slp = value;
	}

	return n_result;
}

int tas256x_update_bop_thr(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 16)) {
		n_result = p_tas256x->bulk_write(p_tas256x, ch,
			TAS256X_BOP_TH,
			(char *)&(bop_thd[value][0]), sizeof(int));
		if (n_result == 0)
			p_tas256x->devs[ch-1]->bop_thd = value;
	}
	return n_result;
}

int tas256x_update_bosd_thr(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 16)) {
		n_result = p_tas256x->bulk_write(p_tas256x, ch,
			TAS256X_BOSD_TH,
			(char *)&(bsd_thd[value][0]), sizeof(int));
		if (n_result == 0)
			p_tas256x->devs[ch-1]->bosd_thd = value;
	}

	return n_result;
}

int tas256x_update_boost_voltage(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if (p_tas256x->devs[ch-1]->device_id == DEVICE_TAS2562) {
		if ((value >= 0) && (value < 16))
			n_result = p_tas256x->update_bits(p_tas256x,
				ch, TAS2562_BOOSTCONFIGURATION2,
				TAS2562_BOOSTCONFIGURATION2_BOOSTMAXVOLTAGE_MASK,
				(value+1) << TAS2562_BOOSTCONFIGURATION2_BOOSTMAXVOLTAGE_SHIFT);
	} else if (p_tas256x->devs[ch-1]->device_id == DEVICE_TAS2564) {
		if ((value >= 0) && (value < 32))
			n_result = p_tas256x->update_bits(p_tas256x,
				ch, TAS2564_BOOSTCONFIGURATION2,
				TAS2564_BOOSTCONFIGURATION2_BOOSTMAXVOLTAGE_MASK,
				(value+7) << TAS2562_BOOSTCONFIGURATION2_BOOSTMAXVOLTAGE_SHIFT);
	}

	if (n_result == 0)
		p_tas256x->devs[ch-1]->bst_vltg = value;

	return n_result;
}

int tas256x_update_current_limit(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 64))
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_BOOSTCONFIGURATION4,
			TAS256X_BOOSTCONFIGURATION4_BOOSTCURRENTLIMIT_MASK,
			(value) << TAS256X_BOOSTCONFIGURATION4_BOOSTCURRENTLIMIT_SHIFT);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->bst_ilm = value;

	return n_result;
}

int tas256x_update_limiter_enable(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 2))
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_LIMITERCONFIGURATION0,
			TAS256X_LIMITER_ENABLE_MASK,
			(value) << TAS256X_LIMITER_ENABLE_SHIFT);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->lim_switch = value;

	return n_result;
}

int tas256x_update_limiter_attack_rate(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 8))
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_LIMITERCONFIGURATION0,
			TAS256X_LIMITER_ATTACKRATE_MASK,
			(value) << TAS256X_LIMITER_ATTACKRATE_SHIFT);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->lim_att_rate = value;

	return n_result;
}

int tas256x_update_limiter_attack_step_size(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 4))
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_LIMITERCONFIGURATION0,
			TAS256X_LIMITER_ATTACKSTEPSIZE_MASK,
			(value) << TAS256X_LIMITER_ATTACKSTEPSIZE_SHIFT);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->lim_att_stp_size = value;

	return n_result;
}

int tas256x_update_limiter_release_rate(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 8))
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_LIMITERCONFIGURATION1,
			TAS256X_LIMITER_RELEASERATE_MASK,
			(value) << TAS256X_LIMITER_RELEASERATE_SHIFT);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->lim_rel_rate = value;

	return n_result;
}

int tas256x_update_limiter_release_step_size(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 4))
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_LIMITERCONFIGURATION1,
			TAS256X_LIMITER_RELEASESTEPSIZE_MASK,
			(value) << TAS256X_LIMITER_RELEASESTEPSIZE_SHIFT);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->lim_rel_stp_size = value;

	return n_result;
}

int tas256x_update_bop_enable(struct tas256x_priv *p_tas256x, int value,
	int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 2))
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_BOPCONFIGURATION0,
			TAS256X_BOP_ENABLE_MASK,
			(value) << TAS256X_BOP_ENABLE_SHIFT);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->bop_enable = value;

	return n_result;
}

int tas256x_update_bop_mute(struct tas256x_priv *p_tas256x, int value,
	int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 2))
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_BOPCONFIGURATION0,
			TAS256X_BOP_MUTE_MASK,
			(value) << TAS256X_BOP_MUTE_SHIFT);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->bop_mute = value;

	return n_result;
}

int tas256x_update_bop_shutdown_enable(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 2))
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_BOPCONFIGURATION0,
			TAS256X_BOP_SHUTDOWN_ENABLE_MASK,
			(value) << TAS256X_BOP_SHUTDOWN_ENABLE_SHIFT);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->bosd_enable = value;

	return n_result;
}

int tas256x_update_bop_attack_rate(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 8))
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_BOPCONFIGURATION1,
			TAS256X_BOP_ATTACKRATE_MASK,
			(value) << TAS256X_BOP_ATTACKRATE_SHIFT);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->bop_att_rate = value;

	return n_result;
}

int tas256x_update_bop_attack_step_size(struct tas256x_priv *p_tas256x,
	int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 4))
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_BOPCONFIGURATION1,
			TAS256X_BOP_ATTACKSTEPSIZE_MASK,
			(value) << TAS256X_BOP_ATTACKSTEPSIZE_SHIFT);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->bop_att_stp_size = value;

	return n_result;
}

int tas256x_update_bop_hold_time(struct tas256x_priv *p_tas256x, int value,
	int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 8))
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_BOPCONFIGURATION1,
			TAS256X_BOP_HOLDTIME_MASK,
			(value) << TAS256X_BOP_HOLDTIME_SHIFT);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->bop_hld_time = value;

	return n_result;
}

int tas256x_update_vbat_lpf(struct tas256x_priv *p_tas256x, int value, int ch)
{
	int n_result = -1;

	if ((value >= 0) && (value < 4)) {
		n_result = p_tas256x->update_bits(p_tas256x,
			ch, TAS256X_VBATFILTER,
			TAS256X_VBAT_LPF_MASK,
			(value) << TAS256X_VBAT_LPF_SHIFT);
		if (n_result == 0)
			p_tas256x->devs[ch-1]->vbat_lpf = value;
	}

	return n_result;
}

int tas256x_update_rx_cfg(struct tas256x_priv *p_tas256x, int value, int ch)
{
	int n_result = -1;
	int data = -1;

	switch (value) {
	case 0:
		data = TAS256X_TDMCONFIGURATIONREG2_RXSCFG54_MONO_I2C;
		break;
	case 1:
		data = TAS256X_TDMCONFIGURATIONREG2_RXSCFG54_MONO_LEFT;
		break;
	case 2:
		data = TAS256X_TDMCONFIGURATIONREG2_RXSCFG54_MONO_RIGHT;
		break;
	case 3:
		data = TAS256X_TDMCONFIGURATIONREG2_RXSCFG54_STEREO_DOWNMIX;
		break;
	}

	if (data >= 0)
		n_result = p_tas256x->update_bits(p_tas256x, ch,
			TAS256X_TDMCONFIGURATIONREG2,
			TAS256X_TDMCONFIGURATIONREG2_RXSCFG54_MASK,
			data);

	if (n_result == 0)
		p_tas256x->devs[ch-1]->rx_cfg = value;

	return n_result;
}
