/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 * Copyright (c) 2011-2013, 2016-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>

#include "wpa_helpers.h"

#ifdef ANDROID
#define SHELL "/system/bin/sh"
#else /* ANDROID */
#define SHELL "/bin/sh"
#endif /* ANDROID */


static int cmd_traffic_send_ping(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *dst, *val;
	int size, dur, pkts;
	int id;
	char resp[100];
	float interval;
	double rate;
	FILE *f;
	char buf[100];
	int type = 1;
	int dscp = 0, use_dscp = 0;
	char extra[100], int_arg[100], intf_arg[100], ip_dst[100];

	val = get_param(cmd, "Type");
	if (!val)
		val = get_param(cmd, "IPType");
	if (val)
		type = atoi(val);
	if (type != 1 && type != 2) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported address type");
		return 0;
	}

	dst = get_param(cmd, "destination");
	if (dst == NULL || (type == 1 && !is_ip_addr(dst)) ||
	    (type == 2 && !is_ipv6_addr(dst)))
		return -1;
	if (dut->ndp_enable && type == 2) {
		snprintf(ip_dst, sizeof(ip_dst), "%s%%nan0", dst);
		dst = ip_dst;
	}

	val = get_param(cmd, "frameSize");
	if (val == NULL)
		return -1;
	size = atoi(val);

	val = get_param(cmd, "frameRate");
	if (val == NULL)
		return -1;
	rate = atof(val);
	if (rate <= 0)
		return -1;

	val = get_param(cmd, "duration");
	if (val == NULL)
		return -1;
	dur = atoi(val);
	if (dur <= 0 || dur > 3600)
		dur = 3600;

	pkts = dur * rate;
	interval = (float) 1 / rate;
	if (interval > 100000)
		return -1;

	val = get_param(cmd, "DSCP");
	if (val) {
		dscp = atoi(val);
		if (dscp < 0 || dscp > 63) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Invalid DSCP value");
			return 0;
		}
		use_dscp = 1;
	}

	id = dut->next_streamid++;
	snprintf(buf, sizeof(buf), SIGMA_TMPDIR "/sigma_dut-ping.%d", id);
	unlink(buf);
	snprintf(buf, sizeof(buf), SIGMA_TMPDIR "/sigma_dut-ping-pid.%d", id);
	unlink(buf);

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Send ping: pkts=%d interval=%f "
			"streamid=%d",
			pkts, interval, id);

	f = fopen(SIGMA_TMPDIR "/sigma_dut-ping.sh", "w");
	if (f == NULL)
		return -2;

	extra[0] = '\0';
	if (use_dscp) {
		snprintf(extra, sizeof(extra), " -Q 0x%02x",
			 dscp << 2);
	}

	int_arg[0] = '\0';
	if (rate != 1)
		snprintf(int_arg, sizeof(int_arg), " -i %f", interval);
	if (!dut->ndp_enable && type == 2)
		snprintf(intf_arg, sizeof(intf_arg), " -I %s",
			 get_station_ifname());
	else
		intf_arg[0] = '\0';
	fprintf(f, "#!" SHELL "\n"
		"ping%s -c %d%s -s %d%s -q%s %s > " SIGMA_TMPDIR
		"/sigma_dut-ping.%d &\n"
		"echo $! > " SIGMA_TMPDIR "/sigma_dut-ping-pid.%d\n",
		type == 2 ? "6" : "", pkts, int_arg, size, extra,
		intf_arg, dst, id, id);

	fclose(f);
	if (chmod(SIGMA_TMPDIR "/sigma_dut-ping.sh",
		  S_IRUSR | S_IWUSR | S_IXUSR) < 0)
		return -2;

	if (system(SIGMA_TMPDIR "/sigma_dut-ping.sh") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to start ping");
		return -2;
	}

	unlink(SIGMA_TMPDIR "/sigma_dut-ping.sh");

	snprintf(resp, sizeof(resp), "streamID,%d", id);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static int cmd_traffic_stop_ping(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *val;
	int id, pid;
	FILE *f;
	char buf[100];
	int res_found = 0, sent = 0, received = 0;

	val = get_param(cmd, "streamID");
	if (val == NULL)
		return -1;
	id = atoi(val);

	snprintf(buf, sizeof(buf), SIGMA_TMPDIR "/sigma_dut-ping-pid.%d", id);
	f = fopen(buf, "r");
	if (f == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unknown streamID");
		return 0;
	}
	if (fscanf(f, "%d", &pid) != 1 || pid <= 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "No PID for ping process");
		fclose(f);
		unlink(buf);
		return -2;
	}

	fclose(f);
	unlink(buf);

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Ping process pid %d", pid);
	if (kill(pid, SIGINT) < 0 && errno != ESRCH) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "kill failed: %s",
				strerror(errno));
	}
	usleep(250000);

	snprintf(buf, sizeof(buf), SIGMA_TMPDIR "/sigma_dut-ping.%d", id);
	f = fopen(buf, "r");
	if (f == NULL) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"No ping result file found");
		send_resp(dut, conn, SIGMA_COMPLETE, "sent,0,replies,0");
		return 0;
	}

	while (fgets(buf, sizeof(buf), f)) {
		char *pos;

		pos = strstr(buf, " packets transmitted");
		if (pos) {
			pos--;
			while (pos > buf && isdigit(pos[-1]))
				pos--;
			sent = atoi(pos);
			res_found = 1;
		}

		pos = strstr(buf, " packets received");
		if (pos == NULL)
			pos = strstr(buf, " received");
		if (pos) {
			pos--;
			while (pos > buf && isdigit(pos[-1]))
				pos--;
			received = atoi(pos);
			res_found = 1;
		}
	}
	fclose(f);
	snprintf(buf, sizeof(buf), SIGMA_TMPDIR "/sigma_dut-ping.%d", id);
	unlink(buf);

	if (!res_found) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"No ping results found");
		send_resp(dut, conn, SIGMA_COMPLETE, "sent,0,replies,0");
		return 0;
	}

	snprintf(buf, sizeof(buf), "sent,%d,replies,%d", sent, received);
	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return 0;
}


