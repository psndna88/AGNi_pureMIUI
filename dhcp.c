/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2018, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include "wpa_helpers.h"
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <pcap.h>
#include <signal.h>

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_ACK         5

#define UDP_PROTOCOL 17

static pcap_t *pcap = NULL;

struct dhcp_pkt {
	struct iphdr iph;
	struct udphdr udph;
	u8 op;
	u8 htype;
	u8 hlen;
	u8 hops;
	u32 xid;
	u16 secs;
	u16 flags;
	u32 client_ip;
	u32 your_ip;
	u32 server_ip;
	u32 relay_ip;
	u8 hw_addr[16];
	u8 serv_name[64];
	u8 boot_file[128];
	u32 magic_cookie;
	u8 options[314];
} __attribute__ ((packed));

enum dhcp_options {
	DHCP_OPT_SUBNET_MASK = 1,
	DHCP_OPT_ROUTER = 3,
	DHCP_OPT_MSG_TYPE = 53,
	DHCP_OPT_END = 255
};


static u8 * get_dhcp_option(u8 *options, u8 type, int len)
{
	u8 *pos = options;
	u8 *end = pos + len;

	while (pos < end && pos + 1 < end && pos + 2 + pos[1] <= end) {
		if (*pos == type)
			return pos;
		pos += 2 + pos[1];
	}
	return NULL;
}


/**
 * 1. Open UDP socket
 * 2. read_msg
 * 3. Process DHCP_ACK
 */
static void * process_dhcp_ack(void *ptr)
{
	struct bpf_program pcap_fp;
	char pcap_filter[200], pcap_err[PCAP_ERRBUF_SIZE];
	struct sigma_dut *dut = ptr;
	int nbytes = 0;
	u8 buf[1024];
	struct dhcp_pkt *dhcp;
	int option_len;
	u8 *msg_type, *val;
	struct in_addr ip;
	char your_ip[16], mask[16], router[16];
	unsigned short protocol, port_no;
	bpf_u_int32 pcap_maskp, pcap_netp;
	char ifname[20];

	protocol = UDP_PROTOCOL;
	port_no = DHCP_SERVER_PORT;

	strlcpy(ifname, get_main_ifname(dut), sizeof(ifname));

	/* gives the network mask for ifname essential for applying filter */
	pcap_lookupnet(ifname, &pcap_netp, &pcap_maskp, pcap_err);

	sigma_dut_print(dut, DUT_MSG_INFO, "DHCP: ifname = %s", ifname);

	/* creates a session for sniffing */
	pcap = pcap_open_live(ifname, 2500, 0, 10, pcap_err);
	if (!pcap) {
		sigma_dut_print(dut, DUT_MSG_INFO, "pcap_open_live: %s",
				pcap_err);
		goto exit;
	}

	snprintf(pcap_filter, sizeof(pcap_filter),
		 "ip proto 0x%x and udp src port 0x%x",
		 protocol, port_no);
	sigma_dut_print(dut, DUT_MSG_INFO, "pcap_flter %s", pcap_filter);

	if (pcap_compile(pcap, &pcap_fp, pcap_filter, 1, pcap_netp) < 0)
		sigma_dut_print(dut, DUT_MSG_INFO, "pcap_compile: %s",
				pcap_geterr(pcap));

	if (pcap_setfilter(pcap, &pcap_fp) < 0)
		sigma_dut_print(dut, DUT_MSG_INFO, "pcap_setfilter: %s",
				pcap_geterr(pcap));

	pcap_freecode(&pcap_fp);

	while (1) {
		memset(buf, 0, sizeof(buf));
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"HLP: Waiting for message to receive");
		nbytes = recvfrom(pcap_get_selectable_fd(pcap), &buf,
				  sizeof(buf), 0, NULL, NULL);
		if (nbytes == -1) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "HLP: failed: %s",
					strerror(errno));
			goto exit;
		}

		sigma_dut_print(dut, DUT_MSG_INFO, "HLP: Received %d bytes",
				nbytes);
		hex_dump(dut, buf, nbytes);

		if (nbytes < 314) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"HLP: Ignore MSG, Too short message received");
			continue;
		}
		nbytes -= 14;

		/*
		 * Process DHCP packet
		 * skip ethernet header from buf and then process the ack
		 */
		dhcp = (struct dhcp_pkt *) (buf + ETH_HLEN);

		option_len = nbytes - ((char *) dhcp->options - (char *) dhcp);

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"option_len %d, First option : %02x",
				option_len, *(dhcp->options));

		/* Check for DHCP_ACK */
		msg_type = get_dhcp_option(dhcp->options, DHCP_OPT_MSG_TYPE,
					   option_len);
		if (!msg_type) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Ignore MSG, DHCP OPT MSG_TYPE missing");
			continue;
		}

		if (msg_type[2] != DHCP_ACK) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Ignore MSG, DHCP message type : %02x",
					msg_type[2]);
			continue;
		}

		ip.s_addr = dhcp->your_ip;
		strlcpy(your_ip, inet_ntoa(ip), sizeof(your_ip));

		val = get_dhcp_option(dhcp->options, DHCP_OPT_SUBNET_MASK,
				      option_len);
		if (!val) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"DHCP option SUBNET_MASK missing");
			continue;
		}

		memcpy(&(ip.s_addr), (val + 2), val[1]);
		strlcpy(mask, inet_ntoa(ip), sizeof(mask));

		val = get_dhcp_option(dhcp->options, DHCP_OPT_ROUTER,
				      option_len);
		if (!val) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"DHCP option DHCP_OPT_ROUTER missing");
			continue;
		}
		memcpy(&(ip.s_addr), val + 2, val[1]);
		strlcpy(router, inet_ntoa(ip), sizeof(router));

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"OP: %d, your_ip: %s, netmask: %s, router: %s",
				dhcp->op, your_ip, mask, router);
		/* set ip configuration */
		if (!set_ipv4_addr(dut, ifname, your_ip, mask)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set IP address");
			continue;
		}
		if (set_ipv4_gw(dut, router) < 1) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set Gateway address");
			continue;
		}

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"IP configuration completed");
	}

exit:
	sigma_dut_print(dut, DUT_MSG_INFO, "HLP: Received failed in exit");
	if (pcap) {
		pcap_close(pcap);
		pcap = NULL;
	}
	dut->hlp_thread = 0;
	return NULL;
}


static void hlp_thread_exit(int signum)
{
	pthread_exit(0);
}


void hlp_thread_cleanup(struct sigma_dut *dut)
{
	if (pcap) {
		pcap_close(pcap);
		pcap = NULL;
	}
	if (dut->hlp_thread) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Kill thread: %ld",
				dut->hlp_thread);
		pthread_kill(dut->hlp_thread, SIGUSR1);
		dut->hlp_thread = 0;
	}
}


void process_fils_hlp(struct sigma_dut *dut)
{
	static pthread_t hlp_thread;

	if (dut->hlp_thread) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"FILS-HLP DHCP thread already running");
		return;
	}

	signal(SIGUSR1, hlp_thread_exit);

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Creating FILS_HLP thread-->");

	/* create FILS_HLP thread */
	if (!pthread_create(&hlp_thread, NULL, &process_dhcp_ack,
			    (void *) dut)) {
		dut->hlp_thread = hlp_thread;
	} else {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"FILS_HLP thread creation failed");
	}

}
