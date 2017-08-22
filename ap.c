/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010-2011, Atheros Communications, Inc.
 * Copyright (c) 2011-2015, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <limits.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#endif /* __linux__ */
#ifdef __QNXNTO__
#include <ifaddrs.h>
#include <net/if_dl.h>
#endif /* __QNXNTO__ */
#include "wpa_helpers.h"
#ifdef ANDROID
#include <hardware_legacy/wifi.h>
#include <private/android_filesystem_config.h>
#endif /* ANDROID */

/* Temporary files for ap_send_addba_req */
#define VI_QOS_TMP_FILE     "/tmp/vi-qos.tmp"
#define VI_QOS_FILE         "/tmp/vi-qos.txt"
#define VI_QOS_REFFILE      "/etc/vi-qos.txt"

/* Configuration file name on Android */
#ifndef ANDROID_CONFIG_FILE
#define ANDROID_CONFIG_FILE		"/data/misc/wifi/hostapd.conf"
#endif /* ANDROID_CONFIG_FILE */
/* Maximum length of the line in the configuration file */
#define MAX_CONF_LINE_LEN  		(156)

#ifndef SIGMA_DUT_HOSTAPD_PID_FILE
#define SIGMA_DUT_HOSTAPD_PID_FILE "/tmp/sigma_dut-ap-hostapd.pid"
#endif /* SIGMA_DUT_HOSTAPD_PID_FILE */

/* The following is taken from Hotspot 2.0 testplan Appendix B.1 */
#define ANQP_VENUE_NAME_1 "02019c0002083d656e6757692d466920416c6c69616e63650a3239383920436f7070657220526f61640a53616e746120436c6172612c2043412039353035312c205553415b63686957692d4669e88194e79b9fe5ae9ee9aa8ce5aea40ae4ba8ce4b99de585abe4b99de5b9b4e5ba93e69f8fe8b7af0ae59ca3e5858be68b89e68b892c20e58aa0e588a9e7a68fe5b0bce4ba9a39353035312c20e7be8ee59bbd"
#define ANQP_VENUE_NAME_1_CHI "P\"\x63\x68\x69\x3a\x57\x69\x2d\x46\x69\xe8\x81\x94\xe7\x9b\x9f\xe5\xae\x9e\xe9\xaa\x8c\xe5\xae\xa4\\n\xe4\xba\x8c\xe4\xb9\x9d\xe5\x85\xab\xe4\xb9\x9d\xe5\xb9\xb4\xe5\xba\x93\xe6\x9f\x8f\xe8\xb7\xaf\\n\xe5\x9c\xa3\xe5\x85\x8b\xe6\x8b\x89\xe6\x8b\x89\x2c\x20\xe5\x8a\xa0\xe5\x88\xa9\xe7\xa6\x8f\xe5\xb0\xbc\xe4\xba\x9a\x39\x35\x30\x35\x31\x2c\x20\xe7\xbe\x8e\xe5\x9b\xbd\""
#define ANQP_IP_ADDR_TYPE_1 "060101000c"
#define ANQP_HS20_OPERATOR_FRIENDLY_NAME_1 "dddd2700506f9a11030011656e6757692d466920416c6c69616e63650e63686957692d4669e88194e79b9f"
#define ANQP_HS20_WAN_METRICS_1 "dddd1300506f9a11040001c40900008001000000000000"
#define ANQP_HS20_CONNECTION_CAPABILITY_1 "dddd3200506f9a1105000100000006140001061600000650000106bb010106bb060006c4130011f4010111c413001194110132000001"
#define QOS_MAP_SET_1 "53,2,22,6,8,15,0,7,255,255,16,31,32,39,255,255,40,47,255,255"
#define QOS_MAP_SET_2 "8,15,0,7,255,255,16,31,32,39,255,255,40,47,48,63"

extern char *sigma_main_ifname;
extern char *sigma_wpas_ctrl;
extern char *sigma_hapd_ctrl;
extern char *ap_inet_addr;
extern char *ap_inet_mask;
extern char *sigma_radio_ifname[];

static int cmd_ap_config_commit(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd);
static int ath_ap_start_hostapd(struct sigma_dut *dut);
static void ath_ap_set_params(struct sigma_dut *dut);
static int kill_process(struct sigma_dut *dut, char *proc_name,
			unsigned char is_proc_instance_one, int sig);


static int cmd_ap_ca_version(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	send_resp(dut, conn, SIGMA_COMPLETE, "version,1.0");
	return 0;
}


static void kill_hostapd_process_pid(struct sigma_dut *dut)
{
	FILE *f;
	int pid, res;
	char path[100];
	int count;

	f = fopen(SIGMA_DUT_HOSTAPD_PID_FILE, "r");
	if (!f)
		return;
	res = fscanf(f, "%d", &pid);
	fclose(f);
	if (res != 1)
		return;
	sigma_dut_print(dut, DUT_MSG_INFO, "Killing hostapd pid %d", pid);
	kill(pid, SIGTERM);
	snprintf(path, sizeof(path), "/proc/%d", pid);
	for (count = 0; count < 20 && file_exists(path); count++)
		usleep(100000);
}


static int get_hwaddr(const char *ifname, unsigned char *hwaddr)
{
#ifndef __QNXNTO__
	struct ifreq ifr;
	int s;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return -1;
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0) {
		perror("ioctl");
		close(s);
		return -1;
	}
	close(s);
	memcpy(hwaddr, ifr.ifr_hwaddr.sa_data, 6);
#else /* __QNXNTO__ */
	struct ifaddrs *ifaddrshead = NULL;
	int found = 0;
	struct ifaddrs *temp_ifap = NULL;
	struct sockaddr_dl *sdl = NULL;

	if (getifaddrs(&ifaddrshead) != 0) {
		perror("getifaddrs failed");
		return -1;
	}

	for (temp_ifap = ifaddrshead; ifaddrshead && !found;
	     ifaddrshead = ifaddrshead->ifa_next) {
		if (ifaddrshead->ifa_addr->sa_family == AF_LINK &&
		    strcmp(ifaddrshead->ifa_name, ifname) == 0) {
			found = 1;
			sdl = (struct sockaddr_dl *) ifaddrshead->ifa_addr;
			if (sdl)
				memcpy(hwaddr, LLADDR(sdl), sdl->sdl_alen);
		}
	}

	if (temp_ifap)
		freeifaddrs(temp_ifap);

	if (!found) {
		perror("Failed to get the interface");
		return -1;
	}
#endif /* __QNXNTO__ */
	return 0;
}


static void ath_ap_set_group_id(struct sigma_dut *dut, const char *ifname,
				const char *val)
{
	char buf[60];

	snprintf(buf, sizeof(buf), "wifitool %s beeliner_fw_test 55 %d",
		 ifname, atoi(val));
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"wifitool ap_group_id failed");
	}
}


void ath_set_cts_width(struct sigma_dut *dut, const char *ifname,
		       const char *val)
{
	char buf[60];

	/* TODO: Enable support for other values */
	if (strcasecmp(val, "40") == 0) {
		snprintf(buf, sizeof(buf), "wifitool %s beeliner_fw_test 54 1",
			 ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"wifitool cts_width failed");
		}
		snprintf(buf, sizeof(buf),
			 "athdiag --set --address=0x10024  --val=0xd90b8a14");
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"disabling phy restart failed");
		}
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Unsupported CTS_WIDTH");
	}
}


void ath_config_dyn_bw_sig(struct sigma_dut *dut, const char *ifname,
			   const char *val)
{
	char buf[60];

	if (strcasecmp(val, "enable") == 0) {
		dut->ap_dyn_bw_sig = VALUE_ENABLED;
		snprintf(buf, sizeof(buf), "iwpriv %s cwmenable 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv cwmenable 1 failed");
		}
		snprintf(buf, sizeof(buf), "wifitool %s beeliner_fw_test 96 1",
			 ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"disabling RTS from rate control logic failed");
		}
	} else if (strcasecmp(val, "disable") == 0) {
		dut->ap_dyn_bw_sig = VALUE_DISABLED;
		snprintf(buf, sizeof(buf), "iwpriv %s cwmenable 0", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv cwmenable 0 failed");
		}
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Unsupported DYN_BW_SGL");
	}
}


static void ath_config_rts_force(struct sigma_dut *dut, const char *ifname,
				 const char *val)
{
	char buf[60];

	if (strcasecmp(val, "enable") == 0) {
		dut->ap_sig_rts = VALUE_ENABLED;
		snprintf(buf, sizeof(buf), "iwconfig %s rts 64", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwconfig rts 64 failed");
		}
		snprintf(buf, sizeof(buf), "wifitool %s beeliner_fw_test 100 1",
			 ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"wifitool beeliner_fw_test 100 1 failed");
		}
	} else if (strcasecmp(val, "disable") == 0) {
		dut->ap_sig_rts = VALUE_DISABLED;
		snprintf(buf, sizeof(buf), "iwconfig %s rts 2347", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv rts 2347 failed");
		}
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Unsupported RTS_FORCE");
	}
}


static enum ap_mode get_mode(const char *str)
{
	if (strcasecmp(str, "11a") == 0)
		return AP_11a;
	else if (strcasecmp(str, "11g") == 0)
		return AP_11g;
	else if (strcasecmp(str, "11b") == 0)
		return AP_11b;
	else if (strcasecmp(str, "11na") == 0)
		return AP_11na;
	else if (strcasecmp(str, "11ng") == 0)
		return AP_11ng;
	else if (strcasecmp(str, "11ac") == 0 || strcasecmp(str, "ac") == 0)
		return AP_11ac;
	else
		return AP_inval;
}


static int run_hostapd_cli(struct sigma_dut *dut, char *buf)
{
	char command[1000];
	const char *bin;
	enum driver_type drv = get_driver_type();
	char *sigma_hapd_file = sigma_hapd_ctrl;

	if (file_exists("hostapd_cli"))
		bin = "./hostapd_cli";
	else if (file_exists("../../hostapd/hostapd_cli"))
		bin = "../../hostapd/hostapd_cli";
	else
		bin = "hostapd_cli";

	if (drv == DRIVER_OPENWRT && sigma_hapd_ctrl == NULL) {
		sigma_hapd_file = "/var/run/hostapd-wifi0";

		if (sigma_radio_ifname[0] &&
		    strcmp(sigma_radio_ifname[0], "wifi1") == 0)
			sigma_hapd_file = "/var/run/hostapd-wifi1";
		else if (sigma_radio_ifname[0] &&
			 strcmp(sigma_radio_ifname[0], "wifi2") == 0)
			sigma_hapd_file = "/var/run/hostapd-wifi2";
	}

	if (sigma_hapd_file)
		snprintf(command, sizeof(command), "%s -p %s %s",
			 bin, sigma_hapd_file, buf);
	else
		snprintf(command, sizeof(command), "%s %s", bin, buf);
	return run_system(dut, command);
}


static int ath_set_lci_config(struct sigma_dut *dut, const char *val,
			      struct sigma_cmd *cmd)
{
	FILE *f;
	int i;

	f = fopen("/tmp/lci_cfg.txt", "w");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open /tmp/lci_cfg.txt");
		return -1;
	}

	for (i = 2; i < cmd->count; i++)
		fprintf(f, "%s = %s \n", cmd->params[i], cmd->values[i]);
	fprintf(f, "\n");
	fclose(f);

	return 0;
}


static int cmd_ap_set_wireless(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	/* const char *ifname = get_param(cmd, "INTERFACE"); */
	const char *val;
	unsigned int wlan_tag = 1;
	char *ifname = get_main_ifname();

	val = get_param(cmd, "WLAN_TAG");
	if (val) {
		wlan_tag = atoi(val);
		if (wlan_tag < 1 || wlan_tag > 3) {
			/*
			 * The only valid WLAN Tags as of now as per the latest
			 * WFA scripts are 1, 2, and 3.
			 */
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid WLAN_TAG");
			return 0;
		}
	}

	val = get_param(cmd, "CountryCode");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_countrycode) - 1)
			return -1;
		snprintf(dut->ap_countrycode, sizeof(dut->ap_countrycode),
			 "%s", val);
	}

	val = get_param(cmd, "regulatory_mode");
	if (val) {
		if (strcasecmp(val, "11d") == 0 || strcasecmp(val, "11h") == 0)
			dut->ap_regulatory_mode = AP_80211D_MODE_ENABLED;
	}

	val = get_param(cmd, "SSID");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_ssid) - 1)
			return -1;

		if (wlan_tag == 1) {
			/*
			 * If tag is not specified, it is deemed to be 1.
			 * Hence tag of 1 is a special case and the values
			 * corresponding to wlan-tag=1 are stored separately
			 * from the values corresponding tags 2 and 3.
			 * This approach minimises the changes to existing code
			 * since most of the sigma_dut code does not deal with
			 * WLAN-TAG CAPI variable.
			 */
			snprintf(dut->ap_ssid,
				 sizeof(dut->ap_ssid), "%s", val);
		} else {
			snprintf(dut->ap_tag_ssid[wlan_tag - 2],
				 sizeof(dut->ap_tag_ssid[wlan_tag - 2]),
				 "%s", val);
		}
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

	/* Overwrite the AP channel with DFS channel if configured */
	val = get_param(cmd, "dfs_chan");
	if (val) {
		dut->ap_channel = atoi(val);
	}

	val = get_param(cmd, "dfs_mode");
	if (val) {
		if (strcasecmp(val, "Enable") == 0)
			dut->ap_dfs_mode = AP_DFS_MODE_ENABLED;
		else if (strcasecmp(val, "Disable") == 0)
			dut->ap_dfs_mode = AP_DFS_MODE_DISABLED;
		else
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Unsupported dfs_mode value: %s", val);
	}

	val = get_param(cmd, "MODE");
	if (val) {
		char *str, *pos;

		str = strdup(val);
		if (str == NULL)
			return -1;
		pos = strchr(str, ';');
		if (pos)
			*pos++ = '\0';

		dut->ap_is_dual = 0;
		dut->ap_mode = get_mode(str);
		if (dut->ap_mode == AP_inval) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported MODE");
			free(str);
			return 0;
		}
		if (dut->ap_mode == AP_11ac)
			dut->ap_chwidth = AP_80;

		if (pos) {
			dut->ap_mode_1 = get_mode(pos);
			if (dut->ap_mode_1 == AP_inval) {
				send_resp(dut, conn, SIGMA_INVALID,
					  "errorCode,Unsupported MODE");
				free(str);
				return 0;
			}
			if (dut->ap_mode_1 == AP_11ac)
				dut->ap_chwidth_1 = AP_80;
			dut->ap_is_dual = 1;
		}

		free(str);
	} else if (dut->ap_mode == AP_inval) {
		if (dut->ap_channel <= 11)
			dut->ap_mode = AP_11ng;
		else if (dut->program == PROGRAM_VHT)
			dut->ap_mode = AP_11ac;
		else
			dut->ap_mode = AP_11na;
	}

	val = get_param(cmd, "WME");
	if (val) {
		if (strcasecmp(val, "on") == 0)
			dut->ap_wme = AP_WME_ON;
		else if (strcasecmp(val, "off") == 0)
			dut->ap_wme = AP_WME_OFF;
		else
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Unsupported WME value: %s", val);
	}

	val = get_param(cmd, "WMMPS");
	if (val) {
		if (strcasecmp(val, "on") == 0)
			dut->ap_wmmps = AP_WMMPS_ON;
		else if (strcasecmp(val, "off") == 0)
			dut->ap_wmmps = AP_WMMPS_OFF;
		else
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Unsupported WMMPS value: %s", val);
	}

	val = get_param(cmd, "RTS");
	if (val)
		dut->ap_rts = atoi(val);

	val = get_param(cmd, "FRGMNT");
	if (val)
		dut->ap_frgmnt = atoi(val);

	/* TODO: PWRSAVE */

	val = get_param(cmd, "BCNINT");
	if (val)
		dut->ap_bcnint = atoi(val);

	val = get_param(cmd, "RADIO");
	if (val) {
		enum driver_type drv = get_driver_type();

		if (strcasecmp(val, "on") == 0) {
			if (drv == DRIVER_ATHEROS)
				ath_ap_start_hostapd(dut);
			else if (cmd_ap_config_commit(dut, conn, cmd) <= 0)
				return 0;
		} else if (strcasecmp(val, "off") == 0) {
			if (drv == DRIVER_OPENWRT) {
				run_system(dut, "wifi down");
				sigma_dut_print(dut, DUT_MSG_INFO,
						"wifi down on radio,off");
			} else if (dut->use_hostapd_pid_file) {
				kill_hostapd_process_pid(dut);
			} else if (kill_process(dut, "(hostapd)", 1,
						SIGTERM) == 0 ||
				   system("killall hostapd") == 0) {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"Killed hostapd on radio,off");
			}
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported RADIO value");
			return 0;
		}
	}

	val = get_param(cmd, "P2PMgmtBit");
	if (val)
		dut->ap_p2p_mgmt = atoi(val);

	/* TODO: ChannelUsage */

	/* TODO: 40_INTOLERANT */

	val = get_param(cmd, "ADDBA_REJECT");
	if (val) {
		if (strcasecmp(val, "Enable") == 0)
			dut->ap_addba_reject = VALUE_ENABLED;
		else if (strcasecmp(val, "Disable") == 0)
			dut->ap_addba_reject = VALUE_DISABLED;
	}

	val = get_param(cmd, "AMPDU");
	if (val) {
		if (strcasecmp(val, "Enable") == 0)
			dut->ap_ampdu = VALUE_ENABLED;
		else if (strcasecmp(val, "Disable") == 0)
			dut->ap_ampdu = VALUE_DISABLED;
	}

	val = get_param(cmd, "AMPDU_EXP");
	if (val)
		dut->ap_ampdu_exp = atoi(val);

	val = get_param(cmd, "AMSDU");
	if (val) {
		if (strcasecmp(val, "Enable") == 0)
			dut->ap_amsdu = VALUE_ENABLED;
		else if (strcasecmp(val, "Disable") == 0)
			dut->ap_amsdu = VALUE_DISABLED;
	}

	val = get_param(cmd, "NoAck");
	if (val) {
		if (strcasecmp(val, "on") == 0)
			dut->ap_noack = VALUE_ENABLED;
		else if (strcasecmp(val, "off") == 0)
			dut->ap_noack = VALUE_DISABLED;
	}

	/* TODO: GREENFIELD */
	/* TODO: MCS_32 */

	val = get_param(cmd, "OFFSET");
	if (val) {
		if (strcasecmp(val, "Above") == 0)
			dut->ap_chwidth_offset = SEC_CH_40ABOVE;
		else if (strcasecmp(val, "Below") == 0)
			dut->ap_chwidth_offset = SEC_CH_40BELOW;
	}

	val = get_param(cmd, "MCS_FIXEDRATE");
	if (val) {
		dut->ap_fixed_rate = 1;
		dut->ap_mcs = atoi(val);
	}

	val = get_param(cmd, "SPATIAL_RX_STREAM");
	if (val) {
		if (strcasecmp(val, "1SS") == 0 || strcasecmp(val, "1") == 0) {
			dut->ap_rx_streams = 1;
			if (dut->device_type == AP_testbed)
				dut->ap_vhtmcs_map = 0xfffc;
		} else if (strcasecmp(val, "2SS") == 0 ||
			   strcasecmp(val, "2") == 0) {
			dut->ap_rx_streams = 2;
			if (dut->device_type == AP_testbed)
				dut->ap_vhtmcs_map = 0xfff0;
		} else if (strcasecmp(val, "3SS") == 0 ||
			   strcasecmp(val, "3") == 0) {
			dut->ap_rx_streams = 3;
			if (dut->device_type == AP_testbed)
				dut->ap_vhtmcs_map = 0xffc0;
		} else if (strcasecmp(val, "4SS") == 0 ||
			   strcasecmp(val, "4") == 0) {
			dut->ap_rx_streams = 4;
		}
	}

	val = get_param(cmd, "SPATIAL_TX_STREAM");
	if (val) {
		if (strcasecmp(val, "1SS") == 0 ||
		    strcasecmp(val, "1") == 0) {
			dut->ap_tx_streams = 1;
			if (dut->device_type == AP_testbed)
				dut->ap_vhtmcs_map =  0xfffc;
		} else if (strcasecmp(val, "2SS") == 0 ||
			   strcasecmp(val, "2") == 0) {
			dut->ap_tx_streams = 2;
			if (dut->device_type == AP_testbed)
				dut->ap_vhtmcs_map = 0xfff0;
		} else if (strcasecmp(val, "3SS") == 0 ||
			   strcasecmp(val, "3") == 0) {
			dut->ap_tx_streams = 3;
			if (dut->device_type == AP_testbed)
				dut->ap_vhtmcs_map = 0xffc0;
		} else if (strcasecmp(val, "4SS") == 0 ||
			   strcasecmp(val, "4") == 0) {
			dut->ap_tx_streams = 4;
		}
	}

	val = get_param(cmd, "BSS_max_idle");
	if (val) {
		if (strncasecmp(val, "Enable", 7) == 0) {
			dut->wnm_bss_max_feature = VALUE_ENABLED;
		} else if (strncasecmp(val, "Disable", 8) == 0) {
			dut->wnm_bss_max_feature = VALUE_DISABLED;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid value for BSS_max_Feature");
			return 0;
		}
	}

	val = get_param(cmd, "BSS_Idle_Protection_options");
	if (val) {
		int protection = (int) strtol(val, (char **) NULL, 10);

		if (protection != 1 && protection != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid value for BSS_Idle_Protection_options");
			return 0;
		}
		dut->wnm_bss_max_protection = protection ?
			VALUE_ENABLED : VALUE_DISABLED;
	}

	val = get_param(cmd, "BSS_max_Idle_period");
	if (val) {
		int idle_time = (int) strtol(val, (char **) NULL, 10);

		if (idle_time == LONG_MIN || idle_time == LONG_MAX) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid value for BSS_max_Idle_period");
			return 0;
		}
		dut->wnm_bss_max_idle_time = idle_time;
	}

	val = get_param(cmd, "PROXY_ARP");
	if (val)
		dut->ap_proxy_arp = (int) strtol(val, (char **) NULL, 10);

	val = get_param(cmd, "nss_mcs_cap");
	if (val) {
		int nss, mcs;
		char token[20];
		char *result = NULL;
		char *saveptr;

		if (strlen(val) >= sizeof(token))
			return -1;
		strlcpy(token, val, sizeof(token));
		result = strtok_r(token, ";", &saveptr);
		if (!result) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"VHT NSS not specified");
			return 0;
		}
		nss = atoi(result);
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
		switch (nss) {
		case 1:
			switch (mcs) {
			case 7:
				dut->ap_vhtmcs_map = 0xfffc;
				break;
			case 8:
				dut->ap_vhtmcs_map = 0xfffd;
				break;
			case 9:
				dut->ap_vhtmcs_map = 0xfffe;
				break;
			default:
				dut->ap_vhtmcs_map = 0xfffe;
				break;
			}
			break;
		case 2:
			switch (mcs) {
			case 7:
				dut->ap_vhtmcs_map = 0xfff0;
				break;
			case 8:
				dut->ap_vhtmcs_map = 0xfff5;
				break;
			case 9:
				dut->ap_vhtmcs_map = 0xfffa;
				break;
			default:
				dut->ap_vhtmcs_map = 0xfffa;
				break;
			}
			break;
		case 3:
			switch (mcs) {
			case 7:
				dut->ap_vhtmcs_map = 0xffc0;
				break;
			case 8:
				dut->ap_vhtmcs_map = 0xffd5;
				break;
			case 9:
				dut->ap_vhtmcs_map = 0xffea;
				break;
			default:
				dut->ap_vhtmcs_map = 0xffea;
				break;
			}
			break;
		default:
			dut->ap_vhtmcs_map = 0xffea;
			break;
		}
	}

	/* TODO: MPDU_MIN_START_SPACING */
	/* TODO: RIFS_TEST */
	/* TODO: SGI20 */

	val = get_param(cmd, "STBC_TX");
	if (val)
		dut->ap_tx_stbc = atoi(val);

	val = get_param(cmd, "WIDTH");
	if (val) {
		if (strcasecmp(val, "20") == 0)
			dut->ap_chwidth = AP_20;
		else if (strcasecmp(val, "40") == 0)
			dut->ap_chwidth = AP_40;
		else if (strcasecmp(val, "80") == 0)
			dut->ap_chwidth = AP_80;
		else if (strcasecmp(val, "160") == 0)
			dut->ap_chwidth = AP_160;
		else if (strcasecmp(val, "Auto") == 0)
			dut->ap_chwidth = AP_AUTO;
		else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported WIDTH");
			return 0;
		}
	}

	/* TODO: WIDTH_SCAN */

	val = get_param(cmd, "TDLSProhibit");
	dut->ap_tdls_prohibit = val && strcasecmp(val, "Enabled") == 0;
	val = get_param(cmd, "TDLSChswitchProhibit");
	dut->ap_tdls_prohibit_chswitch =
		val && strcasecmp(val, "Enabled") == 0;
	val = get_param(cmd, "HS2");
	if (val && wlan_tag == 1)
		dut->ap_hs2 = atoi(val);
	val = get_param(cmd, "P2P_CROSS_CONNECT");
	if (val)
		dut->ap_p2p_cross_connect = strcasecmp(val, "Enabled") == 0;

	val = get_param(cmd, "FakePubKey");
	dut->ap_fake_pkhash = val && atoi(val);

	val = get_param(cmd, "vht_tkip");
	dut->ap_allow_vht_tkip = val && strcasecmp(val, "Enable") == 0;
	val = get_param(cmd, "vht_wep");
	dut->ap_allow_vht_wep = val && strcasecmp(val, "Enable") == 0;

	val = get_param(cmd, "Protect_mode");
	dut->ap_disable_protection = val && strcasecmp(val, "Disable") == 0;

	val = get_param(cmd, "DYN_BW_SGNL");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_OPENWRT:
			switch (get_openwrt_driver_type()) {
			case OPENWRT_DRIVER_ATHEROS:
				ath_config_dyn_bw_sig(dut, ifname, val);
				break;
			default:
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Unsupported DYN_BW_SGNL with OpenWrt driver");
				return 0;
			}
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Unsupported DYN_BW_SGL with the current driver");
			break;
		}
	}

	val = get_param(cmd, "SGI80");
	if (val) {
		if (strcasecmp(val, "enable") == 0)
			dut->ap_sgi80 = 1;
		else if (strcasecmp(val, "disable") == 0)
			dut->ap_sgi80 = 0;
		else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported SGI80");
			return 0;
		}
	}

	val = get_param(cmd, "LDPC");
	if (val) {
		if (strcasecmp(val, "enable") == 0)
			dut->ap_ldpc = VALUE_ENABLED;
		else if (strcasecmp(val, "disable") == 0)
			dut->ap_ldpc = VALUE_DISABLED;
		else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported LDPC");
			return 0;
		}
	}

	val = get_param(cmd, "BW_SGNL");
	if (val) {
		/*
		 * With dynamic bandwidth signaling enabled we should see
		 * RTS if the threshold is met.
		 */
		if (strcasecmp(val, "enable") == 0) {
			dut->ap_sig_rts = VALUE_ENABLED;
		} else if (strcasecmp(val, "disable") == 0) {
			dut->ap_sig_rts = VALUE_DISABLED;
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported BW_SGNL");
			return 0;
		}
	}

	val = get_param(cmd, "RTS_FORCE");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_OPENWRT:
			switch (get_openwrt_driver_type()) {
			case OPENWRT_DRIVER_ATHEROS:
				ath_config_rts_force(dut, ifname, val);
				break;
			default:
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Unsupported RTS_FORCE with OpenWrt driver");
				return 0;
			}
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Unsupported RTS_FORCE with the current driver");
			break;
		}
	}

	val = get_param(cmd, "Zero_crc");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_ATHEROS:
			ath_set_zero_crc(dut, val);
			break;
		case DRIVER_OPENWRT:
			switch (get_openwrt_driver_type()) {
			case OPENWRT_DRIVER_ATHEROS:
				ath_set_zero_crc(dut, val);
				break;
			default:
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Unsupported zero_crc with the current driver");
				return 0;
			}
			break;
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported zero_crc with the current driver");
			return 0;
		}
	}

	val = get_param(cmd, "TxBF");
	if (val)
		dut->ap_txBF = strcasecmp(val, "enable") == 0;

	val = get_param(cmd, "MU_TxBF");
	if (val) {
		if (strcasecmp(val, "enable") == 0) {
			dut->ap_txBF = 1;
			dut->ap_mu_txBF = 1;
		} else if (strcasecmp(val, "disable") == 0) {
			dut->ap_txBF = 0;
			dut->ap_mu_txBF = 0;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Unsupported MU_TxBF");
		}
	}

	/* UNSUPPORTED: tx_lgi_rate */

	val = get_param(cmd, "wpsnfc");
	if (val)
		dut->ap_wpsnfc = atoi(val);

	val = get_param(cmd, "GROUP_ID");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_OPENWRT:
			switch (get_openwrt_driver_type()) {
			case OPENWRT_DRIVER_ATHEROS:
				ath_ap_set_group_id(dut, ifname, val);
				break;
			default:
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Unsupported group_id with the current driver");
				return 0;
			}
			break;
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported group_id with the current driver");
			return 0;
		}
	}

	val = get_param(cmd, "CTS_WIDTH");
	if (val) {
		switch (get_driver_type()) {
		case DRIVER_OPENWRT:
			switch (get_openwrt_driver_type()) {
			case OPENWRT_DRIVER_ATHEROS:
				ath_set_cts_width(dut, ifname, val);
				break;
			default:
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Unsupported cts_width with the current driver");
				return 0;
			}
			break;
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported cts_width with the current driver");
			return 0;
		}
	}

	val = get_param(cmd, "MU_NDPA_FrameFormat");
	if (val)
		dut->ap_ndpa_frame = atoi(val);

	val = get_param(cmd, "interworking");
	if (val && strcmp(val, "1") == 0)
		dut->ap_interworking = 1;

	val = get_param(cmd, "GAS_CB_DELAY");
	if (val)
		dut->ap_gas_cb_delay = atoi(val);

	val = get_param(cmd, "LCI");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_val_lci) - 1)
			return -1;
		dut->ap_lci = 1;
		snprintf(dut->ap_val_lci, sizeof(dut->ap_val_lci), "%s", val);
		ath_set_lci_config(dut, val, cmd);
	}

	val = get_param(cmd, "InfoZ");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_infoz) - 1)
			return -1;
		snprintf(dut->ap_infoz, sizeof(dut->ap_infoz), "%s", val);
	}

	val = get_param(cmd, "LocCivicAddr");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_val_lcr) - 1)
			return -1;
		dut->ap_lcr = 1;
		snprintf(dut->ap_val_lcr, sizeof(dut->ap_val_lcr), "%s", val);
		if (dut->ap_lci == 0)
			ath_set_lci_config(dut, val, cmd);
	}

	val = get_param(cmd, "NeighAPBSSID");
	if (val) {
		if (dut->ap_neighap < 3) {
			if (parse_mac_address(
				    dut, val,
				    dut->ap_val_neighap[dut->ap_neighap]) < 0) {
				send_resp(dut, conn, SIGMA_INVALID,
					  "Failed to parse MAC address");
				return 0;
			}
			dut->ap_neighap++;
			if (dut->ap_lci == 1)
				dut->ap_scan = 1;
		}
	}

	val = get_param(cmd, "OpChannel");
	if (val) {
		if (dut->ap_opchannel < 3) {
			dut->ap_val_opchannel[dut->ap_opchannel] = atoi(val);
			dut->ap_opchannel++;
		}
	}

	val = get_param(cmd, "URI-FQDNdescriptor");
	if (val) {
		if (strcasecmp(val, "HELD") == 0) {
			dut->ap_fqdn_held = 1;
		} else if (strcasecmp(val, "SUPL") == 0) {
			dut->ap_fqdn_supl = 1;
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported URI-FQDNdescriptor");
			return 0;
		}
	}

	val = get_param(cmd, "Reg_Domain");
	if (val) {
		if (strcasecmp(val, "Local") == 0) {
			dut->ap_reg_domain = REG_DOMAIN_LOCAL;
		} else if (strcasecmp(val, "Global") == 0) {
			dut->ap_reg_domain = REG_DOMAIN_GLOBAL;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Wrong value for Reg_Domain");
			return 0;
		}
	}

	val = get_param(cmd, "NAME");
	if (val) {
		if (strcasecmp(val, "ap1mbo") == 0)
			dut->ap_name = 1;
		else if (strcasecmp(val, "ap2mbo") == 0)
			dut->ap_name = 2;
		else
			dut->ap_name = 0;
	}

	val = get_param(cmd, "FT_OA");
	if (val) {
		if (strcasecmp(val, "Enable") == 0) {
			dut->ap_ft_oa = 1;
		} else if (strcasecmp(val, "Disable") == 0) {
			dut->ap_ft_oa = 0;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Wrong value for FT_OA");
			return 0;
		}
	}

	val = get_param(cmd, "Cellular_Cap_Pref");
	if (val)
		dut->ap_cell_cap_pref = atoi(val);

	val = get_param(cmd, "DOMAIN");
	if (val) {
		if (strlen(val) >= sizeof(dut->ap_mobility_domain)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Too long DOMAIN");
			return 0;
		}
		snprintf(dut->ap_mobility_domain,
			 sizeof(dut->ap_mobility_domain), "%s", val);
	}

	val = get_param(cmd, "ft_bss_list");
	if (val) {
		char *mac_str;
		int i;
		char *saveptr;
		char *mac_list_str;

		mac_list_str = strdup(val);
		if (!mac_list_str)
			return -1;
		mac_str = strtok_r(mac_list_str, " ", &saveptr);
		for (i = 0; mac_str && i < MAX_FT_BSS_LIST; i++) {
			if (parse_mac_address(dut, mac_str,
					      dut->ft_bss_mac_list[i]) < 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"MAC Address not in proper format");
				break;
			}
			dut->ft_bss_mac_cnt++;
			mac_str = strtok_r(NULL, " ", &saveptr);
		}
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Storing the following FT BSS MAC List");
		for (i = 0; i < dut->ft_bss_mac_cnt; i++) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"MAC[%d] %02x:%02x:%02x:%02x:%02x:%02x",
					i,
					dut->ft_bss_mac_list[i][0],
					dut->ft_bss_mac_list[i][1],
					dut->ft_bss_mac_list[i][2],
					dut->ft_bss_mac_list[i][3],
					dut->ft_bss_mac_list[i][4],
					dut->ft_bss_mac_list[i][5]);
		}
		free(mac_list_str);
	}

	return 1;
}


static void ath_inject_frame(struct sigma_dut *dut, const char *ifname, int tid)
{
	char buf[256];
	int tid_to_dscp[] = { 0x00, 0x20, 0x40, 0x60, 0x80, 0xa0, 0xc0, 0xe0 };

	if (tid < 0 ||
	    tid >= (int) (sizeof(tid_to_dscp) / sizeof(tid_to_dscp[0]))) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Unsupported TID: %d", tid);
		return;
	}

	snprintf(buf, sizeof(buf),
		 "wlanconfig %s list sta | grep : | cut -b 1-17 > %s",
		 ifname, VI_QOS_TMP_FILE);
	if (system(buf) != 0)
		return;

	snprintf(buf, sizeof(buf),
		 "ifconfig %s | grep HWaddr | cut -b 39-56 >> %s",
		 ifname, VI_QOS_TMP_FILE);
	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "Retrieve HWaddr failed");

	snprintf(buf, sizeof(buf), "sed -n '3,$p' %s >> %s",
		 VI_QOS_REFFILE, VI_QOS_TMP_FILE);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Output redirection to VI_QOS_TMP_FILE failed");
	}

	snprintf(buf, sizeof(buf), "sed '5 c %x' %s > %s",
		 tid_to_dscp[tid], VI_QOS_TMP_FILE, VI_QOS_FILE);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Append TID to VI_QOS_FILE failed ");
	}

	snprintf(buf, sizeof(buf), "ethinject %s %s", ifname, VI_QOS_FILE);
	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "Ethinject frame failed");
}


