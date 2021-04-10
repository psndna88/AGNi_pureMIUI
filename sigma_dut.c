/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010-2011, Atheros Communications, Inc.
 * Copyright (c) 2011-2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2019, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#ifdef __linux__
#include <signal.h>
#include <netinet/tcp.h>
#endif /* __linux__ */
#include "wpa_ctrl.h"
#include "wpa_helpers.h"
#include "miracast.h"

#define SIGMA_DUT_PORT 9000
#define MAX_CONNECTIONS 4

extern enum driver_type wifi_chip_type;

static struct sigma_dut sigma_dut;

char *sigma_radio_ifname[MAX_RADIO] = {};
char *sigma_wpas_ctrl = "/var/run/wpa_supplicant/";
char *sigma_hapd_ctrl = NULL;
char *client_socket_path = NULL;
char *ap_inet_addr = "192.168.43.1";
char *ap_inet_mask = "255.255.255.0";
char *sigma_cert_path = "/etc/wpa_supplicant";

/* For WMM-AC testing this set to 1 through argument,
 * otherwise default WMM-PS 0 */
int sigma_wmm_ac = 0;

/* For VO-Enterprise testing set this to 1 through argument
 * to send periodic data.
 */
int sigma_periodic_data = 0;

#ifdef ANDROID
#include <android/log.h>

#ifdef ANDROID_WIFI_HAL

static void * wifi_hal_event_thread(void *ptr)
{
	struct sigma_dut *dut = ptr;

	wifi_event_loop(dut->wifi_hal_handle);
	pthread_exit(0);

	return NULL;
}


int wifi_hal_initialize(struct sigma_dut *dut)
{
	pthread_t thread1;
	wifi_error err;
	const char *ifname;

	if (dut->wifi_hal_initialized)
		return 0;

	err = wifi_initialize(&dut->wifi_hal_handle);
	if (err) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"wifi hal initialize failed");
		return -1;
	}

	if (if_nametoindex(NAN_AWARE_IFACE))
		ifname = NAN_AWARE_IFACE;
	else
		ifname = "wlan0";

	dut->wifi_hal_iface_handle = wifi_get_iface_handle(dut->wifi_hal_handle,
							   (char *) ifname);

	pthread_create(&thread1, NULL, &wifi_hal_event_thread, (void *) dut);
	dut->wifi_hal_initialized = true;

	return 0;
}

#endif /* ANDROID_WIFI_HAL */


static enum android_LogPriority level_to_android_priority(int level)
{
	switch (level) {
	case DUT_MSG_ERROR:
		return ANDROID_LOG_ERROR;
	case DUT_MSG_INFO:
		return ANDROID_LOG_INFO;
	case DUT_MSG_DEBUG:
		return ANDROID_LOG_DEBUG;
	default:
		return ANDROID_LOG_VERBOSE;
	}
}

#endif /* ANDROID */


void sigma_dut_print(struct sigma_dut *dut, int level, const char *fmt, ...)
{
	va_list ap;
	struct timeval tv;

	if (level < dut->debug_level)
		return;

	gettimeofday(&tv, NULL);
#ifdef ANDROID
	va_start(ap, fmt);
	__android_log_vprint(level_to_android_priority(level),
			     "sigma_dut", fmt, ap);
	va_end(ap);
	if (!dut->stdout_debug)
		return;
#else /* ANDROID */
	if (dut->log_file_fd) {
		va_start(ap, fmt);
		fprintf(dut->log_file_fd, "%ld.%06u: ",
			(long) tv.tv_sec, (unsigned int) tv.tv_usec);
		vfprintf(dut->log_file_fd, fmt, ap);
		fprintf(dut->log_file_fd, "\n");
		va_end(ap);
	}
#endif /* ANDROID */

	va_start(ap, fmt);
	printf("%ld.%06u: ", (long) tv.tv_sec,
	       (unsigned int) tv.tv_usec);
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}


void sigma_dut_summary(struct sigma_dut *dut, const char *fmt, ...)
{
	va_list ap;
	FILE *f;

	if (!dut->summary_log)
		return;

	f = fopen(dut->summary_log, "a");
	if (f == NULL)
		return;

	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	fprintf(f, "\n");
	va_end(ap);
	fclose(f);
}


int sigma_dut_reg_cmd(const char *cmd,
		      int (*validate)(struct sigma_cmd *cmd),
		      enum sigma_cmd_result (*process)(struct sigma_dut *dut,
						       struct sigma_conn *conn,
						       struct sigma_cmd *cmd))
{
	struct sigma_cmd_handler *h;
	size_t clen, len;

	for (h = sigma_dut.cmds; h; h = h->next) {
		if (strcmp(h->cmd, cmd) == 0) {
			printf("ERROR: Duplicate sigma_dut command registration for '%s'\n",
			       cmd);
			return -1;
		}
	}

	clen = strlen(cmd);
	len = sizeof(*h) + clen + 1;
	h = malloc(len);
	if (h == NULL)
		return -1;
	memset(h, 0, len);
	h->cmd = (char *) (h + 1); /* include in same allocation */
	memcpy(h->cmd, cmd, clen);
	h->validate = validate;
	h->process= process;

