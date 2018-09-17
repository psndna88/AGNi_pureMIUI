/*
 * Sigma Control API DUT (station/AP/sniffer)
 * Copyright (c) 2011-2013, 2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include "miracast.h"
#include <sys/wait.h>
#include "wpa_ctrl.h"
#include "wpa_helpers.h"


static int cmd_dev_send_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
#ifdef MIRACAST
	const char *program = get_param(cmd, "Program");

	if (program && (strcasecmp(program, "WFD") == 0 ||
			strcasecmp(program, "DisplayR2") == 0))
		return miracast_dev_send_frame(dut, conn, cmd);
#endif /* MIRACAST */

	if (dut->mode == SIGMA_MODE_STATION ||
	    dut->mode == SIGMA_MODE_UNKNOWN) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Convert "
				"dev_send_frame to sta_send_frame");
		return cmd_sta_send_frame(dut, conn, cmd);
	}

	if (dut->mode == SIGMA_MODE_AP) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Convert "
				"dev_send_frame to ap_send_frame");
		return cmd_ap_send_frame(dut, conn, cmd);
	}

#ifdef CONFIG_WLANTEST
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Convert dev_send_frame to "
			"wlantest_send_frame");
	return cmd_wlantest_send_frame(dut, conn, cmd);
#else /* CONFIG_WLANTEST */
	send_resp(dut, conn, SIGMA_ERROR,
		  "errorCode,Unsupported dev_send_frame");
	return 0;
#endif /* CONFIG_WLANTEST */
}


static int cmd_dev_set_parameter(struct sigma_dut *dut, struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *device = get_param(cmd, "Device");

	if (device && strcasecmp(device, "STA") == 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Convert "
				"dev_set_parameter to sta_set_parameter");
		return cmd_sta_set_parameter(dut, conn, cmd);
	}

	return -1;
}


static int cmd_dev_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *program = get_param(cmd, "Program");

#ifdef MIRACAST
	if (program && (strcasecmp(program, "WFD") == 0 ||
			strcasecmp(program, "DisplayR2") == 0)) {
		if (get_param(cmd, "interface") == NULL)
			return -1;
		return miracast_dev_exec_action(dut, conn, cmd);
	}
#endif /* MIRACAST */

	if (program && strcasecmp(program, "DPP") == 0)
		return dpp_dev_exec_action(dut, conn, cmd);

	return -2;
}


static int cmd_dev_configure_ie(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *ie_name = get_param(cmd, "IE_Name");
	const char *contents = get_param(cmd, "Contents");

	if (!ie_name || !contents)
		return -1;

	if (strcasecmp(ie_name, "RSNE") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported IE_Name value");
		return 0;
	}

	free(dut->rsne_override);
	dut->rsne_override = strdup(contents);

	return dut->rsne_override ? 1 : -1;
}


static int cmd_dev_ble_action(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
#ifdef ANDROID
	char buf[200];
	const char *ble_op = get_param(cmd, "BLEOp");
	const char *prog = get_param(cmd, "Prog");
	const char *service_name = get_param(cmd, "ServiceName");
	const char *ble_role = get_param(cmd, "BLERole");
	const char *discovery_type = get_param(cmd, "DiscoveryType");
	const char *msg_type = get_param(cmd, "messagetype");
	const char *action = get_param(cmd, "action");
	const char *M2Transmit = get_param(cmd, "M2Transmit");
	char *argv[17];
	pid_t pid;

	if (prog && ble_role && action && msg_type) {
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "OrgID,0x00,TransDataHeader,0x00,BloomFilterElement,NULL");
		return 0;
	}
	if (!ble_op || !prog || !service_name || !ble_role || !discovery_type) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Invalid arguments");
		return -1;
	}

	if ((strcasecmp(prog, "NAN") != 0)) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Program %s not supported",
				prog);
		return -1;
	}

	if (strcasecmp(ble_role, "seeker") != 0 &&
	    strcasecmp(ble_role, "provider") != 0 &&
	    strcasecmp(ble_role, "browser") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Invalid BLERole: %s",
				ble_role);
		return -1;
	}

	if (strcasecmp(discovery_type, "active") != 0 &&
	    strcasecmp(discovery_type, "passive") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Invalid DiscoveryType: %s",
				discovery_type);
		return -1;
	}

	if (!M2Transmit)
		M2Transmit = "disable";

	argv[0] = "am";
	argv[1] = "start";
	argv[2] = "-n";
	argv[3] = "org.codeaurora.nanservicediscovery/org.codeaurora.nanservicediscovery.MainActivity";
	argv[4] = "--es";
	argv[5] = "service";
	argv[6] = (char *) service_name;
	argv[7] = "--es";
	argv[8] = "role";
	argv[9] = (char *) ble_role;
	argv[10] = "--es";
	argv[11] = "scantype";
	argv[12] = (char *) discovery_type;
	argv[13] = "--es";
	argv[14] = "M2Transmit";
	argv[15] = (char *) M2Transmit;
	argv[16] = NULL;

	pid = fork();
	if (pid == -1) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "fork: %s",
				strerror(errno));
		return -1;
	}

	if (pid == 0) {
		execv("/system/bin/am", argv);
		sigma_dut_print(dut, DUT_MSG_ERROR, "execv: %s",
				strerror(errno));
		exit(0);
		return -1;
	}

	dut->nanservicediscoveryinprogress = 1;
#endif /* ANDROID */

	return 1;
}



static int cmd_dev_start_test(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	return 1;
}


static int cmd_dev_stop_test(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	return 1;
}


static int cmd_dev_get_log(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	return 1;
}


static int req_intf(struct sigma_cmd *cmd)
{
	return get_param(cmd, "interface") == NULL ? -1 : 0;
}


static int req_role_svcname(struct sigma_cmd *cmd)
{
	if (!get_param(cmd, "BLERole"))
		 return -1;
	if (get_param(cmd, "BLEOp") && !get_param(cmd, "ServiceName"))
		return -1;
	return 0;
}


static int req_intf_prog(struct sigma_cmd *cmd)
{
	if (get_param(cmd, "interface") == NULL)
		return -1;
	if (get_param(cmd, "program") == NULL)
		return -1;
	return 0;
}


static int req_prog(struct sigma_cmd *cmd)
{
	if (get_param(cmd, "program") == NULL)
		return -1;
	return 0;
}


void dev_register_cmds(void)
{
	sigma_dut_reg_cmd("dev_send_frame", req_intf_prog, cmd_dev_send_frame);
	sigma_dut_reg_cmd("dev_set_parameter", req_intf_prog,
			  cmd_dev_set_parameter);
	sigma_dut_reg_cmd("dev_exec_action", req_prog,
			  cmd_dev_exec_action);
	sigma_dut_reg_cmd("dev_configure_ie", req_intf, cmd_dev_configure_ie);
	sigma_dut_reg_cmd("dev_start_test", NULL, cmd_dev_start_test);
	sigma_dut_reg_cmd("dev_stop_test", NULL, cmd_dev_stop_test);
	sigma_dut_reg_cmd("dev_get_log", NULL, cmd_dev_get_log);
	sigma_dut_reg_cmd("dev_ble_action", req_role_svcname,
			  cmd_dev_ble_action);
}