static int ath_ap_send_addba_req(struct sigma_dut *dut, struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *val;
	char *ifname;
	char buf[256];
	int tid = 0;

	ifname = get_main_ifname();
	val = get_param(cmd, "TID");
	if (val) {
		tid = atoi(val);
		if (tid)
			ath_inject_frame(dut, ifname, tid);
	}

	/* NOTE: This is the command sequence on Peregrine for ADDBA */
	snprintf(buf, sizeof(buf), "iwpriv %s setaddbaoper 1", ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv setaddbaoper failed");
	}

	snprintf(buf, sizeof(buf), "wifitool %s senddelba 1 %d 1 4",
		 ifname, tid);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"wifitool senddelba failed");
	}

	snprintf(buf, sizeof(buf), "wifitool %s sendaddba 1 %d 64",
		 ifname, tid);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"wifitool sendaddba failed");
	}

	return 1;
}


static int ath10k_debug_enable_addba_req(struct sigma_dut *dut, int tid,
					 const char *sta_mac,
					 const char *dir_path)
{
	DIR *dir;
	struct dirent *entry;
	char buf[128], path[128];
	int ret = 0;

	dir = opendir(dir_path);
	if (!dir)
		return 0;

	while ((entry = readdir(dir))) {
		ret = 1;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path) - 1, "%s/%s",
			 dir_path, entry->d_name);
		path[sizeof(path) - 1] = 0;

		if (strcmp(entry->d_name, sta_mac) == 0) {
			snprintf(buf, sizeof(buf), "echo 1 > %s/aggr_mode",
				 path);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to set aggr mode for ath10k");
			}

			snprintf(buf, sizeof(buf), "echo %d 32 > %s/addba",
				 tid, path);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to set addbareq for ath10k");
			}

			break;
		}

		/* Recursively search subdirectories */
		ath10k_debug_enable_addba_req(dut, tid, sta_mac, path);
	}

	closedir(dir);

	return ret;
}


static int ath10k_ap_send_addba_req(struct sigma_dut *dut,
				    struct sigma_cmd *cmd)
{
	const char *val;
	int tid = 0;

	val = get_param(cmd, "TID");
	if (val)
		tid = atoi(val);

	val = get_param(cmd, "sta_mac_address");
	if (!val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to parse station MAC address");
		return 0;
	}

	return ath10k_debug_enable_addba_req(dut, tid, val,
					     "/sys/kernel/debug/ieee80211");
}


static int cmd_ap_send_addba_req(struct sigma_dut *dut, struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	/* const char *ifname = get_param(cmd, "INTERFACE"); */
	struct stat s;

	switch (get_driver_type()) {
	case DRIVER_ATHEROS:
		return ath_ap_send_addba_req(dut, conn, cmd);
	case DRIVER_OPENWRT:
		switch (get_openwrt_driver_type()) {
		case OPENWRT_DRIVER_ATHEROS:
			return ath_ap_send_addba_req(dut, conn, cmd);
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,ap_send_addba_req not supported with this driver");
			return 0;
		}
	case DRIVER_WCN:
	case DRIVER_LINUX_WCN:
		/* AP automatically sends ADDBA request after association. */
		sigma_dut_print(dut, DUT_MSG_INFO,
				"ap_send_addba_req command ignored");
		return 1;
	case DRIVER_MAC80211:
		if (stat("/sys/module/ath10k_core", &s) == 0)
			return ath10k_ap_send_addba_req(dut, cmd);
		/* fall through */
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,ap_send_addba_req not supported with this driver");
		return 0;
	}
}


static int cmd_ap_set_security(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	const char *val;
	unsigned int wlan_tag = 1;

	val = get_param(cmd, "WLAN_TAG");
	if (val)
		wlan_tag = atoi(val);

	if (wlan_tag > 1) {
		val = get_param(cmd, "KEYMGNT");
		if (val) {
			if (strcasecmp(val, "NONE") == 0) {
				dut->ap_tag_key_mgmt[wlan_tag - 2] = AP2_OPEN;
			} else if (strcasecmp(val, "OSEN") == 0 &&
				   wlan_tag == 2) {
				/*
				 * OSEN only supported on WLAN_TAG = 2 for now
				 */
				dut->ap_tag_key_mgmt[wlan_tag - 2] = AP2_OSEN;
			} else if (strcasecmp(val, "WPA2-PSK") == 0) {
				dut->ap_tag_key_mgmt[wlan_tag - 2] =
					AP2_WPA2_PSK;
			} else {
				send_resp(dut, conn, SIGMA_INVALID,
					  "errorCode,Unsupported KEYMGNT");
				return 0;
			}
			return 1;
		}
	}

	val = get_param(cmd, "KEYMGNT");
	if (val) {
		if (strcasecmp(val, "WPA2-PSK") == 0) {
			dut->ap_key_mgmt = AP_WPA2_PSK;
			dut->ap_cipher = AP_CCMP;
		} else if (strcasecmp(val, "WPA2-EAP") == 0 ||
			   strcasecmp(val, "WPA2-Ent") == 0) {
			dut->ap_key_mgmt = AP_WPA2_EAP;
			dut->ap_cipher = AP_CCMP;
		} else if (strcasecmp(val, "WPA-PSK") == 0) {
			dut->ap_key_mgmt = AP_WPA_PSK;
			dut->ap_cipher = AP_TKIP;
		} else if (strcasecmp(val, "WPA-EAP") == 0 ||
			   strcasecmp(val, "WPA-Ent") == 0) {
			dut->ap_key_mgmt = AP_WPA_EAP;
			dut->ap_cipher = AP_TKIP;
		} else if (strcasecmp(val, "WPA2-Mixed") == 0) {
			dut->ap_key_mgmt = AP_WPA2_EAP_MIXED;
			dut->ap_cipher = AP_CCMP_TKIP;
		} else if (strcasecmp(val, "WPA2-PSK-Mixed") == 0) {
			dut->ap_key_mgmt = AP_WPA2_PSK_MIXED;
			dut->ap_cipher = AP_CCMP_TKIP;
		} else if (strcasecmp(val, "WPA2-SAE") == 0) {
			dut->ap_key_mgmt = AP_WPA2_SAE;
			dut->ap_cipher = AP_CCMP;
		} else if (strcasecmp(val, "WPA2-PSK-SAE") == 0) {
			dut->ap_key_mgmt = AP_WPA2_PSK_SAE;
			dut->ap_cipher = AP_CCMP;
		} else if (strcasecmp(val, "NONE") == 0) {
			dut->ap_key_mgmt = AP_OPEN;
			dut->ap_cipher = AP_PLAIN;
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported KEYMGNT");
			return 0;
		}
	}

	val = get_param(cmd, "ECGroupID");
	if (val) {
		dut->ap_sae_group = atoi(val);
		if (dut->ap_sae_group == 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid ECGroupID");
			return 0;
		}
	}

	val = get_param(cmd, "ENCRYPT");
	if (val) {
		if (strcasecmp(val, "WEP") == 0) {
			dut->ap_cipher = AP_WEP;
		} else if (strcasecmp(val, "TKIP") == 0) {
			dut->ap_cipher = AP_TKIP;
		} else if (strcasecmp(val, "AES") == 0 ||
			   strcasecmp(val, "AES-CCMP") == 0) {
			dut->ap_cipher = AP_CCMP;
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported ENCRYPT");
			return 0;
		}
	}

	val = get_param(cmd, "WEPKEY");
	if (val) {
		size_t len;
		if (dut->ap_cipher != AP_WEP) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unexpected WEPKEY without WEP "
				  "configuration");
			return 0;
		}
		len = strlen(val);
		if (len != 10 && len != 26) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unexpected WEPKEY length");
			return 0;
		}
		snprintf(dut->ap_wepkey, sizeof(dut->ap_wepkey), "%s", val);
	}

	val = get_param(cmd, "PSK");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_passphrase) - 1)
			return -1;
		snprintf(dut->ap_passphrase, sizeof(dut->ap_passphrase),
			 "%s", val);
	}

	val = get_param(cmd, "PMF");
	if (val) {
		if (strcasecmp(val, "Disabled") == 0) {
			dut->ap_pmf = AP_PMF_DISABLED;
		} else if (strcasecmp(val, "Optional") == 0) {
			dut->ap_pmf = AP_PMF_OPTIONAL;
		} else if (strcasecmp(val, "Required") == 0) {
			dut->ap_pmf = AP_PMF_REQUIRED;
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported PMF");
			return 0;
		}
	}

	if (dut->ap_key_mgmt == AP_OPEN) {
		dut->ap_hs2 = 0;
		dut->ap_pmf = AP_PMF_DISABLED;
	}

	dut->ap_add_sha256 = 0;
	val = get_param(cmd, "SHA256AD");
	if (val == NULL)
		val = get_param(cmd, "SHA256");
	if (val) {
		if (strcasecmp(val, "Disabled") == 0) {
		} else if (strcasecmp(val, "Enabled") == 0) {
			dut->ap_add_sha256 = 1;
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported PMF");
			return 0;
		}
	}

	val = get_param(cmd, "PreAuthentication");
	if (val) {
		if (strcasecmp(val, "disabled") == 0) {
			dut->ap_rsn_preauth = 0;
		} else if (strcasecmp(val, "enabled") == 0) {
			dut->ap_rsn_preauth = 1;
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported PreAuthentication value");
			return 0;
		}
	}

	return 1;
}


static int cmd_ap_set_radius(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	const char *val;
	unsigned int wlan_tag = 1, radius_port = 0;
	char *radius_ipaddr = NULL, *radius_password = NULL;

	val = get_param(cmd, "WLAN_TAG");
	if (val) {
		wlan_tag = atoi(val);
		if (wlan_tag != 1 && wlan_tag != 2) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid WLAN_TAG");
			return 0;
		}
	}

	val = get_param(cmd, "PORT");
	if (val)
		radius_port = atoi(val);

	if (wlan_tag == 1) {
		if (radius_port)
			dut->ap_radius_port = radius_port;
		radius_ipaddr = dut->ap_radius_ipaddr;
		radius_password = dut->ap_radius_password;
	} else if (wlan_tag == 2) {
		if (radius_port)
			dut->ap2_radius_port = radius_port;
		radius_ipaddr = dut->ap2_radius_ipaddr;
		radius_password = dut->ap2_radius_password;
	}

	val = get_param(cmd, "IPADDR");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_radius_ipaddr) - 1)
			return -1;
		snprintf(radius_ipaddr, sizeof(dut->ap_radius_ipaddr),
			 "%s", val);
	}

	val = get_param(cmd, "PASSWORD");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_radius_password) - 1)
			return -1;
		snprintf(radius_password,
			 sizeof(dut->ap_radius_password), "%s", val);
	}

	return 1;
}


static void owrt_ap_set_radio(struct sigma_dut *dut, int id,
			      const char *key, const char *val)
{
	char buf[100];

	if (val == NULL) {
		snprintf(buf, sizeof(buf),
			 "uci delete wireless.wifi%d.%s", id, key);
		run_system(dut, buf);
		return;
	}

	snprintf(buf, sizeof(buf), "uci set wireless.wifi%d.%s=%s",
		 id, key, val);
	run_system(dut, buf);
}


static void owrt_ap_set_list_radio(struct sigma_dut *dut, int id,
				   const char *key, const char *val)
{
	char buf[256];

	if (val == NULL) {
		snprintf(buf, sizeof(buf),
			 "uci del_list wireless.wifi%d.%s", id, key);
		run_system(dut, buf);
		return;
	}

	snprintf(buf, sizeof(buf), "uci add_list wireless.wifi%d.%s=%s",
		 id, key, val);
	run_system(dut, buf);
}


static void owrt_ap_set_vap(struct sigma_dut *dut, int id, const char *key,
			    const char *val)
{
	char buf[256];

	if (val == NULL) {
		snprintf(buf, sizeof(buf),
			 "uci delete wireless.@wifi-iface[%d].%s", id, key);
		run_system(dut, buf);
		return;
	}

	snprintf(buf, sizeof(buf), "uci set wireless.@wifi-iface[%d].%s=%s",
		 id, key, val);
	run_system(dut, buf);
}


static void owrt_ap_set_list_vap(struct sigma_dut *dut, int id,
				 const char *key, const char *val)
{
	char buf[1024];

	if (val == NULL) {
		snprintf(buf, sizeof(buf),
			 "uci del_list wireless.@wifi-iface[%d].%s", id, key);
		run_system(dut, buf);
		return;
	}

	snprintf(buf, sizeof(buf),
		 "uci add_list wireless.@wifi-iface[%d].%s=%s",
		 id, key, val);
	run_system(dut, buf);
}


static void owrt_ap_add_vap(struct sigma_dut *dut, int id, const char *key,
			    const char *val)
{
	char buf[256];

	if (val == NULL) {
		snprintf(buf, sizeof(buf),
			 "uci delete wireless.@wifi-iface[%d].%s", id, key);
		run_system(dut, buf);
		return;
	}

	snprintf(buf, sizeof(buf), "uci add wireless wifi-iface");
	run_system(dut, buf);
	snprintf(buf, sizeof(buf), "uci set wireless.@wifi-iface[%d].%s=%s",
		 id, key, val);
	run_system(dut, buf);
	snprintf(buf, sizeof(buf), "uci set wireless.@wifi-iface[%d].%s=%s",
		 id, "network", "lan");
	run_system(dut, buf);
	snprintf(buf, sizeof(buf), "uci set wireless.@wifi-iface[%d].%s=%s",
		 id, "mode", "ap");
	run_system(dut, buf);
	snprintf(buf, sizeof(buf), "uci set wireless.@wifi-iface[%d].%s=%s",
		 id, "encryption", "none");
	run_system(dut, buf);
}


#define OPENWRT_MAX_NUM_RADIOS 3
static void owrt_ap_config_radio(struct sigma_dut *dut)
{
	int radio_id[MAX_RADIO] = { 0, 1 };
	int radio_count, radio_no;
	char buf[64];

	for (radio_count = 0; radio_count < OPENWRT_MAX_NUM_RADIOS;
	     radio_count++) {
		snprintf(buf, sizeof(buf), "%s%d", "wifi", radio_count);
		for (radio_no = 0; radio_no < MAX_RADIO; radio_no++) {
			if (!sigma_radio_ifname[radio_no] ||
			    strcmp(sigma_radio_ifname[radio_no], buf) != 0)
				continue;
			owrt_ap_set_radio(dut, radio_count, "disabled", "0");
			owrt_ap_set_vap(dut, radio_count, "device", buf);
			radio_id[radio_no] = radio_count;
		}
	}

	/* Hardware mode (11a/b/g/n/ac) & HT mode selection */
	switch (dut->ap_mode) {
	case AP_11g:
		owrt_ap_set_radio(dut, radio_id[0], "hwmode", "11g");
		break;
	case AP_11b:
		owrt_ap_set_radio(dut, radio_id[0], "hwmode", "11b");
		break;
	case AP_11ng:
		owrt_ap_set_radio(dut, radio_id[0], "hwmode", "11ng");
		owrt_ap_set_radio(dut, radio_id[0], "htmode", "HT20");
		break;
	case AP_11a:
		owrt_ap_set_radio(dut, radio_id[0], "hwmode", "11a");
		break;
	case AP_11na:
		owrt_ap_set_radio(dut, radio_id[0], "hwmode", "11na");
		owrt_ap_set_radio(dut, radio_id[0], "htmode", "HT20");
		break;
	case AP_11ac:
		owrt_ap_set_radio(dut, radio_id[0], "hwmode", "11ac");
		owrt_ap_set_radio(dut, radio_id[0], "htmode", "HT80");
		break;
	case AP_inval:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"MODE NOT SPECIFIED!");
		return;
	default:
		owrt_ap_set_radio(dut, radio_id[0], "hwmode", "11ng");
		owrt_ap_set_radio(dut, radio_id[0], "htmode", "HT20");
		break;
	}

	if (dut->ap_is_dual) {
		/* Hardware mode (11a/b/g/n/ac) & HT mode selection */
		switch (dut->ap_mode_1) {
		case AP_11g:
			owrt_ap_set_radio(dut, radio_id[1], "hwmode", "11g");
			break;
		case AP_11b:
			owrt_ap_set_radio(dut, radio_id[1], "hwmode", "11b");
			break;
		case AP_11ng:
			owrt_ap_set_radio(dut, radio_id[1], "hwmode", "11ng");
			owrt_ap_set_radio(dut, radio_id[1], "htmode", "HT20");
			break;
		case AP_11a:
			owrt_ap_set_radio(dut, radio_id[1], "hwmode", "11a");
			break;
		case AP_11na:
			owrt_ap_set_radio(dut, radio_id[1], "hwmode", "11na");
			owrt_ap_set_radio(dut, radio_id[1], "htmode", "HT20");
			break;
		case AP_11ac:
			owrt_ap_set_radio(dut, radio_id[1], "hwmode", "11ac");
			owrt_ap_set_radio(dut, radio_id[1], "htmode", "HT80");
			break;
		case AP_inval:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"MODE NOT SPECIFIED!");
			return;
		default:
			owrt_ap_set_radio(dut, radio_id[1], "hwmode", "11ng");
			owrt_ap_set_radio(dut, radio_id[1], "htmode", "HT20");
			break;
		}

	}

	/* Channel */
	snprintf(buf, sizeof(buf), "%d", dut->ap_channel);
	owrt_ap_set_radio(dut, radio_id[0], "channel", buf);

	switch (dut->ap_chwidth) {
		case AP_20:
			owrt_ap_set_radio(dut, radio_id[0], "htmode", "HT20");
			break;
		case AP_40:
			owrt_ap_set_radio(dut, radio_id[0], "htmode", "HT40");
			break;
		case AP_80:
			owrt_ap_set_radio(dut, radio_id[0], "htmode", "HT80");
			break;
		case AP_160:
			owrt_ap_set_radio(dut, radio_id[0], "htmode", "HT160");
			break;
		case AP_AUTO:
		default:
			break;
	}

	if (dut->ap_channel == 140 || dut->ap_channel == 144) {
		if (get_openwrt_driver_type() == OPENWRT_DRIVER_ATHEROS)
			owrt_ap_set_radio(dut, radio_id[0], "set_ch_144", "3");
	}

	if (dut->ap_is_dual) {
		snprintf(buf, sizeof(buf), "%d", dut->ap_channel_1);
		owrt_ap_set_radio(dut, radio_id[1], "channel", buf);
	}

	/* Country Code */
	if (dut->ap_reg_domain == REG_DOMAIN_GLOBAL) {
		const char *country;

		country = dut->ap_countrycode[0] ? dut->ap_countrycode : "US";
		snprintf(buf, sizeof(buf), "%s4", country);
		owrt_ap_set_radio(dut, radio_id[0], "country", buf);
	} else if (dut->ap_countrycode[0]) {
		owrt_ap_set_radio(dut, radio_id[0], "country",
				  dut->ap_countrycode);
	}

	if (dut->ap_disable_protection == 1) {
		owrt_ap_set_list_radio(dut, radio_id[0], "aggr_burst", "'0 0'");
		owrt_ap_set_list_radio(dut, radio_id[0], "aggr_burst", "'1 0'");
		owrt_ap_set_list_radio(dut, radio_id[0], "aggr_burst", "'2 0'");
		owrt_ap_set_list_radio(dut, radio_id[0], "aggr_burst", "'3 0'");
	}
}


static int owrt_ap_config_vap_hs2(struct sigma_dut *dut, int vap_id)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "%d", dut->ap_hs2);
	owrt_ap_set_vap(dut, vap_id, "hs20", buf);
	owrt_ap_set_vap(dut, vap_id, "qbssload", "1");
	owrt_ap_set_vap(dut, vap_id, "hs20_deauth_req_timeout","3");

	owrt_ap_set_list_vap(dut, vap_id, "hs20_oper_friendly_name",
			     "'eng:Wi-Fi Alliance'");

	owrt_ap_set_list_vap(dut, vap_id, "hs20_oper_friendly_name",
			     "'chi:Wi-Fi\xe8\x81\x94\xe7\x9b\x9f'");

	if (dut->ap_wan_metrics == 1)
		owrt_ap_set_vap(dut, vap_id, "hs20_wan_metrics",
				"'01:2500:384:0:0:10'");
	else if (dut->ap_wan_metrics == 1)
		owrt_ap_set_vap(dut, vap_id, "hs20_wan_metrics",
				"'01:1500:384:20:20:10'");
	else if (dut->ap_wan_metrics == 2)
		owrt_ap_set_vap(dut, vap_id, "hs20_wan_metrics",
				"'01:1500:384:20:20:10'");
	else if (dut->ap_wan_metrics == 3)
		owrt_ap_set_vap(dut, vap_id, "hs20_wan_metrics",
				"'01:2000:1000:20:20:10'");
	else if (dut->ap_wan_metrics == 4)
		owrt_ap_set_vap(dut, vap_id, "hs20_wan_metrics",
				"'01:8000:1000:20:20:10'");
	else if (dut->ap_wan_metrics == 5)
		owrt_ap_set_vap(dut, vap_id, "hs20_wan_metrics",
				"'01:9000:5000:20:20:10'");

	if (dut->ap_conn_capab == 1) {
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab", "'1:0:0'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'6:20:1'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'6:22:0'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'6:80:1'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'6:443:1'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'6:1723:0'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'6:5060:0'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'17:500:1'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'17:5060:0'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'17:4500:1'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'50:0:1'");
	} else if (dut->ap_conn_capab == 2) {
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'6:80:1'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'6:443:1'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'17:5060:1'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'6:5060:1'");
	} else if (dut->ap_conn_capab == 3) {
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'6:80:1'");
		owrt_ap_set_list_vap(dut, vap_id, "hs20_conn_capab",
				     "'6:443:1'");
	}

	if (dut->ap_oper_class == 1)
		snprintf(buf, sizeof(buf), "%s", "51");
	else if (dut->ap_oper_class == 2)
		snprintf(buf, sizeof(buf), "%s", "73");
	else if (dut->ap_oper_class == 3)
		snprintf(buf, sizeof(buf), "%s", "5173");

	if (dut->ap_oper_class)
		owrt_ap_set_vap(dut, vap_id, "hs20_operating_class", buf);

	if (dut->ap_osu_provider_list) {
		char *osu_friendly_name = NULL;
		char *osu_icon = NULL;
		char *osu_ssid = NULL;
		char *osu_nai = NULL;
		char *osu_service_desc = NULL;
		char *hs20_icon_filename = NULL;
		char hs20_icon[150];
		int osu_method;

		hs20_icon_filename = "icon_red_zxx.png";
		if (dut->ap_osu_icon_tag == 2)
			hs20_icon_filename = "wifi-abgn-logo_270x73.png";
		snprintf(hs20_icon, sizeof(hs20_icon),
			 "'128:61:zxx:image/png:icon_red_zxx.png:/etc/ath/%s'",
			 hs20_icon_filename);
		osu_icon = "icon_red_zxx.png";
		osu_ssid = "OSU";
		osu_friendly_name = "'kor:SP 빨강 테스트 전용'";
		osu_service_desc = "'kor:테스트 목적으로 무료 서비스'";
		osu_method = (dut->ap_osu_method[0] == 0xFF) ? 1 :
			dut->ap_osu_method[0];

		if (strlen(dut->ap_osu_server_uri[0]))
			owrt_ap_set_list_vap(dut, vap_id, "osu_server_uri",
					     dut->ap_osu_server_uri[0]);
		else
			owrt_ap_set_list_vap(dut, vap_id, "osu_server_uri",
					     "'https://osu-server.r2-testbed.wi-fi.org/'");
		switch (dut->ap_osu_provider_list) {
		case 1:
		case 101:
			owrt_ap_set_list_vap(dut, vap_id, "osu_friendly_name",
					     "'eng:SP Red Test Only'");
			owrt_ap_set_list_vap(dut, vap_id, "osu_service_desc",
					     "'eng:Free service for test purpose'");
			owrt_ap_set_list_vap(dut, vap_id, "hs20_icon",
					     hs20_icon);

			hs20_icon_filename = "icon_red_eng.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";

			snprintf(hs20_icon, sizeof(hs20_icon),
				 "'160:76:eng:image/png:icon_red_eng.png:/etc/ath/%s'",
				 hs20_icon_filename);
			owrt_ap_set_list_vap(dut, vap_id, "osu_icon",
					     "icon_red_eng.png");
			break;
		case 2:
		case 102:
			owrt_ap_set_list_vap(dut, vap_id, "osu_friendly_name",
					     "'eng:Wireless Broadband Alliance'");
			owrt_ap_set_list_vap(dut, vap_id, "osu_service_desc",
					     "'eng:Free service for test purpose'");
			hs20_icon_filename = "icon_orange_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";

			snprintf(hs20_icon, sizeof(hs20_icon),
				 "'128:61:zxx:image/png:icon_orange_zxx.png:/etc/ath/%s'",
				 hs20_icon_filename);
			osu_icon = "icon_orange_zxx.png";
			osu_friendly_name = "'kor:와이어리스 브로드밴드 얼라이언스'";
			break;
		case 3:
		case 103:
			osu_friendly_name = "spa:SP Red Test Only";
			osu_service_desc = "spa:Free service for test purpose";
			break;
		case 4:
		case 104:
			owrt_ap_set_list_vap(dut, vap_id, "osu_friendly_name",
					     "'eng:SP Orange Test Only'");
			owrt_ap_set_list_vap(dut, vap_id, "osu_service_desc",
					     "'eng:Free service for test purpose'");
			hs20_icon_filename = "icon_orange_eng.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";

			snprintf(hs20_icon, sizeof(hs20_icon),
				 "'160:76:eng:image/png:icon_orange_eng.png:/etc/ath/%s'",
				 hs20_icon_filename);
			owrt_ap_set_list_vap(dut, vap_id, "hs20_icon",
					     hs20_icon);
			osu_friendly_name = "'kor:SP 파랑 테스트 전용'";

			hs20_icon_filename = "icon_orange_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";

			snprintf(hs20_icon, sizeof(hs20_icon),
				 "'128:61:zxx:image/png:icon_orange_zxx.png:/etc/ath/%s'",
				 hs20_icon_filename);
			osu_icon = "icon_orange_zxx.png";
			break;
		case 5:
		case 105:
			owrt_ap_set_list_vap(dut, vap_id, "osu_friendly_name",
					     "'eng:SP Orange Test Only'");
			owrt_ap_set_list_vap(dut, vap_id, "osu_service_desc",
					     "'eng:Free service for test purpose'");
			osu_friendly_name = "'kor:SP 파랑 테스트 전용'";

			hs20_icon_filename = "icon_orange_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";

			snprintf(hs20_icon, sizeof(hs20_icon),
				 "'128:61:zxx:image/png:icon_orange_zxx.png:/etc/ath/%s'",
				 hs20_icon_filename);
			osu_icon = "icon_orange_zxx.png";
			break;
		case 6:
		case 106:
			owrt_ap_set_list_vap(dut, vap_id, "osu_friendly_name",
					     "'eng:SP Green Test Only'");
			owrt_ap_set_list_vap(dut, vap_id, "osu_friendly_name",
					     "'kor:SP 초록 테스트 전용'");

			hs20_icon_filename = "icon_green_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";

			snprintf(hs20_icon, sizeof(hs20_icon),
				 "'128:61:zxx:image/png:icon_green_zxx.png:/etc/ath/%s'",
				 hs20_icon_filename);
			owrt_ap_set_list_vap(dut, vap_id, "hs20_icon",
					     hs20_icon);

			owrt_ap_set_list_vap(dut, vap_id, "osu_icon",
					     "'icon_green_zxx.png'");
			osu_method = (dut->ap_osu_method[0] == 0xFF) ? 0 :
				dut->ap_osu_method[0];

			snprintf(buf, sizeof(buf), "%d", osu_method);
			owrt_ap_set_vap(dut, vap_id, "osu_method_list", buf);

			if (strlen(dut->ap_osu_server_uri[1]))
				owrt_ap_set_list_vap(dut, vap_id,
						     "osu_server_uri",
						     dut->ap_osu_server_uri[1]);
			else
				owrt_ap_set_list_vap(dut, vap_id,
						     "osu_server_uri",
						     "'https://osu-server.r2-testbed.wi-fi.org/'");

			owrt_ap_set_list_vap(dut, vap_id, "osu_friendly_name",
					     "'eng:SP Orange Test Only'");

			hs20_icon_filename = "icon_orange_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";

			snprintf(hs20_icon, sizeof(hs20_icon),
				 "'128:61:zxx:image/png:icon_orange_zxx.png:/etc/ath/%s'",
				 hs20_icon_filename);

			osu_icon = "icon_orange_zxx.png";
			osu_friendly_name = "'kor:SP 오렌지 테스트 전용'";
			osu_method = (dut->ap_osu_method[1] == 0xFF) ? 0 :
				dut->ap_osu_method[1];
			osu_service_desc = NULL;
			break;
		case 7:
		case 107:
			owrt_ap_set_list_vap(dut, vap_id, "osu_friendly_name",
					     "'eng:SP Green Test Only'");
			owrt_ap_set_list_vap(dut, vap_id, "osu_service_desc",
					     "'eng:Free service for test purpose'");

			hs20_icon_filename = "icon_green_eng.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";

			snprintf(hs20_icon, sizeof(hs20_icon),
				 "'160:76:eng:image/png:icon_green_eng.png:/etc/ath/%s'",
				 hs20_icon_filename);
			owrt_ap_set_list_vap(dut, vap_id, "hs20_icon",
					     hs20_icon);

			owrt_ap_set_list_vap(dut, vap_id, "osu_icon",
					     "'icon_green_eng.png'");
			osu_friendly_name = "'kor:SP 오렌지 테스트 전용'";

			hs20_icon_filename = "icon_green_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";

			snprintf(hs20_icon, sizeof(hs20_icon),
				 "'128:61:zxx:image/png:icon_green_zxx.png:/etc/ath/%s'",
				 hs20_icon_filename);
			osu_icon = "icon_green_zxx.png";
			break;
		case 8:
		case 108:
			owrt_ap_set_list_vap(dut, vap_id, "osu_friendly_name",
					     "'eng:SP Red Test Only'");
			owrt_ap_set_list_vap(dut, vap_id, "osu_service_desc",
					     "'eng:Free service for test purpose'");
			osu_ssid = "OSU-Encrypted";
			osu_nai = "'anonymous@hotspot.net'";
			break;
		case 9:
		case 109:
			osu_ssid = "OSU-OSEN";
			osu_nai = "'test-anonymous@wi-fi.org'";
			osu_friendly_name = "'eng:SP Orange Test Only'";
			hs20_icon_filename = "icon_orange_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";

			snprintf(hs20_icon, sizeof(hs20_icon),
				 "'128:61:zxx:image/png:icon_orange_zxx.png:/etc/ath/%s'",
				 hs20_icon_filename);
			osu_icon = "icon_orange_zxx.png";
			osu_method = (dut->ap_osu_method[0] == 0xFF) ? 1 :
				dut->ap_osu_method[0];
			osu_service_desc = NULL;
			break;
		default:
			break;
		}

		if (strlen(dut->ap_osu_ssid)) {
			if (strcmp(dut->ap_tag_ssid[0],
				   dut->ap_osu_ssid) != 0 &&
			    strcmp(dut->ap_tag_ssid[0], osu_ssid) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"OSU_SSID and WLAN_TAG2 SSID differ");
				return -2;
			}

			snprintf(buf, sizeof(buf), "'\"%s\"'",
				 dut->ap_osu_ssid);
		} else {
			snprintf(buf, sizeof(buf), "'\"%s\"'", osu_ssid);
		}

		owrt_ap_set_vap(dut, vap_id, "osu_ssid", buf);


		if (osu_friendly_name)
			owrt_ap_set_list_vap(dut, vap_id, "osu_friendly_name",
					     osu_friendly_name);
		if (osu_service_desc)
			owrt_ap_set_list_vap(dut, vap_id, "osu_service_desc",
					     osu_service_desc);
		if (osu_nai)
			owrt_ap_set_vap(dut, vap_id, "osu_nai", osu_nai);

		owrt_ap_set_list_vap(dut, vap_id, "hs20_icon", hs20_icon);

		if (osu_icon)
			owrt_ap_set_list_vap(dut, vap_id, "osu_icon",
					     osu_icon);

		if (dut->ap_osu_provider_list > 100) {
			owrt_ap_set_list_vap(dut, vap_id, "osu_method_list",
					     "0");
		} else {
			snprintf(buf, sizeof(buf), "%d", osu_method);
			owrt_ap_set_list_vap(dut, vap_id, "osu_method_list",
					     buf);
		}
	}

	return 0;
}


static void set_anqp_elem_value(struct sigma_dut *dut, const char *ifname,
				char *anqp_string, size_t str_size)
{
	unsigned char bssid[ETH_ALEN];
	unsigned char dummy_mac[] = { 0x00, 0x10, 0x20, 0x30, 0x40, 0x50 };
	int preference = 0xff;

	get_hwaddr(ifname, bssid);
	snprintf(anqp_string, str_size,
		 "272:3410%02x%02x%02x%02x%02x%02xf70000007330000301%02x3410%02x%02x%02x%02x%02x%02xf70000007330000301%02x",
		 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
		 preference,
		 dummy_mac[0], dummy_mac[1], dummy_mac[2],
		 dummy_mac[3], dummy_mac[4], dummy_mac[5],
		 preference - 1);
}


static void get_if_name(struct sigma_dut *dut, char *ifname_str,
			size_t str_size, int wlan_tag)
{
	char *ifname;
	enum driver_type drv;

	drv = get_driver_type();
	if (drv == DRIVER_ATHEROS) {
		if ((dut->ap_mode == AP_11a || dut->ap_mode == AP_11na ||
		     dut->ap_mode == AP_11ac) &&
		    if_nametoindex("ath1") > 0)
			ifname = "ath1";
		else
			ifname = "ath0";
	} else if (drv == DRIVER_OPENWRT) {
		if (sigma_radio_ifname[0] &&
		    strcmp(sigma_radio_ifname[0], "wifi2") == 0)
			ifname = "ath2";
		else if (sigma_radio_ifname[0] &&
			 strcmp(sigma_radio_ifname[0], "wifi1") == 0)
			ifname = "ath1";
		else
			ifname = "ath0";
	} else {
		if ((dut->ap_mode == AP_11a || dut->ap_mode == AP_11na ||
		     dut->ap_mode == AP_11ac) &&
		    if_nametoindex("wlan1") > 0)
			ifname = "wlan1";
		else
			ifname = "wlan0";
	}

	if (drv == DRIVER_OPENWRT && wlan_tag > 1) {
		/* Handle tagged-ifname only on OPENWRT for now */
		snprintf(ifname_str, str_size, "%s%d", ifname, wlan_tag - 1);
	} else {
		snprintf(ifname_str, str_size, "%s", ifname);
	}
}


