/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 * Copyright (c) 2011-2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2019, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include "wpa_helpers.h"

#define TG_MAX_CLIENTS_CONNECTIONS 1

/* To send periodic data for VO-Enterprise tests */
extern int sigma_periodic_data;

static enum sigma_cmd_result cmd_traffic_agent_config(struct sigma_dut *dut,
						      struct sigma_conn *conn,
						      struct sigma_cmd *cmd)
{
	struct sigma_stream *s;
	const char *val;
	char buf[100];

	if (dut->num_streams == MAX_SIGMA_STREAMS) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,No more "
			  "concurrent traffic streams supported");
		return STATUS_SENT;
	}

	s = &dut->streams[dut->num_streams];
	free(s->stats);
	memset(s, 0, sizeof(*s));
	s->sock = -1;
	s->no_timestamps = dut->no_timestamps;

	val = get_param(cmd, "profile");
	if (!val)
		return INVALID_SEND_STATUS;

	if (strcasecmp(val, "File_Transfer") == 0)
		s->profile = SIGMA_PROFILE_FILE_TRANSFER;
	else if (strcasecmp(val, "Multicast") == 0)
		s->profile = SIGMA_PROFILE_MULTICAST;
	else if (strcasecmp(val, "IPTV") == 0)
		s->profile = SIGMA_PROFILE_IPTV;
	else if (strcasecmp(val, "Transaction") == 0)
		s->profile = SIGMA_PROFILE_TRANSACTION;
	else if (strcasecmp(val, "Start_Sync") == 0)
		s->profile = SIGMA_PROFILE_START_SYNC;
	else if (strcasecmp(val, "Uapsd") == 0)
		s->profile = SIGMA_PROFILE_UAPSD;
	else {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Unsupported "
			  "profile");
		return STATUS_SENT;
	}

	val = get_param(cmd, "direction");
	if (!val)
		return INVALID_SEND_STATUS;
	if (strcasecmp(val, "send") == 0)
		s->sender = 1;
	else if (strcasecmp(val, "receive") == 0)
		s->sender = 0;
	else
		return INVALID_SEND_STATUS;

	val = get_param(cmd, "destination");
	if (val) {
		if (!is_ipv6_addr(val)) {
			if (inet_aton(val, &s->dst) == 0)
				return INVALID_SEND_STATUS;
		} else {
			if (inet_pton(AF_INET6, val, &s->dst) != 1)
				return INVALID_SEND_STATUS;
		}
	}

	val = get_param(cmd, "source");
	if (val) {
		if (!is_ipv6_addr(val)) {
			if (inet_aton(val, &s->src) == 0)
				return INVALID_SEND_STATUS;
		} else {
			if (inet_pton(AF_INET6, val, &s->src) != 1)
				return INVALID_SEND_STATUS;
		}
	}

	val = get_param(cmd, "destinationPort");
	if (val)
		s->dst_port = atoi(val);

	val = get_param(cmd, "sourcePort");
	if (val)
		s->src_port = atoi(val);

	val = get_param(cmd, "frameRate");
	if (val)
		s->frame_rate = atoi(val);

	val = get_param(cmd, "duration");
	if (val)
		s->duration = atoi(val);

	val = get_param(cmd, "payloadSize");
	if (val)
		s->payload_size = atoi(val);

	val = get_param(cmd, "startDelay");
	if (val)
		s->start_delay = atoi(val);

	val = get_param(cmd, "maxCnt");
	if (val)
		s->max_cnt = atoi(val);

	val = get_param(cmd, "trafficClass");
	if (val) {
		if (strcasecmp(val, "Voice") == 0)
			s->tc = SIGMA_TC_VOICE;
		else if (strcasecmp(val, "Video") == 0)
			s->tc = SIGMA_TC_VIDEO;
		else if (strcasecmp(val, "Background") == 0)
			s->tc = SIGMA_TC_BACKGROUND;
		else if (strcasecmp(val, "BestEffort") == 0)
			s->tc = SIGMA_TC_BEST_EFFORT;
		else
			return INVALID_SEND_STATUS;
	}

	val = get_param(cmd, "userpriority");
	if (val) {
		s->user_priority_set = 1;
		s->user_priority = atoi(val);
	}

	val = get_param(cmd, "tagName");
	if (val) {
		strlcpy(s->test_name, val, sizeof(s->test_name));
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Traffic agent: U-APSD console tagname %s",
				s->test_name);
	}

	if (dut->throughput_pktsize && s->frame_rate == 0 && s->sender &&
	    dut->throughput_pktsize != s->payload_size &&
	    (s->profile == SIGMA_PROFILE_FILE_TRANSFER ||
	     s->profile == SIGMA_PROFILE_IPTV ||
	     s->profile == SIGMA_PROFILE_UAPSD)) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Traffic agent: Override throughput test payload size %u -> %u",
				s->payload_size, dut->throughput_pktsize);
		s->payload_size = dut->throughput_pktsize;
	}

	val = get_param(cmd, "transProtoType");
	if (val) {
		if (strcmp(val, "1") == 0)
			s->trans_proto = IPPROTO_TCP;
		else if (strcmp(val, "0") == 0)
			s->trans_proto = IPPROTO_UDP;
		else
			return INVALID_SEND_STATUS;
	} else {
		s->trans_proto = IPPROTO_UDP;
	}

	if (s->profile == SIGMA_PROFILE_IPTV && !s->sender && !s->no_timestamps)
	{
		s->stats = calloc(MAX_SIGMA_STATS,
				  sizeof(struct sigma_frame_stats));
		if (s->stats == NULL)
			return ERROR_SEND_STATUS;
	}

	dut->stream_id++;
	dut->num_streams++;

	s->stream_id = dut->stream_id;
	snprintf(buf, sizeof(buf), "streamID,%d", s->stream_id);
	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return STATUS_SENT;
}