	h->next = sigma_dut.cmds;
	sigma_dut.cmds = h;

	return 0;
}


static void sigma_dut_unreg_cmds(struct sigma_dut *dut)
{
	struct sigma_cmd_handler *cmd, *prev;
	cmd = dut->cmds;
	dut->cmds = NULL;
	while (cmd) {
		prev = cmd;
		cmd = cmd->next;
		free(prev);
	}
}


static int open_socket(struct sigma_dut *dut, int port)
{
	struct sockaddr_in addr;
#ifndef __QNXNTO__
	int val;
#endif /* !__QNXNTO__ */

#ifdef __QNXNTO__
	dut->s = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
#else /* __QNXNTO__ */
	dut->s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif /* __QNXNTO__ */
	if (dut->s < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "socket: %s",
				strerror(errno));
		return -1;
	}

#ifndef __QNXNTO__
	val = 1;
	if (setsockopt(dut->s, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) <
	    0)
		sigma_dut_print(dut, DUT_MSG_INFO, "setsockopt SO_REUSEADDR: "
				"%s", strerror(errno));
#endif /* !__QNXNTO__ */

#ifdef __linux__
	val = 1;
	if (setsockopt(dut->s, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) <
	    0)
		sigma_dut_print(dut, DUT_MSG_INFO, "setsockopt TCP_NODELAY: "
				"%s", strerror(errno));
#endif /* __linux__ */

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (bind(dut->s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "bind: %s",
				strerror(errno));
		goto fail;
	}

	if (listen(dut->s, 5) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "listen: %s",
				strerror(errno));
		goto fail;
	}

	return 0;

fail:
	shutdown(dut->s, SHUT_RDWR);
	close(dut->s);
	dut->s = -1;
	return -1;
}


static void close_socket(struct sigma_dut *dut)
{
	shutdown(dut->s, SHUT_RDWR);
	close(dut->s);
	dut->s = -1;
}


void send_resp(struct sigma_dut *dut, struct sigma_conn *conn,
	       enum sigma_status status, const char *buf)
{
	struct msghdr msg;
	struct iovec iov[4];
	size_t elems;

	if (!conn)
		return;

	sigma_dut_print(dut, DUT_MSG_INFO, "resp: status=%d buf=%s",
			status, buf ? buf : "N/A");

	iov[0].iov_base = "status,";
	iov[0].iov_len = 7;
	switch (status) {
	case SIGMA_RUNNING:
		iov[1].iov_base = "RUNNING,";
		iov[1].iov_len = 8;
		break;
	case SIGMA_INVALID:
		iov[1].iov_base = "INVALID,";
		iov[1].iov_len = 8;
		break;
	case SIGMA_ERROR:
		iov[1].iov_base = "ERROR,";
		iov[1].iov_len = 6;
		break;
	case SIGMA_COMPLETE:
		iov[1].iov_base = "COMPLETE,";
		iov[1].iov_len = 9;
		break;
	}
	if (status != SIGMA_RUNNING) {
		sigma_dut_summary(dut, "CAPI resp: status,%s%s",
				  (char *) iov[1].iov_base, buf ? buf : "");
	}
	if (buf) {
		iov[2].iov_base = (void *) buf;
		iov[2].iov_len = strlen(buf);
		iov[3].iov_base = "\r\n";
		iov[3].iov_len = 2;
		elems = 4;
	} else {
		iov[1].iov_len--;
		iov[2].iov_base = "\r\n";
		iov[2].iov_len = 2;
		elems = 3;
	}

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = iov;
	msg.msg_iovlen = elems;
	if (sendmsg(conn->s, &msg, 0) < 0)
		sigma_dut_print(dut, DUT_MSG_INFO, "sendmsg: %s",
				strerror(errno));
	dut->response_sent++;
}


const char * get_param(struct sigma_cmd *cmd, const char *name)
{
	int i;
	for (i = 0; i < cmd->count; i++) {
		if (strcasecmp(name, cmd->params[i]) == 0)
			return cmd->values[i];
	}
	return NULL;
}


const char * get_param_indexed(struct sigma_cmd *cmd, const char *name,
			       int index)
{
	int i, j;

	for (i = 0, j = 0; i < cmd->count; i++) {
		if (strcasecmp(name, cmd->params[i]) == 0) {
			j++;
			if (j > index)
				return cmd->values[i];
		}
	}

	return NULL;
}