static int owrt_ap_config_vap(struct sigma_dut *dut)
{
	char buf[256], *temp;
	int vap_id = 0, vap_count, i, j;

	for (vap_count = 0; vap_count < OPENWRT_MAX_NUM_RADIOS; vap_count++) {
		snprintf(buf, sizeof(buf), "wifi%d", vap_count);

		for (vap_id = 0; vap_id < MAX_RADIO; vap_id++) {
			if (sigma_radio_ifname[vap_id] &&
			    strcmp(sigma_radio_ifname[vap_id], buf) == 0)
				break;
		}
		if (vap_id == MAX_RADIO)
			continue;

		/* Single VAP configuration */
		if (!dut->ap_is_dual)
			vap_id = vap_count;

		for (j = 0; j < MAX_WLAN_TAGS - 1; j++) {
			/*
			 * We keep a separate array of ap_tag_ssid and
			 * ap_tag_key_mgmt for tags starting from WLAN_TAG=2.
			 * So j=0 => WLAN_TAG = 2
			 */
			int wlan_tag = j + 2;

			if (dut->ap_tag_ssid[j][0] == '\0')
				continue;

			snprintf(buf, sizeof(buf), "%s%d", "wifi", vap_count);
			owrt_ap_add_vap(dut, vap_count + (wlan_tag - 1),
					"device", buf);
			/* SSID */
			snprintf(buf, sizeof(buf), "\"%s\"",
				 dut->ap_tag_ssid[j]);
			owrt_ap_set_vap(dut, vap_count + (wlan_tag - 1),
					"ssid", buf);

			if (dut->ap_tag_key_mgmt[j] == AP2_OSEN &&
			    wlan_tag == 2) {
				/* Only supported for WLAN_TAG=2 */
				owrt_ap_set_vap(dut, vap_count + 1, "osen",
						"1");
				snprintf(buf, sizeof(buf), "wpa2");
				owrt_ap_set_vap(dut, vap_count + 1,
						"encryption", buf);
				snprintf(buf, sizeof(buf), "%s",
					 dut->ap2_radius_ipaddr);
				owrt_ap_set_vap(dut, vap_count + 1,
						"auth_server", buf);
				snprintf(buf, sizeof(buf), "%d",
					 dut->ap2_radius_port);
				owrt_ap_set_vap(dut, vap_count + 1,
						"auth_port", buf);
				snprintf(buf, sizeof(buf), "%s",
					 dut->ap2_radius_password);
				owrt_ap_set_vap(dut, vap_count + 1,
						"auth_secret", buf);
			} else if (dut->ap_tag_key_mgmt[j] == AP2_WPA2_PSK) {
				owrt_ap_set_vap(dut, vap_count + (wlan_tag - 1),
						"encryption", "psk2+ccmp");
				snprintf(buf, sizeof(buf), "\"%s\"",
					 dut->ap_passphrase);
				owrt_ap_set_vap(dut, vap_count + (wlan_tag - 1),
						"key", buf);
				snprintf(buf, sizeof(buf), "%d", dut->ap_pmf);
				owrt_ap_set_vap(dut, vap_count + 1,
						"ieee80211w", buf);
			}
		}

		/* Now set anqp_elem for wlan_tag = 1 */
		if (dut->program == PROGRAM_MBO &&
		    get_driver_type() == DRIVER_OPENWRT) {
			char anqp_string[200];

			set_anqp_elem_value(dut, sigma_radio_ifname[0],
					    anqp_string, sizeof(anqp_string));
			owrt_ap_set_list_vap(dut, vap_count, "anqp_elem",
					     anqp_string);
		}

		/* SSID */
		snprintf(buf, sizeof(buf), "\"%s\"", dut->ap_ssid);
		owrt_ap_set_vap(dut, vap_count, "ssid", buf);

		/* Encryption */
		switch (dut->ap_key_mgmt) {
		case AP_OPEN:
			if (dut->ap_cipher == AP_WEP) {
				owrt_ap_set_vap(dut, vap_count, "encryption",
						"wep-mixed");
				owrt_ap_set_vap(dut, vap_count, "key",
						dut->ap_wepkey);
			} else {
				owrt_ap_set_vap(dut, vap_count, "encryption",
						"none");
			}
			break;
		case AP_WPA2_PSK:
		case AP_WPA2_PSK_MIXED:
		case AP_WPA_PSK:
		case AP_WPA2_SAE:
		case AP_WPA2_PSK_SAE:
			/* TODO: SAE configuration */
			if (dut->ap_key_mgmt == AP_WPA2_PSK) {
				snprintf(buf, sizeof(buf), "psk2");
			} else if (dut->ap_key_mgmt == AP_WPA2_PSK_MIXED) {
				snprintf(buf, sizeof(buf), "psk-mixed");
			} else {
				snprintf(buf, sizeof(buf), "psk");
			}

			if (dut->ap_cipher == AP_CCMP_TKIP)
				strlcat(buf, "+ccmp+tkip", sizeof(buf));
			else if (dut->ap_cipher == AP_TKIP)
				strlcat(buf, "+tkip", sizeof(buf));
			else
				strlcat(buf, "+ccmp", sizeof(buf));

			owrt_ap_set_vap(dut, vap_count, "encryption", buf);
			snprintf(buf, sizeof(buf), "\"%s\"",
				 dut->ap_passphrase);
			owrt_ap_set_vap(dut, vap_count, "key", buf);
			break;
		case AP_WPA2_EAP:
		case AP_WPA2_EAP_MIXED:
		case AP_WPA_EAP:
			if (dut->ap_key_mgmt == AP_WPA2_EAP) {
				snprintf(buf, sizeof(buf), "wpa2");
			} else if (dut->ap_key_mgmt == AP_WPA2_EAP_MIXED) {
				snprintf(buf, sizeof(buf), "wpa-mixed");
			} else {
				snprintf(buf, sizeof(buf), "wpa");
			}

			if (dut->ap_cipher == AP_CCMP_TKIP)
				strlcat(buf, "+ccmp+tkip", sizeof(buf));
			else if (dut->ap_cipher == AP_TKIP)
				strlcat(buf, "+tkip", sizeof(buf));
			else
				strlcat(buf, "+ccmp", sizeof(buf));

			owrt_ap_set_vap(dut, vap_count, "encryption", buf);
			snprintf(buf, sizeof(buf), "%s", dut->ap_radius_ipaddr);
			owrt_ap_set_vap(dut, vap_count, "auth_server", buf);
			snprintf(buf, sizeof(buf), "%d", dut->ap_radius_port);
			owrt_ap_set_vap(dut, vap_count, "auth_port", buf);
			snprintf(buf, sizeof(buf), "%s",
				 dut->ap_radius_password);
			owrt_ap_set_vap(dut, vap_count, "auth_secret", buf);
			break;
		}

		if (!dut->ap_is_dual)
			break;
	}

	if (dut->ap_is_dual)
		return 1;

	/* PMF */
	snprintf(buf, sizeof(buf), "%d", dut->ap_pmf);
	owrt_ap_set_vap(dut, vap_id, "ieee80211w", buf);

	/* Add SHA256 */
	snprintf(buf, sizeof(buf), "%d", dut->ap_add_sha256);
	owrt_ap_set_vap(dut, vap_id, "add_sha256", buf);

	/* Enable RSN preauthentication, if asked to */
	snprintf(buf, sizeof(buf), "%d", dut->ap_rsn_preauth);
	owrt_ap_set_vap(dut, vap_id, "rsn_preauth", buf);

	/* Hotspot 2.0 */
	if (dut->ap_hs2) {
		int ret;

		ret = owrt_ap_config_vap_hs2(dut, vap_id);
		if (ret)
			return ret;
	}

	/* Interworking */
	if (dut->ap_interworking) {
		snprintf(buf, sizeof(buf), "%d", dut->ap_access_net_type);
		owrt_ap_set_vap(dut, vap_id, "access_network_type", buf);
		snprintf(buf, sizeof(buf), "%d", dut->ap_internet);
		owrt_ap_set_vap(dut, vap_id, "internet", buf);
		snprintf(buf, sizeof(buf), "%d", dut->ap_venue_group);
		owrt_ap_set_vap(dut, vap_id, "venue_group", buf);
		snprintf(buf, sizeof(buf), "%d", dut->ap_venue_type);
		owrt_ap_set_vap(dut, vap_id, "venue_type", buf);
		snprintf(buf, sizeof(buf), "%s", dut->ap_hessid);
		owrt_ap_set_vap(dut, vap_id, "hessid", buf);

		if (dut->ap_gas_cb_delay > 0) {
			snprintf(buf, sizeof(buf), "%d", dut->ap_gas_cb_delay);
			owrt_ap_set_vap(dut, vap_id, "gas_comeback_delay", buf);
		}

		if (dut->ap_roaming_cons[0]) {
			char *rcons, *temp_ptr;

			rcons = strdup(dut->ap_roaming_cons);
			if (rcons == NULL)
				return 0;

			temp_ptr = strchr(rcons, ';');

			if (temp_ptr)
				*temp_ptr++ = '\0';

			owrt_ap_set_list_vap(dut, vap_id, "roaming_consortium",
					     rcons);

			if (temp_ptr)
				owrt_ap_set_list_vap(dut, vap_id,
						     "roaming_consortium",
						     temp_ptr);

			free(rcons);
		}
	}

	if (dut->ap_venue_name) {
		owrt_ap_set_list_vap(dut, vap_id, "venue_name",
				     "'P\"eng:Wi-Fi Alliance\\n2989 Copper Road\\nSanta Clara, CA 95051, USA\"'");
		owrt_ap_set_list_vap(dut, vap_id, "venue_name",
				     "\'"ANQP_VENUE_NAME_1_CHI"\'");
	}

	if (dut->ap_net_auth_type == 1) {
		owrt_ap_set_vap(dut, vap_id, "network_auth_type",
				"'00https://tandc-server.wi-fi.org'");
	} else if (dut->ap_net_auth_type == 2) {
		owrt_ap_set_vap(dut, vap_id, "network_auth_type", "'01'");
	}

	if (dut->ap_nai_realm_list == 1) {
		owrt_ap_set_list_vap(dut, vap_id, "nai_realm",
				     "'0,mail.example.com;cisco.com;wi-fi.org,21[2:4][5:7]'");
		owrt_ap_set_list_vap(dut, vap_id, "nai_realm",
				     "'0,wi-fi.org;example.com,13[5:6]'");

	} else if (dut->ap_nai_realm_list == 2) {
		owrt_ap_set_list_vap(dut, vap_id, "nai_realm",
				     "'0,wi-fi.org,21[2:4][5:7]'");
	} else if (dut->ap_nai_realm_list == 3) {
		owrt_ap_set_list_vap(dut, vap_id, "nai_realm",
				     "'0,cisco.com;wi-fi.org,21[2:4][5:7]'");
		owrt_ap_set_list_vap(dut, vap_id, "nai_realm",
				     "'0,wi-fi.org;example.com,13[5:6]'");
	} else if (dut->ap_nai_realm_list == 4) {
		owrt_ap_set_list_vap(dut, vap_id, "nai_realm",
				     "'0,mail.example.com,21[2:4][5:7],13[5:6]'");
	} else if (dut->ap_nai_realm_list == 5) {
		owrt_ap_set_list_vap(dut, vap_id, "nai_realm",
				     "'0,wi-fi.org;ruckuswireless.com,21[2:4][5:7]'");
	} else if (dut->ap_nai_realm_list == 6) {
		owrt_ap_set_list_vap(dut, vap_id, "nai_realm",
				     "'0,wi-fi.org;mail.example.com,21[2:4][5:7]'");
	} else if (dut->ap_nai_realm_list == 7) {
		owrt_ap_set_list_vap(dut, vap_id, "nai_realm",
				     "'0,wi-fi.org,13[5:6]'");
		owrt_ap_set_list_vap(dut, vap_id, "nai_realm",
				     "'0,wi-fi.org,21[2:4][5:7]'");
	}

	if (dut->ap_domain_name_list[0])
		owrt_ap_set_list_vap(dut, vap_id, "domain_name",
				     dut->ap_domain_name_list);

	if (dut->ap_ip_addr_type_avail)
		owrt_ap_set_vap(dut, vap_id, "ipaddr_type_availability",
				"'0c'");

	temp = buf;

	*temp++ = '\'';

	for (i = 0; dut->ap_plmn_mcc[i][0] && dut->ap_plmn_mnc[i][0]; i++) {
		if (i)
			*temp++ = ';';

		snprintf(temp,
			 sizeof(dut->ap_plmn_mcc[i]) +
			 sizeof(dut->ap_plmn_mnc[i]) + 1,
			 "%s,%s",
			 dut->ap_plmn_mcc[i],
			 dut->ap_plmn_mnc[i]);

		temp += strlen(dut->ap_plmn_mcc[i]) +
			strlen(dut->ap_plmn_mnc[i]) + 1;
	}

	*temp++ = '\'';
	*temp++ = '\0';

	if (i)
		owrt_ap_set_vap(dut, vap_id, "anqp_3gpp_cell_net", buf);

	if (dut->ap_qos_map_set == 1)
		owrt_ap_set_vap(dut, vap_id, "qos_map_set", QOS_MAP_SET_1);
	else if (dut->ap_qos_map_set == 2)
		owrt_ap_set_vap(dut, vap_id, "qos_map_set", QOS_MAP_SET_2);

	/* Proxy-ARP */
	snprintf(buf, sizeof(buf), "%d", dut->ap_proxy_arp);
	owrt_ap_set_vap(dut, vap_id, "proxyarp", buf);

	/* DGAF */
	snprintf(buf, sizeof(buf), "%d", dut->ap_dgaf_disable);
	/* parse to hostapd */
	owrt_ap_set_vap(dut, vap_id, "disable_dgaf", buf);
	/* parse to wifi driver */
	owrt_ap_set_vap(dut, vap_id, "dgaf_disable", buf);

	/* HCBSSLoad */
	if (dut->ap_bss_load) {
		unsigned int bssload = 0;

		if (dut->ap_bss_load == 1) {
			/* STA count: 1, CU: 50, AAC: 65535 */
			bssload = 0x0132ffff;
		} else if (dut->ap_bss_load == 2) {
			/* STA count: 1, CU: 200, AAC: 65535 */
			bssload = 0x01c8ffff;
		} else if (dut->ap_bss_load == 3) {
			/* STA count: 1, CU: 75, AAC: 65535 */
			bssload = 0x014bffff;
		}

		snprintf(buf, sizeof(buf), "%d", bssload);
		owrt_ap_set_vap(dut, vap_id, "hcbssload", buf);
	}

	/* L2TIF */
	if  (dut->ap_l2tif)
		owrt_ap_set_vap(dut, vap_id, "l2tif", "1");

	if (dut->ap_disable_protection == 1)
		owrt_ap_set_vap(dut, vap_id, "enablertscts", "0");

	if (dut->ap_txBF == 1) {
		owrt_ap_set_vap(dut, vap_id, "vhtsubfee", "1");
		owrt_ap_set_vap(dut, vap_id, "vhtsubfer", "1");
		owrt_ap_set_vap(dut, vap_id, "vhtmubfer", "0");
	} else if (dut->ap_txBF == 2) {
		owrt_ap_set_vap(dut, vap_id, "vhtsubfer", "0");
		owrt_ap_set_vap(dut, vap_id, "vhtmubfer", "0");
	}

	if (dut->ap_mu_txBF)
		owrt_ap_set_vap(dut, vap_id, "vhtsubfer", "1");
		owrt_ap_set_vap(dut, vap_id, "vhtmubfer", "1");

	if (dut->ap_tx_stbc) {
		/* STBC and beamforming are mutually exclusive features */
		owrt_ap_set_vap(dut, vap_id, "implicitbf", "0");
	}

	/* enable dfsmode */
	snprintf(buf, sizeof(buf), "%d", dut->ap_dfs_mode);
	owrt_ap_set_vap(dut, vap_id, "doth", buf);

	if (dut->program == PROGRAM_LOC && dut->ap_interworking) {
		char anqpval[1024];

		owrt_ap_set_vap(dut, vap_id, "interworking", "1");

		if (dut->ap_lci == 1 && strlen(dut->ap_tag_ssid[0]) == 0) {
			snprintf(anqpval, sizeof(anqpval),
				"'265:0010%s%s060101'",
				dut->ap_val_lci, dut->ap_infoz);
			owrt_ap_set_list_vap(dut, vap_id, "anqp_elem", anqpval);
		}

		if (dut->ap_lcr == 1) {
			snprintf(anqpval, sizeof(anqpval),
				"'266:0000b2555302ae%s'",
				dut->ap_val_lcr);
			owrt_ap_set_list_vap(dut, vap_id, "anqp_elem", anqpval);
		}

		if (dut->ap_fqdn_held == 1 && dut->ap_fqdn_supl == 1)
			owrt_ap_set_list_vap(dut, vap_id, "anqp_elem",
					     "'267:00110168656c642e6578616d706c652e636f6d0011027375706c2e6578616d706c652e636f6d'");
	}

	if (dut->program == PROGRAM_MBO) {
		owrt_ap_set_vap(dut, vap_id, "interworking", "1");
		owrt_ap_set_vap(dut, vap_id, "mbo", "1");
		owrt_ap_set_vap(dut, vap_id, "rrm", "1");
		owrt_ap_set_vap(dut, vap_id, "mbo_cell_conn_pref", "1");

		owrt_ap_set_list_vap(dut, vap_id, "anqp_elem",
				     "'272:34108cfdf0020df1f7000000733000030101'");
		snprintf(buf, sizeof(buf), "%d", dut->ap_gas_cb_delay);
		owrt_ap_set_vap(dut, vap_id, "gas_comeback_delay", buf);
	}

	if (dut->ap_ft_oa == 1) {
		unsigned char self_mac[ETH_ALEN];
		char mac_str[20];

		owrt_ap_set_vap(dut, vap_id, "ft_over_ds", "0");
		owrt_ap_set_vap(dut, vap_id, "ieee80211r", "1");
		get_hwaddr(sigma_radio_ifname[0], self_mac);
		snprintf(mac_str, sizeof(mac_str),
			 "%02x:%02x:%02x:%02x:%02x:%02x",
			 self_mac[0], self_mac[1], self_mac[2],
			 self_mac[3], self_mac[4], self_mac[5]);
		owrt_ap_set_vap(dut, vap_id, "ap_macaddr", mac_str);
		owrt_ap_set_vap(dut, vap_id, "ft_psk_generate_local", "1");
		owrt_ap_set_vap(dut, vap_id, "kh_key_hex",
				"000102030405060708090a0b0c0d0e0f");
		snprintf(mac_str, sizeof(mac_str),
			 "%02x:%02x:%02x:%02x:%02x:%02x",
			 dut->ft_bss_mac_list[0][0],
			 dut->ft_bss_mac_list[0][1],
			 dut->ft_bss_mac_list[0][2],
			 dut->ft_bss_mac_list[0][3],
			 dut->ft_bss_mac_list[0][4],
			 dut->ft_bss_mac_list[0][5]);
		owrt_ap_set_vap(dut, vap_id, "ap2_macaddr", mac_str);
	}

	if ((dut->ap_ft_oa == 1 && dut->ap_name == 0) ||
	    (dut->ap_ft_oa == 1 && dut->ap_name == 2)) {
		owrt_ap_set_vap(dut, vap_id, "ap2_r1_key_holder",
				"00:01:02:03:04:06");
		owrt_ap_set_vap(dut, vap_id, "nasid2", "nas2.example.com");
		owrt_ap_set_vap(dut, vap_id, "nasid", "nas1.example.com");
		owrt_ap_set_vap(dut, vap_id, "r1_key_holder", "000102030405");
	}

	if (dut->ap_ft_oa == 1 && dut->ap_name == 1) {
		owrt_ap_set_vap(dut, vap_id, "ap2_r1_key_holder",
				"00:01:02:03:04:05");
		owrt_ap_set_vap(dut, vap_id, "nasid2", "nas1.example.com");
		owrt_ap_set_vap(dut, vap_id, "nasid", "nas2.example.com");
		owrt_ap_set_vap(dut, vap_id, "r1_key_holder", "000102030406");
	}

	if (dut->ap_mobility_domain[0])
		owrt_ap_set_vap(dut, vap_id, "mobility_domain",
				dut->ap_mobility_domain);

	return 1;
}


static int owrt_ap_config_vap_anqp(struct sigma_dut *dut)
{
	char anqpval[1024];
	unsigned char addr[6];
	unsigned char addr2[6];
	struct ifreq ifr;
	char *ifname;
	int s;
	int vap_id = 0;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open socket");
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	ifname = "ath0";
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0) {
		perror("ioctl");
		close(s);
		return -1;
	}
	memcpy(addr, ifr.ifr_hwaddr.sa_data, 6);

	memset(&ifr, 0, sizeof(ifr));
	ifname = "ath01";
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0) {
		perror("ioctl");
		close(s);
		return -1;
	}
	close(s);
	memcpy(addr2, ifr.ifr_hwaddr.sa_data, 6);

	snprintf(anqpval, sizeof(anqpval),
		 "'265:0010%s%s060101070d00%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x'",
		 dut->ap_val_lci, dut->ap_infoz,
		 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5],
		 addr2[0], addr2[1], addr2[2], addr2[3], addr2[4], addr2[5]);

	owrt_ap_set_list_vap(dut, vap_id, "anqp_elem", anqpval);
	return 0;
}


static int owrt_ap_post_config_commit(struct sigma_dut *dut,
				      struct sigma_conn *conn,
				      struct sigma_cmd *cmd)
{
	int ap_security = 0;
	int i;

	for (i = 0; i < MAX_WLAN_TAGS - 1; i++) {
		if (dut->ap_tag_key_mgmt[i] != AP2_OPEN)
			ap_security = 1;
	}
	if (dut->ap_key_mgmt != AP_OPEN)
		ap_security = 1;
	if (ap_security) {
		/* allow some time for hostapd to start before returning
		 * success */
		usleep(500000);

		if (run_hostapd_cli(dut, "ping") != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to talk to hostapd");
			return 0;
		}
	}

	if (get_openwrt_driver_type() == OPENWRT_DRIVER_ATHEROS)
		ath_ap_set_params(dut);

	/* Send response */
	return 1;
}


static int cmd_owrt_ap_config_commit(struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct sigma_cmd *cmd)
{
	/* Stop the AP */
	run_system(dut, "wifi down");

	/* Reset the wireless configuration */
	run_system(dut, "rm -rf /etc/config/wireless");
	switch (get_openwrt_driver_type()) {
	case OPENWRT_DRIVER_ATHEROS:
		run_system(dut, "wifi detect qcawifi > /etc/config/wireless");
		break;
	default:
		run_system(dut, "wifi detect > /etc/config/wireless");
		break;
	}

	/* Configure Radio & VAP, commit the config */
	owrt_ap_config_radio(dut);
	owrt_ap_config_vap(dut);
	run_system(dut, "uci commit");

	/* Start AP */
	run_system(dut, "wifi up");
	if (dut->program != PROGRAM_MBO &&
	    dut->ap_lci == 1 && dut->ap_interworking &&
	    strlen(dut->ap_tag_ssid[0]) > 0) {
		/*
		 * MBO has a different ANQP element value which is set in
		 * owrt_ap_config_vap().
		 */
		owrt_ap_config_vap_anqp(dut);
		run_system(dut, "uci commit");
		run_system(dut, "wifi");
	}

	return owrt_ap_post_config_commit(dut, conn, cmd);
}


static void cmd_owrt_ap_hs2_reset(struct sigma_dut *dut)
{
	unsigned char bssid[6];
	char buf[100];
	char *ifname, *radio_name;
	int vap_id = 0;

	if (sigma_radio_ifname[0] &&
	    strcmp(sigma_radio_ifname[0], "wifi2") == 0) {
		ifname = "ath2";
		radio_name = "wifi2";
		vap_id = 2;
	} else if (sigma_radio_ifname[0] &&
		   strcmp(sigma_radio_ifname[0], "wifi1") == 0) {
		ifname = "ath1";
		radio_name = "wifi1";
		vap_id = 1;
	} else {
		ifname = "ath0";
		radio_name = "wifi0";
		vap_id = 0;
	}

	if (!get_hwaddr(ifname, bssid)) {
		snprintf(buf, sizeof(buf), "%s", bssid);
		owrt_ap_set_vap(dut, vap_id, "hessid", buf);
		snprintf(dut->ap_hessid, sizeof(dut->ap_hessid),
			 "%02x:%02x:%02x:%02x:%02x:%02x",
			 bssid[0], bssid[1], bssid[2], bssid[3],
			 bssid[4], bssid[5]);
	} else {
		if (!get_hwaddr(radio_name, bssid)) {
			snprintf(buf, sizeof(buf), "%s", dut->ap_hessid);
			owrt_ap_set_vap(dut, vap_id, "hessid", buf);
			snprintf(dut->ap_hessid, sizeof(dut->ap_hessid),
				 "%02x:%02x:%02x:%02x:%02x:%02x",
				 bssid[0], bssid[1], bssid[2], bssid[3],
				 bssid[4], bssid[5]);
		} else {
			/* Select & enable/disable radios */
			if (sigma_radio_ifname[0] &&
			    strcmp(sigma_radio_ifname[0], "wifi2") == 0) {
				/* We want to use wifi2 */
				owrt_ap_set_radio(dut, 0, "disabled", "1");
				owrt_ap_set_radio(dut, 1, "disabled", "1");
				owrt_ap_set_radio(dut, 2, "disabled", "0");
				owrt_ap_set_vap(dut, vap_id, "device", "wifi2");
			} else if (sigma_radio_ifname[0] &&
				   strcmp(sigma_radio_ifname[0], "wifi1") == 0) {
				/* We want to use wifi1 */
				owrt_ap_set_radio(dut, 0, "disabled", "1");
				owrt_ap_set_radio(dut, 1, "disabled", "0");
				owrt_ap_set_vap(dut, vap_id, "device", "wifi1");
			} else {
				/* We want to use wifi0 */
				owrt_ap_set_radio(dut, 0, "disabled", "0");
				owrt_ap_set_radio(dut, 1, "disabled", "1");
				owrt_ap_set_vap(dut, vap_id, "device", "wifi0");
			}

			run_system(dut, "uci commit");
			run_system(dut, "wifi up");

			if (!get_hwaddr(radio_name, bssid)) {
				snprintf(buf, sizeof(buf), "%s",
					 dut->ap_hessid);
				owrt_ap_set_vap(dut, vap_id, "hessid", buf);
				snprintf(dut->ap_hessid, sizeof(dut->ap_hessid),
					 "%02x:%02x:%02x:%02x:%02x:%02x",
					 bssid[0], bssid[1], bssid[2], bssid[3],
					 bssid[4], bssid[5]);
			}
		}
	}
}


static int cmd_ap_reboot(struct sigma_dut *dut, struct sigma_conn *conn,
			 struct sigma_cmd *cmd)
{
	switch (get_driver_type()) {
	case DRIVER_ATHEROS:
		run_system(dut, "apdown");
		sleep(1);
		run_system(dut, "reboot");
		break;
	case DRIVER_OPENWRT:
		run_system(dut, "wifi down");
		sleep(1);
		run_system(dut, "reboot");
		break;
	default:
		sigma_dut_print(dut, DUT_MSG_INFO, "Ignore ap_reboot command");
		break;
	}

	return 1;
}


int ascii2hexstr(const char *str, char *hex)
{
	int i, length;

	length = strlen(str);

	for (i = 0; i < length; i++)
		snprintf(hex + i * 2, 3, "%X", str[i]);

	hex[length * 2] = '\0';
	return 1;
}


static int kill_process(struct sigma_dut *dut, char *proc_name,
			unsigned char is_proc_instance_one, int sig)
{
#ifdef __linux__
	struct dirent *dp, *dp_in;
	const char *direc = "/proc/";
	char buf[100];
	DIR *dir = opendir(direc);
	DIR *dir_in;
	FILE *fp;
	char *pid, *temp;
	char *saveptr;
	int ret = -1;

	if (dir == NULL)
		return ret;

	while ((dp = readdir(dir)) != NULL) {
		if (dp->d_type != DT_DIR)
			continue;

		snprintf(buf, sizeof(buf), "%s%s", direc, dp->d_name);
		dir_in = opendir(buf);
		if (dir_in == NULL)
			continue;
		dp_in = readdir(dir_in);
		closedir(dir_in);
		if (dp_in == NULL)
			continue;
		snprintf(buf, sizeof(buf), "%s%s/stat", direc, dp->d_name);
		fp = fopen(buf, "r");
		if (fp == NULL)
			continue;
		if (fgets(buf, 100, fp) == NULL)
			buf[0] = '\0';
		fclose(fp);
		pid = strtok_r(buf, " ", &saveptr);
		temp = strtok_r(NULL, " ", &saveptr);
		if (pid && temp &&
		    strncmp(temp, proc_name, strlen(proc_name)) == 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"killing %s process with PID %s",
					proc_name, pid);
			snprintf(buf, sizeof(buf), "kill -%d %d", sig,
				 atoi(pid));
			run_system(dut, buf);
			ret = 0;
			if (is_proc_instance_one)
				break;
		}
	}

	closedir(dir);

	return ret;
#else /* __linux__ */
	return -1;
#endif /* __linux__ */
}


static int run_ndc(struct sigma_dut *dut, char *buf)
{
	sigma_dut_print(dut, DUT_MSG_INFO, "CMD NDC:: %s", buf);
	sleep(2);
	return run_system(dut, buf);
}


static int sigma_write_cfg(struct sigma_dut *dut, const char *pfile,
			   const char *field, const char *value)
{
	FILE *fcfg, *ftmp;
	char buf[MAX_CONF_LINE_LEN + 1];
	int len, found = 0, res;

	/* Open the configuration file */
	fcfg = fopen(pfile, "r");
	if (!fcfg) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open hostapd conf file");
		return -1;
	}

	snprintf(buf, sizeof(buf), "%s~", pfile);
	/* Open a temporary file */
	ftmp = fopen(buf, "w+");
	if (!ftmp) {
		fclose(fcfg);
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open temp buf");
		return -1;
	}

	/* Read the values from the configuration file */
	len = strlen(field);
	while (fgets(buf, MAX_CONF_LINE_LEN, fcfg)) {
		char *pline = buf;

		/* commented line */
		if (buf[0] == '#')
			pline++;

		/* Identify the configuration parameter to be updated */
		if (!found && strncmp(pline, field, len) == 0 &&
		    pline[len] == '=') {
			snprintf(buf, sizeof(buf), "%s=%s\n", field, value);
			found = 1;
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Updated hostapd conf file");
		}

		fprintf(ftmp, "%s", buf);
	}

	if (!found) {
		/* Configuration line not found */
		/* Add the new line at the end of file */
		fprintf(ftmp, "%s=%s\n", field, value);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Adding a new line in hostapd conf file");
	}

	fclose(fcfg);
	fclose(ftmp);

	snprintf(buf, sizeof(buf), "%s~", pfile);

	/* Restore the updated configuration file */
	res = rename(buf, pfile);

	/* Remove the temporary file. Ignore the return value */
	unlink(buf);

	/* chmod is needed because open() may not set permissions properly
	 * depending on the current umask */
	if (chmod(pfile, 0660) < 0) {
		unlink(pfile);
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Error changing permissions");
		return -1;
	}

	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Error restoring conf file");
		return -1;
	}

	return 0;
}


static int cmd_wcn_ap_config_commit(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	char buf[100];
	struct stat s;
	int num_tries = 0, ret;

	if (kill_process(dut, "(netd)", 1, SIGKILL) == 0 ||
	    system("killall netd") == 0) {
		/* Avoid Error: Error connecting (Connection refused)
		 * Wait some time to allow netd to reinitialize.
		 */
		usleep(1500000);
	}

	while (num_tries < 10) {
		ret = run_ndc(dut, "ndc softap stopap");
		num_tries++;
		if (WIFEXITED(ret))
			ret = WEXITSTATUS(ret);
		/* On success, NDC exits with 0 */
		if (ret == 0)
			break;
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Try No. %d: ndc softap stopap failed, exit code %d",
				num_tries, ret);
	}

	if (ret != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"ndc softap stopap command failed for 10 times - giving up");

#ifdef ANDROID
	/* Unload/Load driver to cleanup the state of the driver */
	wifi_unload_driver();
	wifi_load_driver();
#else /* ANDROID */
	run_ndc(dut, "ndc softap qccmd set enable_softap=0");
	run_ndc(dut, "ndc softap qccmd set enable_softap=1");
#endif /* ANDROID */

	switch (dut->ap_mode) {
	case AP_11g:
		run_ndc(dut, "ndc softap qccmd set hw_mode=g-only");
		break;
	case AP_11b:
		run_ndc(dut, "ndc softap qccmd set hw_mode=b-only");
		break;
	case AP_11ng:
		run_ndc(dut, "ndc softap qccmd set hw_mode=n");
		break;
	case AP_11a:
		run_ndc(dut, "ndc softap qccmd set hw_mode=a-only");
		break;
	case AP_11na:
		run_ndc(dut, "ndc softap qccmd set hw_mode=n");
		break;
	case AP_11ac:
		run_ndc(dut, "ndc softap qccmd set hw_mode=ac");
		break;
	default:
		break;
	}

	snprintf(buf, sizeof(buf), "ndc softap qccmd set channel=%d",
		 dut->ap_channel);
	run_ndc(dut, buf);

	/*
	 * ndc doesn't support double quotes as SSID string, so re-write
	 * hostapd configuration file to update SSID.
	 */
	if (dut->ap_ssid[0] != '\0')
		sigma_write_cfg(dut, ANDROID_CONFIG_FILE, "ssid", dut->ap_ssid);

	switch (dut->ap_key_mgmt) {
	case AP_OPEN:
		if (dut->ap_cipher == AP_WEP) {
			run_ndc(dut, "ndc softap qccmd set security_mode=1");
			snprintf(buf, sizeof(buf),
				 "ndc softap qccmd set wep_key0=%s",
				 dut->ap_wepkey);
			run_ndc(dut, buf);
		} else {
			run_ndc(dut, "ndc softap qccmd set security_mode=0");
		}
		break;
	case AP_WPA2_PSK:
	case AP_WPA2_PSK_MIXED:
	case AP_WPA_PSK:
		if (dut->ap_key_mgmt == AP_WPA2_PSK)
			run_ndc(dut, "ndc softap qccmd set security_mode=3");
		else if (dut->ap_key_mgmt == AP_WPA2_PSK_MIXED)
			run_ndc(dut, "ndc softap qccmd set security_mode=4");
		else
			run_ndc(dut, "ndc softap qccmd set security_mode=2");

		/*
		 * ndc doesn't support some special characters as passphrase,
		 * so re-write hostapd configuration file to update Passphrase.
		 */
		if (dut->ap_passphrase[0] != '\0')
			sigma_write_cfg(dut, ANDROID_CONFIG_FILE,
					"wpa_passphrase", dut->ap_passphrase);

		if (dut->ap_cipher == AP_CCMP_TKIP)
			run_ndc(dut, "ndc softap qccmd set wpa_pairwise="
				"TKIP CCMP");
		else if (dut->ap_cipher == AP_TKIP)
			run_ndc(dut, "ndc softap qccmd set wpa_pairwise="
				"TKIP");
		else
			run_ndc(dut, "ndc softap qccmd set wpa_pairwise="
				"CCMP &");
		break;
	case AP_WPA2_SAE:
	case AP_WPA2_PSK_SAE:
	case AP_WPA2_EAP:
	case AP_WPA2_EAP_MIXED:
	case AP_WPA_EAP:
		/* Not supported */
		break;
	}

	switch (dut->ap_pmf) {
	case AP_PMF_DISABLED:
		run_ndc(dut, "ndc softap qccmd set ieee80211w=0");
		break;
	case AP_PMF_OPTIONAL:
		run_ndc(dut, "ndc softap qccmd set ieee80211w=1");
		if (dut->ap_add_sha256)
			run_ndc(dut, "ndc softap qccmd set wpa_key_mgmt=WPA-PSK WPA-PSK-SHA256");
		else
			run_ndc(dut, "ndc softap qccmd set wpa_key_mgmt=WPA-PSK");
		break;
	case AP_PMF_REQUIRED:
		run_ndc(dut, "ndc softap qccmd set ieee80211w=2");
		run_ndc(dut, "ndc softap qccmd set wpa_key_mgmt=WPA-PSK-SHA256");
		break;
	}

	if (dut->ap_countrycode[0]) {
		snprintf(buf, sizeof(buf),
			 "ndc softap qccmd set country_code=%s",
			 dut->ap_countrycode);
		run_ndc(dut, buf);
	}

	if (dut->ap_regulatory_mode == AP_80211D_MODE_ENABLED)
		run_ndc(dut, "ndc softap qccmd set ieee80211d=1");

	if (dut->ap_dfs_mode == AP_DFS_MODE_ENABLED)
		run_ndc(dut, "ndc softap qccmd set ieee80211h=1");

	run_ndc(dut, "ndc softap startap");

	snprintf(buf, sizeof(buf), "%s%s", sigma_wpas_ctrl, sigma_main_ifname);
	num_tries = 0;
	while (num_tries < 10 && (ret = stat(buf, &s) != 0)) {
		run_ndc(dut, "ndc softap stopap");
		run_ndc(dut, "ndc softap startap");
		num_tries++;
	}

	if (num_tries == 10) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Tried 10 times with ctrl "
				"iface %s :: reboot the APDUT", buf);
		return ret;
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "setting ip addr %s mask %s",
			ap_inet_addr, ap_inet_mask);
	snprintf(buf, sizeof(buf), "ifconfig %s %s netmask %s up",
		 sigma_main_ifname, ap_inet_addr, ap_inet_mask);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to intialize the interface");
		return -1;
	}

	return 1;
}