static void stop_stream(struct sigma_stream *s)
{
	if (s && s->started) {
		pthread_join(s->thr, NULL);
		if (s->sock != -1) {
			close(s->sock);
			s->sock = -1;
		}

		s->started = 0;
	}
}


static enum sigma_cmd_result cmd_traffic_agent_reset(struct sigma_dut *dut,
						     struct sigma_conn *conn,
						     struct sigma_cmd *cmd)
{
	int i;
	for (i = 0; i < dut->num_streams; i++) {
		struct sigma_stream *s = &dut->streams[i];
		s->stop = 1;
		stop_stream(s);
	}
	dut->num_streams = 0;
	memset(&dut->streams, 0, sizeof(dut->streams));
	return SUCCESS_SEND_STATUS;
}


static int get_stream_id(const char *str, int streams[MAX_SIGMA_STREAMS])
{
	int count;

	count = 0;
	for (;;) {
		if (count == MAX_SIGMA_STREAMS)
			return -1;
		streams[count] = atoi(str);
		if (streams[count] == 0)
			return -1;
		count++;
		str = strchr(str, ' ');
		if (str == NULL)
			break;
		while (*str == ' ')
			str++;
	}

	return count;
}


static int open_socket_file_transfer(struct sigma_dut *dut,
				     struct sigma_stream *s)
{
	struct sockaddr_in addr;
	int sock_opt_val = 1;

	s->sock = socket(PF_INET, IPPROTO_UDP == s->trans_proto ? SOCK_DGRAM :
			 SOCK_STREAM, s->trans_proto);
	if (s->sock < 0) {
		perror("socket");
		return -1;
	}

	if (setsockopt(s->sock, SOL_SOCKET, SO_REUSEADDR, &sock_opt_val,
		       sizeof(sock_opt_val)) < 0) {
		perror("setsockopt");
		close(s->sock);
		s->sock = -1;
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(s->sender ? s->src_port : s->dst_port);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Traffic agent: sender=%d "
			"bind port %d", s->sender, ntohs(addr.sin_port));
	if (bind(s->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind");
		close(s->sock);
		s->sock = -1;
		return -1;
	}

	if (s->profile == SIGMA_PROFILE_MULTICAST && !s->sender)
		return 0;

	if (s->trans_proto == IPPROTO_TCP && s->sender == 0) {
		if (listen(s->sock, TG_MAX_CLIENTS_CONNECTIONS ) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Listen failed with error %d: %s",
					errno, strerror(errno));
			close(s->sock);
			s->sock = -1;
			return -1;
		}
	} else {
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = s->sender ? s->dst.s_addr :
			s->src.s_addr;
		addr.sin_port = htons(s->sender ? s->dst_port : s->src_port);
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Traffic agent: connect %s:%d",
				inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
		if (connect(s->sock, (struct sockaddr *) &addr, sizeof(addr)) <
		    0) {
			perror("connect");
			close(s->sock);
			s->sock = -1;
			return -1;
		}
	}

	return 0;
}


static int open_socket_multicast(struct sigma_dut *dut, struct sigma_stream *s)
{
	if (open_socket_file_transfer(dut, s) < 0)
		return -1;

	if (!s->sender) {
		struct ip_mreq mr;
		memset(&mr, 0, sizeof(mr));
		mr.imr_multiaddr.s_addr = s->dst.s_addr;
		mr.imr_interface.s_addr = htonl(INADDR_ANY);
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Traffic agent: "
				"IP_ADD_MEMBERSHIP %s", inet_ntoa(s->dst));
		if (setsockopt(s->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			       (void *) &mr, sizeof(mr)) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"setsockopt[IP_ADD_MEMBERSHIP]: %s",
					strerror(errno));
			/*
			 * Continue anyway since this can happen, e.g., if the
			 * default route is missing. This is not critical for
			 * multicast RX testing.
			 */
		}
	}

	return 0;
}


static int set_socket_prio(struct sigma_stream *s)
{
	int tos = 0x00;

	switch (s->tc) {
	case SIGMA_TC_VOICE:
		if (s->user_priority_set) {
			if (s->user_priority == 6)
				tos = 46 << 2;
			else if (s->user_priority == 7)
				tos = 56 << 2;
			else
				return -1;
		} else
			tos = 0xe0; /* DSCP = 56 */
		break;
	case SIGMA_TC_VIDEO:
		if (s->user_priority_set) {
			if (s->user_priority == 4)
				tos = 32 << 2;
			else if (s->user_priority == 5)
				tos = 40 << 2;
			else
				return -1;
		} else
			tos = 0xa0; /* DSCP = 40 */
		break;
	case SIGMA_TC_BACKGROUND:
		if (s->user_priority_set) {
			if (s->user_priority == 1)
				tos = 8 << 2;
			else if (s->user_priority == 2)
				tos = 16 << 2;
			else
				return -1;
		} else
			tos = 0x20; /* DSCP = 8 */
		break;
	case SIGMA_TC_BEST_EFFORT:
		if (s->user_priority_set) {
			if (s->user_priority == 0)
				tos = 0 << 2;
			else if (s->user_priority == 3)
				tos = 20 << 2;
			else
				return -1;
		} else
			tos = 0x00; /* DSCP = 0 */
		break;
	}

	if (setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0) {
		perror("setsockopt");
		return -1;
	}

	return 0;
}


