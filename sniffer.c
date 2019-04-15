/*
 * Sigma Control API DUT (sniffer)
 * Copyright (c) 2013-2014, Qualcomm Atheros, Inc.
 * Copyright (c) 2019, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>


static void capture_process(const char *ifname, const char *filename)
{
	char *env[] = { NULL };
	char *argv[] = { "sigma_dut[capture]", "-i", strdup(ifname),
			 "-w", strdup(filename), NULL };
	execve("/usr/bin/dumpcap", argv, env);
	perror("execve");
	exit(EXIT_FAILURE);
}


static enum sigma_cmd_result cmd_sniffer_control_start(struct sigma_dut *dut,
						       struct sigma_conn *conn,
						       struct sigma_cmd *cmd)
{
	const char *filename = get_param(cmd, "filename");
	enum sigma_cmd_result res;
	pid_t pid;

	if (dut->sniffer_pid) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Sniffer was already capturing - restart based on new parameters");
		sniffer_close(dut);
	}

	if (filename == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing filename argument");
		return STATUS_SENT;
	}
	if (strchr(filename, '/')) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Invalid filename");
		return STATUS_SENT;
	}

	res = cmd_wlantest_set_channel(dut, conn, cmd);
	if (res != SUCCESS_SEND_STATUS)
		return res;

	mkdir("Captures", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

	sigma_dut_print(dut, DUT_MSG_INFO, "Starting sniffer process");
	snprintf(dut->sniffer_filename, sizeof(dut->sniffer_filename),
		 "Captures/%s", filename);
	pid = fork();
	if (pid < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to fork sniffer process");
		return STATUS_SENT;
	}

	if (pid == 0) {
		capture_process(dut->sniffer_ifname, dut->sniffer_filename);
		return SUCCESS_SEND_STATUS;
	}

	dut->sniffer_pid = pid;

	return SUCCESS_SEND_STATUS;
}


void sniffer_close(struct sigma_dut *dut)
{
	if (!dut->sniffer_pid)
		return;

	if (kill(dut->sniffer_pid, SIGTERM) < 0) {
		printf("Failed to kill sniffer process: %s\n", strerror(errno));
		dut->sniffer_pid = 0;
		return;
	}
	waitpid(dut->sniffer_pid, NULL, 0);

	if (dut->sniffer_filename[0]) {
		chmod(dut->sniffer_filename,
		      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		dut->sniffer_filename[0] = '\0';
	}

	dut->sniffer_pid = 0;
}


static enum sigma_cmd_result cmd_sniffer_control_stop(struct sigma_dut *dut,
						      struct sigma_conn *conn,
						      struct sigma_cmd *cmd)
{
	if (!dut->sniffer_pid) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Sniffer was not capturing");
		return STATUS_SENT;
	}

	sniffer_close(dut);
	return SUCCESS_SEND_STATUS;
}


static enum sigma_cmd_result
cmd_sniffer_control_field_check(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *filename = get_param(cmd, "filename");
	const char *framename = get_param(cmd, "framename");
	const char *srcmac = get_param(cmd, "srcmac");
	const char *wsc_state = get_param(cmd, "WSC_State");
	const char *pvb_bit = get_param(cmd, "pvb_bit");
	const char *moredata_bit = get_param(cmd, "MoreData_bit");
	const char *eosp_bit = get_param(cmd, "EOSP_bit");
	char buf[2000], *pos;
	FILE *f;

	if (filename == NULL || srcmac == NULL)
		return INVALID_SEND_STATUS;

	if (strchr(filename, '/')) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Invalid filename");
		return STATUS_SENT;
	}

	if (!file_exists("sniffer-control-field-check.py")) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,sniffer-control-field-check.py not found");
		return STATUS_SENT;
	}

	snprintf(buf, sizeof(buf),
		 "./sniffer-control-field-check.py FileName=Captures/%s SrcMac=%s%s%s%s%s%s%s%s%s%s%s",
		 filename, srcmac,
		 framename ? " FrameName=" : "", framename ? framename : "",
		 wsc_state ? " WSC_State=" : "", wsc_state ? wsc_state : "",
		 moredata_bit ? " MoreData_bit=" : "",
		 moredata_bit ? moredata_bit : "",
		 eosp_bit ? " EOSP_bit=" : "", eosp_bit ? eosp_bit : "",
		 pvb_bit ? " pvb_bit=" : "", pvb_bit ? pvb_bit : "");
	sigma_dut_print(dut, DUT_MSG_INFO, "Run: %s", buf);
	f = popen(buf, "r");
	if (f == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to run sniffer helper");
		return STATUS_SENT;
	}

	if (!fgets(buf, sizeof(buf), f)) {
		pclose(f);
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed extract response from sniffer helper");
		return STATUS_SENT;
	}
	pos = strchr(buf, '\n');
	if (pos)
		*pos = '\0';

	pclose(f);

	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return STATUS_SENT;
}


static enum sigma_cmd_result cmd_sniffer_get_info(struct sigma_dut *dut,
						  struct sigma_conn *conn,
						  struct sigma_cmd *cmd)
{
	char buf[200];

	snprintf(buf, sizeof(buf), "WfaSnifferVersion,SigmaSniffer-foo,SnifferSTA,foo,DeviceSwInfo,foo,WiresharkVersion,foo");
	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return STATUS_SENT;
}


static enum sigma_cmd_result
cmd_sniffer_control_filter_capture(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	const char *infile = get_param(cmd, "InFile");
	const char *outfile = get_param(cmd, "OutFile");
	const char *srcmac = get_param(cmd, "SrcMac");
	const char *framename = get_param(cmd, "FrameName");
	const char *nframes = get_param(cmd, "Nframes");
	const char *hasfield = get_param(cmd, "HasField");
	const char *datalen = get_param(cmd, "Datalen");
	char buf[500], *pos;
	FILE *f;

	if (infile == NULL || outfile == NULL || srcmac == NULL ||
	    nframes == NULL)
		return INVALID_SEND_STATUS;

	if (strchr(infile, '/') || strchr(outfile, '/'))
		return INVALID_SEND_STATUS;

	if (!file_exists("sniffer-control-filter-capture.py")) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,sniffer-control-filter-capture.py not found");
		return STATUS_SENT;
	}

	snprintf(buf, sizeof(buf),
		 "./sniffer-control-filter-capture.py InFile=Captures/%s OutFile=Captures/%s SrcMac=%s%s%s Nframes=%s%s%s%s%s",
		 infile, outfile, srcmac,
		 framename ? " FrameName=" : "", framename ? framename : "",
		 nframes,
		 hasfield ? " HasField=" : "", hasfield ? hasfield : "",
		 datalen ? " Datalen=" : "", datalen ? datalen : "");
	sigma_dut_print(dut, DUT_MSG_INFO, "Run: %s", buf);
	f = popen(buf, "r");
	if (f == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to run sniffer helper");
		return STATUS_SENT;
	}

	if (!fgets(buf, sizeof(buf), f)) {
		pclose(f);
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed extract response from sniffer helper");
		return STATUS_SENT;
	}
	pos = strchr(buf, '\n');
	if (pos)
		*pos = '\0';

	pclose(f);

	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return STATUS_SENT;
}


static enum sigma_cmd_result
cmd_sniffer_get_field_value(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	const char *infile = get_param(cmd, "FileName");
	const char *srcmac = get_param(cmd, "SrcMac");
	const char *framename = get_param(cmd, "FrameName");
	const char *fieldname = get_param(cmd, "FieldName");
	char buf[500], *pos;
	FILE *f;

	if (infile == NULL || srcmac == NULL || framename == NULL ||
	    fieldname == NULL)
		return INVALID_SEND_STATUS;

	if (!file_exists("sniffer-get-field-value.py")) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,sniffer-get-field-value.py not found");
		return STATUS_SENT;
	}

	snprintf(buf, sizeof(buf),
		 "./sniffer-get-field-value.py FileName=Captures/%s SrcMac=%s FrameName=%s FieldName=%s",
		 infile, srcmac, framename, fieldname);
	sigma_dut_print(dut, DUT_MSG_INFO, "Run: %s", buf);
	f = popen(buf, "r");
	if (f == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to run sniffer helper");
		return STATUS_SENT;
	}

	if (!fgets(buf, sizeof(buf), f)) {
		pclose(f);
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed extract response from sniffer helper");
		return STATUS_SENT;
	}
	pos = strchr(buf, '\n');
	if (pos)
		*pos = '\0';

	pclose(f);

	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return STATUS_SENT;
}


static enum sigma_cmd_result
cmd_sniffer_check_p2p_noa_duration(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	FILE *f;
	char buf[200], *pos;
	const char *infile = get_param(cmd, "FileName");
	const char *bssid = get_param(cmd, "bssid");
	const char *srcmac = get_param(cmd, "srcmac");
	const char *destmac = get_param(cmd, "destmac");

	if (infile == NULL || bssid == NULL || srcmac == NULL ||
	    destmac == NULL)
		return INVALID_SEND_STATUS;

	if (!file_exists("sniffer-check-p2p-noa-duration.py")) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,sniffer-check-p2p-noa-duration.py not found");
		return STATUS_SENT;
	}

	snprintf(buf, sizeof(buf),
		 "./sniffer-check-p2p-noa-duration.py Captures/%s %s %s %s",
		 infile, bssid, srcmac, destmac);
	sigma_dut_print(dut, DUT_MSG_INFO, "Run: %s", buf);
	f = popen(buf, "r");
	if (f == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to run sniffer check");
		return STATUS_SENT;
	}

	if (!fgets(buf, sizeof(buf), f)) {
		pclose(f);
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed extract response from sniffer check");
		return STATUS_SENT;
	}
	pos = strchr(buf, '\n');
	if (pos)
		*pos = '\0';

	pclose(f);

	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return STATUS_SENT;
}


static enum sigma_cmd_result
cmd_sniffer_check_p2p_opps_client(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	char buf[200];

	/* TODO */
	snprintf(buf, sizeof(buf), "FilterStatus,SUCCESS");
	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return STATUS_SENT;
}


