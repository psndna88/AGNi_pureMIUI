/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010-2011, Atheros Communications, Inc.
 * Copyright (c) 2011-2015, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/ioctl.h>
#include <sys/stat.h>
#ifdef __linux__
#include <regex.h>
#include <dirent.h>
#include <sys/time.h>
#include <netpacket/packet.h>
#include <linux/if_ether.h>
#ifdef ANDROID
#include <cutils/properties.h>
#include <android/log.h>
#include "keystore_get.h"
#else /* ANDROID */
#include <ifaddrs.h>
#endif /* ANDROID */
#include <netdb.h>
#endif /* __linux__ */
#ifdef __QNXNTO__
#include <net/if_dl.h>
#endif /* __QNXNTO__ */
#include "wpa_ctrl.h"
#include "wpa_helpers.h"
#include "miracast.h"

/* Temporary files for sta_send_addba */
#define VI_QOS_TMP_FILE     "/tmp/vi-qos.tmp"
#define VI_QOS_FILE         "/tmp/vi-qos.txt"
#define VI_QOS_REFFILE      "/etc/vi-qos.txt"

/*
 * MTU for Ethernet need to take into account 8-byte SNAP header
 * to be added when encapsulating Ethernet frame into 802.11
 */
#ifndef IEEE80211_MAX_DATA_LEN_DMG
#define IEEE80211_MAX_DATA_LEN_DMG 7920
#endif
#ifndef IEEE80211_SNAP_LEN_DMG
#define IEEE80211_SNAP_LEN_DMG 8
#endif

#define NON_PREF_CH_LIST_SIZE 100
#define NEIGHBOR_REPORT_SIZE 1000
#define DEFAULT_NEIGHBOR_BSSID_INFO "17"
#define DEFAULT_NEIGHBOR_PHY_TYPE "1"

extern char *sigma_wpas_ctrl;
extern char *sigma_cert_path;
extern enum driver_type wifi_chip_type;
extern char *sigma_radio_ifname[];

#ifdef __linux__
#define WIL_WMI_MAX_PAYLOAD	248
#define WIL_WMI_BF_TRIG_CMDID	0x83a

struct wil_wmi_header {
	uint8_t mid;
	uint8_t reserved;
	uint16_t cmd;
	uint32_t ts;
} __attribute__((packed));

enum wil_wmi_bf_trig_type {
	WIL_WMI_SLS,
	WIL_WMI_BRP_RX,
	WIL_WMI_BRP_TX,
};

struct wil_wmi_bf_trig_cmd {
	/* enum wil_wmi_bf_trig_type */
	uint32_t bf_type;
	/* cid when type == WMI_BRP_RX */
	uint32_t sta_id;
	uint32_t reserved;
	/* mac address when type = WIL_WMI_SLS */
	uint8_t dest_mac[6];
} __attribute__((packed));
#endif /* __linux__ */

#ifdef ANDROID

static int add_ipv6_rule(struct sigma_dut *dut, const char *ifname);

#define ANDROID_KEYSTORE_GET 'g'
#define ANDROID_KEYSTORE_GET_PUBKEY 'b'

static int android_keystore_get(char cmd, const char *key, unsigned char *val)
{
	/* Android 4.3 changed keystore design, so need to use keystore_get() */
#ifndef KEYSTORE_MESSAGE_SIZE
#define KEYSTORE_MESSAGE_SIZE 65535
#endif /* KEYSTORE_MESSAGE_SIZE */

	ssize_t len;
	uint8_t *value = NULL;

	__android_log_print(ANDROID_LOG_DEBUG, "sigma_dut",
			    "keystore command '%c' key '%s' --> keystore_get",
			    cmd, key);

	len = keystore_get(key, strlen(key), &value);
	if (len < 0) {
		__android_log_print(ANDROID_LOG_DEBUG, "sigma_dut",
				    "keystore_get() failed");
		return -1;
	}

	if (len > KEYSTORE_MESSAGE_SIZE)
		len = KEYSTORE_MESSAGE_SIZE;
	memcpy(val, value, len);
	free(value);
	return len;
}
#endif /* ANDROID */


int set_ps(const char *intf, struct sigma_dut *dut, int enabled)
{
#ifdef __linux__
	char buf[100];

	if (wifi_chip_type == DRIVER_WCN) {
		if (enabled) {
			snprintf(buf, sizeof(buf), "iwpriv wlan0 dump 906");
			if (system(buf) != 0)
				goto set_power_save;
		} else {
			snprintf(buf, sizeof(buf), "iwpriv wlan0 dump 905");
			if (system(buf) != 0)
				goto set_power_save;
			snprintf(buf, sizeof(buf), "iwpriv wlan0 dump 912");
			if (system(buf) != 0)
				goto set_power_save;
		}

		return 0;
	}

set_power_save:
	snprintf(buf, sizeof(buf), "./iw dev %s set power_save %s",
		 intf, enabled ? "on" : "off");
	if (system(buf) != 0) {
		snprintf(buf, sizeof(buf), "iw dev %s set power_save %s",
			 intf, enabled ? "on" : "off");
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set power save %s",
					enabled ? "on" : "off");
			return -1;
		}
	}

	return 0;
#else /* __linux__ */
	return -1;
#endif /* __linux__ */
}


#ifdef __linux__

static int wil6210_get_debugfs_dir(struct sigma_dut *dut, char *path,
				   size_t len)
{
	DIR *dir, *wil_dir;
	struct dirent *entry;
	int ret = -1;
	const char *root_path = "/sys/kernel/debug/ieee80211";

	dir = opendir(root_path);
	if (!dir)
		return -2;

	while ((entry = readdir(dir))) {
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		if (snprintf(path, len, "%s/%s/wil6210",
			     root_path, entry->d_name) >= (int) len) {
			ret = -3;
			break;
		}

		wil_dir = opendir(path);
		if (wil_dir) {
			closedir(wil_dir);
			ret = 0;
			break;
		}
	}

	closedir(dir);
	return ret;
}


static int wil6210_wmi_send(struct sigma_dut *dut, uint16_t command,
			    void *payload, uint16_t length)
{
	struct {
		struct wil_wmi_header hdr;
		char payload[WIL_WMI_MAX_PAYLOAD];
	} __attribute__((packed)) cmd;
	char buf[128], fname[128];
	size_t towrite, written;
	FILE *f;

	if (length > WIL_WMI_MAX_PAYLOAD) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"payload too large(%u, max %u)",
				length, WIL_WMI_MAX_PAYLOAD);
		return -1;
	}

	memset(&cmd.hdr, 0, sizeof(cmd.hdr));
	cmd.hdr.cmd = command;
	memcpy(cmd.payload, payload, length);

	if (wil6210_get_debugfs_dir(dut, buf, sizeof(buf))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to get wil6210 debugfs dir");
		return -1;
	}

	snprintf(fname, sizeof(fname), "%s/wmi_send", buf);
	f = fopen(fname, "wb");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to open: %s", fname);
		return -1;
	}

	towrite = sizeof(cmd.hdr) + length;
	written = fwrite(&cmd, 1, towrite, f);
	fclose(f);
	if (written != towrite) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to send wmi %u", command);
		return -1;
	}

	return 0;
}


static int wil6210_get_sta_info_field(struct sigma_dut *dut, const char *bssid,
				      const char *pattern, unsigned int *field)
{
	char buf[128], fname[128];
	FILE *f;
	regex_t re;
	regmatch_t m[2];
	int rc, ret = -1;

	if (wil6210_get_debugfs_dir(dut, buf, sizeof(buf))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to get wil6210 debugfs dir");
		return -1;
	}

	snprintf(fname, sizeof(fname), "%s/stations", buf);
	f = fopen(fname, "r");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to open: %s", fname);
		return -1;
	}

	if (regcomp(&re, pattern, REG_EXTENDED)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"regcomp failed: %s", pattern);
		goto out;
	}

	/*
	 * find the entry for the mac address
	 * line is of the form: [n] 11:22:33:44:55:66 state AID aid
	 */
	while (fgets(buf, sizeof(buf), f)) {
		if (strcasestr(buf, bssid)) {
			/* extract the field (CID/AID/state) */
			rc = regexec(&re, buf, 2, m, 0);
			if (!rc && (m[1].rm_so >= 0)) {
				buf[m[1].rm_eo] = 0;
				*field = atoi(&buf[m[1].rm_so]);
				ret = 0;
				break;
			}
		}
	}

	regfree(&re);
	if (ret)
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"could not extract field");

out:
	fclose(f);

	return ret;
}


static int wil6210_get_cid(struct sigma_dut *dut, const char *bssid,
			   unsigned int *cid)
{
	const char *pattern = "\\[([0-9]+)\\]";

	return wil6210_get_sta_info_field(dut, bssid, pattern, cid);
}


static int wil6210_send_brp_rx(struct sigma_dut *dut, const char *mac,
			       int l_rx)
{
	struct wil_wmi_bf_trig_cmd cmd;
	unsigned int cid;

	memset(&cmd, 0, sizeof(cmd));

	if (wil6210_get_cid(dut, mac, &cid))
		return -1;

	cmd.bf_type = WIL_WMI_BRP_RX;
	cmd.sta_id = cid;
	/* training length (l_rx) is ignored, FW always uses length 16 */
	return wil6210_wmi_send(dut, WIL_WMI_BF_TRIG_CMDID,
				&cmd, sizeof(cmd));
}


static int wil6210_send_sls(struct sigma_dut *dut, const char *mac)
{
	struct wil_wmi_bf_trig_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));

	if (parse_mac_address(dut, mac, (unsigned char *)&cmd.dest_mac))
		return -1;

	cmd.bf_type = WIL_WMI_SLS;
	return wil6210_wmi_send(dut, WIL_WMI_BF_TRIG_CMDID,
				&cmd, sizeof(cmd));
}

#endif /* __linux__ */


static void static_ip_file(int proto, const char *addr, const char *mask,
			   const char *gw)
{
	if (proto) {
		FILE *f = fopen("static-ip", "w");
		if (f) {
			fprintf(f, "%d %s %s %s\n", proto, addr,
				mask ? mask : "N/A",
				gw ? gw : "N/A");
			fclose(f);
		}
	} else {
		unlink("static-ip");
	}
}


static int send_neighbor_request(struct sigma_dut *dut, const char *intf,
				 const char *ssid)
{
#ifdef __linux__
	char buf[100];

	snprintf(buf, sizeof(buf), "iwpriv %s neighbor %s",
		 intf, ssid);
	sigma_dut_print(dut, DUT_MSG_INFO, "Request: %s", buf);

	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv neighbor request failed");
		return -1;
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "iwpriv neighbor request send");

	return 0;
#else /* __linux__ */
	return -1;
#endif /* __linux__ */
}


static int send_trans_mgmt_query(struct sigma_dut *dut, const char *intf,
				 struct sigma_cmd *cmd)
{
	const char *val;
	int reason_code = 0;
	char buf[1024];

	/*
	 * In the earlier builds we used WNM_QUERY and in later
	 * builds used WNM_BSS_QUERY.
	 */

	val = get_param(cmd, "BTMQuery_Reason_Code");
	if (val)
		reason_code = atoi(val);

	val = get_param(cmd, "Cand_List");
	if (val && atoi(val) == 1 && dut->btm_query_cand_list) {
		snprintf(buf, sizeof(buf), "WNM_BSS_QUERY %d%s", reason_code,
			 dut->btm_query_cand_list);
		free(dut->btm_query_cand_list);
		dut->btm_query_cand_list = NULL;
	} else {
		snprintf(buf, sizeof(buf), "WNM_BSS_QUERY %d", reason_code);
	}

	if (wpa_command(intf, buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"transition management query failed");
		return -1;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"transition management query sent");

	return 0;
}


int is_ip_addr(const char *str)
{
	const char *pos = str;
	struct in_addr addr;

	while (*pos) {
		if (*pos != '.' && (*pos < '0' || *pos > '9'))
			return 0;
		pos++;
	}

	return inet_aton(str, &addr);
}


int is_ipv6_addr(const char *str)
{
	struct sockaddr_in6 addr;

	return inet_pton(AF_INET6, str, &(addr.sin6_addr));
}


int get_ip_config(struct sigma_dut *dut, const char *ifname, char *buf,
		  size_t buf_len)
{
	char tmp[256], *pos, *pos2;
	FILE *f;
	char ip[16], mask[15], dns[16], sec_dns[16];
	const char *str_ps;
	int is_dhcp = 0;
	int s;
#ifdef ANDROID
	char prop[PROPERTY_VALUE_MAX];
#endif /* ANDROID */

	ip[0] = '\0';
	mask[0] = '\0';
	dns[0] = '\0';
	sec_dns[0] = '\0';

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s >= 0) {
		struct ifreq ifr;
		struct sockaddr_in saddr;

		memset(&ifr, 0, sizeof(ifr));
		strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(s, SIOCGIFADDR, &ifr) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Failed to get "
					"%s IP address: %s",
					ifname, strerror(errno));
		} else {
			memcpy(&saddr, &ifr.ifr_addr,
			       sizeof(struct sockaddr_in));
			strlcpy(ip, inet_ntoa(saddr.sin_addr), sizeof(ip));
		}

		if (ioctl(s, SIOCGIFNETMASK, &ifr) == 0) {
			memcpy(&saddr, &ifr.ifr_addr,
			       sizeof(struct sockaddr_in));
			strlcpy(mask, inet_ntoa(saddr.sin_addr), sizeof(mask));
		}
		close(s);
	}

#ifdef ANDROID
	snprintf(tmp, sizeof(tmp), "dhcp.%s.pid", ifname);
	if (property_get(tmp, prop, NULL) != 0 && atoi(prop) > 0) {
		snprintf(tmp, sizeof(tmp), "dhcp.%s.result", ifname);
		if (property_get(tmp, prop, NULL) != 0 &&
		    strcmp(prop, "ok") == 0) {
			snprintf(tmp, sizeof(tmp), "dhcp.%s.ipaddress",
				 ifname);
			if (property_get(tmp, prop, NULL) != 0 &&
			    strcmp(ip, prop) == 0)
				is_dhcp = 1;
		}
	}

	snprintf(tmp, sizeof(tmp), "dhcp.%s.dns1", ifname);
	if (property_get(tmp, prop, NULL) != 0)
		strlcpy(dns, prop, sizeof(dns));
	else if (property_get("net.dns1", prop, NULL) != 0)
		strlcpy(dns, prop, sizeof(dns));

	snprintf(tmp, sizeof(tmp), "dhcp.%s.dns2", ifname);
	if (property_get(tmp, prop, NULL) != 0)
		strlcpy(sec_dns, prop, sizeof(sec_dns));
#else /* ANDROID */
#ifdef __linux__
	if (get_driver_type() == DRIVER_OPENWRT)
		str_ps = "ps -w";
	else
		str_ps = "ps ax";
	snprintf(tmp, sizeof(tmp),
		 "%s | grep dhclient | grep -v grep | grep -q %s",
		 str_ps, ifname);
	if (system(tmp) == 0)
		is_dhcp = 1;
	else {
		snprintf(tmp, sizeof(tmp),
			 "%s | grep udhcpc | grep -v grep | grep -q %s",
			 str_ps, ifname);
		if (system(tmp) == 0)
			is_dhcp = 1;
		else {
			snprintf(tmp, sizeof(tmp),
				 "%s | grep dhcpcd | grep -v grep | grep -q %s",
				 str_ps, ifname);
			if (system(tmp) == 0)
				is_dhcp = 1;
		}
	}
#endif /* __linux__ */

	f = fopen("/etc/resolv.conf", "r");
	if (f) {
		while (fgets(tmp, sizeof(tmp), f)) {
			if (strncmp(tmp, "nameserver", 10) != 0)
				continue;
			pos = tmp + 10;
			while (*pos == ' ' || *pos == '\t')
				pos++;
			pos2 = pos;
			while (*pos2) {
				if (*pos2 == '\n' || *pos2 == '\r') {
					*pos2 = '\0';
					break;
				}
				pos2++;
			}
			if (!dns[0])
				strlcpy(dns, pos, sizeof(dns));
			else if (!sec_dns[0])
				strlcpy(sec_dns, pos, sizeof(sec_dns));
		}
		fclose(f);
	}
#endif /* ANDROID */

	snprintf(buf, buf_len, "dhcp,%d,ip,%s,mask,%s,primary-dns,%s",
		 is_dhcp, ip, mask, dns);
	buf[buf_len - 1] = '\0';

	return 0;
}




int get_ipv6_config(struct sigma_dut *dut, const char *ifname, char *buf,
		    size_t buf_len)
{
#ifdef __linux__
#ifdef ANDROID
	char cmd[200], result[1000], *pos, *end;
	FILE *f;
	size_t len;

	snprintf(cmd, sizeof(cmd), "ip addr show dev %s scope global", ifname);
	f = popen(cmd, "r");
	if (f == NULL)
		return -1;
	len = fread(result, 1, sizeof(result) - 1, f);
	pclose(f);
	if (len == 0)
		return -1;
	result[len] = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "%s result: %s\n", cmd, result);

	pos = strstr(result, "inet6 ");
	if (pos == NULL)
		return -1;
	pos += 6;
	end = strchr(pos, ' ');
	if (end)
		*end = '\0';
	end = strchr(pos, '/');
	if (end)
		*end = '\0';
	snprintf(buf, buf_len, "ip,%s", pos);
	buf[buf_len - 1] = '\0';
	return 0;
#else /* ANDROID */
	struct ifaddrs *ifaddr, *ifa;
	int res, found = 0;
	char host[NI_MAXHOST];

	if (getifaddrs(&ifaddr) < 0) {
		perror("getifaddrs");
		return -1;
	}

	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (strcasecmp(ifname, ifa->ifa_name) != 0)
			continue;
		if (ifa->ifa_addr == NULL ||
		    ifa->ifa_addr->sa_family != AF_INET6)
			continue;

		res = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in6),
				  host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
		if (res != 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "getnameinfo: %s",
					gai_strerror(res));
			continue;
		}
		if (strncmp(host, "fe80::", 6) == 0)
			continue; /* skip link-local */

		sigma_dut_print(dut, DUT_MSG_DEBUG, "ifaddr: %s", host);
		found = 1;
		break;
	}

	freeifaddrs(ifaddr);

	if (found) {
		char *pos;
		pos = strchr(host, '%');
		if (pos)
			*pos = '\0';
		snprintf(buf, buf_len, "ip,%s", host);
		buf[buf_len - 1] = '\0';
		return 0;
	}

#endif /* ANDROID */
#endif /* __linux__ */
	return -1;
}


static int cmd_sta_get_ip_config(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	char buf[200];
	const char *val;
	int type = 1;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;

	/*
	 * UCC may assume the IP address to be available immediately after
	 * association without trying to run sta_get_ip_config multiple times.
	 * Sigma CAPI does not specify this command as a block command that
	 * would wait for the address to become available, but to pass tests
	 * more reliably, it looks like such a wait may be needed here.
	 */
	if (wait_ip_addr(dut, ifname, 15) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Could not get IP address "
				"for sta_get_ip_config");
		/*
		 * Try to continue anyway since many UCC tests do not really
		 * care about the return value from here..
		 */
	}

	val = get_param(cmd, "Type");
	if (val)
		type = atoi(val);
	if (type == 2 || dut->last_set_ip_config_ipv6) {
		int i;

		/*
		 * Since we do not have proper wait for IPv6 addresses, use a
		 * fixed two second delay here as a workaround for UCC script
		 * assuming IPv6 address is available when this command returns.
		 * Some scripts did not use Type,2 properly for IPv6, so include
		 * also the cases where the previous sta_set_ip_config indicated
		 * use of IPv6.
		 */
		sigma_dut_print(dut, DUT_MSG_INFO, "Wait up to extra ten seconds in sta_get_ip_config for IPv6 address");
		for (i = 0; i < 10; i++) {
			sleep(1);
			if (get_ipv6_config(dut, ifname, buf, sizeof(buf)) == 0)
			{
				sigma_dut_print(dut, DUT_MSG_INFO, "Found IPv6 address");
				send_resp(dut, conn, SIGMA_COMPLETE, buf);
#ifdef ANDROID
				sigma_dut_print(dut, DUT_MSG_INFO,
						"Adding IPv6 rule on Android");
				add_ipv6_rule(dut, intf);
#endif /* ANDROID */

				return 0;
			}
		}
	}
	if (type == 1) {
		if (get_ip_config(dut, ifname, buf, sizeof(buf)) < 0)
			return -2;
	} else if (type == 2) {
		if (get_ipv6_config(dut, ifname, buf, sizeof(buf)) < 0)
			return -2;
	} else {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported address type");
		return 0;
	}

	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return 0;
}


static void kill_dhcp_client(struct sigma_dut *dut, const char *ifname)
{
#ifdef __linux__
	char buf[200];
	char path[128];
	struct stat s;

#ifdef ANDROID
	snprintf(path, sizeof(path), "/data/misc/dhcp/dhcpcd-%s.pid", ifname);
#else /* ANDROID */
	snprintf(path, sizeof(path), "/var/run/dhclient-%s.pid", ifname);
#endif /* ANDROID */
	if (stat(path, &s) == 0) {
		snprintf(buf, sizeof(buf), "kill `cat %s`", path);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Kill previous DHCP client: %s", buf);
		if (system(buf) != 0)
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to kill DHCP client");
		unlink(path);
		sleep(1);
	} else {
		snprintf(path, sizeof(path), "/var/run/dhcpcd-%s.pid", ifname);

		if (stat(path, &s) == 0) {
			snprintf(buf, sizeof(buf), "kill `cat %s`", path);
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Kill previous DHCP client: %s", buf);
			if (system(buf) != 0)
				sigma_dut_print(dut, DUT_MSG_INFO,
						"Failed to kill DHCP client");
			unlink(path);
			sleep(1);
		}
	}
#endif /* __linux__ */
}


static int start_dhcp_client(struct sigma_dut *dut, const char *ifname)
{
#ifdef __linux__
	char buf[200];

#ifdef ANDROID
	if (access("/system/bin/dhcpcd", F_OK) != -1) {
		snprintf(buf, sizeof(buf),
			 "/system/bin/dhcpcd -b %s", ifname);
	} else if (access("/system/bin/dhcptool", F_OK) != -1) {
		snprintf(buf, sizeof(buf), "/system/bin/dhcptool %s &", ifname);
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"DHCP client program missing");
		return 0;
	}
#else /* ANDROID */
	snprintf(buf, sizeof(buf),
		 "dhclient -nw -pf /var/run/dhclient-%s.pid %s",
		 ifname, ifname);
#endif /* ANDROID */
	sigma_dut_print(dut, DUT_MSG_INFO, "Start DHCP client: %s", buf);
	if (system(buf) != 0) {
		snprintf(buf, sizeof(buf), "dhcpcd -t 0 %s &", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to start DHCP client");
#ifndef ANDROID
			return -1;
#endif /* ANDROID */
		}
	}
#endif /* __linux__ */

	return 0;
}


static int clear_ip_addr(struct sigma_dut *dut, const char *ifname)
{
#ifdef __linux__
	char buf[200];

	snprintf(buf, sizeof(buf), "ip addr flush dev %s", ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to clear IP addresses");
		return -1;
	}
#endif /* __linux__ */

	return 0;
}


#ifdef ANDROID
static int add_ipv6_rule(struct sigma_dut *dut, const char *ifname)
{
	char cmd[200], *result, *pos;
	FILE *fp;
	int tableid;
	size_t len, result_len = 1000;

	snprintf(cmd, sizeof(cmd), "ip -6 route list table all | grep %s",
		 ifname);
	fp = popen(cmd, "r");
	if (fp == NULL)
		return -1;

	result = malloc(result_len);
	if (result == NULL) {
		fclose(fp);
		return -1;
	}

	len = fread(result, 1, result_len - 1, fp);
	fclose(fp);

	if (len == 0) {
		free(result);
		return -1;
	}
	result[len] = '\0';

	pos = strstr(result, "table ");
	if (pos == NULL) {
		free(result);
		return -1;
	}

	pos += strlen("table ");
	tableid = atoi(pos);
	if (tableid != 0) {
		if (system("ip -6 rule del prio 22000") != 0) {
			/* ignore any error */
		}
		snprintf(cmd, sizeof(cmd),
			 "ip -6 rule add from all lookup %d prio 22000",
			 tableid);
		if (system(cmd) != 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to run %s", cmd);
			free(result);
			return -1;
		}
	} else {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"No Valid Table Id found %s", pos);
		free(result);
		return -1;
	}
	free(result);

	return 0;
}
#endif /* ANDROID */


static int cmd_sta_set_ip_config(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	char buf[200];
	const char *val, *ip, *mask, *gw;
	int type = 1;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;

	if (if_nametoindex(ifname) == 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unknown interface");
		return 0;
	}

	val = get_param(cmd, "Type");
	if (val) {
		type = atoi(val);
		if (type != 1 && type != 2) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported address type");
			return 0;
		}
	}

	dut->last_set_ip_config_ipv6 = 0;

	val = get_param(cmd, "dhcp");
	if (val && (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0)) {
		static_ip_file(0, NULL, NULL, NULL);
#ifdef __linux__
		if (type == 2) {
			dut->last_set_ip_config_ipv6 = 1;
			sigma_dut_print(dut, DUT_MSG_INFO, "Using IPv6 "
					"stateless address autoconfiguration");
#ifdef ANDROID
			/*
			 * This sleep is required as the assignment in case of
			 * Android is taking time and is done by the kernel.
			 * The subsequent ping for IPv6 is impacting HS20 test
			 * case.
			 */
			sleep(2);
			add_ipv6_rule(dut, intf);
#endif /* ANDROID */
			/* Assume this happens by default */
			return 1;
		}

		kill_dhcp_client(dut, ifname);
		if (start_dhcp_client(dut, ifname) < 0)
			return -2;
		return 1;
#endif /* __linux__ */
		return -2;
	}

	ip = get_param(cmd, "ip");
	if (!ip) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,Missing IP address");
		return 0;
	}

	mask = get_param(cmd, "mask");
	if (!mask) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,Missing subnet mask");
		return 0;
	}

	if (type == 2) {
		int net = atoi(mask);

		if ((net < 0 && net > 64) || !is_ipv6_addr(ip))
			return -1;

		if (dut->no_ip_addr_set) {
			snprintf(buf, sizeof(buf),
				 "sysctl net.ipv6.conf.%s.disable_ipv6=1",
				 ifname);
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"Failed to disable IPv6 address before association");
			}
		} else {
			snprintf(buf, sizeof(buf),
				 "ip -6 addr del %s/%s dev %s",
				 ip, mask, ifname);
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
			if (system(buf) != 0) {
				/*
				 * This command may fail if the address being
				 * deleted does not exist. Inaction here is
				 * intentional.
				 */
			}

			snprintf(buf, sizeof(buf),
				 "ip -6 addr add %s/%s dev %s",
				 ip, mask, ifname);
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
			if (system(buf) != 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set IPv6 address");
				return 0;
			}
		}

		dut->last_set_ip_config_ipv6 = 1;
		static_ip_file(6, ip, mask, NULL);
		return 1;
	} else if (type == 1) {
		if (!is_ip_addr(ip) || !is_ip_addr(mask))
			return -1;
	}

	kill_dhcp_client(dut, ifname);

	if (!dut->no_ip_addr_set) {
		snprintf(buf, sizeof(buf), "ifconfig %s %s netmask %s",
			 ifname, ip, mask);
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set IP address");
			return 0;
		}
	}

	gw = get_param(cmd, "defaultGateway");
	if (gw) {
		if (!is_ip_addr(gw))
			return -1;
		snprintf(buf, sizeof(buf), "route add default gw %s", gw);
		if (!dut->no_ip_addr_set && system(buf) != 0) {
			snprintf(buf, sizeof(buf), "ip ro re default via %s",
				 gw);
			if (system(buf) != 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed "
					  "to set default gateway");
				return 0;
			}
		}
	}

	val = get_param(cmd, "primary-dns");
	if (val) {
		/* TODO */
		sigma_dut_print(dut, DUT_MSG_INFO, "Ignored primary-dns %s "
				"setting", val);
	}

	val = get_param(cmd, "secondary-dns");
	if (val) {
		/* TODO */
		sigma_dut_print(dut, DUT_MSG_INFO, "Ignored secondary-dns %s "
				"setting", val);
	}

	static_ip_file(4, ip, mask, gw);

	return 1;
}


