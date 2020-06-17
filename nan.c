/*
 * Sigma Control API DUT (NAN functionality)
 * Copyright (c) 2014-2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/stat.h>
#include "wpa_ctrl.h"
#include "wpa_helpers.h"
#include "nan_cert.h"

#if NAN_CERT_VERSION >= 2

#if ((NAN_MAJOR_VERSION > 2) ||					 \
     (NAN_MAJOR_VERSION == 2 &&					 \
      (NAN_MINOR_VERSION >= 1 || NAN_MICRO_VERSION >= 1))) &&	 \
	NAN_CERT_VERSION >= 5
#define NAN_NEW_CERT_VERSION
#endif

pthread_cond_t gCondition;
pthread_mutex_t gMutex;
static NanSyncStats global_nan_sync_stats;
static int nan_state = 0;
static int event_anyresponse = 0;
static int is_fam = 0;

static uint16_t global_ndp_instance_id = 0;
static uint16_t global_publish_id = 0;
static uint16_t global_subscribe_id = 0;
uint16_t global_header_handle = 0;
uint32_t global_match_handle = 0;

#define DEFAULT_SVC "QNanCluster"
#define MAC_ADDR_ARRAY(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MAC_ADDR_STR "%02x:%02x:%02x:%02x:%02x:%02x"
#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif

static const u8 nan_wfa_oui[] = { 0x50, 0x6f, 0x9a };
/* TLV header length = tag (1 byte) + length (2 bytes) */
#define WLAN_NAN_TLV_HEADER_SIZE (1 + 2)
#define NAN_INTF_ID_LEN   8

struct sigma_dut *global_dut = NULL;
static char global_nan_mac_addr[ETH_ALEN];
static char global_peer_mac_addr[ETH_ALEN];
static char global_event_resp_buf[1024];
static u8 global_publish_service_name[NAN_MAX_SERVICE_NAME_LEN];
static u32 global_publish_service_name_len = 0;
static u8 global_subscribe_service_name[NAN_MAX_SERVICE_NAME_LEN];
static u32 global_subscribe_service_name_len = 0;

static int nan_further_availability_tx(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd);
static int nan_further_availability_rx(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd);

enum wlan_nan_tlv_type {
	NAN_TLV_TYPE_IPV6_LINK_LOCAL = 0x00,
	NAN_TLV_TYPE_SERVICE_INFO = 0x01,
	NAN_TLV_TYPE_RSVD_START = 0x02,
	NAN_TLV_TYPE_RSVD_START_END = 0xFF
};

enum wlan_nan_service_protocol_type {
	NAN_TLV_SERVICE_PROTO_TYPE_RSVD1 = 0x00,
	NAN_TLV_SERVICE_PROTO_TYPE_BONJOUR = 0x01,
	NAN_TLV_SERVICE_PROTO_TYPE_GENERIC = 0x02,
	NAN_TLV_SERVICE_PROTO_RSVD2_START = 0x03,
	NAN_TLV_SERVICE_PROTO_TYPE_RSVD2_END = 0xFF
};

enum wlan_nan_generic_service_proto_sub_attr {
	NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_TRANS_PORT = 0x00,
	NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_TRANS_PROTO = 0x01,
	NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_SERVICE_NAME = 0x02,
	NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_TEXTINFO = 0x04,
	NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_UUID = 0x05,
	NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_BLOB = 0x06,
	NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_RSVD1_START = 0x07,
	NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_RSVD1_END = 0xDC,
	NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_VENDOR_SPEC_INFO= 0xDD,
	NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_RSVD2_START = 0xDE,
	NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_RSVD2_END = 0xFF
};


void nan_hex_dump(struct sigma_dut *dut, uint8_t *data, size_t len)
{
	char buf[512];
	uint16_t index;
	uint8_t *ptr;
	int pos;

	memset(buf, 0, sizeof(buf));
	ptr = data;
	pos = 0;
	for (index = 0; index < len; index++) {
		pos += snprintf(&(buf[pos]), sizeof(buf) - pos,
				"%02x ", *ptr++);
		if (pos > 508)
			break;
	}
	sigma_dut_print(dut, DUT_MSG_INFO, "HEXDUMP len=[%d]", (int) len);
	sigma_dut_print(dut, DUT_MSG_INFO, "buf:%s", buf);
}


int nan_parse_hex(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 0;
}


int nan_parse_token(const char *tokenIn, u8 *tokenOut, int *filterLen)
{
	int total_len = 0, len = 0;
	char *saveptr = NULL;

	tokenIn = strtok_r((char *) tokenIn, ":", &saveptr);
	while (tokenIn != NULL) {
		len = strlen(tokenIn);
		if (len == 1 && *tokenIn == '*')
			len = 0;
		tokenOut[total_len++] = (u8) len;
		if (len != 0)
			memcpy((u8 *) tokenOut + total_len, tokenIn, len);
		total_len += len;
		tokenIn = strtok_r(NULL, ":", &saveptr);
	}
	*filterLen = total_len;
	return 0;
}


int nan_parse_mac_address(struct sigma_dut *dut, const char *arg, u8 *addr)
{
	if (strlen(arg) != 17) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Invalid mac address %s",
				arg);
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"expected format xx:xx:xx:xx:xx:xx");
		return -1;
	}

	addr[0] = nan_parse_hex(arg[0]) << 4 | nan_parse_hex(arg[1]);
	addr[1] = nan_parse_hex(arg[3]) << 4 | nan_parse_hex(arg[4]);
	addr[2] = nan_parse_hex(arg[6]) << 4 | nan_parse_hex(arg[7]);
	addr[3] = nan_parse_hex(arg[9]) << 4 | nan_parse_hex(arg[10]);
	addr[4] = nan_parse_hex(arg[12]) << 4 | nan_parse_hex(arg[13]);
	addr[5] = nan_parse_hex(arg[15]) << 4 | nan_parse_hex(arg[16]);

	return 0;
}


int nan_parse_mac_address_list(struct sigma_dut *dut, const char *input,
			       u8 *output, u16 max_addr_allowed)
{
	/*
	 * Reads a list of mac address separated by space. Each MAC address
	 * should have the format of aa:bb:cc:dd:ee:ff.
	 */
	char *saveptr;
	char *token;
	int i = 0;

	for (i = 0; i < max_addr_allowed; i++) {
		token = strtok_r((i == 0) ? (char *) input : NULL,
				 " ", &saveptr);
		if (token) {
			nan_parse_mac_address(dut, token, output);
			output += NAN_MAC_ADDR_LEN;
		} else
			break;
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "Num MacAddress:%d", i);

	return i;
}


int nan_parse_hex_string(struct sigma_dut *dut, const char *input,
			 u8 *output, int *outputlen)
{
	int i = 0;
	int j = 0;

	for (i = 0; i < (int) strlen(input) && j < *outputlen; i += 2) {
		output[j] = nan_parse_hex(input[i]);
		if (i + 1 < (int) strlen(input)) {
			output[j] = ((output[j] << 4) |
				     nan_parse_hex(input[i + 1]));
		}
		j++;
	}
	*outputlen = j;
	sigma_dut_print(dut, DUT_MSG_INFO, "Input:%s inputlen:%d outputlen:%d",
			input, (int) strlen(input), (int) *outputlen);
	return 0;
}


static size_t nan_build_ipv6_link_local_tlv(u8 *p_frame,
					    const u8 *p_ipv6_intf_addr)
{
	/* fill attribute ID */
	*p_frame++ = NAN_TLV_TYPE_IPV6_LINK_LOCAL;

	/* Fill the length */
	*p_frame++ = NAN_INTF_ID_LEN & 0xFF;
	*p_frame++ = NAN_INTF_ID_LEN >> 8;

	/* only the lower 8 bytes is needed */
	memcpy(p_frame, &p_ipv6_intf_addr[NAN_INTF_ID_LEN], NAN_INTF_ID_LEN);

	return NAN_INTF_ID_LEN + WLAN_NAN_TLV_HEADER_SIZE;
}


static size_t nan_build_service_info_tlv_sub_attr(
	u8 *p_frame, const u8 *sub_attr, const u16 sub_attr_len,
	enum wlan_nan_generic_service_proto_sub_attr sub_attr_id)
{
	/* Fill Service Subattibute ID */
	*p_frame++ = (u8) sub_attr_id;

	/* Fill the length */
	*p_frame++ = sub_attr_len & 0xFF;
	*p_frame++ = sub_attr_len >> 8;

	/* Fill the value */
	memcpy(p_frame, sub_attr, sub_attr_len);

	return sub_attr_len + WLAN_NAN_TLV_HEADER_SIZE;
}


static size_t nan_build_service_info_tlv(u8 *p_frame,
					 const NdpIpTransParams *p_ndp_attr)
{
	u16 tlv_len = 0, len = 0;
	u8 *p_offset_len;

	if (p_ndp_attr->trans_port_present || p_ndp_attr->trans_proto_present) {
		/* fill attribute ID */
		*p_frame++ = NAN_TLV_TYPE_SERVICE_INFO;

		p_offset_len = p_frame;
		p_frame += 2;

		/* Fill WFA Specific OUI */
		memcpy(p_frame, nan_wfa_oui, sizeof(nan_wfa_oui));
		p_frame += sizeof(nan_wfa_oui);
		tlv_len += sizeof(nan_wfa_oui);

		/* Fill Service protocol Type */
		*p_frame++ = NAN_TLV_SERVICE_PROTO_TYPE_GENERIC;
		tlv_len += 1;

		if (p_ndp_attr->trans_port_present) {
			len = nan_build_service_info_tlv_sub_attr(
				p_frame,
				(const u8 *) &p_ndp_attr->transport_port,
				sizeof(p_ndp_attr->transport_port),
				NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_TRANS_PORT);
			p_frame += len;
			tlv_len += len;
		}

		if (p_ndp_attr->trans_proto_present) {
			len = nan_build_service_info_tlv_sub_attr(
				p_frame,
				(const u8 *) &p_ndp_attr->transport_protocol,
				sizeof(p_ndp_attr->transport_protocol),
				NAN_GENERIC_SERVICE_PROTO_SUB_ATTR_ID_TRANS_PROTO);
			p_frame += len;
			tlv_len += len;
		}

		/* Fill the length */
		*p_offset_len++ = tlv_len  & 0xFF;
		*p_offset_len = tlv_len >> 8;

		tlv_len += WLAN_NAN_TLV_HEADER_SIZE;
	}

	return tlv_len;
}


int wait(struct timespec abstime)
{
	struct timeval now;

	gettimeofday(&now, NULL);

	abstime.tv_sec += now.tv_sec;
	if (((abstime.tv_nsec + now.tv_usec * 1000) > 1000 * 1000 * 1000) ||
	    (abstime.tv_nsec + now.tv_usec * 1000 < 0)) {
		abstime.tv_sec += 1;
		abstime.tv_nsec += now.tv_usec * 1000;
		abstime.tv_nsec -= 1000 * 1000 * 1000;
	} else {
		abstime.tv_nsec  += now.tv_usec * 1000;
	}

	return pthread_cond_timedwait(&gCondition, &gMutex, &abstime);
}


int nan_cmd_sta_preset_testparameters(struct sigma_dut *dut,
				      struct sigma_conn *conn,
				      struct sigma_cmd *cmd)
{
	const char *oper_chan = get_param(cmd, "oper_chn");
	const char *pmk = get_param(cmd, "PMK");
#ifdef NAN_NEW_CERT_VERSION
	const char *ndpe = get_param(cmd, "NDPE");
	const char *trans_proto = get_param(cmd, "TransProtoType");
	const char *ndp_attr = get_param(cmd, "ndpAttr");
#endif

	if (oper_chan) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Operating Channel: %s",
				oper_chan);
		dut->sta_channel = atoi(oper_chan);
	}

	if (pmk) {
		int pmk_len;

		sigma_dut_print(dut, DUT_MSG_INFO, "%s given string pmk: %s",
				__func__, pmk);
		memset(dut->nan_pmk, 0, NAN_PMK_INFO_LEN);
		dut->nan_pmk_len = 0;
		pmk_len = NAN_PMK_INFO_LEN;
		nan_parse_hex_string(dut, &pmk[2], &dut->nan_pmk[0], &pmk_len);
		dut->nan_pmk_len = pmk_len;
		sigma_dut_print(dut, DUT_MSG_INFO, "%s: pmk len = %d",
				__func__, dut->nan_pmk_len);
		sigma_dut_print(dut, DUT_MSG_INFO, "%s:hex pmk", __func__);
		nan_hex_dump(dut, &dut->nan_pmk[0], dut->nan_pmk_len);
	}

