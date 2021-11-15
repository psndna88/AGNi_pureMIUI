/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 * Copyright (c) 2011-2014, 2016-2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2019-2020, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#ifdef __linux__
#include <sys/stat.h>
#include <linux/ethtool.h>
#include <linux/netlink.h>
#include <linux/sockios.h>
#endif /* __linux__ */
#include "wpa_helpers.h"
#include <sys/ioctl.h>


static enum sigma_cmd_result cmd_ca_get_version(struct sigma_dut *dut,
						struct sigma_conn *conn,
						struct sigma_cmd *cmd)
{
	const char *info;

	info = get_param(cmd, "TestInfo");
	if (info) {
		char buf[200];
		snprintf(buf, sizeof(buf), "NOTE CAPI:TestInfo:%s", info);
		wpa_command(get_main_ifname(dut), buf);
	}

	send_resp(dut, conn, SIGMA_COMPLETE, "version,1.0");
	return STATUS_SENT;
}


#ifdef __linux__

static void first_line(char *s)
{
	while (*s) {
		if (*s == '\r' || *s == '\n') {
			*s = '\0';
			return;
		}
		s++;
	}
}


void get_ver(const char *cmd, char *buf, size_t buflen)
{
	FILE *f;
	char *pos;

	buf[0] = '\0';
	f = popen(cmd, "r");
	if (f == NULL)
		return;
	if (fgets(buf, buflen, f))
		first_line(buf);
	pclose(f);

	pos = strstr(buf, " v");
	if (pos == NULL)
		buf[0] = '\0';
	else
		memmove(buf, pos + 1, strlen(pos));
}

#endif /* __linux__ */


static enum sigma_cmd_result cmd_device_get_info(struct sigma_dut *dut,
						 struct sigma_conn *conn,
						 struct sigma_cmd *cmd)
{
	const char *vendor = "Qualcomm Atheros";
	const char *model = "N/A";
	const char *version = "N/A";
#ifdef __linux__
	char model_buf[128];
	char ver_buf[512];
#endif /* __linux__ */
	int res;
	char resp[512];

#ifdef __linux__
	{
		char fname[128], path[128];
		struct stat s;
		FILE *f;
		char compat_ver[128];
		char wpa_supplicant_ver[128];
		char hostapd_ver[128];
		char host_fw_ver[128];

		snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211",
			 get_main_ifname(dut));
		if (stat(path, &s) == 0) {
			ssize_t res;
			char *pos;

			res = snprintf(fname, sizeof(fname),
				       "/sys/class/net/%s/device/driver",
				       get_main_ifname(dut));
			if (res < 0 || res >= sizeof(fname)) {
				model = "Linux/";
			} else if ((res = readlink(fname, path,
						   sizeof(path))) < 0) {
				model = "Linux/";
			} else {
				if (res >= (int) sizeof(path))
					res = sizeof(path) - 1;
				path[res] = '\0';
				pos = strrchr(path, '/');
				if (pos == NULL)
					pos = path;
				else
					pos++;
				res = snprintf(model_buf, sizeof(model_buf),
					       "Linux/%s", pos);
				if (res >= 0 && res < sizeof(model_buf))
					model = model_buf;
			}
		} else
			model = "Linux";

		/* TODO: get version from wpa_supplicant (+ driver via wpa_s)
		 */

		f = fopen("/sys/module/compat/parameters/"
			  "backported_kernel_version", "r");
		if (f == NULL)
			f = fopen("/sys/module/compat/parameters/"
				  "compat_version", "r");
		if (f) {
			if (fgets(compat_ver, sizeof(compat_ver), f) == NULL)
				compat_ver[0] = '\0';
			else
				first_line(compat_ver);
			fclose(f);
		} else
			compat_ver[0] = '\0';

		get_ver("./hostapd -v 2>&1", hostapd_ver, sizeof(hostapd_ver));
		if (hostapd_ver[0] == '\0')
			get_ver("hostapd -v 2>&1", hostapd_ver,
				sizeof(hostapd_ver));
		get_ver("./wpa_supplicant -v", wpa_supplicant_ver,
			sizeof(wpa_supplicant_ver));
		if (wpa_supplicant_ver[0] == '\0')
			get_ver("wpa_supplicant -v", wpa_supplicant_ver,
				sizeof(wpa_supplicant_ver));