static void process_cmd(struct sigma_dut *dut, struct sigma_conn *conn,
			char *buf)
{
	struct sigma_cmd_handler *h;
	struct sigma_cmd c;
	char *cmd, *pos, *pos2;
	int len;
	char txt[300];
	enum sigma_cmd_result res;

	while (*buf == '\r' || *buf == '\n' || *buf == '\t' || *buf == ' ')
		buf++;
	len = strlen(buf);
	while (len > 0 && buf[len - 1] == ' ') {
		buf[len - 1] = '\0';
		len--;
	}

	if (dut->debug_level < DUT_MSG_INFO) {
		pos = strchr(buf, ',');
		if (pos == NULL)
			pos = buf + len;
		if (pos - buf > 50)
			pos = buf + 50;
		memcpy(txt, "/====[ ", 7);
		pos2 = txt + 7;
		memcpy(pos2, buf, pos - buf);
		pos2 += pos - buf;
		*pos2++ = ' ';
		*pos2++ = ']';
		while (pos2 - txt < 70)
			*pos2++ = '=';
		*pos2++ = '\\';
		*pos2 = '\0';
		printf("\n%s\n\n", txt);
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "cmd: %s", buf);
	sigma_dut_summary(dut, "CAPI cmd: %s", buf);
	snprintf(txt, sizeof(txt), "NOTE CAPI:%s", buf);
	txt[sizeof(txt) - 1] = '\0';
	wpa_command(get_main_ifname(dut), txt);

	memset(&c, 0, sizeof(c));
	cmd = buf;
	pos = strchr(cmd, ',');
	if (pos) {
		*pos++ = '\0';
		if (strcasecmp(cmd, "AccessPoint") == 0 ||
		    strcasecmp(cmd, "PowerSwitch") == 0) {
			pos2 = strchr(pos, ',');
			if (pos2 == NULL)
				goto invalid_params;
			c.params[c.count] = pos;
			c.values[c.count] = pos2;
			c.count++;
			pos = strchr(pos2, ',');
			if (pos)
				*pos++ = '\0';
		}
		while (pos) {
			pos2 = strchr(pos, ',');
			if (pos2 == NULL)
				goto invalid_params;
			*pos2++ = '\0';
			if (c.count == MAX_PARAMS) {
				sigma_dut_print(dut, DUT_MSG_INFO, "Too many "
						"parameters");
				goto invalid_params;
			}
			c.params[c.count] = pos;
			c.values[c.count] = pos2;
			c.count++;
			pos = strchr(pos2, ',');
			if (pos)
				*pos++ = '\0';
		}
	}
	h = dut->cmds;
	while (h) {
		if (strcasecmp(cmd, h->cmd) == 0)
			break;
		h = h->next;
	}

	if (h == NULL) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Unknown command: '%s'",
				cmd);
		send_resp(dut, conn, SIGMA_INVALID,
			  "errorCode,Unknown command");
		goto out;
	}

	if (h->validate && h->validate(&c) < 0) {
	invalid_params:
		sigma_dut_print(dut, DUT_MSG_INFO, "Invalid parameters");
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Invalid "
			  "parameters");
		goto out;
	}

	dut->response_sent = 0;
	send_resp(dut, conn, SIGMA_RUNNING, NULL);
	sigma_dut_print(dut, DUT_MSG_INFO, "Run command: %s", cmd);
	res = h->process(dut, conn, &c);
	switch (res) {
	case ERROR_SEND_STATUS:
		send_resp(dut, conn, SIGMA_ERROR, NULL);
		break;
	case INVALID_SEND_STATUS:
		send_resp(dut, conn, SIGMA_INVALID, NULL);
		break;
	case STATUS_SENT:
	case STATUS_SENT_ERROR:
		break;
	case SUCCESS_SEND_STATUS:
		send_resp(dut, conn, SIGMA_COMPLETE, NULL);
		break;
	}

	if (!conn->waiting_completion && dut->response_sent != 2) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"ERROR: Unexpected number of status lines sent (%d) for command '%s'",
				dut->response_sent, cmd);
	}

out:
	if (dut->debug_level < DUT_MSG_INFO) {
		pos2 = txt;
		*pos2++ = '\\';
		memset(pos2, '-', 69);
		pos2 += 69;
		*pos2++ = '/';
		*pos2 = '\0';
		printf("\n%s\n\n", txt);
	}
}


static void process_conn(struct sigma_dut *dut, struct sigma_conn *conn)
{
	ssize_t res;
	int i;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Read from %s:%d",
			inet_ntoa(conn->addr.sin_addr),
			ntohs(conn->addr.sin_port));

	res = recv(conn->s, conn->buf + conn->pos, MAX_CMD_LEN + 5 - conn->pos,
		   0);
	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "recv: %s",
				strerror(errno));
	}
	if (res <= 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Close connection from "
				"%s:%d",
				inet_ntoa(conn->addr.sin_addr),
				ntohs(conn->addr.sin_port));
		shutdown(conn->s, SHUT_RDWR);
		close(conn->s);
		conn->s = -1;
		return;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Received %d bytes",
			(int) res);

	for (;;) {
		for (i = conn->pos; i < conn->pos + res; i++) {
			if (conn->buf[i] == '\r' || conn->buf[i] == '\n')
				break;
		}

		if (i == conn->pos + res) {
			/* Full command not yet received */
			conn->pos += res;
			if (conn->pos >= MAX_CMD_LEN + 5) {
				sigma_dut_print(dut, DUT_MSG_INFO, "Too long "
						"command dropped");
				conn->pos = 0;
			}
			break;
		}

		/* Full command received */
		conn->buf[i++] = '\0';
		process_cmd(dut, conn, conn->buf);
		while (i < conn->pos + res &&
		       (conn->buf[i] == '\r' || conn->buf[i] == '\n'))
			i++;
		memmove(conn->buf, &conn->buf[i], conn->pos + res - i);
		res = conn->pos + res - i;
		conn->pos = 0;
	}
}


