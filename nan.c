/*
 * Sigma Control API DUT (NAN functionality)
 * Copyright (c) 2014-2015, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/stat.h>
#include "wpa_ctrl.h"
#include "wpa_helpers.h"
#include "wifi_hal.h"
#include "nan_cert.h"

pthread_cond_t gCondition;
pthread_mutex_t gMutex;
wifi_handle global_wifi_handle;
wifi_interface_handle global_interface_handle;
static int nan_state = 0;
static int event_anyresponse = 0;
static int is_fam = 0;

uint16_t global_header_handle = 0;
uint32_t global_match_handle = 0;

#define MAC_ADDR_ARRAY(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MAC_ADDR_STR "%02x:%02x:%02x:%02x:%02x:%02x"
#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif

struct sigma_dut *global_dut = NULL;
static char global_nan_mac_addr[ETH_ALEN];
static char global_event_resp_buf[1024];

static int nan_further_availability_tx(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd);
static int nan_further_availability_rx(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd);


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
		pos += sprintf(&(buf[pos]), "%02x ", *ptr++);
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
	const char *oper_chan = get_param(cmd, "oper_chan");
	int channel = 0;

	channel = atoi(oper_chan);
	dut->sta_channel = channel;

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
	const char *high_tsf = get_param(cmd, "HighTSF");
	const char *sdftx_band = get_param(cmd, "SDFTxBand");
	const char *oper_chan = get_param(cmd, "oper_chn");
	const char *further_avail_ind = get_param(cmd, "FurtherAvailInd");
	const char *band = get_param(cmd, "Band");
	const char *only_5g = get_param(cmd, "5GOnly");
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

	if (band) {
		if (strcasecmp(band, "24G") == 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Band 2.4GHz selected");
			/* Enable 2.4G support */
			req.config_2dot4g_support = 1;
			req.support_2dot4g_val = 1;
			req.config_2dot4g_beacons = 1;
			req.beacon_2dot4g_val = 1;
			req.config_2dot4g_sdf = 1;
			req.sdf_2dot4g_val = 1;

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

	nan_enable_request(0, global_interface_handle, &req);

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

	nan_disable_request(0, global_interface_handle);

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

	nan_config_request(0, global_interface_handle, &req);

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
	NanSubscribeRequest req;
	int filter_len_rx = 0, filter_len_tx = 0;
	u8 input_rx[NAN_MAX_MATCH_FILTER_LEN];
	u8 input_tx[NAN_MAX_MATCH_FILTER_LEN];

	memset(&req, 0, sizeof(NanSubscribeRequest));
	req.ttl = 0;
	req.period =  1000;
	req.subscribe_type = 1;
	req.serviceResponseFilter = 1; /* MAC */
	req.serviceResponseInclude = 0;
	req.ssiRequiredForMatchIndication = 0;
	req.subscribe_match_indicator = NAN_MATCH_ALG_MATCH_CONTINUOUS;
	req.subscribe_count = 0;

	if (subscribe_type) {
		if (strcasecmp(subscribe_type, "Active") == 0) {
			req.subscribe_type = 1;
		} else if (strcasecmp(subscribe_type, "Passive") == 0) {
			req.subscribe_type = 0;
		} else if (strcasecmp(subscribe_type, "Cancel") == 0) {
			NanSubscribeCancelRequest req;

			memset(&req, 0, sizeof(NanSubscribeCancelRequest));
			nan_subscribe_cancel_request(0, global_interface_handle,
						     &req);
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

	strlcpy((char *) req.service_name, service_name,
		strlen(service_name) + 1);
	req.service_name_len = strlen(service_name);

	nan_subscribe_request(0, global_interface_handle, &req);
	return 0;
}


int config_post_disc_attr(void)
{
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

	nan_config_request(0, global_interface_handle, &configReq);
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
	NanPublishRequest req;
	int filter_len_rx = 0, filter_len_tx = 0;
	u8 input_rx[NAN_MAX_MATCH_FILTER_LEN];
	u8 input_tx[NAN_MAX_MATCH_FILTER_LEN];

	memset(&req, 0, sizeof(NanPublishRequest));
	req.ttl = 0;
	req.period = 500;
	req.publish_match_indicator = 1;
	req.publish_type = NAN_PUBLISH_TYPE_UNSOLICITED;
	req.tx_type = NAN_TX_TYPE_BROADCAST;
	req.publish_count = 0;
	strlcpy((char *) req.service_name, service_name,
		strlen(service_name) + 1);
	req.service_name_len = strlen(service_name);

	if (publish_type) {
		if (strcasecmp(publish_type, "Solicited") == 0) {
			req.publish_type = NAN_PUBLISH_TYPE_SOLICITED;
		} else if (strcasecmp(publish_type, "Cancel") == 0) {
			NanPublishCancelRequest req;

			memset(&req, 0, sizeof(NanPublishCancelRequest));
			nan_publish_cancel_request(0, global_interface_handle,
						   &req);
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
		config_post_disc_attr();
		/* TODO: Add comments regarding this step */
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
	strlcpy((char *) req.service_name, service_name,
		strlen(service_name) + 1);
	req.service_name_len = strlen(service_name);

	nan_publish_request(0, global_interface_handle, &req);

	return 0;
}


static int nan_further_availability_rx(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd)
{
	const char *master_pref = get_param(cmd, "MasterPref");
	const char *rand_fac = get_param(cmd, "RandFactor");
	const char *hop_count = get_param(cmd, "HopCount");
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

	nan_enable_request(0, global_interface_handle, &req);

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

	nan_enable_request(0, global_interface_handle, &req);

	/* Start the config of fam */

	memset(&configReq, 0, sizeof(NanConfigRequest));

	configReq.config_fam = 1;
	configReq.fam_val.numchans = 1;
	configReq.fam_val.famchan[0].entry_control = 0;
	configReq.fam_val.famchan[0].class_val = 81;
	configReq.fam_val.famchan[0].channel = 6;
	configReq.fam_val.famchan[0].mapid = 0;
	configReq.fam_val.famchan[0].avail_interval_bitmap = 0x7ffffffe;

	nan_config_request(0, global_interface_handle, &configReq);

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
	req.service_specific_info_len = strlen(service_name);

	if (requestor_id) {
		/* int requestor_id_val = atoi(requestor_id); */
		req.requestor_instance_id = global_match_handle;
	}
	if (local_id) {
		/* int local_id_val = atoi(local_id); */
		req.publish_subscribe_id = global_header_handle;
	}

	if (mac == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Invalid MAC Address");
		return -1;
	}
	nan_parse_mac_address(dut, mac, req.addr);

	if (requestor_id)
		req.requestor_instance_id = strtoul(requestor_id, NULL, 0);


	nan_transmit_followup_request(0, global_interface_handle, &req);
	return 0;
}

/* NotifyResponse invoked to notify the status of the Request */
void nan_notify_response(transaction_id id, NanResponseMsg *rsp_data)
{
	sigma_dut_print(global_dut, DUT_MSG_INFO,
			"%s: status %d value %d response_type %d",
			__func__,
			rsp_data->status, rsp_data->value,
			rsp_data->response_type);
	if (rsp_data->response_type == NAN_RESPONSE_STATS) {
		sigma_dut_print(global_dut, DUT_MSG_INFO, "%s: stats_type %d",
				__func__,
				rsp_data->body.stats_response.stats_type);
	}
#if 0
	if (rsp_data->response_type == NAN_RESPONSE_CONFIG &&
	    rsp_data->status == 0)
		pthread_cond_signal(&gCondition);
#endif
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
		 "EventName,Replied,RemoteInstanceID %d,mac," MAC_ADDR_STR,
		 (event->requestor_instance_id >> 24),
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

	global_match_handle = event->publish_subscribe_id;
	global_header_handle = event->requestor_instance_id;
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
}


void * my_thread_function(void *ptr)
{
	wifi_event_loop(global_wifi_handle);
	pthread_exit(0);
	return (void *) NULL;
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
};

void nan_init(struct sigma_dut *dut)
{
	pthread_t thread1;	/* thread variables */
	wifi_error err = wifi_initialize(&global_wifi_handle);

	if (err) {
		printf("wifi hal initialize failed\n");
		return;
	}

	global_interface_handle = wifi_get_iface_handle(global_wifi_handle,
							(char *) "wlan0");
	/* create threads 1 */
	pthread_create(&thread1, NULL, &my_thread_function, NULL);

	pthread_mutex_init(&gMutex, NULL);
	pthread_cond_init(&gCondition, NULL);
	nan_register_handler(global_interface_handle, callbackHandler);
}


void nan_cmd_sta_reset_default(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	sigma_dut_print(dut, DUT_MSG_INFO, "NAN sta_reset_default");

	if (nan_state == 0) {
		nan_init(dut);
		nan_state = 1;
	}
	is_fam = 0;
	event_anyresponse = 0;
	global_dut = dut;
	memset(global_event_resp_buf, 0, sizeof(global_event_resp_buf));
	sigma_nan_disable(dut, conn, cmd);
}


int nan_cmd_sta_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	const char *program = get_param(cmd, "Prog");
	const char *nan_op = get_param(cmd, "NANOp");
	const char *method_type = get_param(cmd, "MethodType");
	char resp_buf[100];

	if (program == NULL)
		return -1;

	if (strcasecmp(program, "NAN") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported program");
		return 0;
	}

	if (nan_op) {
		/*
		 * NANOp has been specified.
		 * We will build a nan_enable or nan_disable command.
		*/
		if (strcasecmp(nan_op, "On") == 0) {
			if (sigma_nan_enable(dut, conn, cmd) == 0) {
				snprintf(resp_buf, sizeof(resp_buf), "mac,"
					 MAC_ADDR_STR,
					 MAC_ADDR_ARRAY(global_nan_mac_addr));
				send_resp(dut, conn, SIGMA_COMPLETE, resp_buf);
			} else {
				send_resp(dut, conn, SIGMA_ERROR,
					  "NAN_ENABLE_FAILED");
				return -1;
			}
		} else if (strcasecmp(nan_op, "Off") == 0) {
			sigma_nan_disable(dut, conn, cmd);
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
	NanStaParameter rsp;

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
	memset(&rsp, 0, sizeof(NanStaParameter));

	nan_get_sta_parameter(0, global_interface_handle, &rsp);
	sigma_dut_print(dut, DUT_MSG_INFO,
			"%s: NanStaparameter Master_pref:%02x, Random_factor:%02x, hop_count:%02x beacon_transmit_time:%d",
			__func__, rsp.master_pref, rsp.random_factor,
			rsp.hop_count, rsp.beacon_transmit_time);

	if (strcasecmp(parameter, "MasterPref") == 0) {
		snprintf(resp_buf, sizeof(resp_buf), "MasterPref,0x%x",
			 rsp.master_pref);
	} else if (strcasecmp(parameter, "MasterRank") == 0) {
		snprintf(resp_buf, sizeof(resp_buf), "MasterRank,0x%lx",
			 rsp.master_rank);
	} else if (strcasecmp(parameter, "RandFactor") == 0) {
		snprintf(resp_buf, sizeof(resp_buf), "RandFactor,0x%x",
			 rsp.random_factor);
	} else if (strcasecmp(parameter, "HopCount") == 0) {
		snprintf(resp_buf, sizeof(resp_buf), "HopCount,0x%x",
			 rsp.hop_count);
	} else if (strcasecmp(parameter, "BeaconTransTime") == 0) {
		snprintf(resp_buf, sizeof(resp_buf), "BeaconTransTime 0x%x",
			 rsp.beacon_transmit_time);
	} else if (strcasecmp(parameter, "NANStatus") == 0) {
		if (nan_state == 1)
			snprintf(resp_buf, sizeof(resp_buf), "On");
		else
			snprintf(resp_buf, sizeof(resp_buf), "Off");
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
