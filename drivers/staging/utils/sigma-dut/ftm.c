/*
 * Sigma Control API DUT (FTM LOC functionality)
 * Copyright (c) 2016, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/stat.h>
#include <regex.h>
#include "wpa_helpers.h"

static const char LOC_XML_FILE_PATH[] = "./data/sigma-dut-target.xml";

static const char LOC_LOWI_TEST_DISCOVERY[] = "lowi_test -a -b 2 -n 1";
static const char LOC_LOWI_TEST_RANGING[] =
"lowi_test -r ./data/sigma-dut-target.xml -n 1";
static const char LOC_LOWI_TEST_NEIGHBOR_RPT_REQ[] = "lowi_test -nrr";
static const char LOC_LOWI_TEST_ANQP_REQ[] = "lowi_test -anqp -mac ";
static const char WPA_INTERWORKING_ENABLE[] =
"SET interworking 1";
static const char WPA_INTERWORKING_DISABLE[] =
"SET interworking 0";
static const char WPA_RM_ENABLE[] =
"VENDOR 1374 74 08000400BD000000";
static const char WPA_RM_DISABLE[] =
"VENDOR 1374 74 0800040000000000";
static const char WPA_ADDRESS_3_ENABLE[] =
"SET gas_address3 1";
static const char WPA_ADDRESS_3_DISABLE[] =
"SET gas_address3 0";

#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif

#define LOC_MAX_RM_FLAGS 10
#define LOC_RM_FLAG_VAL_ARRAY 2

enum lowi_tst_cmd {
	LOWI_TST_RANGING = 0,
	LOWI_TST_NEIGHBOR_REPORT_REQ = 1,
	LOWI_TST_ANQP_REQ = 2,
};

struct capi_loc_cmd {
	unsigned int chan;
	unsigned int burstExp;
	unsigned int burstDur;
	unsigned int minDeltaFtm;
	unsigned int ptsf;
	unsigned int asap;
	unsigned int ftmsPerBurst;
	unsigned int fmtbw;
	unsigned int burstPeriod;
	unsigned int locCivic;
	unsigned int lci;
};


static int loc_write_xml_file(struct sigma_dut *dut, const char *dst_mac_str,
			      struct capi_loc_cmd *loc_cmd)
{
	FILE *xml;
	unsigned int band, bw, preamble, primary_ch, center_freq;

	xml = fopen(LOC_XML_FILE_PATH, "w");
	if (!xml) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Unable to create the XML file", __func__);
		return -1;
	}

	/* Using this following defaults:
	 * default value of band 1
	 */
	band = 1;

#define FMT_BW_NO_PREF 0
#define FMT_BW_HT_20 9
#define FMT_BW_VHT_20 10
#define FMT_BW_HT_40 11
#define FMT_BW_VHT_40 12
#define FMT_BW_VHT_80 13

#define LOC_BW_20 0
#define LOC_BW_40 1
#define LOC_BW_80 2
#define LOC_PREAMBLE_HT 1
#define LOC_PREAMBLE_VHT 2
	switch (loc_cmd->fmtbw) {
	case FMT_BW_NO_PREF:
	case FMT_BW_HT_20:
		bw = LOC_BW_20;
		preamble = LOC_PREAMBLE_HT;
		primary_ch = 36;
		center_freq = 5180;
		break;
	case FMT_BW_VHT_20:
		bw = LOC_BW_20;
		preamble = LOC_PREAMBLE_VHT;
		primary_ch = 36;
		center_freq = 5180;
		break;
	case FMT_BW_HT_40:
		bw = LOC_BW_40;
		preamble = LOC_PREAMBLE_HT;
		primary_ch = 36;
		center_freq = 5190;
		break;
	case FMT_BW_VHT_40:
		bw = LOC_BW_40;
		preamble = LOC_PREAMBLE_VHT;
		primary_ch = 36;
		center_freq = 5190;
		break;
	case FMT_BW_VHT_80:
		bw = LOC_BW_80;
		preamble = LOC_PREAMBLE_VHT;
		primary_ch = 36;
		center_freq = 5210;
		break;
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Bad Format/BW received", __func__);
		fclose(xml);
		return -1;
	}