static int stop_loop = 0;

#ifdef __linux__
static void handle_term(int sig)
{
	struct sigma_dut *dut = &sigma_dut;

	if (dut->sta_2g_started || dut->sta_5g_started)
		stop_sta_mode(dut);
	stop_loop = 1;
	stop_event_thread();
	printf("sigma_dut terminating\n");
}
#endif /* __linux__ */

static void run_loop(struct sigma_dut *dut)
{
	struct sigma_conn conn[MAX_CONNECTIONS];
	int i, res, maxfd, can_accept;
	fd_set rfds;

	memset(&conn, 0, sizeof(conn));
	for (i = 0; i < MAX_CONNECTIONS; i++)
		conn[i].s = -1;

#ifdef __linux__
	signal(SIGINT, handle_term);
	signal(SIGTERM, handle_term);
	signal(SIGPIPE, SIG_IGN);
#endif /* __linux__ */

	while (!stop_loop) {
		FD_ZERO(&rfds);
		maxfd = -1;
		can_accept = 0;
		for (i = 0; i < MAX_CONNECTIONS; i++) {
			if (conn[i].s >= 0) {
				FD_SET(conn[i].s, &rfds);
				if (conn[i].s > maxfd)
					maxfd = conn[i].s;
			} else if (!conn[i].waiting_completion)
				can_accept = 1;
		}

		if (can_accept) {
			FD_SET(dut->s, &rfds);
			if (dut->s > maxfd)
				maxfd = dut->s;
		}


		sigma_dut_print(dut, DUT_MSG_DEBUG, "Waiting for next "
				"command (can_accept=%d)", can_accept);
		res = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if (res < 0) {
			perror("select");
			if (!stop_loop)
				sleep(1);
			continue;
		}

		if (!res) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Nothing ready");
			sleep(1);
			continue;
		}

		if (FD_ISSET(dut->s, &rfds)) {
			for (i = 0; i < MAX_CONNECTIONS; i++) {
				if (conn[i].s < 0 &&
				    !conn[i].waiting_completion)
					break;
			}
			if (i == MAX_CONNECTIONS) {
				/*
				 * This cannot really happen since can_accept
				 * would not be set to one.
				 */
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"No room for new connection");
				continue;
			}
			conn[i].addrlen = sizeof(conn[i].addr);
			conn[i].s = accept(dut->s,
					   (struct sockaddr *) &conn[i].addr,
					   &conn[i].addrlen);
			if (conn[i].s < 0) {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"accept: %s",
						strerror(errno));
				continue;
			}

			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Connection %d from %s:%d", i,
					inet_ntoa(conn[i].addr.sin_addr),
					ntohs(conn[i].addr.sin_port));
			conn[i].pos = 0;
		}

		for (i = 0; i < MAX_CONNECTIONS; i++) {
			if (conn[i].s < 0)
				continue;
			if (FD_ISSET(conn[i].s, &rfds))
				process_conn(dut, &conn[i]);
		}
	}
}


static int run_local_cmd(int port, char *lcmd)
{
	int s, len;
	struct sockaddr_in addr;
	char cmd[MAX_CMD_LEN];
	ssize_t res;
	int count;
	char resp[MAX_CMD_LEN];
	int pos;


	if (strlen(lcmd) > sizeof(cmd) - 4) {
		printf("Too long command\n");
		return -1;
	}
	len = snprintf(cmd, sizeof(cmd), "%s \r\n", lcmd);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	inet_aton("127.0.0.1", &addr.sin_addr);
	addr.sin_port = htons(port);

	/* Make sure we do not get stuck indefinitely */
	alarm(150);

	s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0) {
		perror("socket");
		return -1;
	}

	if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("connect");
		close(s);
		return -1;
	}

	res = send(s, cmd, len, 0);
	if (res < 0) {
		perror("send");
		close(s);
		return -1;
	}
	if (res != len) {
		printf("Unexpected send result: %d (expected %d)\n",
		       (int) res, len);
		close(s);
		return -1;
	}

	count = 0;
	pos = 0;
	len = 0;
	for (;;) {
		char *e;
		res = recv(s, resp + len, sizeof(resp) - len, 0);
		if (res < 0) {
			perror("recv");
			close(s);
			return -1;
		}
		if (res == 0) {
			printf("Could not read response\n");
			close(s);
			return -1;
		}
		len += res;
	next_line:
		e = memchr(resp + pos, '\r', len - pos);
		if (e == NULL)
			continue;
		*e++ = '\0';
		if (e - resp < len && *e == '\n')
			*e++ = '\n';
		printf("%s\n", resp + pos);
		if (strncasecmp(resp + pos, "status,RUNNING", 14) != 0)
			break;
		count++;
		if (count == 2)
			break;
		pos = e - resp;
		goto next_line;
	}

	close(s);

	return 0;
}


