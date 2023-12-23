// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (C) 2019-2021 The Linux Foundation. All rights reserved.
//               2022      The LineageOS Project
//

#define pr_fmt(fmt) "BATTERY_CHG: %s: " fmt, __func__
#include <linux/power_supply.h>
#include <linux/soc/qcom/pmic_glink.h>
#include <linux/soc/qcom/battery_charger.h>

#include "qti_battery_charger.h"

extern int battery_chg_write(struct battery_chg_dev *bcdev, void *data,
			     int len);
extern int write_property_id(struct battery_chg_dev *bcdev,
			     struct psy_state *pst, u32 prop_id, u32 val);
extern int read_property_id(struct battery_chg_dev *bcdev,
			    struct psy_state *pst, u32 prop_id);
extern int usb_psy_get_prop(struct power_supply *psy,
			    enum power_supply_property prop,
			    union power_supply_propval *pval);

extern const char *const power_supply_usb_type_text[];

static const char *const power_supply_usbc_text[] = {
	"Nothing attached",
	"Source attached (default current)",
	"Source attached (medium current)",
	"Source attached (high current)",
	"Non compliant",
	"Sink attached",
	"Powered cable w/ sink",
	"Debug Accessory",
	"Audio Adapter",
	"Powered cable w/o sink",
};

#define BSWAP_32(x)                                                            \
	(u32)((((u32)(x)&0xff000000) >> 24) | (((u32)(x)&0x00ff0000) >> 8) |   \
	      (((u32)(x)&0x0000ff00) << 8) | (((u32)(x)&0x000000ff) << 24))

int StringToHex(char *str, unsigned char *out, unsigned int *outlen)
{
	char *p = str;
	char high = 0, low = 0;
	int tmplen = strlen(p), cnt = 0;
	tmplen = strlen(p);
	while (cnt < (tmplen / 2)) {
		high = ((*p > '9') && ((*p <= 'F') || (*p <= 'f'))) ?
			       *p - 48 - 7 :
			       *p - 48;
		low = (*(++p) > '9' && ((*p <= 'F') || (*p <= 'f'))) ?
			      *(p)-48 - 7 :
			      *(p)-48;
		out[cnt] = ((high & 0x0f) << 4 | (low & 0x0f));
		p++;
		cnt++;
	}
	if (tmplen % 2 != 0)
		out[cnt] = ((*p > '9') && ((*p <= 'F') || (*p <= 'f'))) ?
				   *p - 48 - 7 :
				   *p - 48;

	if (outlen != NULL)
		*outlen = tmplen / 2 + tmplen % 2;

	return tmplen / 2 + tmplen % 2;
}

static int write_ss_auth_prop_id(struct battery_chg_dev *bcdev,
				 struct psy_state *pst, u32 prop_id, u32 *buff)
{
	struct xm_ss_auth_resp_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_set;
	memcpy(req_msg.data, buff, BATTERY_SS_AUTH_DATA_LEN * sizeof(u32));

	pr_debug(
		"psy: prop_id:%d size:%d data[0]:0x%x data[1]:0x%x data[2]:0x%x data[3]:0x%x\n",
		req_msg.property_id, sizeof(req_msg), req_msg.data[0],
		req_msg.data[1], req_msg.data[2], req_msg.data[3]);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int read_ss_auth_property_id(struct battery_chg_dev *bcdev,
				    struct psy_state *pst, u32 prop_id)
{
	struct xm_ss_auth_resp_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_get;

	pr_debug("psy: %s prop_id: %u\n", pst->psy->desc->name,
		 req_msg.property_id);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int write_verify_digest_prop_id(struct battery_chg_dev *bcdev,
				       struct psy_state *pst, u32 prop_id,
				       u8 *buff)
{
	struct xm_verify_digest_resp_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_set;
	req_msg.slave_fg = bcdev->slave_fg_verify_flag;
	memcpy(req_msg.digest, buff, BATTERY_DIGEST_LEN);

	pr_debug("psy: prop_id:%d size:%d\n", req_msg.property_id,
		 sizeof(req_msg));

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int read_verify_digest_property_id(struct battery_chg_dev *bcdev,
					  struct psy_state *pst, u32 prop_id)
{
	struct xm_verify_digest_resp_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_get;
	req_msg.slave_fg = bcdev->slave_fg_verify_flag;

	pr_debug("psy: %s prop_id: %u\n", pst->psy->desc->name,
		 req_msg.property_id);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

#if defined(CONFIG_MI_WIRELESS)
static int write_wls_bin_prop_id(struct battery_chg_dev *bcdev,
				 struct psy_state *pst, u32 prop_id,
				 u16 total_length, u8 serial_number, u8 fw_area,
				 u8 *buff)
{
	struct xm_set_wls_bin_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_set;
	req_msg.total_length = total_length;
	req_msg.serial_number = serial_number;
	req_msg.fw_area = fw_area;
	if (serial_number < total_length / MAX_STR_LEN)
		memcpy(req_msg.wls_fw_bin, buff, MAX_STR_LEN);
	else if (serial_number == total_length / MAX_STR_LEN)
		memcpy(req_msg.wls_fw_bin, buff,
		       total_length - serial_number * MAX_STR_LEN);

	pr_debug("psy: prop_id:%d size:%d\n", req_msg.property_id,
		 sizeof(req_msg));

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int show_wls_fw_property_id(struct battery_chg_dev *bcdev,
				   struct psy_state *pst, u32 prop_id)
{
	struct wls_fw_resp_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_get;

	pr_debug("psy: %s prop_id: %u\n", pst->psy->desc->name,
		 req_msg.property_id);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int update_wls_fw_version(struct battery_chg_dev *bcdev,
				 struct psy_state *pst, u32 prop_id, u32 val)
{
	struct wls_fw_resp_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.value = val;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_set;

	pr_debug("psy: %s prop_id: %u val: %u\n", pst->psy->desc->name,
		 req_msg.property_id, val);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}
#endif

static ssize_t wireless_register_store(struct class *c,
				       struct class_attribute *attr,
				       const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	u32 val = 0;
	u8 reg_val;
	u16 reg_addr;
	ssize_t len;
	char *p, *token;
	char kbuf[50];
	int rc;

	pr_info("wireless_register_store: buf is %s\n", buf);

	len = min(count, sizeof(kbuf) - 1);
	memcpy(kbuf, buf, len);

	kbuf[len] = '\0';
	p = kbuf;

	token = strsep(&p, " ");
	if (!token)
		return -EINVAL;

	if (kstrtou16(token, 0, &reg_addr) || !reg_addr)
		return -EINVAL;

	if (kstrtou8(p, 0, &reg_val))
		return -EINVAL;

	val = reg_val | (reg_addr << 8);

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_WLS],
			       WLS_REGISTER, val);
	if (rc < 0)
		return rc;

	return count;
}
static CLASS_ATTR_WO(wireless_register);

static ssize_t wireless_input_curr_store(struct class *c,
					 struct class_attribute *attr,
					 const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	u32 val;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_WLS],
			       WLS_INPUT_CURR, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t wireless_input_curr_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int rc;

	rc = read_property_id(bcdev, pst, WLS_INPUT_CURR);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[WLS_INPUT_CURR]);
}
static CLASS_ATTR_RW(wireless_input_curr);

#if defined(CONFIG_MI_WIRELESS)
static ssize_t wireless_chip_fw_store(struct class *c,
				      struct class_attribute *attr,
				      const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int rc;
	u32 val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	rc = update_wls_fw_version(bcdev, pst, WLS_FW_VER, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t wireless_chip_fw_show(struct class *c,
				     struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int rc;

	rc = show_wls_fw_property_id(bcdev, pst, WLS_FW_VER);
	//rc = read_property_id(bcdev, pst, WLS_FW_VER);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%s\n", pst->version);
}
static CLASS_ATTR_RW(wireless_chip_fw);
#endif

static ssize_t real_type_show(struct class *c, struct class_attribute *attr,
			      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_REAL_TYPE);
	if (rc < 0)
		return rc;

	/* sanity check to avoid real_type value above maxium 13(USB_FLOAT) to cause kernel crash */
	if (pst->prop[XM_PROP_REAL_TYPE] > 13)
		pst->prop[XM_PROP_REAL_TYPE] = 0;

	return scnprintf(
		buf, PAGE_SIZE, "%s\n",
		power_supply_usb_type_text[pst->prop[XM_PROP_REAL_TYPE]]);
}
static CLASS_ATTR_RO(real_type);

static ssize_t resistance_id_show(struct class *c, struct class_attribute *attr,
				  char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_RESISTANCE_ID);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_RESISTANCE_ID]);
}
static CLASS_ATTR_RO(resistance_id);