static int append_hostapd_conf_hs2(struct sigma_dut *dut, FILE *f)
{
	fprintf(f, "hs20=1\nhs20_deauth_req_timeout=3\n"
		"disable_dgaf=%d\n", dut->ap_dgaf_disable);

	if (dut->ap_oper_name) {
		fprintf(f, "hs20_oper_friendly_name=eng:Wi-Fi Alliance\n");
		fprintf(f, "hs20_oper_friendly_name=chi:Wi-Fi\xe8\x81\x94\xe7\x9b\x9f\n");
	}

	if (dut->ap_wan_metrics == 1)
		fprintf(f, "hs20_wan_metrics=01:2500:384:0:0:10\n");
	else if (dut->ap_wan_metrics == 2)
		fprintf(f, "hs20_wan_metrics=01:1500:384:20:20:10\n");
	else if (dut->ap_wan_metrics == 3)
		fprintf(f, "hs20_wan_metrics=01:2000:1000:20:20:10\n");
	else if (dut->ap_wan_metrics == 4)
		fprintf(f, "hs20_wan_metrics=01:8000:1000:20:20:10\n");
	else if (dut->ap_wan_metrics == 5)
		fprintf(f, "hs20_wan_metrics=01:9000:5000:20:20:10\n");

	if (dut->ap_conn_capab == 1) {
		fprintf(f, "hs20_conn_capab=1:0:0\n");
		fprintf(f, "hs20_conn_capab=6:20:1\n");
		fprintf(f, "hs20_conn_capab=6:22:0\n");
		fprintf(f, "hs20_conn_capab=6:80:1\n");
		fprintf(f, "hs20_conn_capab=6:443:1\n");
		fprintf(f, "hs20_conn_capab=6:1723:0\n");
		fprintf(f, "hs20_conn_capab=6:5060:0\n");
		fprintf(f, "hs20_conn_capab=17:500:1\n");
		fprintf(f, "hs20_conn_capab=17:5060:0\n");
		fprintf(f, "hs20_conn_capab=17:4500:1\n");
		fprintf(f, "hs20_conn_capab=50:0:1\n");
	} else if (dut->ap_conn_capab == 2) {
		fprintf(f, "hs20_conn_capab=6:80:1\n");
		fprintf(f, "hs20_conn_capab=6:443:1\n");
		fprintf(f, "hs20_conn_capab=17:5060:1\n");
		fprintf(f, "hs20_conn_capab=6:5060:1\n");
	} else if (dut->ap_conn_capab == 3) {
		fprintf(f, "hs20_conn_capab=6:80:1\n");
		fprintf(f, "hs20_conn_capab=6:443:1\n");
	} else if (dut->ap_conn_capab == 4) {
		fprintf(f, "hs20_conn_capab=6:80:1\n");
		fprintf(f, "hs20_conn_capab=6:443:1\n");
		fprintf(f, "hs20_conn_capab=6:5060:1\n");
		fprintf(f, "hs20_conn_capab=17:5060:1\n");
	}

	if (dut->ap_oper_class == 1)
		fprintf(f, "hs20_operating_class=51\n");
	else if (dut->ap_oper_class == 2)
		fprintf(f, "hs20_operating_class=73\n");
	else if (dut->ap_oper_class == 3)
		fprintf(f, "hs20_operating_class=5173\n");

	if (dut->ap_osu_provider_list) {
		char *osu_friendly_name = NULL;
		char *osu_icon = NULL;
		char *osu_ssid = NULL;
		char *osu_nai = NULL;
		char *osu_service_desc = NULL;
		char *hs20_icon_filename = NULL;
		char hs20_icon[150];
		int osu_method;

		hs20_icon_filename = "icon_red_zxx.png";
		if (dut->ap_osu_icon_tag == 2)
			hs20_icon_filename = "wifi-abgn-logo_270x73.png";
		snprintf(hs20_icon, sizeof(hs20_icon),
			 "128:61:zxx:image/png:icon_red_zxx.png:/etc/ath/%s",
			 hs20_icon_filename);
		osu_icon = "icon_red_zxx.png";
		osu_ssid = "OSU";
		osu_friendly_name = "kor:SP 빨강 테스트 전용";
		osu_service_desc = "kor:테스트 목적으로 무료 서비스";
		osu_method = (dut->ap_osu_method[0] == 0xFF) ? 1 : dut->ap_osu_method[0];

		if (strlen(dut->ap_osu_server_uri[0]))
			fprintf(f, "osu_server_uri=%s\n", dut->ap_osu_server_uri[0]);
		else
			fprintf(f, "osu_server_uri=https://osu-server.r2-testbed.wi-fi.org/\n");

		switch (dut->ap_osu_provider_list) {
		case 1:
		case 101:
			fprintf(f, "osu_friendly_name=eng:SP Red Test Only\n");
			fprintf(f, "osu_service_desc=eng:Free service for test purpose\n");
			hs20_icon_filename = "icon_red_eng.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";
			fprintf(f, "hs20_icon=160:76:eng:image/png:icon_red_eng.png:/etc/ath/%s\n",
				hs20_icon_filename);
			fprintf(f, "osu_icon=icon_red_eng.png\n");
			break;
		case 2:
		case 102:
			fprintf(f, "osu_friendly_name=eng:Wireless Broadband Alliance\n");
			fprintf(f, "osu_service_desc=eng:Free service for test purpose\n");
			hs20_icon_filename = "icon_orange_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";
			snprintf(hs20_icon, sizeof(hs20_icon),
				 "128:61:zxx:image/png:icon_orange_zxx.png:/etc/ath/%s",
				 hs20_icon_filename);
			osu_icon = "icon_orange_zxx.png";
			osu_friendly_name = "kor:와이어리스 브로드밴드 얼라이언스";
			break;
		case 3:
		case 103:
			osu_friendly_name = "spa:SP Red Test Only";
			osu_service_desc = "spa:Free service for test purpose";
			break;
		case 4:
		case 104:
			fprintf(f, "osu_friendly_name=eng:SP Orange Test Only\n");
			fprintf(f, "osu_service_desc=eng:Free service for test purpose\n");
			hs20_icon_filename = "icon_orange_eng.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";
			fprintf(f, "hs20_icon=160:76:eng:image/png:icon_orange_eng.png:/etc/ath/%s\n",
				hs20_icon_filename);
			fprintf(f, "osu_icon=icon_orange_eng.png\n");
			osu_friendly_name = "kor:SP 오렌지 테스트 전용";

			hs20_icon_filename = "icon_orange_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";
			snprintf(hs20_icon, sizeof(hs20_icon),
				 "128:61:zxx:image/png:icon_orange_zxx.png:/etc/ath/%s",
				 hs20_icon_filename);
			osu_icon = "icon_orange_zxx.png";
			break;
		case 5:
		case 105:
			fprintf(f, "osu_friendly_name=eng:SP Orange Test Only\n");
			fprintf(f, "osu_service_desc=eng:Free service for test purpose\n");
			osu_friendly_name = "kor:SP 오렌지 테스트 전용";
			hs20_icon_filename = "icon_orange_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";
			snprintf(hs20_icon, sizeof(hs20_icon),
				 "128:61:zxx:image/png:icon_orange_zxx.png:/etc/ath/%s",
				 hs20_icon_filename);
			osu_icon = "icon_orange_zxx.png";
			break;
		case 6:
		case 106:
			fprintf(f, "osu_friendly_name=eng:SP Green Test Only\n");
			fprintf(f, "osu_friendly_name=kor:SP 초록 테스트 전용\n");
			hs20_icon_filename = "icon_green_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";
			fprintf(f, "hs20_icon=128:61:zxx:image/png:icon_green_zxx.png:/etc/ath/%s\n",
				hs20_icon_filename);
			fprintf(f, "osu_icon=icon_green_zxx.png\n");
			osu_method = (dut->ap_osu_method[0] == 0xFF) ? 0 : dut->ap_osu_method[0];
			fprintf(f, "osu_method_list=%d\n", osu_method);

			if (strlen(dut->ap_osu_server_uri[1]))
				fprintf(f, "osu_server_uri=%s\n", dut->ap_osu_server_uri[1]);
			else
				fprintf(f, "osu_server_uri=https://osu-server.r2-testbed.wi-fi.org/\n");
			fprintf(f, "osu_friendly_name=eng:SP Orange Test Only\n");
			hs20_icon_filename = "icon_orange_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";
			snprintf(hs20_icon, sizeof(hs20_icon),
				 "128:61:zxx:image/png:icon_orange_zxx.png:/etc/ath/%s",
				 hs20_icon_filename);
			osu_icon = "icon_orange_zxx.png";
			osu_friendly_name = "kor:SP 오렌지 테스트 전용";
			osu_method = (dut->ap_osu_method[1] == 0xFF) ? 0 : dut->ap_osu_method[1];
			osu_service_desc = NULL;
			break;
		case 7:
		case 107:
			fprintf(f, "osu_friendly_name=eng:SP Green Test Only\n");
			fprintf(f, "osu_service_desc=eng:Free service for test purpose\n");
			hs20_icon_filename = "icon_green_eng.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";
			fprintf(f, "hs20_icon=160:76:eng:image/png:icon_green_eng.png:/etc/ath/%s\n",
				hs20_icon_filename);
			fprintf(f, "osu_icon=icon_green_eng.png\n");
			osu_friendly_name = "kor:SP 초록 테스트 전용";

			hs20_icon_filename = "icon_green_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";
			snprintf(hs20_icon, sizeof(hs20_icon),
				 "128:61:zxx:image/png:icon_green_zxx.png:/etc/ath/%s",
				 hs20_icon_filename);
			osu_icon = "icon_green_zxx.png";
			break;
		case 8:
		case 108:
			fprintf(f, "osu_friendly_name=eng:SP Red Test Only\n");
			fprintf(f, "osu_service_desc=eng:Free service for test purpose\n");
			osu_ssid = "OSU-Encrypted";
			osu_nai = "anonymous@hotspot.net";
			break;
		case 9:
		case 109:
			osu_ssid = "OSU-OSEN";
			osu_nai = "test-anonymous@wi-fi.org";
			osu_friendly_name = "eng:SP Orange Test Only";
			hs20_icon_filename = "icon_orange_zxx.png";
			if (dut->ap_osu_icon_tag == 2)
				hs20_icon_filename = "wifi-abgn-logo_270x73.png";
			snprintf(hs20_icon, sizeof(hs20_icon),
				 "128:61:zxx:image/png:icon_orange_zxx.png:/etc/ath/%s",
				 hs20_icon_filename);
			osu_icon = "icon_orange_zxx.png";
			osu_method = (dut->ap_osu_method[0] == 0xFF) ? 1 : dut->ap_osu_method[0];
			osu_service_desc = NULL;
			break;
		default:
			break;
		}

		if (strlen(dut->ap_osu_ssid)) {
			if (strcmp(dut->ap_tag_ssid[0], dut->ap_osu_ssid) &&
			    strcmp(dut->ap_tag_ssid[0], osu_ssid)) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"OSU_SSID and "
						"WLAN_TAG2 SSID differ");
				return -2;
			}
			fprintf(f, "osu_ssid=\"%s\"\n", dut->ap_osu_ssid);
		} else
			fprintf(f, "osu_ssid=\"%s\"\n", osu_ssid);


		if (osu_friendly_name)
			fprintf(f, "osu_friendly_name=%s\n", osu_friendly_name);

		if (osu_service_desc)
			fprintf(f, "osu_service_desc=%s\n", osu_service_desc);

		if (osu_nai)
			fprintf(f, "osu_nai=%s\n", osu_nai);

		fprintf(f, "hs20_icon=%s\n", hs20_icon);

		if (osu_icon)
			fprintf(f, "osu_icon=%s\n", osu_icon);

		if (dut->ap_osu_provider_list > 100)
			fprintf(f, "osu_method_list=0\n");
		else
			fprintf(f, "osu_method_list=%d\n", osu_method);
	}

	return 0;
}


static void write_ap_roaming_cons(FILE *f, const char *list)
{
	char *buf, *pos, *end;

	if (list == NULL || list[0] == '\0')
		return;

	buf = strdup(list);
	if (buf == NULL)
		return;

	pos = buf;
	while (pos && *pos) {
		end = strchr(pos, ';');
		if (end)
			*end++ = '\0';
		fprintf(f, "roaming_consortium=%s\n", pos);
		pos = end;
	}

	free(buf);
}


static int append_hostapd_conf_interworking(struct sigma_dut *dut, FILE *f)
{
	int i;
	char buf[100], *temp;

	if (dut->ap_gas_cb_delay > 0)
		fprintf(f, "gas_comeback_delay=%d\n",
			dut->ap_gas_cb_delay);

	fprintf(f, "interworking=1\n"
		"access_network_type=%d\n"
		"internet=%d\n"
		"asra=0\n"
		"esr=0\n"
		"uesa=0\n"
		"venue_group=%d\n"
		"venue_type=%d\n",
		dut->ap_access_net_type,
		dut->ap_internet,
		dut->ap_venue_group,
		dut->ap_venue_type);
	if (dut->ap_hessid[0])
		fprintf(f, "hessid=%s\n", dut->ap_hessid);

	write_ap_roaming_cons(f, dut->ap_roaming_cons);

	if (dut->ap_venue_name) {
		fprintf(f, "venue_name=P\"eng:Wi-Fi Alliance\\n2989 Copper Road\\nSanta Clara, CA 95051, USA\"\n");
		fprintf(f, "venue_name=%s\n", ANQP_VENUE_NAME_1_CHI);
	}

	if (dut->ap_net_auth_type == 1)
		fprintf(f, "network_auth_type=00https://tandc-server.wi-fi.org\n");
	else if (dut->ap_net_auth_type == 2)
		fprintf(f, "network_auth_type=01\n");

	if (dut->ap_nai_realm_list == 1) {
		fprintf(f, "nai_realm=0,mail.example.com;cisco.com;wi-fi.org,21[2:4][5:7]\n");
		fprintf(f, "nai_realm=0,wi-fi.org;example.com,13[5:6]\n");
	} else if (dut->ap_nai_realm_list == 2) {
		fprintf(f, "nai_realm=0,wi-fi.org,21[2:4][5:7]\n");
	} else if (dut->ap_nai_realm_list == 3) {
		fprintf(f, "nai_realm=0,cisco.com;wi-fi.org,21[2:4][5:7]\n");
		fprintf(f, "nai_realm=0,wi-fi.org;example.com,13[5:6]\n");
	} else if (dut->ap_nai_realm_list == 4) {
		fprintf(f, "nai_realm=0,mail.example.com,21[2:4][5:7],13[5:6]\n");
	} else if (dut->ap_nai_realm_list == 5) {
		fprintf(f, "nai_realm=0,wi-fi.org;ruckuswireless.com,21[2:4][5:7]\n");
	} else if (dut->ap_nai_realm_list == 6) {
		fprintf(f, "nai_realm=0,wi-fi.org;mail.example.com,21[2:4][5:7]\n");
	} else if (dut->ap_nai_realm_list == 7) {
		fprintf(f, "nai_realm=0,wi-fi.org,13[5:6]\n");
		fprintf(f, "nai_realm=0,wi-fi.org,21[2:4][5:7]\n");
	}

	if (dut->ap_domain_name_list[0]) {
		fprintf(f, "domain_name=%s\n",
			dut->ap_domain_name_list);
	}

	if (dut->ap_ip_addr_type_avail == 1) {
		fprintf(f, "ipaddr_type_availability=0c\n");
	}

	temp = buf;
	for (i = 0; dut->ap_plmn_mcc[i][0] && dut->ap_plmn_mnc[i][0];
	     i++) {
		if (i)
			*temp++ = ';';

		snprintf(temp,
			 sizeof(dut->ap_plmn_mcc[i]) +
			 sizeof(dut->ap_plmn_mnc[i]) + 1,
			 "%s,%s",
			 dut->ap_plmn_mcc[i],
			 dut->ap_plmn_mnc[i]);

		temp += strlen(dut->ap_plmn_mcc[i]) +
			strlen(dut->ap_plmn_mnc[i]) + 1;
	}
	if (i)
		fprintf(f, "anqp_3gpp_cell_net=%s\n", buf);

	if (dut->ap_qos_map_set == 1)
		fprintf(f, "qos_map_set=%s\n", QOS_MAP_SET_1);
	else if (dut->ap_qos_map_set == 2)
		fprintf(f, "qos_map_set=%s\n", QOS_MAP_SET_2);

	return 0;
}


static int ath_ap_append_hostapd_conf(struct sigma_dut *dut)
{
	FILE *f;

	if (kill_process(dut, "(hostapd)", 1, SIGTERM) == 0 ||
	    system("killall hostapd") == 0) {
		int i;

		/* Wait some time to allow hostapd to complete cleanup before
		 * starting a new process */
		for (i = 0; i < 10; i++) {
			usleep(500000);
			if (system("pidof hostapd") != 0)
				break;
		}
	}

	f = fopen("/tmp/secath0", "a");
	if (f == NULL)
		return -2;

	if (dut->ap_hs2 && append_hostapd_conf_hs2(dut, f)) {
		fclose(f);
		return -2;
	}

	if (dut->ap_interworking && append_hostapd_conf_interworking(dut, f)) {
		fclose(f);
		return -2;
	}

	fflush(f);
	fclose(f);
	return ath_ap_start_hostapd(dut);
}


static int ath_ap_start_hostapd(struct sigma_dut *dut)
{
	if (dut->ap_tag_key_mgmt[0] == AP2_OSEN)
		run_system(dut, "hostapd -B /tmp/secath0 /tmp/secath1 -e /etc/wpa2/entropy");
	else
		run_system(dut, "hostapd -B /tmp/secath0 -e /etc/wpa2/entropy");

	return 0;
}


#define LE16(a) ((((a) & 0xff) << 8) | (((a) >> 8) & 0xff))

static int cmd_ath_ap_anqpserver_start(struct sigma_dut *dut)
{
	FILE *f;
	int nai_realm = 0, domain_name = 0, oper_name = 0, venue_name = 0,
		wan_metrics = 0, conn_cap = 0, ipaddr_avail = 0, cell_net = 0;
	char buf[100];
	int i;

	f = fopen("/root/anqpserver.conf", "w");
	if (f == NULL)
		return -1;

	if (dut->ap_nai_realm_list == 1) {
		nai_realm = 1;
		fprintf(f, "dyn_nai_home_realm=encoding=00realm=mail.example.com;eap_method=15auth_id=02auth_val=04auth_id=05auth_val=07encoding=00realm=cisco.com;eap_method=15auth_id=02auth_val=04auth_id=05auth_val=07encoding=00realm=wi-fi.org;eap_method=15auth_id=02auth_val=04auth_id=05auth_val=07encoding=00realm=example.com;eap_method=0Dauth_id=05auth_val=06encoding=00realm=wi-fi.org;eap_method=0Dauth_id=05auth_val=06\n");
	} else if (dut->ap_nai_realm_list == 2) {
		nai_realm = 1;
		fprintf(f, "dyn_nai_home_realm=encoding=00realm=wi-fi.org;eap_method=0Dauth_id=05auth_val=06\n");
	} else if (dut->ap_nai_realm_list == 3) {
		nai_realm = 1;
		fprintf(f, "dyn_nai_home_realm=encoding=00realm=cisco.com;eap_method=15auth_id=02auth_val=04auth_id=05auth_val=07encoding=00realm=wi-fi.org;eap_method=15auth_id=02auth_val=04auth_id=05auth_val=07encoding=00realm=example.com;eap_method=0Dauth_id=05auth_val=06encoding=00realm=wi-fi.org;eap_method=0Dauth_id=05auth_val=06\n");
	} else if (dut->ap_nai_realm_list == 4) {
		nai_realm = 1;
		fprintf(f, "dyn_nai_home_realm=encoding=00realm=mail.example.com;eap_method=15auth_id=02auth_val=04auth_id=05auth_val=07encoding=00realm=mail.example.com;eap_method=0Dauth_id=05auth_val=06\n");
	} else
		sigma_dut_print(dut, DUT_MSG_INFO, "not setting nai_realm");

	if (dut->ap_domain_name_list[0]) {
		char *next, *start, *dnbuf, *dn1, *anqp_dn;
		int len, dn_len_max;
		dnbuf = strdup(dut->ap_domain_name_list);
		if (dnbuf == NULL) {
			fclose(f);
			return 0;
		}

		len = strlen(dnbuf);
		dn_len_max = 50 + len*2;
		anqp_dn = malloc(dn_len_max);
		if (anqp_dn == NULL) {
			free(dnbuf);
			fclose(f);
			return -1;
		}
		start = dnbuf;
		dn1 = anqp_dn;
		while (start && *start) {
			char *hexstr;

			next = strchr(start, ',');
			if (next)
				*next++ = '\0';

			len = strlen(start);
			hexstr = malloc(len * 2 + 1);
			if (hexstr == NULL) {
				free(dnbuf);
				free(anqp_dn);
				fclose(f);
				return -1;
			}
			ascii2hexstr(start, hexstr);
			snprintf(dn1, dn_len_max, "%02x%s", len, hexstr);
			free(hexstr);
			dn1 += 2 + len * 2;
			dn_len_max -= 2 + len * 2;
			start = next;
		}
		free(dnbuf);
		if (dut->ap_gas_cb_delay) {
			fprintf(f, "dyn_domain_name=0c01%04x%s",
				LE16((unsigned int) strlen(anqp_dn)), anqp_dn);
			domain_name = 1;
		} else
			fprintf(f, "domain_name=0c01%04x%s",
				LE16((unsigned int) strlen(anqp_dn)), anqp_dn);
		free(anqp_dn);
	} else
		sigma_dut_print(dut, DUT_MSG_INFO, "not setting domain_name");

	sigma_dut_print(dut, DUT_MSG_INFO, "not setting roaming_consortium");

	if (dut->ap_oper_name) {
		if (dut->ap_gas_cb_delay) {
			fprintf(f, "dyn_oper_friendly_name="
				ANQP_HS20_OPERATOR_FRIENDLY_NAME_1 "\n");
			oper_name = 1;
		} else
			fprintf(f, "oper_friendly_name="
				ANQP_HS20_OPERATOR_FRIENDLY_NAME_1 "\n");
	} else
		sigma_dut_print(dut, DUT_MSG_INFO, "not setting oper_name");

	if (dut->ap_venue_name) {
		if (dut->ap_gas_cb_delay) {
			fprintf(f, "dyn_venue_name=" ANQP_VENUE_NAME_1 "\n");
			venue_name = 1;
		} else
			fprintf(f, "venue_name=" ANQP_VENUE_NAME_1 "\n");
	} else
		sigma_dut_print(dut, DUT_MSG_ERROR, "not setting venue_name");

	if (dut->ap_wan_metrics) {
		if (dut->ap_gas_cb_delay) {
			fprintf(f, "dyn_wan_metrics=" ANQP_HS20_WAN_METRICS_1 "\n");
			wan_metrics = 1;
		} else
			fprintf(f, "wan_metrics=" ANQP_HS20_WAN_METRICS_1
				"\n");
	} else
		sigma_dut_print(dut, DUT_MSG_ERROR, "not setting wan_metrics");

	if (dut->ap_conn_capab) {
		if (dut->ap_gas_cb_delay) {
			fprintf(f, "dyn_conn_capability="
				ANQP_HS20_CONNECTION_CAPABILITY_1 "\n");
			conn_cap = 1;
		} else
			fprintf(f, "conn_capability="
				ANQP_HS20_CONNECTION_CAPABILITY_1 "\n");
	} else
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"not setting conn_capability");

	if (dut->ap_ip_addr_type_avail) {
		if (dut->ap_gas_cb_delay) {
			fprintf(f, "dyn_ipaddr_type=" ANQP_IP_ADDR_TYPE_1
				"\n");
			ipaddr_avail = 1;
		} else
			fprintf(f, "ipaddr_type=" ANQP_IP_ADDR_TYPE_1 "\n");
	} else
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"not setting ipaddr_type_avail");

	for (i = 0; dut->ap_plmn_mcc[i][0] && dut->ap_plmn_mnc[i][0]; i++) {
		snprintf(buf + i * 6, sizeof(buf) - i * 6, "%c%c%c%c%c%c",
			 dut->ap_plmn_mcc[i][1],
			 dut->ap_plmn_mcc[i][0],
			 dut->ap_plmn_mnc[i][2] == '\0' ?
			 'f' : dut->ap_plmn_mnc[i][2],
			 dut->ap_plmn_mcc[i][2],
			 dut->ap_plmn_mnc[i][1],
			 dut->ap_plmn_mnc[i][0]);
	}
	if (i) {
		uint16_t ie_len = (i * 3) + 5;
		if (dut->ap_gas_cb_delay) {
			fprintf(f, "dyn_cell_net=0801");
			cell_net = 1;
		} else
			fprintf(f, "cell_net=0801");
		fprintf(f, "%04x", LE16(ie_len));
		fprintf(f, "00");                /* version */
		fprintf(f, "%02x", (i * 3) + 3); /* user data hdr length */
		fprintf(f, "00");                /* plmn list */
		fprintf(f, "%02x", (i * 3) + 1); /* length of plmn list */
		fprintf(f, "%02x", i);           /* number of plmns */
		fprintf(f, "%s\n", buf);           /* plmns */
	} else
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"not setting 3gpp_cellular_network");

	if (nai_realm || domain_name || oper_name || venue_name ||
		wan_metrics || conn_cap || ipaddr_avail || cell_net) {
		fprintf(f, "anqp_attach=");
		if (venue_name)
			fprintf(f, "00000104%4.4x", dut->ap_gas_cb_delay);
		if (nai_realm)
			fprintf(f, "00000107%4.4x", dut->ap_gas_cb_delay);
		if (cell_net)
			fprintf(f, "00000108%4.4x", dut->ap_gas_cb_delay);
		if (domain_name)
			fprintf(f, "0000010c%4.4x", dut->ap_gas_cb_delay);
		if (oper_name)
			fprintf(f, "00010003%4.4x", dut->ap_gas_cb_delay);
		if (wan_metrics)
			fprintf(f, "00010004%4.4x", dut->ap_gas_cb_delay);
		if (conn_cap)
			fprintf(f, "00010005%4.4x", dut->ap_gas_cb_delay);
		fprintf(f, "00010006%4.4x", dut->ap_gas_cb_delay);
		fprintf(f, "\n");
	}

	fclose(f);

	run_system(dut, "anqpserver -i ath0 &");
	if (!dut->ap_anqpserver_on)
		run_system(dut, "killall anqpserver");

	return 1;
}


static void cmd_ath_ap_radio_config(struct sigma_dut *dut)
{
	char buf[100];

	run_system(dut, "cfg -a AP_STARTMODE=standard");

	if (sigma_radio_ifname[0] &&
	    strcmp(sigma_radio_ifname[0], "wifi1") == 0) {
		run_system(dut, "cfg -a AP_RADIO_ID=1");
		switch (dut->ap_mode) {
		case AP_11g:
			run_system(dut, "cfg -a AP_CHMODE_2=11G");
			break;
		case AP_11b:
			run_system(dut, "cfg -a AP_CHMODE_2=11B");
			break;
		case AP_11ng:
			run_system(dut, "cfg -a AP_CHMODE_2=11NGHT20");
			break;
		case AP_11a:
			run_system(dut, "cfg -a AP_CHMODE_2=11A");
			break;
		case AP_11na:
			run_system(dut, "cfg -a AP_CHMODE_2=11NAHT20");
			break;
		case AP_11ac:
			run_system(dut, "cfg -a AP_CHMODE_2=11ACVHT80");
			break;
		default:
			run_system(dut, "cfg -a AP_CHMODE_2=11ACVHT80");
			break;
		}

		switch (dut->ap_rx_streams) {
		case 1:
			run_system(dut, "cfg -a RX_CHAINMASK_2=1");
			break;
		case 2:
			run_system(dut, "cfg -a RX_CHAINMASK_2=3");
			break;
		case 3:
			run_system(dut, "cfg -a RX_CHAINMASK_2=7");
			break;
		}

		switch (dut->ap_tx_streams) {
		case 1:
			run_system(dut, "cfg -a TX_CHAINMASK_2=1");
			break;
		case 2:
			run_system(dut, "cfg -a TX_CHAINMASK_2=3");
			break;
		case 3:
			run_system(dut, "cfg -a TX_CHAINMASK_2=7");
			break;
		}

		switch (dut->ap_chwidth) {
		case AP_20:
			run_system(dut, "cfg -a AP_CHMODE_2=11ACVHT20");
			break;
		case AP_40:
			run_system(dut, "cfg -a AP_CHMODE_2=11ACVHT40");
			break;
		case AP_80:
			run_system(dut, "cfg -a AP_CHMODE_2=11ACVHT80");
			break;
		case AP_160:
		case AP_AUTO:
		default:
			run_system(dut, "cfg -a AP_CHMODE_2=11ACVHT80");
			break;
		}

		if (dut->ap_tx_stbc) {
			run_system(dut, "cfg -a TX_STBC_2=1");
		}

		snprintf(buf, sizeof(buf), "cfg -a AP_PRIMARY_CH_2=%d",
			 dut->ap_channel);

		if (dut->ap_is_dual) {
			switch (dut->ap_mode_1) {
			case AP_11g:
				run_system(dut, "cfg -a AP_CHMODE=11G");
				break;
			case AP_11b:
				run_system(dut, "cfg -a AP_CHMODE=11B");
				break;
			case AP_11ng:
				run_system(dut, "cfg -a AP_CHMODE=11NGHT20");
				break;
			case AP_11a:
				run_system(dut, "cfg -a AP_CHMODE=11A");
				break;
			case AP_11na:
				run_system(dut, "cfg -a AP_CHMODE=11NAHT20");
				break;
			case AP_11ac:
				run_system(dut, "cfg -a AP_CHMODE=11ACVHT80");
				break;
			default:
				run_system(dut, "cfg -a AP_CHMODE=11NGHT20");
				break;
			}
			snprintf(buf, sizeof(buf), "cfg -a AP_PRIMARY_CH=%d",
				 dut->ap_channel_1);
		}
		run_system(dut, buf);
	} else {
		run_system(dut, "cfg -a AP_RADIO_ID=0");
		switch (dut->ap_mode) {
		case AP_11g:
			run_system(dut, "cfg -a AP_CHMODE=11G");
			break;
		case AP_11b:
			run_system(dut, "cfg -a AP_CHMODE=11B");
			break;
		case AP_11ng:
			run_system(dut, "cfg -a AP_CHMODE=11NGHT20");
			break;
		case AP_11a:
			run_system(dut, "cfg -a AP_CHMODE=11A");
			break;
		case AP_11na:
			run_system(dut, "cfg -a AP_CHMODE=11NAHT20");
			break;
		case AP_11ac:
			run_system(dut, "cfg -a AP_CHMODE=11ACVHT80");
			break;
		default:
			run_system(dut, "cfg -a AP_CHMODE=11NGHT20");
			break;
		}
		snprintf(buf, sizeof(buf), "cfg -a AP_PRIMARY_CH=%d",
			 dut->ap_channel);
		run_system(dut, buf);
	}

	if (dut->ap_sgi80 == 1) {
		run_system(dut, "cfg -a SHORTGI=1");
		run_system(dut, "cfg -a SHORTGI_2=1");
	} else if (dut->ap_sgi80 == 0) {
		run_system(dut, "cfg -a SHORTGI=0");
		run_system(dut, "cfg -a SHORTGI_2=0");
	}

	if (dut->ap_ldpc == VALUE_ENABLED)
		run_system(dut, "cfg -a LDPC=1");
	else if (dut->ap_ldpc == VALUE_DISABLED)
		run_system(dut, "cfg -a LDPC=0");
}


void ath_disable_txbf(struct sigma_dut *dut, const char *intf)
{
	char buf[50];

	snprintf(buf, sizeof(buf), "iwpriv %s vhtsubfee 0", intf);
	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv vhtsubfee failed");

	snprintf(buf, sizeof(buf), "iwpriv %s vhtsubfer 0", intf);
	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv vhtsubfer failed");

	snprintf(buf, sizeof(buf), "iwpriv %s vhtmubfee 0", intf);
	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv vhtmubfee failed");

	snprintf(buf, sizeof(buf), "iwpriv %s vhtmubfer 0", intf);
	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv vhtmubfer failed");
}


static void ath_set_assoc_disallow(struct sigma_dut *dut, const char *ifname,
				   const char *val)
{
	if (strcasecmp(val, "enable") == 0) {
		run_system_wrapper(dut, "iwpriv %s mbo_asoc_dis 1", ifname);
	} else if (strcasecmp(val, "disable") == 0) {
		run_system_wrapper(dut, "iwpriv %s mbo_asoc_dis 0", ifname);
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Unsupported assoc_disallow");
	}
}