static int cmd_traffic_start_iperf(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	const char *val, *dst;
	const char *iptype;
	int port, duration;
	const char *proto;
	char buf[256];
	const char *ifname;
	char port_str[20];
	FILE *f;
	int server, ipv6 = 0;

	val = get_param(cmd, "mode");
	if (!val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing mode parameter");
		return 0;
	}
	server = strcasecmp(val, "server") == 0;

	iptype = "";
	val = get_param(cmd, "iptype");
	if (val) {
		if (strcasecmp(val, "ipv6") == 0) {
			iptype = "-6";
			ipv6 = 1;
		} else {
			iptype = "-4";
			ipv6 = 0;
		}
	}

	port_str[0] = '\0';
	val = get_param(cmd, "port");
	if (val) {
		port = atoi(val);
		snprintf(port_str, sizeof(port_str), "-p %d", port);
	}

	proto = "";
	val = get_param(cmd, "transproto");
	if (val && strcasecmp(val, "udp") == 0)
		proto = "-u";

	dst = get_param(cmd, "destination");
	if (!server && (!dst || (!is_ip_addr(dst) && !is_ipv6_addr(dst)))) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Invalid destination address");
		return 0;
	}

	if (dut->ndpe)
		ifname = "nan0";
	else
		ifname = get_station_ifname();

	val = get_param(cmd, "duration");
	if (val)
		duration = atoi(val);
	else
		duration = 0;

	unlink(SIGMA_TMPDIR "/sigma_dut-iperf");
	unlink(SIGMA_TMPDIR "/sigma_dut-iperf-pid");

	f = fopen(SIGMA_TMPDIR "/sigma_dut-iperf.sh", "w");
	if (!f) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Can not write sigma_dut-iperf.sh");
		return 0;
	}

	if (server) {
		/* write server side command to shell file */
		fprintf(f, "#!" SHELL "\n"
			"iperf3 -s %s %s > " SIGMA_TMPDIR
			"/sigma_dut-iperf &\n"
			"echo $! > " SIGMA_TMPDIR "/sigma_dut-iperf-pid\n",
			port_str, iptype);
	} else {
		/* write client side command to shell file */
		if (!dst)
			return -1;
		if (ipv6)
			snprintf(buf, sizeof(buf), "%s%%%s", dst, ifname);
		else
			snprintf(buf, sizeof(buf), "%s", dst);
		fprintf(f, "#!" SHELL "\n"
			"iperf3 -c %s -t %d %s %s %s > " SIGMA_TMPDIR
			"/sigma_dut-iperf &\n"
			"echo $! > " SIGMA_TMPDIR "/sigma_dut-iperf-pid\n",
			buf, duration, iptype, proto, port_str);
	}

	fclose(f);

	if (chmod(SIGMA_TMPDIR "/sigma_dut-iperf.sh",
		  S_IRUSR | S_IWUSR | S_IXUSR) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Can not chmod sigma_dut-iperf.sh");
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Starting iperf");
	if (system(SIGMA_TMPDIR "/sigma_dut-iperf.sh") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to start iperf");
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to run sigma_dut-iperf.sh");
		return 0;
	}

	unlink(SIGMA_TMPDIR "/sigma_dut-iperf.sh");
	return 1;
}