static ssize_t verify_digest_store(struct class *c,
				   struct class_attribute *attr,
				   const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	u8 random[BATTERY_DIGEST_LEN] = { 0 };
	char kbuf[2 * BATTERY_DIGEST_LEN + 1] = { 0 };
	u8 random_1s[BATTERY_DIGEST_LEN] = { 0 };
	char kbuf_1s[70] = { 0 };
	int rc;
	int i;

	if (bcdev->support_2s_charging) {
		memset(kbuf, 0, sizeof(kbuf));
		strlcpy(kbuf, buf, 2 * BATTERY_DIGEST_LEN + 1);
		StringToHex(kbuf, random, &i);
		pr_err("verify_digest_store  2s:%s \n", random);
		rc = write_verify_digest_prop_id(bcdev,
						 &bcdev->psy_list[PSY_TYPE_XM],
						 XM_PROP_VERIFY_DIGEST, random);
	} else {
		memset(kbuf_1s, 0, sizeof(kbuf_1s));
		strncpy(kbuf_1s, buf, count - 1);
		StringToHex(kbuf_1s, random_1s, &i);
		pr_err("verify_digest_store  1s:%s \n", random_1s);
		rc = write_verify_digest_prop_id(bcdev,
						 &bcdev->psy_list[PSY_TYPE_XM],
						 XM_PROP_VERIFY_DIGEST,
						 random_1s);
	}
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t verify_digest_show(struct class *c, struct class_attribute *attr,
				  char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;
	u8 digest_buf[4];
	int i;
	int len;

	rc = read_verify_digest_property_id(bcdev, pst, XM_PROP_VERIFY_DIGEST);
	if (rc < 0)
		return rc;

	for (i = 0; i < BATTERY_DIGEST_LEN; i++) {
		memset(digest_buf, 0, sizeof(digest_buf));
		snprintf(digest_buf, sizeof(digest_buf) - 1, "%02x",
			 bcdev->digest[i]);
		strlcat(buf, digest_buf, BATTERY_DIGEST_LEN * 2 + 1);
	}
	len = strlen(buf);
	buf[len] = '\0';
	pr_err("verify_digest_show :%s \n", buf);
	return strlen(buf) + 1;
}
static CLASS_ATTR_RW(verify_digest);

static ssize_t verify_slave_flag_store(struct class *c,
				       struct class_attribute *attr,
				       const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	bcdev->slave_fg_verify_flag = val;
	pr_err("verify_digest_flag :%d \n", val);

	return count;
}

static ssize_t verify_slave_flag_show(struct class *c,
				      struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);

	return scnprintf(buf, PAGE_SIZE, "%u\n", bcdev->slave_fg_verify_flag);
}
static CLASS_ATTR_RW(verify_slave_flag);

#if defined(CONFIG_MI_WIRELESS)
static ssize_t wls_bin_store(struct class *c, struct class_attribute *attr,
			     const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc, retry, tmp_serial;
	static u16 total_length = 0;
	static u8 serial_number = 0;
	static u8 fw_area = 0;
	int i;

	pr_err("buf:%s, count:%d\n", buf, count);
	if (strncmp("length:", buf, 7) == 0) {
		if (kstrtou16(buf + 7, 10, &total_length))
			return -EINVAL;
		serial_number = 0;
		pr_err("total_length:%d, serial_number:%d\n", total_length,
		       serial_number);
	} else if (strncmp("area:", buf, 5) == 0) {
		if (kstrtou8(buf + 5, 10, &fw_area))
			return -EINVAL;
		pr_err("area:%d\n", fw_area);
	} else {
		for (i = 0; i < count; ++i)
			pr_err("wls_bin_data[%d]=%x\n",
			       serial_number * MAX_STR_LEN + i, buf[i]);

		for (tmp_serial = 0;
		     (tmp_serial < (count + MAX_STR_LEN - 1) / MAX_STR_LEN) &&
		     (serial_number <
		      (total_length + MAX_STR_LEN - 1) / MAX_STR_LEN);
		     ++tmp_serial, ++serial_number) {
			for (retry = 0; retry < 3; ++retry) {
				rc = write_wls_bin_prop_id(
					bcdev, &bcdev->psy_list[PSY_TYPE_XM],
					XM_PROP_WLS_BIN, total_length,
					serial_number, fw_area,
					(u8 *)buf + tmp_serial * MAX_STR_LEN);
				pr_err("total_length:%d, serial_number:%d, retry:%d\n",
				       total_length, serial_number, retry);
				if (rc == 0)
					break;
			}
		}
	}
	return count;
}
static CLASS_ATTR_WO(wls_bin);
#endif

static ssize_t connector_temp_store(struct class *c,
				    struct class_attribute *attr,
				    const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_CONNECTOR_TEMP, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t connector_temp_show(struct class *c,
				   struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_CONNECTOR_TEMP);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_CONNECTOR_TEMP]);
}
static CLASS_ATTR_RW(connector_temp);

static ssize_t authentic_store(struct class *c, struct class_attribute *attr,
			       const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	pr_err("authentic_store: %d\n", val);
	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_AUTHENTIC, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t authentic_show(struct class *c, struct class_attribute *attr,
			      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_AUTHENTIC);
	if (rc < 0)
		return rc;

	pr_err("authentic_show: %d\n", pst->prop[XM_PROP_AUTHENTIC]);
	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_AUTHENTIC]);
}
static CLASS_ATTR_RW(authentic);

static ssize_t chip_ok_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_CHIP_OK);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_CHIP_OK]);
}
static CLASS_ATTR_RO(chip_ok);

#if defined(CONFIG_DUAL_FUEL_GAUGE)
static ssize_t slave_chip_ok_show(struct class *c, struct class_attribute *attr,
				  char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SLAVE_CHIP_OK);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_SLAVE_CHIP_OK]);
}
static CLASS_ATTR_RO(slave_chip_ok);

static ssize_t slave_authentic_store(struct class *c,
				     struct class_attribute *attr,
				     const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	pr_err("slave_authentic_store: %d\n", val);

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_SLAVE_AUTHENTIC, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t slave_authentic_show(struct class *c,
				    struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SLAVE_AUTHENTIC);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_SLAVE_AUTHENTIC]);
}
static CLASS_ATTR_RW(slave_authentic);

static ssize_t fg1_vol_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_VOL);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_FG1_VOL]);
}
static CLASS_ATTR_RO(fg1_vol);

static ssize_t fg1_soc_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_SOC);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_FG1_SOC]);
}
static CLASS_ATTR_RO(fg1_soc);

static ssize_t fg1_temp_show(struct class *c, struct class_attribute *attr,
			     char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_TEMP);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_TEMP]);
}
static CLASS_ATTR_RO(fg1_temp);

static ssize_t fg1_ibatt_show(struct class *c, struct class_attribute *attr,
			      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_IBATT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_IBATT]);
}
static CLASS_ATTR_RO(fg1_ibatt);

static ssize_t fg2_vol_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_VOL);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_FG2_VOL]);
}
static CLASS_ATTR_RO(fg2_vol);

static ssize_t fg2_soc_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_SOC);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_FG2_SOC]);
}
static CLASS_ATTR_RO(fg2_soc);

static ssize_t fg2_temp_show(struct class *c, struct class_attribute *attr,
			     char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_TEMP);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG2_TEMP]);
}
static CLASS_ATTR_RO(fg2_temp);

static ssize_t fg2_ibatt_show(struct class *c, struct class_attribute *attr,
			      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_IBATT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG2_IBATT]);
}
static CLASS_ATTR_RO(fg2_ibatt);

static ssize_t fg2_qmax_show(struct class *c, struct class_attribute *attr,
			     char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_QMAX);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG2_QMAX]);
}
static CLASS_ATTR_RO(fg2_qmax);

static ssize_t fg2_rm_show(struct class *c, struct class_attribute *attr,
			   char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_RM);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG2_RM]);
}
static CLASS_ATTR_RO(fg2_rm);

static ssize_t fg2_fcc_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_FCC);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG2_FCC]);
}
static CLASS_ATTR_RO(fg2_fcc);

static ssize_t fg2_soh_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_SOH);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG2_SOH]);
}
static CLASS_ATTR_RO(fg2_soh);

static ssize_t fg2_fcc_soh_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_FCC_SOH);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 pst->prop[XM_PROP_FG2_FCC_SOH]);
}
static CLASS_ATTR_RO(fg2_fcc_soh);

static ssize_t fg2_cycle_show(struct class *c, struct class_attribute *attr,
			      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_CYCLE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG2_CYCLE]);
}
static CLASS_ATTR_RO(fg2_cycle);

static ssize_t fg2_fastcharge_show(struct class *c,
				   struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_FAST_CHARGE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 pst->prop[XM_PROP_FG2_FAST_CHARGE]);
}
static CLASS_ATTR_RO(fg2_fastcharge);