static void apply_mbo_pref_ap_list(struct sigma_dut *dut)
{
	int i;
	int least_pref = 1 << 8;
	char ifname[20];
	uint8_t self_mac[ETH_ALEN];
	char buf[200];
	int ap_ne_class, ap_ne_pref, ap_ne_op_ch;

	get_if_name(dut, ifname, sizeof(ifname), 1);
	get_hwaddr(ifname, self_mac);

	/* Clear off */
	snprintf(buf, sizeof(buf),
		 "wifitool %s setbssidpref 00:00:00:00:00:00 0 0 0",
		 ifname);
	run_system(dut, buf);

	/* Find the least preference number */
	for (i = 0; i < dut->mbo_pref_ap_cnt; i++) {
		unsigned char *mac_addr = dut->mbo_pref_aps[i].mac_addr;

		ap_ne_class = 1;
		ap_ne_pref = 255;
		ap_ne_op_ch = 1;
		if (dut->mbo_pref_aps[i].ap_ne_pref != -1)
			ap_ne_pref = dut->mbo_pref_aps[i].ap_ne_pref;
		if (dut->mbo_pref_aps[i].ap_ne_class != -1)
			ap_ne_class = dut->mbo_pref_aps[i].ap_ne_class;
		if (dut->mbo_pref_aps[i].ap_ne_op_ch != -1)
			ap_ne_op_ch = dut->mbo_pref_aps[i].ap_ne_op_ch;

		if (ap_ne_pref < least_pref)
			least_pref = ap_ne_pref;
		snprintf(buf, sizeof(buf),
			 "wifitool %s setbssidpref %02x:%02x:%02x:%02x:%02x:%02x %d %d %d",
			 ifname, mac_addr[0], mac_addr[1], mac_addr[2],
			 mac_addr[3], mac_addr[4], mac_addr[5],
			 ap_ne_pref, ap_ne_class, ap_ne_op_ch);
		run_system(dut, buf);
	}

	/* Now add the self AP Address */
	if (dut->mbo_self_ap_tuple.ap_ne_class == -1) {
		if (dut->ap_channel <= 11)
			ap_ne_class = 81;
		else
			ap_ne_class = 115;
	} else {
		ap_ne_class = dut->mbo_self_ap_tuple.ap_ne_class;
	}

	if (dut->mbo_self_ap_tuple.ap_ne_op_ch == -1)
		ap_ne_op_ch = dut->ap_channel;
	else
		ap_ne_op_ch = dut->mbo_self_ap_tuple.ap_ne_op_ch;

	if (dut->mbo_self_ap_tuple.ap_ne_pref == -1)
		ap_ne_pref = least_pref - 1;
	else
		ap_ne_pref = dut->mbo_self_ap_tuple.ap_ne_pref;

	snprintf(buf, sizeof(buf),
		 "wifitool %s setbssidpref %02x:%02x:%02x:%02x:%02x:%02x %d %d %d",
		 ifname, self_mac[0], self_mac[1], self_mac[2],
		 self_mac[3], self_mac[4], self_mac[5],
		 ap_ne_pref,
		 ap_ne_class,
		 ap_ne_op_ch);
	run_system(dut, buf);
}


static void ath_ap_set_params(struct sigma_dut *dut)
{
	const char *basedev = "wifi1";
	const char *ifname = get_main_ifname();
	int i;
	char buf[300];

	if (sigma_radio_ifname[0])
		basedev = sigma_radio_ifname[0];

	if (dut->ap_countrycode[0]) {
		snprintf(buf, sizeof(buf), "iwpriv %s setCountry %s",
			 basedev, dut->ap_countrycode);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv setCountry failed");
		}
		sigma_dut_print(dut, DUT_MSG_INFO, "Set countrycode");
	}

	for (i = 0; i < NUM_AP_AC; i++) {
		if (dut->ap_qos[i].ac) {
			snprintf(buf, sizeof(buf), "iwpriv %s cwmin %d 0 %d",
				 ifname, i, dut->ap_qos[i].cwmin);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv cwmin failed");
			}

			snprintf(buf, sizeof(buf), "iwpriv %s cwmax %d 0 %d",
				 ifname, i, dut->ap_qos[i].cwmax);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv cwmax failed");
			}

			snprintf(buf, sizeof(buf), "iwpriv %s aifs %d 0 %d",
				 ifname, i, dut->ap_qos[i].aifs);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv aifs failed");
			}

			snprintf(buf, sizeof(buf),
				 "iwpriv %s txoplimit %d 0 %d",
				 ifname, i, dut->ap_qos[i].txop);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv txoplimit failed");
			}

			snprintf(buf, sizeof(buf), "iwpriv %s acm %d 0 %d",
				 ifname, i, dut->ap_qos[i].acm);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv acm failed");
			}
		}
	}

	for (i = 0; i < NUM_AP_AC; i++) {
		if (dut->ap_sta_qos[i].ac) {
			snprintf(buf, sizeof(buf), "iwpriv %s cwmin %d 1 %d",
				 ifname, i, dut->ap_sta_qos[i].cwmin);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv cwmin failed");
			}

			snprintf(buf, sizeof(buf), "iwpriv %s cwmax %d 1 %d",
				 ifname, i, dut->ap_sta_qos[i].cwmax);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv cwmax failed");
			}

			snprintf(buf, sizeof(buf), "iwpriv %s aifs %d 1 %d",
				 ifname, i, dut->ap_sta_qos[i].aifs);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv aifs failed");
			}

			snprintf(buf, sizeof(buf),
				 "iwpriv %s txoplimit %d 1 %d",
				 ifname, i, dut->ap_sta_qos[i].txop);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv txoplimit failed");
			}

			snprintf(buf, sizeof(buf), "iwpriv %s acm %d 1 %d",
				 ifname, i, dut->ap_sta_qos[i].acm);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv acm failed");
			}
		}
	}

	if (dut->ap_disable_protection == 1) {
		snprintf(buf, sizeof(buf), "iwpriv %s enablertscts 0", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv enablertscts failed");
		}
		sigma_dut_print(dut, DUT_MSG_INFO, "Disabled rtscts");
	}

	if (dut->ap_ldpc == VALUE_ENABLED) {
		snprintf(buf, sizeof(buf), "iwpriv %s ldpc 3", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv ldpc 3 failed");
		}
	} else if (dut->ap_ldpc == VALUE_DISABLED) {
		snprintf(buf, sizeof(buf), "iwpriv %s ldpc 0", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv ldpc 0 failed");
		}
	}

	if (dut->ap_ampdu == VALUE_ENABLED) {
		snprintf(buf, sizeof(buf), "iwpriv %s ampdu 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv ampdu 1 failed");
		}
	} else if (dut->ap_ampdu == VALUE_DISABLED) {
		snprintf(buf, sizeof(buf), "iwpriv %s ampdu 0", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv ampdu 0 failed");
		}
	}

	if (dut->ap_ampdu_exp) {
		if (dut->program == PROGRAM_VHT) {
			snprintf(buf, sizeof(buf), "iwpriv %s vhtmaxampdu %d",
				 ifname, dut->ap_ampdu_exp);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv vhtmaxampdu failed");
			}
		} else {
			/* 11N */
			snprintf(buf, sizeof(buf), "iwpriv %s maxampdu %d",
				 ifname, dut->ap_ampdu_exp);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv maxampdu failed");
			}
		}
	}

	if (dut->ap_noack == VALUE_ENABLED) {
		snprintf(buf, sizeof(buf), "iwpriv %s noackpolicy 0 0 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv noackpolicy 0 0  1 failed");
		}
		snprintf(buf, sizeof(buf), "iwpriv %s noackpolicy 1 0 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv noackpolicy 1 0 1 failed");
		}
		snprintf(buf, sizeof(buf), "iwpriv %s noackpolicy 2 0 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv noackpolicy 2 0 1 failed");
		}
		snprintf(buf, sizeof(buf), "iwpriv %s noackpolicy 3 0 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv noackpolicy 3 0 1 failed");
		}
	} else if (dut->ap_noack == VALUE_DISABLED) {
		snprintf(buf, sizeof(buf), "iwpriv %s noackpolicy 0 0 0", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv noackpolicy 0 0 0 failed");
		}
		snprintf(buf, sizeof(buf), "iwpriv %s noackpolicy 1 0 0", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv noackpolicy 1 0 0 failed");
		}
		snprintf(buf, sizeof(buf), "iwpriv %s noackpolicy 2 0 0", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv noackpolicy 2 0 0 failed");
		}
		snprintf(buf, sizeof(buf), "iwpriv %s noackpolicy 3 0 0", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv noackpolicy 3 0 0 failed");
		}
	}

	if (dut->device_type == AP_testbed && dut->ap_vhtmcs_map) {
		snprintf(buf, sizeof(buf), "iwpriv %s vht_mcsmap 0x%04x",
			 ifname, dut->ap_vhtmcs_map);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv vht_mcsmap failed");
		}
	}

	if (dut->ap_amsdu == VALUE_ENABLED) {
		snprintf(buf, sizeof(buf), "iwpriv %s amsdu 3", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv amsdu 3 failed");
		}
	} else if (dut->ap_amsdu == VALUE_DISABLED) {
		snprintf(buf, sizeof(buf), "iwpriv %s amsdu 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv amsdu 1 failed");
		}
	}

	if (dut->ap_rx_amsdu == VALUE_ENABLED) {
		snprintf(buf, sizeof(buf), "iwpriv wifi1 rx_amsdu 1");
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv rx_amsdu 1 failed");
		}
	} else if (dut->ap_rx_amsdu == VALUE_DISABLED) {
		snprintf(buf, sizeof(buf), "iwpriv wifi1 rx_amsdu 0");
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv rx_amsdu 0 failed");
		}
	}

	if (dut->ap_rx_stbc == 1) {
		snprintf(buf, sizeof(buf), "iwpriv %s rx_stbc 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv rx_stbc 1 failed");
		}
	}

	if (dut->ap_opmode_notify == 1) {
		snprintf(buf, sizeof(buf), "iwpriv %s opmode_notify 0", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv opmode_notify 0 failed");
		}
	}

	if (dut->ap_sgi80 == 1) {
		snprintf(buf, sizeof(buf), "iwpriv %s shortgi 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv shortgi 1 failed");
		}
	}
	/* Command sequence to generate single VHT AMSDU and MPDU */
	if (dut->ap_addba_reject != VALUE_NOT_SET &&
	    dut->ap_ampdu == VALUE_DISABLED &&
	    dut->ap_amsdu == VALUE_ENABLED) {
		snprintf(buf, sizeof(buf), "iwpriv %s setaddbaoper 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv setaddbaoper 1 failed");
		}

		snprintf(buf, sizeof(buf),
			 "wifitool %s senddelba 1 0 1 4", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"wifitool senddelba failed");
		}

		snprintf(buf, sizeof(buf), "wifitool %s sendsingleamsdu 1 0",
			 ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"wifitool sendsingleamsdu failed");
		}

		snprintf(buf, sizeof(buf), "iwpriv %s amsdu 10", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv amsdu failed");
		}
	}

	if (dut->ap_mode == AP_11ac) {
		int chwidth, nss;

		switch (dut->ap_chwidth) {
		case AP_20:
			chwidth = 0;
			break;
		case AP_40:
			chwidth = 1;
			break;
		case AP_80:
			chwidth = 2;
			break;
		case AP_160:
			chwidth = 3;
			break;
		default:
			chwidth = 0;
			break;
		}

		switch (dut->ap_tx_streams) {
		case 1:
			nss = 1;
			break;
		case 2:
			nss = 2;
			break;
		case 3:
			nss = 3;
			break;
		case 4:
			nss = 4;
			break;
		default:
			nss = 3;
			break;
		}

		if (dut->ap_fixed_rate) {
			if (nss == 4)
				ath_disable_txbf(dut, ifname);

			/* Set the nss */
			snprintf(buf, sizeof(buf), "iwpriv %s nss %d",
				 ifname, nss);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv nss failed");
			}

			/* Set the channel width */
			snprintf(buf, sizeof(buf), "iwpriv %s chwidth %d",
				 ifname, chwidth);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv chwidth failed");
			}

			/* Set the VHT MCS */
			snprintf(buf, sizeof(buf), "iwpriv %s vhtmcs %d",
				 ifname, dut->ap_mcs);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv vhtmcs failed");
			}
		}
	}

	if (dut->ap_dyn_bw_sig == VALUE_ENABLED) {
		snprintf(buf, sizeof(buf), "iwpriv %s cwmenable 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv cwmenable 1 failed");
		}
	} else if (dut->ap_dyn_bw_sig == VALUE_DISABLED) {
		snprintf(buf, sizeof(buf), "iwpriv %s cwmenable 0", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv cwmenable 0 failed");
		}
	}

	if (dut->ap_sig_rts == VALUE_ENABLED) {
		snprintf(buf, sizeof(buf), "iwconfig %s rts 64", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwconfig rts 64 failed");
		}
	} else if (dut->ap_sig_rts == VALUE_DISABLED) {
		snprintf(buf, sizeof(buf), "iwconfig %s rts 2347", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv rts 2347 failed");
		}
	}

	if (dut->ap_hs2) {
		snprintf(buf, sizeof(buf), "iwpriv %s qbssload 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv qbssload failed");
		}
		sigma_dut_print(dut, DUT_MSG_INFO, "Enabled qbssload");
	}

	if (dut->ap_bss_load && dut->ap_bss_load != -1) {
		unsigned int bssload = 0;

		if (dut->ap_bss_load == 1) {
			/* STA count: 1, CU: 50, AAC: 65535 */
			bssload = 0x0132ffff;
		} else if (dut->ap_bss_load == 2) {
			/* STA count: 1, CU: 200, AAC: 65535 */
			bssload = 0x01c8ffff;
		} else if (dut->ap_bss_load == 3) {
			/* STA count: 1, CU: 75, AAC: 65535 */
			bssload = 0x014bffff;
		}

		snprintf(buf, sizeof(buf), "iwpriv %s hcbssload %u",
			 ifname, bssload);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv hcbssload failed");
		}
	} else if (dut->ap_bss_load == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s qbssload 0", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv qbssload failed");
		}
		sigma_dut_print(dut, DUT_MSG_INFO, "Disabled qbssload");
	}

	if (dut->ap_dgaf_disable) {
		snprintf(buf, sizeof(buf), "iwpriv %s dgaf_disable 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv dgaf_disable failed");
		}
		sigma_dut_print(dut, DUT_MSG_INFO, "Enabled dgaf_disable");
	}

	if (dut->ap_l2tif) {
		snprintf(buf, sizeof(buf), "iwpriv %s l2tif 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv l2tif failed");
		}
		snprintf(buf, sizeof(buf),
			"echo 1 > /sys/class/net/br0/brif/ath0/hotspot_l2tif");
		if (system(buf) != 0)
			sigma_dut_print(dut, DUT_MSG_ERROR,
				"l2tif br failed");

		snprintf(buf, sizeof(buf),
			"echo 1 > /sys/class/net/br0/brif/eth0/hotspot_wan");
		if (system(buf) != 0)
			sigma_dut_print(dut, DUT_MSG_ERROR,
				"l2tif brif failed");
		sigma_dut_print(dut, DUT_MSG_INFO, "Enabled l2tif");
	}

	if (dut->ap_ndpa_frame == 0) {
		snprintf(buf, sizeof(buf),
			 "wifitool %s beeliner_fw_test 117 192", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"wifitool beeliner_fw_test 117 192 failed");
		}
		snprintf(buf, sizeof(buf),
			 "wifitool %s beeliner_fw_test 118 192", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"wifitool beeliner_fw_test 117 192 failed");
		}
	} else if (dut->ap_ndpa_frame == 1) {
		/* Driver default - no changes needed */
	} else if (dut->ap_ndpa_frame == 2) {
		snprintf(buf, sizeof(buf),
			 "wifitool %s beeliner_fw_test 115 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"wifitool beeliner_fw_test 117 192 failed");
		}
		snprintf(buf, sizeof(buf),
			 "wifitool %s beeliner_fw_test 116 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"wifitool beeliner_fw_test 117 192 failed");
		}
	}

	if (dut->ap_rtt == 1) {
		snprintf(buf, sizeof(buf), "iwpriv %s enable_rtt 1", ifname);
		run_system(dut, buf);
	}

	if (dut->ap_lci == 1) {
		snprintf(buf, sizeof(buf), "iwpriv %s enable_lci 1", ifname);
		run_system(dut, buf);
	}

	if (dut->ap_lcr == 1) {
		snprintf(buf, sizeof(buf), "iwpriv %s enable_lcr 1", ifname);
		run_system(dut, buf);
	}

	if (dut->ap_rrm == 1) {
		snprintf(buf, sizeof(buf), "iwpriv %s rrm 1", ifname);
		run_system(dut, buf);
	}

	if (dut->ap_lci == 1 || dut->ap_lcr == 1) {
		run_system(dut, "wpc -l /tmp/lci_cfg.txt");
	}

	if (dut->ap_neighap >= 1 && dut->ap_lci == 0) {
		FILE *f;

		f = fopen("/tmp/nbr_report.txt", "w");
		if (!f) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to open /tmp/nbr_report.txt");
			return;
		}

		fprintf(f,
			"ap_1 = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x00 0x3c 0x00 0x00 0x80 0x%x 0x09 0x06 0x03 0x02 0x2a 0x00\n",
			dut->ap_val_neighap[0][0], dut->ap_val_neighap[0][1],
			dut->ap_val_neighap[0][2], dut->ap_val_neighap[0][3],
			dut->ap_val_neighap[0][4], dut->ap_val_neighap[0][5],
			dut->ap_val_opchannel[0]);
		fclose(f);

		f = fopen("/tmp/ftmrr.txt", "w");
		if (!f) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to open /tmp/ftmrr.txt");
			return;
		}

		fprintf(f,
			"ap_1 = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x00 0x3c 0x00 0x00 0x80 0x%x 0x09 0x06 0x03 0x02 0x2a 0x00\n",
			dut->ap_val_neighap[0][0], dut->ap_val_neighap[0][1],
			dut->ap_val_neighap[0][2], dut->ap_val_neighap[0][3],
			dut->ap_val_neighap[0][4], dut->ap_val_neighap[0][5],
			dut->ap_val_opchannel[0]);
		fclose(f);
	}

	if (dut->ap_neighap >= 2 && dut->ap_lci == 0) {
		FILE *f;

		f = fopen("/tmp/nbr_report.txt", "a");
		if (!f) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to open /tmp/nbr_report.txt");
			return;
		}
		fprintf(f,
			"ap_2 = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x00 0x3c 0x00 0x00 0x80 0x%x 0x09 0x06 0x03 0x02 0x6a 0x00\n",
			dut->ap_val_neighap[1][0], dut->ap_val_neighap[1][1],
			dut->ap_val_neighap[1][2], dut->ap_val_neighap[1][3],
			dut->ap_val_neighap[1][4], dut->ap_val_neighap[1][5],
			dut->ap_val_opchannel[1]);
		fclose(f);

		f = fopen("/tmp/ftmrr.txt", "a");
		if (!f) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to open /tmp/ftmrr.txt");
			return;
		}
		fprintf(f,
			"ap_2 = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x00 0x3c 0x00 0x00 0x80 0x%x 0x09 0x06 0x03 0x02 0x6a 0x00\n",
			dut->ap_val_neighap[1][0], dut->ap_val_neighap[1][1],
			dut->ap_val_neighap[1][2], dut->ap_val_neighap[1][3],
			dut->ap_val_neighap[1][4], dut->ap_val_neighap[1][5],
			dut->ap_val_opchannel[1]);
		fclose(f);
	}

	if (dut->ap_neighap >= 3 && dut->ap_lci == 0) {
		FILE *f;

		f = fopen("/tmp/nbr_report.txt", "a");
		if (!f) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to open /tmp/nbr_report.txt");
			return;
		}

		fprintf(f,
			"ap_3 = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x00 0x3c 0x00 0x00 0x80 0x%x 0x09 0x06 0x03 0x02 0x9b 0x00\n",
			dut->ap_val_neighap[2][0], dut->ap_val_neighap[2][1],
			dut->ap_val_neighap[2][2], dut->ap_val_neighap[2][3],
			dut->ap_val_neighap[2][4], dut->ap_val_neighap[2][5],
			dut->ap_val_opchannel[2]);
		fclose(f);

		f = fopen("/tmp/ftmrr.txt", "a");
		if (!f) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to open /tmp/ftmrr.txt");
			return;
		}

		fprintf(f,
			"ap_3 = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x00 0x3c 0x00 0x00 0x80 0x%x 0x09 0x06 0x03 0x02 0x9b 0x00\n",
			dut->ap_val_neighap[2][0], dut->ap_val_neighap[2][1],
			dut->ap_val_neighap[2][2], dut->ap_val_neighap[2][3],
			dut->ap_val_neighap[2][4], dut->ap_val_neighap[2][5],
			dut->ap_val_opchannel[2]);
		fclose(f);
	}

	if (dut->ap_neighap) {
		snprintf(buf, sizeof(buf), "iwpriv %s enable_rtt 1", ifname);
		run_system(dut, buf);
		snprintf(buf, sizeof(buf), "iwpriv %s enable_lci 1", ifname);
		run_system(dut, buf);
		snprintf(buf, sizeof(buf), "iwpriv %s enable_lcr 1", ifname);
		run_system(dut, buf);
		snprintf(buf, sizeof(buf), "iwpriv %s rrm 1", ifname);
		run_system(dut, buf);
	}

	if (dut->ap_scan == 1) {
		snprintf(buf, sizeof(buf), "iwpriv %s scanentryage 600",
			 ifname);
		run_system(dut, buf);
		snprintf(buf, sizeof(buf), "iwlist %s scan", ifname);
		run_system(dut, buf);
	}

	if (dut->ap_set_bssidpref) {
		snprintf(buf, sizeof(buf),
			 "wifitool %s setbssidpref 00:00:00:00:00:00 0 00 00",
			 ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"wifitool clear bssidpref failed");
		}
	}

	if (dut->wnm_bss_max_feature != VALUE_NOT_SET) {
		int feature_enable;

		feature_enable = dut->wnm_bss_max_feature == VALUE_ENABLED;
		snprintf(buf, sizeof(buf), "iwpriv %s wnm %d",
			 ifname, feature_enable);
		run_system(dut, buf);
		snprintf(buf, sizeof(buf), "iwpriv %s wnm_bss %d",
			 ifname, feature_enable);
		run_system(dut, buf);
		if (feature_enable) {
			const char *extra = "";

			if (dut->wnm_bss_max_protection != VALUE_NOT_SET) {
				if (dut->wnm_bss_max_protection ==
				    VALUE_ENABLED)
					extra = " 1";
				else
					extra = " 0";
			}
			snprintf(buf, sizeof(buf),
				 "wlanconfig %s wnm setbssmax %d%s",
				 ifname, dut->wnm_bss_max_idle_time, extra);
			run_system(dut, buf);
		}
	}

	if (dut->program == PROGRAM_MBO) {
		apply_mbo_pref_ap_list(dut);

		snprintf(buf, sizeof(buf), "iwpriv %s mbo_cel_pref %d",
			 ifname, dut->ap_cell_cap_pref);
		run_system(dut, buf);

		snprintf(buf, sizeof(buf), "iwpriv %s mbocap 0x40", ifname);
		run_system(dut, buf);

		ath_set_assoc_disallow(dut, ifname, "disable");
	}
}


static int cmd_ath_ap_config_commit(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	char buf[100];
	struct stat s;
	const char *ifname = dut->ap_is_dual ? "ath1" : "ath0";

	if (stat("/proc/athversion", &s) == 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Run apdown");
		run_system(dut, "apdown");
	}

	cmd_ath_ap_radio_config(dut);

	snprintf(buf, sizeof(buf), "cfg -a 'AP_SSID=%s'", dut->ap_ssid);
	run_system(dut, buf);

	switch (dut->ap_key_mgmt) {
	case AP_OPEN:
		if (dut->ap_cipher == AP_WEP) {
			run_system(dut, "cfg -a AP_SECMODE=WEP");
			run_system(dut, "cfg -a AP_SECFILE=NONE");
			/* shared auth mode not supported */
			run_system(dut, "cfg -a AP_WEP_MODE_0=1");
			run_system(dut, "cfg -a AP_WEP_MODE_1=1");
			snprintf(buf, sizeof(buf),
				 "cfg -a WEP_RADIO_NUM0_KEY_1=%s",
				 dut->ap_wepkey);
			run_system(dut, buf);
			snprintf(buf, sizeof(buf),
				 "cfg -a WEP_RADIO_NUM1_KEY_1=%s",
				 dut->ap_wepkey);
			run_system(dut, buf);
		} else {
			run_system(dut, "cfg -a AP_SECMODE=None");
		}
		break;
	case AP_WPA2_PSK:
	case AP_WPA2_PSK_MIXED:
	case AP_WPA_PSK:
		case AP_WPA2_SAE:
		case AP_WPA2_PSK_SAE:
		if (dut->ap_key_mgmt == AP_WPA2_PSK ||
		    dut->ap_key_mgmt == AP_WPA2_SAE ||
		    dut->ap_key_mgmt == AP_WPA2_PSK_SAE)
			run_system(dut, "cfg -a AP_WPA=2");
		else if (dut->ap_key_mgmt == AP_WPA2_PSK_MIXED)
			run_system(dut, "cfg -a AP_WPA=3");
		else
			run_system(dut, "cfg -a AP_WPA=1");
		/* TODO: SAE configuration */
		run_system(dut, "cfg -a AP_SECMODE=WPA");
		run_system(dut, "cfg -a AP_SECFILE=PSK");
		snprintf(buf, sizeof(buf), "cfg -a 'PSK_KEY=%s'",
			 dut->ap_passphrase);
		run_system(dut, buf);
		if (dut->ap_cipher == AP_CCMP_TKIP)
			run_system(dut, "cfg -a AP_CYPHER=\"CCMP TKIP\"");
		else if (dut->ap_cipher == AP_TKIP)
			run_system(dut, "cfg -a AP_CYPHER=TKIP");
		else
			run_system(dut, "cfg -a AP_CYPHER=CCMP");
		break;
	case AP_WPA2_EAP:
	case AP_WPA2_EAP_MIXED:
	case AP_WPA_EAP:
		if (dut->ap_key_mgmt == AP_WPA2_EAP)
			run_system(dut, "cfg -a AP_WPA=2");
		else if (dut->ap_key_mgmt == AP_WPA2_EAP_MIXED)
			run_system(dut, "cfg -a AP_WPA=3");
		else
			run_system(dut, "cfg -a AP_WPA=1");
		run_system(dut, "cfg -a AP_SECMODE=WPA");
		run_system(dut, "cfg -a AP_SECFILE=EAP");
		if (dut->ap_cipher == AP_CCMP_TKIP)
			run_system(dut, "cfg -a AP_CYPHER=\"CCMP TKIP\"");
		else if (dut->ap_cipher == AP_TKIP)
			run_system(dut, "cfg -a AP_CYPHER=TKIP");
		else
			run_system(dut, "cfg -a AP_CYPHER=CCMP");
		snprintf(buf, sizeof(buf), "cfg -a AP_AUTH_SERVER=%s",
			 dut->ap_radius_ipaddr);
		run_system(dut, buf);
		snprintf(buf, sizeof(buf), "cfg -a AP_AUTH_PORT=%d",
			 dut->ap_radius_port);
		run_system(dut, buf);
		snprintf(buf, sizeof(buf), "cfg -a AP_AUTH_SECRET=%s",
			 dut->ap_radius_password);
		run_system(dut, buf);
		break;
	}

	if (dut->ap_is_dual) {
		/* ath1 settings in case of dual */
		snprintf(buf, sizeof(buf), "cfg -a 'AP_SSID_2=%s'",
			 dut->ap_ssid);
		run_system(dut, buf);

		switch (dut->ap_key_mgmt) {
		case AP_OPEN:
			if (dut->ap_cipher == AP_WEP) {
				run_system(dut, "cfg -a AP_SECMODE_2=WEP");
				run_system(dut, "cfg -a AP_SECFILE_2=NONE");
				/* shared auth mode not supported */
				run_system(dut, "cfg -a AP_WEP_MODE_0=1");
				run_system(dut, "cfg -a AP_WEP_MODE_1=1");
				snprintf(buf, sizeof(buf),
					 "cfg -a WEP_RADIO_NUM0_KEY_1=%s",
					 dut->ap_wepkey);
				run_system(dut, buf);
				snprintf(buf, sizeof(buf),
					 "cfg -a WEP_RADIO_NUM1_KEY_1=%s",
					 dut->ap_wepkey);
				run_system(dut, buf);
			} else {
				run_system(dut, "cfg -a AP_SECMODE_2=None");
			}
			break;
		case AP_WPA2_PSK:
		case AP_WPA2_PSK_MIXED:
		case AP_WPA_PSK:
		case AP_WPA2_SAE:
		case AP_WPA2_PSK_SAE:
			if (dut->ap_key_mgmt == AP_WPA2_PSK ||
			    dut->ap_key_mgmt == AP_WPA2_SAE ||
			    dut->ap_key_mgmt == AP_WPA2_PSK_SAE)
				run_system(dut, "cfg -a AP_WPA_2=2");
			else if (dut->ap_key_mgmt == AP_WPA2_PSK_MIXED)
				run_system(dut, "cfg -a AP_WPA_2=3");
			else
				run_system(dut, "cfg -a AP_WPA_2=1");
			// run_system(dut, "cfg -a AP_WPA_2=2");
			/* TODO: SAE configuration */
			run_system(dut, "cfg -a AP_SECMODE_2=WPA");
			run_system(dut, "cfg -a AP_SECFILE_2=PSK");
			snprintf(buf, sizeof(buf), "cfg -a 'PSK_KEY_2=%s'",
				 dut->ap_passphrase);
			run_system(dut, buf);
			if (dut->ap_cipher == AP_CCMP_TKIP)
				run_system(dut, "cfg -a AP_CYPHER_2=\"CCMP TKIP\"");
			else if (dut->ap_cipher == AP_TKIP)
				run_system(dut, "cfg -a AP_CYPHER_2=TKIP");
			else
				run_system(dut, "cfg -a AP_CYPHER_2=CCMP");
			break;
		case AP_WPA2_EAP:
		case AP_WPA2_EAP_MIXED:
		case AP_WPA_EAP:
			if (dut->ap_key_mgmt == AP_WPA2_EAP)
				run_system(dut, "cfg -a AP_WPA_2=2");
			else if (dut->ap_key_mgmt == AP_WPA2_EAP_MIXED)
				run_system(dut, "cfg -a AP_WPA_2=3");
			else
				run_system(dut, "cfg -a AP_WPA_2=1");
			run_system(dut, "cfg -a AP_SECMODE_2=WPA");
			run_system(dut, "cfg -a AP_SECFILE_2=EAP");
			if (dut->ap_cipher == AP_CCMP_TKIP)
				run_system(dut, "cfg -a AP_CYPHER_2=\"CCMP TKIP\"");
			else if (dut->ap_cipher == AP_TKIP)
				run_system(dut, "cfg -a AP_CYPHER_2=TKIP");
			else
				run_system(dut, "cfg -a AP_CYPHER_2=CCMP");

			snprintf(buf, sizeof(buf), "cfg -a AP_AUTH_SERVER_2=%s",
				 dut->ap_radius_ipaddr);
			run_system(dut, buf);
			snprintf(buf, sizeof(buf), "cfg -a AP_AUTH_PORT_2=%d",
				 dut->ap_radius_port);
			run_system(dut, buf);
			snprintf(buf, sizeof(buf), "cfg -a AP_AUTH_SECRET_2=%s",
				 dut->ap_radius_password);
			run_system(dut, buf);
			break;
		}

		/* wifi0 settings in case of dual */
		run_system(dut, "cfg -a AP_RADIO_ID=0");
		run_system(dut, "cfg -a AP_PRIMARY_CH=6");
		run_system(dut, "cfg -a AP_STARTMODE=dual");
		run_system(dut, "cfg -a AP_CHMODE=11NGHT40PLUS");
		run_system(dut, "cfg -a TX_CHAINMASK=7");
		run_system(dut, "cfg -a RX_CHAINMASK=7");
	}

	switch (dut->ap_pmf) {
	case AP_PMF_DISABLED:
		snprintf(buf, sizeof(buf), "cfg -a AP_PMF=0");
		run_system(dut, buf);
		break;
	case AP_PMF_OPTIONAL:
		snprintf(buf, sizeof(buf), "cfg -a AP_PMF=1");
		run_system(dut, buf);
		break;
	case AP_PMF_REQUIRED:
		snprintf(buf, sizeof(buf), "cfg -a AP_PMF=2");
		run_system(dut, buf);
		break;
	}
	if (dut->ap_add_sha256) {
		snprintf(buf, sizeof(buf), "cfg -a AP_WPA_SHA256=1");
		run_system(dut, buf);
	} else {
		snprintf(buf, sizeof(buf), "cfg -r AP_WPA_SHA256");
		run_system(dut, buf);
	}

	if (dut->ap_hs2)
		run_system(dut, "cfg -a AP_HOTSPOT=1");
	else
		run_system(dut, "cfg -r AP_HOTSPOT");

	if (dut->ap_interworking) {
		snprintf(buf, sizeof(buf), "cfg -a AP_HOTSPOT_ANT=%d",
			 dut->ap_access_net_type);
		run_system(dut, buf);
		snprintf(buf, sizeof(buf), "cfg -a AP_HOTSPOT_INTERNET=%d",
			 dut->ap_internet);
		run_system(dut, buf);
		snprintf(buf, sizeof(buf), "cfg -a AP_HOTSPOT_VENUEGROUP=%d",
			 dut->ap_venue_group);
		run_system(dut, buf);
		snprintf(buf, sizeof(buf), "cfg -a AP_HOTSPOT_VENUETYPE=%d",
			 dut->ap_venue_type);
		run_system(dut, buf);
		snprintf(buf, sizeof(buf), "cfg -a AP_HOTSPOT_HESSID=%s",
			 dut->ap_hessid);
		run_system(dut, buf);

		if (dut->ap_roaming_cons[0]) {
			char *second, *rc;
			rc = strdup(dut->ap_roaming_cons);
			if (rc == NULL)
				return 0;

			second = strchr(rc, ';');
			if (second)
				*second++ = '\0';

			snprintf(buf, sizeof(buf),
				 "cfg -a AP_HOTSPOT_ROAMINGCONSORTIUM=%s", rc);
			run_system(dut, buf);

			if (second) {
				snprintf(buf, sizeof(buf),
					 "cfg -a AP_HOTSPOT_ROAMINGCONSORTIUM2"
					 "=%s", second);
				run_system(dut, buf);
			}
			free(rc);
		} else {
			run_system(dut, "cfg -r AP_HOTSPOT_ROAMINGCONSORTIUM");
			run_system(dut,
				   "cfg -r AP_HOTSPOT_ROAMINGCONSORTIUM2");
		}
	} else {
		run_system(dut, "cfg -r AP_HOTSPOT_ANT");
		run_system(dut, "cfg -r AP_HOTSPOT_INTERNET");
		run_system(dut, "cfg -r AP_HOTSPOT_VENUEGROUP");
		run_system(dut, "cfg -r AP_HOTSPOT_VENUETYPE");
		run_system(dut, "cfg -r AP_HOTSPOT_HESSID");
		run_system(dut, "cfg -r AP_HOTSPOT_ROAMINGCONSORTIUM");
		run_system(dut, "cfg -r AP_HOTSPOT_ROAMINGCONSORTIUM2");
	}

	if (dut->ap_proxy_arp)
		run_system(dut, "cfg -a IEEE80211V_PROXYARP=1");
	else
		run_system(dut, "cfg -a IEEE80211V_PROXYARP=0");
	if (dut->ap_dgaf_disable)
		run_system(dut, "cfg -a AP_HOTSPOT_DISABLE_DGAF=1");
	else
		run_system(dut, "cfg -r AP_HOTSPOT_DISABLE_DGAF");

	if (strlen(dut->ap_tag_ssid[0])) {
		snprintf(buf, sizeof(buf),
			 "cfg -a AP_SSID_2=%s", dut->ap_tag_ssid[0]);
		run_system(dut, buf);

		if (dut->ap_tag_key_mgmt[0] == AP2_OSEN) {
			run_system(dut, "cfg -a AP_SECMODE_2=WPA");
			run_system(dut, "cfg -a AP_SECFILE_2=OSEN");

			snprintf(buf, sizeof(buf), "cfg -a AP_AUTH_SERVER_2=%s",
				 dut->ap2_radius_ipaddr);
			run_system(dut, buf);

			snprintf(buf, sizeof(buf), "cfg -a AP_AUTH_PORT_2=%d",
				 dut->ap2_radius_port);
			run_system(dut, buf);

			snprintf(buf, sizeof(buf), "cfg -a AP_AUTH_SECRET_2=%s",
				 dut->ap2_radius_password);
			run_system(dut, buf);
		} else {
			run_system(dut, "cfg -a AP_SECMODE_2=None");
			run_system(dut, "cfg -r AP_AUTH_SERVER_2");
			run_system(dut, "cfg -r AP_AUTH_PORT_2");
			run_system(dut, "cfg -r AP_AUTH_SECRET_2");
		}

		run_system(dut, "cfg -a AP_STARTMODE=multi");
	}

	run_system(dut, "cfg -c");

	sigma_dut_print(dut, DUT_MSG_INFO, "Starting AP");
	if (system("apup") != 0) {
		/* to be debugged why apup returns error
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,apup failed");
		return 0;
		*/
	}
	sigma_dut_print(dut, DUT_MSG_INFO, "AP started");

	if (dut->ap_key_mgmt != AP_OPEN) {
		int res;
		/* allow some time for hostapd to start before returning
		 * success */
		usleep(500000);
		if (run_hostapd_cli(dut, "ping") != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to talk to hostapd");
			return 0;
		}

		if (dut->ap_hs2 && !dut->ap_anqpserver) {
			/* the cfg app doesn't like ";" in the variables */
			res = ath_ap_append_hostapd_conf(dut);
			if (res < 0)
				return res;

			/* wait for hostapd to be ready */
			usleep(500000);
			if (run_hostapd_cli(dut, "ping") != 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to talk to "
					  "hostapd");
				return 0;
			}
		}
	}

	ath_ap_set_params(dut);

	if (dut->ap_anqpserver)
		return cmd_ath_ap_anqpserver_start(dut);

	if (dut->ap2_proxy_arp)
		run_system(dut, "iwpriv ath1 proxy_arp 1");

	if (dut->ap_allow_vht_wep || dut->ap_allow_vht_tkip) {
		snprintf(buf, sizeof(buf), "iwpriv %s htweptkip 1", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv htweptkip failed");
		}
	}

	return 1;
}