static int cmd_sta_get_info(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	/* TODO: could report more details here */
	send_resp(dut, conn, SIGMA_COMPLETE, "vendor,Atheros");
	return 0;
}


static int cmd_sta_get_mac_address(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	char addr[20], resp[50];

	if (get_wpa_status(get_station_ifname(), "address", addr, sizeof(addr))
	    < 0)
		return -2;

	snprintf(resp, sizeof(resp), "mac,%s", addr);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static int cmd_sta_is_connected(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	int connected = 0;
	char result[32];
	if (get_wpa_status(get_station_ifname(), "wpa_state", result,
			   sizeof(result)) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Could not get interface "
				"%s status", get_station_ifname());
		return -2;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "wpa_state=%s", result);
	if (strncmp(result, "COMPLETED", 9) == 0)
		connected = 1;

	if (connected)
		send_resp(dut, conn, SIGMA_COMPLETE, "connected,1");
	else
		send_resp(dut, conn, SIGMA_COMPLETE, "connected,0");

	return 0;
}


static int cmd_sta_verify_ip_connection(struct sigma_dut *dut,
					struct sigma_conn *conn,
					struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *dst, *timeout;
	int wait_time = 90;
	char buf[100];
	int res;

	dst = get_param(cmd, "destination");
	if (dst == NULL || !is_ip_addr(dst))
		return -1;

	timeout = get_param(cmd, "timeout");
	if (timeout) {
		wait_time = atoi(timeout);
		if (wait_time < 1)
			wait_time = 1;
	}

	/* TODO: force renewal of IP lease if DHCP is enabled */

	snprintf(buf, sizeof(buf), "ping %s -c 3 -W %d", dst, wait_time);
	res = system(buf);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "ping returned: %d", res);
	if (res == 0)
		send_resp(dut, conn, SIGMA_COMPLETE, "connected,1");
	else if (res == 256)
		send_resp(dut, conn, SIGMA_COMPLETE, "connected,0");
	else
		return -2;

	return 0;
}


static int cmd_sta_get_bssid(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	char bssid[20], resp[50];

	if (get_wpa_status(get_station_ifname(), "bssid", bssid, sizeof(bssid))
	    < 0)
		strlcpy(bssid, "00:00:00:00:00:00", sizeof(bssid));

	snprintf(resp, sizeof(resp), "bssid,%s", bssid);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


#ifdef __SAMSUNG__
static int add_use_network(const char *ifname)
{
	char buf[100];

	snprintf(buf, sizeof(buf), "USE_NETWORK ON");
	wpa_command(ifname, buf);
	return 0;
}
#endif /* __SAMSUNG__ */


static int add_network_common(struct sigma_dut *dut, struct sigma_conn *conn,
			      const char *ifname, struct sigma_cmd *cmd)
{
	const char *ssid = get_param(cmd, "ssid");
	int id;
	const char *val;

	if (ssid == NULL)
		return -1;

	start_sta_mode(dut);

#ifdef __SAMSUNG__
	add_use_network(ifname);
#endif /* __SAMSUNG__ */

	id = add_network(ifname);
	if (id < 0)
		return -2;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Adding network %d", id);

	if (set_network_quoted(ifname, id, "ssid", ssid) < 0)
		return -2;

	dut->infra_network_id = id;
	snprintf(dut->infra_ssid, sizeof(dut->infra_ssid), "%s", ssid);

	val = get_param(cmd, "program");
	if (!val)
		val = get_param(cmd, "prog");
	if (val && strcasecmp(val, "hs2") == 0) {
		char buf[100];
		snprintf(buf, sizeof(buf), "ENABLE_NETWORK %d no-connect", id);
		wpa_command(ifname, buf);

		val = get_param(cmd, "prefer");
		if (val && atoi(val) > 0)
			set_network(ifname, id, "priority", "1");
	}

	return id;
}


static int cmd_sta_set_encryption(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ssid = get_param(cmd, "ssid");
	const char *type = get_param(cmd, "encpType");
	const char *ifname;
	char buf[200];
	int id;

	if (intf == NULL || ssid == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;

	id = add_network_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "key_mgmt", "NONE") < 0)
		return -2;

	if (type && strcasecmp(type, "wep") == 0) {
		const char *val;
		int i;

		val = get_param(cmd, "activeKey");
		if (val) {
			int keyid;
			keyid = atoi(val);
			if (keyid < 1 || keyid > 4)
				return -1;
			snprintf(buf, sizeof(buf), "%d", keyid - 1);
			if (set_network(ifname, id, "wep_tx_keyidx", buf) < 0)
				return -2;
		}

		for (i = 0; i < 4; i++) {
			snprintf(buf, sizeof(buf), "key%d", i + 1);
			val = get_param(cmd, buf);
			if (val == NULL)
				continue;
			snprintf(buf, sizeof(buf), "wep_key%d", i);
			if (set_network(ifname, id, buf, val) < 0)
				return -2;
		}
	}

	return 1;
}


static int set_wpa_common(struct sigma_dut *dut, struct sigma_conn *conn,
			  const char *ifname, struct sigma_cmd *cmd)
{
	const char *val;
	int id;

	id = add_network_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	val = get_param(cmd, "keyMgmtType");
	if (val == NULL) {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Missing keyMgmtType");
		return 0;
	}
	if (strcasecmp(val, "wpa") == 0 ||
	    strcasecmp(val, "wpa-psk") == 0) {
		if (set_network(ifname, id, "proto", "WPA") < 0)
			return -2;
	} else if (strcasecmp(val, "wpa2") == 0 ||
		   strcasecmp(val, "wpa2-psk") == 0 ||
		   strcasecmp(val, "wpa2-ft") == 0 ||
		   strcasecmp(val, "wpa2-sha256") == 0) {
		if (set_network(ifname, id, "proto", "WPA2") < 0)
			return -2;
	} else if (strcasecmp(val, "wpa2-wpa-psk") == 0 ||
		   strcasecmp(val, "wpa2-wpa-ent") == 0) {
		if (set_network(ifname, id, "proto", "WPA WPA2") < 0)
			return -2;
	} else {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Unrecognized keyMgmtType value");
		return 0;
	}

	val = get_param(cmd, "encpType");
	if (val == NULL) {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Missing encpType");
		return 0;
	}
	if (strcasecmp(val, "tkip") == 0) {
		if (set_network(ifname, id, "pairwise", "TKIP") < 0)
			return -2;
	} else if (strcasecmp(val, "aes-ccmp") == 0) {
		if (set_network(ifname, id, "pairwise", "CCMP") < 0)
			return -2;
	} else if (strcasecmp(val, "aes-ccmp-tkip") == 0) {
		if (set_network(ifname, id, "pairwise", "CCMP TKIP") < 0)
			return -2;
	} else if (strcasecmp(val, "aes-gcmp") == 0) {
		if (set_network(ifname, id, "pairwise", "GCMP") < 0)
			return -2;
		if (set_network(ifname, id, "group", "GCMP") < 0)
			return -2;
	} else {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Unrecognized encpType value");
		return 0;
	}

	dut->sta_pmf = STA_PMF_DISABLED;
	val = get_param(cmd, "PMF");
	if (val) {
		if (strcasecmp(val, "Required") == 0 ||
		    strcasecmp(val, "Forced_Required") == 0) {
			dut->sta_pmf = STA_PMF_REQUIRED;
			if (set_network(ifname, id, "ieee80211w", "2") < 0)
				return -2;
		} else if (strcasecmp(val, "Optional") == 0) {
			dut->sta_pmf = STA_PMF_OPTIONAL;
			if (set_network(ifname, id, "ieee80211w", "1") < 0)
				return -2;
		} else if (strcasecmp(val, "Disabled") == 0 ||
			   strcasecmp(val, "Forced_Disabled") == 0) {
			dut->sta_pmf = STA_PMF_DISABLED;
		} else {
			send_resp(dut, conn, SIGMA_INVALID, "errorCode,Unrecognized PMF value");
			return 0;
		}
	}

	return id;
}


static int cmd_sta_set_psk(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *type = get_param(cmd, "Type");
	const char *ifname, *val, *alg;
	int id;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;

	id = set_wpa_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	val = get_param(cmd, "keyMgmtType");
	alg = get_param(cmd, "micAlg");

	if (type && strcasecmp(type, "SAE") == 0) {
		if (val && strcasecmp(val, "wpa2-ft") == 0) {
			if (set_network(ifname, id, "key_mgmt", "FT-SAE") < 0)
				return -2;
		} else {
			if (set_network(ifname, id, "key_mgmt", "SAE") < 0)
				return -2;
		}
		if (wpa_command(ifname, "SET sae_groups ") != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to clear sae_groups to default");
			return -2;
		}
	} else if (alg && strcasecmp(alg, "SHA-256") == 0) {
		if (set_network(ifname, id, "key_mgmt", "WPA-PSK-SHA256") < 0)
			return -2;
	} else if (alg && strcasecmp(alg, "SHA-1") == 0) {
		if (set_network(ifname, id, "key_mgmt", "WPA-PSK") < 0)
			return -2;
	} else if (val && strcasecmp(val, "wpa2-ft") == 0) {
		if (set_network(ifname, id, "key_mgmt", "FT-PSK") < 0)
			return -2;
	} else if ((val && strcasecmp(val, "wpa2-sha256") == 0) ||
		   dut->sta_pmf == STA_PMF_REQUIRED) {
		if (set_network(ifname, id, "key_mgmt",
				"WPA-PSK WPA-PSK-SHA256") < 0)
			return -2;
	} else if (dut->sta_pmf == STA_PMF_OPTIONAL) {
		if (set_network(ifname, id, "key_mgmt",
				"WPA-PSK WPA-PSK-SHA256") < 0)
			return -2;
	} else {
		if (set_network(ifname, id, "key_mgmt", "WPA-PSK") < 0)
			return -2;
	}

	val = get_param(cmd, "passPhrase");
	if (val == NULL)
		return -1;
	if (set_network_quoted(ifname, id, "psk", val) < 0)
		return -2;

	val = get_param(cmd, "ECGroupID");
	if (val) {
		char buf[50];

		snprintf(buf, sizeof(buf), "SET sae_groups %u", atoi(val));
		if (wpa_command(ifname, buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to clear sae_groups");
			return -2;
		}
	}

	return 1;
}


static int set_eap_common(struct sigma_dut *dut, struct sigma_conn *conn,
			  const char *ifname, int username_identity,
			  struct sigma_cmd *cmd)
{
	const char *val, *alg;
	int id;
	char buf[200];
#ifdef ANDROID
	unsigned char kvalue[KEYSTORE_MESSAGE_SIZE];
	int length;
#endif /* ANDROID */

	id = set_wpa_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	val = get_param(cmd, "keyMgmtType");
	alg = get_param(cmd, "micAlg");

	if (alg && strcasecmp(alg, "SHA-256") == 0) {
		if (set_network(ifname, id, "key_mgmt", "WPA-EAP-SHA256") < 0)
			return -2;
	} else if (alg && strcasecmp(alg, "SHA-1") == 0) {
		if (set_network(ifname, id, "key_mgmt", "WPA-EAP") < 0)
			return -2;
	} else if (val && strcasecmp(val, "wpa2-ft") == 0) {
		if (set_network(ifname, id, "key_mgmt", "FT-EAP") < 0)
			return -2;
	} else if ((val && strcasecmp(val, "wpa2-sha256") == 0) ||
		   dut->sta_pmf == STA_PMF_REQUIRED) {
		if (set_network(ifname, id, "key_mgmt",
				"WPA-EAP WPA-EAP-SHA256") < 0)
			return -2;
	} else if (dut->sta_pmf == STA_PMF_OPTIONAL) {
		if (set_network(ifname, id, "key_mgmt",
				"WPA-EAP WPA-EAP-SHA256") < 0)
			return -2;
	} else {
		if (set_network(ifname, id, "key_mgmt", "WPA-EAP") < 0)
			return -2;
	}

	val = get_param(cmd, "trustedRootCA");
	if (val) {
#ifdef ANDROID
		snprintf(buf, sizeof(buf), "CACERT_%s", val);
		length = android_keystore_get(ANDROID_KEYSTORE_GET, buf,
					      kvalue);
		if (length > 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Use Android keystore [%s]", buf);
			snprintf(buf, sizeof(buf), "keystore://CACERT_%s",
				 val);
			goto ca_cert_selected;
		}
#endif /* ANDROID */

		snprintf(buf, sizeof(buf), "%s/%s", sigma_cert_path, val);
#ifdef __linux__
		if (!file_exists(buf)) {
			char msg[300];
			snprintf(msg, sizeof(msg), "ErrorCode,trustedRootCA "
				 "file (%s) not found", buf);
			send_resp(dut, conn, SIGMA_ERROR, msg);
			return -3;
		}
#endif /* __linux__ */
#ifdef ANDROID
ca_cert_selected:
#endif /* ANDROID */
		if (set_network_quoted(ifname, id, "ca_cert", buf) < 0)
			return -2;
	}

	if (username_identity) {
		val = get_param(cmd, "username");
		if (val) {
			if (set_network_quoted(ifname, id, "identity", val) < 0)
				return -2;
		}

		val = get_param(cmd, "password");
		if (val) {
			if (set_network_quoted(ifname, id, "password", val) < 0)
				return -2;
		}
	}

	return id;
}


static int cmd_sta_set_eaptls(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname, *val;
	int id;
	char buf[200];
#ifdef ANDROID
	unsigned char kvalue[KEYSTORE_MESSAGE_SIZE];
	int length;
	int jb_or_newer = 0;
	char prop[PROPERTY_VALUE_MAX];
#endif /* ANDROID */

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, 1, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "TLS") < 0)
		return -2;

	if (!get_param(cmd, "username") &&
	    set_network_quoted(ifname, id, "identity",
			       "wifi-user@wifilabs.local") < 0)
		return -2;

	val = get_param(cmd, "clientCertificate");
	if (val == NULL)
		return -1;
#ifdef ANDROID
	snprintf(buf, sizeof(buf), "USRPKEY_%s", val);
	length = android_keystore_get(ANDROID_KEYSTORE_GET, buf, kvalue);
	if (length < 0) {
		/*
		 * JB started reporting keystore type mismatches, so retry with
		 * the GET_PUBKEY command if the generic GET fails.
		 */
		length = android_keystore_get(ANDROID_KEYSTORE_GET_PUBKEY,
					      buf, kvalue);
	}

	if (property_get("ro.build.version.release", prop, NULL) != 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Android release %s", prop);
		if (strncmp(prop, "4.0", 3) != 0)
			jb_or_newer = 1;
	} else
		jb_or_newer = 1; /* assume newer */

	if (jb_or_newer && length > 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Use Android keystore [%s]", buf);
		if (set_network(ifname, id, "engine", "1") < 0)
			return -2;
		if (set_network_quoted(ifname, id, "engine_id", "keystore") < 0)
			return -2;
		snprintf(buf, sizeof(buf), "USRPKEY_%s", val);
		if (set_network_quoted(ifname, id, "key_id", buf) < 0)
			return -2;
		snprintf(buf, sizeof(buf), "keystore://USRCERT_%s", val);
		if (set_network_quoted(ifname, id, "client_cert", buf) < 0)
			return -2;
		return 1;
	} else if (length > 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Use Android keystore [%s]", buf);
		snprintf(buf, sizeof(buf), "keystore://USRPKEY_%s", val);
		if (set_network_quoted(ifname, id, "private_key", buf) < 0)
			return -2;
		snprintf(buf, sizeof(buf), "keystore://USRCERT_%s", val);
		if (set_network_quoted(ifname, id, "client_cert", buf) < 0)
			return -2;
		return 1;
	}
#endif /* ANDROID */

	snprintf(buf, sizeof(buf), "%s/%s", sigma_cert_path, val);
#ifdef __linux__
	if (!file_exists(buf)) {
		char msg[300];
		snprintf(msg, sizeof(msg), "ErrorCode,clientCertificate file "
			 "(%s) not found", buf);
		send_resp(dut, conn, SIGMA_ERROR, msg);
		return -3;
	}
#endif /* __linux__ */
	if (set_network_quoted(ifname, id, "private_key", buf) < 0)
		return -2;
	if (set_network_quoted(ifname, id, "client_cert", buf) < 0)
		return -2;

	if (set_network_quoted(ifname, id, "private_key_passwd", "wifi") < 0)
		return -2;

	return 1;
}


static int cmd_sta_set_eapttls(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	int id;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, 1, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "TTLS") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to set TTLS method");
		return 0;
	}

	if (set_network_quoted(ifname, id, "phase2", "auth=MSCHAPV2") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to set MSCHAPv2 for TTLS Phase 2");
		return 0;
	}

	return 1;
}


static int cmd_sta_set_eapsim(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	int id;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, !dut->sim_no_username, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "SIM") < 0)
		return -2;

	return 1;
}


static int cmd_sta_set_peap(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname, *val;
	int id;
	char buf[100];

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, 1, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "PEAP") < 0)
		return -2;

	val = get_param(cmd, "innerEAP");
	if (val) {
		if (strcasecmp(val, "MSCHAPv2") == 0) {
			if (set_network_quoted(ifname, id, "phase2",
					       "auth=MSCHAPV2") < 0)
				return -2;
		} else if (strcasecmp(val, "GTC") == 0) {
			if (set_network_quoted(ifname, id, "phase2",
					       "auth=GTC") < 0)
				return -2;
		} else
			return -1;
	}

	val = get_param(cmd, "peapVersion");
	if (val) {
		int ver = atoi(val);
		if (ver < 0 || ver > 1)
			return -1;
		snprintf(buf, sizeof(buf), "peapver=%d", ver);
		if (set_network_quoted(ifname, id, "phase1", buf) < 0)
			return -2;
	}

	return 1;
}


static int cmd_sta_set_eapfast(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname, *val;
	int id;
	char buf[100];

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, 1, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "FAST") < 0)
		return -2;

	val = get_param(cmd, "innerEAP");
	if (val) {
		if (strcasecmp(val, "MSCHAPV2") == 0) {
			if (set_network_quoted(ifname, id, "phase2",
					       "auth=MSCHAPV2") < 0)
				return -2;
		} else if (strcasecmp(val, "GTC") == 0) {
			if (set_network_quoted(ifname, id, "phase2",
					       "auth=GTC") < 0)
				return -2;
		} else
			return -1;
	}

	val = get_param(cmd, "validateServer");
	if (val) {
		/* TODO */
		sigma_dut_print(dut, DUT_MSG_INFO, "Ignored EAP-FAST "
				"validateServer=%s", val);
	}

	val = get_param(cmd, "pacFile");
	if (val) {
		snprintf(buf, sizeof(buf), "blob://%s", val);
		if (set_network_quoted(ifname, id, "pac_file", buf) < 0)
			return -2;
	}

	if (set_network_quoted(ifname, id, "phase1", "fast_provisioning=2") <
	    0)
		return -2;

	return 1;
}


static int cmd_sta_set_eapaka(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	int id;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, !dut->sim_no_username, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "AKA") < 0)
		return -2;

	return 1;
}


static int cmd_sta_set_eapakaprime(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	int id;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, !dut->sim_no_username, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "AKA'") < 0)
		return -2;

	return 1;
}


static int sta_set_open(struct sigma_dut *dut, struct sigma_conn *conn,
			struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	int id;

	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;

	id = add_network_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "key_mgmt", "NONE") < 0)
		return -2;

	return 1;
}


static int cmd_sta_set_security(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *type = get_param(cmd, "Type");

	if (type == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing Type argument");
		return 0;
	}

	if (strcasecmp(type, "OPEN") == 0)
		return sta_set_open(dut, conn, cmd);
	if (strcasecmp(type, "PSK") == 0 ||
	    strcasecmp(type, "SAE") == 0)
		return cmd_sta_set_psk(dut, conn, cmd);
	if (strcasecmp(type, "EAPTLS") == 0)
		return cmd_sta_set_eaptls(dut, conn, cmd);
	if (strcasecmp(type, "EAPTTLS") == 0)
		return cmd_sta_set_eapttls(dut, conn, cmd);
	if (strcasecmp(type, "EAPPEAP") == 0)
		return cmd_sta_set_peap(dut, conn, cmd);
	if (strcasecmp(type, "EAPSIM") == 0)
		return cmd_sta_set_eapsim(dut, conn, cmd);
	if (strcasecmp(type, "EAPFAST") == 0)
		return cmd_sta_set_eapfast(dut, conn, cmd);
	if (strcasecmp(type, "EAPAKA") == 0)
		return cmd_sta_set_eapaka(dut, conn, cmd);
	if (strcasecmp(type, "EAPAKAPRIME") == 0)
		return cmd_sta_set_eapakaprime(dut, conn, cmd);

	send_resp(dut, conn, SIGMA_ERROR,
		  "ErrorCode,Unsupported Type value");
	return 0;
}


int ath6kl_client_uapsd(struct sigma_dut *dut, const char *intf, int uapsd)
{
#ifdef __linux__
	/* special handling for ath6kl */
	char path[128], fname[128], *pos;
	ssize_t res;
	FILE *f;

	snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211", intf);
	res = readlink(path, path, sizeof(path));
	if (res < 0)
		return 0; /* not ath6kl */

	if (res >= (int) sizeof(path))
		res = sizeof(path) - 1;
	path[res] = '\0';
	pos = strrchr(path, '/');
	if (pos == NULL)
		pos = path;
	else
		pos++;
	snprintf(fname, sizeof(fname),
		 "/sys/kernel/debug/ieee80211/%s/ath6kl/"
		 "create_qos", pos);
	if (!file_exists(fname))
		return 0; /* not ath6kl */

	if (uapsd) {
		f = fopen(fname, "w");
		if (f == NULL)
			return -1;

		sigma_dut_print(dut, DUT_MSG_DEBUG, "Use ath6kl create_qos");
		fprintf(f, "4 2 2 1 2 9999999 9999999 9999999 7777777 0 4 "
			"45000 200 56789000 56789000 5678900 0 0 9999999 "
			"20000 0\n");
		fclose(f);
	} else {
		snprintf(fname, sizeof(fname),
			 "/sys/kernel/debug/ieee80211/%s/ath6kl/"
			 "delete_qos", pos);

		f = fopen(fname, "w");
		if (f == NULL)
			return -1;

		sigma_dut_print(dut, DUT_MSG_DEBUG, "Use ath6kl delete_qos");
		fprintf(f, "2 4\n");
		fclose(f);
	}
#endif /* __linux__ */

	return 0;
}


static int cmd_sta_set_uapsd(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	/* const char *ssid = get_param(cmd, "ssid"); */
	const char *val;
	int max_sp_len = 4;
	int ac_be = 1, ac_bk = 1, ac_vi = 1, ac_vo = 1;
	char buf[100];
	int ret1, ret2;

	val = get_param(cmd, "maxSPLength");
	if (val) {
		max_sp_len = atoi(val);
		if (max_sp_len != 0 && max_sp_len != 1 && max_sp_len != 2 &&
		    max_sp_len != 4)
			return -1;
	}

	val = get_param(cmd, "acBE");
	if (val)
		ac_be = atoi(val);

	val = get_param(cmd, "acBK");
	if (val)
		ac_bk = atoi(val);

	val = get_param(cmd, "acVI");
	if (val)
		ac_vi = atoi(val);

	val = get_param(cmd, "acVO");
	if (val)
		ac_vo = atoi(val);

	dut->client_uapsd = ac_be || ac_bk || ac_vi || ac_vo;

	snprintf(buf, sizeof(buf), "P2P_SET client_apsd %d,%d,%d,%d;%d",
		 ac_be, ac_bk, ac_vi, ac_vo, max_sp_len);
	ret1 = wpa_command(intf, buf);

	snprintf(buf, sizeof(buf), "SET uapsd %d,%d,%d,%d;%d",
		 ac_be, ac_bk, ac_vi, ac_vo, max_sp_len);
	ret2 = wpa_command(intf, buf);

	if (ret1 && ret2) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to set client mode "
				"UAPSD parameters.");
		return -2;
	}

	if (ath6kl_client_uapsd(dut, intf, dut->client_uapsd) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to set ath6kl QoS parameters");
		return 0;
	}

	return 1;
}