static ssize_t fg2_current_max_show(struct class *c,
				    struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_CURRENT_MAX);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 pst->prop[XM_PROP_FG2_CURRENT_MAX]);
}
static CLASS_ATTR_RO(fg2_current_max);

static ssize_t fg2_vol_max_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_VOL_MAX);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 pst->prop[XM_PROP_FG2_VOL_MAX]);
}
static CLASS_ATTR_RO(fg2_vol_max);

static ssize_t fg2_tsim_show(struct class *c, struct class_attribute *attr,
			     char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_TSIM);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG2_TSIM]);
}
static CLASS_ATTR_RO(fg2_tsim);

static ssize_t fg2_tambient_show(struct class *c, struct class_attribute *attr,
				 char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_TAMBIENT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 pst->prop[XM_PROP_FG2_TAMBIENT]);
}
static CLASS_ATTR_RO(fg2_tambient);

static ssize_t fg2_tremq_show(struct class *c, struct class_attribute *attr,
			      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_TREMQ);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG2_TREMQ]);
}
static CLASS_ATTR_RO(fg2_tremq);

static ssize_t fg2_tfullq_show(struct class *c, struct class_attribute *attr,
			       char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG2_TFULLQ);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG2_TFULLQ]);
}
static CLASS_ATTR_RO(fg2_tfullq);

static ssize_t is_old_hw_store(struct class *c, struct class_attribute *attr,
			       const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_IS_OLD_HW, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t is_old_hw_show(struct class *c, struct class_attribute *attr,
			      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_IS_OLD_HW);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_IS_OLD_HW]);
}
static CLASS_ATTR_RW(is_old_hw);
#endif

static ssize_t vbus_disable_store(struct class *c, struct class_attribute *attr,
				  const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_VBUS_DISABLE, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t vbus_disable_show(struct class *c, struct class_attribute *attr,
				 char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_VBUS_DISABLE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_VBUS_DISABLE]);
}
static CLASS_ATTR_RW(vbus_disable);

static ssize_t cc_orientation_show(struct class *c,
				   struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_CC_ORIENTATION);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_CC_ORIENTATION]);
}
static CLASS_ATTR_RO(cc_orientation);

static ssize_t slave_batt_present_show(struct class *c,
				       struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SLAVE_BATT_PRESENT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_SLAVE_BATT_PRESENT]);
}
static CLASS_ATTR_RO(slave_batt_present);

#if defined(CONFIG_BQ2597X)
static ssize_t bq2597x_chip_ok_show(struct class *c,
				    struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_BQ2597X_CHIP_OK);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_BQ2597X_CHIP_OK]);
}
static CLASS_ATTR_RO(bq2597x_chip_ok);

static ssize_t bq2597x_slave_chip_ok_show(struct class *c,
					  struct class_attribute *attr,
					  char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_BQ2597X_SLAVE_CHIP_OK);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_BQ2597X_SLAVE_CHIP_OK]);
}
static CLASS_ATTR_RO(bq2597x_slave_chip_ok);

static ssize_t bq2597x_bus_current_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_BQ2597X_BUS_CURRENT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_BQ2597X_BUS_CURRENT]);
}
static CLASS_ATTR_RO(bq2597x_bus_current);

static ssize_t bq2597x_slave_bus_current_show(struct class *c,
					      struct class_attribute *attr,
					      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_BQ2597X_SLAVE_BUS_CURRENT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_BQ2597X_SLAVE_BUS_CURRENT]);
}
static CLASS_ATTR_RO(bq2597x_slave_bus_current);

static ssize_t bq2597x_bus_delta_show(struct class *c,
				      struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_BQ2597X_BUS_DELTA);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_BQ2597X_BUS_DELTA]);
}
static CLASS_ATTR_RO(bq2597x_bus_delta);

static ssize_t bq2597x_bus_voltage_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_BQ2597X_BUS_VOLTAGE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_BQ2597X_BUS_VOLTAGE]);
}
static CLASS_ATTR_RO(bq2597x_bus_voltage);

static ssize_t bq2597x_battery_present_show(struct class *c,
					    struct class_attribute *attr,
					    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_BQ2597X_BATTERY_PRESENT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_BQ2597X_BATTERY_PRESENT]);
}
static CLASS_ATTR_RO(bq2597x_battery_present);

static ssize_t bq2597x_slave_battery_present_show(struct class *c,
						  struct class_attribute *attr,
						  char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst,
			      XM_PROP_BQ2597X_SLAVE_BATTERY_PRESENT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_BQ2597X_SLAVE_BATTERY_PRESENT]);
}
static CLASS_ATTR_RO(bq2597x_slave_battery_present);
static ssize_t bq2597x_battery_voltage_show(struct class *c,
					    struct class_attribute *attr,
					    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_BQ2597X_BATTERY_VOLTAGE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_BQ2597X_BATTERY_VOLTAGE]);
}
static CLASS_ATTR_RO(bq2597x_battery_voltage);

static ssize_t cool_mode_store(struct class *c, struct class_attribute *attr,
			       const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_COOL_MODE, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t cool_mode_show(struct class *c, struct class_attribute *attr,
			      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_COOL_MODE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_COOL_MODE]);
}
static CLASS_ATTR_RW(cool_mode);
#endif

#if defined(CONFIG_REDWOOD_FOR_BUILD)
static ssize_t bq2597x_slave_connector_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_BQ2597X_SLAVE_CONNECTOR);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_BQ2597X_SLAVE_CONNECTOR]);
}
static CLASS_ATTR_RO(bq2597x_slave_connector);
#endif

#if !defined(CONFIG_VENUS_FOR_BUILD)
static ssize_t bt_transfer_start_store(struct class *c,
				       struct class_attribute *attr,
				       const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	pr_err("transfer_start_store %d build: 0x%x\n", val,
	       bcdev->hw_version_build);
	switch (bcdev->hw_version_build) {
	case 0x110001:
	case 0x10001:
	case 0x120000:
	case 0x20000:
	case 0x120005:
	case 0x120001:
	case 0x20001:
	case 0x90002:
		rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
				       XM_PROP_BT_TRANSFER_START, val);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t bt_transfer_start_show(struct class *c,
				      struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_BT_TRANSFER_START);
	if (rc < 0)
		return rc;
	pr_err("transfer_start_show %d\n",
	       pst->prop[XM_PROP_BT_TRANSFER_START]);
	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_BT_TRANSFER_START]);
}
static CLASS_ATTR_RW(bt_transfer_start);
#endif

static ssize_t master_smb1396_online_show(struct class *c,
					  struct class_attribute *attr,
					  char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_MASTER_SMB1396_ONLINE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_MASTER_SMB1396_ONLINE]);
}
static CLASS_ATTR_RO(master_smb1396_online);

static ssize_t master_smb1396_iin_show(struct class *c,
				       struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_MASTER_SMB1396_IIN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_MASTER_SMB1396_IIN]);
}
static CLASS_ATTR_RO(master_smb1396_iin);

static ssize_t slave_smb1396_online_show(struct class *c,
					 struct class_attribute *attr,
					 char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SLAVE_SMB1396_ONLINE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_SLAVE_SMB1396_ONLINE]);
}
static CLASS_ATTR_RO(slave_smb1396_online);

static ssize_t slave_smb1396_iin_show(struct class *c,
				      struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SLAVE_SMB1396_IIN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_SLAVE_SMB1396_IIN]);
}
static CLASS_ATTR_RO(slave_smb1396_iin);

static ssize_t smb_iin_diff_show(struct class *c, struct class_attribute *attr,
				 char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SMB_IIN_DIFF);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_SMB_IIN_DIFF]);
}
static CLASS_ATTR_RO(smb_iin_diff);

static ssize_t soc_decimal_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SOC_DECIMAL);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u", pst->prop[XM_PROP_SOC_DECIMAL]);
}
static CLASS_ATTR_RO(soc_decimal);

static ssize_t soc_decimal_rate_show(struct class *c,
				     struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SOC_DECIMAL_RATE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u",
			 pst->prop[XM_PROP_SOC_DECIMAL_RATE]);
}
static CLASS_ATTR_RO(soc_decimal_rate);

static ssize_t shutdown_delay_show(struct class *c,
				   struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SHUTDOWN_DELAY);
	if (rc < 0)
		return rc;

	if (!bcdev->shutdown_delay_en)
		pst->prop[XM_PROP_SHUTDOWN_DELAY] = 0;

	return scnprintf(buf, PAGE_SIZE, "%u",
			 pst->prop[XM_PROP_SHUTDOWN_DELAY]);
}