static int open_socket(struct sigma_dut *dut, struct sigma_stream *s)
{
	switch (s->profile) {
	case SIGMA_PROFILE_FILE_TRANSFER:
		return open_socket_file_transfer(dut, s);
	case SIGMA_PROFILE_MULTICAST:
		return open_socket_multicast(dut, s);
	case SIGMA_PROFILE_IPTV:
		if (open_socket_file_transfer(dut, s) < 0)
			return -1;
		return set_socket_prio(s);
	case SIGMA_PROFILE_TRANSACTION:
		return open_socket_file_transfer(dut, s);
	case SIGMA_PROFILE_UAPSD:
		return open_socket_file_transfer(dut, s);
	case SIGMA_PROFILE_START_SYNC:
		sigma_dut_print(dut, DUT_MSG_INFO, "Traffic stream profile %d "
				"not yet supported", s->profile);
		/* TODO */
		break;
	}

	return -1;
}


static void send_file_fast(struct sigma_stream *s, char *pkt)
{
	struct timeval stop, now;
	int res;
	unsigned int counter = 0;

	gettimeofday(&stop, NULL);
	stop.tv_sec += s->duration;

	while (!s->stop) {
		counter++;
		WPA_PUT_BE32(&pkt[8], counter);

		if ((counter & 0xf) == 0) {
			gettimeofday(&now, NULL);
			if (now.tv_sec > stop.tv_sec ||
			    (now.tv_sec == stop.tv_sec &&
			     now.tv_usec >= stop.tv_usec))
				break;
		}

		s->tx_act_frames++;
		res = send(s->sock, pkt, s->payload_size, MSG_DONTWAIT);
		if (res >= 0) {
			s->tx_frames++;
			s->tx_payload_bytes += res;
		} else {
			switch (errno) {
			case EAGAIN:
			case ENOBUFS:
				usleep(1000);
				break;
			case ECONNRESET:
			case EPIPE:
				s->stop = 1;
				break;
			default:
				perror("send");
				break;
			}
		}
	}
}


static void send_file(struct sigma_stream *s)
{
	char *pkt;
	struct timeval stop, now, start;
	int res;
	unsigned int counter = 0, total_sleep_usec = 0, total_pkts;
	int sleep_usec = 0;

	if (s->duration <= 0 || s->frame_rate < 0 || s->payload_size < 20)
		return;

	pkt = malloc(s->payload_size);
	if (pkt == NULL)
		return;
	memset(pkt, 1, s->payload_size);
	strlcpy(pkt, "1345678", s->payload_size);

	if (s->frame_rate == 0 && s->no_timestamps) {
		send_file_fast(s, pkt);
		free(pkt);
		return;
	}

	gettimeofday(&stop, NULL);
	stop.tv_sec += s->duration;

	total_pkts = s->duration * s ->frame_rate;

	gettimeofday(&start, NULL);

	while (!s->stop) {
		counter++;
		WPA_PUT_BE32(&pkt[8], counter);

		if (sleep_usec) {
			usleep(sleep_usec);
			total_sleep_usec += sleep_usec;
		}

		gettimeofday(&now, NULL);
		if (now.tv_sec > stop.tv_sec ||
		    (now.tv_sec == stop.tv_sec && now.tv_usec >= stop.tv_usec))
			break;

		if (s->frame_rate && (unsigned int) s->tx_frames >= total_pkts)
			break;

		if (s->frame_rate == 0 || s->tx_frames == 0)
			sleep_usec = 0;
		else if (sleep_usec || s->frame_rate < 10 ||
			 counter % (s->frame_rate / 10) == 0) {
			/* Recalculate sleep_usec for every 100 ms approximately
			 */
			struct timeval tmp;
			int diff, duration;

			timersub(&now, &start, &tmp);

			diff = tmp.tv_sec * 1000000 + tmp.tv_usec;
			duration = (1000000 / s->frame_rate) * s->tx_frames;

			if (duration > diff)
				sleep_usec = (total_sleep_usec +
					      (duration - diff)) / s->tx_frames;
			else
				sleep_usec = 0;
		}

		WPA_PUT_BE32(&pkt[12], now.tv_sec);
		WPA_PUT_BE32(&pkt[16], now.tv_usec);

		s->tx_act_frames++;
		res = send(s->sock, pkt, s->payload_size, MSG_DONTWAIT);
		if (res >= 0) {
			s->tx_frames++;
			s->tx_payload_bytes += res;
		} else {
			switch (errno) {
			case EAGAIN:
			case ENOBUFS:
				usleep(1000);
				break;
			case ECONNRESET:
			case EPIPE:
				s->stop = 1;
				break;
			default:
				perror("send");
				break;
			}
		}
	}

	sigma_dut_print(s->dut, DUT_MSG_DEBUG,
			"send_file: counter %u s->tx_frames %d total_sleep_usec %u",
			counter, s->tx_frames, total_sleep_usec);

	free(pkt);
}