static int cmd_sta_set_wmm(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	char buf[1000];
	const char *intf = get_param(cmd, "Interface");
	const char *grp = get_param(cmd, "Group");
	const char *act = get_param(cmd, "Action");
	const char *tid = get_param(cmd, "Tid");
	const char *dir = get_param(cmd, "Direction");
	const char *psb = get_param(cmd, "Psb");
	const char *up = get_param(cmd, "Up");
	const char *fixed = get_param(cmd, "Fixed");
	const char *size = get_param(cmd, "Size");
	const char *msize = get_param(cmd, "Maxsize");
	const char *minsi = get_param(cmd, "Min_srvc_intrvl");
	const char *maxsi = get_param(cmd, "Max_srvc_intrvl");
	const char *inact = get_param(cmd, "Inactivity");
	const char *sus = get_param(cmd, "Suspension");
	const char *mindr = get_param(cmd, "Mindatarate");
	const char *meandr = get_param(cmd, "Meandatarate");
	const char *peakdr = get_param(cmd, "Peakdatarate");
	const char *phyrate = get_param(cmd, "Phyrate");
	const char *burstsize = get_param(cmd, "Burstsize");
	const char *sba = get_param(cmd, "Sba");
	int direction;
	int handle;
	float sba_fv;
	int fixed_int;
	int psb_ts;

	if (intf == NULL || grp == NULL || act == NULL )
		return -1;

	if (strcasecmp(act, "addts") == 0) {
		if (tid == NULL || dir == NULL || psb == NULL ||
		    up == NULL || fixed == NULL || size == NULL)
			return -1;

		/*
		 * Note: Sigma CAPI spec lists uplink, downlink, and bidi as the
		 * possible values, but WMM-AC and V-E test scripts use "UP,
		 * "DOWN", and "BIDI".
		 */
		if (strcasecmp(dir, "uplink") == 0 ||
		    strcasecmp(dir, "up") == 0) {
			direction = 0;
		} else if (strcasecmp(dir, "downlink") == 0 ||
			   strcasecmp(dir, "down") == 0) {
			direction = 1;
		} else if (strcasecmp(dir, "bidi") == 0) {
			direction = 2;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Direction %s not supported", dir);
			return -1;
		}

		if (strcasecmp(psb, "legacy") == 0) {
			psb_ts = 0;
		} else if (strcasecmp(psb, "uapsd") == 0) {
			psb_ts = 1;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"PSB %s not supported", psb);
			return -1;
		}

		if (atoi(tid) < 0 || atoi(tid) > 7) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"TID %s not supported", tid);
			return -1;
		}

		if (strcasecmp(fixed, "true") == 0) {
			fixed_int = 1;
		} else {
			fixed_int = 0;
		}

		sba_fv = atof(sba);

		dut->dialog_token++;
		handle = 7000 + dut->dialog_token;

		/*
		 * size: convert to hex
		 * maxsi: convert to hex
		 * mindr: convert to hex
		 * meandr: convert to hex
		 * peakdr: convert to hex
		 * burstsize: convert to hex
		 * phyrate: convert to hex
		 * sba: convert to hex with modification
		 * minsi: convert to integer
		 * sus: convert to integer
		 * inact: convert to integer
		 * maxsi: convert to integer
		 */

		/*
		 * The Nominal MSDU Size field is 2 octets long and contains an
		 * unsigned integer that specifies the nominal size, in octets,
		 * of MSDUs belonging to the traffic under this traffic
		 * specification and is defined in Figure 16. If the Fixed
		 * subfield is set to 1, then the size of the MSDU is fixed and
		 * is indicated by the Size Subfield. If the Fixed subfield is
		 * set to 0, then the size of the MSDU might not be fixed and
		 * the Size indicates the nominal MSDU size.
		 *
		 * The Surplus Bandwidth Allowance Factor field is 2 octets long
		 * and specifies the excess allocation of time (and bandwidth)
		 * over and above the stated rates required to transport an MSDU
		 * belonging to the traffic in this TSPEC. This field is
		 * represented as an unsigned binary number with an implicit
		 * binary point after the leftmost 3 bits. For example, an SBA
		 * of 1.75 is represented as 0x3800. This field is included to
		 * account for retransmissions. As such, the value of this field
		 * must be greater than unity.
		 */

		snprintf(buf, sizeof(buf),
			 "iwpriv %s addTspec %d %s %d %d %s 0x%X"
			 " 0x%X 0x%X 0x%X"
			 " 0x%X 0x%X 0x%X"
			 " 0x%X %d %d %d %d"
			 " %d %d",
			 intf, handle, tid, direction, psb_ts, up,
			 (unsigned int) ((fixed_int << 15) | atoi(size)),
			 msize ? atoi(msize) : 0,
			 mindr ? atoi(mindr) : 0,
			 meandr ? atoi(meandr) : 0,
			 peakdr ? atoi(peakdr) : 0,
			 burstsize ? atoi(burstsize) : 0,
			 phyrate ? atoi(phyrate) : 0,
			 sba ? ((unsigned int) (((int) sba_fv << 13) |
						(int)((sba_fv - (int) sba_fv) *
						      8192))) : 0,
			 minsi ? atoi(minsi) : 0,
			 sus ? atoi(sus) : 0,
			 0, 0,
			 inact ? atoi(inact) : 0,
			 maxsi ? atoi(maxsi) : 0);

		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv addtspec request failed");
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to execute addTspec command");
			return 0;
		}

		sigma_dut_print(dut, DUT_MSG_INFO,
				"iwpriv addtspec request send");

		/* Mapping handle to a TID */
		dut->tid_to_handle[atoi(tid)] = handle;
	} else if (strcasecmp(act, "delts") == 0) {
		if (tid == NULL)
			return -1;

		if (atoi(tid) < 0 || atoi(tid) > 7) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"TID %s not supported", tid);
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported TID");
			return 0;
		}

		handle = dut->tid_to_handle[atoi(tid)];

		if (handle < 7000 || handle > 7255) {
			/* Invalid handle ie no mapping for that TID */
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"handle-> %d not found", handle);
		}

		snprintf(buf, sizeof(buf), "iwpriv %s delTspec %d",
			 intf, handle);

		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv deltspec request failed");
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to execute delTspec command");
			return 0;
		}

		sigma_dut_print(dut, DUT_MSG_INFO,
				"iwpriv deltspec request send");

		dut->tid_to_handle[atoi(tid)] = 0;
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Action type %s not supported", act);
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported Action");
		return 0;
	}

	return 1;
}


static int cmd_sta_associate(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *ssid = get_param(cmd, "ssid");
	const char *wps_param = get_param(cmd, "WPS");
	const char *bssid = get_param(cmd, "bssid");
	const char *chan = get_param(cmd, "channel");
	int wps = 0;
	char buf[1000], extra[50];

	if (ssid == NULL)
		return -1;

	if (dut->rsne_override) {
		snprintf(buf, sizeof(buf), "TEST_ASSOC_IE %s",
			 dut->rsne_override);
		if (wpa_command(get_station_ifname(), buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set DEV_CONFIGURE_IE RSNE override");
			return 0;
		}
	}

	if (wps_param &&
	    (strcmp(wps_param, "1") == 0 || strcasecmp(wps_param, "On") == 0))
		wps = 1;

	if (wps) {
		if (dut->wps_method == WFA_CS_WPS_NOT_READY) {
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,WPS "
				  "parameters not yet set");
			return 0;
		}
		if (dut->wps_method == WFA_CS_WPS_PBC) {
			if (wpa_command(get_station_ifname(), "WPS_PBC") < 0)
				return -2;
		} else {
			snprintf(buf, sizeof(buf), "WPS_PIN any %s",
				 dut->wps_pin);
			if (wpa_command(get_station_ifname(), buf) < 0)
				return -2;
		}
	} else {
		if (strcmp(ssid, dut->infra_ssid) != 0) {
			printf("No network parameters known for network "
			       "(ssid='%s')", ssid);
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,"
				  "No network parameters known for network");
			return 0;
		}

		if (bssid &&
		    set_network(get_station_ifname(), dut->infra_network_id,
				"bssid", bssid) < 0) {
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,"
				  "Invalid bssid argument");
			return 0;
		}

		extra[0] = '\0';
		if (chan)
			snprintf(extra, sizeof(extra), " freq=%u",
				 channel_to_freq(atoi(chan)));
		snprintf(buf, sizeof(buf), "SELECT_NETWORK %d%s",
			 dut->infra_network_id, extra);
		if (wpa_command(get_station_ifname(), buf) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Failed to select "
					"network id %d on %s",
					dut->infra_network_id,
					get_station_ifname());
			return -2;
		}
	}

	return 1;
}


static int run_hs20_osu(struct sigma_dut *dut, const char *params)
{
	char buf[500], cmd[200];
	int res;

	/* Use hs20-osu-client file at the current dir, if found; otherwise use
	 * default path */
	res = snprintf(cmd, sizeof(cmd),
		       "%s -w \"%s\" -r hs20-osu-client.res %s%s -dddKt -f Logs/hs20-osu-client.txt",
		       file_exists("./hs20-osu-client") ?
		       "./hs20-osu-client" : "hs20-osu-client",
		       sigma_wpas_ctrl,
		       dut->summary_log ? "-s " : "",
		       dut->summary_log ? dut->summary_log : "");
	if (res < 0 || res >= (int) sizeof(cmd))
		return -1;

	res = snprintf(buf, sizeof(buf), "%s %s", cmd, params);
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);

	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to run: %s", buf);
		return -1;
	}
	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Completed hs20-osu-client operation");

	return 0;
}


static int download_ppsmo(struct sigma_dut *dut,
			  struct sigma_conn *conn,
			  const char *intf,
			  struct sigma_cmd *cmd)
{
	const char *name, *path, *val;
	char url[500], buf[600], fbuf[100];
	char *fqdn = NULL;

	name = get_param(cmd, "FileName");
	path = get_param(cmd, "FilePath");
	if (name == NULL || path == NULL)
		return -1;

	if (strcasecmp(path, "VendorSpecific") == 0) {
		snprintf(url, sizeof(url), "PPS/%s", name);
		sigma_dut_print(dut, DUT_MSG_INFO, "Use pre-configured PPS MO "
				"from the device (%s)", url);
		if (!file_exists(url)) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Requested "
				  "PPS MO file does not exist");
			return 0;
		}
		snprintf(buf, sizeof(buf), "cp %s pps-tnds.xml", url);
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to copy PPS MO");
			return 0;
		}
	} else if (strncasecmp(path, "http:", 5) != 0 &&
		   strncasecmp(path, "https:", 6) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,"
			  "Unsupported FilePath value");
		return 0;
	} else {
		snprintf(url, sizeof(url), "%s/%s", path, name);
		sigma_dut_print(dut, DUT_MSG_INFO, "Downloading PPS MO from %s",
				url);
		snprintf(buf, sizeof(buf), "wget -T 10 -t 3 -O pps-tnds.xml '%s'", url);
		remove("pps-tnds.xml");
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to download PPS MO");
			return 0;
		}
	}

	if (run_hs20_osu(dut, "from_tnds pps-tnds.xml pps.xml") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to parse downloaded PPSMO");
		return 0;
	}
	unlink("pps-tnds.xml");

	val = get_param(cmd, "managementTreeURI");
	if (val) {
		const char *pos, *end;
		sigma_dut_print(dut, DUT_MSG_DEBUG, "managementTreeURI: %s",
				val);
		if (strncmp(val, "./Wi-Fi/", 8) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid managementTreeURI prefix");
			return 0;
		}
		pos = val + 8;
		end = strchr(pos, '/');
		if (end == NULL ||
		    strcmp(end, "/PerProviderSubscription") != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid managementTreeURI postfix");
			return 0;
		}
		if (end - pos >= (int) sizeof(fbuf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Too long FQDN in managementTreeURI");
			return 0;
		}
		memcpy(fbuf, pos, end - pos);
		fbuf[end - pos] = '\0';
		fqdn = fbuf;
		sigma_dut_print(dut, DUT_MSG_INFO,
				"FQDN from managementTreeURI: %s", fqdn);
	} else if (run_hs20_osu(dut, "get_fqdn pps.xml") == 0) {
		FILE *f = fopen("pps-fqdn", "r");
		if (f) {
			if (fgets(fbuf, sizeof(fbuf), f)) {
				fbuf[sizeof(fbuf) - 1] = '\0';
				fqdn = fbuf;
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"Use FQDN %s", fqdn);
			}
			fclose(f);
		}
	}

	if (fqdn == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,No FQDN specified");
		return 0;
	}

	mkdir("SP", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	snprintf(buf, sizeof(buf), "SP/%s", fqdn);
	mkdir(buf, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

	snprintf(buf, sizeof(buf), "SP/%s/pps.xml", fqdn);
	if (rename("pps.xml", buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Could not move PPS MO");
		return 0;
	}

	if (strcasecmp(path, "VendorSpecific") == 0) {
		snprintf(buf, sizeof(buf), "cp Certs/ca.pem SP/%s/ca.pem",
			 fqdn);
		if (system(buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to copy OSU CA cert");
			return 0;
		}

		snprintf(buf, sizeof(buf),
			 "cp Certs/aaa-ca.pem SP/%s/aaa-ca.pem",
			 fqdn);
		if (system(buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to copy AAA CA cert");
			return 0;
		}
	} else {
		snprintf(buf, sizeof(buf),
			 "dl_osu_ca SP/%s/pps.xml SP/%s/ca.pem",
			 fqdn, fqdn);
		if (run_hs20_osu(dut, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to download OSU CA cert");
			return 0;
		}

		snprintf(buf, sizeof(buf),
			 "dl_aaa_ca SP/%s/pps.xml SP/%s/aaa-ca.pem",
			 fqdn, fqdn);
		if (run_hs20_osu(dut, buf) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to download AAA CA cert");
		}
	}

	if (file_exists("next-client-cert.pem")) {
		snprintf(buf, sizeof(buf), "SP/%s/client-cert.pem", fqdn);
		if (rename("next-client-cert.pem", buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Could not move client certificate");
			return 0;
		}
	}

	if (file_exists("next-client-key.pem")) {
		snprintf(buf, sizeof(buf), "SP/%s/client-key.pem", fqdn);
		if (rename("next-client-key.pem", buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Could not move client key");
			return 0;
		}
	}

	snprintf(buf, sizeof(buf), "set_pps SP/%s/pps.xml", fqdn);
	if (run_hs20_osu(dut, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to configure credential from "
			  "PPSMO");
		return 0;
	}

	return 1;
}


static int download_cert(struct sigma_dut *dut,
			 struct sigma_conn *conn,
			 const char *intf,
			 struct sigma_cmd *cmd)
{
	const char *name, *path;
	char url[500], buf[600];

	name = get_param(cmd, "FileName");
	path = get_param(cmd, "FilePath");
	if (name == NULL || path == NULL)
		return -1;

	if (strcasecmp(path, "VendorSpecific") == 0) {
		snprintf(url, sizeof(url), "Certs/%s-cert.pem", name);
		sigma_dut_print(dut, DUT_MSG_INFO, "Use pre-configured client "
				"certificate from the device (%s)", url);
		if (!file_exists(url)) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Requested "
				  "certificate file does not exist");
			return 0;
		}
		snprintf(buf, sizeof(buf), "cp %s next-client-cert.pem", url);
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to copy client "
				  "certificate");
			return 0;
		}

		snprintf(url, sizeof(url), "Certs/%s-key.pem", name);
		sigma_dut_print(dut, DUT_MSG_INFO, "Use pre-configured client "
				"private key from the device (%s)", url);
		if (!file_exists(url)) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Requested "
				  "private key file does not exist");
			return 0;
		}
		snprintf(buf, sizeof(buf), "cp %s next-client-key.pem", url);
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to copy client key");
			return 0;
		}
	} else if (strncasecmp(path, "http:", 5) != 0 &&
		   strncasecmp(path, "https:", 6) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,"
			  "Unsupported FilePath value");
		return 0;
	} else {
		snprintf(url, sizeof(url), "%s/%s.pem", path, name);
		sigma_dut_print(dut, DUT_MSG_INFO, "Downloading client "
				"certificate/key from %s", url);
		snprintf(buf, sizeof(buf),
			 "wget -T 10 -t 3 -O next-client-cert.pem '%s'", url);
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to download client "
				  "certificate");
			return 0;
		}

		if (system("cp next-client-cert.pem next-client-key.pem") != 0)
		{
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to copy client key");
			return 0;
		}
	}

	return 1;
}


static int cmd_sta_preset_testparameters_hs2_r2(struct sigma_dut *dut,
						struct sigma_conn *conn,
						const char *intf,
						struct sigma_cmd *cmd)
{
	const char *val;

	val = get_param(cmd, "FileType");
	if (val && strcasecmp(val, "PPSMO") == 0)
		return download_ppsmo(dut, conn, intf, cmd);
	if (val && strcasecmp(val, "CERT") == 0)
		return download_cert(dut, conn, intf, cmd);
	if (val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported FileType");
		return 0;
	}

	return 1;
}


static void ath_sta_set_noack(struct sigma_dut *dut, const char *intf,
			      const char *val)
{
	int counter = 0;
	char token[50];
	char *result;
	char buf[100];
	char *saveptr;

	strlcpy(token, val, sizeof(token));
	token[sizeof(token) - 1] = '\0';
	result = strtok_r(token, ":", &saveptr);
	while (result) {
		if (strcmp(result, "disable") == 0) {
			snprintf(buf, sizeof(buf),
				 "iwpriv %s noackpolicy %d 1 0",
				 intf, counter);
		} else {
			snprintf(buf, sizeof(buf),
				 "iwpriv %s noackpolicy %d 1 1",
				 intf, counter);
		}
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv noackpolicy failed");
		}
		result = strtok_r(NULL, ":", &saveptr);
		counter++;
	}
}


static void ath_sta_set_rts(struct sigma_dut *dut, const char *intf,
			    const char *val)
{
	char buf[100];

	snprintf(buf, sizeof(buf), "iwconfig %s rts %s", intf, val);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwconfig RTS failed");
	}
}


static void ath_sta_set_wmm(struct sigma_dut *dut, const char *intf,
			    const char *val)
{
	char buf[100];

	if (strcasecmp(val, "off") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s wmm 0", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to turn off WMM");
		}
	}
}


static void ath_sta_set_sgi(struct sigma_dut *dut, const char *intf,
			    const char *val)
{
	char buf[100];
	int sgi20;

	sgi20 = strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0;

	snprintf(buf, sizeof(buf), "iwpriv %s shortgi %d", intf, sgi20);
	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv shortgi failed");
}


static void ath_sta_set_11nrates(struct sigma_dut *dut, const char *intf,
				 const char *val)
{
	char buf[100];
	int rate_code, v;

	/* Disable Tx Beam forming when using a fixed rate */
	ath_disable_txbf(dut, intf);

	v = atoi(val);
	if (v < 0 || v > 32) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Invalid Fixed MCS rate: %d", v);
		return;
	}
	rate_code = 0x80 + v;

	snprintf(buf, sizeof(buf), "iwpriv %s set11NRates 0x%x",
		 intf, rate_code);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv set11NRates failed");
	}

	/* Channel width gets messed up, fix this */
	snprintf(buf, sizeof(buf), "iwpriv %s chwidth %d", intf, dut->chwidth);
	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv chwidth failed");
}


static void ath_sta_set_amsdu(struct sigma_dut *dut, const char *intf,
			      const char *val)
{
	char buf[60];

	if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0)
		snprintf(buf, sizeof(buf), "iwpriv %s amsdu 2", intf);
	else
		snprintf(buf, sizeof(buf), "iwpriv %s amsdu 1", intf);

	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv amsdu failed");
}


static int iwpriv_sta_set_ampdu(struct sigma_dut *dut, const char *intf,
				int ampdu)
{
	char buf[60];

	snprintf(buf, sizeof(buf), "iwpriv %s ampdu %d", intf, ampdu);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv ampdu failed");
		return -1;
	}

	return 0;
}


static void ath_sta_set_stbc(struct sigma_dut *dut, const char *intf,
			     const char *val)
{
	char buf[60];

	snprintf(buf, sizeof(buf), "iwpriv %s tx_stbc %s", intf, val);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv tx_stbc failed");
	}

	snprintf(buf, sizeof(buf), "iwpriv %s rx_stbc %s", intf, val);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv rx_stbc failed");
	}
}


static int wcn_sta_set_cts_width(struct sigma_dut *dut, const char *intf,
				  const char *val)
{
	char buf[60];

	if (strcmp(val, "160") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s cts_cbw 5", intf);
	} else if (strcmp(val, "80") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s cts_cbw 3", intf);
	} else if (strcmp(val, "40") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s cts_cbw 2", intf);
	} else if (strcmp(val, "20") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s cts_cbw 1", intf);
	} else if (strcasecmp(val, "Auto") == 0) {
		buf[0] = '\0';
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"WIDTH/CTS_WIDTH value not supported");
		return -1;
	}

	if (buf[0] != '\0' && system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set WIDTH/CTS_WIDTH");
		return -1;
	}

	return 0;
}


int ath_set_width(struct sigma_dut *dut, struct sigma_conn *conn,
		  const char *intf, const char *val)
{
	char buf[60];

	if (strcasecmp(val, "Auto") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s chwidth 0", intf);
		dut->chwidth = 0;
	} else if (strcasecmp(val, "20") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s chwidth 0", intf);
		dut->chwidth = 0;
	} else if (strcasecmp(val, "40") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s chwidth 1", intf);
		dut->chwidth = 1;
	} else if (strcasecmp(val, "80") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s chwidth 2", intf);
		dut->chwidth = 2;
	} else if (strcasecmp(val, "160") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s chwidth 3", intf);
		dut->chwidth = 3;
	} else {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,WIDTH not supported");
		return -1;
	}

	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv chwidth failed");
	}

	return 0;
}


static int wcn_sta_set_sp_stream(struct sigma_dut *dut, const char *intf,
				 const char *val)
{
	char buf[60];

	if (strcmp(val, "1SS") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s nss 1", intf);
	} else if (strcmp(val, "2SS") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s nss 2", intf);
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"SP_STREAM value not supported");
		return -1;
	}

	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set SP_STREAM");
		return -1;
	}

	return 0;
}


static void wcn_sta_set_stbc(struct sigma_dut *dut, const char *intf,
			     const char *val)
{
	char buf[60];

	snprintf(buf, sizeof(buf), "iwpriv %s tx_stbc %s", intf, val);
	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv tx_stbc failed");

	snprintf(buf, sizeof(buf), "iwpriv %s rx_stbc %s", intf, val);
	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv rx_stbc failed");
}


static int mbo_set_cellular_data_capa(struct sigma_dut *dut,
				      struct sigma_conn *conn,
				      const char *intf, int capa)
{
	char buf[32];

	if (capa > 0 && capa < 4) {
		snprintf(buf, sizeof(buf), "SET mbo_cell_capa %d", capa);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode, Failed to set cellular data capability");
			return 0;
		}
		return 1;
	}

	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Invalid Cellular data capability: %d", capa);
	send_resp(dut, conn, SIGMA_INVALID,
		  "ErrorCode,Invalid cellular data capability");
	return 0;
}


static int mbo_set_roaming(struct sigma_dut *dut, struct sigma_conn *conn,
			   const char *intf, const char *val)
{
	if (strcasecmp(val, "Disable") == 0) {
		if (wpa_command(intf, "SET roaming 0") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to disable roaming");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(val, "Enable") == 0) {
		if (wpa_command(intf, "SET roaming 1") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to enable roaming");
			return 0;
		}
		return 1;
	}

	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Invalid value provided for roaming: %s", val);
	send_resp(dut, conn, SIGMA_INVALID,
		  "ErrorCode,Unknown value provided for Roaming");
	return 0;
}


static int mbo_set_assoc_disallow(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  const char *intf, const char *val)
{
	if (strcasecmp(val, "Disable") == 0) {
		if (wpa_command(intf, "SET ignore_assoc_disallow 1") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to disable Assoc_disallow");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(val, "Enable") == 0) {
		if (wpa_command(intf, "SET ignore_assoc_disallow 0") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to enable Assoc_disallow");
			return 0;
		}
		return 1;
	}

	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Invalid value provided for Assoc_disallow: %s", val);
	send_resp(dut, conn, SIGMA_INVALID,
		  "ErrorCode,Unknown value provided for Assoc_disallow");
	return 0;
}


static int mbo_set_bss_trans_req(struct sigma_dut *dut, struct sigma_conn *conn,
				 const char *intf, const char *val)
{
	if (strcasecmp(val, "Reject") == 0) {
		if (wpa_command(intf, "SET reject_btm_req_reason 1") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to Reject BTM Request");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(val, "Accept") == 0) {
		if (wpa_command(intf, "SET reject_btm_req_reason 0") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to Accept BTM Request");
			return 0;
		}
		return 1;
	}

	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Invalid value provided for BSS_Transition: %s", val);
	send_resp(dut, conn, SIGMA_INVALID,
		  "ErrorCode,Unknown value provided for BSS_Transition");
	return 0;
}


static int mbo_set_non_pref_ch_list(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    const char *intf,
				    struct sigma_cmd *cmd)
{
	const char *ch, *pref, *op_class, *reason;
	char buf[120];
	int len, ret;

	pref = get_param(cmd, "Ch_Pref");
	if (!pref)
		return 1;

	if (strcasecmp(pref, "clear") == 0) {
		free(dut->non_pref_ch_list);
		dut->non_pref_ch_list = NULL;
	} else {
		op_class = get_param(cmd, "Ch_Op_Class");
		if (!op_class) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "ErrorCode,Ch_Op_Class not provided");
			return 0;
		}

		ch = get_param(cmd, "Ch_Pref_Num");
		if (!ch) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "ErrorCode,Ch_Pref_Num not provided");
			return 0;
		}

		reason = get_param(cmd, "Ch_Reason_Code");
		if (!reason) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "ErrorCode,Ch_Reason_Code not provided");
			return 0;
		}

		if (!dut->non_pref_ch_list) {
			dut->non_pref_ch_list =
				calloc(1, NON_PREF_CH_LIST_SIZE);
			if (!dut->non_pref_ch_list) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to allocate memory for non_pref_ch_list");
				return 0;
			}
		}
		len = strlen(dut->non_pref_ch_list);
		ret = snprintf(dut->non_pref_ch_list + len,
			       NON_PREF_CH_LIST_SIZE - len,
			       " %s:%s:%s:%s", op_class, ch, pref, reason);
		if (ret > 0 && ret < NON_PREF_CH_LIST_SIZE - len) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "non_pref_list: %s",
					dut->non_pref_ch_list);
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"snprintf failed for non_pref_list, ret = %d",
					ret);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,snprintf failed");
			free(dut->non_pref_ch_list);
			dut->non_pref_ch_list = NULL;
			return 0;
		}
	}

	ret = snprintf(buf, sizeof(buf), "SET non_pref_chan%s",
		       dut->non_pref_ch_list ? dut->non_pref_ch_list : " ");
	if (ret < 0 || ret >= (int) sizeof(buf)) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"snprintf failed for set non_pref_chan, ret: %d",
				ret);
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,snprint failed");
		return 0;
	}

	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to set non-preferred channel list");
		return 0;
	}

	return 1;
}


