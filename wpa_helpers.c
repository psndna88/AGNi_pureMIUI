/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010-2011, Atheros Communications, Inc.
 * Copyright (c) 2011-2014, 2016, Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/stat.h>
#include "wpa_ctrl.h"
#include "wpa_helpers.h"


#define DEFAULT_HAPD_CTRL_PATH "/var/run/hostapd/"

extern char *sigma_wpas_ctrl;
extern char *client_socket_path;
extern char *sigma_hapd_ctrl;


const char * get_main_ifname(struct sigma_dut *dut)
{
	enum driver_type drv = get_driver_type(dut);
	enum openwrt_driver_type openwrt_drv = get_openwrt_driver_type();

	if (dut->main_ifname) {
		if (dut->use_5g && dut->main_ifname_5g)
			return dut->main_ifname_5g;
		if (!dut->use_5g && dut->main_ifname_2g)
			return dut->main_ifname_2g;
		return dut->main_ifname;
	}

	if (drv == DRIVER_ATHEROS || openwrt_drv == OPENWRT_DRIVER_ATHEROS) {
		if (if_nametoindex("ath2") > 0)
			return "ath2";
		else if (if_nametoindex("ath1") > 0)
			return "ath1";
		else
			return "ath0";
	}

	if (if_nametoindex("p2p0") > 0)
		return "p2p0";
	if (if_nametoindex("wlan1") > 0) {
		struct stat s;
		if (stat("/sys/module/mac80211", &s) == 0 &&
		    if_nametoindex("wlan0")) {
			/*
			 * Likely a dual-radio AP device; use wlan0 for STA/P2P
			 * operations.
			 */
			return "wlan0";
		}
		return "wlan1";
	}
	if (if_nametoindex("wlan0") > 0)
		return "wlan0";

	return "unknown";
}


const char * get_station_ifname(struct sigma_dut *dut)
{
	if (dut->station_ifname) {
		if (dut->use_5g && dut->station_ifname_5g)
			return dut->station_ifname_5g;
		if (!dut->use_5g && dut->station_ifname_2g)
			return dut->station_ifname_2g;
		return dut->station_ifname;
	}

	/*
	 * If we have both wlan0 and wlan1, assume the first one is the station
	 * interface.
	 */
	if (if_nametoindex("wlan1") > 0 && if_nametoindex("wlan0") > 0)
		return "wlan0";

	if (if_nametoindex("ath0") > 0)
		return "ath0";

	/* If nothing else matches, hope for best and guess.. */
	return "wlan0";
}


const char * get_p2p_ifname(struct sigma_dut *dut, const char *primary_ifname)
{
	if (strcmp(get_station_ifname(dut), primary_ifname) != 0)
		return primary_ifname;

	if (dut->p2p_ifname)
		return dut->p2p_ifname;

	return get_station_ifname(dut);
}


void dut_ifc_reset(struct sigma_dut *dut)
{
	char buf[256];
	const char *ifc = get_station_ifname(dut);

	snprintf(buf, sizeof(buf), "ifconfig %s down", ifc);
	run_system(dut, buf);
	snprintf(buf, sizeof(buf), "ifconfig %s up", ifc);
	run_system(dut, buf);
}


int wpa_ctrl_command(const char *path, const char *ifname, const char *cmd)
{
	struct wpa_ctrl *ctrl;
	char buf[128];
	size_t len;

	snprintf(buf, sizeof(buf), "%s%s", path, ifname);
	ctrl = wpa_ctrl_open2(buf, client_socket_path);
	if (ctrl == NULL) {
		printf("wpa_command: wpa_ctrl_open2(%s) failed\n", buf);
		return -1;
	}
	len = sizeof(buf);
	if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), buf, &len, NULL) < 0) {
		printf("wpa_command: wpa_ctrl_request failed\n");
		wpa_ctrl_close(ctrl);
		return -1;
	}
	wpa_ctrl_close(ctrl);
	buf[len] = '\0';
	if (strncmp(buf, "FAIL", 4) == 0) {
		printf("wpa_command: Command failed (FAIL received)\n");
		return -1;
	}
	return 0;
}