static ssize_t shutdown_delay_store(struct class *c,
				    struct class_attribute *attr,
				    const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	bcdev->shutdown_delay_en = val;
	pr_err("use contral shutdown delay featue enable= %d\n",
	       bcdev->shutdown_delay_en);

	return count;
}

static CLASS_ATTR_RW(shutdown_delay);

#if defined(CONFIG_MI_WIRELESS)
static ssize_t tx_mac_show(struct class *c, struct class_attribute *attr,
			   char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;
	u64 value = 0;

	rc = read_property_id(bcdev, pst, XM_PROP_TX_MACL);
	if (rc < 0)
		return rc;

	rc = read_property_id(bcdev, pst, XM_PROP_TX_MACH);
	if (rc < 0)
		return rc;
	value = pst->prop[XM_PROP_TX_MACH];
	value = (value << 32) + pst->prop[XM_PROP_TX_MACL];

	return scnprintf(buf, PAGE_SIZE, "%llx", value);
}
static CLASS_ATTR_RO(tx_mac);

static ssize_t rx_cr_show(struct class *c, struct class_attribute *attr,
			  char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;
	u64 value = 0;
	rc = read_property_id(bcdev, pst, XM_PROP_RX_CRL);
	if (rc < 0)
		return rc;

	rc = read_property_id(bcdev, pst, XM_PROP_RX_CRH);
	if (rc < 0)
		return rc;
	value = pst->prop[XM_PROP_RX_CRH];
	value = (value << 32) + pst->prop[XM_PROP_RX_CRL];

	return scnprintf(buf, PAGE_SIZE, "%llx", value);
}
static CLASS_ATTR_RO(rx_cr);

static ssize_t rx_cep_show(struct class *c, struct class_attribute *attr,
			   char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_RX_CEP);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%x", pst->prop[XM_PROP_RX_CEP]);
}
static CLASS_ATTR_RO(rx_cep);

static ssize_t bt_state_store(struct class *c, struct class_attribute *attr,
			      const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_BT_STATE, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t bt_state_show(struct class *c, struct class_attribute *attr,
			     char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_BT_STATE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u", pst->prop[XM_PROP_BT_STATE]);
}
static CLASS_ATTR_RW(bt_state);
#endif

static ssize_t voter_debug_store(struct class *c, struct class_attribute *attr,
				 const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_VOTER_DEBUG, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t voter_debug_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_VOTER_DEBUG);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u", pst->prop[XM_PROP_VOTER_DEBUG]);
}
static CLASS_ATTR_RW(voter_debug);

static ssize_t wlscharge_control_limit_store(struct class *c,
					     struct class_attribute *attr,
					     const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;

	if (!bcdev->support_wireless_charge)
		return -EINVAL;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	if (val == bcdev->curr_wlsthermal_level)
		return count;

	pr_err("set thermal-level: %d num_thermal_levels: %d \n", val,
	       bcdev->num_thermal_levels);

	if (bcdev->num_thermal_levels <= 0) {
		pr_err("Incorrect num_thermal_levels\n");
		return -EINVAL;
	}

	if (val < 0 || val >= bcdev->num_thermal_levels)
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_WLSCHARGE_CONTROL_LIMIT, val);
	if (rc < 0)
		return rc;

	bcdev->curr_wlsthermal_level = val;

	return count;
}

static ssize_t wlscharge_control_limit_show(struct class *c,
					    struct class_attribute *attr,
					    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	if (!bcdev->support_wireless_charge)
		return -EINVAL;

	rc = read_property_id(bcdev, pst, XM_PROP_WLSCHARGE_CONTROL_LIMIT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u",
			 pst->prop[XM_PROP_WLSCHARGE_CONTROL_LIMIT]);
}
static CLASS_ATTR_RW(wlscharge_control_limit);

#if defined(CONFIG_MI_WIRELESS)
static ssize_t reverse_chg_mode_store(struct class *c,
				      struct class_attribute *attr,
				      const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_REVERSE_CHG_MODE, val);
	if (rc < 0)
		return rc;
	return count;
}

static ssize_t reverse_chg_mode_show(struct class *c,
				     struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_REVERSE_CHG_MODE);
	if (rc < 0)
		goto out;

	if (bcdev->reverse_chg_flag != pst->prop[XM_PROP_REVERSE_CHG_MODE]) {
		if (pst->prop[XM_PROP_REVERSE_CHG_MODE]) {
			pm_stay_awake(bcdev->dev);
			dev_info(bcdev->dev, "reverse chg add lock\n");
		} else {
			pm_relax(bcdev->dev);
			dev_info(bcdev->dev, "reverse chg release lock\n");
		}
		bcdev->reverse_chg_flag = pst->prop[XM_PROP_REVERSE_CHG_MODE];
	}

	return scnprintf(buf, PAGE_SIZE, "%u",
			 pst->prop[XM_PROP_REVERSE_CHG_MODE]);

out:
	dev_err(bcdev->dev, "read reverse chg mode error\n");
	bcdev->reverse_chg_flag = 0;
	pm_relax(bcdev->dev);
	return rc;
}
static CLASS_ATTR_RW(reverse_chg_mode);

#if !defined(CONFIG_VENUS_FOR_BUILD)
static ssize_t wls_tx_speed_store(struct class *c, struct class_attribute *attr,
				  const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;
	if (kstrtoint(buf, 10, &val))
		return -EINVAL;
	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_WLS_TX_SPEED, val);
	if (rc < 0)
		return rc;
	return count;
}

static ssize_t wls_tx_speed_show(struct class *c, struct class_attribute *attr,
				 char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_WLS_TX_SPEED);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u", pst->prop[XM_PROP_WLS_TX_SPEED]);
}
static CLASS_ATTR_RW(wls_tx_speed);
#endif

static ssize_t reverse_chg_state_show(struct class *c,
				      struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_REVERSE_CHG_STATE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u",
			 pst->prop[XM_PROP_REVERSE_CHG_STATE]);
}
static CLASS_ATTR_RO(reverse_chg_state);
#if 0
static ssize_t wls_fw_state_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_WLS_FW_STATE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u", pst->prop[XM_PROP_WLS_FW_STATE]);
}
static CLASS_ATTR_RO(wls_fw_state);
#endif
static ssize_t wls_car_adapter_show(struct class *c,
				    struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_WLS_CAR_ADAPTER);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u",
			 pst->prop[XM_PROP_WLS_CAR_ADAPTER]);
}
static CLASS_ATTR_RO(wls_car_adapter);

static ssize_t rx_vout_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_RX_VOUT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u", pst->prop[XM_PROP_RX_VOUT]);
}
static CLASS_ATTR_RO(rx_vout);

static ssize_t rx_vrect_show(struct class *c, struct class_attribute *attr,
			     char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_RX_VRECT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u", pst->prop[XM_PROP_RX_VRECT]);
}
static CLASS_ATTR_RO(rx_vrect);

static ssize_t rx_iout_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_RX_IOUT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u", pst->prop[XM_PROP_RX_IOUT]);
}
static CLASS_ATTR_RO(rx_iout);

static ssize_t tx_adapter_show(struct class *c, struct class_attribute *attr,
			       char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_TX_ADAPTER);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u", pst->prop[XM_PROP_TX_ADAPTER]);
}
static CLASS_ATTR_RO(tx_adapter);

static ssize_t op_mode_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_OP_MODE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u", pst->prop[XM_PROP_OP_MODE]);
}
static CLASS_ATTR_RO(op_mode);

static ssize_t wls_die_temp_show(struct class *c, struct class_attribute *attr,
				 char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_WLS_DIE_TEMP);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u", pst->prop[XM_PROP_WLS_DIE_TEMP]);
}
static CLASS_ATTR_RO(wls_die_temp);
#endif

static ssize_t power_max_show(struct class *c, struct class_attribute *attr,
			      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *xm_pst = &bcdev->psy_list[PSY_TYPE_XM];
	union power_supply_propval val = {
		0,
	};
	struct power_supply *usb_psy = NULL;
	int rc, usb_present = 0;
	usb_psy = bcdev->psy_list[PSY_TYPE_USB].psy;
	if (usb_psy != NULL) {
		rc = usb_psy_get_prop(usb_psy, POWER_SUPPLY_PROP_ONLINE, &val);
		if (!rc)
			usb_present = val.intval;
		else
			usb_present = 0;
		pr_err("usb_present: %d\n", usb_present);
	}
	if (usb_present) {
		rc = read_property_id(bcdev, xm_pst, XM_PROP_APDO_MAX);
		if (rc < 0)
			return rc;
		return scnprintf(buf, PAGE_SIZE, "%u",
				 xm_pst->prop[XM_PROP_APDO_MAX]);
	}
	pr_err("tx_adapter:%d\n", xm_pst->prop[XM_PROP_TX_ADAPTER]);
#if defined(CONFIG_MI_WIRELESS)
	switch (xm_pst->prop[XM_PROP_TX_ADAPTER]) {
	case ADAPTER_XIAOMI_PD_50W:
		return scnprintf(buf, PAGE_SIZE, "%u", 50);
	case ADAPTER_XIAOMI_PD_60W:
	case ADAPTER_XIAOMI_PD_100W:
		return scnprintf(buf, PAGE_SIZE, "%u", 67);
	case ADAPTER_XIAOMI_PD_30W:
	case ADAPTER_VOICE_BOX_30W:
		return scnprintf(buf, PAGE_SIZE, "%u", 30);
	case ADAPTER_XIAOMI_QC3_20W:
	case ADAPTER_XIAOMI_PD_20W:
	case ADAPTER_XIAOMI_CAR_20W:
		return scnprintf(buf, PAGE_SIZE, "%u", 20);
	default:
		return scnprintf(buf, PAGE_SIZE, "%u", 0);
	}
#endif
	return scnprintf(buf, PAGE_SIZE, "%u", 0);
}
static CLASS_ATTR_RO(power_max);