static int cmd_sta_preset_testparameters(struct sigma_dut *dut,
					 struct sigma_conn *conn,
					 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;

	val = get_param(cmd, "Program");
	if (val && strcasecmp(val, "HS2-R2") == 0)
		return cmd_sta_preset_testparameters_hs2_r2(dut, conn, intf,
							    cmd);

	if (val && strcasecmp(val, "LOC") == 0)
		return loc_cmd_sta_preset_testparameters(dut, conn, cmd);

#ifdef ANDROID_NAN
	if (val && strcasecmp(val, "NAN") == 0)
		return nan_cmd_sta_preset_testparameters(dut, conn, cmd);
#endif /* ANDROID_NAN */
#ifdef MIRACAST
	if (val && (strcasecmp(val, "WFD") == 0 ||
		    strcasecmp(val, "DisplayR2") == 0))
		return miracast_preset_testparameters(dut, conn, cmd);
#endif /* MIRACAST */

	if (val && strcasecmp(val, "MBO") == 0) {
		val = get_param(cmd, "Cellular_Data_Cap");
		if (val &&
		    mbo_set_cellular_data_capa(dut, conn, intf, atoi(val)) == 0)
			return 0;

		val = get_param(cmd, "Ch_Pref");
		if (val && mbo_set_non_pref_ch_list(dut, conn, intf, cmd) == 0)
			return 0;

		val = get_param(cmd, "BSS_Transition");
		if (val && mbo_set_bss_trans_req(dut, conn, intf, val) == 0)
			return 0;

		val = get_param(cmd, "Assoc_Disallow");
		if (val && mbo_set_assoc_disallow(dut, conn, intf, val) == 0)
			return 0;

		val = get_param(cmd, "Roaming");
		if (val && mbo_set_roaming(dut, conn, intf, val) == 0)
			return 0;

		return 1;
	}

#if 0
	val = get_param(cmd, "Supplicant");
	if (val && strcasecmp(val, "Default") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Only default(Vendor) supplicant "
			  "supported");
		return 0;
	}
#endif

	val = get_param(cmd, "RTS");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_ATHEROS:
			ath_sta_set_rts(dut, intf, val);
			break;
		default:
#if 0
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Setting RTS not supported");
			return 0;
#else
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Setting RTS not supported");
			break;
#endif
		}
	}

#if 0
	val = get_param(cmd, "FRGMNT");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Setting FRGMNT not supported");
		return 0;
	}
#endif

#if 0
	val = get_param(cmd, "Preamble");
	if (val) {
		/* TODO: Long/Short */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Setting Preamble not supported");
		return 0;
	}
#endif

	val = get_param(cmd, "Mode");
	if (val) {
		if (strcmp(val, "11b") == 0 ||
		    strcmp(val, "11g") == 0 ||
		    strcmp(val, "11a") == 0 ||
		    strcmp(val, "11n") == 0 ||
		    strcmp(val, "11ng") == 0 ||
		    strcmp(val, "11nl") == 0 ||
		    strcmp(val, "11nl(nabg)") == 0 ||
		    strcmp(val, "AC") == 0 ||
		    strcmp(val, "11AC") == 0 ||
		    strcmp(val, "11ac") == 0 ||
		    strcmp(val, "11na") == 0 ||
		    strcmp(val, "11an") == 0) {
			/* STA supports all modes by default */
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Setting Mode not supported");
			return 0;
		}
	}

	val = get_param(cmd, "wmm");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_ATHEROS:
			ath_sta_set_wmm(dut, intf, val);
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Setting wmm not supported");
			break;
		}
	}

	val = get_param(cmd, "Powersave");
	if (val) {
		if (strcmp(val, "0") == 0 || strcasecmp(val, "off") == 0) {
			if (wpa_command(get_station_ifname(),
					"P2P_SET ps 0") < 0)
				return -2;
			/* Make sure test modes are disabled */
			wpa_command(get_station_ifname(), "P2P_SET ps 98");
			wpa_command(get_station_ifname(), "P2P_SET ps 96");
		} else if (strcmp(val, "1") == 0 ||
			   strcasecmp(val, "PSPoll") == 0 ||
			   strcasecmp(val, "on") == 0) {
			/* Disable default power save mode */
			wpa_command(get_station_ifname(), "P2P_SET ps 0");
			/* Enable PS-Poll test mode */
			if (wpa_command(get_station_ifname(),
					"P2P_SET ps 97") < 0 ||
			    wpa_command(get_station_ifname(),
					"P2P_SET ps 99") < 0)
				return -2;
		} else if (strcmp(val, "2") == 0 ||
			   strcasecmp(val, "Fast") == 0) {
			/* TODO */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Powersave=Fast not supported");
			return 0;
		} else if (strcmp(val, "3") == 0 ||
			   strcasecmp(val, "PSNonPoll") == 0) {
			/* Make sure test modes are disabled */
			wpa_command(get_station_ifname(), "P2P_SET ps 98");
			wpa_command(get_station_ifname(), "P2P_SET ps 96");

			/* Enable default power save mode */
			if (wpa_command(get_station_ifname(),
					"P2P_SET ps 1") < 0)
				return -2;
		} else
			return -1;
	}

	val = get_param(cmd, "NoAck");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_ATHEROS:
			ath_sta_set_noack(dut, intf, val);
			break;
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Setting NoAck not supported");
			return 0;
		}
	}

	val = get_param(cmd, "IgnoreChswitchProhibit");
	if (val) {
		/* TODO: Enabled/disabled */
		if (strcasecmp(val, "Enabled") == 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Enabling IgnoreChswitchProhibit "
				  "not supported");
			return 0;
		}
	}

	val = get_param(cmd, "TDLS");
	if (val) {
		if (strcasecmp(val, "Disabled") == 0) {
			if (wpa_command(intf, "SET tdls_disabled 1")) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to disable TDLS");
				return 0;
			}
		} else if (strcasecmp(val, "Enabled") == 0) {
			if (wpa_command(intf, "SET tdls_disabled 0")) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to enable TDLS");
				return 0;
			}
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported TDLS value");
			return 0;
		}
	}

	val = get_param(cmd, "TDLSmode");
	if (val) {
		if (strcasecmp(val, "Default") == 0) {
			wpa_command(intf, "SET tdls_testing 0");
		} else if (strcasecmp(val, "APProhibit") == 0) {
			if (wpa_command(intf, "SET tdls_testing 0x400")) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to enable ignore "
					  "APProhibit TDLS mode");
				return 0;
			}
		} else if (strcasecmp(val, "HiLoMac") == 0) {
			/* STA should respond with TDLS setup req for a TDLS
			 * setup req */
			if (wpa_command(intf, "SET tdls_testing 0x80")) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to enable HiLoMac "
					  "TDLS mode");
				return 0;
			}
		} else if (strcasecmp(val, "WeakSecurity") == 0) {
			/*
			 * Since all security modes are enabled by default when
			 * Sigma control is used, there is no need to do
			 * anything here.
			 */
		} else if (strcasecmp(val, "ExistLink") == 0) {
			/*
			 * Since we allow new TDLS Setup Request even if there
			 * is an existing link, nothing needs to be done for
			 * this.
			 */
		} else {
			/* TODO:
			 * ExistLink: STA should send TDLS setup req even if
			 * direct link already exists
			 */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported TDLSmode value");
			return 0;
		}
	}

	val = get_param(cmd, "FakePubKey");
	if (val && atoi(val) && wpa_command(intf, "SET wps_corrupt_pkhash 1")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to enable FakePubKey");
		return 0;
	}

	return 1;
}


static const char * ath_get_radio_name(const char *radio_name)
{
	if (radio_name == NULL)
		return "wifi0";
	if (strcmp(radio_name, "wifi1") == 0)
		return "wifi1";
	if (strcmp(radio_name, "wifi2") == 0)
		return "wifi2";
	return "wifi0";
}


static void ath_sta_set_txsp_stream(struct sigma_dut *dut, const char *intf,
				    const char *val)
{
	char buf[60];
	unsigned int vht_mcsmap = 0;
	int txchainmask = 0;
	const char *basedev = ath_get_radio_name(sigma_radio_ifname[0]);

	if (strcasecmp(val, "1") == 0 || strcasecmp(val, "1SS") == 0) {
		if (dut->testbed_flag_txsp == 1) {
			vht_mcsmap = 0xfffc;
			dut->testbed_flag_txsp = 0;
		} else {
			vht_mcsmap = 0xfffe;
		}
		txchainmask = 1;
	} else if (strcasecmp(val, "2") == 0 || strcasecmp(val, "2SS") == 0) {
		if (dut->testbed_flag_txsp == 1) {
			vht_mcsmap = 0xfff0;
			dut->testbed_flag_txsp = 0;
		} else {
			vht_mcsmap = 0xfffa;
		}
		txchainmask = 3;
	} else if (strcasecmp(val, "3") == 0 || strcasecmp(val, "3SS") == 0) {
		if (dut->testbed_flag_txsp == 1) {
			vht_mcsmap = 0xffc0;
			dut->testbed_flag_txsp = 0;
		} else {
			vht_mcsmap = 0xffea;
		}
		txchainmask = 7;
	} else if (strcasecmp(val, "4") == 0 || strcasecmp(val, "4SS") == 0) {
		if (dut->testbed_flag_txsp == 1) {
			vht_mcsmap = 0xff00;
			dut->testbed_flag_txsp = 0;
		} else {
			vht_mcsmap = 0xffaa;
		}
		txchainmask = 15;
	} else {
		if (dut->testbed_flag_txsp == 1) {
			vht_mcsmap = 0xffc0;
			dut->testbed_flag_txsp = 0;
		} else {
			vht_mcsmap = 0xffea;
		}
	}

	if (txchainmask) {
		snprintf(buf, sizeof(buf), "iwpriv %s txchainmask %d",
			 basedev, txchainmask);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv txchainmask failed");
		}
	}

	snprintf(buf, sizeof(buf), "iwpriv %s vht_mcsmap 0x%04x",
		 intf, vht_mcsmap);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv %s vht_mcsmap 0x%04x failed",
				intf, vht_mcsmap);
	}
}


static void ath_sta_set_rxsp_stream(struct sigma_dut *dut, const char *intf,
				    const char *val)
{
	char buf[60];
	unsigned int vht_mcsmap = 0;
	int rxchainmask = 0;
	const char *basedev = ath_get_radio_name(sigma_radio_ifname[0]);

	if (strcasecmp(val, "1") == 0 || strcasecmp(val, "1SS") == 0) {
		if (dut->testbed_flag_rxsp == 1) {
			vht_mcsmap = 0xfffc;
			dut->testbed_flag_rxsp = 0;
		} else {
			vht_mcsmap = 0xfffe;
		}
		rxchainmask = 1;
	} else if (strcasecmp(val, "2") == 0 || strcasecmp(val, "2SS") == 0) {
		if (dut->testbed_flag_rxsp == 1) {
			vht_mcsmap = 0xfff0;
			dut->testbed_flag_rxsp = 0;
		} else {
			vht_mcsmap = 0xfffa;
		}
		rxchainmask = 3;
	} else if (strcasecmp(val, "3") == 0 || strcasecmp(val, "3SS") == 0) {
		if (dut->testbed_flag_rxsp == 1) {
			vht_mcsmap = 0xffc0;
			dut->testbed_flag_rxsp = 0;
		} else {
			vht_mcsmap = 0xffea;
		}
		rxchainmask = 7;
	} else if (strcasecmp(val, "4") == 0 || strcasecmp(val, "4SS") == 0) {
		if (dut->testbed_flag_rxsp == 1) {
			vht_mcsmap = 0xff00;
			dut->testbed_flag_rxsp = 0;
		} else {
			vht_mcsmap = 0xffaa;
		}
		rxchainmask = 15;
	} else {
		if (dut->testbed_flag_rxsp == 1) {
			vht_mcsmap = 0xffc0;
			dut->testbed_flag_rxsp = 0;
		} else {
			vht_mcsmap = 0xffea;
		}
	}

	if (rxchainmask) {
		snprintf(buf, sizeof(buf), "iwpriv %s rxchainmask %d",
			 basedev, rxchainmask);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv rxchainmask failed");
		}
	}

	snprintf(buf, sizeof(buf), "iwpriv %s vht_mcsmap 0x%04x",
		 intf, vht_mcsmap);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv %s vht_mcsmap 0x%04x",
				intf, vht_mcsmap);
	}
}


void ath_set_zero_crc(struct sigma_dut *dut, const char *val)
{
	if (strcasecmp(val, "enable") == 0) {
		if (system("athdiag --set --address=0x2a204 --and=0xbfffffff")
		    != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Disable BB_VHTSIGB_CRC_CALC failed");
		}

		if (system("athdiag --set --address=0x2a204 --or=0x80000000")
		    != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Enable FORCE_VHT_SIGB_CRC_VALUE_ZERO failed");
		}
	} else {
		if (system("athdiag --set --address=0x2a204 --and=0x7fffffff")
		    != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Disable FORCE_VHT_SIGB_CRC_VALUE_ZERO failed");
		}

		if (system("athdiag --set --address=0x2a204 --or=0x40000000")
		    != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Enable BB_VHTSIGB_CRC_CALC failed");
		}
	}
}


static int cmd_sta_set_wireless_common(const char *intf, struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd)
{
	const char *val;
	int ampdu = -1;
	char buf[30];

	val = get_param(cmd, "40_INTOLERANT");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* TODO: iwpriv ht40intol through wpa_supplicant */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,40_INTOLERANT not supported");
			return 0;
		}
	}

	val = get_param(cmd, "ADDBA_REJECT");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* reject any ADDBA with status "decline" */
			ampdu = 0;
		} else {
			/* accept ADDBA */
			ampdu = 1;
		}
	}

	val = get_param(cmd, "AMPDU");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* enable AMPDU Aggregation */
			if (ampdu == 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Mismatch in "
					  "addba_reject/ampdu - "
					  "not supported");
				return 0;
			}
			ampdu = 1;
		} else {
			/* disable AMPDU Aggregation */
			if (ampdu == 1) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Mismatch in "
					  "addba_reject/ampdu - "
					  "not supported");
				return 0;
			}
			ampdu = 0;
		}
	}

	if (ampdu >= 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "%s A-MPDU aggregation",
				ampdu ? "Enabling" : "Disabling");
		snprintf(buf, sizeof(buf), "SET ampdu %d", ampdu);
		if (wpa_command(intf, buf) < 0 &&
		    iwpriv_sta_set_ampdu(dut, intf, ampdu) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,set aggr failed");
			return 0;
		}
	}

	val = get_param(cmd, "AMSDU");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_ATHEROS:
			ath_sta_set_amsdu(dut, intf, val);
			break;
		default:
			if (strcmp(val, "1") == 0 ||
			    strcasecmp(val, "Enable") == 0) {
				/* Enable AMSDU Aggregation */
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,AMSDU aggregation not supported");
				return 0;
			}
			break;
		}
	}

	val = get_param(cmd, "STBC_RX");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_ATHEROS:
			ath_sta_set_stbc(dut, intf, val);
			break;
		case DRIVER_WCN:
			wcn_sta_set_stbc(dut, intf, val);
			break;
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,STBC_RX not supported");
			return 0;
		}
	}

	val = get_param(cmd, "WIDTH");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_WCN:
			if (wcn_sta_set_cts_width(dut, intf, val) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set WIDTH");
				return 0;
			}
			break;
		case DRIVER_ATHEROS:
			if (ath_set_width(dut, conn, intf, val) < 0)
				return 0;
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting WIDTH not supported");
			break;
		}
	}

	val = get_param(cmd, "SMPS");
	if (val) {
		/* TODO: Dynamic/0, Static/1, No Limit/2 */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,SMPS not supported");
		return 0;
	}

	val = get_param(cmd, "TXSP_STREAM");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_WCN:
			if (wcn_sta_set_sp_stream(dut, intf, val) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set TXSP_STREAM");
				return 0;
			}
			break;
		case DRIVER_ATHEROS:
			ath_sta_set_txsp_stream(dut, intf, val);
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting TXSP_STREAM not supported");
			break;
		}
	}

	val = get_param(cmd, "RXSP_STREAM");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_WCN:
			if (wcn_sta_set_sp_stream(dut, intf, val) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set RXSP_STREAM");
				return 0;
			}
			break;
		case DRIVER_ATHEROS:
			ath_sta_set_rxsp_stream(dut, intf, val);
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting RXSP_STREAM not supported");
			break;
		}
	}

	val = get_param(cmd, "DYN_BW_SGNL");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_WCN:
			if (strcasecmp(val, "enable") == 0) {
				snprintf(buf, sizeof(buf),
					 "iwpriv %s cwmenable 1", intf);
				if (system(buf) != 0) {
					sigma_dut_print(dut, DUT_MSG_ERROR,
							"iwpriv cwmenable 1 failed");
					return 0;
				}
			} else if (strcasecmp(val, "disable") == 0) {
				snprintf(buf, sizeof(buf),
					 "iwpriv %s cwmenable 0", intf);
				if (system(buf) != 0) {
					sigma_dut_print(dut, DUT_MSG_ERROR,
							"iwpriv cwmenable 0 failed");
					return 0;
				}
			} else {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Unsupported DYN_BW_SGL");
			}

			snprintf(buf, sizeof(buf), "iwpriv %s cts_cbw 3", intf);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to set cts_cbw in DYN_BW_SGNL");
				return 0;
			}
			break;
		case DRIVER_ATHEROS:
			novap_reset(dut, intf);
			ath_config_dyn_bw_sig(dut, intf, val);
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set DYN_BW_SGNL");
			break;
		}
	}

	val = get_param(cmd, "RTS_FORCE");
	if (val) {
		novap_reset(dut, intf);
		if (strcasecmp(val, "Enable") == 0) {
			snprintf(buf, sizeof(buf), "iwconfig %s rts 64", intf);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to set RTS_FORCE 64");
			}
			snprintf(buf, sizeof(buf),
				 "wifitool %s beeliner_fw_test 100 1", intf);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"wifitool beeliner_fw_test 100 1 failed");
			}
		} else if (strcasecmp(val, "Disable") == 0) {
			snprintf(buf, sizeof(buf), "iwconfig %s rts 2347",
				 intf);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to set RTS_FORCE 2347");
			}
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,RTS_FORCE value not supported");
			return 0;
		}
	}

	val = get_param(cmd, "CTS_WIDTH");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_WCN:
			if (wcn_sta_set_cts_width(dut, intf, val) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set CTS_WIDTH");
				return 0;
			}
			break;
		case DRIVER_ATHEROS:
			ath_set_cts_width(dut, intf, val);
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting CTS_WIDTH not supported");
			break;
		}
	}

	val = get_param(cmd, "BW_SGNL");
	if (val) {
		if (strcasecmp(val, "Enable") == 0) {
			snprintf(buf, sizeof(buf), "iwpriv %s cwmenable 1",
				 intf);
		} else if (strcasecmp(val, "Disable") == 0) {
			/* TODO: Disable */
			buf[0] = '\0';
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,BW_SGNL value not supported");
			return 0;
		}

		if (buf[0] != '\0' && system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set BW_SGNL");
		}
	}

	val = get_param(cmd, "Band");
	if (val) {
		if (strcmp(val, "2.4") == 0 || strcmp(val, "5") == 0) {
			/* STA supports all bands by default */
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported Band");
			return 0;
		}
	}

	val = get_param(cmd, "zero_crc");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_ATHEROS:
			ath_set_zero_crc(dut, val);
			break;
		default:
			break;
		}
	}

	return 1;
}


static int sta_set_60g_common(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *val;
	char buf[100];

	val = get_param(cmd, "MSDUSize");
	if (val) {
		int mtu;

		dut->amsdu_size = atoi(val);
		if (dut->amsdu_size > IEEE80211_MAX_DATA_LEN_DMG ||
		    dut->amsdu_size < IEEE80211_SNAP_LEN_DMG) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"MSDUSize %d is above max %d or below min %d",
					dut->amsdu_size,
					IEEE80211_MAX_DATA_LEN_DMG,
					IEEE80211_SNAP_LEN_DMG);
			dut->amsdu_size = 0;
			return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
		}

		mtu = dut->amsdu_size - IEEE80211_SNAP_LEN_DMG;
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Setting amsdu_size to %d", mtu);
		snprintf(buf, sizeof(buf), "ifconfig %s mtu %d",
			 get_station_ifname(), mtu);

		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to set %s",
					buf);
			return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
		}
	}

	val = get_param(cmd, "BAckRcvBuf");
	if (val) {
		dut->back_rcv_buf = atoi(val);
		if (dut->back_rcv_buf == 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to convert %s or value is 0",
					val);
			return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
		}

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Setting BAckRcvBuf to %s", val);
	}

	return SIGMA_DUT_SUCCESS_CALLER_SEND_STATUS;
}


static int sta_pcp_start(struct sigma_dut *dut, struct sigma_conn *conn,
			 struct sigma_cmd *cmd)
{
	int net_id;
	char *ifname;
	const char *val;
	char buf[100];

	dut->mode = SIGMA_MODE_STATION;
	ifname = get_main_ifname();
	if (wpa_command(ifname, "PING") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Supplicant not running");
		return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
	}

	wpa_command(ifname, "FLUSH");
	net_id = add_network_common(dut, conn, ifname, cmd);
	if (net_id < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to add network");
		return net_id;
	}

	/* TODO: mode=2 for the AP; in the future, replace for mode PCP */
	if (set_network(ifname, net_id, "mode", "2") < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set supplicant network mode");
		return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Supplicant set network with mode 2");

	val = get_param(cmd, "Security");
	if (val && strcasecmp(val, "OPEN") == 0) {
		dut->ap_key_mgmt = AP_OPEN;
		if (set_network(ifname, net_id, "key_mgmt", "NONE") < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set supplicant to %s security",
					val);
			return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
		}
	} else if (val && strcasecmp(val, "WPA2-PSK") == 0) {
		dut->ap_key_mgmt = AP_WPA2_PSK;
		if (set_network(ifname, net_id, "key_mgmt", "WPA-PSK") < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set supplicant to %s security",
					val);
			return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
		}

		if (set_network(ifname, net_id, "proto", "RSN") < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set supplicant to proto RSN");
			return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
		}
	} else if (val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Requested Security %s is not supported on 60GHz",
				val);
		return SIGMA_DUT_INVALID_CALLER_SEND_STATUS;
	}

	val = get_param(cmd, "Encrypt");
	if (val && strcasecmp(val, "AES-GCMP") == 0) {
		if (set_network(ifname, net_id, "pairwise", "GCMP") < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set supplicant to pairwise GCMP");
			return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
		}
		if (set_network(ifname, net_id, "group", "GCMP") < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set supplicant to group GCMP");
			return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
		}
	} else if (val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Requested Encrypt %s is not supported on 60 GHz",
				val);
		return SIGMA_DUT_INVALID_CALLER_SEND_STATUS;
	}

	val = get_param(cmd, "PSK");
	if (val && set_network_quoted(ifname, net_id, "psk", val) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to set psk %s",
				val);
		return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
	}

	/* Convert 60G channel to freq */
	switch (dut->ap_channel) {
	case 1:
		val = "58320";
		break;
	case 2:
		val = "60480";
		break;
	case 3:
		val = "62640";
		break;
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to configure channel %d. Not supported",
				dut->ap_channel);
		return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
	}

	if (set_network(ifname, net_id, "frequency", val) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set supplicant network frequency");
		return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Supplicant set network with frequency");

	snprintf(buf, sizeof(buf), "SELECT_NETWORK %d", net_id);
	if (wpa_command(ifname, buf) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to select network id %d on %s",
				net_id, ifname);
		return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Selected network");

	return SIGMA_DUT_SUCCESS_CALLER_SEND_STATUS;
}


static int wil6210_set_abft_len(struct sigma_dut *dut, int abft_len)
{
	char buf[128], fname[128];
	FILE *f;

	if (wil6210_get_debugfs_dir(dut, buf, sizeof(buf))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to get wil6210 debugfs dir");
		return -1;
	}

	snprintf(fname, sizeof(fname), "%s/abft_len", buf);
	f = fopen(fname, "w");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to open: %s", fname);
		return -1;
	}

	fprintf(f, "%d\n", abft_len);
	fclose(f);

	return 0;
}


static int sta_set_60g_abft_len(struct sigma_dut *dut, struct sigma_conn *conn,
				int abft_len)
{
	switch (get_driver_type()) {
	case DRIVER_WIL6210:
		return wil6210_set_abft_len(dut, abft_len);
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"set abft_len not supported");
		return -1;
	}
}


static int sta_set_60g_pcp(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	const char *val;
	unsigned int abft_len = 1; /* default is one slot */

	if (dut->dev_role != DEVROLE_PCP) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,Invalid DevRole");
		return 0;
	}

	val = get_param(cmd, "SSID");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_ssid) - 1) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "ErrorCode,Invalid SSID");
			return -1;
		}

		strlcpy(dut->ap_ssid, val, sizeof(dut->ap_ssid));
	}

	val = get_param(cmd, "CHANNEL");
	if (val) {
		const char *pos;

		dut->ap_channel = atoi(val);
		pos = strchr(val, ';');
		if (pos) {
			pos++;
			dut->ap_channel_1 = atoi(pos);
		}
	}

	switch (dut->ap_channel) {
	case 1:
	case 2:
	case 3:
		break;
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Channel %d is not supported", dut->ap_channel);
		send_resp(dut, conn, SIGMA_ERROR,
			  "Requested channel is not supported");
		return -1;
	}

	val = get_param(cmd, "BCNINT");
	if (val)
		dut->ap_bcnint = atoi(val);


	val = get_param(cmd, "ExtSchIE");
	if (val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,ExtSchIE is not supported yet");
		return -1;
	}

	val = get_param(cmd, "AllocType");
	if (val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,AllocType is not supported yet");
		return -1;
	}

	val = get_param(cmd, "PercentBI");
	if (val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,PercentBI is not supported yet");
		return -1;
	}

	val = get_param(cmd, "CBAPOnly");
	if (val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,CBAPOnly is not supported yet");
		return -1;
	}

	val = get_param(cmd, "AMPDU");
	if (val) {
		if (strcasecmp(val, "Enable") == 0)
			dut->ap_ampdu = 1;
		else if (strcasecmp(val, "Disable") == 0)
			dut->ap_ampdu = 2;
		else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,AMPDU value is not Enable nor Disabled");
			return -1;
		}
	}

	val = get_param(cmd, "AMSDU");
	if (val) {
		if (strcasecmp(val, "Enable") == 0)
			dut->ap_amsdu = 1;
		else if (strcasecmp(val, "Disable") == 0)
			dut->ap_amsdu = 2;
	}

	val = get_param(cmd, "NumMSDU");
	if (val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode, NumMSDU is not supported yet");
		return -1;
	}

	val = get_param(cmd, "ABFTLRang");
	if (val) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"ABFTLRang parameter %s", val);
		if (strcmp(val, "Gt1") == 0)
			abft_len = 2; /* 2 slots in this case */
	}

	if (sta_set_60g_abft_len(dut, conn, abft_len)) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode, Can't set ABFT length");
		return -1;
	}

	if (sta_pcp_start(dut, conn, cmd) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode, Can't start PCP role");
		return -1;
	}

	return sta_set_60g_common(dut, conn, cmd);
}


static int sta_set_60g_sta(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	const char *val = get_param(cmd, "DiscoveryMode");

	if (dut->dev_role != DEVROLE_STA) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,Invalid DevRole");
		return 0;
	}

	if (val) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Discovery: %s", val);
		/* Ignore Discovery mode till Driver expose API. */