#define LOC_CAPI_DEFAULT_FTMS_PER_BURST 5
#define LOC_CAPI_DEFAULT_BURST_DUR 10
	fprintf(xml, "<body>\n");
	fprintf(xml, "  <ranging>\n");
	fprintf(xml, "    <ap>\n");
	fprintf(xml, "    <band>%u</band>\n", band);
	fprintf(xml, "    <rttType>3</rttType>\n");
	fprintf(xml, "    <numFrames>%u</numFrames>\n",
		LOC_CAPI_DEFAULT_FTMS_PER_BURST);
	fprintf(xml, "    <bw>%u</bw>\n", bw);
	fprintf(xml, "    <preamble>%u</preamble>\n", preamble);
	fprintf(xml, "    <asap>%u</asap>\n", loc_cmd->asap);
	fprintf(xml, "    <lci>%u</lci>\n", loc_cmd->lci);
	fprintf(xml, "    <civic>%u</civic>\n", loc_cmd->locCivic);
	fprintf(xml, "    <burstsexp>%u</burstsexp>\n", loc_cmd->burstExp);
	fprintf(xml, "    <burstduration>%u</burstduration>\n",
		LOC_CAPI_DEFAULT_BURST_DUR);
	fprintf(xml, "    <burstperiod>%u</burstperiod>\n", 0);
	/* Use parameters from LOWI cache */
	fprintf(xml, "    <paramControl>%u</paramControl>\n", 0);
	fprintf(xml, "    <ptsftimer>%u</ptsftimer>\n", 0);
	fprintf(xml, "    <center_freq1>%u</center_freq1>\n", center_freq);
	fprintf(xml, "    <center_freq2>0</center_freq2>\n");
	fprintf(xml, "    <ch>%u</ch>\n", primary_ch);
	fprintf(xml, "    <mac>%s</mac>\n", dst_mac_str);
	fprintf(xml, "    </ap>\n");
	fprintf(xml, "  </ranging>\n");
	fprintf(xml, "  <summary>\n");
	fprintf(xml, "    <mac>%s</mac>\n", dst_mac_str);
	fprintf(xml, "  </summary>\n");
	fprintf(xml, "</body>\n");

	fclose(xml);
	sigma_dut_print(dut, DUT_MSG_INFO,
			"%s - Successfully created XML file", __func__);
	return 0;
}


static int pass_request_to_ltest(struct sigma_dut *dut, enum lowi_tst_cmd cmd,
				 const char *params)
{
#define MAX_ANQP_CMND_SIZE 256
	int ret;
	const char *cmd_str;
	char lowi_anqp_query[MAX_ANQP_CMND_SIZE];

	switch (cmd) {
	case LOWI_TST_RANGING:
		cmd_str = LOC_LOWI_TEST_RANGING;
		break;
	case LOWI_TST_NEIGHBOR_REPORT_REQ:
		cmd_str = LOC_LOWI_TEST_NEIGHBOR_RPT_REQ;
		break;
	case LOWI_TST_ANQP_REQ:
		if (!params) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s - No Destination Mac provided for ANQP Query",
					__func__);
			return -1;
		}

		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s - Destination Mac provided for ANQP Query: %s",
				__func__, params);

		snprintf(lowi_anqp_query, MAX_ANQP_CMND_SIZE, "%s%s",
			 LOC_LOWI_TEST_ANQP_REQ, params);
		cmd_str = lowi_anqp_query;
		break;
	default:
		cmd_str = LOC_LOWI_TEST_DISCOVERY;
		break;
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "%s - 1 - Running command: %s",
			__func__, LOC_LOWI_TEST_DISCOVERY);
	ret = system(LOC_LOWI_TEST_DISCOVERY);
	sigma_dut_print(dut, DUT_MSG_INFO,
			"%s - Finished Performing Discovery Scan through LOWI_test: ret: %d",
			__func__, ret);
	sleep(1);
	sigma_dut_print(dut, DUT_MSG_INFO, "%s - 2 - Running command: %s",
			__func__, cmd_str);
	ret = system(cmd_str);
	sigma_dut_print(dut, DUT_MSG_INFO,
			"%s - Finished Performing command: %s, got ret: %d",
			__func__, cmd_str, ret);

	return ret;
}