static int cmd_traffic_stop_iperf(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	int pid;
	FILE *f;
	char buf[100], summary_buf[100];
	float bandwidth, totalbytes, factor;
	char *pos;
	long l_bandwidth, l_totalbytes;

	f = fopen(SIGMA_TMPDIR "/sigma_dut-iperf-pid", "r");
	if (!f) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,PID file does not exist");
		return 0;
	}
	if (fscanf(f, "%d", &pid) != 1 || pid <= 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "No PID for iperf process");
		fclose(f);
		unlink(SIGMA_TMPDIR "/sigma_dut-iperf-pid");
		return -2;
	}

	fclose(f);
	unlink(SIGMA_TMPDIR "/sigma_dut-iperf-pid");

	if (kill(pid, SIGINT) < 0 && errno != ESRCH) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "kill failed: %s",
				strerror(errno));
	}
	usleep(250000);

	/* parse iperf output which is stored in sigma_dut-iperf */
	f = fopen(SIGMA_TMPDIR "/sigma_dut-iperf", "r");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"No iperf result file found");
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "bandwidth,0,totalbytes,0");
		return 0;
	}

	/* find the last line which has the received bytes summary */
	while (fgets(buf, sizeof(buf), f)) {
		char *pos;

		pos = strchr(buf, '\n');
		if (pos)
			*pos = '\0';
		sigma_dut_print(dut, DUT_MSG_DEBUG, "iperf: %s", buf);
		pos = strstr(buf, "  sec  ");
		if (pos)
			strlcpy(summary_buf, buf, sizeof(summary_buf));
	}

	fclose(f);
	unlink(SIGMA_TMPDIR "/sigma_dut-iperf");

	pos = strstr(summary_buf, "Bytes");
	if (!pos || pos == summary_buf) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Can not parse iperf results");
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "bandwidth,0,totalbytes,0");
		return 0;
	}

	if (pos[-1] == 'G')
		factor = 1024 * 1024 * 1024;
	else if (pos[-1] == 'M')
		factor = 1024 * 1024;
	else if (pos[-1] == 'K')
		factor = 1024;
	else
		factor = 1;

	if (pos) {
		pos -= 2;
		while (pos > summary_buf && (pos[-1] != ' '))
			pos--;
		totalbytes = atof(pos);
	} else
		totalbytes = 0;
	l_totalbytes = totalbytes * factor;

	pos = strstr(summary_buf, "bits/sec");
	if (!pos || pos == summary_buf) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Can not parse iperf results");
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "bandwidth,0,totalbytes,0");
		return 0;
	}

	if (pos[-1] == 'G')
		factor = 1024 * 1024 * 1024 / 8;
	else if (pos[-1] == 'M')
		factor = 1024 * 1024 / 8;
	else if (pos[-1] == 'K')
		factor = 1024 / 8;
	else
		factor = 1 / 8;

	if (pos && pos - summary_buf > 2) {
		pos -= 2;
		while (pos > summary_buf && (pos[-1] != ' '))
			pos--;
		bandwidth = atof(pos);
	} else
		bandwidth = 0;
	l_bandwidth = bandwidth * factor;

	snprintf(buf, sizeof(buf), "bandwidth,%lu,totalbytes,%lu",
		 l_bandwidth, l_totalbytes);
	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return 0;
}


void traffic_register_cmds(void)
{
	sigma_dut_reg_cmd("traffic_send_ping", NULL, cmd_traffic_send_ping);
	sigma_dut_reg_cmd("traffic_stop_ping", NULL, cmd_traffic_stop_ping);
	sigma_dut_reg_cmd("traffic_start_iperf", NULL, cmd_traffic_start_iperf);
	sigma_dut_reg_cmd("traffic_stop_iperf", NULL, cmd_traffic_stop_iperf);
}