#if 0
		if (strcasecmp(val, "1") == 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "ErrorCode,DiscoveryMode 1 not supported");
			return 0;
		}

		if (strcasecmp(val, "0") == 0) {
			/* OK */
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "ErrorCode,DiscoveryMode not supported");
			return 0;
		}
#endif
	}

	if (start_sta_mode(dut) != 0)
		return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
	return sta_set_60g_common(dut, conn, cmd);
}


static int cmd_sta_disconnect(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	disconnect_station(dut);
	/* Try to ignore old scan results to avoid HS 2.0R2 test case failures
	 * due to cached results. */
	wpa_command(intf, "SET ignore_old_scan_res 1");
	wpa_command(intf, "BSS_FLUSH");
	return 1;
}


static int cmd_sta_reassoc(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *bssid = get_param(cmd, "bssid");
	const char *val = get_param(cmd, "CHANNEL");
	struct wpa_ctrl *ctrl;
	char buf[100];
	int res;
	int chan = 0;
	int status = 0;

	if (bssid == NULL) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Missing bssid "
			  "argument");
		return 0;
	}

	if (val)
		chan = atoi(val);

	if (wifi_chip_type != DRIVER_WCN && wifi_chip_type != DRIVER_AR6003) {
		/* The current network may be from sta_associate or
		 * sta_hs2_associate
		 */
		if (set_network(intf, dut->infra_network_id, "bssid", bssid) <
		    0 ||
		    set_network(intf, 0, "bssid", bssid) < 0)
			return -2;
	}

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -1;
	}

	if (wifi_chip_type == DRIVER_WCN) {
#ifdef ANDROID
		if (chan) {
			unsigned int freq;

			freq = channel_to_freq(chan);
			if (!freq) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Invalid channel number provided: %d",
						chan);
				send_resp(dut, conn, SIGMA_INVALID,
					  "ErrorCode,Invalid channel number");
				goto close_mon_conn;
			}
			res = snprintf(buf, sizeof(buf),
				       "SCAN TYPE=ONLY freq=%d", freq);
		} else {
			res = snprintf(buf, sizeof(buf), "SCAN TYPE=ONLY");
		}
		if (res < 0 || res >= (int) sizeof(buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,snprintf failed");
			goto close_mon_conn;
		}
		if (wpa_command(intf, buf) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to start scan");
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,scan failed");
			goto close_mon_conn;
		}

		res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-SCAN-RESULTS",
					buf, sizeof(buf));
		if (res < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Scan did not complete");
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,scan did not complete");
			goto close_mon_conn;
		}

		if (set_network(intf, dut->infra_network_id, "bssid", "any")
		    < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to set "
					"bssid to any during FASTREASSOC");
			status = -2;
			goto close_mon_conn;
		}
		res = snprintf(buf, sizeof(buf), "DRIVER FASTREASSOC %s %d",
			       bssid, chan);
		if (res > 0 && res < (int) sizeof(buf))
			res = wpa_command(intf, buf);

		if (res < 0 || res >= (int) sizeof(buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to run DRIVER FASTREASSOC");
			goto close_mon_conn;
		}
#else /* ANDROID */
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Reassoc using iwpriv - skip chan=%d info",
				chan);
		snprintf(buf, sizeof(buf), "iwpriv %s reassoc", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "%s failed", buf);
			status = -2;
			goto close_mon_conn;
		}
#endif /* ANDROID */
		sigma_dut_print(dut, DUT_MSG_INFO,
				"sta_reassoc: Run %s successful", buf);
	} else if (wpa_command(intf, "REASSOCIATE")) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to "
			  "request reassociation");
		goto close_mon_conn;
	}

	res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-CONNECTED",
				buf, sizeof(buf));
	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Connection did not complete");
		status = -1;
		goto close_mon_conn;
	}
	status = 1;

close_mon_conn:
	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);
	return status;
}


static void hs2_clear_credentials(const char *intf)
{
	wpa_command(intf, "REMOVE_CRED all");
}


#ifdef __linux__
static int wil6210_get_aid(struct sigma_dut *dut, const char *bssid,
			   unsigned int *aid)
{
	const char *pattern = "AID[ \t]+([0-9]+)";

	return wil6210_get_sta_info_field(dut, bssid, pattern, aid);
}
#endif /* __linux__ */


static int sta_get_aid_60g(struct sigma_dut *dut, const char *bssid,
			   unsigned int *aid)
{
	switch (get_driver_type()) {
#ifdef __linux__
	case DRIVER_WIL6210:
		return wil6210_get_aid(dut, bssid, aid);
#endif /* __linux__ */
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR, "get AID not supported");
		return -1;
	}
}


static int sta_get_parameter_60g(struct sigma_dut *dut, struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	char buf[MAX_CMD_LEN];
	char bss_list[MAX_CMD_LEN];
	const char *parameter = get_param(cmd, "Parameter");

	if (parameter == NULL)
		return -1;

	if (strcasecmp(parameter, "AID") == 0) {
		unsigned int aid = 0;
		char bssid[20];

		if (get_wpa_status(get_station_ifname(), "bssid",
				   bssid, sizeof(bssid)) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"could not get bssid");
			return -2;
		}

		if (sta_get_aid_60g(dut, bssid, &aid))
			return -2;

		snprintf(buf, sizeof(buf), "aid,%d", aid);
		sigma_dut_print(dut, DUT_MSG_INFO, "%s", buf);
		send_resp(dut, conn, SIGMA_COMPLETE, buf);
		return 0;
	}

	if (strcasecmp(parameter, "DiscoveredDevList") == 0) {
		char *bss_line;
		char *bss_id = NULL;
		const char *ifname = get_param(cmd, "Interface");
		char *saveptr;

		if (ifname == NULL) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"For get DiscoveredDevList need Interface name.");
			return -1;
		}

		/*
		 * Use "BSS RANGE=ALL MASK=0x2" which provides a list
		 * of BSSIDs in "bssid=<BSSID>\n"
		 */
		if (wpa_command_resp(ifname, "BSS RANGE=ALL MASK=0x2",
				     bss_list,
				     sizeof(bss_list)) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to get bss list");
			return -1;
		}

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"bss list for ifname:%s is:%s",
				ifname, bss_list);

		snprintf(buf, sizeof(buf), "DeviceList");
		bss_line = strtok_r(bss_list, "\n", &saveptr);
		while (bss_line) {
			if (sscanf(bss_line, "bssid=%ms", &bss_id) > 0 &&
			    bss_id) {
				int len;

				len = snprintf(buf + strlen(buf),
					       sizeof(buf) - strlen(buf),
					       ",%s", bss_id);
				free(bss_id);
				bss_id = NULL;
				if (len < 0) {
					sigma_dut_print(dut,
							DUT_MSG_ERROR,
							"Failed to read BSSID");
					send_resp(dut, conn, SIGMA_ERROR,
						  "ErrorCode,Failed to read BSS ID");
					return 0;
				}

				if ((size_t) len >= sizeof(buf) - strlen(buf)) {
					sigma_dut_print(dut,
							DUT_MSG_ERROR,
							"Response buf too small for list");
					send_resp(dut, conn,
						  SIGMA_ERROR,
						  "ErrorCode,Response buf too small for list");
					return 0;
				}
			}

			bss_line = strtok_r(NULL, "\n", &saveptr);
		}

		sigma_dut_print(dut, DUT_MSG_INFO, "DiscoveredDevList is %s",
				buf);
		send_resp(dut, conn, SIGMA_COMPLETE, buf);
		return 0;
	}

	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported parameter");
	return 0;
}


static int cmd_sta_get_parameter(struct sigma_dut *dut, struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *program = get_param(cmd, "Program");

	if (program == NULL)
		return -1;

	if (strcasecmp(program, "P2PNFC") == 0)
		return p2p_cmd_sta_get_parameter(dut, conn, cmd);

	if (strcasecmp(program, "60ghz") == 0)
		return sta_get_parameter_60g(dut, conn, cmd);

#ifdef ANDROID_NAN
	if (strcasecmp(program, "NAN") == 0)
		return nan_cmd_sta_get_parameter(dut, conn, cmd);
#endif /* ANDROID_NAN */

#ifdef MIRACAST
	if (strcasecmp(program, "WFD") == 0 ||
	    strcasecmp(program, "DisplayR2") == 0)
		return miracast_cmd_sta_get_parameter(dut, conn, cmd);
#endif /* MIRACAST */

	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported parameter");
	return 0;
}


static void sta_reset_default_ath(struct sigma_dut *dut, const char *intf,
				  const char *type)
{
	char buf[100];

	if (dut->program == PROGRAM_VHT) {
		snprintf(buf, sizeof(buf), "iwpriv %s chwidth 2", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv %s chwidth failed", intf);
		}

		snprintf(buf, sizeof(buf), "iwpriv %s mode 11ACVHT80", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv %s mode 11ACVHT80 failed",
					intf);
		}

		snprintf(buf, sizeof(buf), "iwpriv %s vhtmcs -1", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv %s vhtmcs -1 failed", intf);
		}
	}

	if (dut->program == PROGRAM_HT) {
		snprintf(buf, sizeof(buf), "iwpriv %s chwidth 0", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv %s chwidth failed", intf);
		}

		snprintf(buf, sizeof(buf), "iwpriv %s mode 11naht40", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv %s mode 11naht40 failed",
					intf);
		}

		snprintf(buf, sizeof(buf), "iwpriv %s set11NRates 0", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv set11NRates failed");
		}
	}

	if (dut->program == PROGRAM_VHT || dut->program == PROGRAM_HT) {
		snprintf(buf, sizeof(buf), "iwpriv %s powersave 0", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"disabling powersave failed");
		}

		/* Reset CTS width */
		snprintf(buf, sizeof(buf), "wifitool %s beeliner_fw_test 54 0",
			 intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"wifitool %s beeliner_fw_test 54 0 failed",
					intf);
		}

		/* Enable Dynamic Bandwidth signalling by default */
		snprintf(buf, sizeof(buf), "iwpriv %s cwmenable 1", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv %s cwmenable 1 failed", intf);
		}

		snprintf(buf, sizeof(buf), "iwconfig %s rts 2347", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv rts failed");
		}
	}

	if (type && strcasecmp(type, "Testbed") == 0) {
		dut->testbed_flag_txsp = 1;
		dut->testbed_flag_rxsp = 1;
		/* STA has to set spatial stream to 2 per Appendix H */
		snprintf(buf, sizeof(buf), "iwpriv %s vht_mcsmap 0xfff0", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv vht_mcsmap failed");
		}

		/* Disable LDPC per Appendix H */
		snprintf(buf, sizeof(buf), "iwpriv %s ldpc 0", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv %s ldpc 0 failed", intf);
		}

		snprintf(buf, sizeof(buf), "iwpriv %s amsdu 1", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv amsdu failed");
		}

		/* TODO: Disable STBC 2x1 transmit and receive */
		snprintf(buf, sizeof(buf), "iwpriv %s tx_stbc 0", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Disable tx_stbc 0 failed");
		}

		snprintf(buf, sizeof(buf), "iwpriv %s rx_stbc 0", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Disable rx_stbc 0 failed");
		}

		/* STA has to disable Short GI per Appendix H */
		snprintf(buf, sizeof(buf), "iwpriv %s shortgi 0", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv %s shortgi 0 failed", intf);
		}
	}

	if (type && strcasecmp(type, "DUT") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s nss 3", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv %s nss 3 failed", intf);
		}

		snprintf(buf, sizeof(buf), "iwpriv %s shortgi 1", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv %s shortgi 1 failed", intf);
		}
	}
}


static int cmd_sta_reset_default(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	int cmd_sta_p2p_reset(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd);
	const char *intf = get_param(cmd, "Interface");
	const char *type;
	const char *program = get_param(cmd, "program");

	if (!program)
		program = get_param(cmd, "prog");
	dut->program = sigma_program_to_enum(program);
	dut->device_type = STA_unknown;
	type = get_param(cmd, "type");
	if (type && strcasecmp(type, "Testbed") == 0)
		dut->device_type = STA_testbed;
	if (type && strcasecmp(type, "DUT") == 0)
		dut->device_type = STA_dut;

	if (dut->program == PROGRAM_TDLS) {
		/* Clear TDLS testing mode */
		wpa_command(intf, "SET tdls_disabled 0");
		wpa_command(intf, "SET tdls_testing 0");
		dut->no_tpk_expiration = 0;
		if (get_driver_type() == DRIVER_WCN) {
			/* Enable the WCN driver in TDLS Explicit trigger mode
			 */
			wpa_command(intf, "SET tdls_external_control 0");
			wpa_command(intf, "SET tdls_trigger_control 0");
		}
	}

#ifdef MIRACAST
	if (dut->program == PROGRAM_WFD ||
	    dut->program == PROGRAM_DISPLAYR2)
		miracast_sta_reset_default(dut, conn, cmd);
#endif /* MIRACAST */

	switch (get_driver_type()) {
	case DRIVER_ATHEROS:
		sta_reset_default_ath(dut, intf, type);
		break;
	default:
		break;
	}

#ifdef ANDROID_NAN
	if (dut->program == PROGRAM_NAN)
		nan_cmd_sta_reset_default(dut, conn, cmd);
#endif /* ANDROID_NAN */

	if (dut->program == PROGRAM_HS2_R2) {
		unlink("SP/wi-fi.org/pps.xml");
		if (system("rm -r SP/*") != 0) {
		}
		unlink("next-client-cert.pem");
		unlink("next-client-key.pem");
	}

	if (dut->program == PROGRAM_60GHZ) {
		const char *dev_role = get_param(cmd, "DevRole");

		if (!dev_role) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Missing DevRole argument");
			return 0;
		}

		if (strcasecmp(dev_role, "STA") == 0)
			dut->dev_role = DEVROLE_STA;
		else if (strcasecmp(dev_role, "PCP") == 0)
			dut->dev_role = DEVROLE_PCP;
		else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unknown DevRole");
			return 0;
		}

		if (dut->device_type == STA_unknown) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Device type is not STA testbed or DUT");
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unknown device type");
			return 0;
		}
	}

	wpa_command(intf, "WPS_ER_STOP");
	wpa_command(intf, "FLUSH");
	wpa_command(intf, "SET radio_disabled 0");

	if (dut->tmp_mac_addr && dut->set_macaddr) {
		dut->tmp_mac_addr = 0;
		if (system(dut->set_macaddr) != 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Failed to clear "
					"temporary MAC address");
		}
	}

	set_ps(intf, dut, 0);

	if (dut->program == PROGRAM_HS2 || dut->program == PROGRAM_HS2_R2) {
		wpa_command(intf, "SET interworking 1");
		wpa_command(intf, "SET hs20 1");
	}

	if (dut->program == PROGRAM_HS2_R2) {
		wpa_command(intf, "SET pmf 1");
	} else {
		wpa_command(intf, "SET pmf 0");
	}

	hs2_clear_credentials(intf);
	wpa_command(intf, "SET hessid 00:00:00:00:00:00");
	wpa_command(intf, "SET access_network_type 15");

	static_ip_file(0, NULL, NULL, NULL);
	kill_dhcp_client(dut, intf);
	clear_ip_addr(dut, intf);

	dut->er_oper_performed = 0;
	dut->er_oper_bssid[0] = '\0';

	if (dut->program == PROGRAM_LOC) {
		/* Disable Interworking by default */
		wpa_command(get_station_ifname(), "SET interworking 0");
	}

	if (dut->program == PROGRAM_MBO) {
		free(dut->non_pref_ch_list);
		dut->non_pref_ch_list = NULL;
		free(dut->btm_query_cand_list);
		dut->btm_query_cand_list = NULL;
		wpa_command(intf, "SET reject_btm_req_reason 0");
		wpa_command(intf, "SET ignore_assoc_disallow 0");
		wpa_command(intf, "SET gas_address3 0");
		wpa_command(intf, "SET roaming 1");
	}

	free(dut->rsne_override);
	dut->rsne_override = NULL;

	if (dut->program != PROGRAM_VHT)
		return cmd_sta_p2p_reset(dut, conn, cmd);
	return 1;
}


static int cmd_sta_get_events(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *program = get_param(cmd, "Program");

	if (program == NULL)
		return -1;
#ifdef ANDROID_NAN
	if (strcasecmp(program, "NAN") == 0)
		return nan_cmd_sta_get_events(dut, conn, cmd);
#endif /* ANDROID_NAN */
	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported parameter");
	return 0;
}


static int cmd_sta_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *program = get_param(cmd, "Prog");

	if (program == NULL)
		return -1;
#ifdef ANDROID_NAN
	if (strcasecmp(program, "NAN") == 0)
		return nan_cmd_sta_exec_action(dut, conn, cmd);
#endif /* ANDROID_NAN */
	if (strcasecmp(program, "Loc") == 0)
		return loc_cmd_sta_exec_action(dut, conn, cmd);
	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported parameter");
	return 0;
}


static int cmd_sta_set_11n(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val, *mcs32, *rate;

	val = get_param(cmd, "GREENFIELD");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* Enable GD */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,GF not supported");
			return 0;
		}
	}

	val = get_param(cmd, "SGI20");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_ATHEROS:
			ath_sta_set_sgi(dut, intf, val);
			break;
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,SGI20 not supported");
			return 0;
		}
	}

	mcs32 = get_param(cmd, "MCS32"); /* HT Duplicate Mode Enable/Disable */
	rate = get_param(cmd, "MCS_FIXEDRATE"); /* Fixed MCS rate (0..31) */
	if (mcs32 && rate) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,MCS32,MCS_FIXEDRATE not supported");
		return 0;
	} else if (mcs32 && !rate) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,MCS32 not supported");
		return 0;
	} else if (!mcs32 && rate) {
		switch (get_driver_type()) {
		case DRIVER_ATHEROS:
			novap_reset(dut, intf);
			ath_sta_set_11nrates(dut, intf, rate);
			break;
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,MCS32_FIXEDRATE not supported");
			return 0;
		}
	}

	return cmd_sta_set_wireless_common(intf, dut, conn, cmd);
}


static int cmd_sta_set_wireless_vht(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;
	char buf[30];
	int tkip = -1;
	int wep = -1;

	val = get_param(cmd, "SGI80");
	if (val) {
		int sgi80;

		sgi80 = strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0;
		snprintf(buf, sizeof(buf), "iwpriv %s shortgi %d", intf, sgi80);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv shortgi failed");
		}
	}

	val = get_param(cmd, "TxBF");
	if (val && (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0)) {
		snprintf(buf, sizeof(buf), "iwpriv %s vhtsubfee 1", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv vhtsubfee failed");
		}
		snprintf(buf, sizeof(buf), "iwpriv %s vhtsubfer 1", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv vhtsubfer failed");
		}
	}

	val = get_param(cmd, "MU_TxBF");
	if (val && (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0)) {
		switch (get_driver_type()) {
		case DRIVER_ATHEROS:
			ath_sta_set_txsp_stream(dut, intf, "1SS");
			ath_sta_set_rxsp_stream(dut, intf, "1SS");
		case DRIVER_WCN:
			if (wcn_sta_set_sp_stream(dut, intf, "1SS") < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set RX/TXSP_STREAM");
				return 0;
			}
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting SP_STREAM not supported");
			break;
		}
		snprintf(buf, sizeof(buf), "iwpriv %s vhtmubfee 1", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv vhtmubfee failed");
		}
		snprintf(buf, sizeof(buf), "iwpriv %s vhtmubfer 1", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv vhtmubfer failed");
		}
	}

	val = get_param(cmd, "LDPC");
	if (val) {
		int ldpc;

		ldpc = strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0;
		snprintf(buf, sizeof(buf), "iwpriv %s ldpc %d", intf, ldpc);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv ldpc failed");
		}
	}

	val = get_param(cmd, "opt_md_notif_ie");
	if (val) {
		char *result = NULL;
		char delim[] = ";";
		char token[30];
		int value, config_val = 0;
		char *saveptr;

		strlcpy(token, val, sizeof(token));
		result = strtok_r(token, delim, &saveptr);

		/* Extract the NSS information */
		if (result) {
			value = atoi(result);
			switch (value) {
			case 1:
				config_val = 1;
				break;
			case 2:
				config_val = 3;
				break;
			case 3:
				config_val = 7;
				break;
			case 4:
				config_val = 15;
				break;
			default:
				config_val = 3;
				break;
			}

			snprintf(buf, sizeof(buf), "iwpriv %s rxchainmask %d",
				 intf, config_val);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv rxchainmask failed");
			}

			snprintf(buf, sizeof(buf), "iwpriv %s txchainmask %d",
				 intf, config_val);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv txchainmask failed");
			}
		}

		/* Extract the channel width information */
		result = strtok_r(NULL, delim, &saveptr);
		if (result) {
			value = atoi(result);
			switch (value) {
			case 20:
				config_val = 0;
				break;
			case 40:
				config_val = 1;
				break;
			case 80:
				config_val = 2;
				break;
			case 160:
				config_val = 3;
				break;
			default:
				config_val = 2;
				break;
			}

			dut->chwidth = config_val;

			snprintf(buf, sizeof(buf), "iwpriv %s chwidth %d",
				 intf, config_val);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv chwidth failed");
			}
		}

		snprintf(buf, sizeof(buf), "iwpriv %s opmode_notify 1", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv opmode_notify failed");
		}
	}

	val = get_param(cmd, "nss_mcs_cap");
	if (val) {
		int nss, mcs;
		char token[20];
		char *result = NULL;
		unsigned int vht_mcsmap = 0;
		char *saveptr;

		strlcpy(token, val, sizeof(token));
		result = strtok_r(token, ";", &saveptr);
		if (!result) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"VHT NSS not specified");
			return 0;
		}
		nss = atoi(result);

		snprintf(buf, sizeof(buf), "iwpriv %s nss %d", intf, nss);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv nss failed");
		}

		result = strtok_r(NULL, ";", &saveptr);
		if (result == NULL) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"VHTMCS NOT SPECIFIED!");
			return 0;
		}
		result = strtok_r(result, "-", &saveptr);
		result = strtok_r(NULL, "-", &saveptr);
		if (!result) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"VHT MCS not specified");
			return 0;
		}
		mcs = atoi(result);

		snprintf(buf, sizeof(buf), "iwpriv %s vhtmcs %d", intf, mcs);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv mcs failed");
		}

		switch (nss) {
		case 1:
			switch (mcs) {
			case 7:
				vht_mcsmap = 0xfffc;
				break;
			case 8:
				vht_mcsmap = 0xfffd;
				break;
			case 9:
				vht_mcsmap = 0xfffe;
				break;
			default:
				vht_mcsmap = 0xfffe;
				break;
			}
			break;
		case 2:
			switch (mcs) {
			case 7:
				vht_mcsmap = 0xfff0;
				break;
			case 8:
				vht_mcsmap = 0xfff5;
				break;
			case 9:
				vht_mcsmap = 0xfffa;
				break;
			default:
				vht_mcsmap = 0xfffa;
				break;
			}
			break;
		case 3:
			switch (mcs) {
			case 7:
				vht_mcsmap = 0xffc0;
				break;
			case 8:
				vht_mcsmap = 0xffd5;
				break;
			case 9:
				vht_mcsmap = 0xffea;
				break;
			default:
				vht_mcsmap = 0xffea;
				break;
			}
			break;
		default:
			vht_mcsmap = 0xffea;
			break;
		}
		snprintf(buf, sizeof(buf), "iwpriv %s vht_mcsmap 0x%04x",
			 intf, vht_mcsmap);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv vht_mcsmap failed");
		}
	}

	/* UNSUPPORTED: val = get_param(cmd, "Tx_lgi_rate"); */

	val = get_param(cmd, "Vht_tkip");
	if (val)
		tkip = strcmp(val, "1") == 0 ||	strcasecmp(val, "Enable") == 0;

	val = get_param(cmd, "Vht_wep");
	if (val)
		wep = strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0;

	if (tkip != -1 || wep != -1) {
		if ((tkip == 1 && wep != 0) || (wep == 1 && tkip != 0)) {
			snprintf(buf, sizeof(buf), "iwpriv %s htweptkip 1",
				 intf);
		} else if ((tkip == 0 && wep != 1) || (wep == 0 && tkip != 1)) {
			snprintf(buf, sizeof(buf), "iwpriv %s htweptkip 0",
				 intf);
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"ErrorCode,mixed mode of VHT TKIP/WEP not supported");
			return 0;
		}

		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv htweptkip failed");
		}
	}

	 val = get_param(cmd, "txBandwidth");
	 if (val) {
		 switch (get_driver_type()) {
		 case DRIVER_ATHEROS:
			 if (ath_set_width(dut, conn, intf, val) < 0) {
				 send_resp(dut, conn, SIGMA_ERROR,
					   "ErrorCode,Failed to set txBandwidth");
				 return 0;
			 }
			 break;
		 default:
			 sigma_dut_print(dut, DUT_MSG_ERROR,
					 "Setting txBandwidth not supported");
			 break;
		 }
	 }

	return cmd_sta_set_wireless_common(intf, dut, conn, cmd);
}


static int sta_set_wireless_60g(struct sigma_dut *dut,
				struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *dev_role = get_param(cmd, "DevRole");

	if (!dev_role) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,DevRole not specified");
		return 0;
	}

	if (strcasecmp(dev_role, "PCP") == 0)
		return sta_set_60g_pcp(dut, conn, cmd);
	if (strcasecmp(dev_role, "STA") == 0)
		return sta_set_60g_sta(dut, conn, cmd);
	send_resp(dut, conn, SIGMA_INVALID,
		  "ErrorCode,DevRole not supported");
	return 0;
}


static int cmd_sta_set_wireless(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *val;

	val = get_param(cmd, "Program");
	if (val) {
		if (strcasecmp(val, "11n") == 0)
			return cmd_sta_set_11n(dut, conn, cmd);
		if (strcasecmp(val, "VHT") == 0)
			return cmd_sta_set_wireless_vht(dut, conn, cmd);
		if (strcasecmp(val, "60ghz") == 0)
			return sta_set_wireless_60g(dut, conn, cmd);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Program value not supported");
	} else {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Program argument not available");
	}

	return 0;
}