int loc_cmd_sta_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	const char *params = NULL;
	enum lowi_tst_cmd cmnd = LOWI_TST_RANGING;
	const char *program = get_param(cmd, "prog");
	const char *loc_op = get_param(cmd, "Trigger");
	const char *interface = get_param(cmd, "interface");

	const char *destMacStr = get_param(cmd, "destmac");
	const char *burstExp = get_param(cmd, "burstsexponent");
	const char *asap = get_param(cmd, "ASAP");
	const char *fmtbw = get_param(cmd, "formatbwftm");
	const char *locCivic = get_param(cmd, "askforloccivic");
	const char *lci = get_param(cmd, "askforlci");
	struct capi_loc_cmd loc_cmd;

	memset(&loc_cmd, 0, sizeof(loc_cmd));

	if (!loc_op) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - No Operation! - Aborting", __func__);
		return -1;
	}

	cmnd = strcasecmp(loc_op, "ANQPQuery") == 0 ?
		LOWI_TST_ANQP_REQ : LOWI_TST_RANGING;
	sigma_dut_print(dut, DUT_MSG_INFO, "%s - Going to perform: %s",
			__func__, loc_op);

	if (!program) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - No Program in Command! - Aborting",
				__func__);
		return -1;
	}

	if (!interface) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Incomplete command in LOC CAPI request",
				__func__);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrMsg,Incomplete Loc CAPI command - missing interface");
		return 0;
	}

	if (!destMacStr) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Incomplete command in LOC CAPI request",
				__func__);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrMsg,Incomplete Loc CAPI command - missing MAC");
		return 0;
	}

	if (cmnd == LOWI_TST_RANGING) {
		sigma_dut_print(dut, DUT_MSG_INFO, "%s - LOWI_TST_RANGING",
				__func__);
		if (!burstExp) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s - Incomplete command in LOC CAPI request",
					__func__);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrMsg,Incomplete Loc CAPI command - missing Burst Exp");
			return 0;
		}

		if (!asap) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"%s - Incomplete command in LOC CAPI request",
					__func__);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrMsg,Incomplete Loc CAPI command - missing ASAP");
			return 0;
		}

		if (!fmtbw) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s - Incomplete command in LOC CAPI request",
					__func__);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrMsg,Incomplete Loc CAPI command - missing Format & BW");
			return 0;
		}

		if (!locCivic) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s - Incomplete command in LOC CAPI request",
					__func__);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrMsg,Incomplete Loc CAPI command - missing Location Civic");
			return 0;
		}

		if (!lci) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s - Incomplete command in LOC CAPI request",
					__func__);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrMsg,Incomplete Loc CAPI command - missing LCI");
			return 0;
		}

		if (strcasecmp(program, "loc") != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s - Unsupported Program: %s",
					__func__, program);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrMsg,Unsupported program");
			return 0;
		}

		if (strcasecmp(interface, "wlan0") != 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"%s - Unsupported Interface Type: %s",
					__func__, interface);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrMsg,Unsupported Interface Type");
			return 0;
		}

		sscanf(burstExp, "%u", &loc_cmd.burstExp);
		sigma_dut_print(dut, DUT_MSG_INFO, "%s - burstExp: %u",
				__func__, loc_cmd.burstExp);
		sscanf(asap, "%u", &loc_cmd.asap);
		sigma_dut_print(dut, DUT_MSG_INFO, "%s - asap: %u",
				__func__, loc_cmd.asap);
		sscanf(fmtbw, "%u", &loc_cmd.fmtbw);
		sigma_dut_print(dut, DUT_MSG_INFO, "%s - fmtbw: %u",
				__func__, loc_cmd.fmtbw);
		sscanf(locCivic, "%u", &loc_cmd.locCivic);
		sigma_dut_print(dut, DUT_MSG_INFO, "%s - locCivic: %u",
				__func__, loc_cmd.locCivic);
		sscanf(lci, "%u", &loc_cmd.lci);
		sigma_dut_print(dut, DUT_MSG_INFO, "%s - lci: %u",
				__func__, loc_cmd.lci);

		if (loc_write_xml_file(dut, destMacStr, &loc_cmd) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s - Failed to write to XML file because of bad command",
					__func__);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrMsg,Bad CAPI command");
			return 0;
		}
	} else {
		/* ANQP Query */
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s - LOWI_TST_ANQP_REQ", __func__);
		params = destMacStr;
	}

	if (pass_request_to_ltest(dut, cmnd, params) < 0) {
		/* Loc operation been failed. */
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Failed to initiate Loc command",
				__func__);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrMsg,Failed to initiate Loc command");
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_INFO,
			"%s - Succeeded to initiate Loc command", __func__);
	send_resp(dut, conn, SIGMA_COMPLETE, NULL);
	return 0;
}