#ifdef NAN_NEW_CERT_VERSION
	if (ndpe) {
		NanConfigRequest req;
		wifi_error ret;

		sigma_dut_print(dut, DUT_MSG_DEBUG, "%s: NDPE: %s",
				__func__, ndpe);
		memset(&req, 0, sizeof(NanConfigRequest));
		dut->ndpe = strcasecmp(ndpe, "Enable") == 0;
		req.config_ndpe_attr = 1;
		req.use_ndpe_attr = dut->ndpe;
		ret = nan_config_request(0, dut->wifi_hal_iface_handle, &req);
		if (ret != WIFI_SUCCESS) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "NAN config request failed");
			return 0;
		}
	}

	if (trans_proto) {
		sigma_dut_print(dut, DUT_MSG_INFO, "%s: Transport protocol: %s",
				__func__, trans_proto);
		if (strcasecmp(trans_proto, "TCP") == 0) {
			dut->trans_proto = TRANSPORT_PROTO_TYPE_TCP;
		} else if (strcasecmp(trans_proto, "UDP") == 0) {
			dut->trans_proto = TRANSPORT_PROTO_TYPE_UDP;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s: Invalid protocol %s, set to TCP",
					__func__, trans_proto);
			dut->trans_proto = TRANSPORT_PROTO_TYPE_TCP;
		}
	}

	if (dut->ndpe && ndp_attr) {
		NanDebugParams cfg_debug;
		int ndp_attr_val;
		int ret, size;

		sigma_dut_print(dut, DUT_MSG_DEBUG, "%s: NDP Attr: %s",
				__func__, ndp_attr);

		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_ENABLE_NDP;
		if (strcasecmp(ndp_attr, "Absent") == 0)
			ndp_attr_val = NAN_NDP_ATTR_ABSENT;
		else
			ndp_attr_val = NAN_NDP_ATTR_PRESENT;
		memcpy(cfg_debug.debug_cmd_data, &ndp_attr_val, sizeof(int));
		size = sizeof(u32) + sizeof(int);
		ret = nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					       cfg_debug, size);
		if (ret != WIFI_SUCCESS) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "NAN config ndpAttr failed");
			return 0;
		}
	}
#endif

	send_resp(dut, conn, SIGMA_COMPLETE, NULL);
	return 0;
}


void nan_print_further_availability_chan(struct sigma_dut *dut,
					 u8 num_chans,
					 NanFurtherAvailabilityChannel *fachan)
{
	int idx;

	sigma_dut_print(dut, DUT_MSG_INFO,
			"********Printing FurtherAvailabilityChan Info******");
	sigma_dut_print(dut, DUT_MSG_INFO, "Numchans:%d", num_chans);
	for (idx = 0; idx < num_chans; idx++) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"[%d]: NanAvailDuration:%d class_val:%02x channel:%d",
				idx, fachan->entry_control,
				fachan->class_val, fachan->channel);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"[%d]: mapid:%d Availability bitmap:%08x",
				idx, fachan->mapid,
				fachan->avail_interval_bitmap);
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"*********************Done**********************");
}


int sigma_nan_enable(struct sigma_dut *dut, struct sigma_conn *conn,
		     struct sigma_cmd *cmd)
{
	const char *master_pref = get_param(cmd, "MasterPref");
	const char *rand_fac = get_param(cmd, "RandFactor");
	const char *hop_count = get_param(cmd, "HopCount");
	const char *sdftx_band = get_param(cmd, "SDFTxBand");
	const char *oper_chan = get_param(cmd, "oper_chn");
	const char *further_avail_ind = get_param(cmd, "FurtherAvailInd");
	const char *band = get_param(cmd, "Band");
	const char *only_5g = get_param(cmd, "5GOnly");
	const char *nan_availability = get_param(cmd, "NANAvailability");
#ifdef NAN_NEW_CERT_VERSION
	const char *ndpe = get_param(cmd, "NDPE");
#endif
	struct timespec abstime;
	NanEnableRequest req;

	memset(&req, 0, sizeof(NanEnableRequest));
	req.cluster_low = 0;
	req.cluster_high = 0xFFFF;
	req.master_pref = 100;

	/* This is a debug hack to beacon in channel 11 */
	if (oper_chan) {
		req.config_2dot4g_support = 1;
		req.support_2dot4g_val = 111;
	}

	if (master_pref) {
		int master_pref_val = strtoul(master_pref, NULL, 0);

		req.master_pref = master_pref_val;
	}

	if (rand_fac) {
		int rand_fac_val = strtoul(rand_fac, NULL, 0);

		req.config_random_factor_force = 1;
		req.random_factor_force_val = rand_fac_val;
	}

	if (hop_count) {
		int hop_count_val = strtoul(hop_count, NULL, 0);

		req.config_hop_count_force = 1;
		req.hop_count_force_val = hop_count_val;
	}

	if (sdftx_band) {
		if (strcasecmp(sdftx_band, "5G") == 0) {
			req.config_2dot4g_support = 1;
			req.support_2dot4g_val = 0;
		}
	}

#ifdef NAN_NEW_CERT_VERSION
	if (ndpe) {
		if (strcasecmp(ndpe, "Enable") == 0) {
			dut->ndpe = 1;
			req.config_ndpe_attr = 1;
			req.use_ndpe_attr = 1;
		} else {
			dut->ndpe = 0;
			req.config_ndpe_attr = 1;
			req.use_ndpe_attr = 0;
		}
		req.config_disc_mac_addr_randomization = 1;
		req.disc_mac_addr_rand_interval_sec = 0;
	}
#endif

	sigma_dut_print(dut, DUT_MSG_INFO,
			"%s: Setting dual band 2.4 GHz and 5 GHz by default",
			__func__);
	/* Enable 2.4 GHz support */
	req.config_2dot4g_support = 1;
	req.support_2dot4g_val = 1;
	req.config_2dot4g_beacons = 1;
	req.beacon_2dot4g_val = 1;
	req.config_2dot4g_sdf = 1;
	req.sdf_2dot4g_val = 1;

	/* Enable 5 GHz support */
	req.config_support_5g = 1;
	req.support_5g_val = 1;
	req.config_5g_beacons = 1;
	req.beacon_5g_val = 1;
	req.config_5g_sdf = 1;
	req.sdf_5g_val = 1;

	if (band) {
		if (strcasecmp(band, "24G") == 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Band 2.4 GHz selected, disable 5 GHz");
			/* Disable 5G support */
			req.config_support_5g = 1;
			req.support_5g_val = 0;
			req.config_5g_beacons = 1;
			req.beacon_5g_val = 0;
			req.config_5g_sdf = 1;
			req.sdf_5g_val = 0;
		}
	}

	if (further_avail_ind) {
		sigma_dut_print(dut, DUT_MSG_INFO, "FAM Test Enabled");
		if (strcasecmp(further_avail_ind, "tx") == 0) {
			is_fam = 1;
			nan_further_availability_tx(dut, conn, cmd);
			return 0;
		} else if (strcasecmp(further_avail_ind, "rx") == 0) {
			nan_further_availability_rx(dut, conn, cmd);
			return 0;
		}
	}

	if (only_5g && atoi(only_5g)) {
		sigma_dut_print(dut, DUT_MSG_INFO, "5GHz only enabled");
		req.config_2dot4g_support = 1;
		req.support_2dot4g_val = 1;
		req.config_2dot4g_beacons = 1;
		req.beacon_2dot4g_val = 0;
		req.config_2dot4g_sdf = 1;
		req.sdf_2dot4g_val = 1;
	}

	if (if_nametoindex(NAN_AWARE_IFACE))
		run_system_wrapper(dut, "ifconfig %s up", NAN_AWARE_IFACE);

	nan_enable_request(0, dut->wifi_hal_iface_handle, &req);

	if (nan_availability) {
		int cmd_len, size;
		NanDebugParams cfg_debug;

		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s given string nan_availability: %s",
				__func__, nan_availability);
		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_NAN_AVAILABILITY;
		size = NAN_MAX_DEBUG_MESSAGE_DATA_LEN;
		nan_parse_hex_string(dut, &nan_availability[2],
				     &cfg_debug.debug_cmd_data[0], &size);
		sigma_dut_print(dut, DUT_MSG_INFO, "%s:hex nan_availability",
				__func__);
		nan_hex_dump(dut, &cfg_debug.debug_cmd_data[0], size);
		cmd_len = size + sizeof(u32);
		nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					 cfg_debug, cmd_len);
	}

	/* To ensure sta_get_events to get the events
	 * only after joining the NAN cluster. */
	abstime.tv_sec = 30;
	abstime.tv_nsec = 0;
	wait(abstime);

	return 0;
}


int sigma_nan_disable(struct sigma_dut *dut, struct sigma_conn *conn,
		      struct sigma_cmd *cmd)
{
	struct timespec abstime;

	nan_disable_request(0, dut->wifi_hal_iface_handle);

	abstime.tv_sec = 4;
	abstime.tv_nsec = 0;
	wait(abstime);

	return 0;
}


int sigma_nan_config_enable(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	const char *master_pref = get_param(cmd, "MasterPref");
	const char *rand_fac = get_param(cmd, "RandFactor");
	const char *hop_count = get_param(cmd, "HopCount");
	wifi_error ret;
	struct timespec abstime;
	NanConfigRequest req;

	memset(&req, 0, sizeof(NanConfigRequest));
	req.config_rssi_proximity = 1;
	req.rssi_proximity = 70;

	if (master_pref) {
		int master_pref_val = strtoul(master_pref, NULL, 0);

		req.config_master_pref = 1;
		req.master_pref = master_pref_val;
	}

	if (rand_fac) {
		int rand_fac_val = strtoul(rand_fac, NULL, 0);

		req.config_random_factor_force = 1;
		req.random_factor_force_val = rand_fac_val;
	}

	if (hop_count) {
		int hop_count_val = strtoul(hop_count, NULL, 0);

		req.config_hop_count_force = 1;
		req.hop_count_force_val = hop_count_val;
	}

	ret = nan_config_request(0, dut->wifi_hal_iface_handle, &req);
	if (ret != WIFI_SUCCESS)
		send_resp(dut, conn, SIGMA_ERROR, "NAN config request failed");

	abstime.tv_sec = 4;
	abstime.tv_nsec = 0;
	wait(abstime);

	return 0;
}