int wpa_command(const char *ifname, const char *cmd)
{
	printf("wpa_command(ifname='%s', cmd='%s')\n", ifname, cmd);
	return wpa_ctrl_command(sigma_wpas_ctrl, ifname, cmd);
}


int hapd_command(const char *ifname, const char *cmd)
{
	const char *path = sigma_hapd_ctrl ? sigma_hapd_ctrl :
		DEFAULT_HAPD_CTRL_PATH;

	printf("hapd_command(ifname='%s', cmd='%s')\n", ifname, cmd);
	return wpa_ctrl_command(path, ifname, cmd);
}


int wpa_ctrl_command_resp(const char *path, const char *ifname,
			  const char *cmd, char *resp, size_t resp_size)
{
	struct wpa_ctrl *ctrl;
	char buf[128];
	size_t len;

	snprintf(buf, sizeof(buf), "%s%s", path, ifname);
	ctrl = wpa_ctrl_open2(buf, client_socket_path);
	if (ctrl == NULL) {
		printf("wpa_command: wpa_ctrl_open2(%s) failed\n", buf);
		return -1;
	}
	len = resp_size;
	if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), resp, &len, NULL) < 0) {
		printf("wpa_command: wpa_ctrl_request failed\n");
		wpa_ctrl_close(ctrl);
		return -1;
	}
	wpa_ctrl_close(ctrl);
	resp[len] = '\0';
	return 0;
}


int wpa_command_resp(const char *ifname, const char *cmd,
		     char *resp, size_t resp_size)
{
	printf("wpa_command(ifname='%s', cmd='%s')\n", ifname, cmd);
	return wpa_ctrl_command_resp(sigma_wpas_ctrl, ifname, cmd,
				     resp, resp_size);
}


int hapd_command_resp(const char *ifname, const char *cmd,
		      char *resp, size_t resp_size)
{
	const char *path = sigma_hapd_ctrl ? sigma_hapd_ctrl :
		DEFAULT_HAPD_CTRL_PATH;

	printf("hapd_command(ifname='%s', cmd='%s')\n", ifname, cmd);
	return wpa_ctrl_command_resp(path, ifname, cmd, resp, resp_size);
}


struct wpa_ctrl * open_wpa_ctrl_mon(const char *ctrl_path, const char *ifname)
{
	struct wpa_ctrl *ctrl;
	char path[256];

	snprintf(path, sizeof(path), "%s%s", ctrl_path, ifname);
	ctrl = wpa_ctrl_open2(path, client_socket_path);
	if (ctrl == NULL)
		return NULL;
	if (wpa_ctrl_attach(ctrl) < 0) {
		wpa_ctrl_close(ctrl);
		return NULL;
	}

	return ctrl;
}


struct wpa_ctrl * open_wpa_mon(const char *ifname)
{
	return open_wpa_ctrl_mon(sigma_wpas_ctrl, ifname);
}


struct wpa_ctrl * open_hapd_mon(const char *ifname)
{
	const char *path = sigma_hapd_ctrl ?
		sigma_hapd_ctrl : DEFAULT_HAPD_CTRL_PATH;

	return open_wpa_ctrl_mon(path, ifname);
}