static void ath_sta_inject_frame(struct sigma_dut *dut, const char *intf,
				 int tid)
{
	char buf[100];
	int tid_to_dscp [] = { 0x00, 0x20, 0x40, 0x60, 0x80, 0xa0, 0xc0, 0xe0 };

	if (tid < 0 ||
	    tid >= (int) (sizeof(tid_to_dscp) / sizeof(tid_to_dscp[0]))) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Unsupported TID: %d", tid);
		return;
	}

	/*
	 * Two ways to ensure that addba request with a
	 * non zero TID could be sent out. EV 117296
	 */
	snprintf(buf, sizeof(buf),
		 "ping -c 8 -Q %d `arp -a | grep wlan0 | awk '{print $2}' | tr -d '()'`",
		 tid);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Ping did not send out");
	}

	snprintf(buf, sizeof(buf),
		 "iwconfig %s | grep Access | awk '{print $6}' > %s",
		 intf, VI_QOS_TMP_FILE);
	if (system(buf) != 0)
		return;

	snprintf(buf, sizeof(buf),
		 "ifconfig %s | grep HWaddr | cut -b 39-56 >> %s",
		 intf, VI_QOS_TMP_FILE);
	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "HWaddr matching failed");

	snprintf(buf,sizeof(buf), "sed -n '3,$p' %s >> %s",
		 VI_QOS_REFFILE, VI_QOS_TMP_FILE);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"VI_QOS_TEMP_FILE generation error failed");
	}
	snprintf(buf, sizeof(buf), "sed '5 c %x' %s > %s",
		 tid_to_dscp[tid], VI_QOS_TMP_FILE, VI_QOS_FILE);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"VI_QOS_FILE generation failed");
	}

	snprintf(buf, sizeof(buf), "sed '5 c %x' %s > %s",
		 tid_to_dscp[tid], VI_QOS_TMP_FILE, VI_QOS_FILE);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"VI_QOS_FILE generation failed");
	}

	snprintf(buf, sizeof(buf), "ethinject %s %s", intf, VI_QOS_FILE);
	if (system(buf) != 0) {
	}
}


static int ath_sta_send_addba(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;
	int tid = 0;
	char buf[100];

	val = get_param(cmd, "TID");
	if (val) {
		tid = atoi(val);
		if (tid)
			ath_sta_inject_frame(dut, intf, tid);
	}

	/* Command sequence for ADDBA request on Peregrine based devices */
	snprintf(buf, sizeof(buf), "iwpriv %s setaddbaoper 1", intf);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv setaddbaoper failed");
	}

	snprintf(buf, sizeof(buf), "wifitool %s senddelba 1 %d 1 4", intf, tid);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"wifitool senddelba failed");
	}

	snprintf(buf, sizeof(buf), "wifitool %s sendaddba 1 %d 64", intf, tid);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"wifitool sendaddba failed");
	}

	/* UNSUPPORTED: val = get_param(cmd, "Dest_mac"); */

	return 1;
}


#ifdef __linux__

static int wil6210_send_addba(struct sigma_dut *dut, const char *dest_mac,
			      int agg_size)
{
	char dir[128], buf[128];
	FILE *f;
	regex_t re;
	regmatch_t m[2];
	int rc, ret = -1, vring_id, found;

	if (wil6210_get_debugfs_dir(dut, dir, sizeof(dir))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to get wil6210 debugfs dir");
		return -1;
	}

	snprintf(buf, sizeof(buf), "%s/vrings", dir);
	f = fopen(buf, "r");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "failed to open: %s", buf);
		return -1;
	}

	if (regcomp(&re, "VRING tx_[ \t]*([0-9]+)", REG_EXTENDED)) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "regcomp failed");
		goto out;
	}

	/* find TX VRING for the mac address */
	found = 0;
	while (fgets(buf, sizeof(buf), f)) {
		if (strcasestr(buf, dest_mac)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"no TX VRING for %s", dest_mac);
		goto out;
	}

	/* extract VRING ID, "VRING tx_<id> = {" */
	if (!fgets(buf, sizeof(buf), f)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"no VRING start line for %s", dest_mac);
		goto out;
	}

	rc = regexec(&re, buf, 2, m, 0);
	regfree(&re);
	if (rc || m[1].rm_so < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"no VRING TX ID for %s", dest_mac);
		goto out;
	}
	buf[m[1].rm_eo] = 0;
	vring_id = atoi(&buf[m[1].rm_so]);

	/* send the addba command */
	fclose(f);
	snprintf(buf, sizeof(buf), "%s/back", dir);
	f = fopen(buf, "w");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to open: %s", buf);
		return -1;
	}

	fprintf(f, "add %d %d\n", vring_id, agg_size);

	ret = 0;

out:
	fclose(f);

	return ret;
}


static int send_addba_60g(struct sigma_dut *dut, struct sigma_conn *conn,
			  struct sigma_cmd *cmd)
{
	const char *val;
	int tid = 0;

	val = get_param(cmd, "TID");
	if (val) {
		tid = atoi(val);
		if (tid != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Ignore TID %d for send_addba use TID 0 for 60g since only 0 required on TX",
					tid);
		}
	}

	val = get_param(cmd, "Dest_mac");
	if (!val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Currently not supporting addba for 60G without Dest_mac");
		return SIGMA_DUT_ERROR_CALLER_SEND_STATUS;
	}

	if (wil6210_send_addba(dut, val, dut->back_rcv_buf))
		return -1;

	return 1;
}

#endif /* __linux__ */


static int cmd_sta_send_addba(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	switch (get_driver_type()) {
	case DRIVER_ATHEROS:
		return ath_sta_send_addba(dut, conn, cmd);
#ifdef __linux__
	case DRIVER_WIL6210:
		return send_addba_60g(dut, conn, cmd);
#endif /* __linux__ */
	default:
		/*
		 * There is no driver specific implementation for other drivers.
		 * Ignore the command and report COMPLETE since the following
		 * throughput test operation will end up sending ADDBA anyway.
		 */
		return 1;
	}
}


int inject_eth_frame(int s, const void *data, size_t len,
		     unsigned short ethtype, char *dst, char *src)
{
	struct iovec iov[4] = {
		{
			.iov_base = dst,
			.iov_len = ETH_ALEN,
		},
		{
			.iov_base = src,
			.iov_len = ETH_ALEN,
		},
		{
			.iov_base = &ethtype,
			.iov_len = sizeof(unsigned short),
		},
		{
			.iov_base = (void *) data,
			.iov_len = len,
		}
	};
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_iov = iov,
		.msg_iovlen = 4,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = 0,
	};

	return sendmsg(s, &msg, 0);
}

#if defined(__linux__) || defined(__QNXNTO__)

int inject_frame(int s, const void *data, size_t len, int encrypt)
{
#define	IEEE80211_RADIOTAP_F_WEP	0x04
#define	IEEE80211_RADIOTAP_F_FRAG	0x08
	unsigned char rtap_hdr[] = {
		0x00, 0x00, /* radiotap version */
		0x0e, 0x00, /* radiotap length */
		0x02, 0xc0, 0x00, 0x00, /* bmap: flags, tx and rx flags */
		IEEE80211_RADIOTAP_F_FRAG, /* F_FRAG (fragment if required) */
		0x00,       /* padding */
		0x00, 0x00, /* RX and TX flags to indicate that */
		0x00, 0x00, /* this is the injected frame directly */
	};
	struct iovec iov[2] = {
		{
			.iov_base = &rtap_hdr,
			.iov_len = sizeof(rtap_hdr),
		},
		{
			.iov_base = (void *) data,
			.iov_len = len,
		}
	};
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_iov = iov,
		.msg_iovlen = 2,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = 0,
	};

	if (encrypt)
		rtap_hdr[8] |= IEEE80211_RADIOTAP_F_WEP;

	return sendmsg(s, &msg, 0);
}


int open_monitor(const char *ifname)
{
#ifdef __QNXNTO__
	struct sockaddr_dl ll;
	int s;

	memset(&ll, 0, sizeof(ll));
	ll.sdl_family = AF_LINK;
	ll.sdl_index = if_nametoindex(ifname);
	if (ll.sdl_index == 0) {
		perror("if_nametoindex");
		return -1;
	}
	s = socket(PF_INET, SOCK_RAW, 0);
#else /* __QNXNTO__ */
	struct sockaddr_ll ll;
	int s;

	memset(&ll, 0, sizeof(ll));
	ll.sll_family = AF_PACKET;
	ll.sll_ifindex = if_nametoindex(ifname);
	if (ll.sll_ifindex == 0) {
		perror("if_nametoindex");
		return -1;
	}
	s = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
#endif /* __QNXNTO__ */
	if (s < 0) {
		perror("socket[PF_PACKET,SOCK_RAW]");
		return -1;
	}

	if (bind(s, (struct sockaddr *) &ll, sizeof(ll)) < 0) {
		perror("monitor socket bind");
		close(s);
		return -1;
	}

	return s;
}


static int hex2num(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


int hwaddr_aton(const char *txt, unsigned char *addr)
{
	int i;

	for (i = 0; i < 6; i++) {
		int a, b;

		a = hex2num(*txt++);
		if (a < 0)
			return -1;
		b = hex2num(*txt++);
		if (b < 0)
			return -1;
		*addr++ = (a << 4) | b;
		if (i < 5 && *txt++ != ':')
			return -1;
	}

	return 0;
}

#endif /* defined(__linux__) || defined(__QNXNTO__) */

enum send_frame_type {
	DISASSOC, DEAUTH, SAQUERY, AUTH, ASSOCREQ, REASSOCREQ, DLS_REQ
};
enum send_frame_protection {
	CORRECT_KEY, INCORRECT_KEY, UNPROTECTED
};


static int sta_inject_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			    enum send_frame_type frame,
			    enum send_frame_protection protected,
			    const char *dest)
{
#ifdef __linux__
	unsigned char buf[1000], *pos;
	int s, res;
	char bssid[20], addr[20];
	char result[32], ssid[100];
	size_t ssid_len;

	if (get_wpa_status(get_station_ifname(), "wpa_state", result,
			   sizeof(result)) < 0 ||
	    strncmp(result, "COMPLETED", 9) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Not connected");
		return 0;
	}

	if (get_wpa_status(get_station_ifname(), "bssid", bssid, sizeof(bssid))
	    < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not get "
			  "current BSSID");
		return 0;
	}

	if (get_wpa_status(get_station_ifname(), "address", addr, sizeof(addr))
	    < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not get "
			  "own MAC address");
		return 0;
	}

	if (get_wpa_status(get_station_ifname(), "ssid", ssid, sizeof(ssid))
	    < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not get "
			  "current SSID");
		return 0;
	}
	ssid_len = strlen(ssid);

	pos = buf;

	/* Frame Control */
	switch (frame) {
	case DISASSOC:
		*pos++ = 0xa0;
		break;
	case DEAUTH:
		*pos++ = 0xc0;
		break;
	case SAQUERY:
		*pos++ = 0xd0;
		break;
	case AUTH:
		*pos++ = 0xb0;
		break;
	case ASSOCREQ:
		*pos++ = 0x00;
		break;
	case REASSOCREQ:
		*pos++ = 0x20;
		break;
	case DLS_REQ:
		*pos++ = 0xd0;
		break;
	}

	if (protected == INCORRECT_KEY)
		*pos++ = 0x40; /* Set Protected field to 1 */
	else
		*pos++ = 0x00;

	/* Duration */
	*pos++ = 0x00;
	*pos++ = 0x00;

	/* addr1 = DA (current AP) */
	hwaddr_aton(bssid, pos);
	pos += 6;
	/* addr2 = SA (own address) */
	hwaddr_aton(addr, pos);
	pos += 6;
	/* addr3 = BSSID (current AP) */
	hwaddr_aton(bssid, pos);
	pos += 6;

	/* Seq# (to be filled by driver/mac80211) */
	*pos++ = 0x00;
	*pos++ = 0x00;

	if (protected == INCORRECT_KEY) {
		/* CCMP parameters */
		memcpy(pos, "\x61\x01\x00\x20\x00\x10\x00\x00", 8);
		pos += 8;
	}

	if (protected == INCORRECT_KEY) {
		switch (frame) {
		case DEAUTH:
			/* Reason code (encrypted) */
			memcpy(pos, "\xa7\x39", 2);
			pos += 2;
			break;
		case DISASSOC:
			/* Reason code (encrypted) */
			memcpy(pos, "\xa7\x39", 2);
			pos += 2;
			break;
		case SAQUERY:
			/* Category|Action|TransID (encrypted) */
			memcpy(pos, "\x6f\xbd\xe9\x4d", 4);
			pos += 4;
			break;
		default:
			return -1;
		}

		/* CCMP MIC */
		memcpy(pos, "\xc8\xd8\x3b\x06\x5d\xb7\x25\x68", 8);
		pos += 8;
	} else {
		switch (frame) {
		case DEAUTH:
			/* reason code = 8 */
			*pos++ = 0x08;
			*pos++ = 0x00;
			break;
		case DISASSOC:
			/* reason code = 8 */
			*pos++ = 0x08;
			*pos++ = 0x00;
			break;
		case SAQUERY:
			/* Category - SA Query */
			*pos++ = 0x08;
			/* SA query Action - Request */
			*pos++ = 0x00;
			/* Transaction ID */
			*pos++ = 0x12;
			*pos++ = 0x34;
			break;
		case AUTH:
			/* Auth Alg (Open) */
			*pos++ = 0x00;
			*pos++ = 0x00;
			/* Seq# */
			*pos++ = 0x01;
			*pos++ = 0x00;
			/* Status code */
			*pos++ = 0x00;
			*pos++ = 0x00;
			break;
		case ASSOCREQ:
			/* Capability Information */
			*pos++ = 0x31;
			*pos++ = 0x04;
			/* Listen Interval */
			*pos++ = 0x0a;
			*pos++ = 0x00;
			/* SSID */
			*pos++ = 0x00;
			*pos++ = ssid_len;
			memcpy(pos, ssid, ssid_len);
			pos += ssid_len;
			/* Supported Rates */
			memcpy(pos, "\x01\x08\x02\x04\x0b\x16\x0c\x12\x18\x24",
			       10);
			pos += 10;
			/* Extended Supported Rates */
			memcpy(pos, "\x32\x04\x30\x48\x60\x6c", 6);
			pos += 6;
			/* RSN */
			memcpy(pos, "\x30\x1a\x01\x00\x00\x0f\xac\x04\x01\x00"
			       "\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x02\xc0"
			       "\x00\x00\x00\x00\x0f\xac\x06", 28);
			pos += 28;
			break;
		case REASSOCREQ:
			/* Capability Information */
			*pos++ = 0x31;
			*pos++ = 0x04;
			/* Listen Interval */
			*pos++ = 0x0a;
			*pos++ = 0x00;
			/* Current AP */
			hwaddr_aton(bssid, pos);
			pos += 6;
			/* SSID */
			*pos++ = 0x00;
			*pos++ = ssid_len;
			memcpy(pos, ssid, ssid_len);
			pos += ssid_len;
			/* Supported Rates */
			memcpy(pos, "\x01\x08\x02\x04\x0b\x16\x0c\x12\x18\x24",
			       10);
			pos += 10;
			/* Extended Supported Rates */
			memcpy(pos, "\x32\x04\x30\x48\x60\x6c", 6);
			pos += 6;
			/* RSN */
			memcpy(pos, "\x30\x1a\x01\x00\x00\x0f\xac\x04\x01\x00"
			       "\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x02\xc0"
			       "\x00\x00\x00\x00\x0f\xac\x06", 28);
			pos += 28;
			break;
		case DLS_REQ:
			/* Category - DLS */
			*pos++ = 0x02;
			/* DLS Action - Request */
			*pos++ = 0x00;
			/* Destination MACAddress */
			if (dest)
				hwaddr_aton(dest, pos);
			else
				memset(pos, 0, 6);
			pos += 6;
			/* Source MACAddress */
			hwaddr_aton(addr, pos);
			pos += 6;
			/* Capability Information */
			*pos++ = 0x10; /* Privacy */
			*pos++ = 0x06; /* QoS */
			/* DLS Timeout Value */
			*pos++ = 0x00;
			*pos++ = 0x01;
			/* Supported rates */
			*pos++ = 0x01;
			*pos++ = 0x08;
			*pos++ = 0x0c; /* 6 Mbps */
			*pos++ = 0x12; /* 9 Mbps */
			*pos++ = 0x18; /* 12 Mbps */
			*pos++ = 0x24; /* 18 Mbps */
			*pos++ = 0x30; /* 24 Mbps */
			*pos++ = 0x48; /* 36 Mbps */
			*pos++ = 0x60; /* 48 Mbps */
			*pos++ = 0x6c; /* 54 Mbps */
			/* TODO: Extended Supported Rates */
			/* TODO: HT Capabilities */
			break;
		}
	}

	s = open_monitor("sigmadut");
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to open "
			  "monitor socket");
		return 0;
	}

	res = inject_frame(s, buf, pos - buf, protected == CORRECT_KEY);
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to "
			  "inject frame");
		close(s);
		return 0;
	}
	if (res < pos - buf) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Only partial "
			  "frame sent");
		close(s);
		return 0;
	}

	close(s);

	return 1;
#else /* __linux__ */
	send_resp(dut, conn, SIGMA_ERROR, "errorCode,sta_send_frame not "
		  "yet supported");
	return 0;
#endif /* __linux__ */
}


static int cmd_sta_send_frame_tdls(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *sta, *val;
	unsigned char addr[ETH_ALEN];
	char buf[100];

	sta = get_param(cmd, "peer");
	if (sta == NULL)
		sta = get_param(cmd, "station");
	if (sta == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing peer address");
		return 0;
	}
	if (hwaddr_aton(sta, addr) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Invalid peer address");
		return 0;
	}

	val = get_param(cmd, "type");
	if (val == NULL)
		return -1;

	if (strcasecmp(val, "DISCOVERY") == 0) {
		snprintf(buf, sizeof(buf), "TDLS_DISCOVER %s", sta);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send TDLS discovery");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(val, "SETUP") == 0) {
		int status = 0, timeout = 0;

		val = get_param(cmd, "Status");
		if (val)
			status = atoi(val);

		val = get_param(cmd, "Timeout");
		if (val)
			timeout = atoi(val);

		if (status != 0 && status != 37) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported status value");
			return 0;
		}

		if (timeout != 0 && timeout != 301) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported timeout value");
			return 0;
		}

		if (status && timeout) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported timeout+status "
				  "combination");
			return 0;
		}

		if (status == 37 &&
		    wpa_command(intf, "SET tdls_testing 0x200")) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to enable "
				  "decline setup response test mode");
			return 0;
		}

		if (timeout == 301) {
			int res;
			if (dut->no_tpk_expiration)
				res = wpa_command(intf,
						  "SET tdls_testing 0x108");
			else
				res = wpa_command(intf,
						  "SET tdls_testing 0x8");
			if (res) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set short TPK "
					  "lifetime");
				return 0;
			}
		}

		snprintf(buf, sizeof(buf), "TDLS_SETUP %s", sta);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send TDLS setup");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(val, "TEARDOWN") == 0) {
		snprintf(buf, sizeof(buf), "TDLS_TEARDOWN %s", sta);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send TDLS teardown");
			return 0;
		}
		return 1;
	}

	send_resp(dut, conn, SIGMA_ERROR,
		  "ErrorCode,Unsupported TDLS frame");
	return 0;
}


static int sta_ap_known(const char *ifname, const char *bssid)
{
	char buf[4096];

	snprintf(buf, sizeof(buf), "BSS %s", bssid);
	if (wpa_command_resp(ifname, buf, buf, sizeof(buf)) < 0)
		return 0;
	if (strncmp(buf, "id=", 3) != 0)
		return 0;
	return 1;
}


static int sta_scan_ap(struct sigma_dut *dut, const char *ifname,
		       const char *bssid)
{
	int res;
	struct wpa_ctrl *ctrl;
	char buf[256];

	if (sta_ap_known(ifname, bssid))
		return 0;
	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"AP not in BSS table - start scan");

	ctrl = open_wpa_mon(ifname);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -1;
	}

	if (wpa_command(ifname, "SCAN") < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to start scan");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return -1;
	}

	res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-SCAN-RESULTS",
				buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Scan did not complete");
		return -1;
	}

	if (sta_ap_known(ifname, bssid))
		return 0;
	sigma_dut_print(dut, DUT_MSG_INFO, "AP not in BSS table");
	return -1;
}


static int cmd_sta_send_frame_hs2_neighadv(struct sigma_dut *dut,
					   struct sigma_conn *conn,
					   struct sigma_cmd *cmd,
					   const char *intf)
{
	char buf[200];

	snprintf(buf, sizeof(buf), "ndsend 2001:DB8::1 %s", intf);
	if (system(buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Failed to run "
			  "ndsend");
		return 0;
	}

	return 1;
}


static int cmd_sta_send_frame_hs2_neighsolreq(struct sigma_dut *dut,
					      struct sigma_conn *conn,
					      struct sigma_cmd *cmd,
					      const char *intf)
{
	char buf[200];
	const char *ip = get_param(cmd, "SenderIP");

	snprintf(buf, sizeof(buf), "ndisc6 -nm %s %s -r 4", ip, intf);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) == 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Neighbor Solicitation got a response "
				"for %s@%s", ip, intf);
	}

	return 1;
}


static int cmd_sta_send_frame_hs2_arpprobe(struct sigma_dut *dut,
					   struct sigma_conn *conn,
					   struct sigma_cmd *cmd,
					   const char *ifname)
{
	char buf[200];
	const char *ip = get_param(cmd, "SenderIP");

	if (ip == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing SenderIP parameter");
		return 0;
	}
	snprintf(buf, sizeof(buf), "arping -I %s -D %s -c 4", ifname, ip);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "arping DAD got a response "
				"for %s@%s", ip, ifname);
	}

	return 1;
}


static int cmd_sta_send_frame_hs2_arpannounce(struct sigma_dut *dut,
					      struct sigma_conn *conn,
					      struct sigma_cmd *cmd,
					      const char *ifname)
{
	char buf[200];
	char ip[16];
	int s;

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s >= 0) {
		struct ifreq ifr;
		struct sockaddr_in saddr;

		memset(&ifr, 0, sizeof(ifr));
		strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(s, SIOCGIFADDR, &ifr) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Failed to get "
					"%s IP address: %s",
					ifname, strerror(errno));
			close(s);
			return -1;
		} else {
			memcpy(&saddr, &ifr.ifr_addr,
			       sizeof(struct sockaddr_in));
			strlcpy(ip, inet_ntoa(saddr.sin_addr), sizeof(ip));
		}
		close(s);

	}

	snprintf(buf, sizeof(buf), "arping -I %s -s %s %s -c 4", ifname, ip,
		 ip);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) != 0) {
	}

	return 1;
}


static int cmd_sta_send_frame_hs2_arpreply(struct sigma_dut *dut,
					   struct sigma_conn *conn,
					   struct sigma_cmd *cmd,
					   const char *ifname)
{
	char buf[200], addr[20];
	char dst[ETH_ALEN], src[ETH_ALEN];
	short ethtype = htons(ETH_P_ARP);
	char *pos;
	int s, res;
	const char *val;
	struct sockaddr_in taddr;

	val = get_param(cmd, "dest");
	if (val)
		hwaddr_aton(val, (unsigned char *) dst);

	val = get_param(cmd, "DestIP");
	if (val)
		inet_aton(val, &taddr.sin_addr);

	if (get_wpa_status(get_station_ifname(), "address", addr,
			   sizeof(addr)) < 0)
		return -2;
	hwaddr_aton(addr, (unsigned char *) src);

	pos = buf;
	*pos++ = 0x00;
	*pos++ = 0x01;
	*pos++ = 0x08;
	*pos++ = 0x00;
	*pos++ = 0x06;
	*pos++ = 0x04;
	*pos++ = 0x00;
	*pos++ = 0x02;
	memcpy(pos, src, ETH_ALEN);
	pos += ETH_ALEN;
	memcpy(pos, &taddr.sin_addr, 4);
	pos += 4;
	memcpy(pos, dst, ETH_ALEN);
	pos += ETH_ALEN;
	memcpy(pos, &taddr.sin_addr, 4);
	pos += 4;

	s = open_monitor(get_station_ifname());
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to open "
			  "monitor socket");
		return 0;
	}

	res = inject_eth_frame(s, buf, pos - buf, ethtype, dst, src);
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to "
			  "inject frame");
		close(s);
		return 0;
	}

	close(s);

	return 1;
}


static int cmd_sta_send_frame_hs2_dls_req(struct sigma_dut *dut,
					  struct sigma_conn *conn,
					  struct sigma_cmd *cmd,
					  const char *intf, const char *dest)
{
	char buf[100];

	if (if_nametoindex("sigmadut") == 0) {
		snprintf(buf, sizeof(buf),
			 "iw dev %s interface add sigmadut type monitor",
			 get_station_ifname());
		if (system(buf) != 0 ||
		    if_nametoindex("sigmadut") == 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to add "
					"monitor interface with '%s'", buf);
			return -2;
		}
	}

	if (system("ifconfig sigmadut up") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to set "
				"monitor interface up");
		return -2;
	}

	return sta_inject_frame(dut, conn, DLS_REQ, UNPROTECTED, dest);
}


static int cmd_sta_send_frame_hs2(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *dest = get_param(cmd, "Dest");
	const char *type = get_param(cmd, "FrameName");
	const char *val;
	char buf[200], *pos, *end;
	int count, count2;

	if (type == NULL)
		type = get_param(cmd, "Type");

	if (intf == NULL || dest == NULL || type == NULL)
		return -1;

	if (strcasecmp(type, "NeighAdv") == 0)
		return cmd_sta_send_frame_hs2_neighadv(dut, conn, cmd, intf);

	if (strcasecmp(type, "NeighSolicitReq") == 0)
		return cmd_sta_send_frame_hs2_neighsolreq(dut, conn, cmd, intf);

	if (strcasecmp(type, "ARPProbe") == 0)
		return cmd_sta_send_frame_hs2_arpprobe(dut, conn, cmd, intf);

	if (strcasecmp(type, "ARPAnnounce") == 0)
		return cmd_sta_send_frame_hs2_arpannounce(dut, conn, cmd, intf);

	if (strcasecmp(type, "ARPReply") == 0)
		return cmd_sta_send_frame_hs2_arpreply(dut, conn, cmd, intf);

	if (strcasecmp(type, "DLS-request") == 0 ||
	    strcasecmp(type, "DLSrequest") == 0)
		return cmd_sta_send_frame_hs2_dls_req(dut, conn, cmd, intf,
						      dest);

	if (strcasecmp(type, "ANQPQuery") != 0 &&
	    strcasecmp(type, "Query") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported HS 2.0 send frame type");
		return 0;
	}

	if (sta_scan_ap(dut, intf, dest) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not find "
			  "the requested AP");
		return 0;
	}

	pos = buf;
	end = buf + sizeof(buf);
	count = 0;
	pos += snprintf(pos, end - pos, "ANQP_GET %s ", dest);

	val = get_param(cmd, "ANQP_CAP_LIST");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s257", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "VENUE_NAME");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s258", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "NETWORK_AUTH_TYPE");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s260", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "ROAMING_CONS");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s261", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "IP_ADDR_TYPE_AVAILABILITY");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s262", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "NAI_REALM_LIST");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s263", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "3GPP_INFO");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s264", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "DOMAIN_LIST");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s268", count > 0 ? "," : "");
		count++;
	}

	if (count && wpa_command(intf, buf)) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,ANQP_GET failed");
		return 0;
	}

	pos = buf;
	end = buf + sizeof(buf);
	count2 = 0;
	pos += snprintf(pos, end - pos, "HS20_ANQP_GET %s ", dest);

	val = get_param(cmd, "HS_CAP_LIST");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s2", count2 > 0 ? "," : "");
		count2++;
	}

	val = get_param(cmd, "OPER_NAME");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s3", count2 > 0 ? "," : "");
		count2++;
	}

	val = get_param(cmd, "WAN_METRICS");
	if (!val)
		val = get_param(cmd, "WAN_MAT");
	if (!val)
		val = get_param(cmd, "WAN_MET");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s4", count2 > 0 ? "," : "");
		count2++;
	}

	val = get_param(cmd, "CONNECTION_CAPABILITY");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s5", count2 > 0 ? "," : "");
		count2++;
	}

	val = get_param(cmd, "OP_CLASS");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s7", count2 > 0 ? "," : "");
		count2++;
	}

	val = get_param(cmd, "OSU_PROVIDER_LIST");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s8", count2 > 0 ? "," : "");
		count2++;
	}

	if (count && count2) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Wait before sending out "
				"second query");
		sleep(1);
	}

	if (count2 && wpa_command(intf, buf)) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,HS20_ANQP_GET "
			  "failed");
		return 0;
	}

	val = get_param(cmd, "NAI_HOME_REALM_LIST");
	if (val) {
		if (count || count2) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Wait before "
					"sending out second query");
			sleep(1);
		}

		if (strcmp(val, "1") == 0)
			val = "mail.example.com";
		snprintf(buf, end - pos,
			 "HS20_GET_NAI_HOME_REALM_LIST %s realm=%s",
			 dest, val);
		if (wpa_command(intf, buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,HS20_GET_NAI_HOME_REALM_LIST "
				  "failed");
			return 0;
		}
	}

	val = get_param(cmd, "ICON_REQUEST");
	if (val) {
		if (count || count2) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Wait before "
					"sending out second query");
			sleep(1);
		}

		snprintf(buf, end - pos,
			 "HS20_ICON_REQUEST %s %s", dest, val);
		if (wpa_command(intf, buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,HS20_ICON_REQUEST failed");
			return 0;
		}
	}

	return 1;
}


