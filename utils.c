/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2014-2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation
 * Copyright (c) 2005-2011, Jouni Malinen <j@w1.fi>
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/ioctl.h>
#include <sys/stat.h>
#include "wpa_helpers.h"

enum driver_type wifi_chip_type = DRIVER_NOT_SET;
enum openwrt_driver_type openwrt_chip_type = OPENWRT_DRIVER_NOT_SET;

struct wcn_drv_priv_cmd {
	char *buf;
	int used_len;
	int total_len;
};

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


enum driver_type get_driver_type(struct sigma_dut *dut)
{
	struct stat s;
	if (wifi_chip_type == DRIVER_NOT_SET) {
		/* Check for 60G driver */
		ssize_t len;
		char link[256];
		char buf[256];
		const char *ifname = get_station_ifname(dut);

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
		if (stat("/sys/module/umac", &s) == 0 ||
		    stat("/sys/module/atd", &s) == 0)
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
	if (strcasecmp(prog, "HS2-R3") == 0)
		return PROGRAM_HS2_R3;
	if (strcasecmp(prog, "WFD") == 0)
		return PROGRAM_WFD;
	if (strcasecmp(prog, "DisplayR2") == 0)
		return PROGRAM_DISPLAYR2;
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
	if (strcasecmp(prog, "LOC") == 0)
		return PROGRAM_LOC;
	if (strcasecmp(prog, "MBO") == 0)
		return PROGRAM_MBO;
	if (strcasecmp(prog, "IoTLP") == 0)
		return PROGRAM_IOTLP;
	if (strcasecmp(prog, "DPP") == 0)
		return PROGRAM_DPP;
	if (strcasecmp(prog, "OCE") == 0)
		return PROGRAM_OCE;
	if (strcasecmp(prog, "WPA3") == 0)
		return PROGRAM_WPA3;
	if (strcasecmp(prog, "HE") == 0)
		return PROGRAM_HE;
	if (strcasecmp(prog, "QM") == 0)
		return PROGRAM_QM;

	return PROGRAM_UNKNOWN;
}


static int parse_hex(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


int hex_byte(const char *str)
{
	int res1, res2;

	res1 = parse_hex(str[0]);
	if (res1 < 0)
		return -1;
	res2 = parse_hex(str[1]);
	if (res2 < 0)
		return -1;
	return (res1 << 4) | res2;
}


int parse_hexstr(const char *hex, unsigned char *buf, size_t buflen)
{
	size_t i;
	const char *pos = hex;

	for (i = 0; i < buflen; i++) {
		int val;

		if (*pos == '\0')
			break;
		val = hex_byte(pos);
		if (val < 0)
			return -1;
		buf[i] = val;
		pos += 2;
	}

	return i;
}


int parse_mac_address(struct sigma_dut *dut, const char *arg,
		      unsigned char *addr)
{
	int i;
	const char *pos = arg;

	if (strlen(arg) != 17)
		goto fail;

	for (i = 0; i < ETH_ALEN; i++) {
		int val;

		val = hex_byte(pos);
		if (val < 0)
			goto fail;
		addr[i] = val;
		if (i + 1 < ETH_ALEN) {
			pos += 2;
			if (*pos != ':')
				goto fail;
			pos++;
		}
	}

	return 0;

fail:
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Invalid MAC address %s (expected format xx:xx:xx:xx:xx:xx)",
			arg);
	return -1;
}


int is_60g_sigma_dut(struct sigma_dut *dut)
{
	return dut->program == PROGRAM_60GHZ ||
		(dut->program == PROGRAM_WPS &&
		 (get_driver_type(dut) == DRIVER_WIL6210));
}


unsigned int channel_to_freq(struct sigma_dut *dut, unsigned int channel)
{
	if (is_60g_sigma_dut(dut)) {
		if (channel >= 1 && channel <= 4)
			return 58320 + 2160 * channel;

		return 0;
	}

	if (channel >= 1 && channel <= 13)
		return 2407 + 5 * channel;
	if (channel == 14)
		return 2484;
	if (channel >= 36 && channel <= 165)
		return 5000 + 5 * channel;

	return 0;
}


unsigned int freq_to_channel(unsigned int freq)
{
	if (freq >= 2412 && freq <= 2472)
		return (freq - 2407) / 5;
	if (freq == 2484)
		return 14;
	if (freq >= 5180 && freq <= 5825)
		return (freq - 5000) / 5;
	if (freq >= 58320 && freq <= 64800)
		return (freq - 58320) / 2160;
	return 0;
}


int is_ipv6_addr(const char *str)
{
	struct sockaddr_in6 addr;

	return inet_pton(AF_INET6, str, &(addr.sin6_addr));
}


void convert_mac_addr_to_ipv6_lladdr(u8 *mac_addr, char *ipv6_buf,
				     size_t buf_len)
{
	u8 temp = mac_addr[0] ^ 0x02;

	snprintf(ipv6_buf, buf_len, "fe80::%02x%02x:%02xff:fe%02x:%02x%02x",
		 temp, mac_addr[1], mac_addr[2],
		 mac_addr[3], mac_addr[4], mac_addr[5]);
}


size_t convert_mac_addr_to_ipv6_linklocal(const u8 *mac_addr, u8 *ipv6)
{
	int i;

	ipv6[0] = 0xfe;
	ipv6[1] = 0x80;
	for (i = 2; i < 8; i++)
		ipv6[i] = 0;
	ipv6[8] = mac_addr[0] ^ 0x02;
	ipv6[9] = mac_addr[1];
	ipv6[10] = mac_addr[2];
	ipv6[11] = 0xff;
	ipv6[12] = 0xfe;
	ipv6[13] = mac_addr[3];
	ipv6[14] = mac_addr[4];
	ipv6[15] = mac_addr[5];

	return 16;
}


#ifndef ANDROID

size_t strlcpy(char *dest, const char *src, size_t siz)
{
	const char *s = src;
	size_t left = siz;

	if (left) {
		/* Copy string up to the maximum size of the dest buffer */
		while (--left != 0) {
			if ((*dest++ = *s++) == '\0')
				break;
		}
	}

	if (left == 0) {
		/* Not enough room for the string; force NUL-termination */
		if (siz != 0)
			*dest = '\0';
		while (*s++)
			; /* determine total src string length */
	}

	return s - src - 1;
}


size_t strlcat(char *dst, const char *str, size_t size)
{
	char *pos;
	size_t dstlen, srclen, copy;

	srclen = strlen(str);
	for (pos = dst; pos - dst < size && *dst; pos++)
		;
	dstlen = pos - dst;
	if (*dst)
		return dstlen + srclen;
	if (dstlen + srclen + 1 > size)
		copy = size - dstlen - 1;
	else
		copy = srclen;
	memcpy(pos, str, copy);
	pos[copy] = '\0';
	return dstlen + srclen;
}

#endif /* ANDROID */


void hex_dump(struct sigma_dut *dut, u8 *data, size_t len)
{
	char buf[1024];
	size_t index;
	u8 *ptr;
	int pos;

	memset(buf, 0, sizeof(buf));
	ptr = data;
	pos = 0;
	for (index = 0; index < len; index++) {
		pos += snprintf(&(buf[pos]), sizeof(buf) - pos,
				"%02x ", *ptr++);
		if (pos > 1020)
			break;
	}
	sigma_dut_print(dut, DUT_MSG_INFO, "HEXDUMP len=[%d]", (int) len);
	sigma_dut_print(dut, DUT_MSG_INFO, "buf:%s", buf);
}


#ifdef NL80211_SUPPORT

void * nl80211_cmd(struct sigma_dut *dut, struct nl80211_ctx *ctx,
		   struct nl_msg *msg, int flags, uint8_t cmd)
{
	return genlmsg_put(msg, 0, 0, ctx->netlink_familyid,
			   0, flags, cmd, 0);
}


static struct nl_msg *
nl80211_ifindex_msg(struct sigma_dut *dut, struct nl80211_ctx *ctx, int ifindex,
		    int flags, uint8_t cmd)
{
	struct nl_msg *msg;

	msg = nlmsg_alloc();
	if (!msg) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to allocate NL message");
		return NULL;
	}