int get_wpa_cli_events_timeout(struct sigma_dut *dut, struct wpa_ctrl *mon,
			       const char **events, char *buf, size_t buf_size,
			       unsigned int timeout)
{
	int fd, ret;
	fd_set rfd;
	char *pos;
	struct timeval tv;
	time_t start, now;
	int i;

	for (i = 0; events[i]; i++) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Waiting for wpa_cli event: %s", events[i]);
	}
	fd = wpa_ctrl_get_fd(mon);
	if (fd < 0)
		return -1;

	if (timeout)
		time(&start);
	while (1) {
		size_t len;

		FD_ZERO(&rfd);
		FD_SET(fd, &rfd);

		if (timeout) {
			time(&now);
			if ((unsigned int) (now - start) >= timeout)
				tv.tv_sec = 1;
			else
				tv.tv_sec = timeout -
					(unsigned int) (now - start) + 1;
			tv.tv_usec = 0;
		}
		ret = select(fd + 1, &rfd, NULL, NULL, timeout ? &tv : NULL);
		if (ret == 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Timeout on "
					"waiting for events");
			return -1;
		}
		if (ret < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "select: %s",
					strerror(errno));
			return -1;
		}
		len = buf_size;
		if (wpa_ctrl_recv(mon, buf, &len) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failure while "
					"waiting for events");
			return -1;
		}
		if (len == buf_size)
			len--;
		buf[len] = '\0';

		pos = strchr(buf, '>');
		if (pos) {
			for (i = 0; events[i]; i++) {
				if (strncmp(pos + 1, events[i],
					    strlen(events[i])) == 0)
					return 0; /* Event found */
			}
		}

		if (!timeout)
			continue;

		time(&now);
		if ((unsigned int) (now - start) > timeout) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Timeout on "
					"waiting for event");
			return -1;
		}
	}
}


int get_wpa_cli_events(struct sigma_dut *dut, struct wpa_ctrl *mon,
		       const char **events, char *buf, size_t buf_size)
{
	return get_wpa_cli_events_timeout(dut, mon, events, buf, buf_size,
					  dut->default_timeout);
}


int get_wpa_cli_event2(struct sigma_dut *dut, struct wpa_ctrl *mon,
		       const char *event, const char *event2,
		       char *buf, size_t buf_size)
{
	const char *events[3] = { event, event2, NULL };
	return get_wpa_cli_events(dut, mon, events, buf, buf_size);
}


int get_wpa_cli_event(struct sigma_dut *dut, struct wpa_ctrl *mon,
		      const char *event, char *buf, size_t buf_size)
{
	return get_wpa_cli_event2(dut, mon, event, NULL, buf, buf_size);
}


/*
 * signal_poll cmd output sample
 * RSSI=-51
 * LINKSPEED=866
 * NOISE=-101
 * FREQUENCY=5180
 * AVG_RSSI=-50
 */
int get_wpa_signal_poll(struct sigma_dut *dut, const char *ifname,
			const char *field, char *obuf, size_t obuf_size)
{
	struct wpa_ctrl *ctrl;
	char buf[4096];
	char *pos, *end;
	size_t len, flen;

	snprintf(buf, sizeof(buf), "%s%s", sigma_wpas_ctrl, ifname);
	ctrl = wpa_ctrl_open2(buf, client_socket_path);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to connect to wpa_supplicant");
		return -1;
	}

	len = sizeof(buf);
	if (wpa_ctrl_request(ctrl, "SIGNAL_POLL", 11, buf, &len, NULL) < 0) {
		wpa_ctrl_close(ctrl);
		sigma_dut_print(dut, DUT_MSG_ERROR, "ctrl request failed");
		return -1;
	}
	buf[len] = '\0';

	wpa_ctrl_close(ctrl);

	flen = strlen(field);
	pos = buf;
	while (pos + flen < buf + len) {
		if (pos > buf) {
			if (*pos != '\n') {
				pos++;
				continue;
			}
			pos++;
		}
		if (strncmp(pos, field, flen) != 0 || pos[flen] != '=') {
			pos++;
			continue;
		}
		pos += flen + 1;
		end = strchr(pos, '\n');
		if (!end) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Could not find signal poll field '%s' - end is NULL",
					field);
			return -1;
		}
		*end++ = '\0';
		if (end - pos > (int) obuf_size) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"signal poll out buffer is too small");
			return -1;
		}
		memcpy(obuf, pos, end - pos);
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_ERROR, "signal poll param not found");
	return -1;
}