int loc_cmd_sta_send_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	const char *address3Cmnd = WPA_ADDRESS_3_DISABLE;
	enum lowi_tst_cmd cmnd = LOWI_TST_NEIGHBOR_REPORT_REQ; /* Default */
	/* Mandatory arguments */
	const char *interface = get_param(cmd, "interface");
	const char *program = get_param(cmd, "program");
	const char *destMacStr = get_param(cmd, "destmac");
	const char *frameName = get_param(cmd, "FrameName");

	/* Optional Arguments */
	const char *locCivic = get_param(cmd, "askforloccivic");
	const char *lci = get_param(cmd, "askforlci");
	const char *fqdn = get_param(cmd, "AskForPublicIdentifierURI-FQDN");
	const char *address3 = get_param(cmd, "address3");

	const char *params = NULL;

	if (!program) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - No Program in Command! - Aborting",
				__func__);
		return -1;
	}

	if (!interface) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Incomplete command in LOC CAPI request",
				__func__);
		send_resp(dut, conn, SIGMA_ERROR, NULL);
		return 0;
	}

	if (!frameName) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Incomplete command in LOC CAPI request",
				__func__);
		send_resp(dut, conn, SIGMA_ERROR, NULL);
		return 0;
	}

	if (strcasecmp(frameName, "AnqpQuery") == 0 && !destMacStr) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Incomplete command in LOC CAPI request",
				__func__);
		send_resp(dut, conn, SIGMA_ERROR, NULL);
		return 0;
	}

	if (!locCivic)
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Command missing LocCivic", __func__);
	if (!lci)
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Command missing LCI", __func__);
	if (!fqdn)
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Command missing FQDN", __func__);
	if (!address3) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Command missing address3", __func__);
	} else {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "%s - address3: %s",
				__func__, address3);
		if (strcasecmp(address3, "FF:FF:FF:FF:FF:FF") == 0)
			address3Cmnd = WPA_ADDRESS_3_ENABLE;
		else
			address3Cmnd = WPA_ADDRESS_3_DISABLE;
	}

	if (strcasecmp(program, "loc") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Unsupported Program: %s", __func__,
				program);
		send_resp(dut, conn, SIGMA_ERROR, NULL);
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "%s - Triggering Frame: %s",
			__func__, frameName);
	if (strcasecmp(frameName, "AnqpQuery") == 0) {
		cmnd = LOWI_TST_ANQP_REQ;
		params = destMacStr;
	} else {
		cmnd = LOWI_TST_NEIGHBOR_REPORT_REQ;
	}

	if (cmnd == LOWI_TST_ANQP_REQ) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "%s - Executing command %s",
				__func__, address3Cmnd);
		if (wpa_command(get_station_ifname(dut), address3Cmnd) < 0) {
			send_resp(dut, conn, SIGMA_ERROR, NULL);
			return -1;
		}
	}
	if (pass_request_to_ltest(dut, cmnd, params) < 0) {
		/* Loc operation has failed. */
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - Failed to initiate Loc command",
				__func__);
		send_resp(dut, conn, SIGMA_ERROR, NULL);
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_INFO,
			"%s - Succeeded to initiate Loc command", __func__);
	send_resp(dut, conn, SIGMA_COMPLETE, NULL);
	return 0;
}