static ssize_t input_suspend_store(struct class *c,
				   struct class_attribute *attr,
				   const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	pr_err("set charger input suspend %d\n", val);

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_INPUT_SUSPEND, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t input_suspend_show(struct class *c, struct class_attribute *attr,
				  char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_INPUT_SUSPEND);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_INPUT_SUSPEND]);
}
static CLASS_ATTR_RW(input_suspend);

static ssize_t night_charging_store(struct class *c,
				    struct class_attribute *attr,
				    const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	pr_err("set charger night charging %d\n", val);

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_NIGHT_CHARGING, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t night_charging_show(struct class *c,
				   struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_NIGHT_CHARGING);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_NIGHT_CHARGING]);
}
static CLASS_ATTR_RW(night_charging);

#if !defined(CONFIG_VENUS_FOR_BUILD)
static ssize_t smart_batt_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	pr_err("set smart batt charging %d\n", val);

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_SMART_BATT, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t smart_batt_show(struct class *c, struct class_attribute *attr,
			       char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SMART_BATT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_SMART_BATT]);
}
static CLASS_ATTR_RW(smart_batt);
#endif

static ssize_t verify_process_store(struct class *c,
				    struct class_attribute *attr,
				    const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_VERIFY_PROCESS, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t verify_process_show(struct class *c,
				   struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_VERIFY_PROCESS);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_VERIFY_PROCESS]);
}
static CLASS_ATTR_RW(verify_process);

static void usbpd_sha256_bitswap32(unsigned int *array, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		array[i] = BSWAP_32(array[i]);
	}
}

static void usbpd_request_vdm_cmd(struct battery_chg_dev *bcdev,
				  enum uvdm_state cmd, unsigned int *data)
{
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	u32 prop_id, val = 0;
	int rc;

	pr_err("usbpd_request_vdm_cmd:cmd = %d, data = %d\n", cmd, *data);
	switch (cmd) {
	case USBPD_UVDM_CHARGER_VERSION:
		prop_id = XM_PROP_VDM_CMD_CHARGER_VERSION;
		break;
	case USBPD_UVDM_CHARGER_VOLTAGE:
		prop_id = XM_PROP_VDM_CMD_CHARGER_VOLTAGE;
		break;
	case USBPD_UVDM_CHARGER_TEMP:
		prop_id = XM_PROP_VDM_CMD_CHARGER_TEMP;
		break;
	case USBPD_UVDM_SESSION_SEED:
		prop_id = XM_PROP_VDM_CMD_SESSION_SEED;
		usbpd_sha256_bitswap32(data, USBPD_UVDM_SS_LEN);
		val = *data;
		pr_err("SESSION_SEED:data = %d\n", val);
		break;
	case USBPD_UVDM_AUTHENTICATION:
		prop_id = XM_PROP_VDM_CMD_AUTHENTICATION;
		usbpd_sha256_bitswap32(data, USBPD_UVDM_SS_LEN);
		val = *data;
		pr_err("AUTHENTICATION:data = %d\n", val);
		break;
#if !defined(CONFIG_VENUS_FOR_BUILD)
	case USBPD_UVDM_REVERSE_AUTHEN:
		prop_id = XM_PROP_VDM_CMD_REVERSE_AUTHEN;
		usbpd_sha256_bitswap32(data, USBPD_UVDM_SS_LEN);
		val = *data;
		pr_err("AUTHENTICATION:data = %d\n", val);
		break;
#endif
	case USBPD_UVDM_REMOVE_COMPENSATION:
		prop_id = XM_PROP_VDM_CMD_REMOVE_COMPENSATION;
		val = *data;
		break;
	case USBPD_UVDM_VERIFIED:
		prop_id = XM_PROP_VDM_CMD_VERIFIED;
		val = *data;
		break;
	default:
		prop_id = XM_PROP_VDM_CMD_CHARGER_VERSION;
		pr_info("cmd:%d is not support\n", cmd);
		break;
	}

	if (cmd == USBPD_UVDM_SESSION_SEED ||
	    cmd == USBPD_UVDM_AUTHENTICATION ||
	    cmd == USBPD_UVDM_REVERSE_AUTHEN) {
		rc = write_ss_auth_prop_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
					   prop_id, data);
	} else
		rc = write_property_id(bcdev, pst, prop_id, val);
}

static ssize_t request_vdm_cmd_store(struct class *c,
				     struct class_attribute *attr,
				     const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int cmd, ret;
	unsigned char buffer[64];
	unsigned char data[32];
	int ccount;

	ret = sscanf(buf, "%d,%s\n", &cmd, buffer);

	pr_info("%s:buf:%s cmd:%d, buffer:%s\n", __func__, buf, cmd, buffer);

	StringToHex(buffer, data, &ccount);
	usbpd_request_vdm_cmd(bcdev, cmd, (unsigned int *)data);
	return count;
}

static ssize_t request_vdm_cmd_show(struct class *c,
				    struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;
	u32 prop_id = 0;
	int i;
	char data[16], str_buf[128] = { 0 };
	enum uvdm_state cmd;

	rc = read_property_id(bcdev, pst, XM_PROP_UVDM_STATE);
	if (rc < 0)
		return rc;

	cmd = pst->prop[XM_PROP_UVDM_STATE];
	pr_info("request_vdm_cmd_show  uvdm_state: %d\n", cmd);

	switch (cmd) {
	case USBPD_UVDM_CHARGER_VERSION:
		prop_id = XM_PROP_VDM_CMD_CHARGER_VERSION;
		rc = read_property_id(bcdev, pst, prop_id);
		return snprintf(buf, PAGE_SIZE, "%d,%d", cmd,
				pst->prop[prop_id]);
		break;
	case USBPD_UVDM_CHARGER_TEMP:
		prop_id = XM_PROP_VDM_CMD_CHARGER_TEMP;
		rc = read_property_id(bcdev, pst, prop_id);
		return snprintf(buf, PAGE_SIZE, "%d,%d", cmd,
				pst->prop[prop_id]);
		break;
	case USBPD_UVDM_CHARGER_VOLTAGE:
		prop_id = XM_PROP_VDM_CMD_CHARGER_VOLTAGE;
		rc = read_property_id(bcdev, pst, prop_id);
		return snprintf(buf, PAGE_SIZE, "%d,%d", cmd,
				pst->prop[prop_id]);
		break;
	case USBPD_UVDM_CONNECT:
	case USBPD_UVDM_DISCONNECT:
	case USBPD_UVDM_SESSION_SEED:
	case USBPD_UVDM_VERIFIED:
	case USBPD_UVDM_REMOVE_COMPENSATION:
	case USBPD_UVDM_REVERSE_AUTHEN:
		return snprintf(buf, PAGE_SIZE, "%d,Null", cmd);
		break;
	case USBPD_UVDM_AUTHENTICATION:
		prop_id = XM_PROP_VDM_CMD_AUTHENTICATION;
		rc = read_ss_auth_property_id(bcdev, pst, prop_id);
		if (rc < 0)
			return rc;
		pr_info("auth:0x%x 0x%x 0x%x 0x%x\n", bcdev->ss_auth_data[0],
			bcdev->ss_auth_data[1], bcdev->ss_auth_data[2],
			bcdev->ss_auth_data[3]);
		for (i = 0; i < USBPD_UVDM_SS_LEN; i++) {
			memset(data, 0, sizeof(data));
			snprintf(data, sizeof(data), "%08lx",
				 bcdev->ss_auth_data[i]);
			strlcat(str_buf, data, sizeof(str_buf));
		}
		return snprintf(buf, PAGE_SIZE, "%d,%s", cmd, str_buf);
		break;
	default:
		pr_info("feedbak cmd:%d is not support\n", cmd);
		break;
	}

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[prop_id]);
}
static CLASS_ATTR_RW(request_vdm_cmd);