	if (!nl80211_cmd(dut, ctx, msg, flags, cmd) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex)) {
		nlmsg_free(msg);
		return NULL;
	}

	return msg;
}


struct nl_msg * nl80211_drv_msg(struct sigma_dut *dut, struct nl80211_ctx *ctx,
				int ifindex, int flags, uint8_t cmd)
{
	return nl80211_ifindex_msg(dut, ctx, ifindex, flags, cmd);
}


static int ack_handler(struct nl_msg *msg, void *arg)
{
	int *err = arg;
	*err = 0;
	return NL_STOP;
}


static int finish_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_SKIP;
}


static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err,
			 void *arg)
{
	int *ret = arg;
	*ret = err->error;
	return NL_SKIP;
}


int send_and_recv_msgs(struct sigma_dut *dut, struct nl80211_ctx *ctx,
		       struct nl_msg *nlmsg,
		       int (*valid_handler)(struct nl_msg *, void *),
		       void *valid_data)
{
	struct nl_cb *cb;
	int err = -ENOMEM;

	if (!nlmsg)
		return -ENOMEM;

	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb)
		goto out;

	err = nl_send_auto_complete(ctx->sock, nlmsg);
	if (err < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"nl80211: failed to send err=%d", err);
		goto out;
	}

	err = 1;

	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &err);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &err);

	if (valid_handler)
		nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM,
			  valid_handler, valid_data);

	while (err > 0) {
		int res = nl_recvmsgs(ctx->sock, cb);

		if (res < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"nl80211: %s->nl_recvmsgs failed: res=%d, err=%d",
					__func__, res, err);
		}
	}
 out:
	nl_cb_put(cb);
	if (!valid_handler && valid_data == (void *) -1) {
		if (nlmsg) {
			struct nlmsghdr *hdr = nlmsg_hdr(nlmsg);
			void *data = nlmsg_data(hdr);
			int len = hdr->nlmsg_len - NLMSG_HDRLEN;

			memset(data, 0, len);
		}
	}

	nlmsg_free(nlmsg);
	return err;
}