static int set_ebtables_proxy_arp(struct sigma_dut *dut, const char *chain,
				  const char *ifname)
{
	char buf[200];

	if (!chain || !ifname)
		return -2;

	snprintf(buf, sizeof(buf), "ebtables -P %s ACCEPT", chain);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set ebtables rules, RULE-1, %s",
				chain);
		return -2;
	}

	snprintf(buf, sizeof(buf),
		 "ebtables -A %s -p ARP -d Broadcast -o %s -j DROP",
		 chain, ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set ebtables rules, RULE-2, %s",
				chain);
		return -2;
	}

	snprintf(buf, sizeof(buf),
		 "ebtables -A %s -d Multicast -p IPv6 --ip6-protocol ipv6-icmp --ip6-icmp-type neighbor-solicitation -o %s -j DROP",
		 chain, ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set ebtables rules, RULE-3, %s",
				chain);
		return -2;
	}

	snprintf(buf, sizeof(buf),
		 "ebtables -A %s -d Multicast -p IPv6 --ip6-protocol ipv6-icmp --ip6-icmp-type neighbor-advertisement -o %s -j DROP",
		 chain, ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set ebtables rules, RULE-4, %s",
				chain);
		return -2;
	}

	return 0;
}


static int set_ebtables_disable_dgaf(struct sigma_dut *dut,
				     const char *chain,
				     const char *ifname)
{
	char buf[200];

	if (!chain || !ifname)
		return -2;

	snprintf(buf, sizeof(buf), "ebtables -P %s ACCEPT", chain);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set ebtables rules, RULE-5, %s",
				chain);
		return -2;
	}

	snprintf(buf, sizeof(buf),
		 "ebtables -A %s -p ARP -d Broadcast -o %s -j DROP",
		 chain, ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set ebtables rules, RULE-6, %s",
				chain);
		return -2;
	}

	snprintf(buf, sizeof(buf),
		 "ebtables -A %s -p IPv4 -d Multicast -o %s -j DROP",
		 chain, ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set ebtables rules, RULE-7, %s",
				chain);
		return -2;
	}

	snprintf(buf, sizeof(buf),
		 "ebtables -A %s -p IPv6 -d Multicast -o %s -j DROP",
		 chain, ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set ebtables rules, RULE-8, %s",
				chain);
		return -2;
	}

	return 0;
}


static int check_channel(int channel)
{
	int channel_list[] = { 36, 40, 44, 48, 52, 60, 64, 100, 104, 108, 112,
			       116, 120, 124, 128, 132, 140, 144, 149, 153, 157,
			       161, 165 };
	int num_chan = sizeof(channel_list) / sizeof(int);
	int i;

	for (i = 0; i < num_chan; i++) {
		if (channel == channel_list[i])
			return i;
	}

	return -1;
}


static int get_oper_centr_freq_seq_idx(int chwidth, int channel)
{
	int ch_base;
	int period;

	if (check_channel(channel) < 0)
		return -1;

	if (channel >= 36 && channel <= 64)
		ch_base = 36;
	else if (channel >= 100 && channel <= 144)
		ch_base = 100;
	else
		ch_base = 149;

	period = channel % ch_base * 5 / chwidth;
	return ch_base + period * chwidth / 5 + (chwidth - 20) / 10;
}


static int is_ht40plus_chan(int chan)
{
	return chan == 36 || chan == 44 || chan == 52 || chan == 60 ||
		chan == 100 || chan == 108 || chan == 116 || chan == 124 ||
		chan == 132 || chan == 149 || chan == 157;
}


static int is_ht40minus_chan(int chan)
{
	return chan == 40 || chan == 48 || chan == 56 || chan == 64 ||
		chan == 104 || chan == 112 || chan == 120 || chan == 128 ||
		chan == 136 || chan == 153 || chan == 161;
}


static int get_5g_channel_freq(int chan)
{
	return 5000 + chan * 5;
}


static int cmd_ap_config_commit(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	FILE *f;
	const char *ifname;
	char buf[500];
	char path[100];
	enum driver_type drv;
	const char *key_mgmt;

	drv = get_driver_type();

	if (dut->mode == SIGMA_MODE_STATION) {
		stop_sta_mode(dut);
		sleep(1);
	}

	if (dut->mode == SIGMA_MODE_SNIFFER && dut->sniffer_ifname) {
		snprintf(buf, sizeof(buf), "ifconfig %s down",
			 dut->sniffer_ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to run '%s'", buf);
		}
		snprintf(buf, sizeof(buf), "iw dev %s set type station",
			 dut->sniffer_ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to run '%s'", buf);
		}
	}

	dut->mode = SIGMA_MODE_AP;

	if (drv == DRIVER_ATHEROS)
		return cmd_ath_ap_config_commit(dut, conn, cmd);
	if (drv == DRIVER_WCN)
		return cmd_wcn_ap_config_commit(dut, conn, cmd);
	if (drv == DRIVER_OPENWRT)
		return cmd_owrt_ap_config_commit(dut, conn, cmd);

	f = fopen(SIGMA_TMPDIR "/sigma_dut-ap.conf", "w");
	if (f == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Failed to open sigma_dut-ap.conf",
				__func__);
		return -2;
	}
	switch (dut->ap_mode) {
	case AP_11g:
	case AP_11b:
	case AP_11ng:
		ifname = (drv == DRIVER_MAC80211) ? "wlan0" : "ath0";
		if ((drv == DRIVER_QNXNTO || drv == DRIVER_LINUX_WCN) &&
		    sigma_main_ifname)
			ifname = sigma_main_ifname;
		fprintf(f, "hw_mode=g\n");
		break;
	case AP_11a:
	case AP_11na:
	case AP_11ac:
		if (drv == DRIVER_QNXNTO || drv == DRIVER_LINUX_WCN) {
			if (sigma_main_ifname)
				ifname = sigma_main_ifname;
			else
				ifname = "wlan0";
		} else if (drv == DRIVER_MAC80211) {
			if (if_nametoindex("wlan1") > 0)
				ifname = "wlan1";
			else
				ifname = "wlan0";
		} else {
			ifname = get_main_ifname();
		}
		fprintf(f, "hw_mode=a\n");
		break;
	default:
		fclose(f);
		return -1;
	}
	if (dut->hostapd_ifname)
		ifname = dut->hostapd_ifname;

	if (drv == DRIVER_MAC80211 || drv == DRIVER_LINUX_WCN)
		fprintf(f, "driver=nl80211\n");

	if ((drv == DRIVER_MAC80211 || drv == DRIVER_QNXNTO ||
	     drv == DRIVER_LINUX_WCN) &&
	    (dut->ap_mode == AP_11ng || dut->ap_mode == AP_11na)) {
		int ht40plus = 0, ht40minus = 0, tx_stbc = 0;

		fprintf(f, "ieee80211n=1\n");
		if (dut->ap_mode == AP_11ng &&
		    (dut->ap_chwidth == AP_40 ||
		     (dut->ap_chwidth == AP_AUTO &&
		      dut->default_11ng_ap_chwidth == AP_40))) {
			if (dut->ap_channel >= 1 && dut->ap_channel <= 7)
				ht40plus = 1;
			else if (dut->ap_channel >= 8 && dut->ap_channel <= 11)
				ht40minus = 1;
			fprintf(f, "obss_interval=300\n");
		}

		/* configure ht_capab based on channel width */
		if (dut->ap_mode == AP_11na &&
		    (dut->ap_chwidth == AP_40 ||
		     (dut->ap_chwidth == AP_AUTO &&
		      dut->default_11na_ap_chwidth == AP_40))) {
			if (is_ht40plus_chan(dut->ap_channel))
				ht40plus = 1;
			else if (is_ht40minus_chan(dut->ap_channel))
				ht40minus = 1;
		}

		if (dut->ap_tx_stbc)
			tx_stbc = 1;

		/* Overwrite the ht_capab with offset value if configured */
		if (dut->ap_chwidth == AP_40 &&
		    dut->ap_chwidth_offset == SEC_CH_40ABOVE) {
			ht40plus = 1;
			ht40minus = 0;
		} else if (dut->ap_chwidth == AP_40 &&
			   dut->ap_chwidth_offset == SEC_CH_40BELOW) {
			ht40minus = 1;
			ht40plus = 0;
		}

		fprintf(f, "ht_capab=%s%s%s\n",
			ht40plus ? "[HT40+]" : "",
			ht40minus ? "[HT40-]" : "",
			tx_stbc ? "[TX-STBC]" : "");
	}

	if ((drv == DRIVER_MAC80211 || drv == DRIVER_QNXNTO ||
	     drv == DRIVER_LINUX_WCN) &&
	    dut->ap_mode == AP_11ac) {
		fprintf(f, "ieee80211ac=1\n"
			"ieee80211n=1\n"
			"ht_capab=[HT40+]\n");
	}

	if ((drv == DRIVER_MAC80211 || drv == DRIVER_QNXNTO ||
	     drv == DRIVER_LINUX_WCN) &&
	    (dut->ap_mode == AP_11ac || dut->ap_mode == AP_11na)) {
		if (dut->ap_countrycode[0]) {
			fprintf(f, "country_code=%s\n", dut->ap_countrycode);
			fprintf(f, "ieee80211d=1\n");
			fprintf(f, "ieee80211h=1\n");
		}
	}

	fprintf(f, "interface=%s\n", ifname);
	if (dut->bridge)
		fprintf(f, "bridge=%s\n", dut->bridge);
	fprintf(f, "channel=%d\n", dut->ap_channel);

	if (sigma_hapd_ctrl)
		fprintf(f, "ctrl_interface=%s\n", sigma_hapd_ctrl);
	else
		fprintf(f, "ctrl_interface=/var/run/hostapd\n");

	if (dut->ap_ssid[0])
		fprintf(f, "ssid=%s\n", dut->ap_ssid);
	else
		fprintf(f, "ssid=QCA AP OOB\n");
	if (dut->ap_bcnint)
		fprintf(f, "beacon_int=%d\n", dut->ap_bcnint);

	switch (dut->ap_key_mgmt) {
	case AP_OPEN:
		if (dut->ap_cipher == AP_WEP)
			fprintf(f, "wep_key0=%s\n", dut->ap_wepkey);
		break;
	case AP_WPA2_PSK:
	case AP_WPA2_PSK_MIXED:
	case AP_WPA_PSK:
	case AP_WPA2_SAE:
	case AP_WPA2_PSK_SAE:
		if (dut->ap_key_mgmt == AP_WPA2_PSK ||
		    dut->ap_key_mgmt == AP_WPA2_SAE ||
		    dut->ap_key_mgmt == AP_WPA2_PSK_SAE)
			fprintf(f, "wpa=2\n");
		else if (dut->ap_key_mgmt == AP_WPA2_PSK_MIXED)
			fprintf(f, "wpa=3\n");
		else
			fprintf(f, "wpa=1\n");
		if (dut->ap_key_mgmt == AP_WPA2_SAE)
			key_mgmt = "SAE";
		else if (dut->ap_key_mgmt == AP_WPA2_PSK_SAE)
			key_mgmt = "WPA-PSK SAE";
		else
			key_mgmt = "WPA-PSK";
		switch (dut->ap_pmf) {
		case AP_PMF_DISABLED:
			fprintf(f, "wpa_key_mgmt=%s%s\n", key_mgmt,
				dut->ap_add_sha256 ? " WPA-PSK-SHA256" : "");
			break;
		case AP_PMF_OPTIONAL:
			fprintf(f, "wpa_key_mgmt=%s%s\n", key_mgmt,
				dut->ap_add_sha256 ? " WPA-PSK-SHA256" : "");
			break;
		case AP_PMF_REQUIRED:
			if (dut->ap_key_mgmt == AP_WPA2_SAE)
				key_mgmt = "SAE";
			else if (dut->ap_key_mgmt == AP_WPA2_PSK_SAE)
				key_mgmt = "WPA-PSK-SHA256 SAE";
			else
				key_mgmt = "WPA-PSK-SHA256";
			fprintf(f, "wpa_key_mgmt=%s\n", key_mgmt);
			break;
		}
		if (dut->ap_cipher == AP_CCMP_TKIP)
			fprintf(f, "wpa_pairwise=CCMP TKIP\n");
		else if (dut->ap_cipher == AP_TKIP)
			fprintf(f, "wpa_pairwise=TKIP\n");
		else
			fprintf(f, "wpa_pairwise=CCMP\n");
		fprintf(f, "wpa_passphrase=%s\n", dut->ap_passphrase);
		break;
	case AP_WPA2_EAP:
	case AP_WPA2_EAP_MIXED:
	case AP_WPA_EAP:
		fprintf(f, "ieee8021x=1\n");
		if (dut->ap_key_mgmt == AP_WPA2_EAP)
			fprintf(f, "wpa=2\n");
		else if (dut->ap_key_mgmt == AP_WPA2_EAP_MIXED)
			fprintf(f, "wpa=3\n");
		else
			fprintf(f, "wpa=1\n");
		switch (dut->ap_pmf) {
		case AP_PMF_DISABLED:
			fprintf(f, "wpa_key_mgmt=WPA-EAP%s\n",
				dut->ap_add_sha256 ? " WPA-EAP-SHA256" : "");
			break;
		case AP_PMF_OPTIONAL:
			fprintf(f, "wpa_key_mgmt=WPA-EAP%s\n",
				dut->ap_add_sha256 ? " WPA-EAP-SHA256" : "");
			break;
		case AP_PMF_REQUIRED:
			fprintf(f, "wpa_key_mgmt=WPA-EAP-SHA256\n");
			break;
		}
		if (dut->ap_cipher == AP_CCMP_TKIP)
			fprintf(f, "wpa_pairwise=CCMP TKIP\n");
		else if (dut->ap_cipher == AP_TKIP)
			fprintf(f, "wpa_pairwise=TKIP\n");
		else
			fprintf(f, "wpa_pairwise=CCMP\n");
		fprintf(f, "auth_server_addr=%s\n", dut->ap_radius_ipaddr);
		if (dut->ap_radius_port)
			fprintf(f, "auth_server_port=%d\n",
				dut->ap_radius_port);
		fprintf(f, "auth_server_shared_secret=%s\n",
			dut->ap_radius_password);
		break;
	}

	if (dut->ap_rsn_preauth)
		fprintf(f, "rsn_preauth=1\n");

	switch (dut->ap_pmf) {
	case AP_PMF_DISABLED:
		break;
	case AP_PMF_OPTIONAL:
		fprintf(f, "ieee80211w=1\n");
		break;
	case AP_PMF_REQUIRED:
		fprintf(f, "ieee80211w=2\n");
		break;
	}

	if (dut->rsne_override)
		fprintf(f, "own_ie_override=%s\n", dut->rsne_override);

	if (dut->ap_sae_group)
		fprintf(f, "sae_groups=%d\n", dut->ap_sae_group);

	if (dut->ap_p2p_mgmt)
		fprintf(f, "manage_p2p=1\n");

	if (dut->ap_tdls_prohibit || dut->ap_l2tif)
		fprintf(f, "tdls_prohibit=1\n");
	if (dut->ap_tdls_prohibit_chswitch || dut->ap_l2tif)
		fprintf(f, "tdls_prohibit_chan_switch=1\n");
	if (dut->ap_p2p_cross_connect >= 0) {
		fprintf(f, "manage_p2p=1\n"
			"allow_cross_connection=%d\n",
			dut->ap_p2p_cross_connect);
	}

	if (dut->ap_l2tif || dut->ap_proxy_arp) {
		if (!dut->bridge) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Bridge must be configured. Run with -b <brname>.");
			fclose(f);
			return -2;
		}
		fprintf(f, "ap_isolate=1\n");
	}

	if (dut->ap_proxy_arp)
		fprintf(f, "proxy_arp=1\n");

	if (dut->ap_wme)
		fprintf(f, "wmm_enabled=1\n");

	if (dut->ap_wmmps == AP_WMMPS_ON)
		fprintf(f, "uapsd_advertisement_enabled=1\n");

	if (dut->ap_hs2) {
		if (dut->ap_bss_load) {
			char *bss_load;

			switch (dut->ap_bss_load) {
			case -1:
				bss_load = "bss_load_update_period=10";
				break;
			case 1:
				/* STA count: 1, CU: 50, AAC: 65535 */
				bss_load = "bss_load_test=1:50:65535";
				break;
			case 2:
				/* STA count: 1, CU: 200, AAC: 65535 */
				bss_load = "bss_load_test=1:200:65535";
				break;
			case 3:
				/* STA count: 1, CU: 75, AAC: 65535 */
				bss_load = "bss_load_test=1:75:65535";
				break;
			default:
				bss_load = NULL;
				break;
			}

			if (!bss_load) {
				fclose(f);
				return -2;
			}
			fprintf(f, "%s\n", bss_load);
		}

		if (append_hostapd_conf_hs2(dut, f)) {
			fclose(f);
			return -2;
		}
	}

	if (dut->ap_interworking && append_hostapd_conf_interworking(dut, f)) {
		fclose(f);
		return -2;
	}

	if (dut->ap_hs2 && strlen(dut->ap_tag_ssid[0])) {
		unsigned char bssid[6];
		char ifname2[50];

		if (get_hwaddr(ifname, bssid)) {
			fclose(f);
			return -2;
		}
		bssid[0] |= 0x02;

		snprintf(ifname2, sizeof(ifname2), "%s_1", ifname);
		fprintf(f, "bss=%s_1\n", ifname2);
		fprintf(f, "ssid=%s\n", dut->ap_tag_ssid[0]);
		if (dut->bridge)
			fprintf(f, "bridge=%s\n", dut->bridge);
		fprintf(f, "bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
			bssid[0], bssid[1], bssid[2], bssid[3],
			bssid[4], bssid[5]);

		if (dut->ap_tag_key_mgmt[0] == AP2_OSEN) {
			fprintf(f, "osen=1\n");
			if (strlen(dut->ap2_radius_ipaddr))
				fprintf(f, "auth_server_addr=%s\n",
					dut->ap2_radius_ipaddr);
			if (dut->ap2_radius_port)
				fprintf(f, "auth_server_port=%d\n",
					dut->ap2_radius_port);
			if (strlen(dut->ap2_radius_password))
				fprintf(f, "auth_server_shared_secret=%s\n",
					dut->ap2_radius_password);
		}

		if (dut->ap2_proxy_arp) {
			if (!dut->bridge) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Bridge must be configured. Run with -b <brname>.");
				fclose(f);
				return -2;
			}
			fprintf(f, "ap_isolate=1\n");
			fprintf(f, "proxy_arp=1\n");

			if (set_ebtables_proxy_arp(dut, "FORWARD", ifname2) ||
			    set_ebtables_proxy_arp(dut, "OUTPUT", ifname2)) {
				fclose(f);
				return -2;
			}

		}
	}

	if (dut->program == PROGRAM_WPS) {
		fprintf(f, "eap_server=1\n"
			"wps_state=1\n"
			"device_name=QCA AP\n"
			"manufacturer=QCA\n"
			"device_type=6-0050F204-1\n"
			"config_methods=label virtual_display "
			"virtual_push_button keypad%s\n"
			"ap_pin=12345670\n"
			"friendly_name=QCA Access Point\n"
			"upnp_iface=%s\n",
			dut->ap_wpsnfc ? " nfc_interface ext_nfc_token" : "",
			dut->bridge ? dut->bridge : ifname);
	}

	if (dut->program == PROGRAM_VHT) {
		int vht_oper_centr_freq_idx;

		if (check_channel(dut->ap_channel) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid channel");
			fclose(f);
			return 0;
		}

		switch (dut->ap_chwidth) {
		case AP_20:
			dut->ap_vht_chwidth = AP_20_40_VHT_OPER_CHWIDTH;
			vht_oper_centr_freq_idx =
				get_oper_centr_freq_seq_idx(20,
							    dut->ap_channel);
			break;
		case AP_40:
			dut->ap_vht_chwidth = AP_20_40_VHT_OPER_CHWIDTH;
			vht_oper_centr_freq_idx =
				get_oper_centr_freq_seq_idx(40,
							    dut->ap_channel);
			break;
		case AP_80:
			dut->ap_vht_chwidth = AP_80_VHT_OPER_CHWIDTH;
			vht_oper_centr_freq_idx =
				get_oper_centr_freq_seq_idx(80,
							    dut->ap_channel);
			break;
		case AP_160:
			dut->ap_vht_chwidth = AP_160_VHT_OPER_CHWIDTH;
			vht_oper_centr_freq_idx =
				get_oper_centr_freq_seq_idx(160,
							    dut->ap_channel);
			break;
		default:
			dut->ap_vht_chwidth = VHT_DEFAULT_OPER_CHWIDTH;
			vht_oper_centr_freq_idx =
				get_oper_centr_freq_seq_idx(80,
							    dut->ap_channel);
			break;
		}
		fprintf(f, "vht_oper_centr_freq_seg0_idx=%d\n",
			vht_oper_centr_freq_idx);
		fprintf(f, "vht_oper_chwidth=%d\n", dut->ap_vht_chwidth);

		if (dut->ap_sgi80 || dut->ap_txBF ||
		    dut->ap_ldpc != VALUE_NOT_SET ||
		    dut->ap_tx_stbc || dut->ap_mu_txBF) {
			fprintf(f, "vht_capab=%s%s%s%s%s\n",
				dut->ap_sgi80 ? "[SHORT-GI-80]" : "",
				dut->ap_txBF ?
				"[SU-BEAMFORMER][SU-BEAMFORMEE][BF-ANTENNA-2][SOUNDING-DIMENSION-2]" : "",
				(dut->ap_ldpc == VALUE_ENABLED) ?
				"[RXLDPC]" : "",
				dut->ap_tx_stbc ? "[TX-STBC-2BY1]" : "",
				dut->ap_mu_txBF ? "[MU-BEAMFORMER]" : "");
		}
	}

	fclose(f);
	if (dut->use_hostapd_pid_file)
		kill_hostapd_process_pid(dut);
#ifdef __QNXNTO__
	if (system("slay hostapd") == 0)
#else /* __QNXNTO__ */
	if (!dut->use_hostapd_pid_file &&
	    (kill_process(dut, "(hostapd)", 1, SIGTERM) == 0 ||
	     system("killall hostapd") == 0))
#endif /* __QNXNTO__ */
	{
		int i;
		/* Wait some time to allow hostapd to complete cleanup before
		 * starting a new process */
		for (i = 0; i < 10; i++) {
			usleep(500000);
#ifdef __QNXNTO__
			if (system("pidin | grep hostapd") != 0)
				break;
#else /* __QNXNTO__ */
			if (system("pidof hostapd") != 0)
				break;
#endif /* __QNXNTO__ */
		}
	}

#ifdef ANDROID
	/* Set proper conf file permissions so that hostapd process
	 * can access it.
	 */
	if (chmod(SIGMA_TMPDIR "/sigma_dut-ap.conf",
		  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) < 0)
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Error changing permissions");

	if (chown(SIGMA_TMPDIR "/sigma_dut-ap.conf", -1, AID_WIFI) < 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "Error changing groupid");
#endif /* ANDROID */

	if (drv == DRIVER_QNXNTO) {
		snprintf(buf, sizeof(buf),
			 "hostapd -B %s%s %s%s" SIGMA_TMPDIR
			 "/sigma_dut-ap.conf",
			 dut->hostapd_debug_log ? "-ddKt -f " : "",
			 dut->hostapd_debug_log ? dut->hostapd_debug_log : "",
			 dut->hostapd_entropy_log ? " -e" : "",
			 dut->hostapd_entropy_log ? dut->hostapd_entropy_log :
			 "");
	} else {
		/*
		 * It looks like a monitor interface can cause some issues for
		 * beaconing, so remove it (if injection was used) before
		 * starting hostapd.
		 */
		if (if_nametoindex("sigmadut") > 0 &&
		    system("iw dev sigmadut del") != 0)
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to remove "
					"monitor interface");

		snprintf(path, sizeof(path), "%shostapd",
			 file_exists("hostapd") ? "./" : "");
		snprintf(buf, sizeof(buf), "%s -B%s%s%s%s%s " SIGMA_TMPDIR
			 "/sigma_dut-ap.conf",
			 dut->hostapd_bin ? dut->hostapd_bin : path,
			 dut->hostapd_debug_log ? " -ddKt -f" : "",
			 dut->hostapd_debug_log ? dut->hostapd_debug_log : "",
			 dut->hostapd_entropy_log ? " -e" : "",
			 dut->hostapd_entropy_log ? dut->hostapd_entropy_log :
			 "",
			 dut->use_hostapd_pid_file ?
			 " -P " SIGMA_DUT_HOSTAPD_PID_FILE : "");
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "hostapd command: %s", buf);
	if (system(buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to start hostapd");
		return 0;
	}

	/* allow some time for hostapd to start before returning success */
	usleep(500000);
	if (run_hostapd_cli(dut, "ping") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to talk to hostapd");
		return 0;
	}

	if (drv == DRIVER_LINUX_WCN) {
		sigma_dut_print(dut, DUT_MSG_INFO, "setting ip addr %s mask %s",
				ap_inet_addr, ap_inet_mask);
		snprintf(buf, sizeof(buf), "ifconfig %s %s netmask %s up",
			 ifname, ap_inet_addr, ap_inet_mask);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to initialize the interface");
			return -1;
		}
	}

	if (dut->ap_l2tif) {
		snprintf(path, sizeof(path),
			 "/sys/class/net/%s/brport/hairpin_mode",
			 ifname);
		if (!file_exists(path)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s must be binded to the bridge for L2TIF",
					ifname);
			return -2;
		}

		snprintf(buf, sizeof(buf), "echo 1 > %s", path);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to enable hairpin_mode for L2TIF");
			return -2;
		}

		snprintf(buf, sizeof(buf), "ebtables -P FORWARD ACCEPT");
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set ebtables rules, RULE-9");
			return -2;
		}

		snprintf(buf, sizeof(buf),
			 "ebtables -A FORWARD -p IPv4 --ip-proto icmp -i %s -j DROP",
			 ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set ebtables rules, RULE-11");
			return -2;
		}
	}

	if (dut->ap_proxy_arp) {
		if (dut->ap_dgaf_disable) {
			if (set_ebtables_disable_dgaf(dut, "FORWARD", ifname) ||
			    set_ebtables_disable_dgaf(dut, "OUTPUT", ifname))
				return -2;
		} else {
			if (set_ebtables_proxy_arp(dut, "FORWARD", ifname) ||
			    set_ebtables_proxy_arp(dut, "OUTPUT", ifname))
				return -2;
		}

		/* For 4.5-(c) */
		snprintf(buf, sizeof(buf),
			 "ebtables -A FORWARD -p ARP --arp-opcode 2 -i %s -j DROP",
			 ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set ebtables rules, RULE-10");
			return -2;
		}
	}

	if (dut->ap_tdls_prohibit || dut->ap_l2tif) {
		/* Drop TDLS frames */
		snprintf(buf, sizeof(buf),
			 "ebtables -A FORWARD -p 0x890d -i %s -j DROP", ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set ebtables rules, RULE-13");
			return -2;
		}
	}

	if (dut->ap_fake_pkhash &&
	    run_hostapd_cli(dut, "set wps_corrupt_pkhash 1") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Could not enable FakePubKey");
		return 0;
	}

	return 1;
}


static int parse_qos_params(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct qos_params *qos, const char *cwmin,
			    const char *cwmax, const char *aifs,
			    const char *txop, const char *acm)
{
	int val;

	if (cwmin) {
		qos->ac = 1;
		val = atoi(cwmin);
		if (val < 0 || val > 15) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid cwMin");
			return 0;
		}
		qos->cwmin = val;
	}

	if (cwmax) {
		qos->ac = 1;
		val = atoi(cwmax);
		if (val < 0 || val > 15) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid cwMax");
			return 0;
		}
		qos->cwmax = val;
	}

	if (aifs) {
		qos->ac = 1;
		val = atoi(aifs);
		if (val < 1 || val > 255) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid AIFS");
			return 0;
		}
		qos->aifs = val;
	}

	if (txop) {
		qos->ac = 1;
		val = atoi(txop);
		if (val < 0 || val > 0xffff) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid txop");
			return 0;
		}
		qos->txop = val * 32;
	}

	if (acm) {
		qos->ac = 1;
		qos->acm = strcasecmp(acm, "on") == 0;
	}

	return 1;
}


static int cmd_ap_set_apqos(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	/* TXOP: The values provided here for VHT5G only */
	if (!parse_qos_params(dut, conn, &dut->ap_qos[AP_AC_VO],
			      get_param(cmd, "cwmin_VO"),
			      get_param(cmd, "cwmax_VO"),
			      get_param(cmd, "AIFS_VO"),
			      get_param(cmd, "TXOP_VO"),
			      get_param(cmd, "ACM_VO")) ||
	    !parse_qos_params(dut, conn, &dut->ap_qos[AP_AC_VI],
			      get_param(cmd, "cwmin_VI"),
			      get_param(cmd, "cwmax_VI"),
			      get_param(cmd, "AIFS_VI"),
			      get_param(cmd, "TXOP_VI"),
			      get_param(cmd, "ACM_VI")) ||
	    !parse_qos_params(dut, conn, &dut->ap_qos[AP_AC_BE],
			      get_param(cmd, "cwmin_BE"),
			      get_param(cmd, "cwmax_BE"),
			      get_param(cmd, "AIFS_BE"),
			      get_param(cmd, "TXOP_BE"),
			      get_param(cmd, "ACM_BE")) ||
	    !parse_qos_params(dut, conn, &dut->ap_qos[AP_AC_BK],
			      get_param(cmd, "cwmin_BK"),
			      get_param(cmd, "cwmax_BK"),
			      get_param(cmd, "AIFS_BK"),
			      get_param(cmd, "TXOP_BK"),
			      get_param(cmd, "ACM_BK")))
		return 0;

	return 1;
}


static int cmd_ap_set_staqos(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	if (!parse_qos_params(dut, conn, &dut->ap_sta_qos[AP_AC_VO],
			      get_param(cmd, "cwmin_VO"),
			      get_param(cmd, "cwmax_VO"),
			      get_param(cmd, "AIFS_VO"),
			      get_param(cmd, "TXOP_VO"),
			      get_param(cmd, "ACM_VO")) ||
	    !parse_qos_params(dut, conn, &dut->ap_sta_qos[AP_AC_VI],
			      get_param(cmd, "cwmin_VI"),
			      get_param(cmd, "cwmax_VI"),
			      get_param(cmd, "AIFS_VI"),
			      get_param(cmd, "TXOP_VI"),
			      get_param(cmd, "ACM_VI")) ||
	    !parse_qos_params(dut, conn, &dut->ap_sta_qos[AP_AC_BE],
			      get_param(cmd, "cwmin_BE"),
			      get_param(cmd, "cwmax_BE"),
			      get_param(cmd, "AIFS_BE"),
			      get_param(cmd, "TXOP_BE"),
			      get_param(cmd, "ACM_BE")) ||
	    !parse_qos_params(dut, conn, &dut->ap_sta_qos[AP_AC_BK],
			      get_param(cmd, "cwmin_BK"),
			      get_param(cmd, "cwmax_BK"),
			      get_param(cmd, "AIFS_BK"),
			      get_param(cmd, "TXOP_BK"),
			      get_param(cmd, "ACM_BK")))
		return 0;

	return 1;
}