int get_wpa_ssid_bssid(struct sigma_dut *dut, const char *ifname,
		       char *buf, size_t buf_size)
{
	struct wpa_ctrl *ctrl;
	char buf_local[4096];
	char *network, *ssid, *bssid;
	size_t buf_size_local;
	unsigned int count = 0;
	int len, res;
	char *save_ptr_network = NULL;

	ctrl = open_wpa_mon(ifname);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to connect to wpa_supplicant");
		return -1;
	}

	wpa_command(ifname, "BSS_FLUSH");
	if (wpa_command(ifname, "SCAN TYPE=ONLY")) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		sigma_dut_print(dut, DUT_MSG_ERROR, "SCAN command failed");
		return -1;
	}

	res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-SCAN-RESULTS",
				buf_local, sizeof(buf_local));
	wpa_ctrl_detach(ctrl);
	buf_size_local = sizeof(buf_local);
	if (res < 0 || wpa_ctrl_request(ctrl, "BSS RANGE=ALL MASK=0x1002", 25,
					buf_local, &buf_size_local, NULL) < 0) {
		wpa_ctrl_close(ctrl);
		sigma_dut_print(dut, DUT_MSG_ERROR, "BSS ctrl request failed");
		return -1;
	}
	buf_local[buf_size_local] = '\0';

	wpa_ctrl_close(ctrl);

	/* Below is BSS RANGE=ALL MASK=0x1002 command sample output which is
	 * parsed to get the BSSID and SSID parameters.
	 * Even number of lines, first line BSSID of network 1, second line SSID
	 * of network 1, ...
	 *
	 * bssid=xx:xx:xx:xx:xx:x1
	 * ssid=SSID1
	 * bssid=xx:xx:xx:xx:xx:x2
	 * ssid=SSID2
	 */

	network = strtok_r(buf_local, "\n", &save_ptr_network);

	while (network) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "BSSID: %s", network);
		bssid = NULL;
		if (!strtok_r(network, "=", &bssid)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Invalid BSS result: BSSID not found");
			return -1;
		}
		network = strtok_r(NULL, "\n", &save_ptr_network);
		if (network) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "SSID: %s",
					network);
			ssid = NULL;
			if (!strtok_r(network, "=", &ssid)) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Invalid BSS result: SSID is null");
				return -1;
			}
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Invalid BSS result: SSID not found");
			return -1;
		}

		/* Skip comma for first entry */
		count++;
		len = snprintf(buf, buf_size, "%sSSID%d,%s,BSSID%d,%s",
			       count > 1 ? "," : "",
			       count, ssid, count, bssid);
		if (len < 0 || (size_t) len >= buf_size) {
			buf[0] = '\0';
			return 0;
		}

		buf_size -= len;
		buf += len;

		network = strtok_r(NULL, "\n", &save_ptr_network);
	}

	return 0;
}


static int get_wpa_ctrl_status_field(const char *path, const char *ifname,
				     const char *cmd, const char *field,
				     char *obuf, size_t obuf_size)
{
	struct wpa_ctrl *ctrl;
	char buf[4096];
	char *pos, *end;
	size_t len, flen;

	snprintf(buf, sizeof(buf), "%s%s", path, ifname);
	ctrl = wpa_ctrl_open2(buf, client_socket_path);
	if (ctrl == NULL)
		return -1;
	len = sizeof(buf);
	if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), buf, &len, NULL) < 0) {
		wpa_ctrl_close(ctrl);
		return -1;
	}
	wpa_ctrl_close(ctrl);
	buf[len] = '\0';

	flen = strlen(field);
	pos = buf;
	while (pos + flen < buf + len) {
		if (pos > buf) {
			if (*pos != '\n') {
				pos++;
				continue;
			}
			pos++;
		}
		if (strncmp(pos, field, flen) != 0 || pos[flen] != '=') {
			pos++;
			continue;
		}
		pos += flen + 1;
		end = strchr(pos, '\n');
		if (end == NULL)
			return -1;
		*end++ = '\0';
		if (end - pos > (int) obuf_size)
			return -1;
		memcpy(obuf, pos, end - pos);
		return 0;
	}

	return -1;
}