static void send_periodic_data(struct sigma_stream *s)
{
	char *pkt;
	struct timeval stop, now, start;
	int res;
	unsigned int counter = 0, total_sleep_usec = 0, total_pkts;
	int sleep_usec = 0;

	if (s->duration <= 0 || s->frame_rate < 0 || s->payload_size < 20)
		return;

	pkt = malloc(s->payload_size);
	if (pkt == NULL)
		return;
	memset(pkt, 1, s->payload_size);
	strlcpy(pkt, "1345678", s->payload_size);

	if (s->frame_rate == 0 && s->no_timestamps) {
		send_file_fast(s, pkt);
		free(pkt);
		return;
	}

	gettimeofday(&stop, NULL);
	stop.tv_sec += s->duration;

	total_pkts = s->duration * s->frame_rate;

	gettimeofday(&start, NULL);
	while (!s->stop) {
		counter++;
		WPA_PUT_BE32(&pkt[8], counter);

		if (sleep_usec) {
			usleep(sleep_usec);
			total_sleep_usec += sleep_usec;
		}
		gettimeofday(&now, NULL);

		if (now.tv_sec > stop.tv_sec ||
		    (now.tv_sec == stop.tv_sec && now.tv_usec >= stop.tv_usec))
			break;

		if (s->frame_rate && (unsigned int) s->tx_frames >= total_pkts)
			break;

		WPA_PUT_BE32(&pkt[12], now.tv_sec);
		WPA_PUT_BE32(&pkt[16], now.tv_usec);

		s->tx_act_frames++;
		res = send(s->sock, pkt, s->payload_size, 0);
		if (res >= 0) {
			s->tx_frames++;
			s->tx_payload_bytes += res;
		} else {
			switch (errno) {
			case EAGAIN:
			case ENOBUFS:
				usleep(1000);
				break;
			case ECONNRESET:
			case EPIPE:
				s->stop = 1;
				break;
			default:
				perror("send");
				break;
			}
		}

		if (s->frame_rate == 0)
			sleep_usec = 0;
		else {
			struct timeval tmp;
			int diff, duration, pkt_spacing;

			gettimeofday(&now, NULL);
			timersub(&now, &start, &tmp);

			pkt_spacing = 1000000 / s->frame_rate ;
			diff = tmp.tv_sec * 1000000 + tmp.tv_usec;
			duration = (pkt_spacing) * s->tx_frames;

			if (duration > diff) {
				if ((duration - diff) > pkt_spacing)
					sleep_usec = (total_sleep_usec +
						      (duration - diff)) / s->tx_frames;
				else
					sleep_usec = duration - diff;
			} else {
				sleep_usec = 0;
			}
		}
	}

	sigma_dut_print(s->dut, DUT_MSG_DEBUG,
			"send_periodic_data: counter %u s->tx_frames %d total_sleep_usec %u",
			counter, s->tx_frames, total_sleep_usec);

	free(pkt);
}

static void send_transaction(struct sigma_stream *s)
{
	char *pkt, *rpkt;
	struct timeval stop, now;
	int res;
	unsigned int counter = 0, rcounter;
	int wait_time;
	fd_set rfds;
	struct timeval tv;

	if (s->duration <= 0 || s->frame_rate <= 0 || s->payload_size < 20)
		return;

	pkt = malloc(s->payload_size);
	if (pkt == NULL)
		return;
	rpkt = malloc(s->payload_size);
	if (rpkt == NULL) {
		free(pkt);
		return;
	}
	memset(pkt, 1, s->payload_size);
	strlcpy(pkt, "1345678", s->payload_size);

	gettimeofday(&stop, NULL);
	stop.tv_sec += s->duration;

	wait_time = 1000000 / s->frame_rate;

	while (!s->stop) {
		counter++;
		if (s->max_cnt && (int) counter > s->max_cnt)
			break;
		WPA_PUT_BE32(&pkt[8], counter);

		gettimeofday(&now, NULL);
		if (now.tv_sec > stop.tv_sec ||
		    (now.tv_sec == stop.tv_sec && now.tv_usec >= stop.tv_usec))
			break;
		WPA_PUT_BE32(&pkt[12], now.tv_sec);
		WPA_PUT_BE32(&pkt[16], now.tv_usec);

		res = send(s->sock, pkt, s->payload_size, MSG_DONTWAIT);
		if (res >= 0) {
			s->tx_frames++;
			s->tx_payload_bytes += res;
		} else {
			switch (errno) {
			case EAGAIN:
			case ENOBUFS:
				usleep(1000);
				break;
			case ECONNRESET:
			case EPIPE:
				s->stop = 1;
				break;
			default:
				perror("send");
				break;
			}
		}

		/* Wait for response */
		tv.tv_sec = 0;
		tv.tv_usec = wait_time;
		FD_ZERO(&rfds);
		FD_SET(s->sock, &rfds);
		res = select(s->sock + 1, &rfds, NULL, NULL, &tv);
		if (res < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			break;
		}

		if (res == 0) {
			/* timeout */
			continue;
		}

		if (FD_ISSET(s->sock, &rfds)) {
			/* response received */
			res = recv(s->sock, rpkt, s->payload_size, 0);
			if (res < 0) {
				perror("recv");
				break;
			}
			rcounter = WPA_GET_BE32(&rpkt[8]);
			if (rcounter != counter)
				s->out_of_seq_frames++;
			s->rx_frames++;
			s->rx_payload_bytes += res;
		}
	}

	free(pkt);
	free(rpkt);
}