static const char *const usbpd_state_strings[] = {
	"UNKNOWN",
	"SNK_Startup",
	"SNK_Ready",
	"SRC_Ready",
};

static ssize_t current_state_show(struct class *c, struct class_attribute *attr,
				  char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_CURRENT_STATE);
	if (rc < 0)
		return rc;
	if (pst->prop[XM_PROP_CURRENT_STATE] == 25)
		return snprintf(buf, PAGE_SIZE, "%s", usbpd_state_strings[1]);
	else if (pst->prop[XM_PROP_CURRENT_STATE] == 31)
		return snprintf(buf, PAGE_SIZE, "%s", usbpd_state_strings[2]);
	else if (pst->prop[XM_PROP_CURRENT_STATE] == 5)
		return snprintf(buf, PAGE_SIZE, "%s", usbpd_state_strings[3]);
	else
		return snprintf(buf, PAGE_SIZE, "%s", usbpd_state_strings[0]);
}
static CLASS_ATTR_RO(current_state);

static ssize_t adapter_id_show(struct class *c, struct class_attribute *attr,
			       char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_ADAPTER_ID);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%08x", pst->prop[XM_PROP_ADAPTER_ID]);
}
static CLASS_ATTR_RO(adapter_id);

static ssize_t adapter_svid_show(struct class *c, struct class_attribute *attr,
				 char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_ADAPTER_SVID);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%04x",
			 pst->prop[XM_PROP_ADAPTER_SVID]);
}
static CLASS_ATTR_RO(adapter_svid);

static ssize_t pd_verifed_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_PD_VERIFED, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t pd_verifed_show(struct class *c, struct class_attribute *attr,
			       char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_PD_VERIFED);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_PD_VERIFED]);
}
static CLASS_ATTR_RW(pd_verifed);

static ssize_t pdo2_show(struct class *c, struct class_attribute *attr,
			 char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_PDO2);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%08x\n", pst->prop[XM_PROP_PDO2]);
}
static CLASS_ATTR_RO(pdo2);

static ssize_t fastchg_mode_show(struct class *c, struct class_attribute *attr,
				 char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FASTCHGMODE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_FASTCHGMODE]);
}
static CLASS_ATTR_RO(fastchg_mode);

static ssize_t apdo_max_show(struct class *c, struct class_attribute *attr,
			     char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_APDO_MAX);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_APDO_MAX]);
}
static CLASS_ATTR_RO(apdo_max);

static ssize_t thermal_remove_store(struct class *c,
				    struct class_attribute *attr,
				    const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_THERMAL_REMOVE, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t thermal_remove_show(struct class *c,
				   struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_THERMAL_REMOVE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_THERMAL_REMOVE]);
}
static CLASS_ATTR_RW(thermal_remove);

static ssize_t fg_rm_show(struct class *c, struct class_attribute *attr,
			  char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG_RM);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_FG_RM]);
}
static CLASS_ATTR_RO(fg_rm);

static ssize_t mtbf_current_store(struct class *c, struct class_attribute *attr,
				  const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_MTBF_CURRENT, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t mtbf_current_show(struct class *c, struct class_attribute *attr,
				 char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_MTBF_CURRENT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 pst->prop[XM_PROP_MTBF_CURRENT]);
}
static CLASS_ATTR_RW(mtbf_current);

static ssize_t fake_temp_store(struct class *c, struct class_attribute *attr,
			       const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
			       XM_PROP_FAKE_TEMP, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t fake_temp_show(struct class *c, struct class_attribute *attr,
			      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FAKE_TEMP);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_FAKE_TEMP]);
}
static CLASS_ATTR_RW(fake_temp);

static ssize_t qbg_vbat_show(struct class *c, struct class_attribute *attr,
			     char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_QBG_VBAT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_QBG_VBAT]);
}
static CLASS_ATTR_RO(qbg_vbat);

static ssize_t vph_pwr_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_QBG_VPH_PWR);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 pst->prop[XM_PROP_QBG_VPH_PWR]);
}
static CLASS_ATTR_RO(vph_pwr);

static ssize_t qbg_temp_show(struct class *c, struct class_attribute *attr,
			     char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_QBG_TEMP);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_QBG_TEMP]);
}
static CLASS_ATTR_RO(qbg_temp);

static ssize_t typec_mode_show(struct class *c, struct class_attribute *attr,
			       char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_TYPEC_MODE);
	if (rc < 0 || pst->prop[XM_PROP_TYPEC_MODE] > 9 ||
	    pst->prop[XM_PROP_TYPEC_MODE] < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			 power_supply_usbc_text[pst->prop[XM_PROP_TYPEC_MODE]]);
}
static CLASS_ATTR_RO(typec_mode);

#if !defined(CONFIG_VENUS_FOR_BUILD)
static ssize_t fg1_qmax_show(struct class *c, struct class_attribute *attr,
			     char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_QMAX);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_QMAX]);
}
static CLASS_ATTR_RO(fg1_qmax);

static ssize_t fg1_rm_show(struct class *c, struct class_attribute *attr,
			   char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_RM);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_RM]);
}
static CLASS_ATTR_RO(fg1_rm);

static ssize_t fg1_fcc_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_FCC);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_FCC]);
}
static CLASS_ATTR_RO(fg1_fcc);

static ssize_t fg1_soh_show(struct class *c, struct class_attribute *attr,
			    char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_SOH);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_SOH]);
}
static CLASS_ATTR_RO(fg1_soh);

static ssize_t fg1_fcc_soh_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_FCC_SOH);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 pst->prop[XM_PROP_FG1_FCC_SOH]);
}
static CLASS_ATTR_RO(fg1_fcc_soh);

static ssize_t fg1_cycle_show(struct class *c, struct class_attribute *attr,
			      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_CYCLE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_CYCLE]);
}
static CLASS_ATTR_RO(fg1_cycle);

static ssize_t fg1_fastcharge_show(struct class *c,
				   struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_FAST_CHARGE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 pst->prop[XM_PROP_FG1_FAST_CHARGE]);
}
static CLASS_ATTR_RO(fg1_fastcharge);

static ssize_t fg1_current_max_show(struct class *c,
				    struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_CURRENT_MAX);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 pst->prop[XM_PROP_FG1_CURRENT_MAX]);
}
static CLASS_ATTR_RO(fg1_current_max);

static ssize_t fg1_vol_max_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_VOL_MAX);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 pst->prop[XM_PROP_FG1_VOL_MAX]);
}
static CLASS_ATTR_RO(fg1_vol_max);

static ssize_t fg1_tsim_show(struct class *c, struct class_attribute *attr,
			     char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_TSIM);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_TSIM]);
}
static CLASS_ATTR_RO(fg1_tsim);

static ssize_t fg1_tambient_show(struct class *c, struct class_attribute *attr,
				 char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_TAMBIENT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 pst->prop[XM_PROP_FG1_TAMBIENT]);
}
static CLASS_ATTR_RO(fg1_tambient);

static ssize_t fg1_tremq_show(struct class *c, struct class_attribute *attr,
			      char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_TREMQ);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_TREMQ]);
}
static CLASS_ATTR_RO(fg1_tremq);

static ssize_t fg1_tfullq_show(struct class *c, struct class_attribute *attr,
			       char *buf)
{
	struct battery_chg_dev *bcdev =
		container_of(c, struct battery_chg_dev, battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_TFULLQ);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_TFULLQ]);
}
static CLASS_ATTR_RO(fg1_tfullq);

#if defined(CONFIG_REDWOOD_FOR_BUILD)
static ssize_t fg1_seal_set_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	pr_err("seal set %d\n", val);

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
				XM_PROP_FG1_SEAL_SET, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t fg1_seal_set_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_SEAL_SET);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_FG1_SEAL_SET]);
}
static CLASS_ATTR_RW(fg1_seal_set);

static ssize_t fg1_seal_state_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_SEAL_STATE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_SEAL_STATE]);
}
static CLASS_ATTR_RO(fg1_seal_state);

static ssize_t fg1_df_check_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_DF_CHECK);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_DF_CHECK]);
}
static CLASS_ATTR_RO(fg1_df_check);

static ssize_t fg1_voltage_max_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_VOLTAGE_MAX);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_VOLTAGE_MAX]);
}
static CLASS_ATTR_RO(fg1_voltage_max);

static ssize_t fg1_charge_current_max_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_Charge_Current_MAX);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_Charge_Current_MAX]);
}
static CLASS_ATTR_RO(fg1_charge_current_max);

static ssize_t fg1_discharge_current_max_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_Discharge_Current_MAX);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_Discharge_Current_MAX]);
}
static CLASS_ATTR_RO(fg1_discharge_current_max);