int get_wpa_status(const char *ifname, const char *field, char *obuf,
		   size_t obuf_size)
{
	return get_wpa_ctrl_status_field(sigma_wpas_ctrl, ifname, "STATUS",
					 field, obuf, obuf_size);
}


int get_hapd_config(const char *ifname, const char *field, char *obuf,
		    size_t obuf_size)
{
	const char *path = sigma_hapd_ctrl ?
		sigma_hapd_ctrl : DEFAULT_HAPD_CTRL_PATH;

	return get_wpa_ctrl_status_field(path, ifname, "GET_CONFIG",
					 field, obuf, obuf_size);
}


int wait_ip_addr(struct sigma_dut *dut, const char *ifname, int timeout)
{
	char ip[30];
	int count = timeout;

	while (count > 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "%s: ifname='%s' - %d "
				"seconds remaining",
				__func__, ifname, count);
		count--;
		if (get_wpa_status(ifname, "ip_address", ip, sizeof(ip)) == 0
		    && strlen(ip) > 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "IP address "
					"found: '%s'", ip);
			return 0;
		}
		sleep(1);
	}
	sigma_dut_print(dut, DUT_MSG_INFO, "%s: Could not get IP address for "
			"ifname='%s'", __func__, ifname);
	return -1;
}


void remove_wpa_networks(const char *ifname)
{
	char buf[4096];
	char cmd[256];
	char *pos;

	if (wpa_command_resp(ifname, "LIST_NETWORKS", buf, sizeof(buf)) < 0)
		return;

	/* Skip the first line (header) */
	pos = strchr(buf, '\n');
	if (pos == NULL)
		return;
	pos++;
	while (pos && pos[0]) {
		int id = atoi(pos);
		snprintf(cmd, sizeof(cmd), "REMOVE_NETWORK %d", id);
		wpa_command(ifname, cmd);
		pos = strchr(pos, '\n');
		if (pos)
			pos++;
	}
}


int add_network(const char *ifname)
{
	char res[30];

	if (wpa_command_resp(ifname, "ADD_NETWORK", res, sizeof(res)) < 0)
		return -1;
	return atoi(res);
}


int set_network(const char *ifname, int id, const char *field,
		const char *value)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "SET_NETWORK %d %s %s", id, field, value);
	return wpa_command(ifname, buf);
}


int set_network_quoted(const char *ifname, int id, const char *field,
		       const char *value)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "SET_NETWORK %d %s \"%s\"",
		 id, field, value);
	return wpa_command(ifname, buf);
}


int add_cred(const char *ifname)
{
	char res[30];

	if (wpa_command_resp(ifname, "ADD_CRED", res, sizeof(res)) < 0)
		return -1;
	return atoi(res);
}


int set_cred(const char *ifname, int id, const char *field, const char *value)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "SET_CRED %d %s %s", id, field, value);
	return wpa_command(ifname, buf);
}


int set_cred_quoted(const char *ifname, int id, const char *field,
		    const char *value)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "SET_CRED %d %s \"%s\"",
		 id, field, value);
	return wpa_command(ifname, buf);
}


const char * concat_sigma_tmpdir(struct sigma_dut *dut, const char *src,
				 char *dst, size_t len)
{
	snprintf(dst, len, "%s%s", dut->sigma_tmpdir, src);

	return dst;
}