static void * send_thread(void *ctx)
{
	struct sigma_stream *s = ctx;

	sleep(s->start_delay);

	switch (s->profile) {
	case SIGMA_PROFILE_FILE_TRANSFER:
		send_file(s);
		break;
	case SIGMA_PROFILE_MULTICAST:
		send_file(s);
		break;
	case SIGMA_PROFILE_IPTV:
		if (sigma_periodic_data)
			send_periodic_data(s);
		else
			send_file(s);
		break;
	case SIGMA_PROFILE_TRANSACTION:
		send_transaction(s);
		break;
	case SIGMA_PROFILE_START_SYNC:
		break;
	case SIGMA_PROFILE_UAPSD:
		send_uapsd_console(s);
		break;
	}

	return NULL;
}


struct traffic_agent_send_data {
	struct sigma_dut *dut;
	struct sigma_conn *conn;
	int streams[MAX_SIGMA_STREAMS];
	int count;
};


static struct sigma_stream * get_stream(struct sigma_dut *dut, int id)
{
	int i;

	for (i = 0; i < dut->num_streams; i++) {
		if ((unsigned int) id == dut->streams[i].stream_id)
			return &dut->streams[i];
	}

	return NULL;
}


static void * send_report_thread(void *ctx)
{
	struct traffic_agent_send_data *data = ctx;
	struct sigma_dut *dut = data->dut;
	struct sigma_conn *conn = data->conn;
	int i, ret;
	char buf[100 + MAX_SIGMA_STREAMS * 60], *pos;

	for (i = 0; i < data->count; i++) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Traffic agent: waiting "
				"for stream %d send to complete",
				data->streams[i]);
		stop_stream(get_stream(dut, data->streams[i]));
	}

	buf[0] = '\0';
	pos = buf;

	pos += snprintf(pos, buf + sizeof(buf) - pos, "streamID,");
	for (i = 0; i < data->count; i++) {
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", data->streams[i]);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	if (dut->program == PROGRAM_60GHZ) {
		sigma_dut_print(dut, DUT_MSG_INFO, "reporting tx_act_frames");
		pos += snprintf(pos, buf + sizeof(buf) - pos, ",txActFrames,");
		for (i = 0; i < data->count; i++) {
			struct sigma_stream *s;

			s = get_stream(dut, data->streams[i]);
			if (!s)
				continue;
			ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
				       i > 0 ? " " : "", s->tx_act_frames);
			if (ret < 0 || ret >= buf + sizeof(buf) - pos)
				break;
			pos += ret;
		}
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",txFrames,");
	for (i = 0; i < data->count; i++) {
		struct sigma_stream *s = get_stream(dut, data->streams[i]);

		if (!s)
			continue;
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->tx_frames);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",rxFrames,");
	for (i = 0; i < data->count; i++) {
		struct sigma_stream *s = get_stream(dut, data->streams[i]);

		if (!s)
			continue;
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->rx_frames);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",txPayloadBytes,");
	for (i = 0; i < data->count; i++) {
		struct sigma_stream *s = get_stream(dut, data->streams[i]);

		if (!s)
			continue;
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%llu",
			       i > 0 ? " " : "", s->tx_payload_bytes);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",rxPayloadBytes,");
	for (i = 0; i < data->count; i++) {
		struct sigma_stream *s = get_stream(dut, data->streams[i]);

		if (!s)
			continue;
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%llu",
			       i > 0 ? " " : "", s->rx_payload_bytes);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",outOfSequenceFrames,");
	for (i = 0; i < data->count; i++) {
		struct sigma_stream *s = get_stream(dut, data->streams[i]);

		if (!s)
			continue;
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->out_of_seq_frames);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	for (i = 0; i < data->count; i++) {
		struct sigma_stream *s = get_stream(dut, data->streams[i]);
		if (!s)
			continue;
		s->ta_send_in_progress = 0;
		if (s->trans_proto == IPPROTO_TCP) {
			/*
			 * Close the socket to make sure client side close the
			 * network before the server. Otherwise, the server
			 * might get "Address already in use" when trying to
			 * reuse the port.
			 */
			close(s->sock);
			s->sock = -1;
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Closed the sender socket");
		}
	}

	buf[sizeof(buf) - 1] = '\0';

	if (conn->s < 0)
		sigma_dut_print(dut, DUT_MSG_INFO, "Cannot send traffic_agent response since control socket has already been closed");
	else
		send_resp(dut, conn, SIGMA_COMPLETE, buf);
	conn->waiting_completion = 0;

	free(data);

	return NULL;
}


static enum sigma_cmd_result cmd_traffic_agent_send(struct sigma_dut *dut,
						    struct sigma_conn *conn,
						    struct sigma_cmd *cmd)
{
	const char *val;
	int i, j, res;
	char buf[100];
	struct traffic_agent_send_data *data;