static void cmd_ath_ap_hs2_reset(struct sigma_dut *dut)
{
	unsigned char bssid[6];
	char buf[100];
	run_system(dut, "cfg -a AP_SSID=\"Hotspot 2.0\"");
	run_system(dut, "cfg -a AP_PRIMARY_CH=1");
	run_system(dut, "cfg -a AP_SECMODE=WPA");
	run_system(dut, "cfg -a AP_SECFILE=EAP");
	run_system(dut, "cfg -a AP_WPA=2");
	run_system(dut, "cfg -a AP_CYPHER=CCMP");
	run_system(dut, "cfg -a AP_HOTSPOT=1");
	run_system(dut, "cfg -a AP_HOTSPOT_ANT=2");
	run_system(dut, "cfg -a AP_HOTSPOT_INTERNET=0");
	run_system(dut, "cfg -a AP_HOTSPOT_VENUEGROUP=2");
	run_system(dut, "cfg -a AP_HOTSPOT_VENUETYPE=8");
	run_system(dut, "cfg -a AP_HOTSPOT_ROAMINGCONSORTIUM=506f9a");
	run_system(dut, "cfg -a AP_HOTSPOT_ROAMINGCONSORTIUM2=001bc504bd");
	if (!get_hwaddr("ath0", bssid)) {
		snprintf(buf, sizeof(buf), "cfg -a AP_HOTSPOT_HESSID="
			"%02x:%02x:%02x:%02x:%02x:%02x",
			bssid[0], bssid[1], bssid[2], bssid[3],
			bssid[4], bssid[5]);
		run_system(dut, buf);
		snprintf(dut->ap_hessid, sizeof(dut->ap_hessid),
			"%02x:%02x:%02x:%02x:%02x:%02x",
			bssid[0], bssid[1], bssid[2], bssid[3],
			bssid[4], bssid[5]);
	} else {
		if (!get_hwaddr("wifi0", bssid)) {
			snprintf(buf, sizeof(buf), "cfg -a AP_HOTSPOT_HESSID="
				"%02x:%02x:%02x:%02x:%02x:%02x",
				bssid[0], bssid[1], bssid[2], bssid[3],
				bssid[4], bssid[5]);
			run_system(dut, buf);
			snprintf(dut->ap_hessid, sizeof(dut->ap_hessid),
				"%02x:%02x:%02x:%02x:%02x:%02x",
				bssid[0], bssid[1], bssid[2], bssid[3],
				bssid[4], bssid[5]);
		} else {
			/* load the driver and try again */
			run_system(dut, "/etc/rc.d/rc.wlan up");

			if (!get_hwaddr("wifi0", bssid)) {
				snprintf(buf, sizeof(buf),
					 "cfg -a AP_HOTSPOT_HESSID="
					 "%02x:%02x:%02x:%02x:%02x:%02x",
					 bssid[0], bssid[1], bssid[2],
					 bssid[3], bssid[4], bssid[5]);
				run_system(dut, buf);
				snprintf(dut->ap_hessid,
					 sizeof(dut->ap_hessid),
					 "%02x:%02x:%02x:%02x:%02x:%02x",
					 bssid[0], bssid[1], bssid[2],
					 bssid[3], bssid[4], bssid[5]);
			}
		}
	}

	run_system(dut, "cfg -r AP_SSID_2");
	run_system(dut, "cfg -c");
	/* run_system(dut, "cfg -s"); */
}


static void ath_reset_vht_defaults(struct sigma_dut *dut)
{
	run_system(dut, "cfg -x");
	run_system(dut, "cfg -a AP_RADIO_ID=1");
	run_system(dut, "cfg -a AP_PRIMARY_CH_2=36");
	run_system(dut, "cfg -a AP_STARTMODE=standard");
	run_system(dut, "cfg -a AP_CHMODE_2=11ACVHT80");
	run_system(dut, "cfg -a TX_CHAINMASK_2=7");
	run_system(dut, "cfg -a RX_CHAINMASK_2=7");
	run_system(dut, "cfg -a ATH_countrycode=0x348");
	/* NOTE: For Beeliner we have to turn off MU-MIMO */
	if (system("rm /tmp/secath*") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to remove secath file");
	}
}


static int cmd_ap_reset_default(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *type;
	enum driver_type drv;
	int i;

	for (i = 0; i < MAX_WLAN_TAGS - 1; i++) {
		/*
		 * Reset all tagged SSIDs to NULL-string and all key management
		 * to open.
		 */
		dut->ap_tag_ssid[i][0] = '\0';
		dut->ap_tag_key_mgmt[i] = AP2_OPEN;
	}

	drv = get_driver_type();
	dut->program = sigma_program_to_enum(get_param(cmd, "PROGRAM"));
	dut->device_type = AP_unknown;
	type = get_param(cmd, "type");
	if (type && strcasecmp(type, "Testbed") == 0)
		dut->device_type = AP_testbed;
	if (type && strcasecmp(type, "DUT") == 0)
		dut->device_type = AP_dut;

	dut->ap_rts = 0;
	dut->ap_frgmnt = 0;
	dut->ap_bcnint = 0;
	dut->ap_key_mgmt = AP_OPEN;
	dut->ap_ssid[0] = '\0';
	dut->ap_fake_pkhash = 0;
	memset(dut->ap_qos, 0, sizeof(dut->ap_qos));
	memset(dut->ap_sta_qos, 0, sizeof(dut->ap_sta_qos));
	dut->ap_addba_reject = VALUE_NOT_SET;
	dut->ap_noack = VALUE_NOT_SET;
	dut->ap_is_dual = 0;
	dut->ap_mode = AP_inval;
	dut->ap_mode_1 = AP_inval;

	dut->ap_allow_vht_wep = 0;
	dut->ap_allow_vht_tkip = 0;
	dut->ap_disable_protection = 0;
	memset(dut->ap_countrycode, 0, sizeof(dut->ap_countrycode));
	dut->ap_dyn_bw_sig = VALUE_NOT_SET;
	dut->ap_ldpc = VALUE_NOT_SET;
	dut->ap_sig_rts = VALUE_NOT_SET;
	dut->ap_rx_amsdu = VALUE_NOT_SET;
	dut->ap_txBF = 0;
	dut->ap_mu_txBF = 0;
	dut->ap_chwidth = AP_AUTO;

	dut->ap_rsn_preauth = 0;
	dut->ap_wpsnfc = 0;
	dut->ap_bss_load = -1;
	dut->ap_p2p_cross_connect = -1;

	dut->ap_regulatory_mode = AP_80211D_MODE_DISABLED;
	dut->ap_dfs_mode = AP_DFS_MODE_DISABLED;
	dut->ap_chwidth_offset = SEC_CH_NO;

	dut->mbo_pref_ap_cnt = 0;
	dut->ft_bss_mac_cnt = 0;

	if (dut->program == PROGRAM_HT || dut->program == PROGRAM_VHT) {
		dut->ap_wme = AP_WME_ON;
		dut->ap_wmmps = AP_WMMPS_ON;
	} else {
		dut->ap_wme = AP_WME_OFF;
		dut->ap_wmmps = AP_WMMPS_OFF;
	}

	if (dut->program == PROGRAM_HS2 || dut->program == PROGRAM_HS2_R2 ||
	    dut->program == PROGRAM_IOTLP) {
		int i;

		if (drv == DRIVER_ATHEROS)
			cmd_ath_ap_hs2_reset(dut);
		else if (drv == DRIVER_OPENWRT)
			cmd_owrt_ap_hs2_reset(dut);

		dut->ap_interworking = 1;
		dut->ap_access_net_type = 2;
		dut->ap_internet = 0;
		dut->ap_venue_group = 2;
		dut->ap_venue_type = 8;
		dut->ap_domain_name_list[0] = '\0';
		dut->ap_hs2 = 1;
		snprintf(dut->ap_roaming_cons, sizeof(dut->ap_roaming_cons),
			 "506f9a;001bc504bd");
		dut->ap_l2tif = 0;
		dut->ap_proxy_arp = 0;
		if (dut->bridge) {
			char buf[50];

			snprintf(buf, sizeof(buf), "ip neigh flush dev %s",
				 dut->bridge);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"%s ip neigh table flushing failed",
						dut->bridge);
			}

			snprintf(buf, sizeof(buf), "ebtables -F");
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"%s ebtables flushing failed",
						dut->bridge);
			}
		}
		dut->ap_dgaf_disable = 0;
		dut->ap_p2p_cross_connect = 0;
		dut->ap_gas_cb_delay = 0;
		dut->ap_nai_realm_list = 0;
		dut->ap_oper_name = 0;
		dut->ap_venue_name = 0;
		for (i = 0; i < 10; i++) {
			dut->ap_plmn_mcc[i][0] = '\0';
			dut->ap_plmn_mnc[i][0] = '\0';
		}
		dut->ap_wan_metrics = 0;
		dut->ap_conn_capab = 0;
		dut->ap_ip_addr_type_avail = 0;
		dut->ap_net_auth_type = 0;
		dut->ap_oper_class = 0;
		dut->ap_pmf = 0;
		dut->ap_add_sha256 = 0;
	}

	if (dut->program == PROGRAM_HS2_R2 || dut->program == PROGRAM_IOTLP) {
		int i;
		const char hessid[] = "50:6f:9a:00:11:22";

		memcpy(dut->ap_hessid, hessid, strlen(hessid) + 1);
		dut->ap_osu_ssid[0] = '\0';
		dut->ap_pmf = 1;
		dut->ap_osu_provider_list = 0;
		for (i = 0; i < 10; i++) {
			dut->ap_osu_server_uri[i][0] = '\0';
			dut->ap_osu_method[i] = 0xFF;
		}
		dut->ap_qos_map_set = 0;
		dut->ap_tag_key_mgmt[0] = AP2_OPEN;
		dut->ap2_proxy_arp = 0;
		dut->ap_osu_icon_tag = 0;
	}

	if (dut->program == PROGRAM_VHT) {
		/* Set up the defaults */
		dut->ap_mode = AP_11ac;
		dut->ap_channel = 36;
		dut->ap_ampdu = VALUE_NOT_SET;
		dut->ap_ndpa_frame = 1;
		dut->ap_txBF = 1;
		if (dut->device_type == AP_testbed) {
			dut->ap_amsdu = VALUE_DISABLED;
			dut->ap_ldpc = VALUE_DISABLED;
			dut->ap_rx_amsdu = VALUE_DISABLED;
			dut->ap_sgi80 = 0;
			dut->ap_txBF = 2;
			dut->ap_rx_stbc = 1;
			dut->ap_opmode_notify = 1;
			dut->ap_txBF = 1;
		} else {
			dut->ap_amsdu = VALUE_ENABLED;
			/*
			 * As LDPC is optional, don't enable this by default
			 * for LINUX-WCN driver. The ap_set_wireless command
			 * can be used to enable LDPC, when needed.
			 */
			if (drv != DRIVER_LINUX_WCN)
				dut->ap_ldpc = VALUE_ENABLED;
			dut->ap_rx_amsdu = VALUE_ENABLED;
			dut->ap_sgi80 = 1;
		}
		dut->ap_fixed_rate = 0;
		dut->ap_rx_streams = 3;
		dut->ap_tx_streams = 3;
		dut->ap_vhtmcs_map = 0;
		dut->ap_chwidth = AP_80;
		dut->ap_tx_stbc = 1;
		dut->ap_dyn_bw_sig = VALUE_ENABLED;
		if (get_openwrt_driver_type() == OPENWRT_DRIVER_ATHEROS)
			dut->ap_dfs_mode = AP_DFS_MODE_ENABLED;
		if (get_driver_type() == DRIVER_ATHEROS)
			ath_reset_vht_defaults(dut);
	}

	if (dut->program == PROGRAM_IOTLP) {
		dut->wnm_bss_max_feature = VALUE_DISABLED;
		dut->wnm_bss_max_idle_time = 0;
		dut->wnm_bss_max_protection = VALUE_NOT_SET;
		dut->ap_proxy_arp = 1;
	} else {
		/*
		 * Do not touch the BSS-MAX Idle time feature
		 * if the program is not IOTLP.
		 */
		dut->wnm_bss_max_feature = VALUE_NOT_SET;
		dut->wnm_bss_max_idle_time = 0;
		dut->wnm_bss_max_protection = VALUE_NOT_SET;
	}

	if (dut->program == PROGRAM_LOC) {
		dut->ap_rrm = 1;
		dut->ap_rtt = 1;
		dut->ap_lci = 0;
		dut->ap_val_lci[0] = '\0';
		dut->ap_infoz[0] = '\0';
		dut->ap_lcr = 0;
		dut->ap_val_lcr[0] = '\0';
		dut->ap_neighap = 0;
		dut->ap_opchannel = 0;
		dut->ap_scan = 0;
		dut->ap_fqdn_held = 0;
		dut->ap_fqdn_supl = 0;
		dut->ap_interworking = 0;
		dut->ap_gas_cb_delay = 0;
		dut->ap_msnt_type = 0;
	}
	dut->ap_ft_oa = 0;
	dut->ap_reg_domain = REG_DOMAIN_NOT_SET;
	dut->ap_mobility_domain[0] = '\0';

	if (dut->program == PROGRAM_MBO) {
		dut->ap_mbo = 1;
		dut->ap_interworking = 1;
		dut->ap_ne_class = 0;
		dut->ap_ne_op_ch = 0;
		dut->ap_set_bssidpref = 1;
		dut->ap_btmreq_disassoc_imnt = 0;
		dut->ap_btmreq_term_bit = 0;
		dut->ap_disassoc_timer = 0;
		dut->ap_btmreq_bss_term_dur = 0;
		dut->ap_channel = 36;
		dut->ap_chwidth = AP_20;
		dut->ap_cell_cap_pref = 0;
		dut->ap_gas_cb_delay = 0;
		dut->mbo_self_ap_tuple.ap_ne_class = -1;
		dut->mbo_self_ap_tuple.ap_ne_pref = -1; /* Not set */
		dut->mbo_self_ap_tuple.ap_ne_op_ch = -1;

	}

	free(dut->rsne_override);
	dut->rsne_override = NULL;

	dut->ap_sae_group = 0;

	if (dut->use_hostapd_pid_file) {
		kill_hostapd_process_pid(dut);
	} else if (kill_process(dut, "(hostapd)", 1, SIGTERM) == 0 ||
		   system("killall hostapd") == 0) {
		int i;
		/* Wait some time to allow hostapd to complete cleanup before
		 * starting a new process */
		for (i = 0; i < 10; i++) {
			usleep(500000);
			if (system("pidof hostapd") != 0)
				break;
		}
	}

	if (if_nametoindex("sigmadut") > 0 &&
	    system("iw dev sigmadut del") != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to remove "
				"monitor interface");

	return 1;
}


static int cmd_ap_get_info(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	struct stat s;
	char resp[200];
	FILE *f;
	enum driver_type drv = get_driver_type();

	switch (drv) {
	case DRIVER_ATHEROS: {
		/* Atheros AP */
		struct utsname uts;
		char *version, athver[100];

		if (stat("/proc/athversion", &s) != 0) {
			if (system("/etc/rc.d/rc.wlan up") != 0) {
			}
		}

		athver[0] = '\0';
		f = fopen("/proc/athversion", "r");
		if (f) {
			if (fgets(athver, sizeof(athver), f)) {
				char *pos = strchr(athver, '\n');
				if (pos)
					*pos = '\0';
			}
			fclose(f);
		}

		if (uname(&uts) == 0)
			version = uts.release;
		else
			version = "Unknown";

		if (if_nametoindex("ath1") > 0)
			snprintf(resp, sizeof(resp), "interface,ath0_24G "
				 "ath1_5G,agent,1.0,version,%s/drv:%s",
				 version, athver);
		else
			snprintf(resp, sizeof(resp), "interface,ath0_24G,"
				 "agent,1.0,version,%s/drv:%s",
				 version, athver);

		send_resp(dut, conn, SIGMA_COMPLETE, resp);
		return 0;
	}
	case DRIVER_LINUX_WCN:
	case DRIVER_MAC80211: {
		struct utsname uts;
		char *version;

		if (uname(&uts) == 0)
			version = uts.release;
		else
			version = "Unknown";

		if (if_nametoindex("wlan1") > 0)
			snprintf(resp, sizeof(resp), "interface,wlan0_24G "
				 "wlan1_5G,agent,1.0,version,%s", version);
		else
			snprintf(resp, sizeof(resp), "interface,wlan0_any,"
				 "agent,1.0,version,%s", version);

		send_resp(dut, conn, SIGMA_COMPLETE, resp);
		return 0;
	}
	case DRIVER_QNXNTO: {
		struct utsname uts;
		char *version;

		if (uname(&uts) == 0)
			version = uts.release;
		else
			version = "Unknown";
		snprintf(resp, sizeof(resp),
			 "interface,%s_any,agent,1.0,version,%s",
			 sigma_main_ifname ? sigma_main_ifname : "NA",
			 version);
		send_resp(dut, conn, SIGMA_COMPLETE, resp);
		return 0;
	}
	case DRIVER_OPENWRT: {
		switch (get_openwrt_driver_type()) {
		case OPENWRT_DRIVER_ATHEROS: {
			struct utsname uts;
			char *version;

			if (uname(&uts) == 0)
				version = uts.release;
			else
				version = "Unknown";

			if (if_nametoindex("ath1") > 0)
				snprintf(resp, sizeof(resp),
					 "interface,ath0_5G ath1_24G,agent,1.0,version,%s",
					 version);
			else
				snprintf(resp, sizeof(resp),
					 "interface,ath0_any,agent,1.0,version,%s",
					 version);

			send_resp(dut, conn, SIGMA_COMPLETE, resp);
			return 0;
		}
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported openwrt driver");
			return 0;
		}
	}
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported driver");
		return 0;
	}
}


static int cmd_ap_deauth_sta(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	/* const char *ifname = get_param(cmd, "INTERFACE"); */
	const char *val;
	char buf[100];

	val = get_param(cmd, "MinorCode");
	if (val) {
		/* TODO: add support for P2P minor code */
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,MinorCode not "
			  "yet supported");
		return 0;
	}

	val = get_param(cmd, "STA_MAC_ADDRESS");
	if (val == NULL)
		return -1;
	snprintf(buf, sizeof(buf), "deauth %s", val);
	if (run_hostapd_cli(dut, buf) != 0)
		return -2;

	return 1;
}


#ifdef __linux__
int inject_frame(int s, const void *data, size_t len, int encrypt);
int open_monitor(const char *ifname);
int hwaddr_aton(const char *txt, unsigned char *addr);
#endif /* __linux__ */

enum send_frame_type {
		DISASSOC, DEAUTH, SAQUERY
};
enum send_frame_protection {
	CORRECT_KEY, INCORRECT_KEY, UNPROTECTED
};


static int ap_inject_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			   enum send_frame_type frame,
			   enum send_frame_protection protected,
			   const char *sta_addr)
{
#ifdef __linux__
	unsigned char buf[1000], *pos;
	int s, res;
	unsigned char addr_sta[6], addr_own[6];
	char *ifname;
	char cbuf[100];
	struct ifreq ifr;

	if ((dut->ap_mode == AP_11a || dut->ap_mode == AP_11na ||
	     dut->ap_mode == AP_11ac) &&
	    if_nametoindex("wlan1") > 0)
		ifname = "wlan1";
	else
		ifname = "wlan0";

	if (hwaddr_aton(sta_addr, addr_sta) < 0)
		return -1;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return -1;
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0) {
		perror("ioctl");
		close(s);
		return -1;
	}
	close(s);
	memcpy(addr_own, ifr.ifr_hwaddr.sa_data, 6);

	if (if_nametoindex("sigmadut") == 0) {
		snprintf(cbuf, sizeof(cbuf),
			 "iw dev %s interface add sigmadut type monitor",
			 ifname);
		if (system(cbuf) != 0 ||
		    if_nametoindex("sigmadut") == 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to add "
					"monitor interface with '%s'", cbuf);
			return -2;
		}
	}

	if (system("ifconfig sigmadut up") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to set "
				"monitor interface up");
		return -2;
	}

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
	}

	if (protected == INCORRECT_KEY)
		*pos++ = 0x40; /* Set Protected field to 1 */
	else
		*pos++ = 0x00;

	/* Duration */
	*pos++ = 0x00;
	*pos++ = 0x00;

	/* addr1 = DA (station) */
	memcpy(pos, addr_sta, 6);
	pos += 6;
	/* addr2 = SA (own address) */
	memcpy(pos, addr_own, 6);
	pos += 6;
	/* addr3 = BSSID (own address) */
	memcpy(pos, addr_own, 6);
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
	send_resp(dut, conn, SIGMA_ERROR, "errorCode,ap_send_frame not "
		  "yet supported");
	return 0;
#endif /* __linux__ */
}


int ap_send_frame_hs2(struct sigma_dut *dut, struct sigma_conn *conn,
		      struct sigma_cmd *cmd)
{
	const char *val, *dest;
	char buf[100];

	val = get_param(cmd, "FrameName");
	if (val == NULL)
		return -1;

	if (strcasecmp(val, "QoSMapConfigure") == 0) {
		dest = get_param(cmd, "Dest");
		if (!dest)
			return -1;

		val = get_param(cmd, "QoS_MAP_SET");
		if (val) {
			dut->ap_qos_map_set = atoi(val);
			sigma_dut_print(dut, DUT_MSG_INFO, "ap_qos_map_set %d",
					dut->ap_qos_map_set);
		}

		if (dut->ap_qos_map_set == 1)
			run_hostapd_cli(dut, "set_qos_map_set " QOS_MAP_SET_1);
		else if (dut->ap_qos_map_set == 2)
			run_hostapd_cli(dut, "set_qos_map_set " QOS_MAP_SET_2);

		snprintf(buf, sizeof(buf), "send_qos_map_conf %s", dest);
		if (run_hostapd_cli(dut, buf) != 0)
			return -1;
	}

	return 1;
}


static int ath_ap_send_frame_vht(struct sigma_dut *dut, struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *val;
	char *ifname;
	char buf[100];
	int chwidth, nss;

	val = get_param(cmd, "FrameName");
	if (!val || strcasecmp(val, "op_md_notif_frm") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported FrameName");
		return 0;
	}

	/*
	 * Sequence of commands for Opmode notification on
	 * Peregrine based products
	 */
	ifname = get_main_ifname();

	/* Disable STBC */
	snprintf(buf, sizeof(buf), "iwpriv %s tx_stbc 0", ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv tx_stbc 0 failed!");
	}

	/* Check whether optional arg channel width was passed */
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

	/* Check whether optional arg NSS was passed */
	val = get_param(cmd, "NSS");
	if (val) {
		/* Convert nss to chainmask */
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
		snprintf(buf, sizeof(buf), "iwpriv %s rxchainmask %d",
			 ifname, nss);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv rxchainmask failed!");
		}
	}

	/* Send the opmode notification */
	snprintf(buf, sizeof(buf), "iwpriv %s opmode_notify 0", ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv opmode_notify failed!");
	}

	return 1;
}


static int ath_ap_send_frame_loc(struct sigma_dut *dut, struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *val;
	FILE *f;
	int rand_int = 0;

	val = get_param(cmd, "MsntType");
	if (val) {
		if (dut->ap_msnt_type == 0)
			dut->ap_msnt_type = atoi(val);

		if (dut->ap_msnt_type != 5 && dut->ap_msnt_type != 2) {
			dut->ap_msnt_type = atoi(val);
			if (dut->ap_msnt_type == 1) {
				val = get_param(cmd, "RandInterval");
				if (val)
					rand_int = atoi(val);
				f = fopen("/tmp/ftmrr.txt", "a");
				if (!f) {
					sigma_dut_print(dut, DUT_MSG_ERROR,
							"Failed to open /tmp/ftmrr.txt");
					return -1;
				}

				fprintf(f, "sta_mac = %s\n", cmd->values[3]);
				fprintf(f, "meas_type = 0x10\nrand_inter = 0x%x\nmin_ap_count = 0x%s\ndialogtoken = 0x1\nnum_repetitions = 0x0\nmeas_token = 0xf\nmeas_req_mode = 0x00\n",
					rand_int, cmd->values[7]);
				fclose(f);
				dut->ap_msnt_type = 5;
				run_system(dut, "wpc -f /tmp/ftmrr.txt");
			}
		} else if (dut->ap_msnt_type == 5) {
			run_system(dut, "wpc -f /tmp/ftmrr.txt");
		} else if (dut->ap_msnt_type == 2) {
			f = fopen("/tmp/wru.txt", "w");
			if (!f) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to open /tmp/wru.txt");
				return -1;
			}

			fprintf(f, "sta_mac = %s\n", cmd->values[3]);
			fprintf(f, "meas_type = 0x08\ndialogtoken = 0x1\nnum_repetitions = 0x0\nmeas_token = 0x1\nmeas_req_mode = 0x00\nloc_subject = 0x01\n");
			fclose(f);
			run_system(dut, "wpc -w /tmp/wru.txt");
		}
	}
	return 1;
}


/*
 * The following functions parse_send_frame_params_int(),
 * parse_send_frame_params_str(), and parse_send_frame_params_mac()
 * are used by ath_ap_send_frame_bcn_rpt_req().
 * Beacon Report Request is a frame used as part of the MBO program.
 * The command for sending beacon report has a lot of
 * arguments and having these functions reduces code size.
 *
 */
static int parse_send_frame_params_int(char *param, struct sigma_cmd *cmd,
				       struct sigma_dut *dut,
				       char *buf, size_t buf_size)
{
	const char *str_val;
	int int_val;
	char temp[100];

	str_val = get_param(cmd, param);
	if (!str_val) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "%s not given", param);
		return -1;
	}
	int_val = atoi(str_val);
	snprintf(temp, sizeof(temp), " %d", int_val);
	strlcat(buf, temp, buf_size);
	return 0;
}


static int parse_send_frame_params_str(char *param, struct sigma_cmd *cmd,
				       struct sigma_dut *dut,
				       char *buf, size_t buf_size)
{
	const char *str_val;
	char temp[100];

	str_val = get_param(cmd, param);
	if (!str_val) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "%s not given", param);
		return -1;
	}
	snprintf(temp, sizeof(temp), " %s", str_val);
	temp[sizeof(temp) - 1] = '\0';
	strlcat(buf, temp, buf_size);
	return 0;
}


static int parse_send_frame_params_mac(char *param, struct sigma_cmd *cmd,
				       struct sigma_dut *dut,
				       char *buf, size_t buf_size)
{
	const char *str_val;
	unsigned char mac[6];
	char temp[100];

	str_val = get_param(cmd, param);
	if (!str_val) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "%s not given", param);
		return -1;
	}

	if (parse_mac_address(dut, str_val, mac) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"MAC Address not in proper format");
		return -1;
	}
	snprintf(temp, sizeof(temp), " %02x:%02x:%02x:%02x:%02x:%02x",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	strlcat(buf, temp, buf_size);
	return 0;
}


static void fill_1_or_0_based_on_presence(struct sigma_cmd *cmd, char *param,
					  char *buf, size_t buf_size)
{
	const char *str_val;
	char *value = " 1";

	str_val = get_param(cmd, param);
	if (!str_val || str_val[0] == '\0')
		value = " 0";
	strlcat(buf, value, buf_size);

}


/*
 * wifitool athN sendbcnrpt
 * <STA MAC        - Plugs in from Dest_MAC>
 * <regclass       - Plugs in from RegClass - int>
 * <channum        - Plugs in from Channel PARAM of dev_send_frame - int>
 * <rand_ivl       - Plugs in from RandInt - string>
 * <duration       - Plugs in from MeaDur - integer>
 * <mode           - Plugs in from MeaMode - string>
 * <req_ssid       - Plugs in from SSID PARAM of dev_send_frame - string>
 * <rep_cond       - Plugs in from RptCond - integer>
 * <rpt_detail     - Plugs in from RptDet - integer>
 * <req_ie         - Plugs in from ReqInfo PARAM of dev_send_frame - string>
 * <chanrpt_mode   - Plugs in from APChanRpt - integer>
 * <specific_bssid - Plugs in from BSSID PARAM of dev_send_frame>
 * [AP channel numbers]
 */
static int ath_ap_send_frame_bcn_rpt_req(struct sigma_dut *dut,
					 struct sigma_cmd *cmd,
					 const char *ifname)
{
	char buf[100];
	int rpt_det;
	const char *str_val;
	const char *mea_mode;

	snprintf(buf, sizeof(buf), "wifitool %s sendbcnrpt", ifname);

	if (parse_send_frame_params_mac("Dest_MAC", cmd, dut, buf, sizeof(buf)))
		return -1;
	if (parse_send_frame_params_int("RegClass", cmd, dut, buf, sizeof(buf)))
		return -1;
	if (parse_send_frame_params_int("Channel", cmd, dut, buf, sizeof(buf)))
		return -1;
	if (parse_send_frame_params_str("RandInt", cmd, dut, buf, sizeof(buf)))
		return -1;
	if (parse_send_frame_params_int("MeaDur", cmd, dut, buf, sizeof(buf)))
		return -1;

	str_val = get_param(cmd, "MeaMode");
	if (!str_val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"MeaMode parameter not present in send bcn-rpt-req");
		return -1;
	}
	if (strcasecmp(str_val, "passive") == 0) {
		mea_mode = " 0";
	} else if (strcasecmp(str_val, "active") == 0) {
		mea_mode = " 1";
	} else if (strcasecmp(str_val, "table") == 0) {
		mea_mode = " 2";
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"MEA-MODE Value not correctly given");
		return -1;
	}
	strlcat(buf, mea_mode, sizeof(buf));

	fill_1_or_0_based_on_presence(cmd, "SSID", buf, sizeof(buf));

	if (parse_send_frame_params_int("RptCond", cmd, dut, buf, sizeof(buf)))
		return -1;

	if (parse_send_frame_params_int("RptDet", cmd, dut, buf, sizeof(buf)))
		return -1;
	str_val = get_param(cmd, "RptDet");
	rpt_det = str_val ? atoi(str_val) : 0;

	if (rpt_det)
		fill_1_or_0_based_on_presence(cmd, "ReqInfo", buf, sizeof(buf));
	else
		strlcat(buf, " 0", sizeof(buf));

	if (rpt_det)
		fill_1_or_0_based_on_presence(cmd, "APChanRpt", buf,
					      sizeof(buf));
	else
		strlcat(buf, " 0", sizeof(buf));

	if (parse_send_frame_params_mac("BSSID", cmd, dut, buf, sizeof(buf)))
		return -1;

	run_system(dut, buf);
	return 0;
}


static void inform_and_sleep(struct sigma_dut *dut, int seconds)
{
	sigma_dut_print(dut, DUT_MSG_DEBUG, "sleeping for %d seconds", seconds);
	sleep(seconds);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "woke up after %d seconds",
			seconds);
}


static int ath_ap_send_frame_btm_req(struct sigma_dut *dut,
				     struct sigma_cmd *cmd, const char *ifname)
{
	unsigned char mac_addr[ETH_ALEN];
	int disassoc_timer;
	char buf[100];
	const char *val;
	int cand_list = 1;
	int tsf = 2;

	val = get_param(cmd, "Dest_MAC");
	if (!val || parse_mac_address(dut, val, mac_addr) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"MAC Address not in proper format");
		return -1;
	}

	val = get_param(cmd, "Disassoc_Timer");
	if (val)
		disassoc_timer = atoi(val);
	else
		disassoc_timer = dut->ap_disassoc_timer;

	val = get_param(cmd, "Cand_List");
	if (val && val[0])
		cand_list = atoi(val);

	val = get_param(cmd, "BTMQuery_Reason_Code");
	if (val) {
		snprintf(buf, sizeof(buf), "iwpriv %s mbo_trans_rs %s",
			 ifname, val);
		run_system(dut, buf);
	}

	snprintf(buf, sizeof(buf),
		 "wifitool %s sendbstmreq %02x:%02x:%02x:%02x:%02x:%02x %d %d 3 %d %d %d %d",
		 ifname, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
		 mac_addr[4], mac_addr[5], cand_list, disassoc_timer,
		 dut->ap_btmreq_disassoc_imnt,
		 dut->ap_btmreq_term_bit,
		 tsf,
		 dut->ap_btmreq_bss_term_dur);
	run_system(dut, buf);

	if (dut->ap_btmreq_term_bit) {
		inform_and_sleep(dut, 3);
		run_system_wrapper(dut, "ifconfig %s down", ifname);
		inform_and_sleep(dut, dut->ap_btmreq_bss_term_dur * 60);
		run_system_wrapper(dut, "ifconfig %s up", ifname);
	} else if (dut->ap_btmreq_disassoc_imnt) {
		inform_and_sleep(dut, (disassoc_timer / 1000) + 1);
		run_system_wrapper(dut,
				   "iwpriv %s kickmac %02x:%02x:%02x:%02x:%02x:%02x",
				   ifname,
				   mac_addr[0], mac_addr[1], mac_addr[2],
				   mac_addr[3], mac_addr[4], mac_addr[5]);
	}
	return 0;
}


static int ath_ap_send_frame_mbo(struct sigma_dut *dut, struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *val;
	char *ifname;

	ifname = get_main_ifname();

	val = get_param(cmd, "FrameName");
	if (!val)
		return -1;

	if (strcasecmp(val, "BTMReq") == 0)
		ath_ap_send_frame_btm_req(dut, cmd, ifname);
	else if (strcasecmp(val, "BcnRptReq") == 0)
		ath_ap_send_frame_bcn_rpt_req(dut, cmd, ifname);
	else
		return -1;

	return 1;
}


static int ap_send_frame_vht(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	switch (get_driver_type()) {
	case DRIVER_ATHEROS:
		return ath_ap_send_frame_vht(dut, conn, cmd);
		break;
	case DRIVER_OPENWRT:
		switch (get_openwrt_driver_type()) {
		case OPENWRT_DRIVER_ATHEROS:
			return ath_ap_send_frame_vht(dut, conn, cmd);
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported ap_send_frame with the current openwrt driver");
			return 0;
		}
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported ap_send_frame with the current driver");
		return 0;
	}
}


static int ap_send_frame_loc(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	switch (get_driver_type()) {
	case DRIVER_ATHEROS:
		return ath_ap_send_frame_loc(dut, conn, cmd);
	case DRIVER_OPENWRT:
		switch (get_openwrt_driver_type()) {
		case OPENWRT_DRIVER_ATHEROS:
			return ath_ap_send_frame_loc(dut, conn, cmd);
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported ap_send_frame_loc with the current openwrt driver");
			return 0;
		}
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported ap_send_frame_loc with the current driver");
		return 0;
	}
}


static int ap_send_frame_mbo(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	switch (get_driver_type()) {
	case DRIVER_ATHEROS:
		return ath_ap_send_frame_mbo(dut, conn, cmd);
	case DRIVER_OPENWRT:
		switch (get_openwrt_driver_type()) {
		case OPENWRT_DRIVER_ATHEROS:
			return ath_ap_send_frame_mbo(dut, conn, cmd);
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported ap_send_frame with the current openwrt driver");
			return 0;
		}
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported ap_send_frame with the current driver");
		return 0;
	}
}