static int sigma_nan_subscribe_request(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd)
{
	const char *subscribe_type = get_param(cmd, "SubscribeType");
	const char *service_name = get_param(cmd, "ServiceName");
	const char *disc_range = get_param(cmd, "DiscoveryRange");
	const char *rx_match_filter = get_param(cmd, "rxMatchFilter");
	const char *tx_match_filter = get_param(cmd, "txMatchFilter");
	const char *sdftx_dw = get_param(cmd, "SDFTxDW");
	const char *discrange_ltd = get_param(cmd, "DiscRangeLtd");
	const char *include_bit = get_param(cmd, "IncludeBit");
	const char *mac = get_param(cmd, "MAC");
	const char *srf_type = get_param(cmd, "SRFType");
#if NAN_CERT_VERSION >= 3
	const char *awake_dw_interval = get_param(cmd, "awakeDWint");
#endif
	NanSubscribeRequest req;
	NanConfigRequest config_req;
	int filter_len_rx = 0, filter_len_tx = 0;
	u8 input_rx[NAN_MAX_MATCH_FILTER_LEN];
	u8 input_tx[NAN_MAX_MATCH_FILTER_LEN];
	wifi_error ret;

	memset(&req, 0, sizeof(NanSubscribeRequest));
	memset(&config_req, 0, sizeof(NanConfigRequest));
	req.ttl = 0;
	req.period = 1;
	req.subscribe_type = 1;
	req.serviceResponseFilter = 1; /* MAC */
	req.serviceResponseInclude = 0;
	req.ssiRequiredForMatchIndication = 0;
	req.subscribe_match_indicator = NAN_MATCH_ALG_MATCH_CONTINUOUS;
	req.subscribe_count = 0;

	if (global_subscribe_service_name_len &&
	    service_name &&
	    strcasecmp((char *) global_subscribe_service_name,
		       service_name) == 0 &&
	    global_subscribe_id) {
		req.subscribe_id = global_subscribe_id;
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: updating subscribe_id = %d in subscribe request",
				__func__, req.subscribe_id);
	}

	if (subscribe_type) {
		if (strcasecmp(subscribe_type, "Active") == 0) {
			req.subscribe_type = 1;
		} else if (strcasecmp(subscribe_type, "Passive") == 0) {
			req.subscribe_type = 0;
		} else if (strcasecmp(subscribe_type, "Cancel") == 0) {
			NanSubscribeCancelRequest req;

			memset(&req, 0, sizeof(NanSubscribeCancelRequest));
			ret = nan_subscribe_cancel_request(
				0, dut->wifi_hal_iface_handle, &req);
			if (ret != WIFI_SUCCESS) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "NAN subscribe cancel request failed");
			}
			return 0;
		}
	}

	if (disc_range)
		req.rssi_threshold_flag = atoi(disc_range);

	if (sdftx_dw)
		req.subscribe_count = atoi(sdftx_dw);

	/* Check this once again if config can be called here (TBD) */
	if (discrange_ltd)
		req.rssi_threshold_flag = atoi(discrange_ltd);

	if (include_bit) {
		int include_bit_val = atoi(include_bit);

		req.serviceResponseInclude = include_bit_val;
		sigma_dut_print(dut, DUT_MSG_INFO, "Includebit set %d",
				req.serviceResponseInclude);
	}

	if (srf_type) {
		int srf_type_val = atoi(srf_type);

		if (srf_type_val == 1)
			req.serviceResponseFilter = 0; /* Bloom */
		else
			req.serviceResponseFilter = 1; /* MAC */
		req.useServiceResponseFilter = 1;
		sigma_dut_print(dut, DUT_MSG_INFO, "srfFilter %d",
				req.serviceResponseFilter);
	}

	if (mac) {
		sigma_dut_print(dut, DUT_MSG_INFO, "MAC_ADDR List %s", mac);
		req.num_intf_addr_present = nan_parse_mac_address_list(
			dut, mac, &req.intf_addr[0][0],
			NAN_MAX_SUBSCRIBE_MAX_ADDRESS);
	}

	memset(input_rx, 0, sizeof(input_rx));
	memset(input_tx, 0, sizeof(input_tx));
	if (rx_match_filter) {
		nan_parse_token(rx_match_filter, input_rx, &filter_len_rx);
		sigma_dut_print(dut, DUT_MSG_INFO, "RxFilterLen %d",
				filter_len_rx);
	}
	if (tx_match_filter) {
		nan_parse_token(tx_match_filter, input_tx, &filter_len_tx);
		sigma_dut_print(dut, DUT_MSG_INFO, "TxFilterLen %d",
				filter_len_tx);
	}

	if (tx_match_filter) {
		req.tx_match_filter_len = filter_len_tx;
		memcpy(req.tx_match_filter, input_tx, filter_len_tx);
		nan_hex_dump(dut, req.tx_match_filter, filter_len_tx);
	}
	if (rx_match_filter) {
		req.rx_match_filter_len = filter_len_rx;
		memcpy(req.rx_match_filter, input_rx, filter_len_rx);
		nan_hex_dump(dut, req.rx_match_filter, filter_len_rx);
	}

	if (service_name) {
		strlcpy((char *) req.service_name, service_name,
			strlen(service_name) + 1);
		req.service_name_len = strlen(service_name);
		strlcpy((char *) global_subscribe_service_name, service_name,
			sizeof(global_subscribe_service_name));
		global_subscribe_service_name_len =
			strlen((char *) global_subscribe_service_name);
	}

#if NAN_CERT_VERSION >= 3
	if (awake_dw_interval) {
		int input_dw_interval_val = atoi(awake_dw_interval);
		int awake_dw_int = 0;

		if (input_dw_interval_val > NAN_MAX_ALLOWED_DW_AWAKE_INTERVAL) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"%s: input active dw interval = %d overwritting dw interval to Max allowed dw interval 16",
					__func__, input_dw_interval_val);
			input_dw_interval_val =
				NAN_MAX_ALLOWED_DW_AWAKE_INTERVAL;
		}
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: input active DW interval = %d",
				__func__, input_dw_interval_val);
		/*
		 * Indicates the interval for Sync beacons and SDF's in 2.4 GHz
		 * or 5 GHz band. Valid values of DW Interval are: 1, 2, 3, 4,
		 * and 5; 0 is reserved. The SDF includes in OTA when enabled.
		 * The publish/subscribe period values don't override the device
		 * level configurations.
		 * input_dw_interval_val is provided by the user are in the
		 * format 2^n-1 = 1/2/4/8/16. Internal implementation expects n
		 * to be passed to indicate the awake_dw_interval.
		 */
		if (input_dw_interval_val == 1 ||
		    input_dw_interval_val % 2 == 0) {
			while (input_dw_interval_val > 0) {
				input_dw_interval_val >>= 1;
				awake_dw_int++;
			}
		}
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s:converted active DW interval = %d",
				__func__, awake_dw_int);
		config_req.config_dw.config_2dot4g_dw_band = 1;
		config_req.config_dw.dw_2dot4g_interval_val = awake_dw_int;
		config_req.config_dw.config_5g_dw_band = 1;
		config_req.config_dw.dw_5g_interval_val = awake_dw_int;
		ret = nan_config_request(0, dut->wifi_hal_iface_handle,
					 &config_req);
		if (ret != WIFI_SUCCESS) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s:NAN config request failed",
					__func__);
			return -2;
		}
	}
#endif

	ret = nan_subscribe_request(0, dut->wifi_hal_iface_handle, &req);
	if (ret != WIFI_SUCCESS) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "NAN subscribe request failed");
	}

	return 0;
}


static int sigma_ndp_configure_band(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd,
				    NdpSupportedBand band_config_val)
{
	wifi_error ret;
	NanDebugParams cfg_debug;
	int size;

	memset(&cfg_debug, 0, sizeof(NanDebugParams));
	cfg_debug.cmd = NAN_TEST_MODE_CMD_NAN_SUPPORTED_BANDS;
	memcpy(cfg_debug.debug_cmd_data, &band_config_val, sizeof(int));
	sigma_dut_print(dut, DUT_MSG_INFO, "%s:setting debug cmd=0x%x",
			__func__, cfg_debug.cmd);
	size = sizeof(u32) + sizeof(int);
	ret = nan_debug_command_config(0, dut->wifi_hal_iface_handle, cfg_debug,
				       size);
	if (ret != WIFI_SUCCESS)
		send_resp(dut, conn, SIGMA_ERROR, "Nan config request failed");

	return 0;
}


static int sigma_nan_data_request(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	const char *ndp_security = get_param(cmd, "DataPathSecurity");
	const char *ndp_resp_mac = get_param(cmd, "RespNanMac");
	const char *include_immutable = get_param(cmd, "includeimmutable");
	const char *avoid_channel = get_param(cmd, "avoidchannel");
	const char *invalid_nan_schedule = get_param(cmd, "InvalidNANSchedule");
	const char *map_order = get_param(cmd, "maporder");
#if NAN_CERT_VERSION >= 3
	const char *qos_config = get_param(cmd, "QoS");
#endif
#ifdef NAN_NEW_CERT_VERSION
	const char *ndpe_enable = get_param(cmd, "Ndpe");
	const char *ndpe_attr = get_param(cmd, "ndpeAttr");
	const char *ndp_attr = get_param(cmd, "ndpAttr");
	const char *tlv_list = get_param(cmd, "TLVList");
#endif
	wifi_error ret;
	NanDataPathInitiatorRequest init_req;
	NanDebugParams cfg_debug;
	int size;

	memset(&init_req, 0, sizeof(NanDataPathInitiatorRequest));

	if (ndp_security) {
		if (strcasecmp(ndp_security, "open") == 0)
			init_req.ndp_cfg.security_cfg =
				NAN_DP_CONFIG_NO_SECURITY;
		else if (strcasecmp(ndp_security, "secure") == 0)
			init_req.ndp_cfg.security_cfg = NAN_DP_CONFIG_SECURITY;
	}

	if (include_immutable) {
		int include_immutable_val = 0;

		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_NDP_INCLUDE_IMMUTABLE;
		include_immutable_val = atoi(include_immutable);
		memcpy(cfg_debug.debug_cmd_data, &include_immutable_val,
		       sizeof(int));
		size = sizeof(u32) + sizeof(int);
		nan_debug_command_config(0, dut->wifi_hal_iface_handle,
		cfg_debug, size);
	}

	if (avoid_channel) {
		int avoid_channel_freq = 0;

		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		avoid_channel_freq = channel_to_freq(dut, atoi(avoid_channel));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_NDP_AVOID_CHANNEL;
		memcpy(cfg_debug.debug_cmd_data, &avoid_channel_freq,
		       sizeof(int));
		size = sizeof(u32) + sizeof(int);
		nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					 cfg_debug, size);
	}

	if (invalid_nan_schedule) {
		int invalid_nan_schedule_type = 0;

		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		invalid_nan_schedule_type = atoi(invalid_nan_schedule);
		cfg_debug.cmd = NAN_TEST_MODE_CMD_NAN_SCHED_TYPE;
		memcpy(cfg_debug.debug_cmd_data,
		       &invalid_nan_schedule_type, sizeof(int));
		size = sizeof(u32) + sizeof(int);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: invalid schedule type: cmd type = %d and command data = %d",
				__func__, cfg_debug.cmd,
				invalid_nan_schedule_type);
		nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					 cfg_debug, size);
	}

	if (map_order) {
		int map_order_val = 0;

		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_NAN_AVAILABILITY_MAP_ORDER;
		map_order_val = atoi(map_order);
		memcpy(cfg_debug.debug_cmd_data, &map_order_val, sizeof(int));
		size = sizeof(u32) + sizeof(int);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: map order: cmd type = %d and command data = %d",
				__func__,
				cfg_debug.cmd, map_order_val);
		nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					 cfg_debug, size);
	}

#if NAN_CERT_VERSION >= 3
	if (qos_config) {
		u32 qos_config_val = 0;

		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_CONFIG_QOS;
		qos_config_val = atoi(qos_config);
		memcpy(cfg_debug.debug_cmd_data, &qos_config_val, sizeof(u32));
		size = sizeof(u32) + sizeof(u32);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: qos config: cmd type = %d and command data = %d",
				__func__, cfg_debug.cmd, qos_config_val);
		nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					 cfg_debug, size);
	}
#endif

#ifdef NAN_NEW_CERT_VERSION
	if (ndpe_enable &&
	    strcasecmp(ndpe_enable, "Enable") == 0)
		dut->ndpe = 1;

	if (dut->ndpe && ndp_attr) {
		NanDebugParams cfg_debug;
		int ndp_attr_val;

		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_ENABLE_NDP;
		if (strcasecmp(ndp_attr, "Absent") == 0)
			ndp_attr_val = NAN_NDP_ATTR_ABSENT;
		else
			ndp_attr_val = NAN_NDP_ATTR_PRESENT;
		memcpy(cfg_debug.debug_cmd_data, &ndp_attr_val, sizeof(int));
		size = sizeof(u32) + sizeof(int);
		ret = nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					       cfg_debug, size);
		if (ret != WIFI_SUCCESS) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "NAN config ndpAttr failed");
			return 0;
		}
	}

	if (dut->ndpe && ndpe_attr) {
		NanDebugParams cfg_debug;
		int ndpe_attr_val;

		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_DISABLE_NDPE;
		if (strcasecmp(ndpe_attr, "Absent") == 0)
			ndpe_attr_val = NAN_NDPE_ATTR_ABSENT;
		else
			ndpe_attr_val = NAN_NDPE_ATTR_PRESENT;
		memcpy(cfg_debug.debug_cmd_data, &ndpe_attr_val, sizeof(int));
		size = sizeof(u32) + sizeof(int);
		ret = nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					       cfg_debug, size);
		if (ret != WIFI_SUCCESS) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "NAN config ndpeAttr failed");
			return 0;
		}
	}
#endif

	/*
	 * Setting this flag, so that interface for ping6 command
	 * is set appropriately in traffic_send_ping().
	 */
	dut->ndp_enable = 1;

	/*
	 * Intended sleep after NAN data interface create
	 * before the NAN data request
	 */
	sleep(4);

	init_req.requestor_instance_id = global_match_handle;
	strlcpy((char *) init_req.ndp_iface, "nan0",
		sizeof(init_req.ndp_iface));

	if (ndp_resp_mac) {
		nan_parse_mac_address(dut, ndp_resp_mac,
				      init_req.peer_disc_mac_addr);
		sigma_dut_print(
			dut, DUT_MSG_INFO, "PEER MAC ADDR: " MAC_ADDR_STR,
			MAC_ADDR_ARRAY(init_req.peer_disc_mac_addr));
	} else {
		memcpy(init_req.peer_disc_mac_addr, global_peer_mac_addr,
		       sizeof(init_req.peer_disc_mac_addr));
	}

	/* Not requesting the channel and letting FW decide */
	if (dut->sta_channel == 0) {
		init_req.channel_request_type = NAN_DP_CHANNEL_NOT_REQUESTED;
		init_req.channel = 0;
	} else {
		init_req.channel_request_type = NAN_DP_FORCE_CHANNEL_SETUP;
		init_req.channel = channel_to_freq(dut, dut->sta_channel);
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"%s: Initiator Request: Channel = %d  Channel Request Type = %d",
			__func__, init_req.channel,
			init_req.channel_request_type);

	if (dut->nan_pmk_len == NAN_PMK_INFO_LEN) {
		init_req.key_info.key_type = NAN_SECURITY_KEY_INPUT_PMK;
		memcpy(&init_req.key_info.body.pmk_info.pmk[0],
		       &dut->nan_pmk[0], NAN_PMK_INFO_LEN);
		init_req.key_info.body.pmk_info.pmk_len = NAN_PMK_INFO_LEN;
		sigma_dut_print(dut, DUT_MSG_INFO, "%s: pmk len = %d",
				__func__,
				init_req.key_info.body.pmk_info.pmk_len);
	}