enum e_rm_parse_states {
	LOC_LOOKING_FOR_BIT = 0,
	LOC_LOOKING_FOR_VAL,
	LOC_MAX
};


void parse_rm_bits(struct sigma_dut *dut, const char *rmFlags,
		   char rmBitFlags[LOC_MAX_RM_FLAGS][LOC_RM_FLAG_VAL_ARRAY])
{

	unsigned int bitPos = 0;
	unsigned int bitVal = 0;
	unsigned int idx = 0;
	unsigned int i = 0;
	enum e_rm_parse_states rmParseStates = LOC_LOOKING_FOR_BIT;
	char temp = '\0';

	if (!rmFlags) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - NULL pointer for rmFlags - Aborting", __func__);
		return;
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "%s - rmFlags: %s",
			__func__, rmFlags);
	while (*rmFlags != '\0' && idx < LOC_MAX_RM_FLAGS) {
		temp = *rmFlags;
		rmFlags++;
		switch (rmParseStates) {
		case LOC_LOOKING_FOR_BIT:
			if (temp >= '0' && temp <= '9') {
				/* Parse Digit for bit Position */
				bitPos = (bitPos * 10) + (temp - '0');
				sigma_dut_print(dut, DUT_MSG_INFO,
						"%s - LOC_LOOKING_FOR_BIT - parsing: %c, bitPos: %u",
						__func__, temp, bitPos);
			} else if (temp == ':') {
				/* move to Parsing Bit Value */
				sigma_dut_print(dut, DUT_MSG_INFO,
						"%s - LOC_LOOKING_FOR_BIT - processing: %c, bitPos: %u",
						__func__, temp, bitPos);
				rmBitFlags[idx][0] = bitPos;
				rmParseStates = LOC_LOOKING_FOR_VAL;
			} else if (temp == ';') {
				/* End of Bit-Value Pair, reset and look for New Bit Position */
				sigma_dut_print(dut, DUT_MSG_INFO,
						"%s - LOC_LOOKING_FOR_BIT - processing: %c",
						__func__, temp);
				rmBitFlags[idx][0] = bitPos;
				/* rmBitFlags[idx][1] = bitVal; */
				bitPos = 0;
				bitVal = 0;
				idx++;
			} else { /* Ignore */
				sigma_dut_print(dut, DUT_MSG_INFO,
						"%s - LOC_LOOKING_FOR_BIT - ignoring: %c",
						__func__, temp);
			}
			break;
		case LOC_LOOKING_FOR_VAL:
			if (temp == '0' || temp == '1') {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"%s - LOC_LOOKING_FOR_VAL - processing: %c",
						__func__, temp);
				bitVal = temp - '0';
				rmBitFlags[idx][1] = bitVal;
			} else if (temp == ';') {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"%s - LOC_LOOKING_FOR_VAL - processing: %c, bitPos: %u, bitVal: %u",
						__func__, temp, bitPos, bitVal);
				/* rmBitFlags[idx][0] = bitPos; */
				/* rmBitFlags[idx][1] = bitVal; */
				bitPos = 0;
				bitVal = 0;
				idx++;
				rmParseStates = LOC_LOOKING_FOR_BIT;
			} else { /* Ignore */
				sigma_dut_print(dut, DUT_MSG_INFO,
						"%s - LOC_LOOKING_FOR_VAL - ignoring: %c",
						__func__, temp);
			}
			break;
		default: /* Ignore */
			sigma_dut_print(dut, DUT_MSG_INFO,
					"%s - default - ignoring: %c",
					__func__, temp);
			break;
		}
	}

	for (i = 0; i < LOC_MAX_RM_FLAGS; i++) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s - Bit Pos: %u : Bit Val: %u",
				__func__, rmBitFlags[i][0],
				rmBitFlags[i][1]);
	}
}


