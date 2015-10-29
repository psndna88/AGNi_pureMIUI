/*
 * Sigma Control API DUT (server)
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"


static int cmd_server_reset_default(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	const char *var;

	var = get_param(cmd, "Program");
	if (var == NULL || strcasecmp(var, "HS2-R2") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported program");
		return 0;
	}

	var = get_param(cmd, "UserName");
	if (var) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Reset user %s", var);
		/* TODO */
	}

	var = get_param(cmd, "SerialNo");
	if (var) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Reset serial number %s",
				var);
		/* TODO */
	}

	return 1;
}


static int cmd_server_request_status(struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct sigma_cmd *cmd)
{
	const char *var, *username, *serialno, *imsi, *addr, *status;
	int osu, timeout;
	char resp[500];

	var = get_param(cmd, "Program");
	if (var == NULL || strcasecmp(var, "HS2-R2") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported program");
		return 0;
	}

	var = get_param(cmd, "Device");
	if (!var ||
	    (strcasecmp(var, "AAAServer") != 0 &&
	     strcasecmp(var, "OSUServer") != 0)) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported device type");
		return 0;
	}
	osu = strcasecmp(var, "OSUServer") == 0;

	var = get_param(cmd, "Timeout");
	if (!var) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing timeout");
		return 0;
	}
	timeout = atoi(var);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "timeout: %d", timeout);

	username = get_param(cmd, "UserName");
	if (username)
		sigma_dut_print(dut, DUT_MSG_DEBUG, "UserName: %s", username);
	serialno = get_param(cmd, "SerialNo");
	if (serialno)
		sigma_dut_print(dut, DUT_MSG_DEBUG, "SerialNo: %s", serialno);
	imsi = get_param(cmd, "imsi_val");
	if (imsi)
		sigma_dut_print(dut, DUT_MSG_DEBUG, "imsi_val: %s", imsi);
	addr = get_param(cmd, "ClientMACAddr");
	if (addr)
		sigma_dut_print(dut, DUT_MSG_DEBUG, "ClientMACAddr: %s", addr);
	status = get_param(cmd, "Status");
	if (status)
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Status: %s", status);

	if (osu && status && strcasecmp(status, "Remediation") == 0) {
		/* TODO */
		sleep(1);
		snprintf(resp, sizeof(resp),
			 "RemediationStatus,Remediation Complete");
		send_resp(dut, conn, SIGMA_COMPLETE, resp);
		return 0;
	}

	return 1;
}


void server_register_cmds(void)
{
	sigma_dut_reg_cmd("server_reset_default", NULL,
			  cmd_server_reset_default);
	sigma_dut_reg_cmd("server_request_status", NULL,
			  cmd_server_request_status);
}