#ifdef NAN_NEW_CERT_VERSION
	if (dut->ndpe) {
		if (dut->device_type == STA_testbed && !tlv_list) {
			init_req.app_info.ndp_app_info_len = 0;
			memset(init_req.app_info.ndp_app_info, 0,
			       sizeof(init_req.app_info.ndp_app_info));
		} else {
			size_t addr_len = 0;
			u8 nan_ipv6_intf_addr[IPV6_ADDR_LEN];
			unsigned char nan_mac_addr[ETH_ALEN];

			if (get_hwaddr("nan0", nan_mac_addr) < 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"%s:get_hwaddr nan0 failed",
						__func__);
				return -1;
			}

			/* store IPv6 into app_info as TLV */
			addr_len = convert_mac_addr_to_ipv6_linklocal(
				nan_mac_addr, &nan_ipv6_intf_addr[0]);
			init_req.app_info.ndp_app_info_len =
				nan_build_ipv6_link_local_tlv(
					init_req.app_info.ndp_app_info,
					&nan_ipv6_intf_addr[0]);
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"%s: Initiator Request: IPv6:",
					__func__);
			nan_hex_dump(dut, &nan_ipv6_intf_addr[0],
				     IPV6_ADDR_LEN);
		}
	}
#endif

	ret = nan_data_request_initiator(0, dut->wifi_hal_iface_handle,
					 &init_req);
	if (ret != WIFI_SUCCESS) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "Unable to initiate nan data request");
		return 0;
	}

	return 0;
}


static int sigma_nan_data_response(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	const char *ndl_response = get_param(cmd, "NDLresponse");
	const char *m4_response_type = get_param(cmd, "M4ResponseType");
#ifdef NAN_NEW_CERT_VERSION
	const char *ndpe_attr = get_param(cmd, "ndpeAttr");
	const char *ndp_attr = get_param(cmd, "ndpAttr");
#endif
	wifi_error ret;
	NanDebugParams cfg_debug;
	int size;

	if (ndl_response) {
		int auto_responder_mode_val = 0;

		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: ndl_response = (%s) is passed",
				__func__, ndl_response);
		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_AUTO_RESPONDER_MODE;
		if (strcasecmp(ndl_response, "Auto") == 0) {
			auto_responder_mode_val = NAN_DATA_RESPONDER_MODE_AUTO;
		} else if (strcasecmp(ndl_response, "Reject") == 0) {
			auto_responder_mode_val =
				NAN_DATA_RESPONDER_MODE_REJECT;
		} else if (strcasecmp(ndl_response, "Accept") == 0) {
			auto_responder_mode_val =
				NAN_DATA_RESPONDER_MODE_ACCEPT;
		} else if (strcasecmp(ndl_response, "Counter") == 0) {
			auto_responder_mode_val =
				NAN_DATA_RESPONDER_MODE_COUNTER;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s: Invalid ndl_response",
					__func__);
			return 0;
		}
		memcpy(cfg_debug.debug_cmd_data, &auto_responder_mode_val,
		       sizeof(int));
		size = sizeof(u32) + sizeof(int);
		ret = nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					       cfg_debug, size);
		if (ret != WIFI_SUCCESS) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "Nan config request failed");
		}
	}

	if (m4_response_type) {
		int m4_response_type_val = 0;

		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: m4_response_type = (%s) is passed",
				__func__, m4_response_type);
		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_M4_RESPONSE_TYPE;
		if (strcasecmp(m4_response_type, "Accept") == 0)
			m4_response_type_val = NAN_DATA_PATH_M4_RESPONSE_ACCEPT;
		else if (strcasecmp(m4_response_type, "Reject") == 0)
			m4_response_type_val = NAN_DATA_PATH_M4_RESPONSE_REJECT;
		else if (strcasecmp(m4_response_type, "BadMic") == 0)
			m4_response_type_val = NAN_DATA_PATH_M4_RESPONSE_BADMIC;

		memcpy(cfg_debug.debug_cmd_data, &m4_response_type_val,
		       sizeof(int));
		size = sizeof(u32) + sizeof(int);
		ret = nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					       cfg_debug, size);
		if (ret != WIFI_SUCCESS) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "Nan config request failed");
		}
	}

#ifdef NAN_NEW_CERT_VERSION
	if (dut->ndpe && ndp_attr) {
		NanDebugParams cfg_debug;
		int ndp_attr_val;

		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_ENABLE_NDP;
		if (strcasecmp(ndp_attr, "Absent") == 0)
			ndp_attr_val = NAN_NDP_ATTR_ABSENT;
		else
			ndp_attr_val = NAN_NDP_ATTR_PRESENT;
		memcpy(cfg_debug.debug_cmd_data, &ndp_attr_val, sizeof(int));
		size = sizeof(u32) + sizeof(int);
		ret = nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					       cfg_debug, size);
		if (ret != WIFI_SUCCESS) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "NAN config ndpAttr failed");
			return 0;
		}
	}

	if (ndpe_attr && dut->ndpe) {
		int ndpe_attr_val;

		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_DISABLE_NDPE;
		if (strcasecmp(ndpe_attr, "Absent") == 0)
			ndpe_attr_val = NAN_NDPE_ATTR_ABSENT;
		else
			ndpe_attr_val = NAN_NDPE_ATTR_PRESENT;
		memcpy(cfg_debug.debug_cmd_data, &ndpe_attr_val, sizeof(int));
		size = sizeof(u32) + sizeof(int);
		ret = nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					       cfg_debug, size);
		if (ret != WIFI_SUCCESS) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "NAN config ndpeAttr failed");
			return 0;
		}
	}
#endif

	return 0;
}


static int sigma_nan_data_end(struct sigma_dut *dut, struct sigma_cmd *cmd)
{
	const char *nmf_security_config = get_param(cmd, "Security");
	NanDataPathEndRequest req;
	NanDebugParams cfg_debug;
	int size;

	memset(&req, 0, sizeof(NanDataPathEndRequest));
	memset(&cfg_debug, 0, sizeof(NanDebugParams));
	if (nmf_security_config) {
		int nmf_security_config_val = 0;

		cfg_debug.cmd = NAN_TEST_MODE_CMD_NAN_NMF_CLEAR_CONFIG;
		if (strcasecmp(nmf_security_config, "open") == 0)
			nmf_security_config_val = NAN_NMF_CLEAR_ENABLE;
		else if (strcasecmp(nmf_security_config, "secure") == 0)
			nmf_security_config_val = NAN_NMF_CLEAR_DISABLE;
		memcpy(cfg_debug.debug_cmd_data,
			&nmf_security_config_val, sizeof(int));
		size = sizeof(u32) + sizeof(int);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: nmf_security_config_val -- cmd type = %d and command data = %d",
				__func__, cfg_debug.cmd,
				nmf_security_config_val);
		nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					 cfg_debug, size);
	}

	req.num_ndp_instances = 1;
	req.ndp_instance_id[0] = global_ndp_instance_id;

	nan_data_end(0, dut->wifi_hal_iface_handle, &req);
	return 0;
}


static int sigma_nan_range_request(struct sigma_dut *dut,
				   struct sigma_cmd *cmd)
{
	const char *dest_mac = get_param(cmd, "destmac");
	NanSubscribeRequest req;

	memset(&req, 0, sizeof(NanSubscribeRequest));
	req.period = 1;
	req.subscribe_type = NAN_SUBSCRIBE_TYPE_PASSIVE;
	req.serviceResponseFilter = NAN_SRF_ATTR_BLOOM_FILTER;
	req.serviceResponseInclude = NAN_SRF_INCLUDE_RESPOND;
	req.ssiRequiredForMatchIndication = NAN_SSI_NOT_REQUIRED_IN_MATCH_IND;
	req.subscribe_match_indicator = NAN_MATCH_ALG_MATCH_CONTINUOUS;
	req.subscribe_count = 0;
	strlcpy((char *) req.service_name, DEFAULT_SVC,
		NAN_MAX_SERVICE_NAME_LEN);
	req.service_name_len = strlen((char *) req.service_name);

	req.subscribe_id = global_subscribe_id;
	req.sdea_params.ranging_state = 1;
	req.sdea_params.range_report = NAN_ENABLE_RANGE_REPORT;
	req.range_response_cfg.requestor_instance_id = global_match_handle;
	req.range_response_cfg.ranging_response = NAN_RANGE_REQUEST_ACCEPT;
	req.ranging_cfg.config_ranging_indications =
		NAN_RANGING_INDICATE_CONTINUOUS_MASK;
	if (dest_mac) {
		nan_parse_mac_address(dut, dest_mac,
				      req.range_response_cfg.peer_addr);
		sigma_dut_print(
			dut, DUT_MSG_INFO, "peer mac addr: " MAC_ADDR_STR,
			MAC_ADDR_ARRAY(req.range_response_cfg.peer_addr));
	}
	nan_subscribe_request(0, dut->wifi_hal_iface_handle, &req);

	return 0;
}


static int sigma_nan_cancel_range(struct sigma_dut *dut,
				  struct sigma_cmd *cmd)
{
	const char *dest_mac = get_param(cmd, "destmac");
	NanPublishRequest req;

	memset(&req, 0, sizeof(NanPublishRequest));
	req.ttl = 0;
	req.period = 1;
	req.publish_match_indicator = 1;
	req.publish_type = NAN_PUBLISH_TYPE_UNSOLICITED;
	req.tx_type = NAN_TX_TYPE_BROADCAST;
	req.publish_count = 0;
	strlcpy((char *) req.service_name, DEFAULT_SVC,
		NAN_MAX_SERVICE_NAME_LEN);
	req.service_name_len = strlen((char *) req.service_name);
	req.publish_id = global_publish_id;
	req.range_response_cfg.ranging_response = NAN_RANGE_REQUEST_CANCEL;
	if (dest_mac) {
		nan_parse_mac_address(dut, dest_mac,
				      req.range_response_cfg.peer_addr);
		sigma_dut_print(
			dut, DUT_MSG_INFO, "peer mac addr: " MAC_ADDR_STR,
			MAC_ADDR_ARRAY(req.range_response_cfg.peer_addr));
	}
	nan_publish_request(0, dut->wifi_hal_iface_handle, &req);

	return 0;
}


static int sigma_nan_schedule_update(struct sigma_dut *dut,
				     struct sigma_cmd *cmd)
{
	const char *schedule_update_type = get_param(cmd, "type");
	const char *channel_availability = get_param(cmd,
						     "ChannelAvailability");
	const char *responder_nmi_mac = get_param(cmd, "ResponderNMI");
	NanDebugParams cfg_debug;
	int size = 0;

	memset(&cfg_debug, 0, sizeof(NanDebugParams));

	if (!schedule_update_type)
		return 0;

	if (strcasecmp(schedule_update_type, "ULWnotify") == 0) {
		cfg_debug.cmd = NAN_TEST_MODE_CMD_NAN_SCHED_UPDATE_ULW_NOTIFY;
		size = sizeof(u32);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: Schedule Update cmd type = %d", __func__,
				cfg_debug.cmd);
		if (channel_availability) {
			int channel_availability_val;

			channel_availability_val = atoi(channel_availability);
			size += sizeof(int);
			memcpy(cfg_debug.debug_cmd_data,
			       &channel_availability_val, sizeof(int));
			sigma_dut_print(dut, DUT_MSG_INFO,
					"%s: Schedule Update cmd data = %d size = %d",
					__func__, channel_availability_val,
					size);
		}
	} else if (strcasecmp(schedule_update_type, "NDLnegotiate") == 0) {
		cfg_debug.cmd =
			NAN_TEST_MODE_CMD_NAN_SCHED_UPDATE_NDL_NEGOTIATE;
		size = sizeof(u32);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: Schedule Update cmd type = %d", __func__,
				cfg_debug.cmd);
		if (responder_nmi_mac) {
			u8 responder_nmi_mac_addr[NAN_MAC_ADDR_LEN];

			nan_parse_mac_address(dut, responder_nmi_mac,
					      responder_nmi_mac_addr);
			size += NAN_MAC_ADDR_LEN;
			memcpy(cfg_debug.debug_cmd_data, responder_nmi_mac_addr,
			       NAN_MAC_ADDR_LEN);
			sigma_dut_print(dut, DUT_MSG_INFO,
					"%s: RESPONDER NMI MAC: "MAC_ADDR_STR,
					__func__,
					MAC_ADDR_ARRAY(responder_nmi_mac_addr));
			sigma_dut_print(dut, DUT_MSG_INFO,
					"%s: Schedule Update: cmd size = %d",
					__func__, size);
		}
	} else if (strcasecmp(schedule_update_type, "NDLnotify") == 0) {
		cfg_debug.cmd = NAN_TEST_MODE_CMD_NAN_SCHED_UPDATE_NDL_NOTIFY;
		size = sizeof(u32);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: Schedule Update cmd type = %d", __func__,
				cfg_debug.cmd);
	}

	nan_debug_command_config(0, dut->wifi_hal_iface_handle, cfg_debug,
				 size);

	return 0;
}