static ssize_t fg1_temp_max_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_TEMP_MAX);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_TEMP_MAX]);
}
static CLASS_ATTR_RO(fg1_temp_max);

static ssize_t fg1_temp_min_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_TEMP_MIN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_TEMP_MIN]);
}
static CLASS_ATTR_RO(fg1_temp_min);

static ssize_t fg1_time_ht_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_TIME_HT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_TIME_HT]);
}
static CLASS_ATTR_RO(fg1_time_ht);

static ssize_t fg1_time_ot_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_TIME_OT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_TIME_OT]);
}
static CLASS_ATTR_RO(fg1_time_ot);

static ssize_t fg1_time_ut_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_TIME_UT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_TIME_UT]);
}
static CLASS_ATTR_RO(fg1_time_ut);

static ssize_t fg1_time_lt_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_TIME_LT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_TIME_LT]);
}
static CLASS_ATTR_RO(fg1_time_lt);
#endif

#if defined(CONFIG_BQ_CLOUD_AUTHENTICATION)
static ssize_t server_sn_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;
	int test[8] = {0};
	int i = 0;

	for(i = 0; i < 8; i++)
	{
		rc = read_property_id(bcdev, pst, XM_PROP_SERVER_SN);
		if (rc < 0)
			return rc;
		test[i] = pst->prop[XM_PROP_SERVER_SN];
	}
	return scnprintf(buf, PAGE_SIZE, "0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x 0x%0x\n", 
		(test[0]>>24)&0xff, (test[0]>>16)&0xff, (test[0]>>8)&0xff, (test[0]>>0)&0xff,
		(test[1]>>24)&0xff, (test[1]>>16)&0xff, (test[1]>>8)&0xff, (test[1]>>0)&0xff,
		(test[2]>>24)&0xff, (test[2]>>16)&0xff, (test[2]>>8)&0xff, (test[2]>>0)&0xff,
		(test[3]>>24)&0xff, (test[3]>>16)&0xff, (test[3]>>8)&0xff, (test[3]>>0)&0xff,
		(test[4]>>24)&0xff, (test[4]>>16)&0xff, (test[4]>>8)&0xff, (test[4]>>0)&0xff,
		(test[5]>>24)&0xff, (test[5]>>16)&0xff, (test[5]>>8)&0xff, (test[5]>>0)&0xff,
		(test[6]>>24)&0xff, (test[6]>>16)&0xff, (test[6]>>8)&0xff, (test[6]>>0)&0xff,
		(test[7]>>24)&0xff, (test[7]>>16)&0xff, (test[7]>>8)&0xff, (test[7]>>0)&0xff);
}
static CLASS_ATTR_RO(server_sn);

static ssize_t server_result_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	bool val;
	int rc;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, pst, XM_PROP_SERVER_RESULT, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t server_result_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SERVER_RESULT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_SERVER_RESULT]);
}
static CLASS_ATTR_RW(server_result);

static ssize_t adsp_result_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_ADSP_RESULT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_ADSP_RESULT]);
}
static CLASS_ATTR_RO(adsp_result);
#endif

#if defined(CONFIG_REDWOOD_FOR_BUILD)
static ssize_t shipmode_count_reset_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
				XM_PROP_SHIPMODE_COUNT_RESET, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t shipmode_count_reset_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SHIPMODE_COUNT_RESET);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_SHIPMODE_COUNT_RESET]);
}
static CLASS_ATTR_RW(shipmode_count_reset);

static ssize_t sport_mode_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_XM],
				XM_PROP_SPORT_MODE, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t sport_mode_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_SPORT_MODE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_SPORT_MODE]);
}
static CLASS_ATTR_RW(sport_mode);

static ssize_t cell1_volt_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_CELL1_VOLT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_CELL1_VOLT]);
}
static CLASS_ATTR_RO(cell1_volt);

static ssize_t cell2_volt_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_CELL2_VOLT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[XM_PROP_CELL2_VOLT]);
}
static CLASS_ATTR_RO(cell2_volt);

static ssize_t fg_vendor_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG_VENDOR_ID);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG_VENDOR_ID]);
}
static CLASS_ATTR_RO(fg_vendor);
#endif

#if defined(CONFIG_AI_RSOC_M20)
static ssize_t fg1_rsoc_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_RSOC);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_RSOC]);
}
static CLASS_ATTR_RO(fg1_rsoc);

static ssize_t fg1_ai_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_XM];
	int rc;

	rc = read_property_id(bcdev, pst, XM_PROP_FG1_AI);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[XM_PROP_FG1_AI]);
}
static CLASS_ATTR_RO(fg1_ai);
#endif
#endif

static struct attribute *xiaomi_battery_class_attrs[] = {
	&class_attr_wireless_register.attr,
	&class_attr_wireless_input_curr.attr,
	&class_attr_real_type.attr,
	&class_attr_resistance_id.attr,
	&class_attr_verify_digest.attr,
	&class_attr_verify_slave_flag.attr,
	&class_attr_connector_temp.attr,
	&class_attr_authentic.attr,
	&class_attr_chip_ok.attr,
#if defined(CONFIG_DUAL_FUEL_GAUGE)
	&class_attr_slave_chip_ok.attr,
	&class_attr_slave_authentic.attr,
	&class_attr_fg1_vol.attr,
	&class_attr_fg1_soc.attr,
	&class_attr_fg1_temp.attr,
	&class_attr_fg1_ibatt.attr,
	&class_attr_fg2_vol.attr,
	&class_attr_fg2_soc.attr,
	&class_attr_fg2_temp.attr,
	&class_attr_fg2_ibatt.attr,
	&class_attr_fg2_qmax.attr,
	&class_attr_fg2_rm.attr,
	&class_attr_fg2_fcc.attr,
	&class_attr_fg2_soh.attr,
	&class_attr_fg2_fcc_soh.attr,
	&class_attr_fg2_cycle.attr,
	&class_attr_fg2_fastcharge.attr,
	&class_attr_fg2_current_max.attr,
	&class_attr_fg2_vol_max.attr,
	&class_attr_fg2_tsim.attr,
	&class_attr_fg2_tambient.attr,
	&class_attr_fg2_tremq.attr,
	&class_attr_fg2_tfullq.attr,
	&class_attr_is_old_hw.attr,
#endif
	&class_attr_vbus_disable.attr,
	&class_attr_cc_orientation.attr,
	&class_attr_slave_batt_present.attr,
#if defined(CONFIG_BQ2597X)
	&class_attr_bq2597x_chip_ok.attr,
	&class_attr_bq2597x_slave_chip_ok.attr,
	&class_attr_bq2597x_bus_current.attr,
	&class_attr_bq2597x_slave_bus_current.attr,
	&class_attr_bq2597x_bus_delta.attr,
	&class_attr_bq2597x_bus_voltage.attr,
	&class_attr_bq2597x_battery_present.attr,
	&class_attr_bq2597x_slave_battery_present.attr,
	&class_attr_bq2597x_battery_voltage.attr,
	&class_attr_cool_mode.attr,
#endif
#if defined(CONFIG_REDWOOD_FOR_BUILD)
	&class_attr_bq2597x_slave_connector.attr,
#endif
#if !defined(CONFIG_VENUS_FOR_BUILD)
	&class_attr_bt_transfer_start.attr,
#endif
	&class_attr_master_smb1396_online.attr,
	&class_attr_master_smb1396_iin.attr,
	&class_attr_slave_smb1396_online.attr,
	&class_attr_slave_smb1396_iin.attr,
	&class_attr_smb_iin_diff.attr,
	&class_attr_soc_decimal.attr,
	&class_attr_shutdown_delay.attr,
	&class_attr_soc_decimal_rate.attr,
#if defined(CONFIG_MI_WIRELESS)
	/* wireless charge attrs */
	&class_attr_tx_mac.attr,
	&class_attr_rx_cr.attr,
	&class_attr_rx_cep.attr,
	&class_attr_bt_state.attr,
	&class_attr_reverse_chg_mode.attr,
	&class_attr_reverse_chg_state.attr,
	//&class_attr_wls_fw_state.attr,
	&class_attr_wireless_chip_fw.attr,
	&class_attr_wls_bin.attr,
	&class_attr_rx_vout.attr,
	&class_attr_rx_vrect.attr,
	&class_attr_rx_iout.attr,
	&class_attr_tx_adapter.attr,
	&class_attr_op_mode.attr,
	&class_attr_wls_die_temp.attr,
	&class_attr_wls_car_adapter.attr,
#if !defined(CONFIG_VENUS_FOR_BUILD)
	&class_attr_wls_tx_speed.attr,
#endif
#endif
	/*****************************/
	&class_attr_input_suspend.attr,
	&class_attr_verify_process.attr,
	&class_attr_request_vdm_cmd.attr,
	&class_attr_current_state.attr,
	&class_attr_adapter_id.attr,
	&class_attr_adapter_svid.attr,
	&class_attr_pd_verifed.attr,
	&class_attr_pdo2.attr,
	&class_attr_fastchg_mode.attr,
	&class_attr_apdo_max.attr,
	&class_attr_thermal_remove.attr,
	&class_attr_voter_debug.attr,
	&class_attr_fg_rm.attr,
	&class_attr_wlscharge_control_limit.attr,
	&class_attr_mtbf_current.attr,
	&class_attr_fake_temp.attr,
	&class_attr_qbg_vbat.attr,
	&class_attr_vph_pwr.attr,
	&class_attr_qbg_temp.attr,
	&class_attr_typec_mode.attr,
	&class_attr_night_charging.attr,
#if !defined(CONFIG_VENUS_FOR_BUILD)
	&class_attr_smart_batt.attr,
	&class_attr_fg1_qmax.attr,
	&class_attr_fg1_rm.attr,
	&class_attr_fg1_fcc.attr,
	&class_attr_fg1_soh.attr,
#if defined(CONFIG_AI_RSOC_M20)
	&class_attr_fg1_rsoc.attr,
	&class_attr_fg1_ai.attr,
#endif
	&class_attr_fg1_fcc_soh.attr,
	&class_attr_fg1_cycle.attr,
	&class_attr_fg1_fastcharge.attr,
	&class_attr_fg1_current_max.attr,
	&class_attr_fg1_vol_max.attr,
	&class_attr_fg1_tsim.attr,
	&class_attr_fg1_tambient.attr,
	&class_attr_fg1_tremq.attr,
	&class_attr_fg1_tfullq.attr,
#if defined(CONFIG_REDWOOD_FOR_BUILD)
	&class_attr_fg1_seal_state.attr,
	&class_attr_fg1_seal_set.attr,
	&class_attr_fg1_df_check.attr,
	&class_attr_fg1_voltage_max.attr,
	&class_attr_fg1_charge_current_max.attr,
	&class_attr_fg1_discharge_current_max.attr,
	&class_attr_fg1_temp_max.attr,
	&class_attr_fg1_temp_min.attr,
	&class_attr_fg1_time_ht.attr,
	&class_attr_fg1_time_ot.attr,
	&class_attr_fg1_time_ut.attr,
	&class_attr_fg1_time_lt.attr,
#endif
#if defined(CONFIG_BQ_CLOUD_AUTHENTICATION)
	&class_attr_server_sn.attr,
	&class_attr_server_result.attr,
	&class_attr_adsp_result.attr,
#endif
#if defined(CONFIG_REDWOOD_FOR_BUILD)
	&class_attr_shipmode_count_reset.attr,
	&class_attr_sport_mode.attr,
	&class_attr_cell1_volt.attr,
	&class_attr_cell2_volt.attr,
	&class_attr_fg_vendor.attr,
#endif
#endif
	&class_attr_power_max.attr,
	NULL,
};