static void determine_sigma_p2p_ifname(struct sigma_dut *dut)
{
	char buf[256];
	struct wpa_ctrl *ctrl;

	if (dut->p2p_ifname)
		return;

	snprintf(buf, sizeof(buf), "p2p-dev-%s", get_station_ifname(dut));
	ctrl = open_wpa_mon(buf);
	if (ctrl) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		dut->p2p_ifname_buf = strdup(buf);
		dut->p2p_ifname = dut->p2p_ifname_buf;
		sigma_dut_print(&sigma_dut, DUT_MSG_INFO,
				"Using interface %s for P2P operations instead of interface %s",
				dut->p2p_ifname ? dut->p2p_ifname : "NULL",
				get_station_ifname(dut));
	} else {
		dut->p2p_ifname = get_station_ifname(dut);
	}
}


static int get_nl80211_config_enable_option(struct sigma_dut *dut)
{
	char cmd[100], result[5];
	FILE *f;
	size_t len;
	int ap_nl80211_enable;

	snprintf(cmd, sizeof(cmd), "uci get qcacfg80211.config.enable");
	f = popen(cmd, "r");
	if (!f)
		return -1;

	len = fread(result, 1, sizeof(result) - 1, f);
	pclose(f);

	if (len == 0)
		return -1;

	result[len] = '\0';
	ap_nl80211_enable = atoi(result);

	if (ap_nl80211_enable)
		dut->priv_cmd = "cfg80211tool";

	return 0;
}


static void set_defaults(struct sigma_dut *dut)
{
	dut->debug_level = DUT_MSG_INFO;
	dut->default_timeout = 120;
	dut->dialog_token = 0;
	dut->dpp_conf_id = -1;
	dut->dpp_local_bootstrap = -1;
	dut->sta_nss = 2; /* Make default nss 2 */
	dut->trans_proto = NAN_TRANSPORT_PROTOCOL_DEFAULT;
	dut->trans_port = NAN_TRANSPORT_PORT_DEFAULT;
	dut->nan_ipv6_len = 0;
	dut->ap_p2p_cross_connect = -1;
	dut->ap_chwidth = AP_AUTO;
	dut->default_11na_ap_chwidth = AP_AUTO;
	dut->default_11ng_ap_chwidth = AP_AUTO;
	/* by default, enable writing of traffic stream stats */
	dut->write_stats = 1;
	dut->priv_cmd = "iwpriv";
	dut->sigma_tmpdir = SIGMA_TMPDIR;
	dut->ap_ocvc = -1;
	dut->ap_sae_commit_status = -1;
}


static void deinit_sigma_dut(struct sigma_dut *dut)
{
	free(dut->non_pref_ch_list);
	dut->non_pref_ch_list = NULL;
	free(dut->btm_query_cand_list);
	dut->btm_query_cand_list = NULL;
	free(dut->rsne_override);
	dut->rsne_override = NULL;
	free(dut->rsnxe_override_eapol);
	dut->rsnxe_override_eapol = NULL;
	free(dut->ap_sae_groups);
	dut->ap_sae_groups = NULL;
	free(dut->dpp_peer_uri);
	dut->dpp_peer_uri = NULL;
	free(dut->ap_sae_passwords);
	dut->ap_sae_passwords = NULL;
	free(dut->ap_sae_pk_modifier);
	dut->ap_sae_pk_modifier = NULL;
	free(dut->ap_sae_pk_keypair);
	dut->ap_sae_pk_keypair = NULL;
	free(dut->ap_sae_pk_keypair_sig);
	dut->ap_sae_pk_keypair_sig = NULL;
	free(dut->ar_ltf);
	dut->ar_ltf = NULL;
	free(dut->ap_dpp_conf_addr);
	dut->ap_dpp_conf_addr = NULL;
	free(dut->ap_dpp_conf_pkhash);
	dut->ap_dpp_conf_pkhash = NULL;
	if (dut->log_file_fd) {
		fclose(dut->log_file_fd);
		dut->log_file_fd = NULL;
	}
	free(dut->p2p_ifname_buf);
	dut->p2p_ifname_buf = NULL;
	free(dut->main_ifname_2g);
	dut->main_ifname_2g = NULL;
	free(dut->main_ifname_5g);
	dut->main_ifname_5g = NULL;
	free(dut->station_ifname_2g);
	dut->station_ifname_2g = NULL;
	free(dut->station_ifname_5g);
	dut->station_ifname_5g = NULL;
}


static void set_main_ifname(struct sigma_dut *dut, const char *val)
{
	const char *pos;

	dut->main_ifname = optarg;
	pos = strchr(val, '/');
	if (!pos)
		return;
	free(dut->main_ifname_2g);
	dut->main_ifname_2g = malloc(pos - val + 1);
	if (dut->main_ifname_2g) {
		memcpy(dut->main_ifname_2g, val, pos - val);
		dut->main_ifname_2g[pos - val] = '\0';
	}
	free(dut->main_ifname_5g);
	dut->main_ifname_5g = strdup(pos + 1);
}