int config_post_disc_attr(struct sigma_dut *dut)
{
	wifi_error ret;
	NanConfigRequest configReq;

	memset(&configReq, 0, sizeof(NanConfigRequest));

	/* Configure Post disc attr */
	/* Make these defines and use correct enum */
	configReq.num_config_discovery_attr = 1;
	configReq.discovery_attr_val[0].type = 4; /* Further Nan discovery */
	configReq.discovery_attr_val[0].role = 0;
	configReq.discovery_attr_val[0].transmit_freq = 1;
	configReq.discovery_attr_val[0].duration = 0;
	configReq.discovery_attr_val[0].avail_interval_bitmap = 0x00000008;

	ret = nan_config_request(0, dut->wifi_hal_iface_handle, &configReq);
	if (ret != WIFI_SUCCESS) {
		sigma_dut_print(global_dut, DUT_MSG_INFO,
				"NAN config request failed while configuring post discovery attribute");
	}

	return 0;
}


int sigma_nan_publish_request(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *publish_type = get_param(cmd, "PublishType");
	const char *service_name = get_param(cmd, "ServiceName");
	const char *disc_range = get_param(cmd, "DiscoveryRange");
	const char *rx_match_filter = get_param(cmd, "rxMatchFilter");
	const char *tx_match_filter = get_param(cmd, "txMatchFilter");
	const char *sdftx_dw = get_param(cmd, "SDFTxDW");
	const char *discrange_ltd = get_param(cmd, "DiscRangeLtd");
	const char *ndp_enable = get_param(cmd, "DataPathFlag");
	const char *ndp_type = get_param(cmd, "DataPathType");
	const char *data_path_security = get_param(cmd, "datapathsecurity");
	const char *range_required = get_param(cmd, "rangerequired");
#if NAN_CERT_VERSION >= 3
	const char *awake_dw_interval = get_param(cmd, "awakeDWint");
	const char *qos_config = get_param(cmd, "QoS");
#endif
	const char *ndpe = get_param(cmd, "NDPE");
	const char *trans_proto = get_param(cmd, "TransProtoType");
#ifdef NAN_NEW_CERT_VERSION
	const char *ndp_attr = get_param(cmd, "ndpAttr");
#endif
	NanPublishRequest req;
	NanConfigRequest config_req;
	int filter_len_rx = 0, filter_len_tx = 0;
	u8 input_rx[NAN_MAX_MATCH_FILTER_LEN];
	u8 input_tx[NAN_MAX_MATCH_FILTER_LEN];
	wifi_error ret;

	memset(&req, 0, sizeof(NanPublishRequest));
	memset(&config_req, 0, sizeof(NanConfigRequest));
	req.ttl = 0;
	req.period = 1;
	req.publish_match_indicator = 1;
	req.publish_type = NAN_PUBLISH_TYPE_UNSOLICITED;
	req.tx_type = NAN_TX_TYPE_BROADCAST;
	req.publish_count = 0;
	req.service_responder_policy = NAN_SERVICE_ACCEPT_POLICY_ALL;

	if (global_publish_service_name_len &&
	    service_name &&
	    strcasecmp((char *) global_publish_service_name,
		       service_name) == 0 &&
	    global_publish_id) {
		req.publish_id = global_publish_id;
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: updating publish_id = %d in publish request",
				__func__, req.publish_id);
	}

	if (service_name) {
		strlcpy((char *) req.service_name, service_name,
			sizeof(req.service_name));
		req.service_name_len = strlen((char *) req.service_name);
		strlcpy((char *) global_publish_service_name, service_name,
			sizeof(global_publish_service_name));
		global_publish_service_name_len =
			strlen((char *) global_publish_service_name);
	}

	if (publish_type) {
		if (strcasecmp(publish_type, "Solicited") == 0) {
			req.publish_type = NAN_PUBLISH_TYPE_SOLICITED;
		} else if (strcasecmp(publish_type, "Unsolicited") == 0) {
			req.publish_type = NAN_PUBLISH_TYPE_UNSOLICITED;
		} else if (strcasecmp(publish_type, "Cancel") == 0) {
			NanPublishCancelRequest req;

			memset(&req, 0, sizeof(NanPublishCancelRequest));
			ret = nan_publish_cancel_request(
				0, dut->wifi_hal_iface_handle, &req);
			if (ret != WIFI_SUCCESS) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "Unable to cancel nan publish request");
			}
			return 0;
		}
	}

	if (disc_range)
		req.rssi_threshold_flag = atoi(disc_range);

	if (sdftx_dw)
		req.publish_count = atoi(sdftx_dw);

	if (discrange_ltd)
		req.rssi_threshold_flag = atoi(discrange_ltd);

	memset(input_rx, 0, sizeof(input_rx));
	memset(input_tx, 0, sizeof(input_tx));
	if (rx_match_filter) {
		nan_parse_token(rx_match_filter, input_rx, &filter_len_rx);
		sigma_dut_print(dut, DUT_MSG_INFO, "RxFilterLen %d",
				filter_len_rx);
	}
	if (tx_match_filter) {
		nan_parse_token(tx_match_filter, input_tx, &filter_len_tx);
		sigma_dut_print(dut, DUT_MSG_INFO, "TxFilterLen %d",
				filter_len_tx);
	}

	if (is_fam == 1) {
		config_post_disc_attr(dut);
		/*
		 * 8-bit bitmap which allows the Host to associate this publish
		 * with a particular Post-NAN Connectivity attribute which has
		 * been sent down in a NanConfigureRequest/NanEnableRequest
		 * message. If the DE fails to find a configured Post-NAN
		 * connectivity attributes referenced by the bitmap, the DE will
		 * return an error code to the Host. If the Publish is
		 * configured to use a Post-NAN Connectivity attribute and the
		 * Host does not refresh the Post-NAN Connectivity attribute the
		 * Publish will be canceled and the Host will be sent a
		 * PublishTerminatedIndication message.
		 */
		req.connmap = 0x10;
	}

	if (tx_match_filter) {
		req.tx_match_filter_len = filter_len_tx;
		memcpy(req.tx_match_filter, input_tx, filter_len_tx);
		nan_hex_dump(dut, req.tx_match_filter, filter_len_tx);
	}

	if (rx_match_filter) {
		req.rx_match_filter_len = filter_len_rx;
		memcpy(req.rx_match_filter, input_rx, filter_len_rx);
		nan_hex_dump(dut, req.rx_match_filter, filter_len_rx);
	}

	if (service_name) {
		strlcpy((char *) req.service_name, service_name,
			strlen(service_name) + 1);
		req.service_name_len = strlen(service_name);
	}

	if (ndp_enable) {
		if (strcasecmp(ndp_enable, "enable") == 0)
			req.sdea_params.config_nan_data_path = 1;
		else
			req.sdea_params.config_nan_data_path = 0;

		if (ndp_type)
			req.sdea_params.ndp_type = atoi(ndp_type);

		if (data_path_security) {
			if (strcasecmp(data_path_security, "secure") == 0) {
				req.sdea_params.security_cfg =
					NAN_DP_CONFIG_SECURITY;
			} else if (strcasecmp(data_path_security, "open") ==
				   0) {
				req.sdea_params.security_cfg =
					NAN_DP_CONFIG_NO_SECURITY;
			}
		}

		if (dut->nan_pmk_len == NAN_PMK_INFO_LEN) {
			req.key_info.key_type = NAN_SECURITY_KEY_INPUT_PMK;
			memcpy(&req.key_info.body.pmk_info.pmk[0],
				&dut->nan_pmk[0], NAN_PMK_INFO_LEN);
			req.key_info.body.pmk_info.pmk_len = NAN_PMK_INFO_LEN;
			sigma_dut_print(dut, DUT_MSG_INFO, "%s: pmk len = %d",
			__func__, req.key_info.body.pmk_info.pmk_len);
		}
	}
	if (range_required && strcasecmp(range_required, "enable") == 0) {
		req.sdea_params.ranging_state = NAN_RANGING_ENABLE;
		req.sdea_params.range_report = NAN_ENABLE_RANGE_REPORT;
	}

#if NAN_CERT_VERSION >= 3
	if (awake_dw_interval) {
		int input_dw_interval_val = atoi(awake_dw_interval);
		int awake_dw_int = 0;

		if (input_dw_interval_val > NAN_MAX_ALLOWED_DW_AWAKE_INTERVAL) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"%s: input active dw interval = %d overwritting dw interval to Max allowed dw interval 16",
					__func__, input_dw_interval_val);
			input_dw_interval_val =
				NAN_MAX_ALLOWED_DW_AWAKE_INTERVAL;
		}
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: input active DW interval = %d",
				__func__, input_dw_interval_val);
		/*
		 * Indicates the interval for Sync beacons and SDF's in 2.4 GHz
		 * or 5 GHz band. Valid values of DW Interval are: 1, 2, 3, 4,
		 * and 5; 0 is reserved. The SDF includes in OTA when enabled.
		 * The publish/subscribe period. values don't override the
		 * device level configurations.
		 * input_dw_interval_val is provided by the user are in the
		 * format 2^n-1 = 1/2/4/8/16. Internal implementation expects n
		 * to be passed to indicate the awake_dw_interval.
		 */
		if (input_dw_interval_val == 1 ||
		    input_dw_interval_val % 2 == 0) {
			while (input_dw_interval_val > 0) {
				input_dw_interval_val >>= 1;
				awake_dw_int++;
			}
		}
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s:converted active DW interval = %d",
				__func__, awake_dw_int);
		config_req.config_dw.config_2dot4g_dw_band = 1;
		config_req.config_dw.dw_2dot4g_interval_val = awake_dw_int;
		config_req.config_dw.config_5g_dw_band = 1;
		config_req.config_dw.dw_5g_interval_val = awake_dw_int;
		ret = nan_config_request(0, dut->wifi_hal_iface_handle,
					 &config_req);
		if (ret != WIFI_SUCCESS) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s:NAN config request failed",
					__func__);
			return -2;
		}
	}

	if (qos_config)
		req.sdea_params.qos_cfg = (NanQosCfgStatus) atoi(qos_config);
#endif

	if (ndpe &&
	    strcasecmp(ndpe, "Enable") == 0)
		dut->ndpe = 1;

	if (trans_proto) {
		if (strcasecmp(trans_proto, "TCP") == 0) {
			dut->trans_proto = TRANSPORT_PROTO_TYPE_TCP;
		} else if (strcasecmp(trans_proto, "UDP") == 0) {
			dut->trans_proto = TRANSPORT_PROTO_TYPE_UDP;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s: Invalid protocol %s",
					__func__, trans_proto);
			return -1;
		}
	}