	val = get_param(cmd, "streamID");
	if (val == NULL)
		return INVALID_SEND_STATUS;

	data = calloc(1, sizeof(*data));
	if (data == NULL)
		return ERROR_SEND_STATUS;
	data->dut = dut;
	data->conn = conn;

	data->count = get_stream_id(val, data->streams);
	if (data->count < 0) {
		free(data);
		return ERROR_SEND_STATUS;
	}
	for (i = 0; i < data->count; i++) {
		struct sigma_stream *s = get_stream(dut, data->streams[i]);

		if (!s) {
			snprintf(buf, sizeof(buf), "errorCode,StreamID %d "
				 "not configured", data->streams[i]);
			send_resp(dut, conn, SIGMA_INVALID, buf);
			free(data);
			return STATUS_SENT;
		}
		for (j = 0; j < i; j++)
			if (data->streams[i] == data->streams[j]) {
				free(data);
				return ERROR_SEND_STATUS;
			}
		if (!s->sender) {
			snprintf(buf, sizeof(buf), "errorCode,Not configured "
				 "as sender for streamID %d", data->streams[i]);
			send_resp(dut, conn, SIGMA_INVALID, buf);
			free(data);
			return STATUS_SENT;
		}
		if (s->ta_send_in_progress) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Multiple concurrent send cmds on same streamID not supported");
			free(data);
			return STATUS_SENT;
		}
	}

	for (i = 0; i < data->count; i++) {
		struct sigma_stream *s = get_stream(dut, data->streams[i]);

		if (!s)
			continue;
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Traffic agent: open "
				"socket for send stream %d", data->streams[i]);
		if (open_socket(dut, s) < 0) {
			free(data);
			return ERROR_SEND_STATUS;
		}
	}

	for (i = 0; i < data->count; i++) {
		struct sigma_stream *s = get_stream(dut, data->streams[i]);

		if (!s)
			continue;

		/*
		 * Provide dut context to the thread to support debugging and
		 * returning of error messages.
		 */
		s->dut = dut;

		sigma_dut_print(dut, DUT_MSG_DEBUG, "Traffic agent: start "
				"send for stream %d", data->streams[i]);
		res = pthread_create(&s->thr, NULL, send_thread, s);
		if (res) {
			sigma_dut_print(dut, DUT_MSG_INFO, "pthread_create "
					"failed: %d", res);
			free(data);
			return ERROR_SEND_STATUS;
		}
		s->started = 1;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Traffic agent: start a thread to track sending streams");
	conn->waiting_completion = 1;
	res = pthread_create(&dut->thr, NULL, send_report_thread, data);
	if (res) {
		sigma_dut_print(dut, DUT_MSG_INFO, "pthread_create failed: %d",
				res);
		free(data);
		conn->waiting_completion = 0;
		return ERROR_SEND_STATUS;
	}

	for (i = 0; i < data->count; i++) {
		struct sigma_stream *s = get_stream(dut, data->streams[i]);

		if (s)
			s->ta_send_in_progress = 1;
	}

	/* Command will be completed in send_report_thread() */

	return STATUS_SENT;
}