static void set_station_ifname(struct sigma_dut *dut, const char *val)
{
	const char *pos;

	dut->station_ifname = optarg;
	pos = strchr(val, '/');
	if (!pos)
		return;
	free(dut->station_ifname_2g);
	dut->station_ifname_2g = malloc(pos - val + 1);
	if (dut->station_ifname_2g) {
		memcpy(dut->station_ifname_2g, val, pos - val);
		dut->station_ifname_2g[pos - val] = '\0';
	}
	free(dut->station_ifname_5g);
	dut->station_ifname_5g = strdup(pos + 1);
}


static const char * const license1 =
"sigma_dut - WFA Sigma DUT/CA\n"
"----------------------------\n"
"\n"
"Copyright (c) 2010-2011, Atheros Communications, Inc.\n"
"Copyright (c) 2011-2017, Qualcomm Atheros, Inc.\n"
"Copyright (c) 2018-2019, The Linux Foundation\n"
"All Rights Reserved.\n"
"Licensed under the Clear BSD license.\n"
"\n";
static const char * const license2 =
"Redistribution and use in source and binary forms, with or without\n"
"modification, are permitted (subject to the limitations in the\n"
"disclaimer below) provided that the following conditions are met:\n"
"\n";
static const char * const license3 =
"* Redistributions of source code must retain the above copyright notice,\n"
"  this list of conditions and the following disclaimer.\n"
"\n"
"* Redistributions in binary form must reproduce the above copyright\n"
"  notice, this list of conditions and the following disclaimer in the\n"
"  documentation and/or other materials provided with the distribution.\n"
"\n"
"* Neither the name of Qualcomm Atheros, Inc. nor the names of its\n"
"  contributors may be used to endorse or promote products derived from\n"
"  this software without specific prior written permission.\n"
"\n";
static const char * const license4 =
"NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED\n"
"BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND\n"
"CONTRIBUTORS \"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,\n"
"BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND\n"
"FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE\n"
"COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,\n"
"INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT\n"
"NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF\n"
"USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON\n"
"ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
"(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF\n"
"THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n";


static void print_license(void)
{
	printf("%s%s%s%s\n",
	       license1, license2, license3, license4);
}


static void usage(void)
{
	printf("usage: sigma_dut [-aABdfGqDIntuVW234] [-p<port>] "
	       "[-s<sniffer>] [-m<set_maccaddr.sh>] \\\n"
	       "       [-M<main ifname>] [-R<radio ifname>] "
	       "[-S<station ifname>] [-P<p2p_ifname>]\\\n"
	       "       [-T<throughput pktsize>] \\\n"
	       "       [-w<wpa_supplicant/hostapd ctrl_iface dir>] \\\n"
	       "       [-H <hostapd log file>] \\\n"
	       "       [-F <hostapd binary path>] \\\n"
	       "       [-j <hostapd ifname>] \\\n"
	       "       [-J <wpa_supplicant debug log>] \\\n"
	       "       [-C <certificate path>] \\\n"
	       "       [-v <version string>] \\\n"
	       "       [-L <summary log>] \\\n"
	       "       [-c <wifi chip type: WCN or ATHEROS or "
	       "AR6003 or MAC80211 or QNXNTO or OPENWRT or LINUX-WCN>] \\\n"
	       "       [-i <IP address of the AP>] \\\n"
	       "       [-k <subnet mask for the AP>] \\\n"
	       "       [-K <sigma_dut log file directory>] \\\n"
	       "       [-e <hostapd entropy file>] \\\n"
	       "       [-N <device_get_info vendor>] \\\n"
	       "       [-o <device_get_info model>] \\\n"
	       "       [-O <device_get_info version>] \\\n"
#ifdef MIRACAST
	       "       [-x <sink|source>] \\\n"
	       "       [-y <Miracast library path>] \\\n"
#endif /* MIRACAST */
	       "       [-z <client socket directory path \\\n"
	       "       Ex: </data/vendor/wifi/sockets>] \\\n"
	       "       [-Z <Override default tmp dir path>] \\\n"
	       "       [-5 <WFD timeout override>] \\\n"
	       "       [-r <HT40 or 2.4_HT40>]\n");
	printf("local command: sigma_dut [-p<port>] <-l<cmd>>\n");
}