#ifdef NAN_NEW_CERT_VERSION
	if (dut->ndpe && ndp_attr) {
		NanDebugParams cfg_debug;
		int ndp_attr_val, size;

		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_ENABLE_NDP;
		if (strcasecmp(ndp_attr, "Absent") == 0)
			ndp_attr_val = NAN_NDP_ATTR_ABSENT;
		else
			ndp_attr_val = NAN_NDP_ATTR_PRESENT;
		memcpy(cfg_debug.debug_cmd_data, &ndp_attr_val, sizeof(int));
		size = sizeof(u32) + sizeof(int);
		ret = nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					       cfg_debug, size);
		if (ret != WIFI_SUCCESS) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "NAN config ndpAttr failed");
			return 0;
		}
	}

	if (dut->ndpe) {
		unsigned char nan_mac_addr[ETH_ALEN];
		size_t len = 0, tlv_len = 0;
		NanDebugParams cfg_debug;
		NdpIpTransParams ndp_ip_trans_param;
		u8 *p_buf;

		if (get_hwaddr("nan0", nan_mac_addr) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s:get_hwaddr nan0 failed", __func__);
			return -1;
		}
		len = convert_mac_addr_to_ipv6_linklocal(
			nan_mac_addr, ndp_ip_trans_param.ipv6_intf_addr);
		ndp_ip_trans_param.ipv6_addr_present = 1;

		ndp_ip_trans_param.trans_port_present = 1;
		ndp_ip_trans_param.transport_port = dut->trans_port;

		ndp_ip_trans_param.trans_proto_present = 1;
		ndp_ip_trans_param.transport_protocol = dut->trans_proto;

		/* build TLV blob for cfg_debug cmd */
		p_buf = cfg_debug.debug_cmd_data;

		/* put IPv6 address tlv into data buffer */
		len = nan_build_ipv6_link_local_tlv(
			p_buf, ndp_ip_trans_param.ipv6_intf_addr);
		tlv_len = len;
		p_buf += len;

		/* put port and protocol TLV into data buffer */
		len = nan_build_service_info_tlv(p_buf, &ndp_ip_trans_param);
		tlv_len += len;

		cfg_debug.cmd = NAN_TEST_MODE_CMD_TRANSPORT_IP_PARAM;
		nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					 cfg_debug, tlv_len + sizeof(u32));
	}
#endif

	ret = nan_publish_request(0, dut->wifi_hal_iface_handle, &req);
	if (ret != WIFI_SUCCESS)
		send_resp(dut, conn, SIGMA_ERROR, "Unable to publish");

	if (ndp_enable)
		dut->ndp_enable = 1;

	return 0;
}


static int nan_further_availability_rx(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd)
{
	const char *master_pref = get_param(cmd, "MasterPref");
	const char *rand_fac = get_param(cmd, "RandFactor");
	const char *hop_count = get_param(cmd, "HopCount");
	wifi_error ret;
	struct timespec abstime;

	NanEnableRequest req;

	memset(&req, 0, sizeof(NanEnableRequest));
	req.cluster_low = 0;
	req.cluster_high = 0xFFFF;
	req.master_pref = 30;

	if (master_pref)
		req.master_pref = strtoul(master_pref, NULL, 0);

	if (rand_fac) {
		int rand_fac_val = strtoul(rand_fac, NULL, 0);

		req.config_random_factor_force = 1;
		req.random_factor_force_val = rand_fac_val;
	}

	if (hop_count) {
		int hop_count_val = strtoul(hop_count, NULL, 0);

		req.config_hop_count_force = 1;
		req.hop_count_force_val = hop_count_val;
	}

	ret = nan_enable_request(0, dut->wifi_hal_iface_handle, &req);
	if (ret != WIFI_SUCCESS) {
		send_resp(dut, conn, SIGMA_ERROR, "Unable to enable nan");
		return 0;
	}

	abstime.tv_sec = 4;
	abstime.tv_nsec = 0;
	wait(abstime);

	return 0;
}


static int nan_further_availability_tx(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd)
{
	const char *master_pref = get_param(cmd, "MasterPref");
	const char *rand_fac = get_param(cmd, "RandFactor");
	const char *hop_count = get_param(cmd, "HopCount");
	wifi_error ret;

	NanEnableRequest req;
	NanConfigRequest configReq;

	memset(&req, 0, sizeof(NanEnableRequest));
	req.cluster_low = 0;
	req.cluster_high = 0xFFFF;
	req.master_pref = 30;

	if (master_pref)
		req.master_pref = strtoul(master_pref, NULL, 0);

	if (rand_fac) {
		int rand_fac_val = strtoul(rand_fac, NULL, 0);

		req.config_random_factor_force = 1;
		req.random_factor_force_val = rand_fac_val;
	}

	if (hop_count) {
		int hop_count_val = strtoul(hop_count, NULL, 0);

		req.config_hop_count_force = 1;
		req.hop_count_force_val = hop_count_val;
	}

	ret = nan_enable_request(0, dut->wifi_hal_iface_handle, &req);
	if (ret != WIFI_SUCCESS) {
		send_resp(dut, conn, SIGMA_ERROR, "Unable to enable nan");
		return 0;
	}

	/* Start the config of fam */

	memset(&configReq, 0, sizeof(NanConfigRequest));

	configReq.config_fam = 1;
	configReq.fam_val.numchans = 1;
	configReq.fam_val.famchan[0].entry_control = 0;
	configReq.fam_val.famchan[0].class_val = 81;
	configReq.fam_val.famchan[0].channel = 6;
	configReq.fam_val.famchan[0].mapid = 0;
	configReq.fam_val.famchan[0].avail_interval_bitmap = 0x7ffffffe;

	ret = nan_config_request(0, dut->wifi_hal_iface_handle, &configReq);
	if (ret != WIFI_SUCCESS)
		send_resp(dut, conn, SIGMA_ERROR, "Nan config request failed");

	return 0;
}


int sigma_nan_transmit_followup(struct sigma_dut *dut,
				struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *mac = get_param(cmd, "mac");
	const char *requestor_id = get_param(cmd, "RemoteInstanceId");
	const char *local_id = get_param(cmd, "LocalInstanceId");
	const char *service_name = get_param(cmd, "servicename");
	wifi_error ret;
	NanTransmitFollowupRequest req;

	memset(&req, 0, sizeof(NanTransmitFollowupRequest));
	req.requestor_instance_id = global_match_handle;
	req.addr[0] = 0xFF;
	req.addr[1] = 0xFF;
	req.addr[2] = 0xFF;
	req.addr[3] = 0xFF;
	req.addr[4] = 0xFF;
	req.addr[5] = 0xFF;
	req.priority = NAN_TX_PRIORITY_NORMAL;
	req.dw_or_faw = 0;

	if (service_name)
		req.service_specific_info_len = strlen(service_name);

	if (requestor_id) {
		/* int requestor_id_val = atoi(requestor_id); */
		if (global_match_handle != 0) {
			req.requestor_instance_id = global_match_handle;
		} else {
			u32 requestor_id_val = atoi(requestor_id);
			requestor_id_val =
					(requestor_id_val << 24) | 0x0000FFFF;
			req.requestor_instance_id = requestor_id_val;
		}
	}
	if (local_id) {
		/* int local_id_val = atoi(local_id); */
		if (global_header_handle != 0)
			req.publish_subscribe_id = global_header_handle;
		else
			req.publish_subscribe_id = atoi(local_id);
	}

	if (mac == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Invalid MAC Address");
		return -1;
	}
	nan_parse_mac_address(dut, mac, req.addr);

	ret = nan_transmit_followup_request(0, dut->wifi_hal_iface_handle,
					    &req);
	if (ret != WIFI_SUCCESS) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "Unable to complete nan transmit followup");
	}

	return 0;
}


/* NotifyResponse invoked to notify the status of the Request */
void nan_notify_response(transaction_id id, NanResponseMsg *rsp_data)
{
	sigma_dut_print(global_dut, DUT_MSG_INFO,
			"%s: status %d response_type %d",
			__func__, rsp_data->status, rsp_data->response_type);
	if (rsp_data->response_type == NAN_RESPONSE_STATS &&
	    rsp_data->body.stats_response.stats_type ==
	    NAN_STATS_ID_DE_TIMING_SYNC) {
		NanSyncStats *pSyncStats;

		sigma_dut_print(global_dut, DUT_MSG_INFO,
				"%s: stats_type %d", __func__,
				rsp_data->body.stats_response.stats_type);
		pSyncStats = &rsp_data->body.stats_response.data.sync_stats;
		memcpy(&global_nan_sync_stats, pSyncStats,
		       sizeof(NanSyncStats));
		pthread_cond_signal(&gCondition);
	} else if (rsp_data->response_type == NAN_RESPONSE_PUBLISH) {
		sigma_dut_print(global_dut, DUT_MSG_INFO,
				"%s: publish_id %d\n",
				__func__,
				rsp_data->body.publish_response.publish_id);
		global_publish_id = rsp_data->body.publish_response.publish_id;
	} else if (rsp_data->response_type == NAN_RESPONSE_SUBSCRIBE) {
		sigma_dut_print(global_dut, DUT_MSG_INFO,
				"%s: subscribe_id %d\n",
				__func__,
				rsp_data->body.subscribe_response.subscribe_id);
		global_subscribe_id =
			rsp_data->body.subscribe_response.subscribe_id;
	}
}


/* Events Callback */
void nan_event_publish_replied(NanPublishRepliedInd *event)
{
	sigma_dut_print(global_dut, DUT_MSG_INFO,
			"%s: handle %d " MAC_ADDR_STR " rssi:%d",
			__func__, event->requestor_instance_id,
			MAC_ADDR_ARRAY(event->addr), event->rssi_value);
	event_anyresponse = 1;
	snprintf(global_event_resp_buf, sizeof(global_event_resp_buf),
		 "EventName,Replied,RemoteInstanceID,%d,LocalInstanceID,%d,mac," MAC_ADDR_STR" ",
		 (event->requestor_instance_id >> 24),
		 (event->requestor_instance_id & 0xFFFF),
		 MAC_ADDR_ARRAY(event->addr));
}


/* Events Callback */
void nan_event_publish_terminated(NanPublishTerminatedInd *event)
{
	sigma_dut_print(global_dut, DUT_MSG_INFO, "%s: publish_id %d reason %d",
			__func__, event->publish_id, event->reason);
}


/* Events Callback */
void nan_event_match(NanMatchInd *event)
{
	sigma_dut_print(global_dut, DUT_MSG_INFO,
			"%s: Pub/Sub Id %d remote_requestor_id %08x "
			MAC_ADDR_STR
			" rssi:%d",
			__func__,
			event->publish_subscribe_id,
			event->requestor_instance_id,
			MAC_ADDR_ARRAY(event->addr),
			event->rssi_value);
	event_anyresponse = 1;
	global_header_handle = event->publish_subscribe_id;
	global_match_handle = event->requestor_instance_id;
	memcpy(global_peer_mac_addr, event->addr, sizeof(global_peer_mac_addr));

	/* memset(event_resp_buf, 0, sizeof(event_resp_buf)); */
	/* global_pub_sub_handle = event->header.handle; */
	/* Print the SSI */
	sigma_dut_print(global_dut, DUT_MSG_INFO, "Printing SSI:");
	nan_hex_dump(global_dut, event->service_specific_info,
		event->service_specific_info_len);
	snprintf(global_event_resp_buf, sizeof(global_event_resp_buf),
		 "EventName,DiscoveryResult,RemoteInstanceID,%d,LocalInstanceID,%d,mac,"
		 MAC_ADDR_STR " ", (event->requestor_instance_id >> 24),
		 event->publish_subscribe_id, MAC_ADDR_ARRAY(event->addr));

	/* Print the match filter */
	sigma_dut_print(global_dut, DUT_MSG_INFO, "Printing sdf match filter:");
	nan_hex_dump(global_dut, event->sdf_match_filter,
		     event->sdf_match_filter_len);

	/* Print the conn_capability */
	sigma_dut_print(global_dut, DUT_MSG_INFO,
			"Printing PostConnectivity Capability");
	if (event->is_conn_capability_valid) {
		sigma_dut_print(global_dut, DUT_MSG_INFO, "Wfd supported:%s",
				event->conn_capability.is_wfd_supported ?
				"yes" : "no");
		sigma_dut_print(global_dut, DUT_MSG_INFO, "Wfds supported:%s",
				(event->conn_capability.is_wfds_supported ?
				 "yes" : "no"));
		sigma_dut_print(global_dut, DUT_MSG_INFO, "TDLS supported:%s",
				(event->conn_capability.is_tdls_supported ?
				 "yes" : "no"));
		sigma_dut_print(global_dut, DUT_MSG_INFO, "IBSS supported:%s",
				(event->conn_capability.is_ibss_supported ?
				 "yes" : "no"));
		sigma_dut_print(global_dut, DUT_MSG_INFO, "Mesh supported:%s",
				(event->conn_capability.is_mesh_supported ?
				 "yes" : "no"));
		sigma_dut_print(global_dut, DUT_MSG_INFO, "Infra Field:%d",
				event->conn_capability.wlan_infra_field);
	} else {
		sigma_dut_print(global_dut, DUT_MSG_INFO,
				"PostConnectivity Capability not present");
	}

	/* Print the discovery_attr */
	sigma_dut_print(global_dut, DUT_MSG_INFO,
			"Printing PostDiscovery Attribute");
	if (event->num_rx_discovery_attr) {
		int idx;

		for (idx = 0; idx < event->num_rx_discovery_attr; idx++) {
			sigma_dut_print(global_dut, DUT_MSG_INFO,
					"PostDiscovery Attribute - %d", idx);
			sigma_dut_print(global_dut, DUT_MSG_INFO,
					"Conn Type:%d Device Role:%d"
					MAC_ADDR_STR,
					event->discovery_attr[idx].type,
					event->discovery_attr[idx].role,
					MAC_ADDR_ARRAY(event->discovery_attr[idx].addr));
			sigma_dut_print(global_dut, DUT_MSG_INFO,
					"Duration:%d MapId:%d "
					"avail_interval_bitmap:%04x",
					event->discovery_attr[idx].duration,
					event->discovery_attr[idx].mapid,
					event->discovery_attr[idx].avail_interval_bitmap);
			sigma_dut_print(global_dut, DUT_MSG_INFO,
					"Printing Mesh Id:");
			nan_hex_dump(global_dut,
				     event->discovery_attr[idx].mesh_id,
				     event->discovery_attr[idx].mesh_id_len);
			sigma_dut_print(global_dut, DUT_MSG_INFO,
					"Printing Infrastructure Ssid:");
			nan_hex_dump(global_dut,
				     event->discovery_attr[idx].infrastructure_ssid_val,
				     event->discovery_attr[idx].infrastructure_ssid_len);
		}
	} else {
		sigma_dut_print(global_dut, DUT_MSG_INFO,
				"PostDiscovery attribute not present");
	}

	/* Print the fam */
	if (event->num_chans) {
		nan_print_further_availability_chan(global_dut,
						    event->num_chans,
						    &event->famchan[0]);
	} else {
		sigma_dut_print(global_dut, DUT_MSG_INFO,
				"Further Availability Map not present");
	}
	if (event->cluster_attribute_len) {
		sigma_dut_print(global_dut, DUT_MSG_INFO,
				"Printing Cluster Attribute:");
		nan_hex_dump(global_dut, event->cluster_attribute,
			     event->cluster_attribute_len);
	} else {
		sigma_dut_print(global_dut, DUT_MSG_INFO,
				"Cluster Attribute not present");
	}
}