static enum sigma_cmd_result
cmd_sniffer_check_frame_field(struct sigma_dut *dut,
			      struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	char buf[200];

	/* TODO */
	snprintf(buf, sizeof(buf), "FilterStatus,SUCCESS");
	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return STATUS_SENT;
}


void sniffer_register_cmds(void)
{
	sigma_dut_reg_cmd("sniffer_control_start", NULL,
			  cmd_sniffer_control_start);
	sigma_dut_reg_cmd("sniffer_control_stop", NULL,
			  cmd_sniffer_control_stop);
	sigma_dut_reg_cmd("sniffer_control_field_check", NULL,
			  cmd_sniffer_control_field_check);
	sigma_dut_reg_cmd("sniffer_get_info", NULL, cmd_sniffer_get_info);
	sigma_dut_reg_cmd("sniffer_control_filter_capture", NULL,
			  cmd_sniffer_control_filter_capture);
	sigma_dut_reg_cmd("wfa_sniffer_control_filter_capture", NULL,
			  cmd_sniffer_control_filter_capture);
	sigma_dut_reg_cmd("sniffer_get_field_value", NULL,
			  cmd_sniffer_get_field_value);
	sigma_dut_reg_cmd("sniffer_check_p2p_NoA_duration", NULL,
			  cmd_sniffer_check_p2p_noa_duration);
	sigma_dut_reg_cmd("sniffer_check_p2p_opps_client", NULL,
			  cmd_sniffer_check_p2p_opps_client);
	sigma_dut_reg_cmd("sniffer_check_frame_field", NULL,
			  cmd_sniffer_check_frame_field);
}