		host_fw_ver[0] = '\0';
		if (get_driver_type(dut) == DRIVER_WCN ||
		    get_driver_type(dut) == DRIVER_LINUX_WCN) {
			get_ver("iwpriv wlan0 version", host_fw_ver,
				sizeof(host_fw_ver));
		} else if (get_driver_type(dut) == DRIVER_WIL6210) {
			struct ethtool_drvinfo drvinfo;
			struct ifreq ifr; /* ifreq suitable for ethtool ioctl */
			int fd; /* socket suitable for ethtool ioctl */

			memset(&drvinfo, 0, sizeof(drvinfo));
			drvinfo.cmd = ETHTOOL_GDRVINFO;

			memset(&ifr, 0, sizeof(ifr));
			strlcpy(ifr.ifr_name, get_main_ifname(dut),
				sizeof(ifr.ifr_name));

			fd = socket(AF_INET, SOCK_DGRAM, 0);
			if (fd < 0)
				fd = socket(AF_NETLINK, SOCK_RAW,
					    NETLINK_GENERIC);
			if (fd >= 0) {
				ifr.ifr_data = (void *) &drvinfo;
				if (ioctl(fd, SIOCETHTOOL, &ifr) == 0)
					strlcpy(host_fw_ver, drvinfo.fw_version,
						sizeof(host_fw_ver));
				close(fd);
			}
		}
		res = snprintf(ver_buf, sizeof(ver_buf),
			       "drv=%s%s%s%s%s%s%s/sigma=" SIGMA_DUT_VER "%s%s",
			       compat_ver,
			       wpa_supplicant_ver[0] ? "/wpas=" : "",
			       wpa_supplicant_ver,
			       hostapd_ver[0] ? "/hapd=" : "",
			       hostapd_ver,
			       host_fw_ver[0] ? "/wlan=" : "",
			       host_fw_ver,
			       dut->version ? "@" : "",
			       dut->version ? dut->version : "");
		if (res < 0 || res >= sizeof(ver_buf))
			return ERROR_SEND_STATUS;
		version = ver_buf;
	}
#endif /* __linux__ */

	if (dut->vendor_name)
		vendor = dut->vendor_name;
	if (dut->model_name)
		model = dut->model_name;
	if (dut->version_name)
		version = dut->version_name;
	res = snprintf(resp, sizeof(resp), "vendor,%s,model,%s,version,%s",
		       vendor, model, version);
	if (res < 0 || res >= sizeof(resp))
		return ERROR_SEND_STATUS;

	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return STATUS_SENT;
}


static int check_device_list_interfaces(struct sigma_cmd *cmd)
{
	if (get_param(cmd, "interfaceType") == NULL)
		return -1;
	return 0;
}


static enum sigma_cmd_result cmd_device_list_interfaces(struct sigma_dut *dut,
							struct sigma_conn *conn,
							struct sigma_cmd *cmd)
{
	const char *type, *band;
	char resp[200];

	type = get_param(cmd, "interfaceType");
	if (type == NULL)
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "device_list_interfaces - "
			"interfaceType=%s", type);
	if (strcmp(type, "802.11") != 0)
		return ERROR_SEND_STATUS;

	band = get_param(cmd, "band");
	if (!band) {
	} else if (strcasecmp(band, "24g") == 0) {
		dut->use_5g = 0;
	} else if (strcasecmp(band, "5g") == 0) {
		dut->use_5g = 1;
	} else {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "errorCode,Unsupported band value");
		return STATUS_SENT_ERROR;
	}
	snprintf(resp, sizeof(resp), "interfaceType,802.11,interfaceID,%s",
		 get_main_ifname(dut));
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return STATUS_SENT;
}


void basic_register_cmds(void)
{
	sigma_dut_reg_cmd("ca_get_version", NULL, cmd_ca_get_version);
	sigma_dut_reg_cmd("device_get_info", NULL, cmd_device_get_info);
	sigma_dut_reg_cmd("device_list_interfaces",
			  check_device_list_interfaces,
			  cmd_device_list_interfaces);
}