/* Events Callback */
void nan_event_match_expired(NanMatchExpiredInd *event)
{
	sigma_dut_print(global_dut, DUT_MSG_INFO,
			"%s: publish_subscribe_id %d match_handle %08x",
			__func__, event->publish_subscribe_id,
			event->requestor_instance_id);
}


/* Events Callback */
void nan_event_subscribe_terminated(NanSubscribeTerminatedInd *event)
{
	sigma_dut_print(global_dut, DUT_MSG_INFO,
			"%s: Subscribe Id %d reason %d",
			__func__, event->subscribe_id, event->reason);
}


/* Events Callback */
void nan_event_followup(NanFollowupInd *event)
{
	sigma_dut_print(global_dut, DUT_MSG_INFO,
			"%s: Publish/Subscribe Id %d match_handle 0x%08x dw_or_faw %d "
			MAC_ADDR_STR, __func__, event->publish_subscribe_id,
			event->requestor_instance_id, event->dw_or_faw,
			MAC_ADDR_ARRAY(event->addr));

	global_match_handle = event->requestor_instance_id;
	global_header_handle = event->publish_subscribe_id;
	sigma_dut_print(global_dut, DUT_MSG_INFO, "%s: Printing SSI", __func__);
	nan_hex_dump(global_dut, event->service_specific_info,
		     event->service_specific_info_len);
	event_anyresponse = 1;
	snprintf(global_event_resp_buf, sizeof(global_event_resp_buf),
		 "EventName,FollowUp,RemoteInstanceID,%d,LocalInstanceID,%d,mac,"
		 MAC_ADDR_STR " ", event->requestor_instance_id >> 24,
		 event->publish_subscribe_id, MAC_ADDR_ARRAY(event->addr));
}


/* Events Callback */
void nan_event_disceng_event(NanDiscEngEventInd *event)
{
	sigma_dut_print(global_dut, DUT_MSG_INFO, "%s: event_type %d",
			__func__, event->event_type);

	if (event->event_type == NAN_EVENT_ID_JOINED_CLUSTER) {
		sigma_dut_print(global_dut, DUT_MSG_INFO, "%s: Joined cluster "
				MAC_ADDR_STR,
				__func__,
				MAC_ADDR_ARRAY(event->data.cluster.addr));
		/* To ensure sta_get_events to get the events
		 * only after joining the NAN cluster. */
		pthread_cond_signal(&gCondition);
	}
	if (event->event_type == NAN_EVENT_ID_STARTED_CLUSTER) {
		sigma_dut_print(global_dut, DUT_MSG_INFO,
				"%s: Started cluster " MAC_ADDR_STR,
				__func__,
				MAC_ADDR_ARRAY(event->data.cluster.addr));
	}
	if (event->event_type == NAN_EVENT_ID_DISC_MAC_ADDR) {
		sigma_dut_print(global_dut, DUT_MSG_INFO,
				"%s: Discovery Mac Address "
				MAC_ADDR_STR,
				__func__,
				MAC_ADDR_ARRAY(event->data.mac_addr.addr));
		memcpy(global_nan_mac_addr, event->data.mac_addr.addr,
		       sizeof(global_nan_mac_addr));
	}
}


/* Events Callback */
void nan_event_disabled(NanDisabledInd *event)
{
	sigma_dut_print(global_dut, DUT_MSG_INFO, "%s: reason %d",
			__func__, event->reason);
	/* pthread_cond_signal(&gCondition); */
	if (if_nametoindex(NAN_AWARE_IFACE))
		run_system_wrapper(global_dut, "ifconfig %s down",
				   NAN_AWARE_IFACE);
}


/* Events callback */
static void ndp_event_data_indication(NanDataPathRequestInd *event)
{
	u8 *p_frame;
	u16 ipv6_addr_len = 0;
	static const u8 ipv6_intf_addr_msb[] = {
		0xFE, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	sigma_dut_print(global_dut, DUT_MSG_INFO,
			"%s: Service Instance Id: %d  Peer Discovery MAC ADDR "
			MAC_ADDR_STR
			" NDP Instance Id: %d App Info  len %d App Info %s",
			__func__,
			event->service_instance_id,
			MAC_ADDR_ARRAY(event->peer_disc_mac_addr),
			event->ndp_instance_id,
			event->app_info.ndp_app_info_len,
			event->app_info.ndp_app_info);

	global_ndp_instance_id = event->ndp_instance_id;
	memset(global_dut->nan_ipv6_addr, 0, sizeof(global_dut->nan_ipv6_addr));
	global_dut->nan_ipv6_len = 0;

	if (event->app_info.ndp_app_info_len > 0) {
		p_frame = event->app_info.ndp_app_info;
		if (*p_frame == NAN_TLV_TYPE_IPV6_LINK_LOCAL) {
			p_frame++;
			ipv6_addr_len = *p_frame++;
			ipv6_addr_len |= (*p_frame++) << 8;
			memcpy(global_dut->nan_ipv6_addr, ipv6_intf_addr_msb,
			       NAN_INTF_ID_LEN);
			global_dut->nan_ipv6_len = NAN_INTF_ID_LEN;
			if (ipv6_addr_len > 0 &&
			    ipv6_addr_len <= NAN_INTF_ID_LEN) {
				memcpy(global_dut->nan_ipv6_addr +
				       NAN_INTF_ID_LEN,
				       p_frame, ipv6_addr_len);
				global_dut->nan_ipv6_len += ipv6_addr_len;
			}
		}
	}
}


/* Events callback */
static void ndp_event_data_confirm(NanDataPathConfirmInd *event)
{
	char cmd[200];
	char ipv6_buf[100];

	sigma_dut_print(global_dut, DUT_MSG_INFO,
			"Received NDP Confirm Indication");

	memset(cmd, 0, sizeof(cmd));
	memset(ipv6_buf, 0, sizeof(ipv6_buf));

	global_ndp_instance_id = event->ndp_instance_id;

	if (event->rsp_code == NAN_DP_REQUEST_ACCEPT) {
		if (system("ifconfig nan0 up") != 0) {
			sigma_dut_print(global_dut, DUT_MSG_ERROR,
					"Failed to set nan interface up");
			return;
		}
		if (system("ip -6 route replace fe80::/64 dev nan0 table local") !=
		    0) {
			sigma_dut_print(global_dut, DUT_MSG_ERROR,
					"Failed to run:ip -6 route replace fe80::/64 dev nan0 table local");
		}

		if (global_dut->nan_ipv6_len > 0 &&
		    global_dut->nan_ipv6_len >= IPV6_ADDR_LEN)
			snprintf(ipv6_buf, sizeof(ipv6_buf),
				 "fe80::%02x%02x:%02xff:fe%02x:%02x%02x",
				 global_dut->nan_ipv6_addr[8],
				 global_dut->nan_ipv6_addr[9],
				 global_dut->nan_ipv6_addr[10],
				 global_dut->nan_ipv6_addr[13],
				 global_dut->nan_ipv6_addr[14],
				 global_dut->nan_ipv6_addr[15]);
		else
			convert_mac_addr_to_ipv6_lladdr(
				event->peer_ndi_mac_addr,
				ipv6_buf, sizeof(ipv6_buf));

		snprintf(cmd, sizeof(cmd),
			 "ip -6 neighbor replace %s lladdr %02x:%02x:%02x:%02x:%02x:%02x nud permanent dev nan0",
			 ipv6_buf, event->peer_ndi_mac_addr[0],
			 event->peer_ndi_mac_addr[1],
			 event->peer_ndi_mac_addr[2],
			 event->peer_ndi_mac_addr[3],
			 event->peer_ndi_mac_addr[4],
			 event->peer_ndi_mac_addr[5]);
		sigma_dut_print(global_dut, DUT_MSG_INFO,
				"neighbor replace cmd = %s", cmd);
		if (system(cmd) != 0) {
			sigma_dut_print(global_dut, DUT_MSG_ERROR,
					"Failed to run: ip -6 neighbor replace");
			return;
		}
	}
}


static NanCallbackHandler callbackHandler = {
	.NotifyResponse = nan_notify_response,
	.EventPublishReplied = nan_event_publish_replied,
	.EventPublishTerminated = nan_event_publish_terminated,
	.EventMatch = nan_event_match,
	.EventMatchExpired = nan_event_match_expired,
	.EventSubscribeTerminated = nan_event_subscribe_terminated,
	.EventFollowup = nan_event_followup,
	.EventDiscEngEvent = nan_event_disceng_event,
	.EventDisabled = nan_event_disabled,
	.EventDataRequest = ndp_event_data_indication,
	.EventDataConfirm = ndp_event_data_confirm,
};


void nan_init(struct sigma_dut *dut)
{
	if (wifi_hal_initialize(dut)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - wifi hal init failed for - NAN",
				__func__);
		exit(0);
	}
	pthread_mutex_init(&gMutex, NULL);
	pthread_cond_init(&gCondition, NULL);
	if (dut->wifi_hal_iface_handle)
		nan_register_handler(dut->wifi_hal_iface_handle,
				     callbackHandler);
}


void nan_cmd_sta_reset_default(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	sigma_dut_print(dut, DUT_MSG_INFO, "NAN sta_reset_default");

#ifdef ANDROID
	if (dut->nanservicediscoveryinprogress) {
		char *argv[5];
		pid_t pid;

		argv[0] = "am";
		argv[1] = "broadcast";
		argv[2] = "-a";
		argv[3] = "org.codeaurora.nanservicediscovery.close";
		argv[4] = NULL;

		pid = fork();
		if (pid == -1) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "fork: %s",
					strerror(errno));
		} else if (pid == 0) {
			execv("/system/bin/am", argv);
			sigma_dut_print(dut, DUT_MSG_ERROR, "execv: %s",
					strerror(errno));
			exit(0);
		}
		dut->nanservicediscoveryinprogress = 0;
	}
#endif /* ANDROID */

	if (nan_state == 0) {
		nan_init(dut);
		nan_state = 1;
	}
	is_fam = 0;
	event_anyresponse = 0;
	global_dut = dut;
	memset(&dut->nan_pmk[0], 0, NAN_PMK_INFO_LEN);
	dut->nan_pmk_len = 0;
	dut->sta_channel = 0;
	dut->ndpe = 0;
	dut->trans_proto = NAN_TRANSPORT_PROTOCOL_DEFAULT;
	dut->trans_port = NAN_TRANSPORT_PORT_DEFAULT;
	memset(global_event_resp_buf, 0, sizeof(global_event_resp_buf));
	memset(&global_nan_sync_stats, 0, sizeof(global_nan_sync_stats));
	memset(global_publish_service_name, 0,
	       sizeof(global_publish_service_name));
	global_publish_service_name_len = 0;
	global_publish_id = 0;
	global_subscribe_id = 0;

	sigma_nan_data_end(dut, cmd);
	nan_data_interface_delete(0, dut->wifi_hal_iface_handle,
				  (char *) "nan0");
	sigma_nan_disable(dut, conn, cmd);
	global_header_handle = 0;
	global_match_handle = 0;
}