int main(int argc, char *argv[])
{
	int c;
	int daemonize = 0;
	int port = SIGMA_DUT_PORT;
	char *local_cmd = NULL;
	int internal_dhcp_enabled = 0;
#ifdef __QNXNTO__
	char *env_str = NULL;
	char buf[20];
	char *sigma_ctrl_sock = NULL; /* env used for QNX */
#endif /* __QNXNTO__ */

	memset(&sigma_dut, 0, sizeof(sigma_dut));
	set_defaults(&sigma_dut);

	for (;;) {
		c = getopt(argc, argv,
			   "aAb:Bc:C:dDE:e:fF:gGhH:j:J:i:Ik:K:l:L:m:M:nN:o:O:p:P:qQr:R:s:S:tT:uv:VWw:x:y:z:Z:2345:");
		if (c < 0)
			break;
		switch (c) {
		case 'a':
			sigma_dut.ap_anqpserver = 1;
			break;
		case 'b':
			sigma_dut.bridge = optarg;
			break;
		case 'B':
			daemonize++;
			break;
		case 'C':
			sigma_cert_path = optarg;
			break;
		case 'd':
			if (sigma_dut.debug_level > 0)
				sigma_dut.debug_level--;
			break;
#ifdef __QNXNTO__
		case 'E':
			sigma_ctrl_sock = optarg;
			break;
#endif /* __QNXNTO__ */
		case 'D':
			sigma_dut.stdout_debug = 1;
			break;
		case 'e':
			sigma_dut.hostapd_entropy_log = optarg;
			break;
		case 'f':
			/* Disable writing stats */
			sigma_dut.write_stats = 0;
			break;
		case 'F':
			sigma_dut.hostapd_bin = optarg;
			break;
		case 'g':
			/* Enable internal processing of P2P group formation
			 * events to start/stop DHCP server/client. */
			internal_dhcp_enabled = 1;
			break;
		case 'G':
			sigma_dut.use_hostapd_pid_file = 1;
			break;
		case 'H':
			sigma_dut.hostapd_debug_log = optarg;
			break;
		case 'I':
			print_license();
			exit(0);
			break;
		case 'j':
			sigma_dut.hostapd_ifname = optarg;
			break;
		case 'J':
			sigma_dut.wpa_supplicant_debug_log = optarg;
			break;
		case 'l':
			local_cmd = optarg;
			break;
		case 'L':
			sigma_dut.summary_log = optarg;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'P':
			sigma_dut.p2p_ifname = optarg;
			break;
		case 'q':
			sigma_dut.debug_level++;
			break;
		case 'Q':
			sigma_dut_print(&sigma_dut, DUT_MSG_INFO,
					"Send Periodic data");
			sigma_periodic_data = 1;
			break;
		case 'r':
			if (strcmp(optarg, "HT40") == 0) {
				sigma_dut.default_11na_ap_chwidth = AP_40;
			} else if (strcmp(optarg, "2.4_HT40") == 0) {
				sigma_dut.default_11ng_ap_chwidth = AP_40;
			} else {
				printf("Unsupported -r value\n");
				exit(1);
			}
			break;
		case 'R': {
			static int num_radio = 0;
			static char **radio_ptr = sigma_radio_ifname;

			num_radio++;
			if (num_radio > MAX_RADIO) {
				printf("Multiple radio support limit (%d) exceeded\n",
				       MAX_RADIO);
				exit(1);
			}
			*radio_ptr++ = optarg;
			break;
		}
		case 's':
			sigma_dut.sniffer_ifname = optarg;
			break;
		case 't':
			sigma_dut.no_timestamps = 1;
			break;
		case 'T':
			sigma_dut.throughput_pktsize = atoi(optarg);
			if (sigma_dut.throughput_pktsize == 0) {
				printf("Invalid -T value\n");
				exit(0);
			}
			break;
		case 'm':
			sigma_dut.set_macaddr = optarg;
			break;
		case 'M':
			set_main_ifname(&sigma_dut, optarg);
			break;
		case 'n':
			sigma_dut.no_ip_addr_set = 1;
			break;
		case 'N':
			sigma_dut.vendor_name = optarg;
			break;
		case 'o':
			sigma_dut.model_name = optarg;
			break;
		case 'O':
			sigma_dut.version_name = optarg;
			break;
		case 'K':
			sigma_dut.log_file_dir = optarg;
			break;
		case 'S':
			set_station_ifname(&sigma_dut, optarg);
			break;
		case 'w':
			sigma_hapd_ctrl = optarg;
			sigma_wpas_ctrl = optarg;
			break;
		case 'i':
			ap_inet_addr = optarg;
			break;
		case 'k':
			ap_inet_mask = optarg;
			break;
		case 'c':
			printf("%s", optarg);
			if (set_wifi_chip(optarg) < 0)
				sigma_dut_print(&sigma_dut, DUT_MSG_ERROR,
						"WRONG CHIP TYPE: SAP will "
						"not load");
			break;
		case 'v':
			sigma_dut.version = optarg;
			break;
		case 'V':
			printf("sigma_dut " SIGMA_DUT_VER "\n");
			exit(0);
			break;
		case 'W':
			sigma_dut_print(&sigma_dut, DUT_MSG_INFO,
					"Running WMM-AC test suite");
			sigma_wmm_ac = 1;
			break;
		case 'u':
		       sigma_dut_print(&sigma_dut, DUT_MSG_INFO,
				       "Use iface down/up in reset cmd");
		       sigma_dut.iface_down_on_reset = 1;
		       break;
		case 'A':
			sigma_dut.sim_no_username = 1;
			break;
#ifdef MIRACAST
		case 'x':
			if (strcmp(optarg, "sink") == 0) {
				sigma_dut.wfd_device_type = 1;
				sigma_dut_print(&sigma_dut, DUT_MSG_INFO,
						"Device Type is SINK");
			} else if (strcmp(optarg, "source") == 0) {
				sigma_dut.wfd_device_type = 0;
				sigma_dut_print(&sigma_dut, DUT_MSG_INFO,
						"Device Type is SOURCE");
			}
			break;
		case 'y':
			sigma_dut.miracast_lib_path = optarg;
			break;
#endif /* MIRACAST */
		case 'z':
			client_socket_path = optarg;
			break;
		case 'Z':
			sigma_dut.sigma_tmpdir = optarg;
			break;
		case '2':
			sigma_dut.sae_h2e_default = 1;
			break;
		case '3':
			sigma_dut.owe_ptk_workaround = 1;
			break;
		case '4':
			sigma_dut.client_privacy_default = 1;
			break;
		case '5': {
			int timeout;

			errno = 0;
			timeout = strtol(optarg, NULL, 10);
			if (errno || timeout < 0) {
				sigma_dut_print(&sigma_dut, DUT_MSG_ERROR,
					       "failed to set default_timeout");
				return -1;
			}
			sigma_dut_print(&sigma_dut, DUT_MSG_INFO,
					"default timeout set to %d", timeout);
			sigma_dut.user_config_timeout = timeout;
			break;
		}
		case 'h':
		default:
			usage();
			exit(0);
			break;
		}
	}

	determine_sigma_p2p_ifname(&sigma_dut);
#ifdef MIRACAST
	miracast_init(&sigma_dut);
#endif /* MIRACAST */
	if (local_cmd)
		return run_local_cmd(port, local_cmd);

	if ((wifi_chip_type == DRIVER_QNXNTO ||
	     wifi_chip_type == DRIVER_LINUX_WCN) &&
	    (!sigma_dut.main_ifname || !sigma_dut.station_ifname)) {
		sigma_dut_print(&sigma_dut, DUT_MSG_ERROR,
				"Interface should be provided for QNX/LINUX-WCN driver - check option M and S");
	}

	if (get_openwrt_driver_type() == OPENWRT_DRIVER_ATHEROS)
		get_nl80211_config_enable_option(&sigma_dut);

	sigma_dut_get_device_driver_name(get_main_ifname(&sigma_dut),
					 sigma_dut.device_driver,
					 sizeof(sigma_dut.device_driver));
	if (sigma_dut.device_driver[0])
		sigma_dut_print(&sigma_dut, DUT_MSG_DEBUG, "device driver: %s",
				sigma_dut.device_driver);

#ifdef NL80211_SUPPORT
	sigma_dut.nl_ctx = nl80211_init(&sigma_dut);
#endif /* NL80211_SUPPORT */
	sigma_dut_register_cmds();

#ifdef __QNXNTO__
	/* Try to open socket in other env dev */
	if (sigma_ctrl_sock) {
		env_str = getenv("SOCK");
		if (env_str) {
			sigma_dut_print(&sigma_dut, DUT_MSG_INFO,
					"SOCK=%s", env_str);
		}
		snprintf(buf, sizeof(buf), "SOCK=%s", sigma_ctrl_sock);
		if (putenv(buf) != 0) {
			sigma_dut_print(&sigma_dut, DUT_MSG_ERROR,
					"putenv() failed setting SOCK");
			return EXIT_FAILURE;
		}
	}
#endif /* __QNXNTO__ */

	if (open_socket(&sigma_dut, port) < 0)
		return -1;

#ifdef __QNXNTO__
	/* restore back the SOCK */
	if (sigma_ctrl_sock) {
		if (env_str) {
			snprintf(buf, sizeof(buf), "SOCK=%s", env_str);
			if (putenv(buf) != 0) {
				sigma_dut_print(&sigma_dut, DUT_MSG_ERROR,
						"putenv() failed setting SOCK");
				return EXIT_FAILURE;
			}
		} else {
			/* unset the env for sock */
			unsetenv("SOCK");
		}
	}
#endif /* __QNXNTO__ */

	if (daemonize) {
		if (daemon(0, 0) < 0) {
			perror("daemon");
			exit(-1);
		}
	} else {
#ifdef __linux__
		setlinebuf(stdout);
#endif /* __linux__ */
	}

	if (internal_dhcp_enabled)
		p2p_create_event_thread(&sigma_dut);

	run_loop(&sigma_dut);

#ifdef CONFIG_SNIFFER
	sniffer_close(&sigma_dut);
#endif /* CONFIG_SNIFFER */

	close_socket(&sigma_dut);
#ifdef MIRACAST
	miracast_deinit(&sigma_dut);
#endif /* MIRACAST */
	deinit_sigma_dut(&sigma_dut);
#ifdef NL80211_SUPPORT
	nl80211_deinit(&sigma_dut, sigma_dut.nl_ctx);
#endif /* NL80211_SUPPORT */
	sigma_dut_unreg_cmds(&sigma_dut);
#ifdef ANDROID
	hlp_thread_cleanup(&sigma_dut);
#endif /* ANDROID */

	return 0;
}