static int ap_send_frame_60g(struct sigma_dut *dut,
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


int cmd_ap_send_frame(struct sigma_dut *dut, struct sigma_conn *conn,
		      struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	/* const char *ifname = get_param(cmd, "INTERFACE"); */
	const char *val;
	enum send_frame_type frame;
	enum send_frame_protection protected;
	char buf[100];

	val = get_param(cmd, "Program");
	if (val) {
		if (strcasecmp(val, "HS2") == 0 ||
		    strcasecmp(val, "HS2-R2") == 0 ||
		    strcasecmp(val, "IOTLP") == 0)
			return ap_send_frame_hs2(dut, conn, cmd);
		if (strcasecmp(val, "VHT") == 0)
			return ap_send_frame_vht(dut, conn, cmd);
		if (strcasecmp(val, "LOC") == 0)
			return ap_send_frame_loc(dut, conn, cmd);
		if (strcasecmp(val, "MBO") == 0)
			return ap_send_frame_mbo(dut, conn, cmd);
		if (strcasecmp(val, "60GHz") == 0)
			return ap_send_frame_60g(dut, conn, cmd);
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
	else {
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

	val = get_param(cmd, "stationID");
	if (val == NULL)
		return -1;

	if (protected == INCORRECT_KEY ||
	    (protected == UNPROTECTED && frame == SAQUERY))
		return ap_inject_frame(dut, conn, frame, protected, val);

	switch (frame) {
	case DISASSOC:
		snprintf(buf, sizeof(buf), "disassoc %s test=%d",
			 val, protected == CORRECT_KEY);
		break;
	case DEAUTH:
		snprintf(buf, sizeof(buf), "deauth %s test=%d",
			 val, protected == CORRECT_KEY);
		break;
	case SAQUERY:
		snprintf(buf, sizeof(buf), "sa_query %s", val);
		break;
	}

	if (run_hostapd_cli(dut, buf) != 0)
		return -2;

	return 1;
}


static int cmd_ap_get_mac_address(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
#if defined( __linux__)
	/* const char *name = get_param(cmd, "NAME"); */
	/* const char *ifname = get_param(cmd, "INTERFACE"); */
	char resp[50];
	unsigned char addr[6];
	char ifname[50];
	struct ifreq ifr;
	int s, wlan_tag = 1;
	const char *val;

	val = get_param(cmd, "WLAN_TAG");
	if (val) {
		wlan_tag = atoi(val);
		if (wlan_tag < 1 || wlan_tag > 3) {
			/*
			 * The only valid WLAN Tags as of now as per the latest
			 * WFA scripts are 1, 2, and 3.
			 */
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported WLAN_TAG");
			return 0;
		}
	}

	get_if_name(dut, ifname, sizeof(ifname), wlan_tag);

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return -1;
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0) {
		perror("ioctl");
		close(s);
		return -1;
	}
	close(s);
	memcpy(addr, ifr.ifr_hwaddr.sa_data, 6);

	snprintf(resp, sizeof(resp), "mac,%02x:%02x:%02x:%02x:%02x:%02x",
		 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
#elif defined( __QNXNTO__)
	char resp[50];
	unsigned char addr[6];

	if (!sigma_main_ifname) {
		send_resp(dut, conn, SIGMA_ERROR, "ifname is null");
		return 0;
	}

	if (get_hwaddr(sigma_main_ifname, addr) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to get address");
		return 0;
	}
	snprintf(resp, sizeof(resp), "mac,%02x:%02x:%02x:%02x:%02x:%02x",
		 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
#else /* __linux__ */
	send_resp(dut, conn, SIGMA_ERROR, "errorCode,ap_get_mac_address not "
		  "yet supported");
	return 0;
#endif /* __linux__ */
}


static int cmd_ap_set_pmf(struct sigma_dut *dut, struct sigma_conn *conn,
			  struct sigma_cmd *cmd)
{
	/*
	 * Ignore the command since the parameters are already handled through
	 * ap_set_security.
	 */

	return 1;
}


static int cmd_ap_set_hs2(struct sigma_dut *dut, struct sigma_conn *conn,
			  struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	/* const char *ifname = get_param(cmd, "INTERFACE"); */
	const char *val, *dest;
	char *pos, buf[100];
	int i, wlan_tag = 1;

	sigma_dut_print(dut, DUT_MSG_INFO, "ap_set_hs2: Processing the "
			"following parameters");
	for (i = 0; i < cmd->count; i++) {
		sigma_dut_print(dut, DUT_MSG_INFO, "%s %s", cmd->params[i],
				(cmd->values[i] ? cmd->values[i] : "NULL"));
	}

	val = get_param(cmd, "ICMPv4_ECHO");
	if (val && atoi(val)) {
		snprintf(buf, sizeof(buf), "ebtables -F");
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set ebtables rules, RULE-12");
		}
		return 1;
	}

	val = get_param(cmd, "WLAN_TAG");
	if (val) {
		wlan_tag = atoi(val);
		if (wlan_tag != 1 && wlan_tag != 2) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid WLAN_TAG");
			return 0;
		}
	}

	if (wlan_tag == 2) {
		val = get_param(cmd, "PROXY_ARP");
		if (val)
			dut->ap2_proxy_arp = atoi(val);
		return 1;
	}

	dest = get_param(cmd, "STA_MAC");
	if (dest) {
		/* This is a special/ugly way of using this command.
		 * If "Dest" MAC is included, assume that this command
		 * is being issued after ap_config_commit for dynamically
		 * setting the QoS Map Set.
		 */
		val = get_param(cmd, "QoS_MAP_SET");
		if (val) {
			dut->ap_qos_map_set = atoi(val);
			sigma_dut_print(dut, DUT_MSG_INFO, "ap_qos_map_set %d",
					dut->ap_qos_map_set);
		}

		if (dut->ap_qos_map_set == 1)
			run_hostapd_cli(dut, "set_qos_map_set " QOS_MAP_SET_1);
		else if (dut->ap_qos_map_set == 2)
			run_hostapd_cli(dut, "set_qos_map_set " QOS_MAP_SET_2);

		snprintf(buf, sizeof(buf), "send_qos_map_conf %s", dest);
		if (run_hostapd_cli(dut, buf) != 0)
			return -1;
	}

	val = get_param(cmd, "DGAF_DISABLE");
	if (val)
		dut->ap_dgaf_disable = atoi(val);

	dut->ap_interworking = 1;

	val = get_param(cmd, "INTERWORKING");
	if (val == NULL)
		val = get_param(cmd, "INTERNETWORKING");
	if (val != NULL && atoi(val) == 0) {
		dut->ap_interworking = 0;
		dut->ap_hs2 = 0;
		return 1;
	}

	val = get_param(cmd, "ACCS_NET_TYPE");
	if (val) {
		if (strcasecmp(val, "Chargeable_Public_Network") == 0 ||
		    strcasecmp(val, "Chargable_Public_Network") == 0 ||
		    strcasecmp(val, "Chargable Public Network") == 0)
			dut->ap_access_net_type = 2;
		else
			dut->ap_access_net_type = atoi(val);
	}

	val = get_param(cmd, "INTERNET");
	if (val)
		dut->ap_internet = atoi(val);

	val = get_param(cmd, "VENUE_GRP");
	if (val) {
		if (strcasecmp(val, "Business") == 0)
			dut->ap_venue_group = 2;
		else
			dut->ap_venue_group = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_venue_name %d",
				dut->ap_venue_name);
	}

	val = get_param(cmd, "VENUE_TYPE");
	if (val) {
		if (strcasecmp(val, "R&D") == 0)
			dut->ap_venue_type = 8;
		else
			dut->ap_venue_type = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_venue_type %d",
				dut->ap_venue_type);
	}

	val = get_param(cmd, "HESSID");
	if (val) {
		if (strlen(val) >= sizeof(dut->ap_hessid)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid HESSID");
			return 0;
		}
		snprintf(dut->ap_hessid, sizeof(dut->ap_hessid), "%s", val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_hessid %s",
				dut->ap_hessid);
	}

	val = get_param(cmd, "ROAMING_CONS");
	if (val) {
		if (strlen(val) >= sizeof(dut->ap_roaming_cons)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid ROAMING_CONS");
			return 0;
		}
		if (strcasecmp(val, "Disabled") == 0) {
			dut->ap_roaming_cons[0] = '\0';
		} else {
			snprintf(dut->ap_roaming_cons,
				 sizeof(dut->ap_roaming_cons), "%s", val);
		}
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_roaming_cons %s",
				dut->ap_roaming_cons);
	}

	val = get_param(cmd, "ANQP");
	if (val)
		dut->ap_anqpserver_on = atoi(val);

	val = get_param(cmd, "NAI_REALM_LIST");
	if (val) {
		dut->ap_nai_realm_list = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_nai_realm_list %d",
				dut->ap_nai_realm_list);
	}

	val = get_param(cmd, "3GPP_INFO");
	if (val) {
		/* What kind of encoding format is used?! */
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,3GPP_INFO "
			  "not yet supported (contents not fully defined)");
		return 0;
	}

	val = get_param(cmd, "DOMAIN_LIST");
	if (val) {
		if (strlen(val) >= sizeof(dut->ap_domain_name_list)) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Too long "
				  "DOMAIN_LIST");
			return 0;
		}
		snprintf(dut->ap_domain_name_list,
			 sizeof(dut->ap_domain_name_list), "%s", val);
		pos = dut->ap_domain_name_list;
		while (*pos) {
			if (*pos == ';')
				*pos = ',';
			pos++;
		}
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_domain_name_list %s",
				dut->ap_domain_name_list);
	}

	val = get_param(cmd, "OPER_NAME");
	if (val) {
		dut->ap_oper_name = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_oper_name %d",
				dut->ap_oper_name);
	}

	val = get_param(cmd, "VENUE_NAME");
	if (val) {
		dut->ap_venue_name = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_venue_name %d",
				dut->ap_venue_name);
	}

	val = get_param(cmd, "GAS_CB_DELAY");
	if (val) {
		dut->ap_gas_cb_delay = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_gas_cb_delay %d",
				dut->ap_gas_cb_delay);
	}

	val = get_param(cmd, "MIH");
	if (val && atoi(val) > 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,MIH not "
			  "supported");
		return 0;
	}

	val = get_param(cmd, "L2_TRAFFIC_INSPECT");
	if (val) {
		dut->ap_l2tif = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_l2tif %d",
				dut->ap_l2tif);
	}

	val = get_param(cmd, "BCST_UNCST");
	if (val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,BCST_UNCST not yet supported");
		return 0;
	}

	val = get_param(cmd, "PLMN_MCC");
	if (val) {
		char mcc[100], *start, *end;
		int i = 0;
		if (strlen(val) >= sizeof(mcc)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,PLMN_MCC too long");
			return 0;
		}
		strlcpy(mcc, val, sizeof(mcc));
		start = mcc;
		while ((end = strchr(start, ';'))) {
			/* process all except the last */
			*end = '\0';
			if (strlen(start) != 3) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Invalid PLMN_MCC");
				return 0;
			}
			snprintf(dut->ap_plmn_mcc[i],
				 sizeof(dut->ap_plmn_mcc[i]), "%s", start);
			sigma_dut_print(dut, DUT_MSG_INFO, "ap_plmn_mcc %s",
					dut->ap_plmn_mcc[i]);
			i++;
			start = end + 1;
			*end = ';';
		}
		if (strlen(start) != 3) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid PLMN_MCC");
			return 0;
		}
		/* process last or only one */
		snprintf(dut->ap_plmn_mcc[i],
			sizeof(dut->ap_plmn_mcc[i]), "%s", start);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_plmn_mcc %s",
			dut->ap_plmn_mcc[i]);
	}

	val = get_param(cmd, "PLMN_MNC");
	if (val) {
		char mnc[100], *start, *end;
		int i = 0;
		if (strlen(val) >= sizeof(mnc)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,PLMN_MNC too long");
			return 0;
		}
		strlcpy(mnc, val, sizeof(mnc));
		start = mnc;
		while ((end = strchr(start, ';'))) {
			*end = '\0';
			if (strlen(start) != 2 && strlen(start) != 3) {
				send_resp(dut, conn, SIGMA_ERROR,
					"errorCode,Invalid PLMN_MNC");
				return 0;
			}
			snprintf(dut->ap_plmn_mnc[i],
				 sizeof(dut->ap_plmn_mnc[i]), "%s", start);
			sigma_dut_print(dut, DUT_MSG_INFO, "ap_plmn_mnc %s",
				dut->ap_plmn_mnc[i]);
			i++;
			start = end + 1;
			*end = ';';
		}
		if (strlen(start) != 2 && strlen(start) != 3) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid PLMN_MNC");
			return 0;
		}
		snprintf(dut->ap_plmn_mnc[i],
			sizeof(dut->ap_plmn_mnc[i]), "%s", start);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_plmn_mnc %s",
			dut->ap_plmn_mnc[i]);
	}

	val = get_param(cmd, "PROXY_ARP");
	if (val) {
		dut->ap_proxy_arp = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_proxy_arp %d",
				dut->ap_proxy_arp);
	}

	val = get_param(cmd, "WAN_METRICS");
	if (val) {
		dut->ap_wan_metrics = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_wan_metrics %d",
				dut->ap_wan_metrics);
	}

	val = get_param(cmd, "CONN_CAP");
	if (val) {
		dut->ap_conn_capab = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_conn_capab %d",
				dut->ap_conn_capab);
	}

	val = get_param(cmd, "IP_ADD_TYPE_AVAIL");
	if (val) {
		dut->ap_ip_addr_type_avail = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_ip_addr_type_avail %d",
				dut->ap_ip_addr_type_avail);
	}

	val = get_param(cmd, "NET_AUTH_TYPE");
	if (val) {
		dut->ap_net_auth_type = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_net_auth_type %d",
				dut->ap_net_auth_type);
	}

	val = get_param(cmd, "OP_CLASS");
	if (val == NULL)
		val = get_param(cmd, "OPER_CLASS");
	if (val) {
		dut->ap_oper_class = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_oper_class %d",
				dut->ap_oper_class);
	}

	val = get_param(cmd, "OSU_PROVIDER_LIST");
	if (val) {
		dut->ap_osu_provider_list = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_osu_provider_list %d",
				dut->ap_osu_provider_list);
	}

	val = get_param(cmd, "OSU_SERVER_URI");
	if (val) {
		i = 0;
		do {
			int len;
			const char *uri = val;
			val = strchr(val, ' ');
			len = val ? (val++ - uri) : (int) strlen(uri);
			if (len > 0 && len < 256) {
				memcpy(dut->ap_osu_server_uri[i], uri, len);
				dut->ap_osu_server_uri[i][len] = '\0';
				sigma_dut_print(dut, DUT_MSG_INFO,
						"ap_osu_server_uri[%d] %s", i,
						dut->ap_osu_server_uri[i]);
			}
		} while (val && ++i < 10);
	}

	val = get_param(cmd, "OSU_METHOD");
	if (val) {
		i = 0;
		do {
			int len;
			const char *method = val;
			val = strchr(val, ' ');
			len = val ? (val++ - method) : (int) strlen(method);
			if (len > 0) {
				if (strncasecmp(method, "SOAP", len) == 0)
					dut->ap_osu_method[i] = 1;
				else if (strncasecmp(method, "OMADM", len) == 0)
					dut->ap_osu_method[i] = 0;
				else
					return -2;
			}
		} while (val && ++i < 10);
	}

	val = get_param(cmd, "OSU_SSID");
	if (val) {
		if (strlen(val) > 0 && strlen(val) <= 32) {
			strlcpy(dut->ap_osu_ssid, val,
				sizeof(dut->ap_osu_ssid));
			sigma_dut_print(dut, DUT_MSG_INFO,
					"ap_osu_ssid %s",
					dut->ap_osu_ssid);
		}
	}

	val = get_param(cmd, "OSU_ICON_TAG");
	if (val)
		dut->ap_osu_icon_tag = atoi(val);

	val = get_param(cmd, "QoS_MAP_SET");
	if (val) {
		dut->ap_qos_map_set = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_qos_map_set %d",
				dut->ap_qos_map_set);
	}

	val = get_param(cmd, "BSS_LOAD");
	if (val) {
		dut->ap_bss_load = atoi(val);
		sigma_dut_print(dut, DUT_MSG_INFO, "ap_bss_load %d",
				dut->ap_bss_load);
	}

	return 1;
}


void nfc_status(struct sigma_dut *dut, const char *state, const char *oper)
{
	char buf[100];

	if (!file_exists("nfc-status"))
		return;

	snprintf(buf, sizeof(buf), "./nfc-status %s %s", state, oper);
	run_system(dut, buf);
}


static int run_nfc_command(struct sigma_dut *dut, const char *cmd,
			   const char *info)
{
	int res;

	printf("\n\n\n=====[ NFC operation ]=========================\n\n");
	printf("%s\n\n", info);

	nfc_status(dut, "START", info);
	res = run_system(dut, cmd);
	nfc_status(dut, res ? "FAIL" : "SUCCESS", info);
	if (res) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to run '%s': %d",
				cmd, res);
		return res;
	}

	return 0;
}


static int ap_nfc_write_config_token(struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct sigma_cmd *cmd)
{
	int res;
	char buf[300];

	run_system(dut, "killall wps-ap-nfc.py");
	unlink("nfc-success");
	snprintf(buf, sizeof(buf),
		 "./wps-ap-nfc.py --no-wait %s%s --success nfc-success write-config",
		 dut->summary_log ? "--summary " : "",
		 dut->summary_log ? dut->summary_log : "");
	res = run_nfc_command(dut, buf,
			      "Touch NFC Tag to write WPS configuration token");
	if (res || !file_exists("nfc-success")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to write tag");
		return 0;
	}

	return 1;
}


static int ap_nfc_wps_read_passwd(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	int res;
	char buf[300];

	run_system(dut, "killall wps-ap-nfc.py");

	unlink("nfc-success");
	snprintf(buf, sizeof(buf),
		 "./wps-ap-nfc.py -1 --no-wait %s%s --success nfc-success",
		 dut->summary_log ? "--summary " : "",
		 dut->summary_log ? dut->summary_log : "");
	res = run_nfc_command(dut, buf, "Touch NFC Tag to read it");
	if (res || !file_exists("nfc-success")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to read tag");
		return 0;
	}

	return 1;
}


static int ap_nfc_write_password_token(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd)
{
	int res;
	char buf[300];

	run_system(dut, "killall wps-ap-nfc.py");
	unlink("nfc-success");
	snprintf(buf, sizeof(buf),
		 "./wps-ap-nfc.py --no-wait %s%s --success nfc-success write-password",
		 dut->summary_log ? "--summary " : "",
		 dut->summary_log ? dut->summary_log : "");
	res = run_nfc_command(dut, buf,
			      "Touch NFC Tag to write WPS password token");
	if (res || !file_exists("nfc-success")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to write tag");
		return 0;
	}

	if (run_hostapd_cli(dut, "wps_nfc_token enable") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to enable NFC password token");
		return 0;
	}

	return 1;
}


static int ap_nfc_wps_connection_handover(struct sigma_dut *dut,
					  struct sigma_conn *conn,
					  struct sigma_cmd *cmd)
{
	int res;
	char buf[300];

	run_system(dut, "killall wps-ap-nfc.py");
	unlink("nfc-success");
	snprintf(buf, sizeof(buf),
		 "./wps-ap-nfc.py -1 --no-wait %s%s --success nfc-success",
		 dut->summary_log ? "--summary " : "",
		 dut->summary_log ? dut->summary_log : "");
	res = run_nfc_command(dut, buf,
			      "Touch NFC Device to respond to WPS connection handover");
	if (res) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to enable NFC for connection "
			  "handover");
		return 0;
	}
	if (!file_exists("nfc-success")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to complete NFC connection handover");
		return 0;
	}

	return 1;
}


static int cmd_ap_nfc_action(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "Name"); */
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *oper = get_param(cmd, "Operation");

	if (oper == NULL)
		return -1;

	if (strcasecmp(oper, "WRITE_CONFIG") == 0)
		return ap_nfc_write_config_token(dut, conn, cmd);
	if (strcasecmp(oper, "WRITE_PASSWD") == 0)
		return ap_nfc_write_password_token(dut, conn, cmd);
	if (strcasecmp(oper, "WPS_READ_PASSWD") == 0)
		return ap_nfc_wps_read_passwd(dut, conn, cmd);
	if (strcasecmp(oper, "WPS_CONN_HNDOVR") == 0)
		return ap_nfc_wps_connection_handover(dut, conn, cmd);

	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported operation");
	return 0;
}


static int cmd_ap_wps_read_pin(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	char *pin = "12345670"; /* TODO: use random PIN */
	char resp[100];

	snprintf(resp, sizeof(resp), "PIN,%s", pin);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);

	return 0;
}


static int ath_vht_op_mode_notif(struct sigma_dut *dut, const char *ifname,
				 const char *val)
{
	char *token, *result;
	int nss = 0, chwidth = 0;
	char buf[100];
	char *saveptr;

	/*
	 * The following commands should be invoked to generate
	 * VHT op mode notification
	 */

	/* Extract the NSS info */
	token = strdup(val);
	if (!token)
		return -1;
	result = strtok_r(token, ";", &saveptr);
	if (result) {
		int count = atoi(result);

		/* We do not support NSS > 3 */
		if (count < 0 || count > 3) {
			free(token);
			return -1;
		}

		/* Convert nss to chainmask */
		while (count--)
			nss = (nss << 1) | 1;

		snprintf(buf, sizeof(buf), "iwpriv %s rxchainmask %d",
			 ifname, nss);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv wifi1 rxchainmask failed!");
		}
	}

	/* Extract the Channel width info */
	result = strtok_r(NULL, ";", &saveptr);
	if (result) {
		switch (atoi(result)) {
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

	/* Send the opmode notification */
	snprintf(buf, sizeof(buf), "iwpriv %s opmode_notify 0", ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv opmode_notify failed!");
	}
	free(token);

	return 0;
}


static int ath_vht_nss_mcs(struct sigma_dut *dut, const char *ifname,
			   const char *val)
{
	/* String (nss_operating_mode; mcs_operating_mode) */
	int nss, mcs;
	char *token, *result;
	char buf[100];
	char *saveptr;

	token = strdup(val);
	if (!token)
		return -1;
	result = strtok_r(token, ";", &saveptr);
	if (!result) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"VHT NSS not specified");
		goto end;
	}
	if (strcasecmp(result, "def") != 0) {
		nss = atoi(result);

		if (nss == 4)
			ath_disable_txbf(dut, ifname);

		snprintf(buf, sizeof(buf), "iwpriv %s nss %d", ifname, nss);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv nss failed");
		}
	} else {
		if (dut->device_type == AP_testbed && dut->ap_sgi80 == 1) {
			snprintf(buf, sizeof(buf), "iwpriv %s nss 1", ifname);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv nss failed");
			}
		}
	}

	result = strtok_r(NULL, ";", &saveptr);
	if (!result) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"VHT MCS not specified");
		goto end;
	}
	if (strcasecmp(result, "def") == 0) {
		if (dut->device_type == AP_testbed && dut->ap_sgi80 == 1) {
			snprintf(buf, sizeof(buf), "iwpriv %s vhtmcs 7",
				 ifname);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv vhtmcs failed");
			}
		} else {
			snprintf(buf, sizeof(buf),
				 "iwpriv %s set11NRates 0", ifname);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv set11NRates failed");
			}
		}
	} else {
		mcs = atoi(result);
		snprintf(buf, sizeof(buf), "iwpriv %s vhtmcs %d", ifname, mcs);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv vhtmcs failed");
		}
	}

end:
	free(token);
	return 0;
}


static int ath_vht_chnum_band(struct sigma_dut *dut, const char *ifname,
			      const char *val)
{
	char *token, *result;
	int channel = 36;
	int chwidth = 80;
	char buf[100];
	char *saveptr;

	/* Extract the channel info */
	token = strdup(val);
	if (!token)
		return -1;
	result = strtok_r(token, ";", &saveptr);
	if (result)
		channel = atoi(result);

	/* Extract the channel width info */
	result = strtok_r(NULL, ";", &saveptr);
	if (result)
		chwidth = atoi(result);

	/* Issue the channel switch command */
	snprintf(buf, sizeof(buf), "iwpriv %s doth_ch_chwidth %d 10 %d",
		 ifname, channel, chwidth);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv doth_ch_chwidth failed!");
	}

	free(token);
	return 0;
}


static int ath_ndpa_stainfo_mac(struct sigma_dut *dut, const char *ifname,
				const char *val)
{
	char buf[80];
	unsigned char mac_addr[6];

	if (parse_mac_address(dut, val, mac_addr) < 0)
		return -1;

	snprintf(buf, sizeof(buf),
		 "wifitool %s beeliner_fw_test 92 0x%02x%02x%02x%02x",
		 ifname, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3]);
	run_system(dut, buf);

	snprintf(buf, sizeof(buf), "wifitool %s beeliner_fw_test 93 0x%02x%02x",
		 ifname, mac_addr[4], mac_addr[5]);
	run_system(dut, buf);

	snprintf(buf, sizeof(buf), "wifitool %s beeliner_fw_test 94 1", ifname);
	run_system(dut, buf);

	return 0;
}


void novap_reset(struct sigma_dut *dut, const char *ifname)
{
	char buf[60];

	snprintf(buf, sizeof(buf), "iwpriv %s novap_reset 1", ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"disabling novap reset failed");
	}
}


struct mbo_pref_ap * mbo_find_nebor_ap_entry(struct sigma_dut *dut,
					     const uint8_t *mac_addr)
{
	int i;

	for (i = 0; i < dut->mbo_pref_ap_cnt; i++) {
		if (memcmp(mac_addr, dut->mbo_pref_aps[i].mac_addr,
			   ETH_ALEN) == 0)
			return &dut->mbo_pref_aps[i];
	}
	return NULL;
}


static void mbo_add_nebor_entry(struct sigma_dut *dut, const uint8_t *mac_addr,
				int ap_ne_class, int ap_ne_op_ch,
				int ap_ne_pref)
{
	struct mbo_pref_ap *entry;
	uint8_t self_mac[ETH_ALEN];
	char ifname[50];

	get_if_name(dut, ifname, sizeof(ifname), 1);
	get_hwaddr(ifname, self_mac);

	if (memcmp(mac_addr, self_mac, ETH_ALEN) == 0)
		entry = &dut->mbo_self_ap_tuple;
	else
		entry = mbo_find_nebor_ap_entry(dut, mac_addr);

	if (!entry) {
		if (dut->mbo_pref_ap_cnt >= MBO_MAX_PREF_BSSIDS) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Nebor AP List is full. Not adding");
			return;
		}
		entry = &dut->mbo_pref_aps[dut->mbo_pref_ap_cnt];
		dut->mbo_pref_ap_cnt++;
		memcpy(entry->mac_addr, mac_addr, ETH_ALEN);
		entry->ap_ne_class = -1;
		entry->ap_ne_op_ch = -1;
		entry->ap_ne_pref = -1;
	}
	if (ap_ne_class != -1)
		entry->ap_ne_class = ap_ne_class;
	if (ap_ne_op_ch != -1)
		entry->ap_ne_op_ch = ap_ne_op_ch;
	if (ap_ne_pref != -1)
		entry->ap_ne_pref = ap_ne_pref;
}


static int ath_set_nebor_bssid(struct sigma_dut *dut, const char *ifname,
			       struct sigma_cmd *cmd)
{
	unsigned char mac_addr[ETH_ALEN];
	const char *val;
	/*
	 * -1 is invalid value for the following
	 *  to differentiate between unset and set values
	 *  -1 => implies not set by CAPI
	 */
	int ap_ne_class = -1, ap_ne_op_ch = -1, ap_ne_pref = -1;
	int list_offset = dut->mbo_pref_ap_cnt;

	if (list_offset >= MBO_MAX_PREF_BSSIDS) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"AP Pref Entry list is full");
		return -1;
	}

	val = get_param(cmd, "Nebor_Op_Class");
	if (val)
		ap_ne_class = atoi(val);

	val = get_param(cmd, "Nebor_Op_Ch");
	if (val)
		ap_ne_op_ch = atoi(val);

	val = get_param(cmd, "Nebor_Pref");
	if (val)
		ap_ne_pref = atoi(val);

	val = get_param(cmd, "Nebor_BSSID");
	if (!val || parse_mac_address(dut, val, mac_addr) < 0)
		return -1;

	mbo_add_nebor_entry(dut, mac_addr, ap_ne_class, ap_ne_op_ch,
			    ap_ne_pref);
	apply_mbo_pref_ap_list(dut);
	return 0;
}


static int ath_ap_set_rfeature(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *val;
	char *ifname;

	ifname = get_main_ifname();

	/* Disable vap reset between the commands */
	novap_reset(dut, ifname);

	val = get_param(cmd, "Opt_md_notif_ie");
	if (val && ath_vht_op_mode_notif(dut, ifname, val) < 0)
		return -1;

	/* TODO: Optional arguments */

	val = get_param(cmd, "nss_mcs_opt");
	if (val && ath_vht_nss_mcs(dut, ifname, val) < 0)
		return -1;

	val = get_param(cmd, "chnum_band");
	if (val && ath_vht_chnum_band(dut, ifname, val) < 0)
		return -1;

	val = get_param(cmd, "RTS_FORCE");
	if (val)
		ath_config_rts_force(dut, ifname, val);

	val = get_param(cmd, "DYN_BW_SGNL");
	if (val)
		ath_config_dyn_bw_sig(dut, ifname, val);

	val = get_param(cmd, "CTS_WIDTH");
	if (val)
		ath_set_cts_width(dut, ifname, val);

	val = get_param(cmd, "Ndpa_stainfo_mac");
	if (val && ath_ndpa_stainfo_mac(dut, ifname, val) < 0)
		return -1;

	val = get_param(cmd, "txBandwidth");
	if (val && ath_set_width(dut, conn, ifname, val) < 0)
		return -1;

	val = get_param(cmd, "Assoc_Disallow");
	if (val)
		ath_set_assoc_disallow(dut, ifname, val);


	ath_set_nebor_bssid(dut, ifname, cmd);
	val = get_param(cmd, "BTMReq_DisAssoc_Imnt");
	if (val)
		dut->ap_btmreq_disassoc_imnt = atoi(val);

	val = get_param(cmd, "BTMReq_Term_Bit");
	if (val)
		dut->ap_btmreq_term_bit = atoi(val);

	val = get_param(cmd, "Assoc_Delay");
	if (val)
		run_system_wrapper(dut, "iwpriv %s mbo_asoc_ret %s",
				   ifname, val);

	val = get_param(cmd, "Disassoc_Timer");
	if (val)
		dut->ap_disassoc_timer = atoi(val);

	val = get_param(cmd, "BSS_Term_Duration");
	if (val)
		dut->ap_btmreq_bss_term_dur = atoi(val);

	return 1;
}


static int wcn_vht_chnum_band(struct sigma_dut *dut, const char *ifname,
			      const char *val)
{
	char *token, *result;
	int channel = 36;
	char buf[100];
	char *saveptr;

	/* Extract the channel info */
	token = strdup(val);
	if (!token)
		return -1;
	result = strtok_r(token, ";", &saveptr);
	if (result)
		channel = atoi(result);

	/* Issue the channel switch command */
	snprintf(buf, sizeof(buf), "iwpriv %s setChanChange %d",
		 ifname, channel);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv setChanChange failed!");
	}

	free(token);
	return 0;
}


static int wcn_ap_set_rfeature(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *val;
	char *ifname;

	ifname = get_main_ifname();

	val = get_param(cmd, "chnum_band");
	if (val && wcn_vht_chnum_band(dut, ifname, val) < 0)
		return -1;

	return 1;
}


static int mac80211_vht_chnum_band(struct sigma_dut *dut, const char *ifname,
				   const char *val)
{
	char *token, *result;
	int channel = 36, chwidth = 80, center_freq_idx, center_freq,
		channel_freq;
	char buf[100];
	char *saveptr;

	/* Extract the channel info */
	token = strdup(val);
	if (!token)
		return -1;
	result = strtok_r(token, ";", &saveptr);
	if (result)
		channel = atoi(result);

	/* Extract the channel width info */
	result = strtok_r(NULL, ";", &saveptr);
	if (result)
		chwidth = atoi(result);

	center_freq_idx = get_oper_centr_freq_seq_idx(chwidth, channel);
	if (center_freq_idx < 0) {
		free(token);
		return -1;
	}

	center_freq = get_5g_channel_freq(center_freq_idx);
	channel_freq = get_5g_channel_freq(channel);

	/* Issue the channel switch command */
	snprintf(buf, sizeof(buf),
		 " -i %s chan_switch 10 %d sec_channel_offset=1 center_freq1=%d bandwidth=%d blocktx vht",
		 ifname, channel_freq, center_freq, chwidth);
	if (run_hostapd_cli(dut,buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"hostapd_cli chan_switch failed");
	}

	free(token);
	return 0;
}


static int mac80211_ap_set_rfeature(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	const char *val;
	char *ifname;

	ifname = get_main_ifname();
	val = get_param(cmd, "chnum_band");
	if (val && mac80211_vht_chnum_band(dut, ifname, val) < 0)
		return -1;

	return 1;
}


static int cmd_ap_set_rfeature(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	/* const char *type = get_param(cmd, "Type"); */

	switch (get_driver_type()) {
	case DRIVER_ATHEROS:
		return ath_ap_set_rfeature(dut, conn, cmd);
	case DRIVER_OPENWRT:
		switch (get_openwrt_driver_type()) {
		case OPENWRT_DRIVER_ATHEROS:
			return ath_ap_set_rfeature(dut, conn, cmd);
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported ap_set_rfeature with the current openwrt driver");
			return 0;
		}
	case DRIVER_LINUX_WCN:
	case DRIVER_WCN:
		return wcn_ap_set_rfeature(dut, conn, cmd);
	case DRIVER_MAC80211:
		return mac80211_ap_set_rfeature(dut, conn, cmd);
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported ap_set_rfeature with the current driver");
		return 0;
	}
}


static int cmd_accesspoint(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	return 1;
}


void ap_register_cmds(void)
{
	sigma_dut_reg_cmd("ap_ca_version", NULL, cmd_ap_ca_version);
	sigma_dut_reg_cmd("ap_set_wireless", NULL, cmd_ap_set_wireless);
	sigma_dut_reg_cmd("ap_send_addba_req", NULL, cmd_ap_send_addba_req);
	sigma_dut_reg_cmd("ap_set_11n_wireless", NULL, cmd_ap_set_wireless);
	sigma_dut_reg_cmd("ap_set_11n", NULL, cmd_ap_set_wireless);
	sigma_dut_reg_cmd("ap_set_11d", NULL, cmd_ap_set_wireless);
	sigma_dut_reg_cmd("ap_set_11h", NULL, cmd_ap_set_wireless);
	sigma_dut_reg_cmd("ap_set_security", NULL, cmd_ap_set_security);
	sigma_dut_reg_cmd("ap_set_apqos", NULL, cmd_ap_set_apqos);
	sigma_dut_reg_cmd("ap_set_staqos", NULL, cmd_ap_set_staqos);
	sigma_dut_reg_cmd("ap_set_radius", NULL, cmd_ap_set_radius);
	sigma_dut_reg_cmd("ap_reboot", NULL, cmd_ap_reboot);
	sigma_dut_reg_cmd("ap_config_commit", NULL, cmd_ap_config_commit);
	sigma_dut_reg_cmd("ap_reset_default", NULL, cmd_ap_reset_default);
	sigma_dut_reg_cmd("ap_get_info", NULL, cmd_ap_get_info);
	sigma_dut_reg_cmd("ap_deauth_sta", NULL, cmd_ap_deauth_sta);
	sigma_dut_reg_cmd("ap_send_frame", NULL, cmd_ap_send_frame);
	sigma_dut_reg_cmd("ap_get_mac_address", NULL, cmd_ap_get_mac_address);
	sigma_dut_reg_cmd("ap_set_pmf", NULL, cmd_ap_set_pmf);
	sigma_dut_reg_cmd("ap_set_hs2", NULL, cmd_ap_set_hs2);
	sigma_dut_reg_cmd("ap_set_rfeature", NULL, cmd_ap_set_rfeature);
	sigma_dut_reg_cmd("ap_nfc_action", NULL, cmd_ap_nfc_action);
	sigma_dut_reg_cmd("ap_wps_read_pin", NULL, cmd_ap_wps_read_pin);
	sigma_dut_reg_cmd("AccessPoint", NULL, cmd_accesspoint);
}