int start_sta_mode(struct sigma_dut *dut)
{
	FILE *f;
	char buf[256];
	char sta_conf_path[100];
	const char *ifname;
	char *tmp, *pos;

	if (dut->mode == SIGMA_MODE_STATION) {
		if ((dut->use_5g && dut->sta_2g_started) ||
		    (!dut->use_5g && dut->sta_5g_started)) {
			stop_sta_mode(dut);
			sleep(1);
		} else {
			return 0;
		}
	}

	if (dut->mode == SIGMA_MODE_AP) {
		if (system("killall hostapd") == 0) {
			int i;

			/* Wait some time to allow hostapd to complete cleanup
			 * before starting a new process */
			for (i = 0; i < 10; i++) {
				usleep(500000);
				if (system("pidof hostapd") != 0)
					break;
			}
		}
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

	dut->mode = SIGMA_MODE_STATION;

	ifname = get_main_ifname(dut);
	if (wpa_command(ifname, "PING") == 0)
		return 0; /* wpa_supplicant is already running */

	/* Start wpa_supplicant */
	f = fopen(concat_sigma_tmpdir(dut, "/sigma_dut-sta.conf", sta_conf_path,
				      sizeof(sta_conf_path)), "w");
	if (f == NULL)
		return -1;

	tmp = strdup(sigma_wpas_ctrl);
	if (tmp == NULL) {
		fclose(f);
		return -1;
	}
	pos = tmp;
	while (pos[0] != '\0' && pos[1] != '\0')
		pos++;
	if (*pos == '/')
		*pos = '\0';
	fprintf(f, "ctrl_interface=%s\n", tmp);
	free(tmp);
	fprintf(f, "device_name=Test client\n");
	fprintf(f, "device_type=1-0050F204-1\n");
	if (is_60g_sigma_dut(dut)) {
		fprintf(f, "eapol_version=2\n");
		fprintf(f,
			"config_methods=display push_button keypad virtual_display physical_display virtual_push_button\n");
	}
	fclose(f);

#ifdef  __QNXNTO__
	snprintf(buf, sizeof(buf),
		 "wpa_supplicant -Dqca -i%s -B %s%s%s -c %s/sigma_dut-sta.conf",
		 ifname,
		 dut->wpa_supplicant_debug_log ? "-K -t -ddd " : "",
		 (dut->wpa_supplicant_debug_log &&
		  dut->wpa_supplicant_debug_log[0]) ? "-f " : "",
		 dut->wpa_supplicant_debug_log ?
		 dut->wpa_supplicant_debug_log : "",
		 dut->sigma_tmpdir);
#else /*__QNXNTO__*/
	snprintf(buf, sizeof(buf),
		 "%swpa_supplicant -Dnl80211 -i%s -B %s%s%s -c %s/sigma_dut-sta.conf",
		 file_exists("wpa_supplicant") ? "./" : "",
		 ifname,
		 dut->wpa_supplicant_debug_log ? "-K -t -ddd " : "",
		 (dut->wpa_supplicant_debug_log &&
		  dut->wpa_supplicant_debug_log[0]) ? "-f " : "",
		 dut->wpa_supplicant_debug_log ?
		 dut->wpa_supplicant_debug_log : "",
		 dut->sigma_tmpdir);
#endif /*__QNXNTO__*/
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to run '%s'", buf);
		return -1;
	}

	sleep(1);

	if (wpa_command(ifname, "PING")) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to communicate "
				"with wpa_supplicant");
		return -1;
	}
	if (dut->use_5g)
		dut->sta_5g_started = 1;
	else
		dut->sta_2g_started = 1;

	return 0;
}


void stop_sta_mode(struct sigma_dut *dut)
{
	if (is_60g_sigma_dut(dut)) {
		wpa_command(get_main_ifname(dut), "TERMINATE");
		return;
	}

	wpa_command("wlan0", "TERMINATE");
	wpa_command("wlan1", "TERMINATE");
	wpa_command("ath0", "TERMINATE");
	wpa_command("ath1", "TERMINATE");
	if (dut->main_ifname_2g)
		wpa_command(dut->main_ifname_2g, "TERMINATE");
	if (dut->main_ifname_5g)
		wpa_command(dut->main_ifname_5g, "TERMINATE");
	if (dut->station_ifname_2g)
		wpa_command(dut->station_ifname_2g, "TERMINATE");
	if (dut->station_ifname_5g)
		wpa_command(dut->station_ifname_5g, "TERMINATE");
	dut->sta_2g_started = 0;
	dut->sta_5g_started = 0;
}