static void receive_file(struct sigma_stream *s)
{
	struct timeval tv, now;
	fd_set rfds;
	int res;
	char *pkt;
	int pktlen;
	unsigned int last_rx = 0, counter;

	pktlen = 65536 + 1;
	pkt = malloc(pktlen);
	if (pkt == NULL)
		return;

	while (!s->stop) {
		FD_ZERO(&rfds);
		FD_SET(s->sock, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 300000;
		res = select(s->sock + 1, &rfds, NULL, NULL, &tv);
		if (res < 0) {
			perror("select");
			usleep(10000);
		} else if (FD_ISSET(s->sock, &rfds)) {
			res = recv(s->sock, pkt, pktlen, 0);
			if (res >= 0) {
				s->rx_frames++;
				s->rx_payload_bytes += res;

				counter = WPA_GET_BE32(&pkt[8]);
				if (counter < last_rx)
					s->out_of_seq_frames++;
				last_rx = counter;
			} else {
				perror("recv");
				break;
			}

			if (res >= 20 && s->stats &&
			    s->num_stats < MAX_SIGMA_STATS) {
				struct sigma_frame_stats *stats;
				stats = &s->stats[s->num_stats];
				s->num_stats++;
				gettimeofday(&now, NULL);
				stats->seqnum = counter;
				stats->local_sec = now.tv_sec;
				stats->local_usec = now.tv_usec;
				stats->remote_sec = WPA_GET_BE32(&pkt[12]);
				stats->remote_usec = WPA_GET_BE32(&pkt[16]);
			}
		}
	}

	free(pkt);
}


static void receive_transaction(struct sigma_stream *s)
{
	struct timeval tv;
	fd_set rfds;
	int res;
	char *pkt;
	int pktlen;
	unsigned int last_rx = 0, counter;
	struct sockaddr_in addr;
	socklen_t addrlen;

	if (s->payload_size)
		pktlen = s->payload_size;
	else
		pktlen = 65536 + 1;
	pkt = malloc(pktlen);
	if (pkt == NULL)
		return;

	while (!s->stop) {
		FD_ZERO(&rfds);
		FD_SET(s->sock, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 300000;
		res = select(s->sock + 1, &rfds, NULL, NULL, &tv);
		if (res < 0) {
			perror("select");
			usleep(10000);
		} else if (FD_ISSET(s->sock, &rfds)) {
			addrlen = sizeof(addr);
			res = recvfrom(s->sock, pkt, pktlen, 0,
				       (struct sockaddr *) &addr, &addrlen);
			if (res < 0) {
				perror("recv");
				break;
			}

			s->rx_frames++;
			s->rx_payload_bytes += res;

			counter = WPA_GET_BE32(&pkt[8]);
			if (counter < last_rx)
				s->out_of_seq_frames++;
			last_rx = counter;

			/* send response */
			res = sendto(s->sock, pkt, pktlen, 0,
				     (struct sockaddr *) &addr, addrlen);
			if (res < 0) {
				perror("sendto");
			} else {
				s->tx_frames++;
				s->tx_payload_bytes += res;
			}
		}
	}

	free(pkt);
}


static void * receive_thread(void *ctx)
{
	struct sigma_stream *s = ctx;

	if (s->trans_proto == IPPROTO_TCP) {
		/* Wait for socket to be accepted */
		struct sockaddr_in connected_addr;
		int connected_sock; /* returned from accept on sock */
		socklen_t connected_addr_len = sizeof(connected_addr);

		sigma_dut_print(s->dut, DUT_MSG_DEBUG,
				"Traffic agent: Waiting on accept");
		connected_sock = accept(s->sock,
					(struct sockaddr *) &connected_addr,
					&connected_addr_len);
		if (connected_sock < 0) {
			sigma_dut_print(s->dut, DUT_MSG_ERROR,
					"Traffic agent: Failed to accept: %s",
					strerror(errno));
			return NULL;
		}

		sigma_dut_print(s->dut, DUT_MSG_DEBUG,
				"Traffic agent: Accepted client closing parent socket and talk over connected sock.");
		close(s->sock);
		s->sock = connected_sock;
	}

	switch (s->profile) {
	case SIGMA_PROFILE_FILE_TRANSFER:
		receive_file(s);
		break;
	case SIGMA_PROFILE_MULTICAST:
		receive_file(s);
		break;
	case SIGMA_PROFILE_IPTV:
		receive_file(s);
		break;
	case SIGMA_PROFILE_TRANSACTION:
		receive_transaction(s);
		break;
	case SIGMA_PROFILE_START_SYNC:
		break;
	case SIGMA_PROFILE_UAPSD:
		receive_uapsd(s);
		break;
	}

	return NULL;
}


static enum sigma_cmd_result
cmd_traffic_agent_receive_start(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *val;
	int streams[MAX_SIGMA_STREAMS];
	int i, j, count;
	char buf[100];

	val = get_param(cmd, "streamID");
	if (val == NULL)
		return INVALID_SEND_STATUS;
	count = get_stream_id(val, streams);
	if (count < 0)
		return ERROR_SEND_STATUS;
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = get_stream(dut, streams[i]);

		if (!s) {
			snprintf(buf, sizeof(buf), "errorCode,StreamID %d "
				 "not configured", streams[i]);
			send_resp(dut, conn, SIGMA_INVALID, buf);
			return STATUS_SENT;
		}
		for (j = 0; j < i; j++)
			if (streams[i] == streams[j])
				return ERROR_SEND_STATUS;
		if (s->sender) {
			snprintf(buf, sizeof(buf), "errorCode,Not configured "
				 "as receiver for streamID %d", streams[i]);
			send_resp(dut, conn, SIGMA_INVALID, buf);
			return STATUS_SENT;
		}
	}

	for (i = 0; i < count; i++) {
		struct sigma_stream *s = get_stream(dut, streams[i]);

		if (!s)
			continue;
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Traffic agent: open "
				"receive socket for stream %d", streams[i]);
		if (open_socket(dut, s) < 0)
			return ERROR_SEND_STATUS;
	}

	for (i = 0; i < count; i++) {
		struct sigma_stream *s = get_stream(dut, streams[i]);
		int res;

		if (!s)
			continue;
		/*
		 * Provide dut context to the thread to support debugging and
		 * returning of error messages. Similarly, provide interface
		 * information to the thread. If the Interface parameter is not
		 * passed, get it from get_station_ifname() since the interface
		 * name is needed for power save mode configuration for Uapsd
		 * cases.
		 */
		s->dut = dut;
		val = get_param(cmd, "Interface");
		strlcpy(s->ifname, (val ? val : get_station_ifname(dut)),
			sizeof(s->ifname));

		sigma_dut_print(dut, DUT_MSG_DEBUG, "Traffic agent: start "
				"receive for stream %d", streams[i]);
		res = pthread_create(&s->thr, NULL, receive_thread, s);
		if (res) {
			sigma_dut_print(dut, DUT_MSG_INFO, "pthread_create "
					"failed: %d", res);
			return ERROR_SEND_STATUS;
		}
		s->started = 1;
	}

	return SUCCESS_SEND_STATUS;
}