const struct attribute_group xiaomi_battery_class_group = {
	.attrs = xiaomi_battery_class_attrs,
};

#define MAX_UEVENT_LENGTH 50
void generate_xm_charge_uvent(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(
		work, struct battery_chg_dev, xm_prop_change_work.work);

	static char uevent_string[][MAX_UEVENT_LENGTH + 1] = {
#if defined(CONFIG_MI_WIRELESS)
		"POWER_SUPPLY_REVERSE_CHG_STATE=\n", //length=31+1
		"POWER_SUPPLY_REVERSE_CHG_MODE=\n", //length=30+1
		"POWER_SUPPLY_TX_MAC=\n", //length=20+16
		"POWER_SUPPLY_RX_CEP=\n", //length=20+16
		"POWER_SUPPLY_RX_CR=\n", //length=19+8
		//"POWER_SUPPLY_WLS_FW_STATE=\n",	//length=26+1
		"POWER_SUPPLY_WLS_CAR_ADAPTER=\n", //length=29+1
#endif
		"POWER_SUPPLY_SOC_DECIMAL=\n", //length=31+8
		"POWER_SUPPLY_SOC_DECIMAL_RATE=\n", //length=31+8
		"POWER_SUPPLY_SHUTDOWN_DELAY=\n", //28+8
		"POWER_SUPPLY_VBUS_DISABLE=\n", //length=26+1
	};
	static char *envp[] = {
		uevent_string[0],
		uevent_string[1],
		uevent_string[2],
		uevent_string[3],
#if defined(CONFIG_MI_WIRELESS)
		uevent_string[4],
		uevent_string[5],
		uevent_string[6],
		uevent_string[7],
		uevent_string[8],
		uevent_string[9],
#endif
		NULL,

	};
	char *prop_buf = NULL;

	prop_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!prop_buf)
		return;

#if defined(CONFIG_MI_WIRELESS)
	/*add our prop start*/
	reverse_chg_state_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[0] + 31, prop_buf, MAX_UEVENT_LENGTH - 31);

	reverse_chg_mode_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[1] + 30, prop_buf, MAX_UEVENT_LENGTH - 30);

	tx_mac_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[2] + 20, prop_buf, MAX_UEVENT_LENGTH - 20);

	rx_cep_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[3] + 20, prop_buf, MAX_UEVENT_LENGTH - 20);

	rx_cr_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[4] + 19, prop_buf, MAX_UEVENT_LENGTH - 19);

	wls_car_adapter_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[5] + 29, prop_buf, MAX_UEVENT_LENGTH - 29);

	soc_decimal_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[6] + 25, prop_buf, MAX_UEVENT_LENGTH - 25);

	soc_decimal_rate_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[7] + 30, prop_buf, MAX_UEVENT_LENGTH - 30);

	shutdown_delay_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[8] + 28, prop_buf, MAX_UEVENT_LENGTH - 28);

	vbus_disable_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[9] + 26, prop_buf, MAX_UEVENT_LENGTH - 26);

	dev_err(bcdev->dev,
		"uevent test : %s\n %s\n %s\n %s\n %s\n %s\n %s\n %s\n %s\n %s\n",
		envp[0], envp[1], envp[2], envp[3], envp[4], envp[5], envp[6],
		envp[7], envp[8], envp[9]);
#else
	soc_decimal_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[0] + 25, prop_buf, MAX_UEVENT_LENGTH - 25);

	soc_decimal_rate_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[1] + 30, prop_buf, MAX_UEVENT_LENGTH - 30);

	shutdown_delay_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[2] + 28, prop_buf, MAX_UEVENT_LENGTH - 28);

	vbus_disable_show(&(bcdev->battery_class), NULL, prop_buf);
	strncpy(uevent_string[3] + 26, prop_buf, MAX_UEVENT_LENGTH - 26);

	dev_err(bcdev->dev, "uevent test : %s\n %s\n %s\n %s\n", envp[0],
		envp[1], envp[2], envp[3]);
#endif

	/*add our prop end*/

	kobject_uevent_env(&bcdev->dev->kobj, KOBJ_CHANGE, envp);

	free_page((unsigned long)prop_buf);
	return;
}

#define CHARGING_PERIOD_S 60
#define DISCHARGE_PERIOD_S 300
void xm_charger_debug_info_print_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev =
		container_of(work, struct battery_chg_dev,
			     charger_debug_info_print_work.work);
	struct power_supply *usb_psy = NULL;
	int rc, usb_present = 0;
	int vbus_vol_uv, ibus_ua;
	int interval = DISCHARGE_PERIOD_S;
	union power_supply_propval val = {
		0,
	};

	usb_psy = bcdev->psy_list[PSY_TYPE_USB].psy;
	if (usb_psy != NULL) {
		rc = usb_psy_get_prop(usb_psy, POWER_SUPPLY_PROP_ONLINE, &val);
		if (!rc)
			usb_present = val.intval;
		else
			usb_present = 0;
		pr_err("usb_present: %d\n", usb_present);
	} else {
		return;
	}

	if (usb_present == 1) {
		rc = usb_psy_get_prop(usb_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW,
				      &val);
		if (!rc)
			vbus_vol_uv = val.intval;
		else
			vbus_vol_uv = 0;

		rc = usb_psy_get_prop(usb_psy, POWER_SUPPLY_PROP_CURRENT_NOW,
				      &val);
		if (!rc)
			ibus_ua = val.intval;
		else
			ibus_ua = 0;

		pr_err("vbus_vol_uv: %d, ibus_ua: %d\n", vbus_vol_uv, ibus_ua);
		interval = CHARGING_PERIOD_S;
	} else {
		interval = DISCHARGE_PERIOD_S;
	}

	schedule_delayed_work(&bcdev->charger_debug_info_print_work,
			      interval * HZ);
}