static int ath_sta_send_frame_vht(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	const char *val;
	char *ifname;
	char buf[100];
	int chwidth, nss;

	val = get_param(cmd, "framename");
	if (!val)
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "framename is %s", val);

	/* Command sequence to generate Op mode notification */
	if (val && strcasecmp(val, "Op_md_notif_frm") == 0) {
		ifname = get_station_ifname();

		/* Disable STBC */
		snprintf(buf, sizeof(buf),
			 "iwpriv %s tx_stbc 0", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv tx_stbc 0 failed!");
		}

		/* Extract Channel width */
		val = get_param(cmd, "Channel_width");
		if (val) {
			switch (atoi(val)) {
			case 20:
				chwidth = 0;
				break;
			case 40:
				chwidth = 1;
				break;
			case 80:
				chwidth = 2;
				break;
			case 160:
				chwidth = 3;
				break;
			default:
				chwidth = 2;
				break;
			}

			snprintf(buf, sizeof(buf), "iwpriv %s chwidth %d",
				 ifname, chwidth);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv chwidth failed!");
			}
		}

		/* Extract NSS */
		val = get_param(cmd, "NSS");
		if (val) {
			switch (atoi(val)) {
			case 1:
				nss = 1;
				break;
			case 2:
				nss = 3;
				break;
			case 3:
				nss = 7;
				break;
			default:
				/* We do not support NSS > 3 */
				nss = 3;
				break;
			}
			snprintf(buf, sizeof(buf),
				 "iwpriv %s rxchainmask %d", ifname, nss);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv rxchainmask failed!");
			}
		}

		/* Opmode notify */
		snprintf(buf, sizeof(buf), "iwpriv %s opmode_notify 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv opmode_notify failed!");
		} else {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Sent out the notify frame!");
		}
	}

	return 1;
}


static int cmd_sta_send_frame_vht(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	switch (get_driver_type()) {
	case DRIVER_ATHEROS:
		return ath_sta_send_frame_vht(dut, conn, cmd);
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported sta_set_frame(VHT) with the current driver");
		return 0;
	}
}


#ifdef __linux__
int wil6210_send_frame_60g(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	const char *frame_name = get_param(cmd, "framename");
	const char *mac = get_param(cmd, "dest_mac");

	if (!frame_name || !mac) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"framename and dest_mac must be provided");
		return -1;
	}

	if (strcasecmp(frame_name, "brp") == 0) {
		const char *l_rx = get_param(cmd, "L-RX");
		int l_rx_i;

		if (!l_rx) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"L-RX must be provided");
			return -1;
		}
		l_rx_i = atoi(l_rx);

		sigma_dut_print(dut, DUT_MSG_INFO,
				"dev_send_frame: BRP-RX, dest_mac %s, L-RX %s",
				mac, l_rx);
		if (l_rx_i != 16) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"unsupported L-RX: %s", l_rx);
			return -1;
		}

		if (wil6210_send_brp_rx(dut, mac, l_rx_i))
			return -1;
	} else if (strcasecmp(frame_name, "ssw") == 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"dev_send_frame: SLS, dest_mac %s", mac);
		if (wil6210_send_sls(dut, mac))
			return -1;
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"unsupported frame type: %s", frame_name);
		return -1;
	}

	return 1;
}
#endif /* __linux__ */


static int cmd_sta_send_frame_60g(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	switch (get_driver_type()) {
#ifdef __linux__
	case DRIVER_WIL6210:
		return wil6210_send_frame_60g(dut, conn, cmd);
#endif /* __linux__ */
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported sta_set_frame(60G) with the current driver");
		return 0;
	}
}


static int mbo_send_anqp_query(struct sigma_dut *dut, struct sigma_conn *conn,
			       const char *intf, struct sigma_cmd *cmd)
{
	const char *val, *addr;
	char buf[100];

	addr = get_param(cmd, "DestMac");
	if (!addr) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,AP MAC address is missing");
		return 0;
	}

	val = get_param(cmd, "ANQPQuery_ID");
	if (!val) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,Missing ANQPQuery_ID");
		return 0;
	}

	if (strcasecmp(val, "NeighborReportReq") == 0) {
		snprintf(buf, sizeof(buf), "ANQP_GET %s 272", addr);
	} else if (strcasecmp(val, "QueryListWithCellPref") == 0) {
		snprintf(buf, sizeof(buf), "ANQP_GET %s 272,mbo:2", addr);
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Invalid ANQPQuery_ID: %s",
				val);
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,Invalid ANQPQuery_ID");
		return 0;
	}

	/* Set gas_address3 field to IEEE 802.11-2012 standard compliant form
	 * (Address3 = Wildcard BSSID when sent to not-associated AP;
	 * if associated, AP BSSID).
	 */
	if (wpa_command(intf, "SET gas_address3 1") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to set gas_address3");
		return 0;
	}

	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to send ANQP query");
		return 0;
	}

	return 1;
}


static int mbo_cmd_sta_send_frame(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  const char *intf,
				  struct sigma_cmd *cmd)
{
	const char *val = get_param(cmd, "FrameName");

	if (val && strcasecmp(val, "ANQPQuery") == 0)
		return mbo_send_anqp_query(dut, conn, intf, cmd);

	return 2;
}


int cmd_sta_send_frame(struct sigma_dut *dut, struct sigma_conn *conn,
		       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;
	enum send_frame_type frame;
	enum send_frame_protection protected;
	char buf[100];
	unsigned char addr[ETH_ALEN];
	int res;

	val = get_param(cmd, "program");
	if (val == NULL)
		val = get_param(cmd, "frame");
	if (val && strcasecmp(val, "TDLS") == 0)
		return cmd_sta_send_frame_tdls(dut, conn, cmd);
	if (val && (strcasecmp(val, "HS2") == 0 ||
		    strcasecmp(val, "HS2-R2") == 0))
		return cmd_sta_send_frame_hs2(dut, conn, cmd);
	if (val && strcasecmp(val, "VHT") == 0)
		return cmd_sta_send_frame_vht(dut, conn, cmd);
	if (val && strcasecmp(val, "LOC") == 0)
		return loc_cmd_sta_send_frame(dut, conn, cmd);
	if (val && strcasecmp(val, "60GHz") == 0)
		return cmd_sta_send_frame_60g(dut, conn, cmd);
	if (val && strcasecmp(val, "MBO") == 0) {
		res = mbo_cmd_sta_send_frame(dut, conn, intf, cmd);
		if (res != 2)
			return res;
	}

	val = get_param(cmd, "TD_DISC");
	if (val) {
		if (hwaddr_aton(val, addr) < 0)
			return -1;
		snprintf(buf, sizeof(buf), "TDLS_DISCOVER %s", val);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send TDLS discovery");
			return 0;
		}
		return 1;
	}

	val = get_param(cmd, "TD_Setup");
	if (val) {
		if (hwaddr_aton(val, addr) < 0)
			return -1;
		snprintf(buf, sizeof(buf), "TDLS_SETUP %s", val);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to start TDLS setup");
			return 0;
		}
		return 1;
	}

	val = get_param(cmd, "TD_TearDown");
	if (val) {
		if (hwaddr_aton(val, addr) < 0)
			return -1;
		snprintf(buf, sizeof(buf), "TDLS_TEARDOWN %s", val);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to tear down TDLS link");
			return 0;
		}
		return 1;
	}

	val = get_param(cmd, "TD_ChannelSwitch");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,TD_ChannelSwitch not yet supported");
		return 0;
	}

	val = get_param(cmd, "TD_NF");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,TD_NF not yet supported");
		return 0;
	}

	val = get_param(cmd, "PMFFrameType");
	if (val == NULL)
		val = get_param(cmd, "FrameName");
	if (val == NULL)
		val = get_param(cmd, "Type");
	if (val == NULL)
		return -1;
	if (strcasecmp(val, "disassoc") == 0)
		frame = DISASSOC;
	else if (strcasecmp(val, "deauth") == 0)
		frame = DEAUTH;
	else if (strcasecmp(val, "saquery") == 0)
		frame = SAQUERY;
	else if (strcasecmp(val, "auth") == 0)
		frame = AUTH;
	else if (strcasecmp(val, "assocreq") == 0)
		frame = ASSOCREQ;
	else if (strcasecmp(val, "reassocreq") == 0)
		frame = REASSOCREQ;
	else if (strcasecmp(val, "neigreq") == 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Got neighbor request");

		val = get_param(cmd, "ssid");
		if (val == NULL)
			return -1;

		res = send_neighbor_request(dut, intf, val);
		if (res) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,"
				  "Failed to send neighbor report request");
			return 0;
		}

		return 1;
	} else if (strcasecmp(val, "transmgmtquery") == 0 ||
		   strcasecmp(val, "BTMQuery") == 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Got Transition Management Query");

		res = send_trans_mgmt_query(dut, intf, cmd);
		if (res) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,"
				  "Failed to send Transition Management Query");
			return 0;
		}

		return 1;
	} else {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
			  "PMFFrameType");
		return 0;
	}

	val = get_param(cmd, "PMFProtected");
	if (val == NULL)
		val = get_param(cmd, "Protected");
	if (val == NULL)
		return -1;
	if (strcasecmp(val, "Correct-key") == 0 ||
	    strcasecmp(val, "CorrectKey") == 0)
		protected = CORRECT_KEY;
	else if (strcasecmp(val, "IncorrectKey") == 0)
		protected = INCORRECT_KEY;
	else if (strcasecmp(val, "Unprotected") == 0)
		protected = UNPROTECTED;
	else {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
			  "PMFProtected");
		return 0;
	}

	if (protected != UNPROTECTED &&
	    (frame == AUTH || frame == ASSOCREQ || frame == REASSOCREQ)) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Impossible "
			  "PMFProtected for auth/assocreq/reassocreq");
		return 0;
	}

	if (if_nametoindex("sigmadut") == 0) {
		snprintf(buf, sizeof(buf),
			 "iw dev %s interface add sigmadut type monitor",
			 get_station_ifname());
		if (system(buf) != 0 ||
		    if_nametoindex("sigmadut") == 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to add "
					"monitor interface with '%s'", buf);
			return -2;
		}
	}

	if (system("ifconfig sigmadut up") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to set "
				"monitor interface up");
		return -2;
	}

	return sta_inject_frame(dut, conn, frame, protected, NULL);
}


static int cmd_sta_set_parameter_hs2(struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct sigma_cmd *cmd,
				     const char *ifname)
{
	char buf[200];
	const char *val;

	val = get_param(cmd, "ClearARP");
	if (val && atoi(val) == 1) {
		snprintf(buf, sizeof(buf), "ip neigh flush dev %s", ifname);
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to clear ARP cache");
			return 0;
		}
	}

	return 1;
}


int cmd_sta_set_parameter(struct sigma_dut *dut, struct sigma_conn *conn,
			  struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;

	if (intf == NULL)
		return -1;

	val = get_param(cmd, "program");
	if (val && (strcasecmp(val, "HS2") == 0 ||
		    strcasecmp(val, "HS2-R2") == 0))
		return cmd_sta_set_parameter_hs2(dut, conn, cmd, intf);

	return -1;
}


static int cmd_sta_set_macaddr(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *mac = get_param(cmd, "MAC");

	if (intf == NULL || mac == NULL)
		return -1;

	sigma_dut_print(dut, DUT_MSG_INFO, "Change local MAC address for "
			"interface %s to %s", intf, mac);

	if (dut->set_macaddr) {
		char buf[128];
		int res;
		if (strcasecmp(mac, "default") == 0) {
			res = snprintf(buf, sizeof(buf), "%s",
				       dut->set_macaddr);
			dut->tmp_mac_addr = 0;
		} else {
			res = snprintf(buf, sizeof(buf), "%s %s",
				       dut->set_macaddr, mac);
			dut->tmp_mac_addr = 1;
		}
		if (res < 0 || res >= (int) sizeof(buf))
			return -1;
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to set MAC "
				  "address");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(mac, "default") == 0)
		return 1;

	send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
		  "command");
	return 0;
}


static int iwpriv_tdlsoffchnmode(struct sigma_dut *dut,
				 struct sigma_conn *conn, const char *intf,
				 int val)
{
	char buf[200];
	int res;

	res = snprintf(buf, sizeof(buf), "iwpriv %s tdlsoffchnmode %d",
		       intf, val);
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to configure offchannel mode");
		return 0;
	}

	return 1;
}


static int off_chan_val(enum sec_ch_offset off)
{
	switch (off) {
	case SEC_CH_NO:
		return 0;
	case SEC_CH_40ABOVE:
		return 40;
	case SEC_CH_40BELOW:
		return -40;
	}

	return 0;
}


static int iwpriv_set_offchan(struct sigma_dut *dut, struct sigma_conn *conn,
			      const char *intf, int off_ch_num,
			      enum sec_ch_offset sec)
{
	char buf[200];
	int res;

	res = snprintf(buf, sizeof(buf), "iwpriv %s tdlsoffchan %d",
		       intf, off_ch_num);
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to set offchan");
		return 0;
	}

	res = snprintf(buf, sizeof(buf), "iwpriv %s tdlsecchnoffst %d",
		       intf, off_chan_val(sec));
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to set sec chan offset");
		return 0;
	}

	return 1;
}


static int tdls_set_offchannel_offset(struct sigma_dut *dut,
				      struct sigma_conn *conn,
				      const char *intf, int off_ch_num,
				      enum sec_ch_offset sec)
{
	char buf[200];
	int res;

	res = snprintf(buf, sizeof(buf), "DRIVER TDLSOFFCHANNEL %d",
		       off_ch_num);
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);

	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to set offchan");
		return 0;
	}
	res = snprintf(buf, sizeof(buf), "DRIVER TDLSSECONDARYCHANNELOFFSET %d",
		       off_chan_val(sec));
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);

	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to set sec chan offset");
		return 0;
	}

	return 1;
}


static int tdls_set_offchannel_mode(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    const char *intf, int val)
{
	char buf[200];
	int res;

	res = snprintf(buf, sizeof(buf), "DRIVER TDLSOFFCHANNELMODE %d",
		       val);
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);

	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			"ErrorCode,Failed to configure offchannel mode");
		return 0;
	}

	return 1;
}


static int cmd_sta_set_rfeature_tdls(const char *intf, struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct sigma_cmd *cmd)
{
	const char *val;
	enum {
		CHSM_NOT_SET,
		CHSM_ENABLE,
		CHSM_DISABLE,
		CHSM_REJREQ,
		CHSM_UNSOLRESP
	} chsm = CHSM_NOT_SET;
	int off_ch_num = -1;
	enum sec_ch_offset sec_ch = SEC_CH_NO;
	int res;

	val = get_param(cmd, "Uapsd");
	if (val) {
		char buf[100];
		if (strcasecmp(val, "Enable") == 0)
			snprintf(buf, sizeof(buf), "SET ps 99");
		else if (strcasecmp(val, "Disable") == 0)
			snprintf(buf, sizeof(buf), "SET ps 98");
		else {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,"
				  "Unsupported uapsd parameter value");
			return 0;
		}
		if (wpa_command(intf, buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to change U-APSD "
				  "powersave mode");
			return 0;
		}
	}

	val = get_param(cmd, "TPKTIMER");
	if (val && strcasecmp(val, "DISABLE") == 0) {
		if (wpa_command(intf, "SET tdls_testing 0x100")) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to enable no TPK "
				  "expiration test mode");
			return 0;
		}
		dut->no_tpk_expiration = 1;
	}

	val = get_param(cmd, "ChSwitchMode");
	if (val) {
		if (strcasecmp(val, "Enable") == 0 ||
		    strcasecmp(val, "Initiate") == 0)
			chsm = CHSM_ENABLE;
		else if (strcasecmp(val, "Disable") == 0 ||
		    strcasecmp(val, "passive") == 0)
			chsm = CHSM_DISABLE;
		else if (strcasecmp(val, "RejReq") == 0)
			chsm = CHSM_REJREQ;
		else if (strcasecmp(val, "UnSolResp") == 0)
			chsm = CHSM_UNSOLRESP;
		else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unknown ChSwitchMode value");
			return 0;
		}
	}

	val = get_param(cmd, "OffChNum");
	if (val) {
		off_ch_num = atoi(val);
		if (off_ch_num == 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Invalid OffChNum");
			return 0;
		}
	}

	val = get_param(cmd, "SecChOffset");
	if (val) {
		if (strcmp(val, "20") == 0)
			sec_ch = SEC_CH_NO;
		else if (strcasecmp(val, "40above") == 0)
			sec_ch = SEC_CH_40ABOVE;
		else if (strcasecmp(val, "40below") == 0)
			sec_ch = SEC_CH_40BELOW;
		else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unknown SecChOffset value");
			return 0;
		}
	}

	if (chsm == CHSM_NOT_SET) {
		/* no offchannel changes requested */
		return 1;
	}

	if (strcmp(intf, get_main_ifname()) != 0 &&
	    strcmp(intf, get_station_ifname()) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unknown interface");
		return 0;
	}

	switch (chsm) {
	case CHSM_NOT_SET:
		res = 1;
		break;
	case CHSM_ENABLE:
		if (off_ch_num < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Missing OffChNum argument");
			return 0;
		}
		if (wifi_chip_type == DRIVER_WCN) {
			res = tdls_set_offchannel_offset(dut, conn, intf,
							 off_ch_num, sec_ch);
		} else {
			res = iwpriv_set_offchan(dut, conn, intf, off_ch_num,
						 sec_ch);
		}
		if (res != 1)
			return res;
		if (wifi_chip_type == DRIVER_WCN)
			res = tdls_set_offchannel_mode(dut, conn, intf, 1);
		else
			res = iwpriv_tdlsoffchnmode(dut, conn, intf, 1);
		break;
	case CHSM_DISABLE:
		if (wifi_chip_type == DRIVER_WCN)
			res = tdls_set_offchannel_mode(dut, conn, intf, 2);
		else
			res = iwpriv_tdlsoffchnmode(dut, conn, intf, 2);
		break;
	case CHSM_REJREQ:
		if (wifi_chip_type == DRIVER_WCN)
			res = tdls_set_offchannel_mode(dut, conn, intf, 3);
		else
			res = iwpriv_tdlsoffchnmode(dut, conn, intf, 3);
		break;
	case CHSM_UNSOLRESP:
		if (off_ch_num < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Missing OffChNum argument");
			return 0;
		}
		if (wifi_chip_type == DRIVER_WCN) {
			res = tdls_set_offchannel_offset(dut, conn, intf,
							 off_ch_num, sec_ch);
		} else {
			res = iwpriv_set_offchan(dut, conn, intf, off_ch_num,
						 sec_ch);
		}
		if (res != 1)
			return res;
		if (wifi_chip_type == DRIVER_WCN)
			res = tdls_set_offchannel_mode(dut, conn, intf, 4);
		else
			res = iwpriv_tdlsoffchnmode(dut, conn, intf, 4);
		break;
	}

	return res;
}


static int ath_sta_set_rfeature_vht(const char *intf, struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	const char *val;
	char *token, *result;

	novap_reset(dut, intf);

	val = get_param(cmd, "nss_mcs_opt");
	if (val) {
		/* String (nss_operating_mode; mcs_operating_mode) */
		int nss, mcs;
		char buf[50];
		char *saveptr;

		token = strdup(val);
		if (!token)
			return 0;
		result = strtok_r(token, ";", &saveptr);
		if (!result) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
				"VHT NSS not specified");
			goto failed;
		}
		if (strcasecmp(result, "def") != 0) {
			nss = atoi(result);
			if (nss == 4)
				ath_disable_txbf(dut, intf);
			snprintf(buf, sizeof(buf), "iwpriv %s nss %d",
				 intf, nss);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv nss failed");
				goto failed;
			}
		}

		result = strtok_r(NULL, ";", &saveptr);
		if (!result) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
				"VHT MCS not specified");
			goto failed;
		}
		if (strcasecmp(result, "def") == 0) {
			snprintf(buf, sizeof(buf), "iwpriv %s set11NRates 0",
				 intf);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv set11NRates failed");
				goto failed;
			}

		} else {
			mcs = atoi(result);
			snprintf(buf, sizeof(buf), "iwpriv %s vhtmcs %d",
				 intf, mcs);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv vhtmcs failed");
				goto failed;
			}
		}
		/* Channel width gets messed up, fix this */
		snprintf(buf, sizeof(buf), "iwpriv %s chwidth %d",
			 intf, dut->chwidth);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv chwidth failed");
		}
	}

	return 1;
failed:
	free(token);
	return 0;
}


static int cmd_sta_set_rfeature_vht(const char *intf, struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	switch (get_driver_type()) {
	case DRIVER_ATHEROS:
		return ath_sta_set_rfeature_vht(intf, dut, conn, cmd);
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported sta_set_rfeature(VHT) with the current driver");
		return 0;
	}
}


static int btm_query_candidate_list(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	const char *bssid, *info, *op_class, *ch, *phy_type, *pref;
	int len, ret;
	char buf[10];

	/*
	 * Neighbor Report elements format:
	 * neighbor=<BSSID>,<BSSID Information>,<Operating Class>,
	 * <Channel Number>,<PHY Type>[,<hexdump of Optional Subelements>]
	 * eg: neighbor=aa:bb:cc:dd:ee:ff,17,81,6,1,030101
	 */

	bssid = get_param(cmd, "Nebor_BSSID");
	if (!bssid) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "errorCode,Nebor_BSSID is missing");
		return 0;
	}

	info = get_param(cmd, "Nebor_Bssid_Info");
	if (!info) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Using default value for Nebor_Bssid_Info: %s",
				DEFAULT_NEIGHBOR_BSSID_INFO);
		info = DEFAULT_NEIGHBOR_BSSID_INFO;
	}

	op_class = get_param(cmd, "Nebor_Op_Class");
	if (!op_class) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "errorCode,Nebor_Op_Class is missing");
		return 0;
	}

	ch = get_param(cmd, "Nebor_Op_Ch");
	if (!ch) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "errorCode,Nebor_Op_Ch is missing");
		return 0;
	}

	phy_type = get_param(cmd, "Nebor_Phy_Type");
	if (!phy_type) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Using default value for Nebor_Phy_Type: %s",
				DEFAULT_NEIGHBOR_PHY_TYPE);
		phy_type = DEFAULT_NEIGHBOR_PHY_TYPE;
	}

	/* Parse optional subelements */
	buf[0] = '\0';
	pref = get_param(cmd, "Nebor_Pref");
	if (pref) {
		/* hexdump for preferrence subelement */
		ret = snprintf(buf, sizeof(buf), ",0301%02x", atoi(pref));
		if (ret < 0 || ret >= (int) sizeof(buf)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"snprintf failed for optional subelement ret: %d",
					ret);
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,snprintf failed for subelement");
			return 0;
		}
	}

	if (!dut->btm_query_cand_list) {
		dut->btm_query_cand_list = calloc(1, NEIGHBOR_REPORT_SIZE);
		if (!dut->btm_query_cand_list) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to allocate memory for btm_query_cand_list");
			return 0;
		}
	}

	len = strlen(dut->btm_query_cand_list);
	ret = snprintf(dut->btm_query_cand_list + len,
		       NEIGHBOR_REPORT_SIZE - len, " neighbor=%s,%s,%s,%s,%s%s",
		       bssid, info, op_class, ch, phy_type, buf);
	if (ret < 0 || ret >= NEIGHBOR_REPORT_SIZE - len) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"snprintf failed for neighbor report list ret: %d",
				ret);
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,snprintf failed for neighbor report");
		free(dut->btm_query_cand_list);
		dut->btm_query_cand_list = NULL;
		return 0;
	}

	return 1;
}


static int cmd_sta_set_rfeature(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *prog = get_param(cmd, "Prog");
	const char *val;

	if (intf == NULL || prog == NULL)
		return -1;

	/* BSS Transition candidate list for BTM query */
	val = get_param(cmd, "Nebor_BSSID");
	if (val && btm_query_candidate_list(dut, conn, cmd) == 0)
		return 0;

	if (strcasecmp(prog, "TDLS") == 0)
		return cmd_sta_set_rfeature_tdls(intf, dut, conn, cmd);

	if (strcasecmp(prog, "VHT") == 0)
		return cmd_sta_set_rfeature_vht(intf, dut, conn, cmd);

	if (strcasecmp(prog, "MBO") == 0) {
		val = get_param(cmd, "Cellular_Data_Cap");
		if (val &&
		    mbo_set_cellular_data_capa(dut, conn, intf, atoi(val)) == 0)
			return 0;

		val = get_param(cmd, "Ch_Pref");
		if (val && mbo_set_non_pref_ch_list(dut, conn, intf, cmd) == 0)
			return 0;

		return 1;
	}

	send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported Prog");
	return 0;
}