static void write_frame_stats(struct sigma_dut *dut, struct sigma_stream *s,
			      int id)
{
	char fname[128];
	FILE *f;
	unsigned int i;

	snprintf(fname, sizeof(fname), "%s/e2e%u-%d.txt",
		 dut->sigma_tmpdir, (unsigned int) time(NULL), id);
	f = fopen(fname, "w");
	if (f == NULL) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Could not write %s",
				fname);
		return;
	}
	fprintf(f, "seqnum:local_sec:local_usec:remote_sec:remote_usec\n");

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Writing frame stats to %s",
			fname);

	for (i = 0; i < s->num_stats; i++) {
		struct sigma_frame_stats *stats = &s->stats[i];
		fprintf(f, "%u:%u:%u:%u:%u\n", stats->seqnum,
			stats->local_sec, stats->local_usec,
			stats->remote_sec, stats->remote_usec);
	}

	fclose(f);
}


static enum sigma_cmd_result
cmd_traffic_agent_receive_stop(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *val;
	int streams[MAX_SIGMA_STREAMS];
	int i, j, ret, count;
	char buf[100 + MAX_SIGMA_STREAMS * 60], *pos;

	val = get_param(cmd, "streamID");
	if (val == NULL)
		return INVALID_SEND_STATUS;
	count = get_stream_id(val, streams);
	if (count < 0)
		return ERROR_SEND_STATUS;
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = get_stream(dut, streams[i]);

		if (!s) {
			snprintf(buf, sizeof(buf), "errorCode,StreamID %d "
				 "not configured", streams[i]);
			send_resp(dut, conn, SIGMA_INVALID, buf);
			return STATUS_SENT;
		}
		for (j = 0; j < i; j++)
			if (streams[i] == streams[j])
				return ERROR_SEND_STATUS;
		if (!s->started) {
			snprintf(buf, sizeof(buf), "errorCode,Receive not "
				 "started for streamID %d", streams[i]);
			send_resp(dut, conn, SIGMA_INVALID, buf);
			return STATUS_SENT;
		}
	}

	for (i = 0; i < count; i++) {
		struct sigma_stream *s = get_stream(dut, streams[i]);

		if (s)
			s->stop = 1;
	}

	for (i = 0; i < count; i++) {
		struct sigma_stream *s = get_stream(dut, streams[i]);

		if (!s)
			continue;
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Traffic agent: stop "
				"receive for stream %d", streams[i]);
		stop_stream(s);
	}

	buf[0] = '\0';
	pos = buf;

	pos += snprintf(pos, buf + sizeof(buf) - pos, "streamID,");
	for (i = 0; i < count; i++) {
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", streams[i]);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	if (dut->program == PROGRAM_60GHZ) {
		pos += snprintf(pos, buf + sizeof(buf) - pos, ",txActFrames,");
		for (i = 0; i < count; i++) {
			struct sigma_stream *s = get_stream(dut, streams[i]);

			if (!s)
				continue;
			ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
				       i > 0 ? " " : "", s->tx_act_frames);
			if (ret < 0 || ret >= buf + sizeof(buf) - pos)
				break;
			pos += ret;
		}
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",txFrames,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = get_stream(dut, streams[i]);

		if (!s)
			continue;
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->tx_frames);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",rxFrames,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = get_stream(dut, streams[i]);

		if (!s)
			continue;
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->rx_frames);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",txPayloadBytes,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = get_stream(dut, streams[i]);

		if (!s)
			continue;
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%llu",
			       i > 0 ? " " : "", s->tx_payload_bytes);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",rxPayloadBytes,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = get_stream(dut, streams[i]);

		if (!s)
			continue;
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%llu",
			       i > 0 ? " " : "", s->rx_payload_bytes);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",outOfSequenceFrames,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = get_stream(dut, streams[i]);

		if (!s)
			continue;
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->out_of_seq_frames);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	buf[sizeof(buf) - 1] = '\0';

	send_resp(dut, conn, SIGMA_COMPLETE, buf);

	for (i = 0; i < count; i++) {
		struct sigma_stream *s = get_stream(dut, streams[i]);

		if (!s)
			continue;
		if (s->profile == SIGMA_PROFILE_IPTV && s->num_stats > 0 &&
		    dut->write_stats)
			write_frame_stats(dut, s, streams[i]);
		free(s->stats);
		s->stats = NULL;
		s->num_stats = 0;
	}

	return STATUS_SENT;
}


static enum sigma_cmd_result cmd_traffic_agent_version(struct sigma_dut *dut,
						       struct sigma_conn *conn,
						       struct sigma_cmd *cmd)
{
	send_resp(dut, conn, SIGMA_COMPLETE, "version,1.0");
	return STATUS_SENT;
}


void traffic_agent_register_cmds(void)
{
	sigma_dut_reg_cmd("traffic_agent_config", NULL,
			  cmd_traffic_agent_config);
	sigma_dut_reg_cmd("traffic_agent_reset", NULL,
			  cmd_traffic_agent_reset);
	sigma_dut_reg_cmd("traffic_agent_send", NULL,
			  cmd_traffic_agent_send);
	sigma_dut_reg_cmd("traffic_agent_receive_start", NULL,
			  cmd_traffic_agent_receive_start);
	sigma_dut_reg_cmd("traffic_agent_receive_stop", NULL,
			  cmd_traffic_agent_receive_stop);
	sigma_dut_reg_cmd("traffic_agent_version", NULL,
			  cmd_traffic_agent_version);
}