struct nl80211_ctx * nl80211_init(struct sigma_dut *dut)
{
	struct nl80211_ctx *ctx;

	ctx = calloc(1, sizeof(struct nl80211_ctx));
	if (!ctx) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to alloc nl80211_ctx");
		return NULL;
	}

	ctx->sock = nl_socket_alloc();
	if (!ctx->sock) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to create NL socket, err: %s",
				strerror(errno));
		goto cleanup;
	}

	if (nl_connect(ctx->sock, NETLINK_GENERIC)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Could not connect socket, err: %s",
				strerror(errno));
		goto cleanup;
	}

	if (nl_socket_set_buffer_size(ctx->sock, SOCK_BUF_SIZE, 0) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Could not set nl_socket RX buffer size for sock: %s",
				strerror(errno));
	}

	ctx->netlink_familyid = genl_ctrl_resolve(ctx->sock, "nl80211");
	if (ctx->netlink_familyid < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Could not resolve nl80211 family id");
		goto cleanup;
	}

	ctx->nlctrl_familyid = genl_ctrl_resolve(ctx->sock, "nlctrl");
	if (ctx->nlctrl_familyid < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"net link family nlctrl is not present: %d err:%s",
				ctx->nlctrl_familyid, strerror(errno));
		goto cleanup;
	}

	return ctx;

cleanup:
	if (ctx->sock)
		nl_socket_free(ctx->sock);

	free(ctx);
	return NULL;
}


void nl80211_deinit(struct sigma_dut *dut, struct nl80211_ctx *ctx)
{
	if (!ctx || !ctx->sock) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "%s: ctx/sock is NULL",
				__func__);
		return;
	}
	nl_socket_free(ctx->sock);
	free(ctx);
}

#endif /* NL80211_SUPPORT */


static int get_wps_pin_checksum(int pin)
{
	int a = 0;

	while (pin > 0) {
		a += 3 * (pin % 10);
		pin = pin / 10;
		a += (pin % 10);
		pin = pin / 10;
	}

	return (10 - (a % 10)) % 10;
}