static int cmd_sta_set_radio(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *mode = get_param(cmd, "Mode");
	int res;

	if (intf == NULL || mode == NULL)
		return -1;

	if (strcasecmp(mode, "On") == 0)
		res = wpa_command(intf, "SET radio_disabled 0");
	else if (strcasecmp(mode, "Off") == 0)
		res = wpa_command(intf, "SET radio_disabled 1");
	else
		return -1;

	if (res) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to change "
			  "radio mode");
		return 0;
	}

	return 1;
}


static int cmd_sta_set_pwrsave(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *mode = get_param(cmd, "Mode");
	int res;

	if (intf == NULL || mode == NULL)
		return -1;

	if (strcasecmp(mode, "On") == 0)
		res = set_ps(intf, dut, 1);
	else if (strcasecmp(mode, "Off") == 0)
		res = set_ps(intf, dut, 0);
	else
		return -1;

	if (res) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to change "
			  "power save mode");
		return 0;
	}

	return 1;
}


static int cmd_sta_bssid_pool(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val, *bssid;
	int res;
	char *buf;
	size_t buf_len;

	val = get_param(cmd, "BSSID_FILTER");
	if (val == NULL)
		return -1;

	bssid = get_param(cmd, "BSSID_List");
	if (atoi(val) == 0 || bssid == NULL) {
		/* Disable BSSID filter */
		if (wpa_command(intf, "SET bssid_filter ")) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed "
				  "to disable BSSID filter");
			return 0;
		}

		return 1;
	}

	buf_len = 100 + strlen(bssid);
	buf = malloc(buf_len);
	if (buf == NULL)
		return -1;

	snprintf(buf, buf_len, "SET bssid_filter %s", bssid);
	res = wpa_command(intf, buf);
	free(buf);
	if (res) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to enable "
			  "BSSID filter");
		return 0;
	}

	return 1;
}


static int cmd_sta_reset_parm(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;

	/* TODO: ARP */

	val = get_param(cmd, "HS2_CACHE_PROFILE");
	if (val && strcasecmp(val, "All") == 0)
		hs2_clear_credentials(intf);

	return 1;
}


static int cmd_sta_get_key(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *key_type = get_param(cmd, "KeyType");
	char buf[100], resp[200];

	if (key_type == NULL)
		return -1;

	if (strcasecmp(key_type, "GTK") == 0) {
		if (wpa_command_resp(intf, "GET gtk", buf, sizeof(buf)) < 0 ||
		    strncmp(buf, "FAIL", 4) == 0) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
				  "not fetch current GTK");
			return 0;
		}
		snprintf(resp, sizeof(resp), "KeyValue,%s", buf);
		send_resp(dut, conn, SIGMA_COMPLETE, resp);
		return 0;
	} else {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
			  "KeyType");
		return 0;
	}

	return 1;
}


static int hs2_set_policy(struct sigma_dut *dut)
{
#ifdef ANDROID
	system("ip rule del prio 23000");
	if (system("ip rule add from all lookup main prio 23000") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to run:ip rule add from all lookup main prio");
		return -1;
	}
	if (system("ip route flush cache") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to run ip route flush cache");
		return -1;
	}
	return 1;
#else /* ANDROID */
	return 0;
#endif /* ANDROID */
}


static int cmd_sta_hs2_associate(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val = get_param(cmd, "Ignore_blacklist");
	struct wpa_ctrl *ctrl;
	int res;
	char bssid[20], ssid[40], resp[100], buf[100], blacklisted[100];
	int tries = 0;
	int ignore_blacklist = 0;
	const char *events[] = {
		"CTRL-EVENT-CONNECTED",
		"INTERWORKING-BLACKLISTED",
		"INTERWORKING-NO-MATCH",
		NULL
	};

	start_sta_mode(dut);

	blacklisted[0] = '\0';
	if (val && atoi(val))
		ignore_blacklist = 1;

try_again:
	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	tries++;
	if (wpa_command(intf, "INTERWORKING_SELECT auto")) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to start "
			  "Interworking connection");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return 0;
	}

	buf[0] = '\0';
	while (1) {
		char *pos;
		res = get_wpa_cli_events(dut, ctrl, events, buf, sizeof(buf));
		pos = strstr(buf, "INTERWORKING-BLACKLISTED");
		if (!pos)
			break;
		pos += 25;
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Found blacklisted AP: %s",
				pos);
		if (!blacklisted[0])
			memcpy(blacklisted, pos, strlen(pos) + 1);
	}

	if (ignore_blacklist && blacklisted[0]) {
		char *end;
		end = strchr(blacklisted, ' ');
		if (end)
			*end = '\0';
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Try to connect to a blacklisted network: %s",
				blacklisted);
		snprintf(buf, sizeof(buf), "INTERWORKING_CONNECT %s",
			 blacklisted);
		if (wpa_command(intf, buf)) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to start Interworking connection to blacklisted network");
			wpa_ctrl_detach(ctrl);
			wpa_ctrl_close(ctrl);
			return 0;
		}
		res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-CONNECTED",
					buf, sizeof(buf));
	}

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "connect");
		return 0;
	}

	if (strstr(buf, "INTERWORKING-NO-MATCH") ||
	    strstr(buf, "INTERWORKING-BLACKLISTED")) {
		if (tries < 2) {
			sigma_dut_print(dut, DUT_MSG_INFO, "No match found - try again to verify no APs were missed in the scan");
			goto try_again;
		}
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,No network with "
			  "matching credentials found");
		return 0;
	}

	if (get_wpa_status(intf, "bssid", bssid, sizeof(bssid)) < 0 ||
	    get_wpa_status(intf, "ssid", ssid, sizeof(ssid)) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "get current BSSID/SSID");
		return 0;
	}

	snprintf(resp, sizeof(resp), "SSID,%s,BSSID,%s", ssid, bssid);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	hs2_set_policy(dut);
	return 0;
}


static int sta_add_credential_uname_pwd(struct sigma_dut *dut,
					struct sigma_conn *conn,
					const char *ifname,
					struct sigma_cmd *cmd)
{
	const char *val;
	int id;

	id = add_cred(ifname);
	if (id < 0)
		return -2;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Adding credential %d", id);

	val = get_param(cmd, "prefer");
	if (val && atoi(val) > 0)
		set_cred(ifname, id, "priority", "1");

	val = get_param(cmd, "REALM");
	if (val && set_cred_quoted(ifname, id, "realm", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "realm");
		return 0;
	}

	val = get_param(cmd, "HOME_FQDN");
	if (val && set_cred_quoted(ifname, id, "domain", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "home_fqdn");
		return 0;
	}

	val = get_param(cmd, "Username");
	if (val && set_cred_quoted(ifname, id, "username", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "username");
		return 0;
	}

	val = get_param(cmd, "Password");
	if (val && set_cred_quoted(ifname, id, "password", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "password");
		return 0;
	}

	val = get_param(cmd, "ROOT_CA");
	if (val) {
		char fname[200];
		snprintf(fname, sizeof(fname), "%s/%s", sigma_cert_path, val);
#ifdef __linux__
		if (!file_exists(fname)) {
			char msg[300];
			snprintf(msg, sizeof(msg), "ErrorCode,ROOT_CA "
				 "file (%s) not found", fname);
			send_resp(dut, conn, SIGMA_ERROR, msg);
			return 0;
		}
#endif /* __linux__ */
		if (set_cred_quoted(ifname, id, "ca_cert", fname) < 0) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
				  "not set root CA");
			return 0;
		}
	}

	return 1;
}


static int update_devdetail_imsi(struct sigma_dut *dut, const char *imsi)
{
	FILE *in, *out;
	char buf[500];
	int found = 0;

	in = fopen("devdetail.xml", "r");
	if (in == NULL)
		return -1;
	out = fopen("devdetail.xml.tmp", "w");
	if (out == NULL) {
		fclose(in);
		return -1;
	}

	while (fgets(buf, sizeof(buf), in)) {
		char *pos = strstr(buf, "<IMSI>");
		if (pos) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Updated DevDetail IMSI to %s",
					imsi);
			pos += 6;
			*pos = '\0';
			fprintf(out, "%s%s</IMSI>\n", buf, imsi);
			found++;
		} else {
			fprintf(out, "%s", buf);
		}
	}

	fclose(out);
	fclose(in);
	if (found)
		rename("devdetail.xml.tmp", "devdetail.xml");
	else
		unlink("devdetail.xml.tmp");

	return 0;
}


static int sta_add_credential_sim(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  const char *ifname, struct sigma_cmd *cmd)
{
	const char *val, *imsi = NULL;
	int id;
	char buf[200];
	int res;
	const char *pos;
	size_t mnc_len;
	char plmn_mcc[4];
	char plmn_mnc[4];

	id = add_cred(ifname);
	if (id < 0)
		return -2;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Adding credential %d", id);

	val = get_param(cmd, "prefer");
	if (val && atoi(val) > 0)
		set_cred(ifname, id, "priority", "1");

	val = get_param(cmd, "PLMN_MCC");
	if (val == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing PLMN_MCC");
		return 0;
	}
	if (strlen(val) != 3) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Invalid MCC");
		return 0;
	}
	snprintf(plmn_mcc, sizeof(plmn_mcc), "%s", val);

	val = get_param(cmd, "PLMN_MNC");
	if (val == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing PLMN_MNC");
		return 0;
	}
	if (strlen(val) != 2 && strlen(val) != 3) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Invalid MNC");
		return 0;
	}
	snprintf(plmn_mnc, sizeof(plmn_mnc), "%s", val);

	val = get_param(cmd, "IMSI");
	if (val == NULL) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Missing SIM "
			  "IMSI");
		return 0;
	}

	imsi = pos = val;

	if (strncmp(plmn_mcc, pos, 3) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,MCC mismatch");
		return 0;
	}
	pos += 3;

	mnc_len = strlen(plmn_mnc);
	if (mnc_len < 2) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,MNC not set");
		return 0;
	}

	if (strncmp(plmn_mnc, pos, mnc_len) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,MNC mismatch");
		return 0;
	}
	pos += mnc_len;

	res = snprintf(buf, sizeof(buf), "%s%s-%s",plmn_mcc, plmn_mnc, pos);
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	if (set_cred_quoted(ifname, id, "imsi", buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
			  "not set IMSI");
		return 0;
	}

	val = get_param(cmd, "Password");
	if (val && set_cred_quoted(ifname, id, "milenage", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
			  "not set password");
		return 0;
	}

	if (dut->program == PROGRAM_HS2_R2) {
		/*
		 * Set provisioning_sp for the test cases where SIM/USIM
		 * provisioning is used.
		 */
		if (val && set_cred_quoted(ifname, id, "provisioning_sp",
					   "wi-fi.org") < 0) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
				  "not set provisioning_sp");
			return 0;
		}

		update_devdetail_imsi(dut, imsi);
	}

	return 1;
}


static int sta_add_credential_cert(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   const char *ifname,
				   struct sigma_cmd *cmd)
{
	const char *val;
	int id;

	id = add_cred(ifname);
	if (id < 0)
		return -2;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Adding credential %d", id);

	val = get_param(cmd, "prefer");
	if (val && atoi(val) > 0)
		set_cred(ifname, id, "priority", "1");

	val = get_param(cmd, "REALM");
	if (val && set_cred_quoted(ifname, id, "realm", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "realm");
		return 0;
	}

	val = get_param(cmd, "HOME_FQDN");
	if (val && set_cred_quoted(ifname, id, "domain", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "home_fqdn");
		return 0;
	}

	val = get_param(cmd, "Username");
	if (val && set_cred_quoted(ifname, id, "username", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "username");
		return 0;
	}

	val = get_param(cmd, "clientCertificate");
	if (val) {
		char fname[200];
		snprintf(fname, sizeof(fname), "%s/%s", sigma_cert_path, val);
#ifdef __linux__
		if (!file_exists(fname)) {
			char msg[300];
			snprintf(msg, sizeof(msg),
				 "ErrorCode,clientCertificate "
				 "file (%s) not found", fname);
			send_resp(dut, conn, SIGMA_ERROR, msg);
			return 0;
		}
#endif /* __linux__ */
		if (set_cred_quoted(ifname, id, "client_cert", fname) < 0) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
				  "not set client_cert");
			return 0;
		}
		if (set_cred_quoted(ifname, id, "private_key", fname) < 0) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
				  "not set private_key");
			return 0;
		}
	}

	val = get_param(cmd, "ROOT_CA");
	if (val) {
		char fname[200];
		snprintf(fname, sizeof(fname), "%s/%s", sigma_cert_path, val);
#ifdef __linux__
		if (!file_exists(fname)) {
			char msg[300];
			snprintf(msg, sizeof(msg), "ErrorCode,ROOT_CA "
				 "file (%s) not found", fname);
			send_resp(dut, conn, SIGMA_ERROR, msg);
			return 0;
		}
#endif /* __linux__ */
		if (set_cred_quoted(ifname, id, "ca_cert", fname) < 0) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
				  "not set root CA");
			return 0;
		}
	}

	return 1;
}


static int cmd_sta_add_credential(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *type;

	start_sta_mode(dut);

	type = get_param(cmd, "Type");
	if (!type)
		return -1;

	if (strcasecmp(type, "uname_pwd") == 0)
		return sta_add_credential_uname_pwd(dut, conn, intf, cmd);

	if (strcasecmp(type, "sim") == 0)
		return sta_add_credential_sim(dut, conn, intf, cmd);

	if (strcasecmp(type, "cert") == 0)
		return sta_add_credential_cert(dut, conn, intf, cmd);

	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported credential "
		  "type");
	return 0;
}


static int cmd_sta_scan(struct sigma_dut *dut, struct sigma_conn *conn,
			struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;
	char buf[100];
	int res;

	val = get_param(cmd, "HESSID");
	if (val) {
		res = snprintf(buf, sizeof(buf), "SET hessid %s", val);
		if (res < 0 || res >= (int) sizeof(buf))
			return -1;
		wpa_command(intf, buf);
	}

	val = get_param(cmd, "ACCS_NET_TYPE");
	if (val) {
		res = snprintf(buf, sizeof(buf), "SET access_network_type %s",
			       val);
		if (res < 0 || res >= (int) sizeof(buf))
			return -1;
		wpa_command(intf, buf);
	}

	if (wpa_command(intf, "SCAN")) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not start "
			  "scan");
		return 0;
	}

	return 1;
}


static int cmd_sta_set_systime(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
#ifdef __linux__
	struct timeval tv;
	struct tm tm;
	time_t t;
	const char *val;
	int v;

	wpa_command(get_station_ifname(), "PMKSA_FLUSH");

	memset(&tm, 0, sizeof(tm));
	val = get_param(cmd, "seconds");
	if (val)
		tm.tm_sec = atoi(val);
	val = get_param(cmd, "minutes");
	if (val)
		tm.tm_min = atoi(val);
	val = get_param(cmd, "hours");
	if (val)
		tm.tm_hour = atoi(val);
	val = get_param(cmd, "date");
	if (val)
		tm.tm_mday = atoi(val);
	val = get_param(cmd, "month");
	if (val) {
		v = atoi(val);
		if (v < 1 || v > 12) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid month");
			return 0;
		}
		tm.tm_mon = v - 1;
	}
	val = get_param(cmd, "year");
	if (val) {
		int year = atoi(val);
#ifdef ANDROID
		if (year > 2035)
			year = 2035; /* years beyond 2035 not supported */
#endif /* ANDROID */
		tm.tm_year = year - 1900;
	}
	t = mktime(&tm);
	if (t == (time_t) -1) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Invalid date or time");
		return 0;
	}

	memset(&tv, 0, sizeof(tv));
	tv.tv_sec = t;

	if (settimeofday(&tv, NULL) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "settimeofday failed: %s",
				strerror(errno));
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to set time");
		return 0;
	}

	return 1;
#endif /* __linux__ */

	return -1;
}


static int cmd_sta_osu(struct sigma_dut *dut, struct sigma_conn *conn,
		       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *name, *val;
	int prod_ess_assoc = 1;
	char buf[200], bssid[100], ssid[100];
	int res;
	struct wpa_ctrl *ctrl;

	name = get_param(cmd, "osuFriendlyName");

	val = get_param(cmd, "ProdESSAssoc");
	if (val)
		prod_ess_assoc = atoi(val);

	kill_dhcp_client(dut, intf);
	if (start_dhcp_client(dut, intf) < 0)
		return -2;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Trigger OSU");
	mkdir("Logs", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	res = snprintf(buf, sizeof(buf),
		       "%s %s%s%s signup osu-ca.pem",
		       prod_ess_assoc ? "" : "-N",
		       name ? "-O'" : "", name ? name : "",
		       name ? "'" : "");

	hs2_set_policy(dut);
	if (run_hs20_osu(dut, buf) < 0) {
		FILE *f;

		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to complete OSU");

		f = fopen("hs20-osu-client.res", "r");
		if (f) {
			char resp[400], res[300], *pos;
			if (!fgets(res, sizeof(res), f))
				res[0] = '\0';
			pos = strchr(res, '\n');
			if (pos)
				*pos = '\0';
			fclose(f);
			sigma_dut_summary(dut, "hs20-osu-client provisioning failed: %s",
					  res);
			snprintf(resp, sizeof(resp), "notify-send '%s'", res);
			if (system(resp) != 0) {
			}
			snprintf(resp, sizeof(resp),
				 "SSID,,BSSID,,failureReason,%s", res);
			send_resp(dut, conn, SIGMA_COMPLETE, resp);
			return 0;
		}

		send_resp(dut, conn, SIGMA_COMPLETE, "SSID,,BSSID,");
		return 0;
	}

	if (!prod_ess_assoc)
		goto report;

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -1;
	}

	res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-CONNECTED",
				buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to connect to "
				"network after OSU");
		send_resp(dut, conn, SIGMA_COMPLETE, "SSID,,BSSID,");
		return 0;
	}

report:
	if (get_wpa_status(intf, "bssid", bssid, sizeof(bssid)) < 0 ||
	    get_wpa_status(intf, "ssid", ssid, sizeof(ssid)) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to get BSSID/SSID");
		send_resp(dut, conn, SIGMA_COMPLETE, "SSID,,BSSID,");
		return 0;
	}

	snprintf(buf, sizeof(buf), "SSID,%s,BSSID,%s", ssid, bssid);
	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return 0;
}


static int cmd_sta_policy_update(struct sigma_dut *dut, struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *val;
	int timeout = 120;

	val = get_param(cmd, "PolicyUpdate");
	if (val == NULL || atoi(val) == 0)
		return 1; /* No operation requested */

	val = get_param(cmd, "Timeout");
	if (val)
		timeout = atoi(val);

	if (timeout) {
		/* TODO: time out the command and return
		 * PolicyUpdateStatus,TIMEOUT if needed. */
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Trigger policy update");
	mkdir("Logs", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	if (run_hs20_osu(dut, "pol_upd fqdn=wi-fi.org") < 0) {
		send_resp(dut, conn, SIGMA_COMPLETE, "PolicyUpdateStatus,FAIL");
		return 0;
	}

	send_resp(dut, conn, SIGMA_COMPLETE, "PolicyUpdateStatus,SUCCESS");
	return 0;
}


static int cmd_sta_er_config(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	struct wpa_ctrl *ctrl;
	const char *intf = get_param(cmd, "Interface");
	const char *bssid = get_param(cmd, "Bssid");
	const char *ssid = get_param(cmd, "SSID");
	const char *security = get_param(cmd, "Security");
	const char *passphrase = get_param(cmd, "Passphrase");
	const char *pin = get_param(cmd, "PIN");
	char buf[1000];
	char ssid_hex[200], passphrase_hex[200];
	const char *keymgmt, *cipher;

	if (intf == NULL)
		intf = get_main_ifname();

	if (!bssid) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing Bssid argument");
		return 0;
	}

	if (!ssid) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing SSID argument");
		return 0;
	}

	if (!security) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing Security argument");
		return 0;
	}

	if (!passphrase) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing Passphrase argument");
		return 0;
	}

	if (!pin) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing PIN argument");
		return 0;
	}

	if (2 * strlen(ssid) >= sizeof(ssid_hex) ||
	    2 * strlen(passphrase) >= sizeof(passphrase_hex)) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Too long SSID/passphrase");
		return 0;
	}

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	if (strcasecmp(security, "wpa2-psk") == 0) {
		keymgmt = "WPA2PSK";
		cipher = "CCMP";
	} else {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported Security value");
		return 0;
	}

	ascii2hexstr(ssid, ssid_hex);
	ascii2hexstr(passphrase, passphrase_hex);
	snprintf(buf, sizeof(buf), "WPS_REG %s %s %s %s %s %s",
		 bssid, pin, ssid_hex, keymgmt, cipher, passphrase_hex);

	if (wpa_command(intf, buf) < 0) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to start registrar");
		return 0;
	}

	snprintf(dut->er_oper_bssid, sizeof(dut->er_oper_bssid), "%s", bssid);
	dut->er_oper_performed = 1;

	return wps_connection_event(dut, conn, ctrl, intf, 0);
}


static int cmd_sta_wps_connect_pw_token(struct sigma_dut *dut,
					struct sigma_conn *conn,
					struct sigma_cmd *cmd)
{
	struct wpa_ctrl *ctrl;
	const char *intf = get_param(cmd, "Interface");
	const char *bssid = get_param(cmd, "Bssid");
	char buf[100];

	if (!bssid) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing Bssid argument");
		return 0;
	}

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	snprintf(buf, sizeof(buf), "WPS_NFC %s", bssid);

	if (wpa_command(intf, buf) < 0) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to start registrar");
		return 0;
	}

	return wps_connection_event(dut, conn, ctrl, intf, 0);
}


static int req_intf(struct sigma_cmd *cmd)
{
	return get_param(cmd, "interface") == NULL ? -1 : 0;
}


void sta_register_cmds(void)
{
	sigma_dut_reg_cmd("sta_get_ip_config", req_intf,
			  cmd_sta_get_ip_config);
	sigma_dut_reg_cmd("sta_set_ip_config", req_intf,
			  cmd_sta_set_ip_config);
	sigma_dut_reg_cmd("sta_get_info", req_intf, cmd_sta_get_info);
	sigma_dut_reg_cmd("sta_get_mac_address", req_intf,
			  cmd_sta_get_mac_address);
	sigma_dut_reg_cmd("sta_is_connected", req_intf, cmd_sta_is_connected);
	sigma_dut_reg_cmd("sta_verify_ip_connection", req_intf,
			  cmd_sta_verify_ip_connection);
	sigma_dut_reg_cmd("sta_get_bssid", req_intf, cmd_sta_get_bssid);
	sigma_dut_reg_cmd("sta_set_encryption", req_intf,
			  cmd_sta_set_encryption);
	sigma_dut_reg_cmd("sta_set_psk", req_intf, cmd_sta_set_psk);
	sigma_dut_reg_cmd("sta_set_eaptls", req_intf, cmd_sta_set_eaptls);
	sigma_dut_reg_cmd("sta_set_eapttls", req_intf, cmd_sta_set_eapttls);
	sigma_dut_reg_cmd("sta_set_eapsim", req_intf, cmd_sta_set_eapsim);
	sigma_dut_reg_cmd("sta_set_peap", req_intf, cmd_sta_set_peap);
	sigma_dut_reg_cmd("sta_set_eapfast", req_intf, cmd_sta_set_eapfast);
	sigma_dut_reg_cmd("sta_set_eapaka", req_intf, cmd_sta_set_eapaka);
	sigma_dut_reg_cmd("sta_set_eapakaprime", req_intf,
			  cmd_sta_set_eapakaprime);
	sigma_dut_reg_cmd("sta_set_security", req_intf, cmd_sta_set_security);
	sigma_dut_reg_cmd("sta_set_uapsd", req_intf, cmd_sta_set_uapsd);
	/* TODO: sta_set_ibss */
	/* TODO: sta_set_mode */
	sigma_dut_reg_cmd("sta_set_wmm", req_intf, cmd_sta_set_wmm);
	sigma_dut_reg_cmd("sta_associate", req_intf, cmd_sta_associate);
	/* TODO: sta_up_load */
	sigma_dut_reg_cmd("sta_preset_testparameters", req_intf,
			  cmd_sta_preset_testparameters);
	/* TODO: sta_set_system */
	sigma_dut_reg_cmd("sta_set_11n", req_intf, cmd_sta_set_11n);
	/* TODO: sta_set_rifs_test */
	sigma_dut_reg_cmd("sta_set_wireless", req_intf, cmd_sta_set_wireless);
	sigma_dut_reg_cmd("sta_send_addba", req_intf, cmd_sta_send_addba);
	/* TODO: sta_send_coexist_mgmt */
	sigma_dut_reg_cmd("sta_disconnect", req_intf, cmd_sta_disconnect);
	sigma_dut_reg_cmd("sta_reassoc", req_intf, cmd_sta_reassoc);
	sigma_dut_reg_cmd("sta_reassociate", req_intf, cmd_sta_reassoc);
	sigma_dut_reg_cmd("sta_reset_default", req_intf,
			  cmd_sta_reset_default);
	sigma_dut_reg_cmd("sta_send_frame", req_intf, cmd_sta_send_frame);
	sigma_dut_reg_cmd("sta_set_macaddr", req_intf, cmd_sta_set_macaddr);
	sigma_dut_reg_cmd("sta_set_rfeature", req_intf, cmd_sta_set_rfeature);
	sigma_dut_reg_cmd("sta_set_radio", req_intf, cmd_sta_set_radio);
	sigma_dut_reg_cmd("sta_set_pwrsave", req_intf, cmd_sta_set_pwrsave);
	sigma_dut_reg_cmd("sta_bssid_pool", req_intf, cmd_sta_bssid_pool);
	sigma_dut_reg_cmd("sta_reset_parm", req_intf, cmd_sta_reset_parm);
	sigma_dut_reg_cmd("sta_get_key", req_intf, cmd_sta_get_key);
	sigma_dut_reg_cmd("sta_hs2_associate", req_intf,
			  cmd_sta_hs2_associate);
	sigma_dut_reg_cmd("sta_add_credential", req_intf,
			  cmd_sta_add_credential);
	sigma_dut_reg_cmd("sta_scan", req_intf, cmd_sta_scan);
	sigma_dut_reg_cmd("sta_set_systime", NULL, cmd_sta_set_systime);
	sigma_dut_reg_cmd("sta_osu", req_intf, cmd_sta_osu);
	sigma_dut_reg_cmd("sta_policy_update", req_intf, cmd_sta_policy_update);
	sigma_dut_reg_cmd("sta_er_config", NULL, cmd_sta_er_config);
	sigma_dut_reg_cmd("sta_wps_connect_pw_token", req_intf,
			  cmd_sta_wps_connect_pw_token);
	sigma_dut_reg_cmd("sta_exec_action", req_intf, cmd_sta_exec_action);
	sigma_dut_reg_cmd("sta_get_events", req_intf, cmd_sta_get_events);
	sigma_dut_reg_cmd("sta_get_parameter", req_intf, cmd_sta_get_parameter);
}