int nan_cmd_sta_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	const char *program = get_param(cmd, "Prog");
	const char *nan_op = get_param(cmd, "NANOp");
	const char *method_type = get_param(cmd, "MethodType");
	const char *band = get_param(cmd, "band");
	const char *disc_mac_addr = get_param(cmd, "DiscoveryMacAddress");
	char resp_buf[100];
	wifi_error ret;

	if (program == NULL)
		return -1;

	if (strcasecmp(program, "NAN") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported program");
		return 0;
	}

	if (nan_op) {
#if NAN_CERT_VERSION >= 3
		int size = 0;
		u32 device_type_val = 0;
		NanDebugParams cfg_debug;

		memset(&cfg_debug, 0, sizeof(NanDebugParams));
		cfg_debug.cmd = NAN_TEST_MODE_CMD_DEVICE_TYPE;
		if (dut->device_type == STA_testbed)
			device_type_val = NAN_DEVICE_TYPE_TEST_BED;
		else if (dut->device_type == STA_dut)
			device_type_val = NAN_DEVICE_TYPE_DUT;

		memcpy(cfg_debug.debug_cmd_data, &device_type_val, sizeof(u32));
		size = sizeof(u32) + sizeof(u32);

		if (if_nametoindex(NAN_AWARE_IFACE))
		    run_system_wrapper(dut, "ifconfig %s up", NAN_AWARE_IFACE);

		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s: Device Type: cmd type = %d and command data = %u",
				__func__, cfg_debug.cmd, device_type_val);
		nan_debug_command_config(0, dut->wifi_hal_iface_handle,
					 cfg_debug, size);
#endif
		/*
		 * NANOp has been specified.
		 * We will build a nan_enable or nan_disable command.
		*/
		if (strcasecmp(nan_op, "On") == 0) {
			if (sigma_nan_enable(dut, conn, cmd) == 0) {
				ret = nan_data_interface_create(
					0, dut->wifi_hal_iface_handle,
					(char *) "nan0");
				if (ret != WIFI_SUCCESS) {
					sigma_dut_print(
						global_dut, DUT_MSG_ERROR,
						"Unable to create NAN data interface");
				}
				snprintf(resp_buf, sizeof(resp_buf), "mac,"
					 MAC_ADDR_STR,
					 MAC_ADDR_ARRAY(global_nan_mac_addr));
				send_resp(dut, conn, SIGMA_COMPLETE, resp_buf);
			} else {
				send_resp(dut, conn, SIGMA_ERROR,
					  "NAN_ENABLE_FAILED");
				return -1;
			}

			if (band && strcasecmp(band, "24g") == 0) {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"%s: Setting band to 2G Only",
						__func__);
				sigma_ndp_configure_band(
					dut, conn, cmd,
					NAN_DATA_PATH_SUPPORTED_BAND_2G);
			} else if (band && dut->sta_channel > 12) {
				sigma_ndp_configure_band(
					dut, conn, cmd,
					NAN_DATA_PATH_SUPPORT_DUAL_BAND);
			}
		} else if (strcasecmp(nan_op, "Off") == 0) {
			nan_data_interface_delete(0,
				dut->wifi_hal_iface_handle, (char *) "nan0");
			sigma_nan_disable(dut, conn, cmd);
			memset(global_publish_service_name, 0,
			       sizeof(global_publish_service_name));
			global_publish_service_name_len = 0;
			global_publish_id = 0;
			global_subscribe_id = 0;
			global_header_handle = 0;
			global_match_handle = 0;
			send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
		}
	}
	if (nan_state && nan_op == NULL) {
		if (method_type) {
			if (strcasecmp(method_type, "Publish") == 0) {
				sigma_nan_publish_request(dut, conn, cmd);
				send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
			}
			if (strcasecmp(method_type, "Subscribe") == 0) {
				sigma_nan_subscribe_request(dut, conn, cmd);
				send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
			}
			if (strcasecmp(method_type, "Followup") == 0) {
				sigma_nan_transmit_followup(dut, conn, cmd);
				send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
			}
			if (strcasecmp(method_type, "DataRequest") == 0) {
				sigma_nan_data_request(dut, conn, cmd);
				send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
			}
			if (strcasecmp(method_type, "DataResponse") == 0) {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"%s: method_type is DataResponse",
						__func__);
				sigma_nan_data_response(dut, conn, cmd);
				send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
			}
			if (strcasecmp(method_type, "DataEnd") == 0) {
				sigma_nan_data_end(dut, cmd);
				send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
			}
			if (strcasecmp(method_type, "rangerequest") == 0) {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"%s: method_type is rangerequest",
						__func__);
				sigma_nan_range_request(dut, cmd);
				send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
			}
			if (strcasecmp(method_type, "cancelrange") == 0) {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"%s: method_type is cancelrange",
						__func__);
				sigma_nan_cancel_range(dut, cmd);
				send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
			}
			if (strcasecmp(method_type, "SchedUpdate") == 0) {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"%s: method_type is SchedUpdate",
						__func__);
				sigma_nan_schedule_update(dut, cmd);
				send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
			}
		} else if (disc_mac_addr &&
			   strcasecmp(disc_mac_addr, "GET") == 0) {
			snprintf(resp_buf, sizeof(resp_buf), "mac,"
				 MAC_ADDR_STR,
				 MAC_ADDR_ARRAY(global_nan_mac_addr));
			send_resp(dut, conn, SIGMA_COMPLETE, resp_buf);
		} else {
			sigma_nan_config_enable(dut, conn, cmd);
			snprintf(resp_buf, sizeof(resp_buf), "mac,"
				 MAC_ADDR_STR,
				 MAC_ADDR_ARRAY(global_nan_mac_addr));
			send_resp(dut, conn, SIGMA_COMPLETE, resp_buf);
		}
	}

	return 0;
}


int nan_cmd_sta_get_parameter(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{

	const char *program = get_param(cmd, "Program");
	const char *parameter = get_param(cmd, "Parameter");
	char resp_buf[100];
	NanStatsRequest req;
	struct timespec abstime;
	u64 master_rank;
	u8 master_pref;
	u8 random_factor;
	u8 hop_count;
	u32 beacon_transmit_time;
	u32 ndp_channel_freq;
	u32 ndp_channel_freq2;
#if NAN_CERT_VERSION >= 3
	u32 sched_update_channel_freq;
#endif

	if (program == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Invalid Program Name");
		return -1;
	}
	if (strcasecmp(program, "NAN") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported program");
		return 0;
	}

	if (parameter == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Invalid Parameter");
		return -1;
	}

	memset(&req, 0, sizeof(NanStatsRequest));
	memset(resp_buf, 0, sizeof(resp_buf));
	req.stats_type = (NanStatsType) NAN_STATS_ID_DE_TIMING_SYNC;
	nan_stats_request(0, dut->wifi_hal_iface_handle, &req);
	/*
	 * To ensure sta_get_events to get the events
	 * only after joining the NAN cluster
	 */
	abstime.tv_sec = 4;
	abstime.tv_nsec = 0;
	wait(abstime);

	master_rank = global_nan_sync_stats.myRank;
	master_pref = (global_nan_sync_stats.myRank & 0xFF00000000000000) >> 56;
	random_factor = (global_nan_sync_stats.myRank & 0x00FF000000000000) >>
		48;
	hop_count = global_nan_sync_stats.currAmHopCount;
	beacon_transmit_time = global_nan_sync_stats.currAmBTT;
	ndp_channel_freq = global_nan_sync_stats.ndpChannelFreq;
	ndp_channel_freq2 = global_nan_sync_stats.ndpChannelFreq2;
#if NAN_CERT_VERSION >= 3
	sched_update_channel_freq =
		global_nan_sync_stats.schedUpdateChannelFreq;

	sigma_dut_print(dut, DUT_MSG_INFO,
			"%s: NanStatsRequest Master_pref:%02x, Random_factor:%02x, hop_count:%02x beacon_transmit_time:%d ndp_channel_freq:%d ndp_channel_freq2:%d sched_update_channel_freq:%d",
			__func__, master_pref, random_factor,
			hop_count, beacon_transmit_time,
			ndp_channel_freq, ndp_channel_freq2,
			sched_update_channel_freq);
#else /* #if NAN_CERT_VERSION >= 3 */
	sigma_dut_print(dut, DUT_MSG_INFO,
			"%s: NanStatsRequest Master_pref:%02x, Random_factor:%02x, hop_count:%02x beacon_transmit_time:%d ndp_channel_freq:%d ndp_channel_freq2:%d",
			__func__, master_pref, random_factor,
			hop_count, beacon_transmit_time,
			ndp_channel_freq, ndp_channel_freq2);
#endif /* #if NAN_CERT_VERSION >= 3 */

	if (strcasecmp(parameter, "MasterPref") == 0) {
		snprintf(resp_buf, sizeof(resp_buf), "MasterPref,0x%x",
			 master_pref);
	} else if (strcasecmp(parameter, "MasterRank") == 0) {
		snprintf(resp_buf, sizeof(resp_buf), "MasterRank,0x%lx",
			 master_rank);
	} else if (strcasecmp(parameter, "RandFactor") == 0) {
		snprintf(resp_buf, sizeof(resp_buf), "RandFactor,0x%x",
			 random_factor);
	} else if (strcasecmp(parameter, "HopCount") == 0) {
		snprintf(resp_buf, sizeof(resp_buf), "HopCount,0x%x",
			 hop_count);
	} else if (strcasecmp(parameter, "BeaconTransTime") == 0) {
		snprintf(resp_buf, sizeof(resp_buf), "BeaconTransTime 0x%x",
			 beacon_transmit_time);
	} else if (strcasecmp(parameter, "NANStatus") == 0) {
		if (nan_state == 1)
			snprintf(resp_buf, sizeof(resp_buf), "On");
		else
			snprintf(resp_buf, sizeof(resp_buf), "Off");
	} else if (strcasecmp(parameter, "NDPChannel") == 0) {
		if (ndp_channel_freq != 0 && ndp_channel_freq2 != 0) {
			snprintf(resp_buf, sizeof(resp_buf),
				 "ndpchannel,%d,ndpchannel,%d",
				 freq_to_channel(ndp_channel_freq),
				 freq_to_channel(ndp_channel_freq2));
		} else if (ndp_channel_freq != 0) {
			snprintf(resp_buf, sizeof(resp_buf), "ndpchannel,%d",
				 freq_to_channel(ndp_channel_freq));
		} else if (ndp_channel_freq2 != 0) {
			snprintf(resp_buf, sizeof(resp_buf), "ndpchannel,%d",
				 freq_to_channel(ndp_channel_freq2));
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: No Negotiated NDP Channels", __func__);
		}
#if NAN_CERT_VERSION >= 3
	} else if (strcasecmp(parameter, "SchedUpdateChannel") == 0) {
		snprintf(resp_buf, sizeof(resp_buf), "schedupdatechannel,%d",
			 freq_to_channel(sched_update_channel_freq));
#endif
	} else {
		send_resp(dut, conn, SIGMA_ERROR, "Invalid Parameter");
		return 0;
	}

	send_resp(dut, conn, SIGMA_COMPLETE, resp_buf);
	return 0;
}


int nan_cmd_sta_get_events(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	const char *action = get_param(cmd, "Action");

	if (!action)
		return 0;

	/* Check action for start, stop and get events. */
	if (strcasecmp(action, "Start") == 0) {
		memset(global_event_resp_buf, 0, sizeof(global_event_resp_buf));
		send_resp(dut, conn, SIGMA_COMPLETE, NULL);
	} else if (strcasecmp(action, "Stop") == 0) {
		event_anyresponse = 0;
		memset(global_event_resp_buf, 0, sizeof(global_event_resp_buf));
		send_resp(dut, conn, SIGMA_COMPLETE, NULL);
	} else if (strcasecmp(action, "Get") == 0) {
		if (event_anyresponse == 1) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  global_event_resp_buf);
		} else {
			send_resp(dut, conn, SIGMA_COMPLETE, "EventList,NONE");
		}
	}
	return 0;
}

#else /* #if NAN_CERT_VERSION */

int nan_cmd_sta_preset_testparameters(struct sigma_dut *dut,
				      struct sigma_conn *conn,
				      struct sigma_cmd *cmd)
{
	return 1;
}


int nan_cmd_sta_get_parameter(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	return 0;

}


void nan_cmd_sta_reset_default(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	return;
}


int nan_cmd_sta_get_events(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	return 0;
}


int nan_cmd_sta_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	return 0;
}

#endif /* #if NAN_CERT_VERSION */