int get_wps_pin_from_mac(struct sigma_dut *dut, const char *macaddr,
			 char *pin, size_t len)
{
	unsigned char mac[ETH_ALEN];
	int tmp, checksum;

	if (len < 9)
		return -1;
	if (parse_mac_address(dut, macaddr, mac))
		return -1;

	/*
	 * get 7 digit PIN from the last 24 bits of MAC
	 * range 1000000 - 9999999
	 */
	tmp = (mac[5] & 0xFF) | ((mac[4] & 0xFF) << 8) |
	      ((mac[3] & 0xFF) << 16);
	tmp = (tmp % 9000000) + 1000000;
	checksum = get_wps_pin_checksum(tmp);
	snprintf(pin, len, "%07d%01d", tmp, checksum);
	return 0;
}


int get_wps_forced_version(struct sigma_dut *dut, const char *str)
{
	int major, minor, result = 0;
	int count = sscanf(str, "%d.%d", &major, &minor);

	if (count == 2) {
		result = major * 16 + minor;
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Force WPS version to 0x%02x (%s)",
				result, str);
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Invalid WPS version %s", str);
	}

	return result;
}


void str_remove_chars(char *str, char ch)
{
	char *pr = str, *pw = str;

	while (*pr) {
		*pw = *pr++;
		if (*pw != ch)
			pw++;
	}
	*pw = '\0';
}


static const char base64_table[65] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


int base64_encode(const char *src, size_t len, char *out, size_t out_len)
{
	unsigned char *pos;
	const unsigned char *end, *in;
	size_t olen;

	olen = len * 4 / 3 + 4; /* 3-byte blocks to 4-byte */
	olen++; /* nul termination */
	if (olen < len || olen > out_len)
		return -1;

	end = (unsigned char *)(src + len);
	in = (unsigned char *)src;
	pos = (unsigned char *)out;
	while (end - in >= 3) {
		*pos++ = base64_table[(in[0] >> 2) & 0x3f];
		*pos++ = base64_table[(((in[0] & 0x03) << 4) |
				       (in[1] >> 4)) & 0x3f];
		*pos++ = base64_table[(((in[1] & 0x0f) << 2) |
				       (in[2] >> 6)) & 0x3f];
		*pos++ = base64_table[in[2] & 0x3f];
		in += 3;
	}

	if (end - in) {
		*pos++ = base64_table[(in[0] >> 2) & 0x3f];
		if (end - in == 1) {
			*pos++ = base64_table[((in[0] & 0x03) << 4) & 0x3f];
			*pos++ = '=';
		} else {
			*pos++ = base64_table[(((in[0] & 0x03) << 4) |
					       (in[1] >> 4)) & 0x3f];
			*pos++ = base64_table[((in[1] & 0x0f) << 2) & 0x3f];
		}
		*pos++ = '=';
	}

	*pos = '\0';
	return 0;
}


int random_get_bytes(char *buf, size_t len)
{
	FILE *f;
	size_t rc;

	f = fopen("/dev/urandom", "rb");
	if (!f)
		return -1;

	rc = fread(buf, 1, len, f);
	fclose(f);

	return rc != len ? -1 : 0;
}


int get_enable_disable(const char *val)
{
	if (strcasecmp(val, "enable") == 0 ||
	    strcasecmp(val, "enabled") == 0 ||
	    strcasecmp(val, "on") == 0 ||
	    strcasecmp(val, "yes") == 0)
		return 1;
	return atoi(val);
}


int wcn_driver_cmd(const char *ifname, char *buf)
{
	int s, res;
	size_t buf_len;
	struct wcn_drv_priv_cmd priv_cmd;
	struct ifreq ifr;

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		perror("socket");
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	memset(&priv_cmd, 0, sizeof(priv_cmd));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	buf_len = strlen(buf);
	priv_cmd.buf = buf;
	priv_cmd.used_len = buf_len;
	priv_cmd.total_len = buf_len;
	ifr.ifr_data = (void *) &priv_cmd;
	res = ioctl(s, SIOCDEVPRIVATE + 1, &ifr);
	close(s);
	return res;
}
