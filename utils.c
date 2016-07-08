/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2014-2015, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/stat.h>
#include "wpa_helpers.h"

enum driver_type wifi_chip_type = DRIVER_NOT_SET;
enum openwrt_driver_type openwrt_chip_type = OPENWRT_DRIVER_NOT_SET;


int file_exists(const char *fname)
{
	struct stat s;
	return stat(fname, &s) == 0;
}


int set_wifi_chip(const char *chip_type)
{
	if (!strncmp(chip_type, "WCN", strlen("WCN")))
		wifi_chip_type = DRIVER_WCN;
	else if (!strncmp(chip_type, "ATHEROS", strlen("ATHEROS")))
		wifi_chip_type = DRIVER_ATHEROS;
	else if (!strncmp(chip_type, "AR6003", strlen("AR6003")))
		wifi_chip_type = DRIVER_AR6003;
	else if (strcmp(chip_type, "MAC80211") == 0)
		wifi_chip_type = DRIVER_MAC80211;
	else if (strcmp(chip_type, "QNXNTO") == 0)
		wifi_chip_type = DRIVER_QNXNTO;
	else if (strcmp(chip_type, "OPENWRT") == 0)
		wifi_chip_type = DRIVER_OPENWRT;
	else if (!strncmp(chip_type, "LINUX-WCN", strlen("LINUX-WCN")))
		wifi_chip_type = DRIVER_LINUX_WCN;
	else
		return -1;

	return 0;
}


enum driver_type get_driver_type(void)
{
	struct stat s;
	if (wifi_chip_type == DRIVER_NOT_SET) {
		/* Check for 60G driver */
		ssize_t len;
		char link[256];
		char buf[256];
		char *ifname = get_station_ifname();

		snprintf(buf, sizeof(buf), "/sys/class/net/%s/device/driver",
			 ifname);
		len = readlink(buf, link, sizeof(link) - 1);
		if (len >= 0) {
			link[len] = '\0';
			if (strstr(link, DRIVER_NAME_60G))
				return DRIVER_WIL6210;
		}

		if (stat("/sys/module/mac80211", &s) == 0)
			return DRIVER_MAC80211;
		return DRIVER_ATHEROS;
	}
	return wifi_chip_type;
}


enum openwrt_driver_type get_openwrt_driver_type(void)
{
	struct stat s;

	if (openwrt_chip_type == OPENWRT_DRIVER_NOT_SET) {
		if (stat("/sys/module/umac", &s) == 0)
			openwrt_chip_type = OPENWRT_DRIVER_ATHEROS;
	}

	return openwrt_chip_type;
}


enum sigma_program sigma_program_to_enum(const char *prog)
{
	if (prog == NULL)
		return PROGRAM_UNKNOWN;

	if (strcasecmp(prog, "TDLS") == 0)
		return PROGRAM_TDLS;
	if (strcasecmp(prog, "HS2") == 0)
		return PROGRAM_HS2;
	if (strcasecmp(prog, "HS2_R2") == 0 ||
	    strcasecmp(prog, "HS2-R2") == 0)
		return PROGRAM_HS2_R2;
	if (strcasecmp(prog, "WFD") == 0)
		return PROGRAM_WFD;
	if (strcasecmp(prog, "PMF") == 0)
		return PROGRAM_PMF;
	if (strcasecmp(prog, "WPS") == 0)
		return PROGRAM_WPS;
	if (strcasecmp(prog, "11n") == 0)
		return PROGRAM_HT;
	if (strcasecmp(prog, "VHT") == 0)
		return PROGRAM_VHT;
	if (strcasecmp(prog, "60GHZ") == 0)
		return PROGRAM_60GHZ;
	if (strcasecmp(prog, "NAN") == 0)
		return PROGRAM_NAN;

	return PROGRAM_UNKNOWN;
}