int loc_cmd_sta_preset_testparameters(struct sigma_dut *dut,
				      struct sigma_conn *conn,
				      struct sigma_cmd *cmd)
{
	const char *rmFTMRFlagStr = get_param(cmd, "RMEnabledCapBitmap");
	const char *interworkingEn = get_param(cmd, "Interworking");
	unsigned int rmFTMRFlag = 0;
	unsigned int i, interworking = 0;
	char rmBitFlags[LOC_MAX_RM_FLAGS][LOC_RM_FLAG_VAL_ARRAY];

	sigma_dut_print(dut, DUT_MSG_INFO, "%s", __func__);

	memset(rmBitFlags, 0, sizeof(rmBitFlags));

	sigma_dut_print(dut, DUT_MSG_INFO, "%s - 1", __func__);
	/*
	 * This function is used to configure the RM capability bits and
	 * the Interworking bit only.
	 * If these parameters are not present just returning COMPLETE
	 * because all other parameters are ignored.
	 */
	if (!rmFTMRFlagStr && !interworkingEn) {
		sigma_dut_print(dut, DUT_MSG_INFO, "%s - 2", __func__);
		sigma_dut_print(dut, DUT_MSG_ERROR, "%s - Did not get %s",
				__func__, "RMEnabledCapBitmap");
		send_resp(dut, conn, SIGMA_COMPLETE, NULL);
		return 0;
	}

	if (rmFTMRFlagStr) {
		rmFTMRFlag = 25; /* Default invalid */
		sigma_dut_print(dut, DUT_MSG_INFO, "%s - rmFTMRFlagStr: %s",
				__func__, rmFTMRFlagStr);
		parse_rm_bits(dut, rmFTMRFlagStr, rmBitFlags);
		for (i = 0; i < LOC_MAX_RM_FLAGS; i++) {
			if (rmBitFlags[i][0] == 34)
				rmFTMRFlag = rmBitFlags[i][1];
		}
		sigma_dut_print(dut, DUT_MSG_INFO, "%s - rmFTMRFlag %u",
				__func__, rmFTMRFlag);
		if (rmFTMRFlag == 0) { /* Disable RM - FTMRR capability */
			sigma_dut_print(dut, DUT_MSG_INFO,
					"%s - Disabling RM - FTMRR",
					__func__);
			if (wpa_command(get_station_ifname(dut),
					WPA_RM_DISABLE) < 0) {
				send_resp(dut, conn, SIGMA_ERROR, NULL);
				return -1;
			}
		} else if (rmFTMRFlag == 1) { /* Enable RM - FTMRR capability */
			sigma_dut_print(dut, DUT_MSG_INFO,
					"%s - Enabling RM - FTMRR",
					__func__);
			if (wpa_command(get_station_ifname(dut),
					WPA_RM_ENABLE) < 0) {
				send_resp(dut, conn, SIGMA_ERROR, NULL);
				return 0;
			}
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s - No Setting for - FTMRR",
					__func__);
		}
		sigma_dut_print(dut, DUT_MSG_INFO,
				"%s - Succeeded in Enabling/Disabling RM Capability for FTMRR",
				__func__);
	}

	if (interworkingEn) {
		sscanf(interworkingEn, "%u", &interworking);
		sigma_dut_print(dut, DUT_MSG_INFO, "%s - interworking: %u",
				__func__, interworking);
		if (interworking)
			wpa_command(get_station_ifname(dut),
				    WPA_INTERWORKING_ENABLE);
		else
			wpa_command(get_station_ifname(dut),
				    WPA_INTERWORKING_DISABLE);
	}

	send_resp(dut, conn, SIGMA_COMPLETE, NULL);
	return 0;
}


int lowi_cmd_sta_reset_default(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
#ifdef ANDROID_WIFI_HAL
	if (wifi_hal_initialize(dut)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s - wifihal init failed for - LOC",
				__func__);
		return -1;
	}
#endif /* ANDROID_WIFI_HAL */

	return 0;
}
